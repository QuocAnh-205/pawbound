import { ConflictException, ForbiddenException, Inject, Injectable, UnauthorizedException } from '@nestjs/common';
import { JwtService } from '@nestjs/jwt';
import * as argon2 from 'argon2';
import { randomBytes, randomUUID } from 'node:crypto';
import { AuditService } from '../audit/audit.service';
import { sha256 } from '../common/security';
import { APP_ENV, AppEnv } from '../config/env';
import { PrismaService } from '../prisma/prisma.service';
import { RegisterDto } from './auth.dto';

// Argon2id với tham số OWASP khuyến nghị (m=19 MiB, t=2, p=1).
const ARGON = { type: argon2.argon2id, memoryCost: 19456, timeCost: 2, parallelism: 1 } as const;
// Hash giả để so sánh khi email không tồn tại -> thời gian phản hồi như nhau (không lộ email nào có tài khoản).
let dummyHash: Promise<string> | undefined;

@Injectable()
export class AuthService {
  constructor(
    private readonly db: PrismaService,
    private readonly jwt: JwtService,
    private readonly audit: AuditService,
    @Inject(APP_ENV) private readonly env: AppEnv,
  ) {}

  async register(dto: RegisterDto, ip: string) {
    const email = dto.email.trim().toLowerCase();
    const name = dto.displayName.trim();
    const exists = await this.db.user.findUnique({ where: { email } });
    const nameTaken = await this.db.playerProfile.findUnique({ where: { displayNameLower: name.toLowerCase() } });
    if (exists || nameTaken) throw new ConflictException({ code: 'ACCOUNT_EXISTS', message: 'Email hoặc tên hiển thị đã được dùng' });
    const passwordHash = await argon2.hash(dto.password, ARGON);
    const user = await this.db.user.create({
      data: { email, passwordHash, profile: { create: { displayName: name, displayNameLower: name.toLowerCase() } } },
    });
    await this.audit.record('user', user.id, 'auth.register', undefined, { ipHash: sha256(ip).slice(0, 16) });
    return this.issueTokens(user.id, user.role, randomUUID());
  }

  async login(emailRaw: string, password: string, ip: string) {
    const email = emailRaw.trim().toLowerCase();
    const user = await this.db.user.findUnique({ where: { email } });
    dummyHash ??= argon2.hash(randomBytes(16).toString('hex'), ARGON);
    const ok = await argon2.verify(user?.passwordHash ?? (await dummyHash), password);
    if (!user || !ok) {
      await this.audit.record('user', user?.id ?? null, 'auth.login_failed', undefined, { ipHash: sha256(ip).slice(0, 16) });
      throw new UnauthorizedException({ code: 'INVALID_CREDENTIALS', message: 'Email hoặc mật khẩu không đúng' });
    }
    if (user.status !== 'ACTIVE') throw new ForbiddenException({ code: 'ACCOUNT_DISABLED', message: 'Tài khoản bị khóa' });
    await this.audit.record('user', user.id, 'auth.login');
    return this.issueTokens(user.id, user.role, randomUUID());
  }

  // Xoay vòng refresh token. Dùng lại token đã bị thay -> nghi bị đánh cắp -> thu hồi cả họ.
  async refresh(token: string) {
    const row = await this.db.refreshToken.findUnique({ where: { tokenHash: sha256(token) }, include: { user: true } });
    if (!row) throw new UnauthorizedException({ code: 'INVALID_REFRESH', message: 'Refresh token không hợp lệ' });
    if (row.revokedAt) {
      await this.db.refreshToken.updateMany({ where: { familyId: row.familyId, revokedAt: null }, data: { revokedAt: new Date() } });
      await this.audit.record('system', row.userId, 'auth.refresh_reuse_detected', row.familyId);
      throw new UnauthorizedException({ code: 'REFRESH_REUSED', message: 'Phiên đã bị thu hồi, hãy đăng nhập lại' });
    }
    if (row.expiresAt < new Date()) throw new UnauthorizedException({ code: 'REFRESH_EXPIRED', message: 'Phiên hết hạn' });
    if (row.user.status !== 'ACTIVE') throw new ForbiddenException({ code: 'ACCOUNT_DISABLED', message: 'Tài khoản bị khóa' });
    const next = await this.issueTokens(row.userId, row.user.role, row.familyId);
    // Chỉ thu hồi nếu chưa ai xoay token này (chống hai request refresh song song cùng thành công)
    const res = await this.db.refreshToken.updateMany({
      where: { id: row.id, revokedAt: null },
      data: { revokedAt: new Date(), replacedById: next.refreshTokenId },
    });
    if (res.count !== 1) {
      await this.db.refreshToken.updateMany({ where: { familyId: row.familyId, revokedAt: null }, data: { revokedAt: new Date() } });
      throw new UnauthorizedException({ code: 'REFRESH_REUSED', message: 'Phiên đã bị thu hồi, hãy đăng nhập lại' });
    }
    return next;
  }

  async logout(token: string) {
    const row = await this.db.refreshToken.findUnique({ where: { tokenHash: sha256(token) } });
    if (row) {
      await this.db.refreshToken.updateMany({ where: { familyId: row.familyId, revokedAt: null }, data: { revokedAt: new Date() } });
      await this.audit.record('user', row.userId, 'auth.logout');
    }
    return { ok: true };
  }

  private async issueTokens(userId: string, role: 'PLAYER' | 'ADMIN', familyId: string) {
    const accessToken = await this.jwt.signAsync({ sub: userId, role, typ: 'access' }, { expiresIn: this.env.accessTtlS });
    const refreshToken = 'rt_' + randomBytes(32).toString('base64url');
    const row = await this.db.refreshToken.create({
      data: { userId, familyId, tokenHash: sha256(refreshToken), expiresAt: new Date(Date.now() + this.env.refreshTtlS * 1000) },
    });
    return { accessToken, refreshToken, refreshTokenId: row.id, expiresIn: this.env.accessTtlS };
  }
}
