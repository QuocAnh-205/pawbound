// Phase 12 — E2E xã hội trên PostgreSQL thật: bạn bè, chặn, party, guild (ma trận quyền + race), chat, thông báo.
import { Harness, startHarness } from './harness';

let h: Harness;
beforeAll(async () => {
  h = await startHarness('social', { THROTTLE_REGISTER_PER_MIN: '1000', THROTTLE_DEFAULT_PER_MIN: '100000', THROTTLE_CHAT_PER_MIN: '100000', PAWBOUND_GUILD_FEE: 'off' });
}, 120_000);
afterAll(async () => h?.close());

interface U { id: string; name: string; auth: { Authorization: string } }
let seq = 0;
async function user(prefix: string): Promise<U> {
  const n = `${prefix}${++seq}`;
  const r = await h.http.post('/api/v1/auth/register').send({ email: `${n.toLowerCase()}@test.vn`, password: 'mat-khau-dai-123', displayName: n });
  expect(r.status).toBe(201);
  const me = await h.http.get('/api/v1/me').set({ Authorization: `Bearer ${r.body.accessToken}` });
  return { id: me.body.id, name: n, auth: { Authorization: `Bearer ${r.body.accessToken}` } };
}
const post = (u: U, path: string, body: object = {}) => h.http.post(`/api/v1${path}`).set(u.auth).send(body);
const get = (u: U, path: string) => h.http.get(`/api/v1${path}`).set(u.auth);

describe('bạn bè + chặn', () => {
  it('gửi, nhận, chấp nhận, gửi lại idempotent, hủy kết bạn', async () => {
    const a = await user('Ban'), b = await user('Ban');
    const r1 = await post(a, '/friends/requests', { displayName: b.name.toUpperCase() });  // không phân biệt hoa thường
    expect(r1.status).toBe(201);
    expect((await post(a, '/friends/requests', { displayName: b.name })).body.id).toBe(r1.body.id);  // gửi lại không tạo trùng
    const inc = await get(b, '/friends');
    expect(inc.body.incoming).toHaveLength(1);
    const notes = await get(b, '/notifications');
    expect(notes.body.filter((n: { type: string }) => n.type === 'friend_request')).toHaveLength(1);
    expect((await post(a, `/friends/requests/${r1.body.id}/accept`)).status).toBe(404);  // người gửi không tự chấp nhận được
    expect((await post(b, `/friends/requests/${r1.body.id}/accept`)).status).toBe(200);
    expect((await post(b, `/friends/requests/${r1.body.id}/accept`)).status).toBe(404);  // dùng lại
    const fa = await get(a, '/friends');
    expect(fa.body.friends.map((f: { userId: string }) => f.userId)).toEqual([b.id]);
    expect(fa.body.friends[0].online).toBe(false);
    expect((await post(a, '/friends/requests', { displayName: b.name })).status).toBe(409);
    expect((await h.http.delete(`/api/v1/friends/${b.id}`).set(a.auth)).status).toBe(200);
    expect((await get(b, '/friends')).body.friends).toHaveLength(0);
    expect((await post(a, '/friends/requests', { displayName: a.name })).status).toBe(422);
    expect((await post(a, '/friends/requests', { displayName: 'KhongTonTai' })).status).toBe(404);
  });

  it('hai người gửi cho nhau ĐỒNG THỜI -> đúng một quan hệ, đã là bạn', async () => {
    for (let i = 0; i < 5; i++) {
      const c = await user('Dua'), d = await user('Dua');
      const rs = await Promise.all([post(c, '/friends/requests', { displayName: d.name }), post(d, '/friends/requests', { displayName: c.name })]);
      expect(rs.every((r) => r.status === 201)).toBe(true);
      const rows = await h.db.friendship.findMany({ where: { OR: [{ requesterId: c.id }, { requesterId: d.id }] } });
      expect(rows).toHaveLength(1);
      expect(rows[0].status).toBe('ACCEPTED');
    }
  });

  it('chặn: hủy kết bạn, không gửi lời mời được, ẩn chat', async () => {
    const a = await user('Chan'), b = await user('Chan');
    const r = await post(a, '/friends/requests', { displayName: b.name });
    await post(b, `/friends/requests/${r.body.id}/accept`);
    expect((await post(a, '/blocks', { userId: b.id })).status).toBe(201);
    expect((await post(a, '/blocks', { userId: b.id })).status).toBe(201);  // idempotent
    expect((await get(a, '/friends')).body.friends).toHaveLength(0);
    expect((await post(b, '/friends/requests', { displayName: a.name })).status).toBe(404);  // không lộ là bị chặn
    expect((await post(a, '/friends/requests', { displayName: b.name })).body.code).toBe('UNBLOCK_FIRST');
    const msg = await post(b, '/chat/global', { text: 'xin chao' });
    expect(msg.status).toBe(201);
    const histA = await get(a, '/chat/global');
    expect(histA.body.some((m: { id: string }) => m.id === msg.body.id)).toBe(false);
    expect((await get(b, '/chat/global')).body.some((m: { id: string }) => m.id === msg.body.id)).toBe(true);
    expect((await h.http.delete(`/api/v1/blocks/${b.id}`).set(a.auth)).status).toBe(200);
    expect((await get(a, '/chat/global')).body.some((m: { id: string }) => m.id === msg.body.id)).toBe(true);
  });
});

