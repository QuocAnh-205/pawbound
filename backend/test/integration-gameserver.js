// Tích hợp THẬT: PostgreSQL (DB tạm) + Backend (node dist) + pawbound_server (--backend) + pawbound_bot.
// Kiểm tra: heartbeat -> matchmaking cấp ticket -> server nạp trạng thái -> lưu khi rời -> vào lại nạp đúng bản đã lưu
//           -> ticket nhân vật không tồn tại bị từ chối -> tắt server lưu hết + DRAINING. Xóa DB tạm khi xong.
// Chạy: npm run build && node test/integration-gameserver.js [thư mục bin]
'use strict';
const { spawn, execSync } = require('node:child_process');
const { readFileSync, existsSync } = require('node:fs');
const { resolve } = require('node:path');
const { PrismaClient } = require('@prisma/client');

const ROOT = resolve(__dirname, '..');
const REPO = resolve(ROOT, '..');
const BIN = resolve(process.argv[2] || resolve(REPO, 'build/win-debug/bin'));
const exe = (n) => resolve(BIN, n + (process.platform === 'win32' ? '.exe' : ''));
const API_PORT = 3911, GAME_PORT = 27411, SERVER_ID = 'it-1';
const API = `http://127.0.0.1:${API_PORT}/api/v1`;
const WORLD = resolve(REPO, 'out/world_demo');

const fileEnv = {};
for (const line of readFileSync(resolve(ROOT, '.env'), 'utf8').split(/\r?\n/)) {
  const m = /^([A-Z0-9_]+)=(.*)$/.exec(line.trim());
  if (m) fileEnv[m[1]] = m[2];
}
const TEMP_DB = `pawbound_it_${Date.now()}`;
const baseUrl = new URL(fileEnv.DATABASE_URL_TEST);
const tempUrl = new URL(baseUrl.toString());
tempUrl.pathname = `/${TEMP_DB}`;
const env = { ...process.env, ...fileEnv, DATABASE_URL: tempUrl.toString(), PORT: String(API_PORT), THROTTLE_REGISTER_PER_MIN: '100' };

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failures = 0;
function check(cond, msg) {
  console.log(`${cond ? 'PASS' : 'FAIL'}  ${msg}`);
  if (!cond) failures++;
}
async function api(method, path, body, headers = {}) {
  const r = await fetch(API + path, { method, headers: { 'content-type': 'application/json', ...headers }, body: body ? JSON.stringify(body) : undefined });
  const text = await r.text();
  let json = null;
  try { json = JSON.parse(text); } catch { /* không phải JSON */ }
  return { status: r.status, body: json ?? text };
}
const svc = { 'X-Service-Token': fileEnv.INTERNAL_SERVICE_TOKEN };
function startProc(cmd, args, opts, tag) {
  const p = spawn(cmd, args, { ...opts, stdio: ['pipe', 'pipe', 'pipe'] });
  p.log = '';
  const take = (d) => { p.log += d.toString(); };
  p.stdout.on('data', take);
  p.stderr.on('data', take);
  p.tag = tag;
  return p;
}
async function waitFor(fn, ms, what) {
  const end = Date.now() + ms;
  while (Date.now() < end) {
    try { if (await fn()) return true; } catch { /* thử lại */ }
    await sleep(200);
  }
  console.log(`  (hết giờ chờ: ${what})`);
  return false;
}
function runBot(args) {
  return new Promise((res) => {
    const p = startProc(exe('pawbound_bot'), ['--port', String(GAME_PORT), '--world', WORLD, '--server-id', SERVER_ID, '--count', '1', ...args], { cwd: REPO }, 'bot');
    p.on('exit', (code) => {
      const line = p.log.split(/\r?\n/).find((l) => l.startsWith('{"bots"'));
      res({ code, summary: line ? JSON.parse(line) : null, log: p.log });
    });
  });
}

