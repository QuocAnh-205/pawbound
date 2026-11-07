import { Body, ConflictException, Controller, Delete, ForbiddenException, Get, HttpCode, HttpException, HttpStatus, Inject, Injectable, NotFoundException, Param,
  ParseUUIDPipe, Patch, Post, Query, UnprocessableEntityException, UseGuards } from '@nestjs/common';
import { ApiBearerAuth, ApiProperty, ApiPropertyOptional, ApiTags } from '@nestjs/swagger';
import { Prisma } from '@prisma/client';
import { IsInt, IsOptional, IsString, Length, Max, MaxLength, Min } from 'class-validator';
import { AuditService } from '../audit/audit.service';
import { AuthUser, CurrentUser, JwtAuthGuard } from '../common/security';
import { PrismaService } from '../prisma/prisma.service';
import { blockedEither, presence, UserIdDto } from './friends';
import { canAct, GuildAction, GuildRole, permissionsOf } from './guild.permissions';
import { ECONOMY, EconomyConfig, GUILD_CREATION } from '../payments/economy.config';
import { ModerationService } from './moderation';
import { isUniqueViolation, NotificationsService } from './notifications';
import { SOCIAL, SocialConfig, secondsFromNow } from './social.config';

type Tx = Prisma.TransactionClient;

export class CreateGuildDto {
  @ApiProperty({ example: 'Đàn Sói Phố' }) @IsString() @Length(1, 64) name: string;
  @ApiPropertyOptional() @IsOptional() @IsString() @MaxLength(500) description?: string;
  @ApiPropertyOptional() @IsOptional() @IsInt() @Min(0) @Max(255) emblem?: number;
}
export class UpdateGuildDto {
  @ApiPropertyOptional() @IsOptional() @IsString() @Length(1, 64) name?: string;
  @ApiPropertyOptional() @IsOptional() @IsString() @MaxLength(500) description?: string;
  @ApiPropertyOptional() @IsOptional() @IsInt() @Min(0) @Max(255) emblem?: number;
}

// Chuẩn hóa tên: NFC, gộp khoảng trắng; khóa unique = chữ thường -> "Đàn Sói" và "đàn  sói" là cùng tên.
export function normalizeGuildName(raw: string) {
  const name = raw.normalize('NFC').trim().replace(/\s+/g, ' ');
  return { name, key: name.toLowerCase() };
}

const forbidden = (action: GuildAction) => new ForbiddenException({ code: 'GUILD_FORBIDDEN', message: `Không có quyền: ${action}` });
const guildNotFound = () => new NotFoundException({ code: 'GUILD_NOT_FOUND', message: 'Không tìm thấy bang hội' });
const nameTaken = () => new ConflictException({ code: 'GUILD_NAME_TAKEN', message: 'Tên bang hội đã được dùng' });

@Injectable()
export class GuildService {
  constructor(private readonly db: PrismaService, private readonly notes: NotificationsService, private readonly audit: AuditService,
              private readonly mod: ModerationService, @Inject(SOCIAL) private readonly cfg: SocialConfig,
              @Inject(ECONOMY) private readonly eco: EconomyConfig) {}

  validateName(raw: string) {
    const { name, key } = normalizeGuildName(raw);
    const g = this.cfg.guild;
    if ([...name].length < g.name_min || [...name].length > g.name_max || !/^[\p{L}\p{N} _-]+$/u.test(name))
      throw new UnprocessableEntityException({ code: 'GUILD_NAME_INVALID', message: `Tên ${g.name_min}–${g.name_max} ký tự: chữ, số, khoảng trắng, _ -` });
    if (this.mod.containsBanned(name)) throw new UnprocessableEntityException({ code: 'GUILD_NAME_INAPPROPRIATE', message: 'Tên không phù hợp' });
    return { name, key };
  }
  private validateProfile(description?: string, emblem?: number) {
    if (description !== undefined && [...description].length > this.cfg.guild.description_max)
      throw new UnprocessableEntityException({ code: 'DESCRIPTION_TOO_LONG', message: `Mô tả tối đa ${this.cfg.guild.description_max} ký tự` });
    if (emblem !== undefined && emblem >= this.cfg.guild.emblem_count)
      throw new UnprocessableEntityException({ code: 'EMBLEM_INVALID', message: 'Emblem không hợp lệ' });
    return { description: description === undefined ? undefined : this.mod.mask(description.trim()), emblem };
  }

