import { Body, ConflictException, Controller, ForbiddenException, Get, HttpCode, Inject, Injectable, NotFoundException, Param,
  ParseUUIDPipe, Post, UnprocessableEntityException, UseGuards } from '@nestjs/common';
import { ApiBearerAuth, ApiTags } from '@nestjs/swagger';
import { Prisma } from '@prisma/client';
import { AuthUser, CurrentUser, JwtAuthGuard } from '../common/security';
import { PrismaService } from '../prisma/prisma.service';
import { blockedEither, presence, UserIdDto } from './friends';
import { isUniqueViolation, NotificationsService } from './notifications';
import { SOCIAL, SocialConfig, secondsFromNow } from './social.config';

type Tx = Prisma.TransactionClient;
const noParty = () => new NotFoundException({ code: 'NOT_IN_PARTY', message: 'Bạn không ở trong nhóm nào' });
const notLeader = () => new ForbiddenException({ code: 'NOT_PARTY_LEADER', message: 'Chỉ trưởng nhóm được làm việc này' });

@Injectable()
export class PartyService {
  constructor(private readonly db: PrismaService, private readonly notes: NotificationsService, @Inject(SOCIAL) private readonly cfg: SocialConfig) {}

  // Khóa hàng party trong transaction rồi đọc lại quyền -> tránh TOCTOU khi nhiều thao tác đồng thời.
  private async lockMine(tx: Tx, me: string) {
    const m = await tx.partyMember.findUnique({ where: { userId: me } });
    if (!m) throw noParty();
    await tx.$queryRaw`SELECT id FROM "Party" WHERE id = ${m.partyId}::uuid FOR UPDATE`;
    const party = await tx.party.findUnique({ where: { id: m.partyId }, include: { members: { orderBy: { joinedAt: 'asc' } } } });
    if (!party || !party.members.some((x) => x.userId === me)) throw noParty();
    return party;
  }

  async mine(me: string) {
    const m = await this.db.partyMember.findUnique({ where: { userId: me } });
    if (!m) return null;
    const p = await this.db.party.findUniqueOrThrow({
      where: { id: m.partyId },
      include: { members: { include: { user: { include: { profile: true } } }, orderBy: { joinedAt: 'asc' } },
                 invites: { where: { status: 'PENDING', expiresAt: { gt: new Date() } } } },
    });
    const on = await presence(this.db, p.members.map((x) => x.userId));
    return {
      id: p.id, leaderId: p.leaderId, maxMembers: this.cfg.party.max_members,
      members: p.members.map((x) => ({ userId: x.userId, displayName: x.user.profile?.displayName ?? '?', online: on.has(x.userId), serverId: on.get(x.userId) ?? null })),
      pendingInvites: p.leaderId === me ? p.invites.map((i) => ({ id: i.id, inviteeId: i.inviteeId, expiresAt: i.expiresAt })) : [],
    };
  }

  async create(me: string) {
    try {
      return await this.db.$transaction(async (tx) => {
        const p = await tx.party.create({ data: { leaderId: me } });
        await tx.partyMember.create({ data: { userId: me, partyId: p.id } });
        return { id: p.id, leaderId: me };
      });
    } catch (e) {
      if (isUniqueViolation(e)) throw new ConflictException({ code: 'ALREADY_IN_PARTY', message: 'Bạn đã ở trong một nhóm' });
      throw e;
    }
  }

  async invite(me: string, target: string) {
    if (target === me) throw new UnprocessableEntityException({ code: 'SELF_INVITE', message: 'Không thể tự mời' });
    if (!(await this.db.user.findUnique({ where: { id: target } })) || (await blockedEither(this.db, me, target)))
      throw new NotFoundException({ code: 'USER_NOT_FOUND', message: 'Không tìm thấy người chơi' });
    try {
      return await this.db.$transaction(async (tx) => {
        const p = await this.lockMine(tx, me);
        if (p.leaderId !== me) throw notLeader();
        if (p.members.some((x) => x.userId === target)) throw new ConflictException({ code: 'ALREADY_MEMBER', message: 'Đã ở trong nhóm' });
        if (p.members.length >= this.cfg.party.max_members) throw full(this.cfg.party.max_members);
        // Lời mời cũ hết hạn -> đóng lại trước khi tạo mới (index unique chỉ cho một PENDING)
        await tx.partyInvite.updateMany({ where: { partyId: p.id, inviteeId: target, status: 'PENDING', expiresAt: { lte: new Date() } },
                                          data: { status: 'CANCELLED', respondedAt: new Date() } });
        const inv = await tx.partyInvite.create({ data: { partyId: p.id, inviterId: me, inviteeId: target, expiresAt: secondsFromNow(this.cfg.party.invite_ttl_s) } });
        await this.notes.notify(tx, target, 'party_invite', { inviteId: inv.id, partyId: p.id, fromUserId: me }, `party_invite:${inv.id}`);
        return { id: inv.id, expiresAt: inv.expiresAt };
      });
    } catch (e) {
      if (isUniqueViolation(e)) {  // đã có lời mời đang chờ -> trả lại lời mời đó (idempotent)
        const m = await this.db.partyMember.findUnique({ where: { userId: me } });
        const ex = m && (await this.db.partyInvite.findFirst({ where: { partyId: m.partyId, inviteeId: target, status: 'PENDING' } }));
        if (ex) return { id: ex.id, expiresAt: ex.expiresAt };
      }
      throw e;
    }
  }

