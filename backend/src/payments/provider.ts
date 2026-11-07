// Giao diện cổng thanh toán: domain (bang hội, entitlement) KHÔNG phụ thuộc nhà cung cấp cụ thể.
// Thêm cổng thật = viết một class implements PaymentProvider + đăng ký trong payments.module — không sửa logic order/guild.
import { createHash, createHmac, randomBytes, timingSafeEqual } from 'node:crypto';

export type WebhookStatus = 'PAID' | 'FAILED' | 'CANCELLED' | 'REFUNDED';

// Sự kiện đã được CHUẨN HÓA sau khi xác minh chữ ký. Mọi trường đến từ provider — backend vẫn đối chiếu với order trong DB.
export interface WebhookEvent {
  eventId: string;
  type: string;
  status: WebhookStatus;
  reference: string; // mã order phía mình (merchant reference)
  providerRef: string;
  amount: number;
  currency: string;
  occurredAt: number; // unix giây
}

export interface CheckoutSession {
  providerRef: string;
  checkoutUrl: string;
}

export type VerifyResult = { ok: true; event: WebhookEvent } | { ok: false; reason: string };

export interface PaymentProvider {
  readonly name: string;
  createCheckout(order: { id: string; amount: number; currency: string; description: string }): Promise<CheckoutSession>;
  // rawBody: đúng byte nhận được (không parse lại JSON trước khi kiểm chữ ký).
  verifyWebhook(rawBody: Buffer, headers: Record<string, string | string[] | undefined>, nowS: number): VerifyResult;
}

export const PAYMENT_PROVIDER = Symbol('PAYMENT_PROVIDER');
export const sha256hex = (b: Buffer | string) => createHash('sha256').update(b).digest('hex');

// ============================================================================ Mock (DEV/TEST) — không có tiền thật
// Chữ ký: header X-Mock-Signature: t=<unix>,v1=<hex HMAC-SHA256(secret, `${t}.${rawBody}`)>; lệch giờ > 300 s = từ chối (chống replay).
export class MockPaymentProvider implements PaymentProvider {
  readonly name = 'mock';
  static readonly TOLERANCE_S = 300;
  constructor(private readonly secret: string) {}

  async createCheckout(order: { id: string }): Promise<CheckoutSession> {
    const providerRef = `mock_${randomBytes(12).toString('hex')}`;
    // Trang "thanh toán" giả lập (chỉ tồn tại khi provider = mock)
    return { providerRef, checkoutUrl: `/api/v1/payments/mock/checkout/${providerRef}?order=${order.id}` };
  }

  sign(rawBody: string, t: number) {
    return `t=${t},v1=${createHmac('sha256', this.secret).update(`${t}.${rawBody}`).digest('hex')}`;
  }

  verifyWebhook(rawBody: Buffer, headers: Record<string, string | string[] | undefined>, nowS: number): VerifyResult {
    const h = headers['x-mock-signature'];
    if (typeof h !== 'string') return { ok: false, reason: 'thiếu chữ ký' };
    const parts = Object.fromEntries(h.split(',').map((kv) => kv.split('=', 2) as [string, string]));
    const t = Number(parts.t);
    if (!Number.isInteger(t) || !/^[0-9a-f]{64}$/.test(parts.v1 ?? '')) return { ok: false, reason: 'chữ ký sai định dạng' };
    if (Math.abs(nowS - t) > MockPaymentProvider.TOLERANCE_S) return { ok: false, reason: 'chữ ký quá hạn (replay?)' };
    const expect = createHmac('sha256', this.secret).update(`${t}.`).update(rawBody).digest();
    const got = Buffer.from(parts.v1, 'hex');
    if (got.length !== expect.length || !timingSafeEqual(got, expect)) return { ok: false, reason: 'chữ ký không khớp' };
    let j: Record<string, unknown>;
    try {
      j = JSON.parse(rawBody.toString('utf8'));
    } catch {
      return { ok: false, reason: 'payload không phải JSON' };
    }
    const d = (j.data ?? {}) as Record<string, unknown>;
    const map: Record<string, WebhookStatus> = { 'payment.succeeded': 'PAID', 'payment.failed': 'FAILED', 'payment.cancelled': 'CANCELLED', 'payment.refunded': 'REFUNDED' };
    const status = map[String(j.type)];
    if (typeof j.id !== 'string' || !status || typeof d.reference !== 'string' || typeof d.providerRef !== 'string' ||
        !Number.isInteger(d.amount) || typeof d.currency !== 'string' || !Number.isInteger(j.created))
      return { ok: false, reason: 'payload thiếu trường' };
    return { ok: true, event: { eventId: j.id, type: String(j.type), status, reference: d.reference, providerRef: d.providerRef,
                                amount: d.amount as number, currency: d.currency, occurredAt: j.created as number } };
  }

  // Dùng cho trang checkout giả lập và test: dựng một webhook đã ký như provider thật sẽ gửi.
  buildWebhook(e: { id?: string; type: string; reference: string; providerRef: string; amount: number; currency: string }, nowS: number) {
    const body = JSON.stringify({ id: e.id ?? `evt_${randomBytes(10).toString('hex')}`, type: e.type, created: nowS,
                                  data: { reference: e.reference, providerRef: e.providerRef, amount: e.amount, currency: e.currency } });
    return { body, signature: this.sign(body, nowS) };
  }
}
