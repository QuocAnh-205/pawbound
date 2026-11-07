// Phase 13 — Thanh toán MOCK (không có tiền thật): order 100.000 VND, webhook ký HMAC, idempotency, threat cases, ví & cosmetic.
import { createHmac } from 'node:crypto';
import { Harness, startHarness } from './harness';

let h: Harness;
beforeAll(async () => {
  h = await startHarness('pay', { THROTTLE_REGISTER_PER_MIN: '1000', THROTTLE_DEFAULT_PER_MIN: '100000', THROTTLE_ORDER_PER_MIN: '1000', PAWBOUND_GUILD_FEE: 'on' });
}, 120_000);
afterAll(async () => h?.close());

interface U { id: string; numericId: string; auth: { Authorization: string } }
let seq = 0;
async function user(): Promise<U> {
  const n = `Pay${++seq}x${Date.now() % 10000}`;
  const r = await h.http.post('/api/v1/auth/register').send({ email: `${n.toLowerCase()}@test.vn`, password: 'mat-khau-dai-123', displayName: n });
  const auth = { Authorization: `Bearer ${r.body.accessToken}` };
  const me = await h.http.get('/api/v1/me').set(auth);
  const row = await h.db.user.findUniqueOrThrow({ where: { id: me.body.id } });
  return { id: me.body.id, numericId: row.numericId.toString(), auth };
}
const post = (u: U, path: string, body: object = {}) => h.http.post(`/api/v1${path}`).set(u.auth).send(body);
const get = (u: U, path: string) => h.http.get(`/api/v1${path}`).set(u.auth);
const secret = () => process.env.MOCK_PAYMENT_WEBHOOK_SECRET!;

// Webhook "từ provider" do test tự dựng (giả lập provider thật, hoặc kẻ tấn công nếu ký sai)
function webhook(body: object, opts: { key?: string; t?: number } = {}) {
  const raw = JSON.stringify(body);
  const t = opts.t ?? Math.floor(Date.now() / 1000);
  const sig = createHmac('sha256', opts.key ?? secret()).update(`${t}.${raw}`).digest('hex');
  return h.http.post('/api/v1/payments/webhooks/mock').set('Content-Type', 'application/json').set('X-Mock-Signature', `t=${t},v1=${sig}`).send(raw);
}
async function newOrder(u: U, key = `k-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`) {
  const r = await post(u, '/payments/orders', { purpose: 'GUILD_CREATION', idempotencyKey: key });
  expect(r.status).toBe(201);
  const o = await h.db.paymentOrder.findUniqueOrThrow({ where: { id: r.body.id } });
  return { ...r.body, providerRef: o.providerRef as string };
}
const paidEvt = (o: { id: string; providerRef: string }, over: Record<string, unknown> = {}, id = `evt_${Math.random().toString(36).slice(2)}`) =>
  ({ id, type: 'payment.succeeded', created: Math.floor(Date.now() / 1000), data: { reference: o.id, providerRef: o.providerRef, amount: 100000, currency: 'VND', ...over } });

