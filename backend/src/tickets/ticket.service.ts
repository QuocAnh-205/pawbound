import { Inject, Injectable } from '@nestjs/common';
import { createPrivateKey, createPublicKey, KeyObject, randomBytes, sign, verify } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { APP_ENV, AppEnv } from '../config/env';

// Session ticket tương thích src/net/ticket.cpp (C++ game server, libsodium):
//   base64url(payload JSON) "." base64url(Ed25519(chuỗi payload base64url))
// Khóa bí mật đọc từ file định dạng libsodium (64 byte: seed 32 + public 32). Dùng crypto có sẵn của Node — không tự viết thuật toán.
export interface TicketClaims {
  sub: number; // user numericId
  chr: number; // character numericId
  srv: string;
  exp: number; // unix giây
  jti: string;
}

const PKCS8_ED25519_PREFIX = Buffer.from('302e020100300506032b657004220420', 'hex');
const SPKI_ED25519_PREFIX = Buffer.from('302a300506032b6570032100', 'hex');

export function keysFromLibsodiumSecret(b64url: string): { priv: KeyObject; pub: KeyObject } {
  const raw = Buffer.from(b64url.trim(), 'base64url');
  if (raw.length !== 64) throw new Error('secret key libsodium phải dài 64 byte');
  const priv = createPrivateKey({ key: Buffer.concat([PKCS8_ED25519_PREFIX, raw.subarray(0, 32)]), format: 'der', type: 'pkcs8' });
  const pub = createPublicKey({ key: Buffer.concat([SPKI_ED25519_PREFIX, raw.subarray(32)]), format: 'der', type: 'spki' });
  return { priv, pub };
}

@Injectable()
export class TicketService {
  private readonly priv: KeyObject;
  private readonly pub: KeyObject;

  constructor(@Inject(APP_ENV) private readonly env: AppEnv) {
    const k = keysFromLibsodiumSecret(readFileSync(env.ticketSkFile, 'utf8'));
    this.priv = k.priv;
    this.pub = k.pub;
  }

  issue(userNumericId: bigint, characterNumericId: bigint, serverId: string, nowS = Math.floor(Date.now() / 1000)) {
    const claims: TicketClaims = {
      sub: Number(userNumericId),
      chr: Number(characterNumericId),
      srv: serverId,
      exp: nowS + this.env.ticketTtlS,
      jti: randomBytes(16).toString('base64url'),
    };
    const p64 = Buffer.from(JSON.stringify({ v: 1, ...claims }), 'utf8').toString('base64url');
    const sig = sign(null, Buffer.from(p64, 'utf8'), this.priv).toString('base64url');
    return { ticket: `${p64}.${sig}`, claims };
  }

  // Dùng trong test để kiểm tra chữ ký (game server dùng bản C++).
  verify(ticket: string): TicketClaims | null {
    const [p64, s64] = ticket.split('.');
    if (!p64 || !s64) return null;
    if (!verify(null, Buffer.from(p64, 'utf8'), this.pub, Buffer.from(s64, 'base64url'))) return null;
    return JSON.parse(Buffer.from(p64, 'base64url').toString('utf8')) as TicketClaims;
  }
}
