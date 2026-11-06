import { generateKeyPairSync } from 'node:crypto';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { validateState } from '../game/game';
import { TicketService } from './ticket.service';

// Tạo secret key định dạng libsodium (seed 32 + public 32) từ cặp khóa Node
function libsodiumSecret() {
  const { privateKey, publicKey } = generateKeyPairSync('ed25519');
  const seed = privateKey.export({ format: 'der', type: 'pkcs8' }).subarray(16);
  const pub = publicKey.export({ format: 'der', type: 'spki' }).subarray(12);
  const dir = mkdtempSync(join(tmpdir(), 'pb-'));
  const file = join(dir, 'sk.b64');
  writeFileSync(file, Buffer.concat([seed, pub]).toString('base64url'));
  return file;
}

describe('TicketService', () => {
  const svc = new TicketService({ ticketSkFile: libsodiumSecret(), ticketTtlS: 60 } as never);

  it('ký và xác minh ticket; payload đúng định dạng C++ mong đợi', () => {
    const { ticket, claims } = svc.issue(7n, 42n, 'dev-1', 1000);
    expect(ticket.split('.')).toHaveLength(2);
    expect(claims.exp).toBe(1060);
    const v = svc.verify(ticket);
    expect(v).toMatchObject({ v: 1, sub: 7, chr: 42, srv: 'dev-1', exp: 1060 });
  });

  it('ticket bị sửa payload hoặc chữ ký đều không qua', () => {
    const { ticket } = svc.issue(1n, 1n, 'dev-1');
    const [p, s] = ticket.split('.');
    const forged = Buffer.from(JSON.stringify({ v: 1, sub: 999, chr: 1, srv: 'dev-1', exp: 9e9, jti: 'x' })).toString('base64url');
    expect(svc.verify(`${forged}.${s}`)).toBeNull();
    expect(svc.verify(`${p}.${s.slice(0, -2)}AA`)).toBeNull();
  });

  it('mỗi ticket có jti khác nhau (dùng một lần)', () => {
    const a = svc.issue(1n, 1n, 'dev-1').claims.jti;
    const b = svc.issue(1n, 1n, 'dev-1').claims.jti;
    expect(a).not.toBe(b);
  });
});

describe('validateState', () => {
  const ok = { pos: { x: 10, y: 0, z: 10 }, vitals: { hp: 100, hunger: 50, thirst: 50, stamina: 80, rest: 90 }, inventory: [null, { item: 'scrap_food', count: 2 }] };
  it('nhận trạng thái hợp lệ', () => expect(validateState(ok)).toBeNull());
  it('từ chối HP ngoài khoảng, item lạ, túi quá 6 ô, số lượng âm', () => {
    expect(validateState({ ...ok, vitals: { ...ok.vitals, hp: 1e9 } })).not.toBeNull();
    expect(validateState({ ...ok, inventory: [{ item: 'GOLD!!', count: 1 }] })).not.toBeNull();
    expect(validateState({ ...ok, inventory: new Array(7).fill(null) })).not.toBeNull();
    expect(validateState({ ...ok, inventory: [{ item: 'scrap_food', count: -5 }] })).not.toBeNull();
    expect(validateState({ ...ok, pos: { x: NaN, y: 0, z: 0 } })).not.toBeNull();
  });
});