describe('luồng mock 100.000 VND -> lập bang', () => {
  it('chưa trả -> 402; tạo order (số tiền do server đặt), idempotent; thanh toán mock; lập bang tiêu entitlement', async () => {
    const u = await user();
    const g0 = await post(u, '/guilds', { name: `Bang ${u.numericId}` });
    expect(g0.status).toBe(402);
    expect(g0.body.amount).toBe(100000);
    // Client cố gửi số tiền/tiền tệ/trạng thái -> bị chặn (field lạ)
    expect((await post(u, '/payments/orders', { purpose: 'GUILD_CREATION', idempotencyKey: 'abcdefgh1', amount: 1 })).status).toBe(400);
    expect((await post(u, '/payments/orders', { purpose: 'GUILD_CREATION', idempotencyKey: 'abcdefgh1', status: 'PAID' })).status).toBe(400);
    const o = await newOrder(u, 'same-key-001');
    expect(o).toMatchObject({ amount: 100000, currency: 'VND', status: 'PENDING', purpose: 'GUILD_CREATION' });
    expect(o.checkoutUrl).toContain('/payments/mock/checkout/');
    expect((await newOrder(u, 'same-key-001')).id).toBe(o.id);  // bấm 2 lần -> 1 order
    // Trang return / query string không làm đơn thành PAID
    expect((await get(u, `/payments/return?orderId=${o.id}&status=PAID&vnp_ResponseCode=00`)).body.status).toBe('PENDING');
    // Không có endpoint nào cho client sửa trạng thái
    expect((await h.http.patch(`/api/v1/payments/orders/${o.id}`).set(u.auth).send({ status: 'PAID' })).status).toBe(404);
    expect((await h.http.put(`/api/v1/payments/orders/${o.id}`).set(u.auth).send({ status: 'PAID' })).status).toBe(404);
    // Người khác không xem/hoàn tất được order của u
    const other = await user();
    expect((await get(other, `/payments/orders/${o.id}`)).status).toBe(404);
    expect((await post(other, `/payments/mock/checkout/${o.providerRef}/complete`, { outcome: 'success' })).status).toBe(404);
    // Thanh toán tại trang mock -> webhook ký -> PAID + entitlement
    const done = await post(u, `/payments/mock/checkout/${o.providerRef}/complete`, { outcome: 'success' });
    expect(done.status).toBe(200);
    expect(done.body.outcome).toBe('APPLIED');
    const after = await get(u, `/payments/orders/${o.id}`);
    expect(after.body.status).toBe('PAID');
    expect(after.body.entitlement.status).toBe('AVAILABLE');
    // Lập bang: tên trùng -> lỗi, entitlement CÒN NGUYÊN; tên mới -> thành công, entitlement CONSUMED
    const rival = await user();
    const ro = await newOrder(rival);
    await post(rival, `/payments/mock/checkout/${ro.providerRef}/complete`, { outcome: 'success' });
    expect((await post(rival, '/guilds', { name: `Trung ${u.numericId}` })).status).toBe(201);
    expect((await post(u, '/guilds', { name: `Trung ${u.numericId}` })).status).toBe(409);
    expect((await get(u, `/payments/orders/${o.id}`)).body.entitlement.status).toBe('AVAILABLE');
    const g = await post(u, '/guilds', { name: `Bang ${u.numericId}` });
    expect(g.status).toBe(201);
    const ent = (await get(u, `/payments/orders/${o.id}`)).body.entitlement;
    expect(ent).toMatchObject({ status: 'CONSUMED', guildId: g.body.id });
    // Rời bang rồi muốn lập bang mới -> phải trả phí lần nữa
    await post(u, `/guilds/${g.body.id}/members/${rival.id}/transfer`);  // rival không ở bang này -> 404, bỏ qua
    expect((await h.http.delete(`/api/v1/guilds/${g.body.id}`).set(u.auth)).status).toBe(200);
    expect((await post(u, '/guilds', { name: `Lan Hai ${u.numericId}` })).status).toBe(402);
  });

  it('thanh toán thất bại / hủy -> không có entitlement', async () => {
    const u = await user();
    const o = await newOrder(u);
    expect((await post(u, `/payments/mock/checkout/${o.providerRef}/complete`, { outcome: 'fail' })).status).toBe(200);
    expect((await get(u, `/payments/orders/${o.id}`)).body).toMatchObject({ status: 'FAILED', entitlement: null });
    expect((await post(u, '/guilds', { name: `Khong ${u.numericId}` })).status).toBe(402);
  });
});

