import { Body, ConflictException, Controller, DefaultValuePipe, ForbiddenException, Get, HttpCode, HttpException, HttpStatus, Inject,
  Injectable, NotFoundException, OnModuleDestroy, OnModuleInit, Param, ParseUUIDPipe, Post, Query, Req, UnauthorizedException,
  UnprocessableEntityException, UseGuards } from '@nestjs/common';
import { ApiBearerAuth, ApiExcludeEndpoint, ApiProperty, ApiTags } from '@nestjs/swagger';
import { Throttle } from '@nestjs/throttler';
import { Prisma } from '@prisma/client';
import { IsIn, IsString, Matches } from 'class-validator';
import { AuditService } from '../audit/audit.service';
import { AuthUser, CurrentUser, JwtAuthGuard, Roles, RolesGuard } from '../common/security';
import { APP_ENV, AppEnv } from '../config/env';
import { PrismaService } from '../prisma/prisma.service';
import { isUniqueViolation } from '../social/notifications';
import { ECONOMY, EconomyConfig, GUILD_CREATION } from './economy.config';
import { MockPaymentProvider, PAYMENT_PROVIDER, PaymentProvider, sha256hex, WebhookEvent } from './provider';

type Tx = Prisma.TransactionClient;
const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

export class CreateOrderDto {
  @ApiProperty({ enum: [GUILD_CREATION] }) @IsIn([GUILD_CREATION]) purpose: string;
  @ApiProperty({ description: 'Khóa idempotency do client sinh (bấm "Thanh toán" hai lần -> một order)' })
  @IsString() @Matches(/^[A-Za-z0-9_-]{8,64}$/) idempotencyKey: string;
}
export class MockCompleteDto {
  @ApiProperty({ enum: ['success', 'fail', 'cancel'] }) @IsIn(['success', 'fail', 'cancel']) outcome: string;
}

// Kết quả xử lý webhook. REJECTED_* -> HTTP 4xx; còn lại 200 (provider ngừng gửi lại).
type Outcome = 'APPLIED' | 'APPLIED_LATE' | 'APPLIED_REFUND_GUILD_KEPT' | 'DUPLICATE' | 'IGNORED_ALREADY_PAID' | 'IGNORED_TRANSITION' |
  'REJECTED_SIGNATURE' | 'REJECTED_UNKNOWN_ORDER' | 'REJECTED_PROVIDER_REF' | 'REJECTED_AMOUNT' | 'REJECTED_CURRENCY';

const view = (o: { id: string; purpose: string; amount: number; currency: string; status: string; checkoutUrl: string | null;
                   expiresAt: Date; paidAt: Date | null; createdAt: Date }) =>
  ({ id: o.id, purpose: o.purpose, amount: o.amount, currency: o.currency, status: o.status, checkoutUrl: o.status === 'PENDING' ? o.checkoutUrl : null,
     expiresAt: o.expiresAt, paidAt: o.paidAt, createdAt: o.createdAt });

@Injectable()
export class PaymentsService implements OnModuleInit, OnModuleDestroy {
  private timer?: NodeJS.Timeout;
  constructor(private readonly db: PrismaService, private readonly audit: AuditService, @Inject(ECONOMY) private readonly eco: EconomyConfig,
              @Inject(PAYMENT_PROVIDER) private readonly provider: PaymentProvider) {}

  onModuleInit() {
    this.timer = setInterval(() => void this.expireStale().catch(() => undefined), 60_000);
    this.timer.unref();
  }
  onModuleDestroy() {
    if (this.timer) clearInterval(this.timer);
  }

  async expireStale() {
    const r = await this.db.paymentOrder.updateMany({ where: { status: 'PENDING', expiresAt: { lt: new Date() } }, data: { status: 'EXPIRED' } });
    return r.count;
  }