  async respond(me: string, inviteId: string, accept: boolean) {
    try {
      return await this.db.$transaction(async (tx) => {
        const inv = await tx.partyInvite.findFirst({ where: { id: inviteId, inviteeId: me, status: 'PENDING', expiresAt: { gt: new Date() } } });
        if (!inv) throw new NotFoundException({ code: 'INVITE_NOT_FOUND', message: 'Lời mời không tồn tại hoặc đã hết hạn' });
        if (!accept) {
          await tx.partyInvite.update({ where: { id: inv.id }, data: { status: 'REJECTED', respondedAt: new Date() } });
          return { status: 'REJECTED' };
        }
        await tx.$queryRaw`SELECT id FROM "Party" WHERE id = ${inv.partyId}::uuid FOR UPDATE`;
        const n = await tx.partyMember.count({ where: { partyId: inv.partyId } });
        if (n === 0) throw new NotFoundException({ code: 'PARTY_GONE', message: 'Nhóm đã giải tán' });
        if (n >= this.cfg.party.max_members) throw full(this.cfg.party.max_members);
        const r = await tx.partyInvite.updateMany({ where: { id: inv.id, status: 'PENDING' }, data: { status: 'ACCEPTED', respondedAt: new Date() } });
        if (r.count !== 1) throw new ConflictException({ code: 'INVITE_USED', message: 'Lời mời đã được dùng' });
        await tx.partyMember.create({ data: { userId: me, partyId: inv.partyId } });  // PK userId: đang ở nhóm khác -> P2002
        return { status: 'ACCEPTED', partyId: inv.partyId };
      });
    } catch (e) {
      if (isUniqueViolation(e)) throw new ConflictException({ code: 'ALREADY_IN_PARTY', message: 'Rời nhóm hiện tại trước khi vào nhóm khác' });
      throw e;
    }
  }

  async leave(me: string) {
    return this.db.$transaction(async (tx) => {
      const p = await this.lockMine(tx, me);
      await tx.partyMember.delete({ where: { userId: me } });
      const rest = p.members.filter((x) => x.userId !== me);
      if (rest.length === 0) {
        await tx.party.delete({ where: { id: p.id } });
        return { disbanded: true };
      }
      if (p.leaderId === me) await tx.party.update({ where: { id: p.id }, data: { leaderId: rest[0].userId } });  // người vào sớm nhất lên trưởng nhóm
      return { disbanded: false, leaderId: p.leaderId === me ? rest[0].userId : p.leaderId };
    });
  }

  async kick(me: string, target: string) {
    return this.db.$transaction(async (tx) => {
      const p = await this.lockMine(tx, me);
      if (p.leaderId !== me) throw notLeader();
      if (target === me) throw new UnprocessableEntityException({ code: 'SELF_KICK', message: 'Dùng "rời nhóm" thay vì tự kick' });
      const r = await tx.partyMember.deleteMany({ where: { userId: target, partyId: p.id } });
      if (!r.count) throw new NotFoundException({ code: 'NOT_MEMBER', message: 'Người này không ở trong nhóm' });
      return { ok: true };
    });
  }

  async transfer(me: string, target: string) {
    return this.db.$transaction(async (tx) => {
      const p = await this.lockMine(tx, me);
      if (p.leaderId !== me) throw notLeader();
      if (!p.members.some((x) => x.userId === target)) throw new NotFoundException({ code: 'NOT_MEMBER', message: 'Người này không ở trong nhóm' });
      await tx.party.update({ where: { id: p.id }, data: { leaderId: target } });
      return { leaderId: target };
    });
  }
}

const full = (n: number) => new UnprocessableEntityException({ code: 'PARTY_FULL', message: `Nhóm tối đa ${n} người` });

@ApiTags('social')
@ApiBearerAuth()
@UseGuards(JwtAuthGuard)
@Controller({ path: 'party', version: '1' })
export class PartyController {
  constructor(private readonly svc: PartyService) {}

  @Get()
  mine(@CurrentUser() u: AuthUser) {
    return this.svc.mine(u.id);
  }
  @Post()
  create(@CurrentUser() u: AuthUser) {
    return this.svc.create(u.id);
  }
  @Post('invites')
  invite(@CurrentUser() u: AuthUser, @Body() d: UserIdDto) {
    return this.svc.invite(u.id, d.userId);
  }
  @Post('invites/:id/accept')
  @HttpCode(200)
  accept(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    return this.svc.respond(u.id, id, true);
  }
  @Post('invites/:id/reject')
  @HttpCode(200)
  reject(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    return this.svc.respond(u.id, id, false);
  }
  @Post('leave')
  @HttpCode(200)
  leave(@CurrentUser() u: AuthUser) {
    return this.svc.leave(u.id);
  }
  @Post('members/:userId/kick')
  @HttpCode(200)
  kick(@CurrentUser() u: AuthUser, @Param('userId', ParseUUIDPipe) t: string) {
    return this.svc.kick(u.id, t);
  }
  @Post('leader/:userId')
  @HttpCode(200)
  transfer(@CurrentUser() u: AuthUser, @Param('userId', ParseUUIDPipe) t: string) {
    return this.svc.transfer(u.id, t);
  }
}
