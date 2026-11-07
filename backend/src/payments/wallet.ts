import { Body, ConflictException, Controller, Get, Inject, Injectable, NotFoundException, Param, Post, UnprocessableEntityException,
  UseGuards } from '@nestjs/common';
import { ApiBearerAuth, ApiHeader, ApiProperty, ApiTags } from '@nestjs/swagger';
import { IsInt, IsString, Matches, Max, Min } from 'class-validator';
import { AuthUser, CurrentUser, JwtAuthGuard, ServiceTokenGuard } from '../common/security';
import { PrismaService } from '../prisma/prisma.service';
import { isUniqueViolation } from '../social/notifications';
import { ECONOMY, EconomyConfig } from './economy.config';

export class EarnDto {
  @ApiProperty() @IsInt() @Min(1) @Max(100000) amount: number;
  @ApiProperty({ example: 'forage' }) @IsString() @Matches(/^[a-z_]{2,32}$/) reason: string;
  @ApiProperty({ description: 'Khóa duy nhất của lần cộng (server:nhân vật:lượt) — gửi lại không cộng hai lần' })
  @IsString() @Matches(/^[A-Za-z0-9:_-]{8,100}$/) refKey: string;
}
export class BuyDto {
  @ApiProperty() @IsString() @Matches(/^[A-Za-z0-9_-]{8,64}$/) idempotencyKey: string;
}

@Injectable()
export class WalletService {
  constructor(private readonly db: PrismaService, @Inject(ECONOMY) private readonly eco: EconomyConfig) {}

  async balance(userId: string) {
    const w = await this.db.wallet.findUnique({ where: { userId } });
    const owned = await this.db.userCosmetic.findMany({ where: { userId }, select: { cosmeticId: true, acquiredAt: true } });
    return { currency: 'bones', balance: w?.balance ?? 0, cosmetics: owned };
  }

  // Chỉ game server (token dịch vụ) được cộng tiền; có trần mỗi lần và idempotent theo refKey.
  async earn(userId: string, d: EarnDto) {
    if (d.amount > this.eco.earnCapPerCall)
      throw new UnprocessableEntityException({ code: 'EARN_CAP', message: `Tối đa ${this.eco.earnCapPerCall} mỗi lần` });
    try {
      return await this.db.$transaction(async (tx) => {
        const prev = await tx.currencyTx.findUnique({ where: { refKey: d.refKey } });
        if (prev) return { balance: prev.balanceAfter, duplicate: true };
        const w = await tx.wallet.upsert({ where: { userId }, create: { userId, balance: d.amount }, update: { balance: { increment: d.amount } } });
        await tx.currencyTx.create({ data: { userId, delta: d.amount, reason: d.reason, refKey: d.refKey, balanceAfter: w.balance } });
        return { balance: w.balance, duplicate: false };
      });
    } catch (e) {
      if (isUniqueViolation(e)) {
        const prev = await this.db.currencyTx.findUnique({ where: { refKey: d.refKey } });
        if (prev) return { balance: prev.balanceAfter, duplicate: true };
      }
      throw e;
    }
  }

  async buy(userId: string, cosmeticId: string, key: string) {
    const item = this.eco.cosmetics.find((c) => c.id === cosmeticId);
    if (!item) throw new NotFoundException({ code: 'COSMETIC_NOT_FOUND', message: 'Không có vật phẩm này' });
    const refKey = `buy:${userId}:${key}`;
    try {
      return await this.db.$transaction(async (tx) => {
        const prev = await tx.currencyTx.findUnique({ where: { refKey } });
        if (prev) return { ok: true, balance: prev.balanceAfter, duplicate: true };
        if (await tx.userCosmetic.findUnique({ where: { userId_cosmeticId: { userId, cosmeticId } } }))
          throw new ConflictException({ code: 'ALREADY_OWNED', message: 'Bạn đã có vật phẩm này' });
        // Trừ có điều kiện: hai lần mua song song không làm số dư âm (thêm CHECK balance >= 0 ở DB)
        const r = await tx.wallet.updateMany({ where: { userId, balance: { gte: item.price } }, data: { balance: { decrement: item.price } } });
        if (r.count !== 1) throw new UnprocessableEntityException({ code: 'INSUFFICIENT_FUNDS', message: 'Không đủ xương' });
        const w = await tx.wallet.findUniqueOrThrow({ where: { userId } });
        await tx.currencyTx.create({ data: { userId, delta: -item.price, reason: `cosmetic:${item.id}`, refKey, balanceAfter: w.balance } });
        await tx.userCosmetic.create({ data: { userId, cosmeticId } });
        return { ok: true, balance: w.balance, duplicate: false };
      });
    } catch (e) {
      if (isUniqueViolation(e)) throw new ConflictException({ code: 'ALREADY_OWNED', message: 'Bạn đã có vật phẩm này' });
      throw e;
    }
  }
}

@ApiTags('economy')
@ApiBearerAuth()
@UseGuards(JwtAuthGuard)
@Controller({ version: '1' })
export class WalletController {
  constructor(private readonly svc: WalletService, @Inject(ECONOMY) private readonly eco: EconomyConfig) {}
  @Get('wallet')
  wallet(@CurrentUser() u: AuthUser) {
    return this.svc.balance(u.id);
  }
  @Get('shop/cosmetics')
  catalog() {
    return this.eco.cosmetics;
  }
  @Post('shop/cosmetics/:id/buy')
  buy(@CurrentUser() u: AuthUser, @Param('id') id: string, @Body() d: BuyDto) {
    return this.svc.buy(u.id, id, d.idempotencyKey);
  }
}

@ApiTags('internal (game server)')
@ApiHeader({ name: 'X-Service-Token', required: true })
@UseGuards(ServiceTokenGuard)
@Controller({ path: 'internal/wallet', version: '1' })
export class InternalWalletController {
  constructor(private readonly svc: WalletService, private readonly db: PrismaService) {}
  @Post(':numericId/earn')
  async earn(@Param('numericId') numericId: string, @Body() d: EarnDto) {
    if (!/^\d{1,18}$/.test(numericId)) throw new NotFoundException({ code: 'USER_NOT_FOUND', message: 'Không tìm thấy' });
    const u = await this.db.user.findUnique({ where: { numericId: BigInt(numericId) } });
    if (!u) throw new NotFoundException({ code: 'USER_NOT_FOUND', message: 'Không tìm thấy' });
    return this.svc.earn(u.id, d);
  }
}
