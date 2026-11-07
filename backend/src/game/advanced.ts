// Phase 14 — phần Backend của gameplay nâng cao: thông báo đồng đội khi bị bắt, lãnh thổ bang (MVP).
// Quyết định gameplay (ai bị bắt, ai có mặt trong khu) ở game server; Backend kiểm quyền/nhất quán và lưu.
import { Body, ConflictException, Controller, Get, HttpCode, Injectable, NotFoundException, Post, UnprocessableEntityException,
  UseGuards } from '@nestjs/common';
import { ApiHeader, ApiProperty, ApiTags } from '@nestjs/swagger';
import { IsIn, IsInt, IsString, IsUUID, Matches, Max, Min } from 'class-validator';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { AuditService } from '../audit/audit.service';
import { ServiceTokenGuard } from '../common/security';
import { PrismaService } from '../prisma/prisma.service';
import { NotificationsService } from '../social/notifications';

interface TerritoryConfig { upkeep_s: number; protect_s: number; max_per_guild: number; min_members: number }
export function loadTerritoryConfig(): TerritoryConfig {
  const file = resolve(process.env.PAWBOUND_DATA_ROOT ?? resolve(__dirname, '../../..'), 'data/gameplay/advanced.json');
  const t = JSON.parse(readFileSync(file, 'utf8')).territory as TerritoryConfig;
  for (const k of ['upkeep_s', 'protect_s', 'max_per_guild', 'min_members'] as const)
    if (!Number.isInteger(t?.[k]) || t[k] < 1) throw new Error(`advanced.json: territory.${k} không hợp lệ`);
  return t;
}

export class CaptureEventDto {
  @ApiProperty() @IsString() @Matches(/^\d{1,18}$/) userNumericId: string;
  @ApiProperty() @IsInt() @Min(0) captureId: number;
  @ApiProperty({ enum: ['CAPTURED', 'RESCUED', 'RELEASED'] }) @IsIn(['CAPTURED', 'RESCUED', 'RELEASED']) state: string;
  @ApiProperty() @IsString() @Matches(/^[a-z_]{1,32}$/) district: string;
}
export class TerritoryClaimDto {
  @ApiProperty() @IsInt() @Min(0) @Max(10000) zoneId: number;
  @ApiProperty() @IsUUID() guildId: string;
  @ApiProperty() @IsString() @Matches(/^[a-z0-9-]{2,32}$/) serverId: string;
  @ApiProperty() @IsInt() @Min(1) @Max(1000) members: number;
}

// Chống spam: mỗi người chơi tối đa một thông báo "bị bắt" gửi đồng đội trong 10 phút.
export const CAPTURE_NOTIFY_COOLDOWN_MS = 10 * 60_000;

@Injectable()
export class AdvancedService {
  private readonly terr = loadTerritoryConfig();
  constructor(private readonly db: PrismaService, private readonly notes: NotificationsService, private readonly audit: AuditService) {}

  // Đồng đội = cùng party + cùng bang (không gồm bạn bè thường: tránh lộ trạng thái riêng tư quá rộng)
  async teammates(userId: string) {
    const [pm, gm] = await Promise.all([this.db.partyMember.findUnique({ where: { userId } }), this.db.guildMember.findUnique({ where: { userId } })]);
    const ids = new Set<string>();
    if (pm) for (const m of await this.db.partyMember.findMany({ where: { partyId: pm.partyId } })) ids.add(m.userId);
    if (gm) for (const m of await this.db.guildMember.findMany({ where: { guildId: gm.guildId } })) ids.add(m.userId);
    ids.delete(userId);
    return { ids: [...ids], guildId: gm?.guildId ?? null };
  }

  async captureEvent(d: CaptureEventDto) {
    const u = await this.db.user.findUnique({ where: { numericId: BigInt(d.userNumericId) }, include: { profile: true } });
    if (!u) throw new NotFoundException({ code: 'USER_NOT_FOUND', message: 'Không tìm thấy' });
    if (d.state === 'CAPTURED') {
      const recent = await this.db.notification.findFirst({
        where: { type: 'teammate_captured', dedupeKey: { startsWith: `capture:${u.id}:` }, createdAt: { gt: new Date(Date.now() - CAPTURE_NOTIFY_COOLDOWN_MS) } },
      });
      if (recent) return { notified: 0, throttled: true };
    }
    const { ids } = await this.teammates(u.id);
    // Chỉ tên + quận — KHÔNG gửi tọa độ (manh mối chi tiết chỉ có trong game cho đồng đội cùng server)
    const payload = { userId: u.id, displayName: u.profile?.displayName ?? '?', state: d.state, district: d.district };
    const type = d.state === 'CAPTURED' ? 'teammate_captured' : 'teammate_freed';
    for (const id of ids) await this.notes.notify(undefined, id, type, payload, `capture:${u.id}:${d.captureId}:${d.state}`);
    return { notified: ids.length, throttled: false };
  }

