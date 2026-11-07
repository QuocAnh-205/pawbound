// Test SAO LƯU / KHÔI PHỤC PostgreSQL thật (Phase 15).
// DB nguồn tạm -> migrate -> dữ liệu mẫu (gồm thanh toán) -> pg_dump -Fc -> pg_restore vào DB mới -> so khớp từng bảng
// (số dòng + checksum nội dung) -> trigger append-only vẫn hoạt động -> xóa cả hai DB tạm.
// Chạy: node test/backup-restore.js [thư mục bin PostgreSQL]
'use strict';
const { execFileSync, execSync } = require('node:child_process');
const { readFileSync, mkdtempSync, statSync, rmSync } = require('node:fs');
const { resolve, join } = require('node:path');
const os = require('node:os');
const { PrismaClient } = require('@prisma/client');

const ROOT = resolve(__dirname, '..');
const PGBIN = process.argv[2] || 'D:/DevTools/pgsql/bin';
const env = {};
for (const line of readFileSync(resolve(ROOT, '.env'), 'utf8').split(/\r?\n/)) {
  const m = /^([A-Z0-9_]+)=(.*)$/.exec(line.trim());
  if (m) env[m[1]] = m[2];
}
const base = new URL(env.DATABASE_URL_TEST);
const stamp = Date.now();
const SRC = `pawbound_bk_src_${stamp}`, DST = `pawbound_bk_dst_${stamp}`;
const urlOf = (db) => { const u = new URL(base.toString()); u.pathname = `/${db}`; return u.toString(); };
const pgEnv = { ...process.env, PGPASSWORD: decodeURIComponent(base.password) };
const pgArgs = ['-h', base.hostname, '-p', base.port || '5432', '-U', decodeURIComponent(base.username)];
let failures = 0;
const check = (c, m) => { console.log(`${c ? 'PASS' : 'FAIL'}  ${m}`); if (!c) failures++; };

async function tableDigest(db) {
  const tables = await db.$queryRawUnsafe(`SELECT tablename FROM pg_tables WHERE schemaname='public' ORDER BY tablename`);
  const out = {};
  for (const { tablename } of tables) {
    // Checksum toàn bộ nội dung bảng theo thứ tự ổn định
    const [r] = await db.$queryRawUnsafe(
      `SELECT count(*)::int AS n, coalesce(md5(string_agg(t::text, '|' ORDER BY t::text)), '') AS h FROM "${tablename}" t`);
    out[tablename] = `${r.n}:${r.h}`;
  }
  return out;
}