  // Khóa hàng guild (FOR UPDATE) rồi đọc vai trò của người làm TRONG transaction -> quyền không bị đổi giữa chừng.
  private async lock(tx: Tx, guildId: string) {
    const rows = await tx.$queryRaw<{ id: string }[]>`SELECT id FROM "Guild" WHERE id = ${guildId}::uuid AND "disbandedAt" IS NULL FOR UPDATE`;
    if (!rows.length) throw guildNotFound();
    return tx.guild.findUniqueOrThrow({ where: { id: guildId } });
  }
  private async roleOf(tx: Tx, guildId: string, userId: string): Promise<GuildRole | null> {
    const m = await tx.guildMember.findUnique({ where: { userId } });
    return m && m.guildId === guildId ? m.role : null;
  }
  private async actor(tx: Tx, guildId: string, me: string, action: GuildAction, target?: GuildRole) {
    const role = await this.roleOf(tx, guildId, me);
    if (!role) throw new ForbiddenException({ code: 'NOT_GUILD_MEMBER', message: 'Bạn không thuộc bang hội này' });
    if (!canAct(role, action, target)) throw forbidden(action);
    return role;
  }

  // Dùng chung cho POST /guilds và cho Phase 13 (tạo bang sau khi thanh toán) — luôn trong transaction của bên gọi.
  async createWithin(tx: Tx, me: string, dto: CreateGuildDto) {
    const { name, key } = this.validateName(dto.name);
    const prof = this.validateProfile(dto.description, dto.emblem);
    const g = await tx.guild.create({
      data: { name, nameKey: key, description: prof.description ?? '', emblem: prof.emblem ?? 0, ownerId: me, memberLimit: this.cfg.guild.member_limit },
    });
    await tx.guildMember.create({ data: { userId: me, guildId: g.id, role: 'OWNER' } });  // PK userId: đã ở bang khác -> P2002
    await tx.guildRequest.updateMany({ where: { userId: me, status: 'PENDING' }, data: { status: 'CANCELLED', respondedAt: new Date() } });
    await this.audit.record('user', me, 'guild.create', g.id, { name }, tx);
    return g;
  }

  async mapCreateError(e: unknown, me: string): Promise<never> {
    if (isUniqueViolation(e)) {
      if (await this.db.guildMember.findUnique({ where: { userId: me } }))
        throw new ConflictException({ code: 'ALREADY_IN_GUILD', message: 'Bạn đã ở trong một bang hội' });
      throw nameTaken();
    }
    throw e;
  }