  async claim(d: TerritoryClaimDto) {
    if (d.members < this.terr.min_members)
      throw new UnprocessableEntityException({ code: 'TERRITORY_NOT_ENOUGH_MEMBERS', message: `Cần ít nhất ${this.terr.min_members} thành viên có mặt` });
    return this.db.$transaction(async (tx) => {
      const g = await tx.guild.findFirst({ where: { id: d.guildId, disbandedAt: null } });
      if (!g) throw new NotFoundException({ code: 'GUILD_NOT_FOUND', message: 'Không tìm thấy bang hội' });
      // Tạo hàng nếu chưa có rồi KHÓA hàng: hai bang xin chiếm cùng lúc -> tuần tự hóa, một bên thắng
      await tx.$executeRaw`INSERT INTO "Territory" ("zoneId", "updatedAt") VALUES (${d.zoneId}, NOW()) ON CONFLICT ("zoneId") DO NOTHING`;
      await tx.$queryRaw`SELECT "zoneId" FROM "Territory" WHERE "zoneId" = ${d.zoneId} FOR UPDATE`;
      const t = await tx.territory.findUniqueOrThrow({ where: { zoneId: d.zoneId } });
      const now = new Date();
      const owned = t.guildId && t.expiresAt && t.expiresAt > now;
      const upkeep = new Date(now.getTime() + this.terr.upkeep_s * 1000);
      if (owned && t.guildId === d.guildId) {  // duy trì (upkeep): gia hạn
        await tx.territory.update({ where: { zoneId: d.zoneId }, data: { expiresAt: upkeep, serverId: d.serverId } });
        return { zoneId: d.zoneId, guildId: d.guildId, renewed: true, expiresAt: upkeep };
      }
      if (owned && t.protectedTill && t.protectedTill > now)
        throw new ConflictException({ code: 'TERRITORY_PROTECTED', message: 'Khu vừa bị chiếm, đang được bảo hộ', guildId: d.guildId,
                                      protectedTill: t.protectedTill.toISOString() });
      const held = await tx.territory.count({ where: { guildId: d.guildId, expiresAt: { gt: now } } });
      if (held >= this.terr.max_per_guild)
        throw new UnprocessableEntityException({ code: 'TERRITORY_LIMIT', message: `Mỗi bang tối đa ${this.terr.max_per_guild} khu`, guildId: d.guildId });
      await tx.territory.update({ where: { zoneId: d.zoneId },
        data: { guildId: d.guildId, claimedAt: now, expiresAt: upkeep, protectedTill: new Date(now.getTime() + this.terr.protect_s * 1000), serverId: d.serverId } });
      await this.audit.record('server', d.serverId, 'territory.claim', String(d.zoneId), { guildId: d.guildId, previous: owned ? t.guildId : null }, tx);
      return { zoneId: d.zoneId, guildId: d.guildId, renewed: false, previousGuildId: owned ? t.guildId : null, expiresAt: upkeep };
    });
  }
}

@ApiTags('internal (game server)')
@ApiHeader({ name: 'X-Service-Token', required: true })
@UseGuards(ServiceTokenGuard)
@Controller({ path: 'internal', version: '1' })
export class InternalAdvancedController {
  constructor(private readonly svc: AdvancedService) {}
  @Post('events/capture')
  @HttpCode(200)
  capture(@Body() d: CaptureEventDto) {
    return this.svc.captureEvent(d);
  }
  @Post('territory/claim')
  @HttpCode(200)
  claim(@Body() d: TerritoryClaimDto) {
    return this.svc.claim(d);
  }
}

@ApiTags('guild')
@Controller({ path: 'territories', version: '1' })
export class TerritoryController {
  constructor(private readonly db: PrismaService) {}
  @Get()
  async list() {
    const ts = await this.db.territory.findMany({ where: { guildId: { not: null }, expiresAt: { gt: new Date() } }, orderBy: { zoneId: 'asc' } });
    const gs = await this.db.guild.findMany({ where: { id: { in: ts.map((t) => t.guildId!) } }, select: { id: true, name: true, emblem: true } });
    return ts.map((t) => ({ zoneId: t.zoneId, guild: gs.find((g) => g.id === t.guildId) ?? null, expiresAt: t.expiresAt, protectedTill: t.protectedTill }));
  }
}