  async createOrder(userId: string, dto: CreateOrderDto) {
    const price = this.eco.pricing.guildCreation;
    if (!price.enabled) throw new UnprocessableEntityException({ code: 'FEE_DISABLED', message: 'Hiện không thu phí lập bang' });
    const ex = await this.db.paymentOrder.findUnique({ where: { userId_idempotencyKey: { userId, idempotencyKey: dto.idempotencyKey } } });
    if (ex) return view(ex);  // gửi lại cùng khóa -> cùng order
    if (await this.db.guildMember.findUnique({ where: { userId } }))
      throw new ConflictException({ code: 'ALREADY_IN_GUILD', message: 'Bạn đã ở trong một bang — không cần trả phí lập bang' });
    const ent = await this.db.entitlement.findFirst({ where: { userId, kind: GUILD_CREATION, status: 'AVAILABLE' } });
    if (ent) throw new ConflictException({ code: 'ENTITLEMENT_AVAILABLE', message: 'Bạn đã có quyền lập bang chưa dùng', entitlementId: ent.id });
    let order;
    try {
      // Số tiền lấy từ cấu hình server, KHÔNG từ request
      order = await this.db.paymentOrder.create({
        data: { userId, purpose: GUILD_CREATION, amount: price.amount, currency: price.currency, provider: this.provider.name,
                idempotencyKey: dto.idempotencyKey, expiresAt: new Date(Date.now() + this.eco.pricing.orderTtlS * 1000) },
      });
    } catch (e) {
      if (isUniqueViolation(e)) {
        const again = await this.db.paymentOrder.findUnique({ where: { userId_idempotencyKey: { userId, idempotencyKey: dto.idempotencyKey } } });
        if (again) return view(again);
      }
      throw e;
    }
    const cs = await this.provider.createCheckout({ id: order.id, amount: order.amount, currency: order.currency, description: 'PAWBOUND - phí lập bang hội' });
    order = await this.db.paymentOrder.update({ where: { id: order.id }, data: { providerRef: cs.providerRef, checkoutUrl: cs.checkoutUrl } });
    await this.audit.record('user', userId, 'payment.order_create', order.id, { amount: order.amount, currency: order.currency, purpose: order.purpose });
    return view(order);
  }

  async getOrder(userId: string, id: string) {
    await this.expireStale();
    const o = await this.db.paymentOrder.findFirst({ where: { id, userId }, include: { entitlement: true } });
    if (!o) throw new NotFoundException({ code: 'ORDER_NOT_FOUND', message: 'Không tìm thấy đơn' });
    return { ...view(o), entitlement: o.entitlement ? { id: o.entitlement.id, status: o.entitlement.status, guildId: o.entitlement.guildId } : null };
  }

  // Điểm vào DUY NHẤT làm thay đổi trạng thái tiền: webhook đã xác minh chữ ký (và hoàn tiền của admin qua provider).
  async handleWebhook(providerName: string, rawBody: Buffer | undefined, headers: Record<string, string | string[] | undefined>) {
    if (providerName !== this.provider.name) throw new NotFoundException({ code: 'PROVIDER_NOT_FOUND', message: 'Không có provider này' });
    if (!rawBody?.length) throw new HttpException({ code: 'EMPTY_BODY', message: 'Thiếu body' }, HttpStatus.BAD_REQUEST);
    const payloadSha256 = sha256hex(rawBody);
    const v = this.provider.verifyWebhook(rawBody, headers, Math.floor(Date.now() / 1000));
    if (!v.ok) {
      await this.db.paymentEvent.create({ data: { provider: this.provider.name, type: 'unverified', signatureValid: false, outcome: 'REJECTED_SIGNATURE', payloadSha256 } });
      throw new UnauthorizedException({ code: 'WEBHOOK_SIGNATURE_INVALID', message: v.reason });
    }
    const e = v.event;
    let outcome: Outcome;
    try {
      outcome = await this.db.$transaction(async (tx) => {
        if (await tx.paymentEvent.findUnique({ where: { provider_providerEventId: { provider: this.provider.name, providerEventId: e.eventId } } }))
          return 'DUPLICATE' as const;
        const out = await this.apply(tx, e);
        await tx.paymentEvent.create({
          data: { provider: this.provider.name, providerEventId: e.eventId, orderId: out.orderId, type: e.type, amount: e.amount, currency: e.currency,
                  signatureValid: true, outcome: out.outcome, payloadSha256 },
        });
        return out.outcome;
      });
    } catch (err) {
      if (isUniqueViolation(err)) outcome = 'DUPLICATE';  // hai bản webhook giống nhau đến cùng lúc
      else throw err;
    }
    if (outcome.startsWith('REJECTED'))
      throw new UnprocessableEntityException({ code: outcome, message: 'Webhook không khớp đơn hàng — đã ghi nhận để đối soát' });
    return { received: true, outcome };
  }