  async create(me: string, dto: CreateGuildDto) {
    this.validateName(dto.name);
    if (await this.db.guildMember.findUnique({ where: { userId: me } }))
      throw new ConflictException({ code: 'ALREADY_IN_GUILD', message: 'Bạn đã ở trong một bang hội' });
    const fee = this.eco.pricing.guildCreation.enabled;
    if (fee && !(await this.db.entitlement.findFirst({ where: { userId: me, kind: GUILD_CREATION, status: 'AVAILABLE' } })))
      throw new HttpException({ code: 'PAYMENT_REQUIRED', message: `Lập bang cần phí ${this.eco.pricing.guildCreation.amount} ${this.eco.pricing.guildCreation.currency}`,
                                amount: this.eco.pricing.guildCreation.amount, currency: this.eco.pricing.guildCreation.currency }, HttpStatus.PAYMENT_REQUIRED);
    try {
      const g = await this.db.$transaction(async (tx) => {
        const created = await this.createWithin(tx, me, dto);
        if (fee) {
          // Tiêu entitlement TRONG CÙNG transaction: lập bang lỗi (trùng tên...) -> rollback, quyền còn nguyên để thử lại
          const ent = await tx.entitlement.findFirst({ where: { userId: me, kind: GUILD_CREATION, status: 'AVAILABLE' }, orderBy: { createdAt: 'asc' } });
          const r = ent && (await tx.entitlement.updateMany({ where: { id: ent.id, status: 'AVAILABLE' },
                                                              data: { status: 'CONSUMED', consumedAt: new Date(), guildId: created.id } }));
          if (!r || r.count !== 1) throw new HttpException({ code: 'PAYMENT_REQUIRED', message: 'Quyền lập bang đã được dùng' }, HttpStatus.PAYMENT_REQUIRED);
          await this.audit.record('user', me, 'entitlement.consume', ent!.id, { guildId: created.id }, tx);
        }
        return created;
      });
      return this.publicView(g.id);
    } catch (e) {
      return this.mapCreateError(e, me);
    }
  }

  async publicView(id: string) {
    const g = await this.db.guild.findFirst({ where: { id, disbandedAt: null }, include: { _count: { select: { members: true } } } });
    if (!g) throw guildNotFound();
    return { id: g.id, name: g.name, description: g.description, emblem: g.emblem, memberCount: g._count.members, memberLimit: g.memberLimit, createdAt: g.createdAt };
  }

  async search(q?: string) {
    const gs = await this.db.guild.findMany({
      where: { disbandedAt: null, ...(q ? { nameKey: { contains: normalizeGuildName(q).key } } : {}) },
      include: { _count: { select: { members: true } } }, orderBy: { createdAt: 'asc' }, take: 20,
    });
    return gs.map((g) => ({ id: g.id, name: g.name, emblem: g.emblem, memberCount: g._count.members, memberLimit: g.memberLimit }));
  }

  async mine(me: string) {
    const m = await this.db.guildMember.findUnique({ where: { userId: me } });
    if (!m) return null;
    const g = await this.db.guild.findUniqueOrThrow({
      where: { id: m.guildId }, include: { members: { include: { user: { include: { profile: true } } }, orderBy: { joinedAt: 'asc' } } },
    });
    const on = await presence(this.db, g.members.map((x) => x.userId));
    return {
      id: g.id, name: g.name, description: g.description, emblem: g.emblem, memberLimit: g.memberLimit, myRole: m.role,
      permissions: permissionsOf(m.role),  // chỉ để client ẩn/hiện nút; server vẫn kiểm tra lại mọi request
      members: g.members.map((x) => ({ userId: x.userId, displayName: x.user.profile?.displayName ?? '?', role: x.role, joinedAt: x.joinedAt, online: on.has(x.userId) })),
    };
  }

  async update(me: string, id: string, dto: UpdateGuildDto) {
    try {
      return await this.db.$transaction(async (tx) => {
        await this.lock(tx, id);
        const data: Prisma.GuildUpdateInput = {};
        if (dto.name !== undefined) {
          await this.actor(tx, id, me, 'rename');
          const { name, key } = this.validateName(dto.name);
          Object.assign(data, { name, nameKey: key });
        }
        if (dto.description !== undefined || dto.emblem !== undefined) {
          await this.actor(tx, id, me, 'edit_profile');
          const p = this.validateProfile(dto.description, dto.emblem);
          if (p.description !== undefined) data.description = p.description;
          if (p.emblem !== undefined) data.emblem = p.emblem;
        }
        if (!Object.keys(data).length) throw new UnprocessableEntityException({ code: 'NOTHING_TO_UPDATE', message: 'Không có gì để cập nhật' });
        await tx.guild.update({ where: { id }, data });
        await this.audit.record('user', me, 'guild.update', id, { fields: Object.keys(data) }, tx);
        return { ok: true };
      });
    } catch (e) {
      if (isUniqueViolation(e)) throw nameTaken();
      throw e;
    }
  }