describe('party', () => {
  it('mời/nhận/rời/kick, giới hạn thành viên, quyền trưởng nhóm, lời mời dùng lại', async () => {
    const [l, a, b, c, d] = await Promise.all([user('Pt'), user('Pt'), user('Pt'), user('Pt'), user('Pt')]);
    expect((await post(l, '/party')).status).toBe(201);
    expect((await post(l, '/party')).status).toBe(409);
    const ia = await post(l, '/party/invites', { userId: a.id });
    expect(ia.status).toBe(201);
    expect((await post(l, '/party/invites', { userId: a.id })).body.id).toBe(ia.body.id);  // idempotent
    expect((await post(b, `/party/invites/${ia.body.id}/accept`)).status).toBe(404);  // không phải người được mời
    expect((await post(a, `/party/invites/${ia.body.id}/accept`)).status).toBe(200);
    expect((await post(a, `/party/invites/${ia.body.id}/accept`)).status).toBe(404);  // replay
    expect((await post(a, '/party/invites', { userId: b.id })).body.code).toBe('NOT_PARTY_LEADER');
    expect((await post(a, `/party/members/${l.id}/kick`)).status).toBe(403);
    // Đầy (max 4): mời b, c, d; chấp nhận đồng thời -> chỉ 2 người vào được
    const inv = await Promise.all([b, c, d].map((x) => post(l, '/party/invites', { userId: x.id })));
    const acc = await Promise.all([b, c, d].map((x, i) => post(x, `/party/invites/${inv[i].body.id}/accept`)));
    expect(acc.filter((r) => r.status === 200)).toHaveLength(2);
    expect(acc.filter((r) => r.body.code === 'PARTY_FULL')).toHaveLength(1);
    const p = await get(l, '/party');
    expect(p.body.members).toHaveLength(4);
    // Trưởng nhóm rời -> người vào sớm nhất (a) lên thay
    expect((await post(l, '/party/leave')).body.leaderId).toBe(a.id);
    expect((await get(a, '/party')).body.leaderId).toBe(a.id);
    expect((await post(a, `/party/members/${a.id}/kick`)).status).toBe(422);
  });

  it('matchmaking xếp người cùng party vào server đồng đội đang chơi', async () => {
    const svc = { 'X-Service-Token': process.env.INTERNAL_SERVICE_TOKEN! };
    const hb = (id: string, cur: number) => h.http.post('/api/v1/internal/servers/heartbeat').set(svc)
      .send({ id, region: 'vn', address: '127.0.0.1', port: 27015, capacity: 32, currentPlayers: cur, buildVersion: 't' });
    await hb('srv-busy', 20);
    await hb('srv-idle', 0);
    const a = await user('Mm'), b = await user('Mm');
    await post(a, '/party');
    const inv = await post(a, '/party/invites', { userId: b.id });
    await post(b, `/party/invites/${inv.body.id}/accept`);
    const ca = await post(a, '/characters', { name: 'Vang', breedId: 'shiba' });
    const cb = await post(b, '/characters', { name: 'Den', breedId: 'shiba' });
    // a đang chơi trên srv-busy (mô phỏng phiên mở)
    await h.db.gameSession.create({ data: { userId: a.id, characterId: ca.body.id, serverId: 'srv-busy', ticketJti: `jti-${Date.now()}` } });
    const solo = await user('Mm');
    const cs = await post(solo, '/characters', { name: 'Le', breedId: 'shiba' });
    expect((await post(solo, '/matchmaking/join', { characterId: cs.body.id, region: 'vn' })).body.server.id).toBe('srv-idle');  // tải thấp
    expect((await post(b, '/matchmaking/join', { characterId: cb.body.id, region: 'vn' })).body.server.id).toBe('srv-busy');  // theo đồng đội
    expect((await get(b, '/party')).body.members.find((m: { userId: string }) => m.userId === a.id).online).toBe(true);
  });
});