  private async apply(tx: Tx, e: WebhookEvent): Promise<{ outcome: Outcome; orderId: string | null }> {
    if (!UUID.test(e.reference)) return { outcome: 'REJECTED_UNKNOWN_ORDER', orderId: null };
    const rows = await tx.$queryRaw<{ id: string }[]>`SELECT id FROM "PaymentOrder" WHERE id = ${e.reference}::uuid FOR UPDATE`;
    if (!rows.length) return { outcome: 'REJECTED_UNKNOWN_ORDER', orderId: null };
    const o = await tx.paymentOrder.findUniqueOrThrow({ where: { id: e.reference }, include: { entitlement: true } });
    const reject = async (outcome: Outcome) => {
      await this.audit.record('system', null, 'payment.webhook_rejected', o.id, { outcome, amount: e.amount, currency: e.currency }, tx);
      return { outcome, orderId: o.id };
    };
    if (o.provider !== this.provider.name || o.providerRef !== e.providerRef) return reject('REJECTED_PROVIDER_REF');
    if (e.amount !== o.amount) return reject('REJECTED_AMOUNT');
    if (e.currency !== o.currency) return reject('REJECTED_CURRENCY');
    const now = new Date();
    switch (e.status) {
      case 'PAID': {
        if (o.status === 'PAID') return { outcome: 'IGNORED_ALREADY_PAID', orderId: o.id };
        if (o.status === 'REFUNDED') return { outcome: 'IGNORED_TRANSITION', orderId: o.id };
        // Tiền đã thực sự vào (kể cả đến muộn sau EXPIRED/FAILED/CANCELLED) -> phải ghi nhận cho người chơi
        const late = o.status !== 'PENDING' || o.expiresAt < now;
        await tx.paymentOrder.update({ where: { id: o.id }, data: { status: 'PAID', paidAt: now } });
        await tx.entitlement.createMany({ data: [{ userId: o.userId, kind: o.purpose, orderId: o.id }], skipDuplicates: true });
        await this.audit.record('system', null, 'payment.paid', o.id, { amount: o.amount, currency: o.currency, late }, tx);
        return { outcome: late ? 'APPLIED_LATE' : 'APPLIED', orderId: o.id };
      }
      case 'FAILED':
      case 'CANCELLED':
        if (o.status !== 'PENDING') return { outcome: 'IGNORED_TRANSITION', orderId: o.id };
        await tx.paymentOrder.update({ where: { id: o.id }, data: { status: e.status } });
        return { outcome: 'APPLIED', orderId: o.id };
      case 'REFUNDED': {
        if (o.status !== 'PAID') return { outcome: 'IGNORED_TRANSITION', orderId: o.id };
        await tx.paymentOrder.update({ where: { id: o.id }, data: { status: 'REFUNDED' } });
        let outcome: Outcome = 'APPLIED';
        if (o.entitlement?.status === 'AVAILABLE') await tx.entitlement.update({ where: { id: o.entitlement.id }, data: { status: 'REVOKED' } });
        else if (o.entitlement?.status === 'CONSUMED') outcome = 'APPLIED_REFUND_GUILD_KEPT';  // bang đã lập: để admin xem xét, không tự xóa bang của người khác
        await this.audit.record('system', null, 'payment.refunded', o.id, { outcome }, tx);
        return { outcome, orderId: o.id };
      }
    }
  }

