// Xác minh chữ ký minisign (Ed25519, chế độ prehash BLAKE2b-512 "ED" hoặc legacy "Ed") bằng crypto có sẵn của Node.
// Dùng: node tools/verify_minisign.js <file> <file.minisig> <public-key-base64>
// Exit 0 = hợp lệ, 1 = sai chữ ký, 2 = lỗi đầu vào.
'use strict';
const fs = require('fs');
const crypto = require('crypto');

function ed25519Key(raw) {
  const spkiPrefix = Buffer.from('302a300506032b6570032100', 'hex');
  return crypto.createPublicKey({ key: Buffer.concat([spkiPrefix, raw]), format: 'der', type: 'spki' });
}

try {
  const [file, sigFile, pkB64] = process.argv.slice(2);
  if (!file || !sigFile || !pkB64) throw new Error('cần <file> <file.minisig> <public-key>');
  const pk = Buffer.from(pkB64, 'base64');
  if (pk.length !== 42 || pk.toString('latin1', 0, 2) !== 'Ed') throw new Error('public key không hợp lệ');
  const keyId = pk.subarray(2, 10), key = ed25519Key(pk.subarray(10));
  const lines = fs.readFileSync(sigFile, 'utf8').split(/\r?\n/);
  const sigBlob = Buffer.from(lines[1], 'base64');
  const alg = sigBlob.toString('latin1', 0, 2);
  if (!sigBlob.subarray(2, 10).equals(keyId)) { console.error('key id không khớp'); process.exit(1); }
  const signature = sigBlob.subarray(10, 74);
  const data = fs.readFileSync(file);
  const message = alg === 'ED' ? crypto.createHash('blake2b512').update(data).digest() : data;
  const okFile = crypto.verify(null, message, key, signature);
  const trusted = lines[2].replace(/^trusted comment: /, '');
  const globalSig = Buffer.from(lines[3], 'base64');
  const okGlobal = crypto.verify(null, Buffer.concat([signature, Buffer.from(trusted, 'utf8')]), key, globalSig);
  console.log(`thuật toán ${alg}; chữ ký file: ${okFile ? 'HỢP LỆ' : 'SAI'}; chữ ký trusted comment: ${okGlobal ? 'HỢP LỆ' : 'SAI'}`);
  console.log(`trusted comment: ${trusted}`);
  process.exit(okFile && okGlobal ? 0 : 1);
} catch (e) {
  console.error('LỖI:', e.message);
  process.exit(2);
}