  async invite(me: string, id: string, target: string) {
    if (target === me) throw new UnprocessableEntityException({ code: 'SELF_INVITE', message: 'Không thể tự mời' });
    if (!(await this.db.user.findUnique({ where: { id: target } })) || (await blockedEither(this.db, me, target)))
      throw new NotFoundException({ code: 'USER_NOT_FOUND', message: 'Không tìm thấy người chơi' });
    return this.createRequest(id, target, me, 'INVITE', async (tx) => {
      await this.actor(tx, id, me, 'invite');
    });
  }

  async requestJoin(me: string, id: string) {
    return this.createRequest(id, me, me, 'JOIN', async () => undefined);
  }

  private async createRequest(guildId: string, userId: string, actorId: string, kind: 'INVITE' | 'JOIN', authorize: (tx: Tx) => Promise<void>) {
    try {
      return await this.db.$transaction(async (tx) => {
        const g = await this.lock(tx, guildId);
        await authorize(tx);
        if (await tx.guildMember.findUnique({ where: { userId } }))
          throw new ConflictException({ code: kind === 'JOIN' ? 'ALREADY_IN_GUILD' : 'TARGET_IN_GUILD', message: 'Người chơi đã ở trong một bang hội' });
        if ((await tx.guildMember.count({ where: { guildId } })) >= g.memberLimit) throw guildFull(g.memberLimit);
        await tx.guildRequest.updateMany({ where: { guildId, userId, kind, status: 'PENDING', expiresAt: { lte: new Date() } },
                                           data: { status: 'CANCELLED', respondedAt: new Date() } });
        const r = await tx.guildRequest.create({ data: { guildId, userId, actorId, kind, expiresAt: secondsFromNow(this.cfg.guild.invite_ttl_s) } });
        if (kind === 'INVITE')
          await this.notes.notify(tx, userId, 'guild_invite', { requestId: r.id, guildId, guildName: g.name, fromUserId: actorId }, `guild_invite:${r.id}`);
        else
          await this.notes.notify(tx, g.ownerId, 'guild_join_request', { requestId: r.id, guildId, fromUserId: userId }, `guild_join:${r.id}`);
        return { id: r.id, kind, expiresAt: r.expiresAt };
      });
    } catch (e) {
      if (isUniqueViolation(e)) {  // đã có yêu cầu PENDING giống hệt -> idempotent
        const ex = await this.db.guildRequest.findFirst({ where: { guildId, userId, kind, status: 'PENDING' } });
        if (ex) return { id: ex.id, kind, expiresAt: ex.expiresAt };
      }
      throw e;
    }
  }

  async listRequests(me: string, id: string) {
    return this.db.$transaction(async (tx) => {
      await this.lock(tx, id);
      await this.actor(tx, id, me, 'view_requests');
      return tx.guildRequest.findMany({ where: { guildId: id, status: 'PENDING', expiresAt: { gt: new Date() } }, orderBy: { createdAt: 'asc' },
                                        select: { id: true, kind: true, userId: true, actorId: true, createdAt: true, expiresAt: true } });
    });
  }