  // ---------------------------------------------------------------- mock: giả lập người dùng thanh toán tại trang provider
  async mockComplete(userId: string, providerRef: string, outcome: string) {
    const mock = this.mock();
    const o = await this.db.paymentOrder.findFirst({ where: { providerRef, userId } });
    if (!o) throw new NotFoundException({ code: 'ORDER_NOT_FOUND', message: 'Không tìm thấy đơn' });
    const type = outcome === 'success' ? 'payment.succeeded' : outcome === 'fail' ? 'payment.failed' : 'payment.cancelled';
    const w = mock.buildWebhook({ type, reference: o.id, providerRef, amount: o.amount, currency: o.currency }, Math.floor(Date.now() / 1000));
    // Đi qua ĐÚNG đường xử lý webhook (kiểm chữ ký, đối chiếu số tiền...) như provider thật gọi vào
    return this.handleWebhook(mock.name, Buffer.from(w.body), { 'x-mock-signature': w.signature });
  }

  async refund(adminId: string, orderId: string) {
    const mock = this.mock();
    const o = await this.db.paymentOrder.findUnique({ where: { id: orderId } });
    if (!o) throw new NotFoundException({ code: 'ORDER_NOT_FOUND', message: 'Không tìm thấy đơn' });
    if (o.status !== 'PAID') throw new UnprocessableEntityException({ code: 'NOT_REFUNDABLE', message: `Đơn đang ${o.status}` });
    await this.audit.record('admin', adminId, 'payment.refund_requested', o.id);
    // Provider thật: gọi API hoàn tiền, trạng thái đổi khi provider gửi webhook refunded. Mock: tự phát webhook đó.
    const w = mock.buildWebhook({ type: 'payment.refunded', reference: o.id, providerRef: o.providerRef!, amount: o.amount, currency: o.currency },
                                Math.floor(Date.now() / 1000));
    return this.handleWebhook(mock.name, Buffer.from(w.body), { 'x-mock-signature': w.signature });
  }

  private mock() {
    if (!(this.provider instanceof MockPaymentProvider)) throw new NotFoundException({ code: 'NOT_FOUND', message: 'Không hỗ trợ' });
    return this.provider;
  }

  async report(from?: Date, to?: Date) {
    const where = { createdAt: { gte: from ?? new Date(0), lte: to ?? new Date() } };
    const by = await this.db.paymentOrder.groupBy({ by: ['status', 'currency'], where, _count: { _all: true }, _sum: { amount: true } });
    const rejected = await this.db.paymentEvent.groupBy({ by: ['outcome'], where: { receivedAt: where.createdAt }, _count: { _all: true } });
    return { from: where.createdAt.gte, to: where.createdAt.lte,
             orders: by.map((b) => ({ status: b.status, currency: b.currency, count: b._count._all, amount: b._sum.amount ?? 0 })),
             webhookOutcomes: rejected.map((r) => ({ outcome: r.outcome, count: r._count._all })) };
  }
}

@ApiTags('payments')
@Controller({ path: 'payments', version: '1' })
export class PaymentsController {
  constructor(private readonly svc: PaymentsService, private readonly db: PrismaService, @Inject(APP_ENV) private readonly env: AppEnv) {}

  @Post('orders')
  @ApiBearerAuth()
  @UseGuards(JwtAuthGuard)
  @Throttle({ default: { limit: () => Number(process.env.THROTTLE_ORDER_PER_MIN ?? 10), ttl: 60_000 } })
  create(@CurrentUser() u: AuthUser, @Body() d: CreateOrderDto) {
    return this.svc.createOrder(u.id, d);
  }

  @Get('orders')
  @ApiBearerAuth()
  @UseGuards(JwtAuthGuard)
  async list(@CurrentUser() u: AuthUser) {
    await this.svc.expireStale();
    return (await this.db.paymentOrder.findMany({ where: { userId: u.id }, orderBy: { createdAt: 'desc' }, take: 20 })).map(view);
  }

  @Get('orders/:id')
  @ApiBearerAuth()
  @UseGuards(JwtAuthGuard)
  get(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    return this.svc.getOrder(u.id, id);
  }