async function main() {
  const admin = new PrismaClient({ datasources: { db: { url: base.toString() } } });
  await admin.$executeRawUnsafe(`CREATE DATABASE "${SRC}"`);
  await admin.$executeRawUnsafe(`CREATE DATABASE "${DST}"`);
  const dir = mkdtempSync(join(os.tmpdir(), 'pawbound-backup-'));
  let src, dst;
  try {
    execSync('npx prisma migrate deploy', { cwd: ROOT, stdio: 'pipe', env: { ...process.env, DATABASE_URL: urlOf(SRC) } });
    src = new PrismaClient({ datasources: { db: { url: urlOf(SRC) } } });
    // Dữ liệu mẫu đa dạng (tiếng Việt, JSON, BigInt, quan hệ, sổ cái)
    for (let i = 0; i < 25; i++) {
      const u = await src.user.create({ data: { email: `bk${i}@test.vn`, passwordHash: 'x'.repeat(60),
        profile: { create: { displayName: `Chó Mực ${i}`, displayNameLower: `chó mực ${i}` } } } });
      const c = await src.dogCharacter.create({ data: { userId: u.id, name: `Vàng ${i}`, breedId: 'shiba',
        state: { pos: { x: i, y: 0, z: i }, vitals: { hp: 50 + i }, inventory: [{ item: 'meat_bone', count: 1 }] }, stateVersion: i } });
      const o = await src.paymentOrder.create({ data: { userId: u.id, purpose: 'GUILD_CREATION', amount: 100000, currency: 'VND', provider: 'mock',
        providerRef: `mock_${i}`, idempotencyKey: `key-${i}-abcdef`, status: i % 2 ? 'PAID' : 'PENDING', expiresAt: new Date() } });
      await src.paymentEvent.create({ data: { provider: 'mock', providerEventId: `evt_${i}`, orderId: o.id, type: 'payment.succeeded',
        amount: 100000, currency: 'VND', signatureValid: true, outcome: 'APPLIED', payloadSha256: 'a'.repeat(64) } });
      await src.auditLog.create({ data: { actorType: 'system', action: 'test', target: c.id, metadata: { i } } });
    }
    const before = await tableDigest(src);
    // ---- SAO LƯU (định dạng custom, nén) — giống scripts/db-backup.ps1
    const file = join(dir, 'pawbound.dump');
    const t0 = Date.now();
    execFileSync(join(PGBIN, 'pg_dump'), [...pgArgs, '-Fc', '-Z', '6', '-f', file, SRC], { env: pgEnv });
    check(statSync(file).size > 1000, `pg_dump tạo bản sao lưu (${statSync(file).size} byte, ${Date.now() - t0} ms)`);
    // ---- KHÔI PHỤC vào DB rỗng khác
    const t1 = Date.now();
    execFileSync(join(PGBIN, 'pg_restore'), [...pgArgs, '--no-owner', '--exit-on-error', '-d', DST, file], { env: pgEnv });
    check(true, `pg_restore hoàn tất (${Date.now() - t1} ms)`);
    dst = new PrismaClient({ datasources: { db: { url: urlOf(DST) } } });
    const after = await tableDigest(dst);
    const diff = Object.keys(before).filter((t) => before[t] !== after[t]);
    check(Object.keys(before).length >= 25, `so khớp ${Object.keys(before).length} bảng`);
    check(diff.length === 0, diff.length ? `bảng lệch: ${diff.join(', ')}` : 'MỌI bảng khớp số dòng + checksum nội dung');
    check(after._prisma_migrations === before._prisma_migrations, 'lịch sử migration được khôi phục (migrate deploy tiếp theo hoạt động)');
    // Ràng buộc/trigger khôi phục đúng
    let blocked = false;
    try { await dst.$executeRawUnsafe(`DELETE FROM "PaymentEvent"`); } catch { blocked = true; }
    check(blocked, 'trigger append-only của sổ cái thanh toán vẫn hoạt động sau khôi phục');
    let dup = false;
    try { await dst.user.create({ data: { email: 'bk0@test.vn', passwordHash: 'x' } }); } catch { dup = true; }
    check(dup, 'ràng buộc UNIQUE vẫn hoạt động sau khôi phục');
    // Sequence/BigInt tiếp tục đúng (không trùng id sau khôi phục)
    const nu = await dst.user.create({ data: { email: 'new@test.vn', passwordHash: 'x' } });
    const maxOld = (await dst.user.findMany({ where: { email: { not: 'new@test.vn' } }, orderBy: { numericId: 'desc' }, take: 1 }))[0].numericId;
    check(nu.numericId > maxOld, 'sequence numericId tiếp tục đúng sau khôi phục');
    execSync('npx prisma migrate deploy', { cwd: ROOT, stdio: 'pipe', env: { ...process.env, DATABASE_URL: urlOf(DST) } });
    check(true, 'prisma migrate deploy trên DB đã khôi phục: không có migration lỗi');
  } finally {
    await src?.$disconnect();
    await dst?.$disconnect();
    await admin.$executeRawUnsafe(`DROP DATABASE IF EXISTS "${SRC}" WITH (FORCE)`);
    await admin.$executeRawUnsafe(`DROP DATABASE IF EXISTS "${DST}" WITH (FORCE)`);
    await admin.$disconnect();
    rmSync(dir, { recursive: true, force: true });
  }
  console.log(failures ? `${failures} kiểm tra THẤT BẠI` : 'TẤT CẢ PASS');
  process.exit(failures ? 1 : 0);
}
main().catch((e) => { console.error(e); process.exit(1); });
