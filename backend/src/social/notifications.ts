import { Controller, Get, HttpCode, Inject, Injectable, NotFoundException, Param, ParseUUIDPipe, Post, UseGuards } from '@nestjs/common';
import { ApiBearerAuth, ApiTags } from '@nestjs/swagger';
import { Prisma } from '@prisma/client';
import { AuthUser, CurrentUser, JwtAuthGuard } from '../common/security';
import { PrismaService } from '../prisma/prisma.service';
import { SOCIAL, SocialConfig, secondsFromNow } from './social.config';

type Tx = Prisma.TransactionClient;

export function isUniqueViolation(e: unknown): boolean {
  return e instanceof Prisma.PrismaClientKnownRequestError && e.code === 'P2002';
}

@Injectable()
export class NotificationsService {
  constructor(private readonly db: PrismaService, @Inject(SOCIAL) private readonly cfg: SocialConfig) {}

  // Idempotent theo (userId, dedupeKey): gửi lại cùng sự kiện không tạo thông báo trùng.
  async notify(tx: Tx | undefined, userId: string, type: string, payload: Record<string, unknown>, dedupeKey: string) {
    await (tx ?? this.db).notification.createMany({
      data: [{ userId, type, payload: payload as Prisma.InputJsonValue, dedupeKey, expiresAt: secondsFromNow(this.cfg.notifications.ttl_s) }],
      skipDuplicates: true,
    });
  }

  list(userId: string) {
    return this.db.notification.findMany({
      where: { userId, expiresAt: { gt: new Date() } },
      orderBy: { createdAt: 'desc' },
      take: this.cfg.notifications.max_list,
      select: { id: true, type: true, payload: true, createdAt: true, readAt: true, expiresAt: true },
    });
  }
}

@ApiTags('notifications')
@ApiBearerAuth()
@UseGuards(JwtAuthGuard)
@Controller({ path: 'notifications', version: '1' })
export class NotificationsController {
  constructor(private readonly svc: NotificationsService, private readonly db: PrismaService) {}

  // Realtime giai đoạn này = polling (xem docs/social/social.md): client gọi định kỳ 5–15 s.
  @Get()
  list(@CurrentUser() u: AuthUser) {
    return this.svc.list(u.id);
  }

  @Post(':id/read')
  @HttpCode(200)
  async read(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    const r = await this.db.notification.updateMany({ where: { id, userId: u.id, readAt: null }, data: { readAt: new Date() } });
    if (r.count === 0 && !(await this.db.notification.findFirst({ where: { id, userId: u.id } })))
      throw new NotFoundException({ code: 'NOTIFICATION_NOT_FOUND', message: 'Không tìm thấy thông báo' });
    return { ok: true };
  }
}