  async respond(me: string, requestId: string, accept: boolean) {
    try {
      return await this.db.$transaction(async (tx) => {
        const r = await tx.guildRequest.findFirst({ where: { id: requestId, status: 'PENDING', expiresAt: { gt: new Date() } } });
        if (!r) throw new NotFoundException({ code: 'REQUEST_NOT_FOUND', message: 'Yêu cầu không tồn tại hoặc đã hết hạn' });
        const g = await this.lock(tx, r.guildId);
        // INVITE: chỉ người được mời trả lời. JOIN: chỉ OWNER/OFFICER của đúng bang đó duyệt.
        if (r.kind === 'INVITE' && r.userId !== me) throw new NotFoundException({ code: 'REQUEST_NOT_FOUND', message: 'Yêu cầu không tồn tại hoặc đã hết hạn' });
        if (r.kind === 'JOIN') await this.actor(tx, r.guildId, me, 'review_join');
        const upd = await tx.guildRequest.updateMany({ where: { id: r.id, status: 'PENDING' },
                                                       data: { status: accept ? 'ACCEPTED' : 'REJECTED', respondedAt: new Date() } });
        if (upd.count !== 1) throw new ConflictException({ code: 'REQUEST_USED', message: 'Yêu cầu đã được xử lý' });
        if (!accept) return { status: 'REJECTED' };
        if ((await tx.guildMember.count({ where: { guildId: r.guildId } })) >= g.memberLimit) throw guildFull(g.memberLimit);
        await tx.guildMember.create({ data: { userId: r.userId, guildId: r.guildId, role: 'MEMBER' } });
        await tx.guildRequest.updateMany({ where: { userId: r.userId, status: 'PENDING', id: { not: r.id } }, data: { status: 'CANCELLED', respondedAt: new Date() } });
        await this.audit.record('user', me, 'guild.member_join', r.guildId, { userId: r.userId, via: r.kind }, tx);
        if (r.kind === 'JOIN') await this.notes.notify(tx, r.userId, 'guild_join_accepted', { guildId: g.id, guildName: g.name }, `guild_join_ok:${r.id}`);
        return { status: 'ACCEPTED', guildId: r.guildId };
      });
    } catch (e) {
      if (isUniqueViolation(e)) throw new ConflictException({ code: 'ALREADY_IN_GUILD', message: 'Người chơi đã ở trong một bang hội' });
      throw e;
    }
  }

  async cancelRequest(me: string, requestId: string) {
    return this.db.$transaction(async (tx) => {
      const r = await tx.guildRequest.findFirst({ where: { id: requestId, status: 'PENDING' } });
      if (!r) throw new NotFoundException({ code: 'REQUEST_NOT_FOUND', message: 'Không tìm thấy yêu cầu' });
      await this.lock(tx, r.guildId);
      // Người tạo được hủy; lời mời của bang thì OWNER/OFFICER cũng hủy được
      if (r.actorId !== me) {
        if (r.kind !== 'INVITE') throw new NotFoundException({ code: 'REQUEST_NOT_FOUND', message: 'Không tìm thấy yêu cầu' });
        await this.actor(tx, r.guildId, me, 'invite');
      }
      await tx.guildRequest.update({ where: { id: r.id }, data: { status: 'CANCELLED', respondedAt: new Date() } });
      return { ok: true };
    });
  }

  async memberAction(me: string, guildId: string, target: string, action: 'kick' | 'promote' | 'demote' | 'transfer') {
    if (target === me) throw new UnprocessableEntityException({ code: 'SELF_TARGET', message: 'Không thể tự thực hiện thao tác này với chính mình' });
    return this.db.$transaction(async (tx) => {
      await this.lock(tx, guildId);
      const tRole = await this.roleOf(tx, guildId, target);
      if (!tRole) throw new NotFoundException({ code: 'NOT_MEMBER', message: 'Người này không ở trong bang' });
      await this.actor(tx, guildId, me, action, tRole);
      if (action === 'kick') await tx.guildMember.delete({ where: { userId: target } });
      if (action === 'promote') await tx.guildMember.update({ where: { userId: target }, data: { role: 'OFFICER' } });
      if (action === 'demote') await tx.guildMember.update({ where: { userId: target }, data: { role: 'MEMBER' } });
      if (action === 'transfer') {
        await tx.guildMember.update({ where: { userId: target }, data: { role: 'OWNER' } });
        await tx.guildMember.update({ where: { userId: me }, data: { role: 'OFFICER' } });
        await tx.guild.update({ where: { id: guildId }, data: { ownerId: target } });
      }
      await this.audit.record('user', me, `guild.${action}`, guildId, { userId: target }, tx);
      return { ok: true };
    });
  }