describe('guild', () => {
  async function guildWith(n: number) {
    const owner = await user('Gd');
    const g = await post(owner, '/guilds', { name: `Bang ${owner.name}`, description: 'Đàn chó phố' });
    expect(g.status).toBe(201);
    const members: U[] = [];
    for (let i = 0; i < n; i++) {
      const m = await user('Gm');
      const jr = await post(m, `/guilds/${g.body.id}/join-requests`);
      expect((await post(owner, `/guild-requests/${jr.body.id}/accept`)).status).toBe(200);
      members.push(m);
    }
    return { owner, id: g.body.id as string, members };
  }

  it('ma trận quyền qua HTTP (gồm leo thang quyền bằng request giả)', async () => {
    const { owner, id, members: [off, m1, m2] } = await guildWith(3);
    const outsider = await user('Out');
    const act = (u: U, t: U, a: string) => post(u, `/guilds/${id}/members/${t.id}/${a}`);
    // MEMBER: không làm được gì quản trị
    expect((await act(m1, m2, 'kick')).status).toBe(403);
    expect((await act(m1, m2, 'promote')).status).toBe(403);
    expect((await act(m1, m1, 'promote')).status).toBe(422);  // tự thăng cấp
    expect((await post(m1, `/guilds/${id}/invites`, { userId: outsider.id })).status).toBe(403);
    expect((await h.http.patch(`/api/v1/guilds/${id}`).set(m1.auth).send({ description: 'hack' })).status).toBe(403);
    expect((await h.http.patch(`/api/v1/guilds/${id}`).set(m1.auth).send({ role: 'OWNER' })).status).toBe(400);  // field lạ bị chặn
    expect((await get(m1, `/guilds/${id}/requests`)).status).toBe(403);
    expect((await h.http.delete(`/api/v1/guilds/${id}`).set(m1.auth)).status).toBe(403);
    // OWNER thăng off lên OFFICER
    expect((await act(owner, off, 'promote')).status).toBe(200);
    expect((await act(owner, off, 'promote')).status).toBe(403);  // đã là OFFICER
    // OFFICER: kick MEMBER được; không kick/đổi cấp OWNER hay OFFICER; không đổi tên; sửa mô tả được
    expect((await act(off, owner, 'kick')).status).toBe(403);
    expect((await act(off, m1, 'promote')).status).toBe(403);
    expect((await act(off, owner, 'transfer')).status).toBe(403);
    expect((await h.http.patch(`/api/v1/guilds/${id}`).set(off.auth).send({ name: 'Bang Moi Ten' })).status).toBe(403);
    expect((await h.http.patch(`/api/v1/guilds/${id}`).set(off.auth).send({ description: 'Mô tả mới' })).status).toBe(200);
    expect((await act(off, m2, 'kick')).status).toBe(200);
    // Người ngoài
    expect((await act(outsider, m1, 'kick')).status).toBe(403);
    expect((await h.http.patch(`/api/v1/guilds/${id}`).set(outsider.auth).send({ description: 'x' })).status).toBe(403);
    // OWNER không leave được; chuyển quyền rồi mới leave
    expect((await post(owner, `/guilds/${id}/leave`)).body.code).toBe('OWNER_MUST_TRANSFER');
    expect((await act(owner, m1, 'transfer')).status).toBe(200);
    const mine = await get(m1, '/guilds/mine');
    expect(mine.body.myRole).toBe('OWNER');
    expect(mine.body.permissions).toContain('disband');
    expect((await get(owner, '/guilds/mine')).body.myRole).toBe('OFFICER');
    expect((await act(owner, m1, 'kick')).status).toBe(403);  // chủ cũ không kick được chủ mới
    expect((await post(owner, `/guilds/${id}/leave`)).status).toBe(200);
    expect(await h.db.auditLog.count({ where: { action: 'guild.transfer', target: id } })).toBe(1);
  });

  it('lời mời: chỉ người được mời chấp nhận, dùng lại bị từ chối, người đã có bang không nhận được', async () => {
    const { owner, id } = await guildWith(0);
    const x = await user('Inv'), y = await user('Inv');
    const inv = await post(owner, `/guilds/${id}/invites`, { userId: x.id });
    expect(inv.status).toBe(201);
    expect((await post(owner, `/guilds/${id}/invites`, { userId: x.id })).body.id).toBe(inv.body.id);
    expect((await post(y, `/guild-requests/${inv.body.id}/accept`)).status).toBe(404);
    expect((await post(owner, `/guild-requests/${inv.body.id}/accept`)).status).toBe(404);  // chủ bang không tự nhận thay
    expect((await post(x, `/guild-requests/${inv.body.id}/accept`)).status).toBe(200);
    expect((await post(x, `/guild-requests/${inv.body.id}/accept`)).status).toBe(404);  // replay
    expect((await post(owner, `/guilds/${id}/invites`, { userId: x.id })).body.code).toBe('TARGET_IN_GUILD');
    const other = await post(y, '/guilds', { name: `Khac ${y.name}` });
    expect(other.status).toBe(201);
    expect((await post(y, `/guilds/${id}/join-requests`)).body.code).toBe('ALREADY_IN_GUILD');
  });

  it('RACE: nhiều người tạo bang cùng tên (khác hoa/thường, khoảng trắng) -> đúng một thành công', async () => {
    const us = await Promise.all(Array.from({ length: 8 }, () => user('Race')));
    const names = ['Soi Xam', 'soi xam', 'SOI  XAM', ' Soi Xam ', 'Soi xam', 'sOi XaM', 'Soi Xam', 'soi   xam'];
    const rs = await Promise.all(us.map((u, i) => post(u, '/guilds', { name: names[i] })));
    expect(rs.filter((r) => r.status === 201)).toHaveLength(1);
    expect(rs.filter((r) => r.status === 409 && r.body.code === 'GUILD_NAME_TAKEN')).toHaveLength(7);
    expect(await h.db.guild.count({ where: { nameKey: 'soi xam' } })).toBe(1);
  });

  it('RACE: một người tạo hai bang cùng lúc -> chỉ một', async () => {
    const u = await user('Two');
    const rs = await Promise.all([post(u, '/guilds', { name: `Mot ${u.name}` }), post(u, '/guilds', { name: `Hai ${u.name}` })]);
    expect(rs.filter((r) => r.status === 201)).toHaveLength(1);
    expect(await h.db.guildMember.count({ where: { userId: u.id } })).toBe(1);
  });

  it('RACE: duyệt đồng thời nhiều đơn khi gần đầy -> không vượt giới hạn; một người được hai bang duyệt cùng lúc -> chỉ vào một', async () => {
    const { owner, id } = await guildWith(0);
    await h.db.guild.update({ where: { id }, data: { memberLimit: 3 } });
    const apps = await Promise.all(Array.from({ length: 6 }, () => user('Full')));
    const reqs = await Promise.all(apps.map((u) => post(u, `/guilds/${id}/join-requests`)));
    const acc = await Promise.all(reqs.map((r) => post(owner, `/guild-requests/${r.body.id}/accept`)));
    expect(acc.filter((r) => r.status === 200)).toHaveLength(2);
    expect(await h.db.guildMember.count({ where: { guildId: id } })).toBe(3);

    const g1 = await guildWith(0), g2 = await guildWith(0);
    const z = await user('Both');
    const r1 = await post(z, `/guilds/${g1.id}/join-requests`), r2 = await post(z, `/guilds/${g2.id}/join-requests`);
    const both = await Promise.all([post(g1.owner, `/guild-requests/${r1.body.id}/accept`), post(g2.owner, `/guild-requests/${r2.body.id}/accept`)]);
    expect(both.filter((r) => r.status === 200)).toHaveLength(1);
    expect(await h.db.guildMember.count({ where: { userId: z.id } })).toBe(1);
  });

  it('giải tán: xóa thành viên, hủy yêu cầu, tên dùng lại được; tên không hợp lệ bị từ chối', async () => {
    const { owner, id, members: [m] } = await guildWith(1);
    const name = (await get(owner, `/guilds/${id}`)).body.name as string;
    const pending = await post(await user('Late'), `/guilds/${id}/join-requests`);
    expect((await h.http.delete(`/api/v1/guilds/${id}`).set(owner.auth)).status).toBe(200);
    expect((await get(m, '/guilds/mine')).body).toEqual({});
    expect((await h.db.guildRequest.findUniqueOrThrow({ where: { id: pending.body.id } })).status).toBe('CANCELLED');
    expect((await get(owner, `/guilds/${id}`)).status).toBe(404);
    expect((await post(m, '/guilds', { name })).status).toBe(201);
    const u = await user('Bad');
    expect((await post(u, '/guilds', { name: 'ab' })).body.code).toBe('GUILD_NAME_INVALID');
    expect((await post(u, '/guilds', { name: 'Bang <script>' })).body.code).toBe('GUILD_NAME_INVALID');
    expect((await post(u, '/guilds', { name: 'Bang fuck' })).body.code).toBe('GUILD_NAME_INAPPROPRIATE');
  });
});

