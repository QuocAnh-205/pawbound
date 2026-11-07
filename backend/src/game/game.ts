import { Body, ConflictException, Controller, Get, HttpCode, Injectable, NotFoundException, Param, Post, Put,
  ServiceUnavailableException, UnprocessableEntityException, UseGuards } from '@nestjs/common';
import { ApiBearerAuth, ApiHeader, ApiProperty, ApiPropertyOptional, ApiTags } from '@nestjs/swagger';
import { Prisma } from '@prisma/client';
import { IsIn, IsInt, IsObject, IsOptional, IsString, IsUUID, Length, Matches, Max, Min } from 'class-validator';
import { AuditService } from '../audit/audit.service';
import { CharactersService } from '../characters/characters';
import { AuthUser, CurrentUser, JwtAuthGuard, ServiceTokenGuard } from '../common/security';
import { PrismaService } from '../prisma/prisma.service';
import { TicketService } from '../tickets/ticket.service';

export const HEARTBEAT_STALE_MS = 30_000;

// ============================================================================ DTO
export class HeartbeatDto {
  @ApiProperty({ example: 'dev-1' }) @IsString() @Matches(/^[a-z0-9-]{2,32}$/) id: string;
  @ApiProperty({ example: 'vn' }) @IsString() @Length(2, 16) region: string;
  @ApiProperty({ example: '127.0.0.1' }) @IsString() @Length(3, 255) address: string;
  @ApiProperty({ example: 27015 }) @IsInt() @Min(1) @Max(65535) port: number;
  @ApiProperty({ example: 32 }) @IsInt() @Min(1) @Max(1000) capacity: number;
  @ApiProperty({ example: 0 }) @IsInt() @Min(0) @Max(1000) currentPlayers: number;
  @ApiProperty({ example: '0.1.0' }) @IsString() @Length(1, 32) buildVersion: string;
  @ApiPropertyOptional({ enum: ['ONLINE', 'DRAINING'] }) @IsOptional() @IsIn(['ONLINE', 'DRAINING']) status?: 'ONLINE' | 'DRAINING';
}

export class JoinDto {
  @ApiProperty() @IsUUID() characterId: string;
  @ApiPropertyOptional({ example: 'vn' }) @IsOptional() @IsString() @Length(2, 16) region?: string;
}

export class SaveStateDto {
  @ApiProperty({ description: 'stateVersion server đọc được lần trước (optimistic concurrency)' })
  @IsInt() @Min(0) expectedVersion: number;
  @ApiProperty({ description: 'Khóa idempotency: gửi lại cùng key -> không ghi lặp' })
  @IsString() @Matches(/^[A-Za-z0-9:_-]{8,80}$/) idempotencyKey: string;
  @ApiProperty() @IsString() @Matches(/^[a-z0-9-]{2,32}$/) serverId: string;
  @ApiProperty({ type: Object }) @IsObject() state: Record<string, unknown>;
}

// Kiểm tra cấu trúc trạng thái do game server gửi (không tin dữ liệu chỉ vì đến từ mạng nội bộ).
export function validateState(s: Record<string, unknown>): string | null {
  const num = (v: unknown, lo: number, hi: number) => typeof v === 'number' && Number.isFinite(v) && v >= lo && v <= hi;
  const pos = s.pos as Record<string, unknown> | undefined;
  if (!pos || !num(pos.x, 0, 100000) || !num(pos.y, -1000, 1000) || !num(pos.z, 0, 100000)) return 'pos không hợp lệ';
  const v = s.vitals as Record<string, unknown> | undefined;
  if (!v || !['hp', 'hunger', 'thirst', 'stamina', 'rest'].every((k) => num(v[k], 0, 100))) return 'vitals không hợp lệ';
  const inv = s.inventory;
  if (!Array.isArray(inv) || inv.length > 6) return 'inventory phải là mảng ≤ 6 ô';
  for (const slot of inv) {
    if (slot === null) continue;
    const o = slot as Record<string, unknown>;
    if (typeof o.item !== 'string' || !/^[a-z0-9_]{1,32}$/.test(o.item) || !Number.isInteger(o.count) || !num(o.count, 1, 99))
      return 'ô inventory không hợp lệ';
  }
  if (JSON.stringify(s).length > 16_384) return 'state quá lớn';
  return null;
}

