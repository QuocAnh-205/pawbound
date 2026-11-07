import { Body, Controller, ForbiddenException, Get, HttpCode, HttpException, HttpStatus, Inject, Injectable, NotFoundException, Param,
  Post, Query, UnprocessableEntityException, UseGuards } from '@nestjs/common';
import { ApiBearerAuth, ApiProperty, ApiPropertyOptional, ApiTags } from '@nestjs/swagger';
import { Throttle } from '@nestjs/throttler';
import { IsOptional, IsString, Length, MaxLength } from 'class-validator';
import { AuditService } from '../audit/audit.service';
import { AuthUser, CurrentUser, JwtAuthGuard } from '../common/security';
import { PrismaService } from '../prisma/prisma.service';
import { ModerationService, sanitizeText } from './moderation';
import { SOCIAL, SocialConfig } from './social.config';

const CHANNELS = ['global', 'party', 'guild'] as const;
type ChannelKind = (typeof CHANNELS)[number];

export class SendChatDto {
  @ApiProperty() @IsString() @Length(1, 1000) text: string;
}
export class ReportDto {
  @ApiPropertyOptional() @IsOptional() @IsString() @MaxLength(200) reason?: string;
}

@Injectable()
export class ChatService {
  constructor(private readonly db: PrismaService, private readonly mod: ModerationService, private readonly audit: AuditService,
              @Inject(SOCIAL) private readonly cfg: SocialConfig) {}

  // Kênh party/guild suy ra từ membership HIỆN TẠI ở server — client không chọn được kênh của nhóm khác.
  async resolve(me: string, kind: ChannelKind) {
    if (kind === 'global') return 'global';
    if (kind === 'party') {
      const m = await this.db.partyMember.findUnique({ where: { userId: me } });
      if (!m) throw new ForbiddenException({ code: 'NOT_IN_PARTY', message: 'Bạn không ở trong nhóm nào' });
      return `party:${m.partyId}`;
    }
    const g = await this.db.guildMember.findUnique({ where: { userId: me } });
    if (!g) throw new ForbiddenException({ code: 'NOT_GUILD_MEMBER', message: 'Bạn không ở trong bang hội nào' });
    return `guild:${g.guildId}`;
  }

  async send(me: string, kind: ChannelKind, raw: string) {
    const user = await this.db.user.findUniqueOrThrow({ where: { id: me } });
    if (user.chatMutedUntil && user.chatMutedUntil > new Date())
      throw new ForbiddenException({ code: 'CHAT_MUTED', message: 'Bạn đang bị cấm chat', mutedUntil: user.chatMutedUntil.toISOString() });
    const text = sanitizeText(raw);
    if (!text.length || [...text].length > this.cfg.chat.max_length)
      throw new UnprocessableEntityException({ code: 'CHAT_LENGTH', message: `Tin nhắn 1–${this.cfg.chat.max_length} ký tự` });
    const channel = await this.resolve(me, kind);
    // Rate limit theo người dùng, đếm trong DB (đúng cả khi chạy nhiều instance backend). Advisory lock theo user
    // để N request song song không cùng lọt qua bước đếm.
    const m = await this.db.$transaction(async (tx) => {
      await tx.$executeRaw`SELECT pg_advisory_xact_lock(hashtext(${'chat:' + me}))`;
      const since = new Date(Date.now() - this.cfg.chat.rate_window_s * 1000);
      const recent = await tx.chatMessage.count({ where: { senderId: me, createdAt: { gt: since } } });
      if (recent >= this.cfg.chat.rate_max_messages)
        throw new HttpException({ code: 'CHAT_RATE_LIMIT', message: 'Gửi quá nhanh, chờ một chút' }, HttpStatus.TOO_MANY_REQUESTS);
      return tx.chatMessage.create({ data: { channel, senderId: me, text: this.mod.mask(text) } });
    });
    return { id: m.id.toString(), channel: kind, text: m.text, createdAt: m.createdAt };
  }