  async leave(me: string, guildId: string) {
    return this.db.$transaction(async (tx) => {
      await this.lock(tx, guildId);
      const role = await this.roleOf(tx, guildId, me);
      if (!role) throw new ForbiddenException({ code: 'NOT_GUILD_MEMBER', message: 'Bạn không thuộc bang hội này' });
      if (!canAct(role, 'leave'))
        throw new UnprocessableEntityException({ code: 'OWNER_MUST_TRANSFER', message: 'Chủ bang phải chuyển quyền hoặc giải tán trước khi rời' });
      await tx.guildMember.delete({ where: { userId: me } });
      await this.audit.record('user', me, 'guild.leave', guildId, undefined, tx);
      return { ok: true };
    });
  }

  async disband(me: string, guildId: string) {
    return this.db.$transaction(async (tx) => {
      await this.lock(tx, guildId);
      await this.actor(tx, guildId, me, 'disband');
      await tx.guildMember.deleteMany({ where: { guildId } });
      await tx.guildRequest.updateMany({ where: { guildId, status: 'PENDING' }, data: { status: 'CANCELLED', respondedAt: new Date() } });
      await tx.guild.update({ where: { id: guildId }, data: { disbandedAt: new Date(), nameKey: null } });  // giải phóng tên
      await this.audit.record('user', me, 'guild.disband', guildId, undefined, tx);
      return { ok: true };
    });
  }
}

const guildFull = (n: number) => new UnprocessableEntityException({ code: 'GUILD_FULL', message: `Bang hội tối đa ${n} thành viên` });

@ApiTags('guild')
@ApiBearerAuth()
@UseGuards(JwtAuthGuard)
@Controller({ version: '1' })
export class GuildController {
  constructor(private readonly svc: GuildService) {}

  @Post('guilds')
  create(@CurrentUser() u: AuthUser, @Body() d: CreateGuildDto) {
    return this.svc.create(u.id, d);
  }
  @Get('guilds')
  search(@Query('q') q?: string) {
    return this.svc.search(typeof q === 'string' ? q.slice(0, 64) : undefined);
  }
  @Get('guilds/mine')
  mine(@CurrentUser() u: AuthUser) {
    return this.svc.mine(u.id);
  }
  @Get('guilds/:id')
  view(@Param('id', ParseUUIDPipe) id: string) {
    return this.svc.publicView(id);
  }
  @Patch('guilds/:id')
  update(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string, @Body() d: UpdateGuildDto) {
    return this.svc.update(u.id, id, d);
  }
  @Delete('guilds/:id')
  disband(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    return this.svc.disband(u.id, id);
  }
  @Post('guilds/:id/invites')
  invite(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string, @Body() d: UserIdDto) {
    return this.svc.invite(u.id, id, d.userId);
  }
  @Post('guilds/:id/join-requests')
  join(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    return this.svc.requestJoin(u.id, id);
  }
  @Get('guilds/:id/requests')
  requests(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    return this.svc.listRequests(u.id, id);
  }
  @Post('guilds/:id/leave')
  @HttpCode(200)
  leave(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    return this.svc.leave(u.id, id);
  }
  @Post('guilds/:id/members/:userId/:action')
  @HttpCode(200)
  member(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string, @Param('userId', ParseUUIDPipe) t: string, @Param('action') action: string) {
    if (!['kick', 'promote', 'demote', 'transfer'].includes(action)) throw new NotFoundException({ code: 'NOT_FOUND', message: 'Không có thao tác này' });
    return this.svc.memberAction(u.id, id, t, action as 'kick' | 'promote' | 'demote' | 'transfer');
  }
  @Post('guild-requests/:id/accept')
  @HttpCode(200)
  accept(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    return this.svc.respond(u.id, id, true);
  }
  @Post('guild-requests/:id/reject')
  @HttpCode(200)
  reject(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    return this.svc.respond(u.id, id, false);
  }
  @Delete('guild-requests/:id')
  cancel(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    return this.svc.cancelRequest(u.id, id);
  }
}