async function main() {
  for (const f of ['pawbound_server', 'pawbound_bot']) if (!existsSync(exe(f))) throw new Error(`thiếu ${exe(f)}`);
  if (!existsSync(resolve(ROOT, 'dist/main.js'))) throw new Error('chưa build backend (npm run build)');
  const admin = new PrismaClient({ datasources: { db: { url: baseUrl.toString() } } });
  await admin.$executeRawUnsafe(`CREATE DATABASE "${TEMP_DB}"`);
  let backend, server;
  try {
    execSync('npx prisma migrate deploy', { cwd: ROOT, stdio: 'pipe', env });
    backend = startProc(process.execPath, ['dist/main.js'], { cwd: ROOT, env }, 'backend');
    check(await waitFor(async () => (await fetch(`${API}/health/ready`)).ok, 20000, 'backend ready'), 'backend khởi động với DB tạm');

    server = startProc(exe('pawbound_server'), ['--world', WORLD, '--data', REPO, '--port', String(GAME_PORT), '--server-id', SERVER_ID,
      '--backend', API, '--linger', '1', '--autosave', '2', '--advanced', resolve(REPO, 'tests/data/advanced_fast.json')], { cwd: REPO, env: { ...process.env, PAWBOUND_SERVICE_TOKEN: fileEnv.INTERNAL_SERVICE_TOKEN } }, 'server');

    // Người chơi: đăng ký + tạo chó
    const email = `it${Date.now()}@test.vn`;
    const reg = await api('POST', '/auth/register', { email, password: 'mat-khau-dai-123', displayName: 'Tester' });
    check(reg.status === 201, `đăng ký (HTTP ${reg.status})`);
    const auth = { Authorization: `Bearer ${reg.body.accessToken}` };
    const ch = await api('POST', '/characters', { name: 'Mực', breedId: 'shiba' }, auth);
    check(ch.status === 201, `tạo nhân vật (HTTP ${ch.status})`);

    check(await waitFor(async () => (await api('GET', '/servers', null, auth)).body.some?.((s) => s.id === SERVER_ID && s.status === 'ONLINE'), 15000, 'heartbeat'),
      'game server gửi heartbeat -> có trong /servers (ONLINE)');

    // Lần chơi 1
    const j1 = await api('POST', '/matchmaking/join', { characterId: ch.body.id, region: 'vn' }, auth);
    check(j1.status === 200 && j1.body.server?.id === SERVER_ID, `matchmaking cấp ticket cho ${SERVER_ID} (HTTP ${j1.status})`);
    const b1 = await runBot(['--ticket', j1.body.ticket, '--duration', '6']);
    check(b1.summary?.welcomed === 1, `bot vào game bằng ticket Backend (welcomed=${b1.summary?.welcomed})`);
    check(/nạp nhân vật \d+ v0 \(mới\)/.test(server.log), 'server nạp nhân vật mới (v0) từ Backend');

    const numericId = ch.body.numericId ?? (await api('GET', `/characters/${ch.body.id}`, null, auth)).body.numericId;
    // Chờ server gỡ người chơi (lưu lần cuối + đóng phiên) rồi mới đọc trạng thái
    const db = new PrismaClient({ datasources: { db: { url: tempUrl.toString() } } });
    check(await waitFor(async () => (await db.gameSession.count({ where: { serverId: SERVER_ID, endedAt: { not: null } } })) >= 1, 15000, 'end session'),
      'phiên chơi được đóng (endedAt) khi người chơi bị gỡ');
    const st1 = await api('GET', `/internal/characters/${numericId}/state`, null, svc);
    check(st1.body.stateVersion >= 1 && !!st1.body.state?.pos, `trạng thái được lưu khi chơi/rời game (stateVersion=${st1.body.stateVersion})`);
    const pos1 = st1.body.state.pos;
    check(Math.hypot(pos1.x, pos1.z) > 0, `vị trí đã lưu (${pos1.x?.toFixed?.(1)}, ${pos1.z?.toFixed?.(1)})`);

    // Sửa trạng thái "đã lưu" (giả lập lần chơi trước có đồ) -> lần vào sau server phải nạp đúng
    const edit = await api('PUT', `/internal/characters/${numericId}/state`, {
      expectedVersion: st1.body.stateVersion, idempotencyKey: `it-edit-${Date.now()}`, serverId: 'it-admin',
      state: { ...st1.body.state, vitals: { hp: 42, hunger: 50, thirst: 60, stamina: 70, rest: 80 }, inventory: [{ item: 'meat_bone', count: 2 }, null, null, null, null, null] },
    }, svc);
    check(edit.status === 200, `ghi trạng thái thử nghiệm (HTTP ${edit.status})`);
    const v2 = edit.body.stateVersion;

    const j2 = await api('POST', '/matchmaking/join', { characterId: ch.body.id, region: 'vn' }, auth);
    const b2 = await runBot(['--ticket', j2.body.ticket, '--duration', '4']);
    check(b2.summary?.welcomed === 1, 'vào game lần 2');
    check(new RegExp(`nạp nhân vật ${numericId} v${v2} \\(đã lưu\\)`).test(server.log), `server nạp bản đã lưu v${v2}`);
    let st2;
    check(await waitFor(async () => {
      st2 = await api('GET', `/internal/characters/${numericId}/state`, null, svc);
      return st2.body.stateVersion > v2;
    }, 15000, 'save lần 2'), `lưu lần 2 tăng version (${v2} -> ${st2?.body.stateVersion})`);
    const inv = st2.body.state.inventory;
    check(inv?.[0]?.item === 'meat_bone' && inv[0].count === 2, `inventory giữ nguyên qua phiên (${JSON.stringify(inv?.[0])})`);
    check(st2.body.state.vitals.hp < 60, `hp không bị reset về 100 (hp=${st2.body.state.vitals.hp.toFixed(1)})`);
    const saveOps = await db.saveOperation.count({ where: { serverId: SERVER_ID } });
    check(saveOps >= 2, `bảng SaveOperation ghi nhận ${saveOps} lần lưu (idempotency)`);

    // Ticket hợp lệ chữ ký nhưng nhân vật không có trong DB -> server từ chối (không tạo nhân vật ma)
    const bad = await runBot(['--sk', resolve(REPO, 'config/dev_keys/ticket_sk.b64'), '--char-base', '987654', '--duration', '3']);
    check(bad.summary?.welcomed === 0 && bad.summary?.rejected === 1, 'ticket nhân vật không tồn tại bị từ chối');
    check(/không nạp được nhân vật 987654: HTTP 404/.test(server.log), 'server ghi log 404 từ Backend');

    // Bị cấm chat ở Backend (admin mute) -> game server cũng chặn chat gần
    await db.user.update({ where: { email }, data: { chatMutedUntil: new Date(Date.now() + 600_000) } });
    const jm = await api('POST', '/matchmaking/join', { characterId: ch.body.id, region: 'vn' }, auth);
    const bm = await runBot(['--ticket', jm.body.ticket, '--duration', '4', '--still', '--chat', 'alo alo']);
    check(bm.summary?.welcomed === 1 && bm.summary.chat_rx === 0 && /EVENT 0 30 /.test(bm.log), 'người bị mute ở Backend không chat gần được');
    await db.user.update({ where: { email }, data: { chatMutedUntil: null } });
    await waitFor(async () => (await db.gameSession.count({ where: { serverId: SERVER_ID, endedAt: null } })) === 0, 10000, 'đóng phiên mute');

    // Chặn (Backend) áp dụng cho chat gần trên game server: B chặn A -> B không nhận tin của A, C (không chặn) vẫn nhận
    const mk = async (name) => {
      const r = await api('POST', '/auth/register', { email: `${name}${Date.now()}@test.vn`, password: 'mat-khau-dai-123', displayName: `${name}${Date.now() % 100000}` });
      const a = { Authorization: `Bearer ${r.body.accessToken}` };
      const c = await api('POST', '/characters', { name: 'Cun', breedId: 'shiba' }, a);
      const me = await api('GET', '/me', null, a);
      return { auth: a, charId: c.body.id, userId: me.body.id };
    };
    const ua = await mk('blka'), ub = await mk('blkb'), uc = await mk('blkc');
    check((await api('POST', '/blocks', { userId: ua.userId }, ub.auth)).status === 201, 'B chặn A qua API');
    const tk = async (u) => (await api('POST', '/matchmaking/join', { characterId: u.charId, region: 'vn' }, u.auth)).body.ticket;
    const [ta, tb, tc] = [await tk(ua), await tk(ub), await tk(uc)];
    const pb = runBot(['--ticket', tb, '--duration', '6', '--still', '--chat', 'b noi', '--chat-delay', '4']);
    const pc = runBot(['--ticket', tc, '--duration', '6', '--still', '--chat', 'c noi', '--chat-delay', '4']);
    await sleep(800);
    const ra = await runBot(['--ticket', ta, '--duration', '7', '--still', '--chat', 'a noi day', '--chat-delay', '1.5']);
    const [rb, rc] = await Promise.all([pb, pc]);
    const got = (r, t) => r.log.split(/\r?\n/).filter((l) => l.startsWith('CHAT ') && l.endsWith(t)).length;
    check(got(ra, 'a noi day') === 1, 'A nhận lại tin của mình');
    check(got(rc, 'a noi day') === 1, 'C (không chặn) nhận tin của A');
    check(got(rb, 'a noi day') === 0, 'B (đã chặn A) KHÔNG nhận tin của A');
    check(got(rb, 'b noi') === 1 && got(rb, 'c noi') === 1, 'B vẫn trong game, nhận tin của mình và của C (kiểm soát)');
    check(got(ra, 'b noi') === 0 && got(ra, 'c noi') === 1, 'A không nhận tin của B (chặn hai chiều) nhưng nhận tin của C (kiểm soát)');
    await waitFor(async () => (await db.gameSession.count({ where: { serverId: SERVER_ID, endedAt: null } })) === 0, 10000, 'đóng phiên block');

    // ---- Phase 14: bị bắt -> relog KHÔNG thoát; giải cứu -> thưởng vào ví đúng một lần
    const hold = /khu giữ \((-?[\d.]+), (-?[\d.]+)\)/.exec(server.log);
    check(!!hold, 'server có khu giữ');
    const hx = Number(hold[1]), hz = Number(hold[2]);
    const uv = await mk('cap'), ur = await mk('res');
    const pv = runBot(['--ticket', await tk(uv), '--duration', '30', '--still', '--print-events']);
    const vid = await waitFor(async () => /người chơi #(\d+) \(user \d+ nhân vật \d+\) vào game[^\n]*$/.test(server.log), 5000, 'victim vào');
    void vid;
    const ids = [...server.log.matchAll(/người chơi #(\d+) \(user/g)].map((m) => m[1]);
    server.stdin.write(`tp ${ids[ids.length - 1]} ${hx.toFixed(1)} ${(hz + 8).toFixed(1)}\n`);
    let victimDone;
    pv.then((r) => { victimDone = r; });
    const capturedDb = await waitFor(async () => {
      const c = await db.dogCharacter.findUnique({ where: { id: uv.charId } });
      return c?.state?.capture?.state === 2;
    }, 40000, 'bị bắt + lưu');
    check(capturedDb, 'bị bắt -> trạng thái bị bắt được lưu NGAY vào DB');
    while (!victimDone) await sleep(200);
    check(/^EVENT 0 40 /m.test(victimDone.log) && /^CAPTURE 0 2 /m.test(victimDone.log), 'nạn nhân được cảnh báo rồi mới bị bắt');
    await waitFor(async () => (await db.gameSession.count({ where: { serverId: SERVER_ID, endedAt: null } })) === 0, 10000, 'nạn nhân rời');
    // Relog: vào lại -> vẫn bị bắt (ngay snapshot đầu)
    server.stdin.write('capture off\n');
    const pv2 = runBot(['--ticket', await tk(uv), '--duration', '12', '--still', '--print-events']);
    await sleep(2500);
    const rIds = [...server.log.matchAll(/người chơi #(\d+) \(user/g)].map((m) => m[1]);
    const pr = runBot(['--ticket', await tk(ur), '--duration', '8', '--still', '--print-events', '--interact-every', '0.5']);
    await waitFor(async () => [...server.log.matchAll(/người chơi #(\d+) \(user/g)].length > rIds.length, 5000, 'người cứu vào');
    const all = [...server.log.matchAll(/người chơi #(\d+) \(user/g)].map((m) => m[1]);
    server.stdin.write(`tp ${all[all.length - 1]} ${(hx + 1.5).toFixed(1)} ${hz.toFixed(1)}\n`);
    const [rv2, rr] = await Promise.all([pv2, pr]);
    check(/^CAPTURE 0 2 /m.test(rv2.log), 'RELOG khi bị bắt: vẫn bị bắt (không thoát bằng cách thoát game)');
    check(/^EVENT 0 46 /m.test(rv2.log), 'đồng đội giải cứu thành công sau relog');
    check(/^EVENT 0 60 \+80 xương \(rescue\)/m.test(rr.log), 'người cứu nhận thông báo thưởng');
    await waitFor(async () => (await db.currencyTx.count({ where: { userId: ur.userId, reason: 'rescue' } })) >= 1, 8000, 'ví người cứu');
    const txs = await db.currencyTx.findMany({ where: { userId: ur.userId }, orderBy: { id: 'asc' } });
    const rescueTx = txs.filter((t) => t.reason === 'rescue');
    check(rescueTx.length === 1 && rescueTx[0].delta === 80, `thưởng giải cứu vào ví Backend đúng MỘT lần (+80, refKey ${rescueTx[0]?.refKey})`);
    check(txs.filter((t) => t.refKey.startsWith('quest:') && t.refKey.includes(':hero_rescue:')).length === 1, 'nhiệm vụ "giải cứu" thưởng đúng một lần');
    const bal = (await db.wallet.findUnique({ where: { userId: ur.userId } }))?.balance;
    check(bal === txs.reduce((a, t) => a + t.delta, 0), `số dư khớp sổ giao dịch (${bal})`);
    const notes = await db.notification.count({ where: { type: 'teammate_captured' } });
    check(notes === 0, 'không có đồng đội -> không ai nhận thông báo (không gửi cho người lạ)');
    server.stdin.write('capture on\n');
    await waitFor(async () => (await db.gameSession.count({ where: { serverId: SERVER_ID, endedAt: null } })) === 0, 10000, 'đóng phiên P14');

    // Tắt server có người chơi đang online: phải lưu + báo DRAINING
    const loadsBefore = (server.log.match(/\(đã lưu\)/g) || []).length;
    const j3 = await api('POST', '/matchmaking/join', { characterId: ch.body.id, region: 'vn' }, auth);
    const b3p = runBot(['--ticket', j3.body.ticket, '--duration', '8']);
    check(await waitFor(async () => (server.log.match(/\(đã lưu\)/g) || []).length > loadsBefore, 8000, 'vào lần 3'), 'vào game lần 3');
    const before = (await api('GET', `/internal/characters/${numericId}/state`, null, svc)).body.stateVersion;
    server.stdin.write('quit\n');
    const code = await new Promise((r) => server.on('exit', r));
    check(code === 0, `server thoát sạch (exit ${code})`);
    check(/đã lưu xong/.test(server.log), 'server lưu hết nhân vật trước khi tắt');
    const after = (await api('GET', `/internal/characters/${numericId}/state`, null, svc)).body.stateVersion;
    check(after > before, `trạng thái được lưu khi tắt (${before} -> ${after})`);
    const srv = await db.serverInstance.findUnique({ where: { id: SERVER_ID } });
    check(srv?.status === 'DRAINING', `server báo DRAINING khi tắt (${srv?.status})`);
    const open = await db.gameSession.count({ where: { serverId: SERVER_ID, endedAt: null } });
    check(open === 0, `không còn phiên mở (${open})`);
    await b3p;
    await db.$disconnect();
  } finally {
    if (server && server.exitCode === null) server.kill();
    if (backend) backend.kill();
    await sleep(500);
    await admin.$executeRawUnsafe(`DROP DATABASE IF EXISTS "${TEMP_DB}" WITH (FORCE)`);
    await admin.$disconnect();
    if (failures && server) console.log('--- log server ---\n' + server.log.slice(-4000));
    if (failures && backend) console.log('--- log backend ---\n' + backend.log.slice(-3000));
  }
  console.log(failures ? `\n${failures} kiểm tra THẤT BẠI` : '\nTẤT CẢ PASS');
  process.exit(failures ? 1 : 0);
}
main().catch((e) => { console.error(e); process.exit(1); });