// ============================================================================ matchmaking (người chơi)
@Injectable()
export class MatchmakingService {
  constructor(private readonly db: PrismaService, private readonly tickets: TicketService, private readonly chars: CharactersService,
              private readonly audit: AuditService) {}

  async join(userId: string, dto: JoinDto) {
    const ch = await this.chars.owned(userId, dto.characterId);
    const user = await this.db.user.findUniqueOrThrow({ where: { id: userId } });
    const fresh = new Date(Date.now() - HEARTBEAT_STALE_MS);
    const servers = await this.db.serverInstance.findMany({ where: { status: 'ONLINE', lastHeartbeatAt: { gte: fresh } } });
    // Ưu tiên đúng vùng, còn chỗ, tải thấp nhất
    const ranked = servers
      .filter((s) => s.currentPlayers < s.capacity)
      .sort((a, b) => Number(b.region === dto.region) - Number(a.region === dto.region) || a.currentPlayers / a.capacity - b.currentPlayers / b.capacity);
    // Party: nếu đồng đội đang chơi trên một server còn chỗ -> xếp vào đúng server đó (đi cùng nhau)
    let srv = ranked[0];
    const pm = await this.db.partyMember.findUnique({ where: { userId } });
    if (pm) {
      const mates = await this.db.partyMember.findMany({ where: { partyId: pm.partyId, userId: { not: userId } }, select: { userId: true } });
      const live = await this.db.gameSession.findMany({
        where: { userId: { in: mates.map((m) => m.userId) }, endedAt: null, startedAt: { gt: new Date(Date.now() - 12 * 3600_000) } },
        orderBy: { startedAt: 'desc' }, select: { serverId: true },
      });
      const withMates = live.map((s) => ranked.find((r) => r.id === s.serverId)).find((r) => r !== undefined);
      if (withMates) srv = withMates;
    }
    if (!srv) throw new ServiceUnavailableException({ code: 'NO_SERVER_AVAILABLE', message: 'Hiện không có server trống, thử lại sau' });
    const { ticket, claims } = this.tickets.issue(user.numericId, ch.numericId, srv.id);
    await this.db.$transaction([
      this.db.sessionTicket.create({ data: { jti: claims.jti, userId, characterId: ch.id, serverId: srv.id, expiresAt: new Date(claims.exp * 1000) } }),
      this.db.gameSession.create({ data: { userId, characterId: ch.id, serverId: srv.id, ticketJti: claims.jti } }),
    ]);
    await this.audit.record('user', userId, 'matchmaking.join', srv.id, { character: ch.id });
    return { server: { id: srv.id, address: srv.address, port: srv.port, region: srv.region }, ticket, expiresAt: claims.exp };
  }
}

@ApiTags('matchmaking')
@ApiBearerAuth()
@Controller({ version: '1' })
export class MatchmakingController {
  constructor(private readonly mm: MatchmakingService, private readonly db: PrismaService) {}

  @Post('matchmaking/join')
  @UseGuards(JwtAuthGuard)
  @HttpCode(200)
  join(@CurrentUser() u: AuthUser, @Body() dto: JoinDto) {
    return this.mm.join(u.id, dto);
  }

  @Get('servers')
  async servers() {
    const fresh = new Date(Date.now() - HEARTBEAT_STALE_MS);
    const s = await this.db.serverInstance.findMany({ where: { lastHeartbeatAt: { gte: fresh } }, orderBy: { id: 'asc' } });
    return s.map(({ id, region, currentPlayers, capacity, status }) => ({ id, region, currentPlayers, capacity, status }));
  }
}

// ============================================================================ API nội bộ (game server)
@ApiTags('internal (game server)')
@ApiHeader({ name: 'X-Service-Token', required: true })
@UseGuards(ServiceTokenGuard)
@Controller({ path: 'internal', version: '1' })
export class InternalController {
  constructor(private readonly db: PrismaService, private readonly audit: AuditService) {}

  @Post('servers/heartbeat')
  @HttpCode(200)
  async heartbeat(@Body() d: HeartbeatDto) {
    await this.db.serverInstance.upsert({
      where: { id: d.id },
      create: { ...d, status: d.status ?? 'ONLINE', lastHeartbeatAt: new Date() },
      update: { ...d, status: d.status ?? 'ONLINE', lastHeartbeatAt: new Date() },
    });
    return { ok: true, staleAfterMs: HEARTBEAT_STALE_MS };
  }