  // Trang người dùng quay về sau checkout: CHỈ hiển thị trạng thái, KHÔNG bao giờ đánh dấu đã trả (query có ghi gì cũng vậy).
  @Get('return')
  @ApiBearerAuth()
  @UseGuards(JwtAuthGuard)
  ret(@CurrentUser() u: AuthUser, @Query('orderId', ParseUUIDPipe) id: string) {
    return this.svc.getOrder(u.id, id);
  }

  // Webhook từ provider: không có JWT — tin cậy chỉ đến từ chữ ký trên RAW body.
  @Post('webhooks/:provider')
  @HttpCode(200)
  @Throttle({ default: { limit: 120, ttl: 60_000 } })
  webhook(@Param('provider') provider: string, @Req() req: { rawBody?: Buffer; headers: Record<string, string | string[] | undefined> }) {
    return this.svc.handleWebhook(provider, req.rawBody, req.headers);
  }

  // ---- Chỉ khi provider = mock và không phải production
  @Get('mock/checkout/:providerRef')
  @ApiExcludeEndpoint()
  @ApiBearerAuth()
  @UseGuards(JwtAuthGuard)
  async mockPage(@CurrentUser() u: AuthUser, @Param('providerRef') ref: string) {
    this.guardMock();
    const o = await this.db.paymentOrder.findFirst({ where: { providerRef: ref, userId: u.id } });
    if (!o) throw new NotFoundException({ code: 'ORDER_NOT_FOUND', message: 'Không tìm thấy đơn' });
    return { mock: true, note: 'CỔNG THANH TOÁN GIẢ LẬP — không có tiền thật', orderId: o.id, amount: o.amount, currency: o.currency, status: o.status,
             complete: `POST /api/v1/payments/mock/checkout/${ref}/complete {"outcome":"success|fail|cancel"}` };
  }

  @Post('mock/checkout/:providerRef/complete')
  @ApiExcludeEndpoint()
  @ApiBearerAuth()
  @UseGuards(JwtAuthGuard)
  @HttpCode(200)
  mockComplete(@CurrentUser() u: AuthUser, @Param('providerRef') ref: string, @Body() d: MockCompleteDto) {
    this.guardMock();
    return this.svc.mockComplete(u.id, ref, d.outcome);
  }

  private guardMock() {
    if (this.env.paymentProvider !== 'mock' || this.env.appEnv === 'production') throw new ForbiddenException({ code: 'MOCK_DISABLED', message: 'Mock bị tắt' });
  }
}

@ApiTags('admin')
@ApiBearerAuth()
@UseGuards(JwtAuthGuard, RolesGuard)
@Roles('ADMIN')
@Controller({ path: 'admin/payments', version: '1' })
export class AdminPaymentsController {
  constructor(private readonly svc: PaymentsService, private readonly db: PrismaService) {}

  @Get()
  async list(@Query('status') status?: string, @Query('take', new DefaultValuePipe(50)) take?: string) {
    const st = ['PENDING', 'PAID', 'FAILED', 'EXPIRED', 'CANCELLED', 'REFUNDED'].includes(status ?? '') ? status : undefined;
    return this.db.paymentOrder.findMany({ where: st ? { status: st as Prisma.EnumPaymentStatusFilter['equals'] } : {}, orderBy: { createdAt: 'desc' },
                                           take: Math.min(Number(take) || 50, 200), include: { events: { orderBy: { id: 'asc' } }, entitlement: true } })
      .then((rows) => rows.map((r) => ({ ...r, events: r.events.map((e) => ({ ...e, id: e.id.toString() })) })));
  }

  @Get('report')
  report(@Query('from') from?: string, @Query('to') to?: string) {
    const d = (s?: string) => (s && !Number.isNaN(Date.parse(s)) ? new Date(s) : undefined);
    return this.svc.report(d(from), d(to));
  }

  @Post('reconcile')
  @HttpCode(200)
  async reconcile() {
    return { expired: await this.svc.expireStale() };
  }

  @Post(':id/refund')
  @HttpCode(200)
  refund(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    return this.svc.refund(u.id, id);
  }
}
