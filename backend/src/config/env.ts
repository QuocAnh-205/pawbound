import { existsSync } from 'node:fs';
import { resolve } from 'node:path';

// Cấu hình đã kiểm tra. Thiếu/yếu -> lỗi khởi động rõ ràng (không chạy với secret rỗng).
export interface AppEnv {
  databaseUrl: string;
  port: number;
  jwtSecret: string;
  accessTtlS: number;
  refreshTtlS: number;
  ticketSkFile: string;
  ticketTtlS: number;
  serviceToken: string;
  appEnv: 'development' | 'test' | 'production';
  paymentProvider: string;
  mockWebhookSecret: string;
}

export function loadEnv(src: NodeJS.ProcessEnv = process.env): AppEnv {
  const errors: string[] = [];
  const need = (k: string) => {
    const v = src[k];
    if (!v) errors.push(`thiếu biến môi trường ${k}`);
    return v ?? '';
  };
  const num = (k: string, def: number, min: number, max: number) => {
    const raw = src[k];
    const v = raw === undefined ? def : Number(raw);
    if (!Number.isInteger(v) || v < min || v > max) errors.push(`${k} phải là số nguyên trong [${min}, ${max}]`);
    return v;
  };
  const env: AppEnv = {
    databaseUrl: need('DATABASE_URL'),
    port: num('PORT', 3000, 1, 65535),
    jwtSecret: need('JWT_SECRET'),
    accessTtlS: num('ACCESS_TOKEN_TTL_S', 900, 60, 86400),
    refreshTtlS: num('REFRESH_TOKEN_TTL_S', 2592000, 3600, 31536000),
    ticketSkFile: resolve(need('TICKET_SK_FILE')),
    ticketTtlS: num('TICKET_TTL_S', 60, 10, 600),
    serviceToken: need('INTERNAL_SERVICE_TOKEN'),
    appEnv: (src.APP_ENV ?? 'development') as AppEnv['appEnv'],
    paymentProvider: src.PAYMENT_PROVIDER ?? 'mock',
    mockWebhookSecret: src.MOCK_PAYMENT_WEBHOOK_SECRET ?? '',
  };
  if (!['development', 'test', 'production'].includes(env.appEnv)) errors.push('APP_ENV phải là development | test | production');
  if (env.paymentProvider !== 'mock') errors.push(`PAYMENT_PROVIDER=${env.paymentProvider} chưa được hỗ trợ (chỉ có mock — xem docs/payments)`);
  // Mock thanh toán KHÔNG BAO GIỜ chạy ở production: nó cho phép "trả tiền" giả.
  if (env.appEnv === 'production' && env.paymentProvider === 'mock') errors.push('không được dùng PAYMENT_PROVIDER=mock khi APP_ENV=production');
  if (env.paymentProvider === 'mock' && (env.mockWebhookSecret.length < 32 || env.mockWebhookSecret.includes('CHANGE_ME')))
    errors.push('MOCK_PAYMENT_WEBHOOK_SECRET phải là chuỗi ngẫu nhiên >= 32 ký tự');
  if (env.jwtSecret && (env.jwtSecret.length < 32 || env.jwtSecret.includes('CHANGE_ME')))
    errors.push('JWT_SECRET phải là chuỗi ngẫu nhiên >= 32 ký tự (không dùng giá trị mẫu)');
  if (env.serviceToken && (env.serviceToken.length < 32 || env.serviceToken.includes('CHANGE_ME')))
    errors.push('INTERNAL_SERVICE_TOKEN phải là chuỗi ngẫu nhiên >= 32 ký tự');
  if (src.TICKET_SK_FILE && !existsSync(env.ticketSkFile))
    errors.push(`không thấy TICKET_SK_FILE ${env.ticketSkFile} (tạo bằng: pawbound net keygen)`);
  if (errors.length) throw new Error('Cấu hình không hợp lệ:\n - ' + errors.join('\n - '));
  return env;
}

export const APP_ENV = Symbol('APP_ENV');