  private async byNumeric(numericId: string) {
    if (!/^\d{1,18}$/.test(numericId)) throw new NotFoundException({ code: 'CHARACTER_NOT_FOUND', message: 'Không tìm thấy' });
    const c = await this.db.dogCharacter.findFirst({ where: { numericId: BigInt(numericId), deletedAt: null } });
    if (!c) throw new NotFoundException({ code: 'CHARACTER_NOT_FOUND', message: 'Không tìm thấy nhân vật' });
    return c;
  }

  @Get('characters/:numericId/state')
  async load(@Param('numericId') numericId: string) {
    const c = await this.byNumeric(numericId);
    const u = await this.db.user.findUniqueOrThrow({ where: { id: c.userId }, select: { chatMutedUntil: true } });
    // chatMutedUntil (unix giây): game server dùng cho chat gần; null = không bị cấm
    const muted = u.chatMutedUntil && u.chatMutedUntil > new Date() ? Math.floor(u.chatMutedUntil.getTime() / 1000) : null;
    return { characterId: c.id, numericId, breedId: c.breedId, name: c.name, state: c.state, stateVersion: c.stateVersion, chatMutedUntil: muted,
             blockedUserIds: await this.blockedNumeric(c.userId) };
  }

  // Người chặn / bị chặn (cả hai chiều), dạng id số — game server lọc chat gần.
  private async blockedNumeric(userId: string) {
    const bs = await this.db.userBlock.findMany({ where: { OR: [{ blockerId: userId }, { blockedId: userId }] },
                                                  include: { blocker: { select: { numericId: true } }, blocked: { select: { numericId: true } } } });
    return bs.map((b) => (b.blockerId === userId ? b.blocked.numericId : b.blocker.numericId).toString());
  }

  @Get('users/:numericId/blocks')
  async blocks(@Param('numericId') numericId: string) {
    if (!/^\d{1,18}$/.test(numericId)) throw new NotFoundException({ code: 'USER_NOT_FOUND', message: 'Không tìm thấy' });
    const u = await this.db.user.findUnique({ where: { numericId: BigInt(numericId) }, select: { id: true, chatMutedUntil: true } });
    if (!u) throw new NotFoundException({ code: 'USER_NOT_FOUND', message: 'Không tìm thấy' });
    const muted = u.chatMutedUntil && u.chatMutedUntil > new Date() ? Math.floor(u.chatMutedUntil.getTime() / 1000) : null;
    return { blockedUserIds: await this.blockedNumeric(u.id), chatMutedUntil: muted };
  }

  // Lưu trạng thái: idempotent theo key; optimistic concurrency theo stateVersion (409 nếu server khác đã ghi trước).
  @Put('characters/:numericId/state')
  async save(@Param('numericId') numericId: string, @Body() d: SaveStateDto) {
    const bad = validateState(d.state);
    if (bad) throw new UnprocessableEntityException({ code: 'INVALID_STATE', message: bad });
    const c = await this.byNumeric(numericId);
    return this.db.$transaction(async (tx) => {
      const prev = await tx.saveOperation.findUnique({ where: { characterId_idempotencyKey: { characterId: c.id, idempotencyKey: d.idempotencyKey } } });
      if (prev) return { stateVersion: prev.resultVersion, idempotent: true };
      const upd = await tx.dogCharacter.updateMany({
        where: { id: c.id, stateVersion: d.expectedVersion },
        data: { state: d.state as Prisma.InputJsonValue, stateVersion: { increment: 1 } },
      });
      if (upd.count !== 1)
        throw new ConflictException({ code: 'STATE_VERSION_CONFLICT', message: 'stateVersion đã thay đổi — nạp lại trước khi lưu' });
      const version = d.expectedVersion + 1;
      await tx.saveOperation.create({ data: { characterId: c.id, idempotencyKey: d.idempotencyKey, resultVersion: version, serverId: d.serverId } });
      await this.audit.record('server', d.serverId, 'character.save', c.id, { version }, tx);
      return { stateVersion: version, idempotent: false };
    });
  }

  @Post('sessions/:jti/end')
  @HttpCode(200)
  async endSession(@Param('jti') jti: string, @Body() body: { reason?: string }) {
    const r = await this.db.gameSession.updateMany({ where: { ticketJti: jti, endedAt: null },
      data: { endedAt: new Date(), endReason: String(body?.reason ?? 'disconnect').slice(0, 64) } });
    return { ended: r.count };
  }
}