describe('threat cases webhook', () => {
  it('chữ ký giả / sai khóa / thiếu / quá hạn (replay) -> 401, đơn không đổi, vẫn ghi sổ', async () => {
    const u = await user();
    const o = await newOrder(u);
    const before = await h.db.paymentEvent.count({ where: { outcome: 'REJECTED_SIGNATURE' } });
    expect((await webhook(paidEvt(o), { key: 'khoa-cua-ke-tan-cong-dai-hon-32-ky-tu!!' })).status).toBe(401);
    expect((await webhook(paidEvt(o), { t: Math.floor(Date.now() / 1000) - 3600 })).status).toBe(401);
    expect((await h.http.post('/api/v1/payments/webhooks/mock').set('Content-Type', 'application/json').send(paidEvt(o))).status).toBe(401);
    // Body bị sửa sau khi ký (đổi số tiền) -> chữ ký không khớp
    const raw = JSON.stringify(paidEvt(o));
    const t = Math.floor(Date.now() / 1000);
    const sig = createHmac('sha256', secret()).update(`${t}.${raw}`).digest('hex');
    const tampered = raw.replace('100000', '1000');
    expect((await h.http.post('/api/v1/payments/webhooks/mock').set('Content-Type', 'application/json').set('X-Mock-Signature', `t=${t},v1=${sig}`).send(tampered)).status).toBe(401);
    expect((await h.http.post('/api/v1/payments/webhooks/khac').send({})).status).toBe(404);
    expect(await h.db.paymentEvent.count({ where: { outcome: 'REJECTED_SIGNATURE' } })).toBe(before + 4);
    expect((await get(u, `/payments/orders/${o.id}`)).body.status).toBe('PENDING');
  });

  it('sai số tiền / sai tiền tệ / sai providerRef / order lạ -> 422, không PAID, ghi sổ + audit', async () => {
    const u = await user();
    const o = await newOrder(u);
    expect((await webhook(paidEvt(o, { amount: 1000 }))).body.code).toBe('REJECTED_AMOUNT');
    expect((await webhook(paidEvt(o, { amount: 1000000 }))).body.code).toBe('REJECTED_AMOUNT');
    expect((await webhook(paidEvt(o, { currency: 'USD' }))).body.code).toBe('REJECTED_CURRENCY');
    expect((await webhook(paidEvt(o, { providerRef: 'mock_khac' }))).body.code).toBe('REJECTED_PROVIDER_REF');
    expect((await webhook(paidEvt(o, { reference: '00000000-0000-4000-8000-000000000000' }))).body.code).toBe('REJECTED_UNKNOWN_ORDER');
    expect((await webhook(paidEvt(o, { reference: "x' OR 1=1 --" }))).body.code).toBe('REJECTED_UNKNOWN_ORDER');
    expect((await get(u, `/payments/orders/${o.id}`)).body).toMatchObject({ status: 'PENDING', entitlement: null });
    const ev = await h.db.paymentEvent.findMany({ where: { orderId: o.id } });
    expect(ev.map((e) => e.outcome).sort()).toEqual(['REJECTED_AMOUNT', 'REJECTED_AMOUNT', 'REJECTED_CURRENCY', 'REJECTED_PROVIDER_REF']);
    expect(await h.db.auditLog.count({ where: { action: 'payment.webhook_rejected', target: o.id } })).toBe(4);
  });

  it('webhook lặp (tuần tự và ĐỒNG THỜI) -> đúng một entitlement, một lần PAID', async () => {
    const u = await user();
    const o = await newOrder(u);
    const evt = paidEvt(o, {}, 'evt_dup_1');
    const first = await webhook(evt);
    expect(first.body.outcome).toBe('APPLIED');
    expect((await webhook(evt)).body.outcome).toBe('DUPLICATE');
    // Cùng đơn nhưng event id khác (provider gửi 2 sự kiện thành công) -> không tạo thêm
    expect((await webhook(paidEvt(o))).body.outcome).toBe('IGNORED_ALREADY_PAID');
    // 10 bản giống hệt nhau cùng lúc trên đơn mới
    const o2 = await newOrder(u, `k2-${Date.now()}`).catch(() => null);
    expect(o2).toBeNull();  // đã có entitlement chưa dùng -> không cho mua thêm (409)
    const v = await user();
    const o3 = await newOrder(v);
    const e3 = paidEvt(o3, {}, 'evt_burst');
    const rs = await Promise.all(Array.from({ length: 10 }, () => webhook(e3)));
    expect(rs.every((r) => r.status === 200)).toBe(true);
    expect(rs.filter((r) => r.body.outcome === 'APPLIED')).toHaveLength(1);
    expect(await h.db.entitlement.count({ where: { userId: v.id } })).toBe(1);
    expect(await h.db.paymentEvent.count({ where: { providerEventId: 'evt_burst' } })).toBe(1);
    // Lập bang 2 lần song song với 1 entitlement -> chỉ 1 bang
    const gs = await Promise.all([post(v, '/guilds', { name: `Song A ${v.numericId}` }), post(v, '/guilds', { name: `Song B ${v.numericId}` })]);
    expect(gs.filter((r) => r.status === 201)).toHaveLength(1);
    expect(await h.db.guild.count({ where: { ownerId: v.id, disbandedAt: null } })).toBe(1);
  });

  it('webhook đến muộn sau khi đơn hết hạn -> vẫn ghi nhận PAID (tiền đã trừ của người chơi), đánh dấu muộn', async () => {
    const u = await user();
    const o = await newOrder(u);
    await h.db.paymentOrder.update({ where: { id: o.id }, data: { expiresAt: new Date(Date.now() - 1000) } });
    expect((await get(u, `/payments/orders/${o.id}`)).body.status).toBe('EXPIRED');
    const r = await webhook(paidEvt(o));
    expect(r.body.outcome).toBe('APPLIED_LATE');
    expect((await get(u, `/payments/orders/${o.id}`)).body).toMatchObject({ status: 'PAID', entitlement: { status: 'AVAILABLE' } });
    // FAILED đến sau PAID không làm mất trạng thái
    expect((await webhook({ ...paidEvt(o), type: 'payment.failed' })).body.outcome).toBe('IGNORED_TRANSITION');
  });

  it('sổ cái bất biến: UPDATE/DELETE PaymentEvent bị DB từ chối', async () => {
    await expect(h.db.$executeRawUnsafe(`UPDATE "PaymentEvent" SET outcome = 'APPLIED'`)).rejects.toThrow(/append-only/);
    await expect(h.db.$executeRawUnsafe(`DELETE FROM "PaymentEvent"`)).rejects.toThrow(/append-only/);
  });
});