describe('chat', () => {
  it('kênh, độ dài, ký tự điều khiển, lọc từ, rate limit, mute, báo cáo', async () => {
    const a = await user('Chat'), b = await user('Chat');
    expect((await post(a, '/chat/party', { text: 'hi' })).body.code).toBe('NOT_IN_PARTY');
    expect((await post(a, '/chat/guild', { text: 'hi' })).body.code).toBe('NOT_GUILD_MEMBER');
    expect((await post(a, '/chat/secret', { text: 'hi' })).status).toBe(404);
    expect((await post(a, '/chat/global', { text: 'x'.repeat(201) })).body.code).toBe('CHAT_LENGTH');
    expect((await post(a, '/chat/global', { text: '   \n\t ' })).body.code).toBe('CHAT_LENGTH');
    const m1 = await post(a, '/chat/global', { text: 'xin\u0000 chao‮ ban' });
    expect(m1.body.text).toBe('xin chao ban');
    expect((await post(a, '/chat/global', { text: 'đồ shit' })).body.text).toBe('đồ ****');
    expect((await post(a, '/chat/global', { text: 'shitake ngon' })).body.text).toBe('shitake ngon');  // không bắt nhầm từ chứa chuỗi con
    // Rate limit 5 tin / 10 s: gửi song song 6 tin nữa -> đã có 3, chỉ 2 lọt
    const burst = await Promise.all(Array.from({ length: 6 }, (_, i) => post(a, '/chat/global', { text: `spam ${i}` })));
    expect(burst.filter((r) => r.status === 201)).toHaveLength(2);
    expect(burst.filter((r) => r.status === 429)).toHaveLength(4);
    // Polling: after=id chỉ trả tin mới hơn
    const after = await get(b, `/chat/global?after=${m1.body.id}`);
    expect(after.body.every((m: { id: string }) => BigInt(m.id) > BigInt(m1.body.id))).toBe(true);
    // Mute bởi admin
    const adm = await user('Adm');
    await h.db.user.update({ where: { id: adm.id }, data: { role: 'ADMIN' } });
    const login = await h.http.post('/api/v1/auth/login').send({ email: `${adm.name.toLowerCase()}@test.vn`, password: 'mat-khau-dai-123' });
    const admAuth = { Authorization: `Bearer ${login.body.accessToken}` };
    expect((await h.http.post(`/api/v1/admin/users/${b.id}/mute`).set(admAuth).send({ minutes: 10 })).status).toBe(200);
    expect((await h.http.post(`/api/v1/admin/users/${b.id}/mute`).set(b.auth).send({ minutes: 0 })).status).toBe(403);  // người thường không gỡ được
    expect((await post(b, '/chat/global', { text: 'alo' })).body.code).toBe('CHAT_MUTED');
    await h.http.post(`/api/v1/admin/users/${b.id}/mute`).set(admAuth).send({ minutes: 0 });
    expect((await post(b, '/chat/global', { text: 'alo' })).status).toBe(201);
    expect((await post(b, `/chat/messages/${m1.body.id}/report`, { reason: 'spam' })).status).toBe(200);
    expect(await h.db.auditLog.count({ where: { action: 'chat.report', target: m1.body.id } })).toBe(1);
  });

  it('chat bang/nhóm chỉ thành viên đọc được', async () => {
    const owner = await user('Gc'), outsider = await user('Gc');
    await post(owner, '/guilds', { name: `Chat ${owner.name}` });
    const sent = await post(owner, '/chat/guild', { text: 'họp bang' });
    expect(sent.status).toBe(201);
    expect((await get(owner, '/chat/guild')).body.map((m: { text: string }) => m.text)).toContain('họp bang');
    await post(outsider, '/guilds', { name: `Rieng ${outsider.name}` });
    expect((await get(outsider, '/chat/guild')).body.map((m: { text: string }) => m.text)).not.toContain('họp bang');
    // Báo cáo tin của kênh mình không ở -> 404
    expect((await post(outsider, `/chat/messages/${sent.body.id}/report`, {})).status).toBe(404);
  });
});
