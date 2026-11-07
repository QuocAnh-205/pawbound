// Phase 14 — Backend: lãnh thổ (race), thưởng không nhân đôi, thông báo đồng đội khi bị bắt (chống spam, riêng tư).
import { Harness, startHarness } from './harness';

let h: Harness;
beforeAll(async () => {
  h = await startHarness('adv', { THROTTLE_REGISTER_PER_MIN: '1000', THROTTLE_DEFAULT_PER_MIN: '100000', PAWBOUND_GUILD_FEE: 'off' });
}, 120_000);
afterAll(async () => h?.close());

interface U { id: string; numericId: string; auth: { Authorization: string }; name: string }
let seq = 0;
async function user(): Promise<U> {
  const name = `Adv${++seq}x${Date.now() % 10000}`;
  const r = await h.http.post('/api/v1/auth/register').send({ email: `${name.toLowerCase()}@test.vn`, password: 'mat-khau-dai-123', displayName: name });
  const auth = { Authorization: `Bearer ${r.body.accessToken}` };
  const me = await h.http.get('/api/v1/me').set(auth);
  const row = await h.db.user.findUniqueOrThrow({ where: { id: me.body.id } });
  return { id: me.body.id, numericId: row.numericId.toString(), auth, name };
}
const svc = () => ({ 'X-Service-Token': process.env.INTERNAL_SERVICE_TOKEN! });
const post = (u: U, path: string, body: object = {}) => h.http.post(`/api/v1${path}`).set(u.auth).send(body);
const claim = (zoneId: number, guildId: string, members = 2) =>
  h.http.post('/api/v1/internal/territory/claim').set(svc()).send({ zoneId, guildId, serverId: 'dev-1', members });
async function guild(owner: U) {
  const g = await post(owner, '/guilds', { name: `G ${owner.name}` });
  expect(g.status).toBe(201);
  return g.body.id as string;
}

describe('lãnh thổ', () => {
  it('RACE: hai bang chiếm cùng khu đồng thời -> đúng một bang thắng; bảo hộ; gia hạn; giới hạn; điều kiện', async () => {
    const ga = await guild(await user()), gb = await guild(await user());
    const rs = await Promise.all([claim(7, ga), claim(7, gb), claim(7, ga), claim(7, gb)]);
    const ok = rs.filter((r) => r.status === 200 && r.body.renewed === false);
    expect(ok).toHaveLength(1);
    const winner = ok[0].body.guildId;
    expect(rs.filter((r) => r.status === 409).every((r) => r.body.code === 'TERRITORY_PROTECTED')).toBe(true);
    const t = await h.db.territory.findUniqueOrThrow({ where: { zoneId: 7 } });
    expect(t.guildId).toBe(winner);
    // Bên thắng gửi lại -> gia hạn (không tạo bản ghi/chiếm mới)
    expect((await claim(7, winner)).body.renewed).toBe(true);
    expect(await h.db.auditLog.count({ where: { action: 'territory.claim', target: '7' } })).toBe(1);
    // Hết bảo hộ -> bang kia chiếm lại được
    await h.db.territory.update({ where: { zoneId: 7 }, data: { protectedTill: new Date(Date.now() - 1000) } });
    const loser = winner === ga ? gb : ga;
    const take = await claim(7, loser);
    expect(take.status).toBe(200);
    expect(take.body.previousGuildId).toBe(winner);
    // Điều kiện: ít thành viên -> 422; bang giải tán -> 404; giới hạn 3 khu
    expect((await claim(8, ga, 1)).body.code).toBe('TERRITORY_NOT_ENOUGH_MEMBERS');
    const gc = await guild(await user());
    for (const z of [20, 21, 22]) expect((await claim(z, gc)).status).toBe(200);
    expect((await claim(23, gc)).body.code).toBe('TERRITORY_LIMIT');
    // Chỉ game server (token dịch vụ) được báo chiếm
    expect((await h.http.post('/api/v1/internal/territory/claim').send({ zoneId: 9, guildId: ga, serverId: 'x', members: 5 })).status).toBe(401);
    const list = await h.http.get('/api/v1/territories');
    expect(list.body.some((x: { zoneId: number; guild: { id: string } }) => x.zoneId === 7 && x.guild.id === loser)).toBe(true);
  });
});