describe('admin: hoàn tiền, báo cáo', () => {
  it('hoàn tiền thu hồi entitlement chưa dùng; giữ bang đã lập (để xem xét); người thường không gọi được', async () => {
    const adm = await user();
    await h.db.user.update({ where: { id: adm.id }, data: { role: 'ADMIN' } });
    const row = await h.db.user.findUniqueOrThrow({ where: { id: adm.id } });
    const login = await h.http.post('/api/v1/auth/login').send({ email: row.email, password: 'mat-khau-dai-123' });
    const A = { Authorization: `Bearer ${login.body.accessToken}` };
    const u = await user();
    const o = await newOrder(u);
    await post(u, `/payments/mock/checkout/${o.providerRef}/complete`, { outcome: 'success' });
    expect((await post(u, `/admin/payments/${o.id}/refund`)).status).toBe(403);
    const r = await h.http.post(`/api/v1/admin/payments/${o.id}/refund`).set(A);
    expect(r.body.outcome).toBe('APPLIED');
    expect((await get(u, `/payments/orders/${o.id}`)).body).toMatchObject({ status: 'REFUNDED', entitlement: { status: 'REVOKED' } });
    expect((await post(u, '/guilds', { name: `Hoan ${u.numericId}` })).status).toBe(402);
    expect((await h.http.post(`/api/v1/admin/payments/${o.id}/refund`).set(A)).status).toBe(422);  // hoàn 2 lần
    // Đã lập bang rồi mới hoàn -> bang giữ nguyên, outcome báo cần xem xét
    const w = await user();
    const ow = await newOrder(w);
    await post(w, `/payments/mock/checkout/${ow.providerRef}/complete`, { outcome: 'success' });
    const gw = await post(w, '/guilds', { name: `Giu ${w.numericId}` });
    expect((await h.http.post(`/api/v1/admin/payments/${ow.id}/refund`).set(A)).body.outcome).toBe('APPLIED_REFUND_GUILD_KEPT');
    expect((await get(w, `/guilds/${gw.body.id}`)).status).toBe(200);
    const rep = await h.http.get('/api/v1/admin/payments/report').set(A);
    expect(rep.status).toBe(200);
    expect(rep.body.orders.some((x: { status: string }) => x.status === 'REFUNDED')).toBe(true);
    expect(rep.body.webhookOutcomes.some((x: { outcome: string }) => x.outcome === 'REJECTED_SIGNATURE')).toBe(true);
    expect(await h.db.auditLog.count({ where: { action: 'payment.refund_requested' } })).toBe(2);
  });
});

