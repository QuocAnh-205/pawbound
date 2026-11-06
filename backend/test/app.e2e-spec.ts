// E2E với PostgreSQL THẬT (database pawbound_test): migrate từ rỗng, rồi gọi API qua HTTP.
import { INestApplication } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { execFileSync, execSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import request from 'supertest';
import { buildModule, configureApp, loadEnv } from '../src/app.module';
import { PrismaClient } from '@prisma/client';
import { PrismaService } from '../src/prisma/prisma.service';

const ROOT = resolve(__dirname, '..');
for (const line of readFileSync(resolve(ROOT, '.env'), 'utf8').split(/\r?\n/)) {
  const m = /^([A-Z0-9_]+)=(.*)$/.exec(line.trim());
  if (m) process.env[m[1]] = m[2];
}
process.env.THROTTLE_REGISTER_PER_MIN = '1000';  // test tạo nhiều tài khoản; giới hạn đăng nhập giữ mặc định (10)
// Database TẠM riêng cho lần chạy này (tạo mới -> migrate deploy -> xóa khi xong). Không đụng database dev/test có sẵn.
const TEMP_DB = `pawbound_e2e_${Date.now()}`;
const baseUrl = new URL(process.env.DATABASE_URL_TEST!);
const tempUrl = new URL(baseUrl.toString());
tempUrl.pathname = `/${TEMP_DB}`;
process.env.DATABASE_URL = tempUrl.toString();
const CPP_TOOL = resolve(ROOT, '../build/win-debug/bin/pawbound.exe');

let app: INestApplication;
let db: PrismaService;
let admin: PrismaClient;
let http: ReturnType<typeof request>;
const svc = () => ({ 'X-Service-Token': process.env.INTERNAL_SERVICE_TOKEN! });

beforeAll(async () => {
  admin = new PrismaClient({ datasources: { db: { url: baseUrl.toString() } } });
  await admin.$executeRawUnsafe(`CREATE DATABASE "${TEMP_DB}"`);
  // Migration chạy từ database RỖNG (chứng minh migrate từ đầu hoạt động)
  execSync('npx prisma migrate deploy', { cwd: ROOT, stdio: 'pipe', env: process.env });
  app = await NestFactory.create(buildModule(loadEnv()), { logger: false });
  configureApp(app);
  await app.init();
  db = app.get(PrismaService);
  http = request(app.getHttpServer());
}, 120_000);
afterAll(async () => {
  await app?.close();
  // Chỉ xóa database tạm do chính lần chạy này tạo ra
  await admin.$executeRawUnsafe(`DROP DATABASE IF EXISTS "${TEMP_DB}" WITH (FORCE)`);
  await admin.$disconnect();
});

async function register(email: string, name: string, password = 'mat-khau-dai-123') {
  const r = await http.post('/api/v1/auth/register').send({ email, password, displayName: name });
  expect(r.status).toBe(201);
  return r.body as { accessToken: string; refreshToken: string };
}

describe('auth', () => {
  it('đăng ký, đăng nhập, /me; không lưu mật khẩu thô', async () => {
    const t = await register('a@test.vn', 'ChoA');
    const me = await http.get('/api/v1/me').set('Authorization', `Bearer ${t.accessToken}`);
    expect(me.status).toBe(200);
    expect(me.body.displayName).toBe('ChoA');
    const u = await db.user.findUniqueOrThrow({ where: { email: 'a@test.vn' } });
    expect(u.passwordHash.startsWith('$argon2id$')).toBe(true);
    expect(u.passwordHash).not.toContain('mat-khau-dai-123');
    const rt = await db.refreshToken.findFirstOrThrow({ where: { userId: u.id } });
    expect(rt.tokenHash).not.toBe(t.refreshToken);  // chỉ lưu hash
    const ok = await http.post('/api/v1/auth/login').send({ email: 'A@test.vn', password: 'mat-khau-dai-123' });
    expect(ok.status).toBe(200);
    const bad = await http.post('/api/v1/auth/login').send({ email: 'a@test.vn', password: 'sai-mat-khau-xx' });
    expect(bad.status).toBe(401);
    expect(bad.body.code).toBe('INVALID_CREDENTIALS');
  });

  it('validation DTO: email sai, mật khẩu ngắn, trường lạ bị từ chối; email trùng -> 409', async () => {
    expect((await http.post('/api/v1/auth/register').send({ email: 'x', password: 'ngan', displayName: 'ab' })).status).toBe(400);
    expect((await http.post('/api/v1/auth/register').send({ email: 'b@test.vn', password: 'mat-khau-dai-123', displayName: 'ChoB', role: 'ADMIN' })).status).toBe(400);
    await register('c@test.vn', 'ChoC');
    expect((await http.post('/api/v1/auth/register').send({ email: 'c@test.vn', password: 'mat-khau-dai-123', displayName: 'Khac' })).status).toBe(409);
  });

  it('refresh xoay vòng; dùng lại token cũ -> thu hồi cả họ; logout thu hồi', async () => {
    const t = await register('d@test.vn', 'ChoD');
    const r1 = await http.post('/api/v1/auth/refresh').send({ refreshToken: t.refreshToken });
    expect(r1.status).toBe(200);
    expect(r1.body.refreshToken).not.toBe(t.refreshToken);
    const reuse = await http.post('/api/v1/auth/refresh').send({ refreshToken: t.refreshToken });
    expect(reuse.status).toBe(401);
    expect(reuse.body.code).toBe('REFRESH_REUSED');
    // Token mới nhất cũng bị thu hồi theo họ (kẻ trộm và chủ đều phải đăng nhập lại)
    expect((await http.post('/api/v1/auth/refresh').send({ refreshToken: r1.body.refreshToken })).status).toBe(401);
    const t2 = await http.post('/api/v1/auth/login').send({ email: 'd@test.vn', password: 'mat-khau-dai-123' });
    expect((await http.post('/api/v1/auth/logout').send({ refreshToken: t2.body.refreshToken })).status).toBe(200);
    expect((await http.post('/api/v1/auth/refresh').send({ refreshToken: t2.body.refreshToken })).status).toBe(401);
  });

  it('token sai/thiếu -> 401; người chơi gọi admin -> 403', async () => {
    expect((await http.get('/api/v1/me')).status).toBe(401);
    expect((await http.get('/api/v1/me').set('Authorization', 'Bearer abc.def.ghi')).status).toBe(401);
    const t = await register('e@test.vn', 'ChoE');
    expect((await http.get('/api/v1/admin/users').set('Authorization', `Bearer ${t.accessToken}`)).status).toBe(403);
  });

});

describe('characters', () => {
  it('tạo/xem/đổi tên/xóa; tối đa 3; người khác không truy cập được', async () => {
    const a = await register('f@test.vn', 'ChoF');
    const b = await register('g@test.vn', 'ChoG');
    const auth = { Authorization: `Bearer ${a.accessToken}` };
    const c1 = await http.post('/api/v1/characters').set(auth).send({ name: 'Mực', breedId: 'shiba', appearance: { coatColor: 2 } });
    expect(c1.status).toBe(201);
    expect(c1.body.breedId).toBe('shiba');
    expect((await http.post('/api/v1/characters').set(auth).send({ name: 'Sai', breedId: 'rong_bay' })).status).toBe(400);
    expect((await http.post('/api/v1/characters').set(auth).send({ name: 'X', breedId: 'shiba', appearance: { sizeVariant: 3 } })).status).toBe(400);
    await http.post('/api/v1/characters').set(auth).send({ name: 'Vàng', breedId: 'street_mix' });
    await http.post('/api/v1/characters').set(auth).send({ name: 'Đốm', breedId: 'shepherd' });
    const fourth = await http.post('/api/v1/characters').set(auth).send({ name: 'Thừa', breedId: 'shiba' });
    expect(fourth.status).toBe(422);
    expect(fourth.body.code).toBe('CHARACTER_LIMIT');
    expect((await http.get('/api/v1/characters').set(auth)).body).toHaveLength(3);
    // Người B không thấy/sửa/xóa được nhân vật của A
    const other = { Authorization: `Bearer ${b.accessToken}` };
    expect((await http.get(`/api/v1/characters/${c1.body.id}`).set(other)).status).toBe(404);
    expect((await http.patch(`/api/v1/characters/${c1.body.id}`).set(other).send({ name: 'Cướp' })).status).toBe(404);
    expect((await http.delete(`/api/v1/characters/${c1.body.id}`).set(other)).status).toBe(404);
    expect((await http.patch(`/api/v1/characters/${c1.body.id}`).set(auth).send({ name: 'Mực Đen' })).body.name).toBe('Mực Đen');
    expect((await http.delete(`/api/v1/characters/${c1.body.id}`).set(auth)).status).toBe(204);
    expect((await http.get('/api/v1/characters').set(auth)).body).toHaveLength(2);
  });

  it('tạo song song không vượt giới hạn 3', async () => {
    const a = await register('h@test.vn', 'ChoH');
    const auth = { Authorization: `Bearer ${a.accessToken}` };
    const rs = await Promise.all([1, 2, 3, 4, 5, 6].map((i) => http.post('/api/v1/characters').set(auth).send({ name: `C${i}`, breedId: 'shiba' })));
    expect(rs.filter((r) => r.status === 201)).toHaveLength(3);
  });
});

describe('matchmaking + server nội bộ + lưu trạng thái', () => {
  it('không có server -> 503 rõ ràng', async () => {
    const a = await register('i@test.vn', 'ChoI');
    const auth = { Authorization: `Bearer ${a.accessToken}` };
    const c = await http.post('/api/v1/characters').set(auth).send({ name: 'Bơ', breedId: 'shiba' });
    const r = await http.post('/api/v1/matchmaking/join').set(auth).send({ characterId: c.body.id });
    expect(r.status).toBe(503);
    expect(r.body.code).toBe('NO_SERVER_AVAILABLE');
  });

  it('API nội bộ đòi service token', async () => {
    const hb = { id: 'dev-1', region: 'vn', address: '127.0.0.1', port: 27015, capacity: 32, currentPlayers: 0, buildVersion: '0.1.0' };
    expect((await http.post('/api/v1/internal/servers/heartbeat').send(hb)).status).toBe(401);
    expect((await http.post('/api/v1/internal/servers/heartbeat').set('X-Service-Token', 'sai').send(hb)).status).toBe(401);
    expect((await http.post('/api/v1/internal/servers/heartbeat').set(svc()).send(hb)).status).toBe(200);
  });

  it('heartbeat -> join chọn server đúng vùng/còn chỗ, cấp ticket mà game server C++ xác minh được', async () => {
    await http.post('/api/v1/internal/servers/heartbeat').set(svc())
      .send({ id: 'eu-1', region: 'eu', address: '10.0.0.2', port: 27015, capacity: 32, currentPlayers: 3, buildVersion: '0.1.0' });
    await http.post('/api/v1/internal/servers/heartbeat').set(svc())
      .send({ id: 'vn-full', region: 'vn', address: '10.0.0.3', port: 27015, capacity: 2, currentPlayers: 2, buildVersion: '0.1.0' });
    await http.post('/api/v1/internal/servers/heartbeat').set(svc())
      .send({ id: 'dev-1', region: 'vn', address: '127.0.0.1', port: 27015, capacity: 32, currentPlayers: 5, buildVersion: '0.1.0' });
    const a = await register('j@test.vn', 'ChoJ');
    const auth = { Authorization: `Bearer ${a.accessToken}` };
    const c = await http.post('/api/v1/characters').set(auth).send({ name: 'Ki', breedId: 'shiba' });
    const r = await http.post('/api/v1/matchmaking/join').set(auth).send({ characterId: c.body.id, region: 'vn' });
    expect(r.status).toBe(200);
    expect(r.body.server.id).toBe('dev-1');  // vn-full đầy, eu-1 khác vùng
    expect(r.body.expiresAt - Date.now() / 1000).toBeLessThanOrEqual(61);
    const sess = await db.gameSession.findFirstOrThrow({ where: { characterId: c.body.id } });
    expect(sess.serverId).toBe('dev-1');
    if (existsSync(CPP_TOOL)) {
      const out = execFileSync(CPP_TOOL, ['net', 'verify-ticket', '--pk', resolve(ROOT, '../config/dev_keys/ticket_pk.b64'),
                                           '--server', 'dev-1', '--ticket', r.body.ticket], { encoding: 'utf8' });
      expect(out).toContain(`char=${c.body.numericId}`);
      // Ticket cho dev-1 không dùng được ở server khác
      expect(() => execFileSync(CPP_TOOL, ['net', 'verify-ticket', '--pk', resolve(ROOT, '../config/dev_keys/ticket_pk.b64'),
                                           '--server', 'eu-1', '--ticket', r.body.ticket])).toThrow();
    } else {
      console.warn('Bỏ qua kiểm tra chéo C++: chưa build pawbound.exe');
    }
    // Server "chết" (heartbeat cũ) không được chọn
    await db.serverInstance.updateMany({ data: { lastHeartbeatAt: new Date(Date.now() - 120_000) } });
    expect((await http.post('/api/v1/matchmaking/join').set(auth).send({ characterId: c.body.id })).status).toBe(503);
  });

  it('lưu trạng thái: idempotent, xung đột phiên bản -> 409, dữ liệu sai -> 422', async () => {
    const a = await register('k@test.vn', 'ChoK');
    const c = await http.post('/api/v1/characters').set({ Authorization: `Bearer ${a.accessToken}` }).send({ name: 'Lu', breedId: 'shiba' });
    const nid = c.body.numericId as string;
    const state = { pos: { x: 100, y: 0, z: 120 }, vitals: { hp: 80, hunger: 60, thirst: 55, stamina: 90, rest: 70 }, inventory: [{ item: 'scrap_food', count: 2 }, null] };
    const load0 = await http.get(`/api/v1/internal/characters/${nid}/state`).set(svc());
    expect(load0.body.stateVersion).toBe(0);
    const put = (body: object) => http.put(`/api/v1/internal/characters/${nid}/state`).set(svc()).send(body);
    const s1 = await put({ expectedVersion: 0, idempotencyKey: 'save-0001-a', serverId: 'dev-1', state });
    expect(s1.status).toBe(200);
    expect(s1.body).toEqual({ stateVersion: 1, idempotent: false });
    const again = await put({ expectedVersion: 0, idempotencyKey: 'save-0001-a', serverId: 'dev-1', state });  // gửi lại do timeout
    expect(again.body).toEqual({ stateVersion: 1, idempotent: true });
    const stale = await put({ expectedVersion: 0, idempotencyKey: 'save-0002-b', serverId: 'dev-2', state });  // server khác, bản cũ
    expect(stale.status).toBe(409);
    expect((await put({ expectedVersion: 1, idempotencyKey: 'save-0003-c', serverId: 'dev-1', state: { ...state, vitals: { ...state.vitals, hp: 99999 } } })).status).toBe(422);
    const load1 = await http.get(`/api/v1/internal/characters/${nid}/state`).set(svc());
    expect(load1.body.stateVersion).toBe(1);
    expect(load1.body.state.vitals.hp).toBe(80);
    expect((await http.get('/api/v1/internal/characters/999999/state').set(svc())).status).toBe(404);
  });
});

describe('admin + health', () => {
  it('admin xem danh sách có phân trang và khóa tài khoản (thu hồi phiên)', async () => {
    const adm = await register('admin@test.vn', 'QuanTri');
    await db.user.update({ where: { email: 'admin@test.vn' }, data: { role: 'ADMIN' } });
    const login = await http.post('/api/v1/auth/login').send({ email: 'admin@test.vn', password: 'mat-khau-dai-123' });
    const auth = { Authorization: `Bearer ${login.body.accessToken}` };
    void adm;
    const list = await http.get('/api/v1/admin/users?page=1&size=5').set(auth);
    expect(list.status).toBe(200);
    expect(list.body.items.length).toBeLessThanOrEqual(5);
    expect(list.body.total).toBeGreaterThan(5);
    const victim = await register('l@test.vn', 'ChoL');
    const vu = await db.user.findUniqueOrThrow({ where: { email: 'l@test.vn' } });
    expect((await http.post(`/api/v1/admin/users/${vu.id}/ban`).set(auth)).status).toBe(200);
    expect((await http.post('/api/v1/auth/login').send({ email: 'l@test.vn', password: 'mat-khau-dai-123' })).status).toBe(403);
    expect((await http.post('/api/v1/auth/refresh').send({ refreshToken: victim.refreshToken })).status).toBe(401);
    expect(await db.auditLog.count({ where: { action: 'admin.ban' } })).toBe(1);
  });

  it('health + readiness', async () => {
    expect((await http.get('/api/v1/health')).body.status).toBe('ok');
    expect((await http.get('/api/v1/health/ready')).body.db).toBe('ok');
  });
});

// Chạy CUỐI: test này cố ý dùng hết hạn mức đăng nhập của IP.
describe('rate limit', () => {
  it('rate limit đăng nhập', async () => {
    let limited = false;
    for (let i = 0; i < 15 && !limited; i++)
      limited = (await http.post('/api/v1/auth/login').send({ email: 'z@test.vn', password: 'x'.repeat(12) })).status === 429;
    expect(limited).toBe(true);
  });
});