  // Polling: ?after=<id> trả tin mới hơn. Tin từ người mình chặn (hoặc chặn mình) bị lọc.
  async history(me: string, kind: ChannelKind, after?: string) {
    const channel = await this.resolve(me, kind);
    const blocks = await this.db.userBlock.findMany({ where: { OR: [{ blockerId: me }, { blockedId: me }] } });
    const hidden = new Set(blocks.map((b) => (b.blockerId === me ? b.blockedId : b.blockerId)));
    const afterId = after && /^\d{1,18}$/.test(after) ? BigInt(after) : undefined;
    const rows = await this.db.chatMessage.findMany({
      where: { channel, ...(afterId !== undefined ? { id: { gt: afterId } } : {}), ...(hidden.size ? { senderId: { notIn: [...hidden] } } : {}) },
      orderBy: { id: afterId !== undefined ? 'asc' : 'desc' },
      take: this.cfg.chat.history_limit,
      include: { sender: { include: { profile: true } } },
    });
    if (afterId === undefined) rows.reverse();
    return rows.map((r) => ({ id: r.id.toString(), senderId: r.senderId, sender: r.sender.profile?.displayName ?? '?', text: r.text, createdAt: r.createdAt }));
  }

  async report(me: string, id: string, reason?: string) {
    if (!/^\d{1,18}$/.test(id)) throw new NotFoundException({ code: 'MESSAGE_NOT_FOUND', message: 'Không tìm thấy tin nhắn' });
    const m = await this.db.chatMessage.findUnique({ where: { id: BigInt(id) } });
    if (!m) throw new NotFoundException({ code: 'MESSAGE_NOT_FOUND', message: 'Không tìm thấy tin nhắn' });
    // Người báo cáo phải đọc được kênh đó
    if (m.channel !== 'global') {
      const mine = await Promise.all([this.resolve(me, 'party').catch(() => null), this.resolve(me, 'guild').catch(() => null)]);
      if (!mine.includes(m.channel)) throw new NotFoundException({ code: 'MESSAGE_NOT_FOUND', message: 'Không tìm thấy tin nhắn' });
    }
    // Hook kiểm duyệt: ghi audit (nội dung tin lưu kèm để quản trị xem xét; chỉ admin đọc AuditLog)
    await this.audit.record('user', me, 'chat.report', id, { senderId: m.senderId, channel: m.channel.split(':')[0], reason: reason?.slice(0, 200), text: m.text });
    return { ok: true };
  }
}

const parseKind = (k: string): ChannelKind => {
  if (!(CHANNELS as readonly string[]).includes(k)) throw new NotFoundException({ code: 'CHANNEL_NOT_FOUND', message: 'Kênh không tồn tại' });
  return k as ChannelKind;
};

@ApiTags('chat')
@ApiBearerAuth()
@UseGuards(JwtAuthGuard)
@Controller({ path: 'chat', version: '1' })
export class ChatController {
  constructor(private readonly svc: ChatService) {}

  @Post(':channel')
  @Throttle({ default: { limit: () => Number(process.env.THROTTLE_CHAT_PER_MIN ?? 30), ttl: 60_000 } })  // lớp chặn thô theo IP; giới hạn chính theo user ở service
  send(@CurrentUser() u: AuthUser, @Param('channel') ch: string, @Body() d: SendChatDto) {
    return this.svc.send(u.id, parseKind(ch), d.text);
  }
  @Get(':channel')
  history(@CurrentUser() u: AuthUser, @Param('channel') ch: string, @Query('after') after?: string) {
    return this.svc.history(u.id, parseKind(ch), typeof after === 'string' ? after : undefined);
  }
  @Post('messages/:id/report')
  @HttpCode(200)
  report(@CurrentUser() u: AuthUser, @Param('id') id: string, @Body() d: ReportDto) {
    return this.svc.report(u.id, id, d.reason);
  }
}

