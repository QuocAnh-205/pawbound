// Cấu hình xã hội: MỘT nguồn duy nhất (data/social/social.json), không hard-code giới hạn ở nhiều module.
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

export interface SocialConfig {
  friends: { max: number; request_ttl_s: number };
  party: { max_members: number; invite_ttl_s: number };
  guild: { member_limit: number; invite_ttl_s: number; name_min: number; name_max: number; description_max: number; emblem_count: number };
  chat: { max_length: number; rate_window_s: number; rate_max_messages: number; history_limit: number; banned_words: string[] };
  notifications: { ttl_s: number; max_list: number };
}

export const SOCIAL = Symbol('SOCIAL_CONFIG');

export function loadSocialConfig(): SocialConfig {
  const file = resolve(process.env.PAWBOUND_DATA_ROOT ?? resolve(__dirname, '../../..'), 'data/social/social.json');
  const c = JSON.parse(readFileSync(file, 'utf8')) as SocialConfig;
  const pos = (v: unknown, name: string, max = 1e9) => {
    if (!Number.isInteger(v) || (v as number) < 1 || (v as number) > max) throw new Error(`social.json: ${name} không hợp lệ`);
  };
  pos(c.friends.max, 'friends.max', 1000);
  pos(c.party.max_members, 'party.max_members', 16);
  pos(c.guild.member_limit, 'guild.member_limit', 500);
  pos(c.guild.name_min, 'guild.name_min', 64);
  pos(c.guild.name_max, 'guild.name_max', 64);
  pos(c.chat.max_length, 'chat.max_length', 500);
  pos(c.chat.rate_max_messages, 'chat.rate_max_messages', 1000);
  for (const k of ['request_ttl_s'] as const) pos(c.friends[k], `friends.${k}`);
  pos(c.party.invite_ttl_s, 'party.invite_ttl_s');
  pos(c.guild.invite_ttl_s, 'guild.invite_ttl_s');
  pos(c.notifications.ttl_s, 'notifications.ttl_s');
  if (c.guild.name_min > c.guild.name_max) throw new Error('social.json: guild.name_min > name_max');
  return c;
}

export const secondsFromNow = (s: number) => new Date(Date.now() + s * 1000);
