import { Body, ConflictException, Controller, Delete, Get, HttpCode, Inject, Injectable, NotFoundException, Param, ParseUUIDPipe,
  Post, UnprocessableEntityException, UseGuards } from '@nestjs/common';
import { ApiBearerAuth, ApiProperty, ApiTags } from '@nestjs/swagger';
import { Prisma } from '@prisma/client';
import { IsString, IsUUID, Length } from 'class-validator';
import { AuditService } from '../audit/audit.service';
import { AuthUser, CurrentUser, JwtAuthGuard } from '../common/security';
import { PrismaService } from '../prisma/prisma.service';
import { isUniqueViolation, NotificationsService } from './notifications';
import { SOCIAL, SocialConfig, secondsFromNow } from './social.config';

type Db = PrismaService | Prisma.TransactionClient;
export const pairKey = (a: string, b: string) => (a < b ? `${a}:${b}` : `${b}:${a}`);
export const PRESENCE_MAX_AGE_MS = 12 * 3600_000;

export async function blockedEither(db: Db, a: string, b: string) {
  const n = await db.userBlock.count({ where: { OR: [{ blockerId: a, blockedId: b }, { blockerId: b, blockedId: a }] } });
  return n > 0;
}

// Online = có phiên game chưa kết thúc (game server đóng phiên khi người chơi rời — Phase 11).
export async function presence(db: Db, userIds: string[]) {
  const s = await db.gameSession.findMany({
    where: { userId: { in: userIds }, endedAt: null, startedAt: { gt: new Date(Date.now() - PRESENCE_MAX_AGE_MS) } },
    select: { userId: true, serverId: true },
  });
  return new Map(s.map((x) => [x.userId, x.serverId]));
}

export class FriendRequestDto {
  @ApiProperty({ example: 'ChoMuc' }) @IsString() @Length(3, 24) displayName: string;
}
export class UserIdDto {
  @ApiProperty() @IsUUID() userId: string;
}

const notFoundUser = () => new NotFoundException({ code: 'USER_NOT_FOUND', message: 'Không tìm thấy người chơi' });

@Injectable()
export class FriendsService {
  constructor(private readonly db: PrismaService, private readonly notes: NotificationsService, private readonly audit: AuditService,
              @Inject(SOCIAL) private readonly cfg: SocialConfig) {}

  private friendCount(db: Db, userId: string) {
    return db.friendship.count({ where: { status: 'ACCEPTED', OR: [{ requesterId: userId }, { addresseeId: userId }] } });
  }

  async request(me: string, displayName: string) {
    const prof = await this.db.playerProfile.findUnique({ where: { displayNameLower: displayName.trim().toLowerCase() }, include: { user: true } });
    if (!prof || prof.user.status !== 'ACTIVE') throw notFoundUser();
    const target = prof.userId;
    if (target === me) throw new UnprocessableEntityException({ code: 'SELF_FRIEND', message: 'Không thể kết bạn với chính mình' });
    if (await this.db.userBlock.findUnique({ where: { blockerId_blockedId: { blockerId: me, blockedId: target } } }))
      throw new ConflictException({ code: 'UNBLOCK_FIRST', message: 'Bạn đang chặn người này' });
    if (await blockedEither(this.db, me, target)) throw notFoundUser();  // bị chặn: không lộ thông tin
    for (let attempt = 0; attempt < 2; attempt++) {
      try {
        return await this.db.$transaction(async (tx) => {
          const key = pairKey(me, target);
          const ex = await tx.friendship.findUnique({ where: { pairKey: key } });
          if (ex?.status === 'ACCEPTED') throw new ConflictException({ code: 'ALREADY_FRIENDS', message: 'Đã là bạn bè' });
          const live = ex && ex.expiresAt > new Date();
          if (ex && live && ex.requesterId === me) return { id: ex.id, status: ex.status };  // gửi lại: idempotent
          if (ex && live && ex.requesterId === target) {  // hai bên cùng gửi -> chấp nhận luôn
            if ((await this.friendCount(tx, me)) >= this.cfg.friends.max) throw limit(this.cfg.friends.max);
            await tx.friendship.update({ where: { id: ex.id }, data: { status: 'ACCEPTED', respondedAt: new Date() } });
            await this.notes.notify(tx, target, 'friend_accepted', { userId: me }, `friend_accepted:${ex.id}`);
            return { id: ex.id, status: 'ACCEPTED' as const };
          }
          if (ex) await tx.friendship.delete({ where: { id: ex.id } });  // lời mời cũ hết hạn
          if ((await this.friendCount(tx, me)) >= this.cfg.friends.max) throw limit(this.cfg.friends.max);
          const f = await tx.friendship.create({
            data: { pairKey: key, requesterId: me, addresseeId: target, expiresAt: secondsFromNow(this.cfg.friends.request_ttl_s) },
          });
          await this.notes.notify(tx, target, 'friend_request', { requestId: f.id, fromUserId: me }, `friend_request:${f.id}`);
          return { id: f.id, status: f.status };
        });
      } catch (e) {
        if (isUniqueViolation(e) && attempt === 0) continue;  // đua với request ngược chiều -> chạy lại logic
        throw e;
      }
    }
    throw new ConflictException({ code: 'CONCURRENT_UPDATE', message: 'Thử lại' });
  }