describe('thưởng không nhân đôi', () => {
  it('cùng phần thưởng giải cứu gửi 6 lần đồng thời (hai server / gửi lại) -> cộng đúng một lần', async () => {
    const u = await user();
    const rs = await Promise.all(Array.from({ length: 6 }, () =>
      h.http.post(`/api/v1/internal/wallet/${u.numericId}/earn`).set(svc()).send({ amount: 80, reason: 'rescue', refKey: 'rescue:42:7' })));
    expect(rs.every((r) => r.status === 200 || r.status === 201)).toBe(true);
    expect(rs.filter((r) => r.body.duplicate === false)).toHaveLength(1);
    expect((await h.http.get('/api/v1/wallet').set(u.auth)).body.balance).toBe(80);
    // Nhiệm vụ: khóa theo (nhân vật, nhiệm vụ, ngày) -> nhận lại cùng ngày không cộng
    for (let i = 0; i < 3; i++)
      await h.http.post(`/api/v1/internal/wallet/${u.numericId}/earn`).set(svc()).send({ amount: 30, reason: 'quest', refKey: 'quest:42:daily_forage:20399' });
    expect((await h.http.get('/api/v1/wallet').set(u.auth)).body.balance).toBe(110);
  });
});

describe('bị bắt -> thông báo đồng đội', () => {
  it('chỉ party/bang nhận; không lộ tọa độ; chống spam; game server nhận danh sách đồng đội', async () => {
    const victim = await user(), mate = await user(), guildMate = await user(), friend = await user();
    await post(victim, '/party');
    const inv = await post(victim, '/party/invites', { userId: mate.id });
    await post(mate, `/party/invites/${inv.body.id}/accept`);
    const gid = await guild(guildMate);
    const jr = await post(victim, `/guilds/${gid}/join-requests`);
    await post(guildMate, `/guild-requests/${jr.body.id}/accept`);
    const fr = await post(victim, '/friends/requests', { displayName: friend.name });
    await post(friend, `/friends/requests/${fr.body.id}/accept`);
    // Game server thấy đồng đội + bang
    const soc = await h.http.get(`/api/v1/internal/users/${victim.numericId}/blocks`).set(svc());
    const mateRow = await h.db.user.findUniqueOrThrow({ where: { id: mate.id } });
    expect(soc.body.teammateUserIds).toEqual(expect.arrayContaining([mateRow.numericId.toString()]));
    expect(soc.body.teammateUserIds).toHaveLength(2);
    expect(soc.body.guildId).toBe(gid);
    const ev = (captureId: number, state = 'CAPTURED') => h.http.post('/api/v1/internal/events/capture').set(svc())
      .send({ userNumericId: victim.numericId, captureId, state, district: 'industrial' });
    expect((await ev(1)).body).toEqual({ notified: 2, throttled: false });
    await ev(1);  // gửi lại (mất mạng/thử lại): không được tạo thông báo trùng
    expect(await h.db.notification.count({ where: { userId: mate.id, type: 'teammate_captured' } })).toBe(1);
    expect((await ev(2)).body.throttled).toBe(true);  // lần bắt mới trong 10 phút -> không spam
    expect((await ev(1, 'RESCUED')).body.notified).toBe(2);
    const n = (await h.http.get('/api/v1/notifications').set(mate.auth)).body.filter((x: { type: string }) => x.type.startsWith('teammate_'));
    expect(n.map((x: { type: string }) => x.type).sort()).toEqual(['teammate_captured', 'teammate_freed']);
    expect(JSON.stringify(n)).not.toMatch(/"x"|"z"|pos/);  // không có tọa độ
    expect((await h.http.get('/api/v1/notifications').set(friend.auth)).body.some((x: { type: string }) => x.type.startsWith('teammate_'))).toBe(false);
    expect((await h.http.post('/api/v1/internal/events/capture').set(victim.auth).send({})).status).toBe(401);
  });
});
