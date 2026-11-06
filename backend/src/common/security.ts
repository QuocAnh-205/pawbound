import {
  CanActivate,
  ExecutionContext,
  ForbiddenException,
  Inject,
  Injectable,
  SetMetadata,
  UnauthorizedException,
  createParamDecorator,
} from '@nestjs/common';
import { Reflector } from '@nestjs/core';
import { JwtService } from '@nestjs/jwt';
import { createHash, timingSafeEqual } from 'node:crypto';
import { APP_ENV, AppEnv } from '../config/env';

export interface AuthUser {
  id: string;
  role: 'PLAYER' | 'ADMIN';
}

export const CurrentUser = createParamDecorator((_: unknown, ctx: ExecutionContext): AuthUser => {
  return ctx.switchToHttp().getRequest().user;
});

// Access token (JWT HS256, hạn ngắn). Không tin claim nào ngoài sub/role do chính server ký.
@Injectable()
export class JwtAuthGuard implements CanActivate {
  constructor(private readonly jwt: JwtService) {}
  async canActivate(ctx: ExecutionContext) {
    const req = ctx.switchToHttp().getRequest();
    const h: string | undefined = req.headers['authorization'];
    if (!h?.startsWith('Bearer ')) throw new UnauthorizedException('Thiếu access token');
    try {
      const p = await this.jwt.verifyAsync<{ sub: string; role: AuthUser['role']; typ: string }>(h.slice(7));
      if (p.typ !== 'access') throw new Error('sai loại token');
      req.user = { id: p.sub, role: p.role } satisfies AuthUser;
      return true;
    } catch {
      throw new UnauthorizedException('Access token không hợp lệ hoặc hết hạn');
    }
  }
}

export const ROLES = 'roles';
export const Roles = (...roles: AuthUser['role'][]) => SetMetadata(ROLES, roles);

@Injectable()
export class RolesGuard implements CanActivate {
  constructor(private readonly reflector: Reflector) {}
  canActivate(ctx: ExecutionContext) {
    const need = this.reflector.getAllAndOverride<AuthUser['role'][]>(ROLES, [ctx.getHandler(), ctx.getClass()]);
    if (!need?.length) return true;
    const user: AuthUser | undefined = ctx.switchToHttp().getRequest().user;
    if (!user || !need.includes(user.role)) throw new ForbiddenException('Không đủ quyền');
    return true;
  }
}

// API nội bộ cho game server: header X-Service-Token, so sánh thời gian hằng (chống timing attack).
@Injectable()
export class ServiceTokenGuard implements CanActivate {
  private readonly expected: Buffer;
  constructor(@Inject(APP_ENV) env: AppEnv) {
    this.expected = createHash('sha256').update(env.serviceToken).digest();
  }
  canActivate(ctx: ExecutionContext) {
    const got: string | undefined = ctx.switchToHttp().getRequest().headers['x-service-token'];
    const h = createHash('sha256').update(got ?? '').digest();
    if (!got || !timingSafeEqual(h, this.expected)) throw new UnauthorizedException('Service token không hợp lệ');
    return true;
  }
}

export const sha256 = (s: string) => createHash('sha256').update(s).digest('hex');