  async respond(me: string, id: string, accept: boolean) {
    return this.db.$transaction(async (tx) => {
      const f = await tx.friendship.findFirst({ where: { id, addresseeId: me, status: 'PENDING', expiresAt: { gt: new Date() } } });
      if (!f) throw new NotFoundException({ code: 'REQUEST_NOT_FOUND', message: 'Lời mời không tồn tại hoặc đã hết hạn' });
      if (!accept) {
        await tx.friendship.delete({ where: { id } });
        return { status: 'REJECTED' };
      }
      if ((await this.friendCount(tx, me)) >= this.cfg.friends.max) throw limit(this.cfg.friends.max);
      // updateMany có điều kiện PENDING: chấp nhận hai lần đồng thời chỉ một lần có hiệu lực
      const r = await tx.friendship.updateMany({ where: { id, status: 'PENDING' }, data: { status: 'ACCEPTED', respondedAt: new Date() } });
      if (r.count === 1) await this.notes.notify(tx, f.requesterId, 'friend_accepted', { userId: me }, `friend_accepted:${id}`);
      return { status: 'ACCEPTED' };
    });
  }

  async list(me: string) {
    const rows = await this.db.friendship.findMany({
      where: { OR: [{ requesterId: me }, { addresseeId: me }] },
      include: { requester: { include: { profile: true } }, addressee: { include: { profile: true } } },
      orderBy: { createdAt: 'asc' },
    });
    const now = new Date();
    const otherOf = (r: (typeof rows)[number]) => (r.requesterId === me ? r.addressee : r.requester);
    const friends = rows.filter((r) => r.status === 'ACCEPTED');
    const online = await presence(this.db, friends.map((r) => otherOf(r).id));
    const brief = (r: (typeof rows)[number]) => ({ requestId: r.id, userId: otherOf(r).id, displayName: otherOf(r).profile?.displayName ?? '?' });
    return {
      friends: friends.map((r) => ({ ...brief(r), since: r.respondedAt, online: online.has(otherOf(r).id), serverId: online.get(otherOf(r).id) ?? null })),
      incoming: rows.filter((r) => r.status === 'PENDING' && r.addresseeId === me && r.expiresAt > now).map(brief),
      outgoing: rows.filter((r) => r.status === 'PENDING' && r.requesterId === me && r.expiresAt > now).map(brief),
    };
  }

  async block(me: string, target: string) {
    if (target === me) throw new UnprocessableEntityException({ code: 'SELF_BLOCK', message: 'Không thể tự chặn' });
    if (!(await this.db.user.findUnique({ where: { id: target } }))) throw notFoundUser();
    await this.db.$transaction(async (tx) => {
      await tx.userBlock.createMany({ data: [{ blockerId: me, blockedId: target }], skipDuplicates: true });
      await tx.friendship.deleteMany({ where: { pairKey: pairKey(me, target) } });  // chặn = hủy kết bạn/lời mời
      await tx.partyInvite.updateMany({ where: { status: 'PENDING', OR: [{ inviterId: me, inviteeId: target }, { inviterId: target, inviteeId: me }] },
                                        data: { status: 'CANCELLED', respondedAt: new Date() } });
    });
    await this.audit.record('user', me, 'social.block', target);
    return { ok: true };
  }
}

const limit = (n: number) => new UnprocessableEntityException({ code: 'FRIEND_LIMIT', message: `Tối đa ${n} bạn bè` });

@ApiTags('social')
@ApiBearerAuth()
@UseGuards(JwtAuthGuard)
@Controller({ version: '1' })
export class FriendsController {
  constructor(private readonly svc: FriendsService, private readonly db: PrismaService) {}

  @Get('friends')
  list(@CurrentUser() u: AuthUser) {
    return this.svc.list(u.id);
  }
  @Post('friends/requests')
  request(@CurrentUser() u: AuthUser, @Body() d: FriendRequestDto) {
    return this.svc.request(u.id, d.displayName);
  }
  @Post('friends/requests/:id/accept')
  @HttpCode(200)
  accept(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    return this.svc.respond(u.id, id, true);
  }
  @Post('friends/requests/:id/reject')
  @HttpCode(200)
  reject(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    return this.svc.respond(u.id, id, false);
  }
  @Delete('friends/requests/:id')  // người gửi hủy lời mời của mình
  async cancel(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    const r = await this.db.friendship.deleteMany({ where: { id, requesterId: u.id, status: 'PENDING' } });
    if (!r.count) throw new NotFoundException({ code: 'REQUEST_NOT_FOUND', message: 'Không tìm thấy lời mời' });
    return { ok: true };
  }
  @Delete('friends/:userId')
  async remove(@CurrentUser() u: AuthUser, @Param('userId', ParseUUIDPipe) other: string) {
    const r = await this.db.friendship.deleteMany({ where: { pairKey: pairKey(u.id, other), status: 'ACCEPTED' } });
    if (!r.count) throw new NotFoundException({ code: 'NOT_FRIENDS', message: 'Không phải bạn bè' });
    return { ok: true };
  }

  @Get('blocks')
  async blocks(@CurrentUser() u: AuthUser) {
    const b = await this.db.userBlock.findMany({ where: { blockerId: u.id }, include: { blocked: { include: { profile: true } } } });
    return b.map((x) => ({ userId: x.blockedId, displayName: x.blocked.profile?.displayName ?? '?', since: x.createdAt }));
  }
  @Post('blocks')
  block(@CurrentUser() u: AuthUser, @Body() d: UserIdDto) {
    return this.svc.block(u.id, d.userId);
  }
  @Delete('blocks/:userId')
  async unblock(@CurrentUser() u: AuthUser, @Param('userId', ParseUUIDPipe) other: string) {
    await this.db.userBlock.deleteMany({ where: { blockerId: u.id, blockedId: other } });
    return { ok: true };
  }
}
