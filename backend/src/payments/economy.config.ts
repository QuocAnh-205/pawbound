// Giá và cosmetic do SERVER quyết định (data/economy/*.json). Client không gửi số tiền.
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

export interface Pricing {
  guildCreation: { enabled: boolean; amount: number; currency: string };
  orderTtlS: number;
}
export interface Cosmetic {
  id: string;
  name: string;
  price: number;
  coat_color?: number;
}
export interface EconomyConfig {
  pricing: Pricing;
  cosmetics: Cosmetic[];
  earnCapPerCall: number;
}

export const ECONOMY = Symbol('ECONOMY_CONFIG');
export const GUILD_CREATION = 'GUILD_CREATION';

export function loadEconomyConfig(src: NodeJS.ProcessEnv = process.env): EconomyConfig {
  const root = src.PAWBOUND_DATA_ROOT ?? resolve(__dirname, '../../..');
  const p = JSON.parse(readFileSync(resolve(root, 'data/economy/pricing.json'), 'utf8'));
  const c = JSON.parse(readFileSync(resolve(root, 'data/economy/cosmetics.json'), 'utf8'));
  const g = p.guild_creation;
  if (!Number.isInteger(g?.amount) || g.amount <= 0 || !/^[A-Z]{3}$/.test(g.currency)) throw new Error('pricing.json: guild_creation không hợp lệ');
  if (!Number.isInteger(p.order_ttl_s) || p.order_ttl_s < 60) throw new Error('pricing.json: order_ttl_s không hợp lệ');
  const ids = new Set<string>();
  for (const it of c.items as Cosmetic[]) {
    if (!/^[a-z0-9_]{2,32}$/.test(it.id) || ids.has(it.id) || !Number.isInteger(it.price) || it.price <= 0)
      throw new Error(`cosmetics.json: item ${it.id} không hợp lệ`);
    // Chống pay-to-win: cosmetic chỉ được có trường ngoại hình
    const allowed = new Set(['id', 'name', 'price', 'coat_color']);
    for (const k of Object.keys(it)) if (!allowed.has(k)) throw new Error(`cosmetics.json: ${it.id} có trường không cho phép "${k}" (chỉ ngoại hình)`);
    ids.add(it.id);
  }
  // Tắt phí lập bang chỉ qua biến môi trường rõ ràng (dùng cho test tính năng bang không liên quan thanh toán)
  const enabled = src.PAWBOUND_GUILD_FEE === 'off' ? false : Boolean(g.enabled);
  return {
    pricing: { guildCreation: { enabled, amount: g.amount, currency: g.currency }, orderTtlS: p.order_ttl_s },
    cosmetics: c.items,
    earnCapPerCall: c.earn_cap_per_call,
  };
}