describe('ví trong game & cosmetic (không pay-to-win)', () => {
  it('chỉ game server cộng tiền (idempotent, có trần); mua cosmetic trừ đúng, không âm khi mua song song', async () => {
    const u = await user();
    const svc = { 'X-Service-Token': process.env.INTERNAL_SERVICE_TOKEN! };
    const earn = (amount: number, refKey: string) => h.http.post(`/api/v1/internal/wallet/${u.numericId}/earn`).set(svc).send({ amount, reason: 'forage', refKey });
    expect((await h.http.post(`/api/v1/internal/wallet/${u.numericId}/earn`).set(u.auth).send({ amount: 100, reason: 'forage', refKey: 'hack-0001' })).status).toBe(401);
    expect((await earn(400, 'srv:1:0001')).body).toEqual({ balance: 400, duplicate: false });
    expect((await earn(400, 'srv:1:0001')).body).toEqual({ balance: 400, duplicate: true });  // gửi lại
    expect((await earn(5000, 'srv:1:0002')).status).toBe(422);  // vượt trần
    const cat = await get(u, '/shop/cosmetics');
    expect(cat.body.every((c: Record<string, unknown>) => Object.keys(c).every((k) => ['id', 'name', 'price', 'coat_color'].includes(k)))).toBe(true);
    // 400 xương, 2 món 300 mua song song -> chỉ 1 thành công, số dư không âm
    const rs = await Promise.all([post(u, '/shop/cosmetics/coat_golden/buy', { idempotencyKey: 'buy-gold-1' }),
                                  post(u, '/shop/cosmetics/coat_midnight/buy', { idempotencyKey: 'buy-night-1' })]);
    expect(rs.filter((r) => r.status === 201)).toHaveLength(1);
    expect(rs.filter((r) => r.body.code === 'INSUFFICIENT_FUNDS')).toHaveLength(1);
    const wallet = await get(u, '/wallet');
    expect(wallet.body.balance).toBe(100);
    expect(wallet.body.cosmetics).toHaveLength(1);
    expect((await post(u, '/shop/cosmetics/khong_co/buy', { idempotencyKey: 'buy-none-1' })).status).toBe(404);
    const txs = await h.db.currencyTx.findMany({ where: { userId: u.id }, orderBy: { id: 'asc' } });
    expect(txs.map((t) => t.balanceAfter)).toEqual([400, 100]);
    await expect(h.db.$executeRawUnsafe(`UPDATE "CurrencyTx" SET delta = 999999`)).rejects.toThrow(/append-only/);
    await expect(h.db.$executeRawUnsafe(`UPDATE "Wallet" SET balance = -1`)).rejects.toThrow();
  });
});
