import { Body, Controller, Get, HttpCode, Ip, Post, UseGuards } from '@nestjs/common';
import { ApiBearerAuth, ApiTags } from '@nestjs/swagger';
import { Throttle } from '@nestjs/throttler';
import { AuthUser, CurrentUser, JwtAuthGuard } from '../common/security';
import { PrismaService } from '../prisma/prisma.service';
import { LoginDto, RefreshDto, RegisterDto } from './auth.dto';
import { AuthService } from './auth.service';

// Giới hạn đọc lúc chạy (mặc định an toàn); test e2e có thể nới giới hạn ĐĂNG KÝ qua biến môi trường.
const limit = (name: string, def: number) => () => Number(process.env[name] ?? def);

const strip = <T extends { refreshTokenId?: string }>(t: T) => {
  const { refreshTokenId: _omit, ...rest } = t;
  return rest;
};

@ApiTags('auth')
@Controller({ path: 'auth', version: '1' })
export class AuthController {
  constructor(private readonly auth: AuthService) {}

  @Post('register')
  @Throttle({ default: { limit: limit('THROTTLE_REGISTER_PER_MIN', 5), ttl: 60_000 } })
  async register(@Body() dto: RegisterDto, @Ip() ip: string) {
    return strip(await this.auth.register(dto, ip));
  }

  @Post('login')
  @HttpCode(200)
  @Throttle({ default: { limit: limit('THROTTLE_LOGIN_PER_MIN', 10), ttl: 60_000 } })
  async login(@Body() dto: LoginDto, @Ip() ip: string) {
    return strip(await this.auth.login(dto.email, dto.password, ip));
  }

  @Post('refresh')
  @HttpCode(200)
  @Throttle({ default: { limit: 30, ttl: 60_000 } })
  async refresh(@Body() dto: RefreshDto) {
    return strip(await this.auth.refresh(dto.refreshToken));
  }

  @Post('logout')
  @HttpCode(200)
  logout(@Body() dto: RefreshDto) {
    return this.auth.logout(dto.refreshToken);
  }
}

@ApiTags('users')
@ApiBearerAuth()
@UseGuards(JwtAuthGuard)
@Controller({ path: 'me', version: '1' })
export class MeController {
  constructor(private readonly db: PrismaService) {}
  @Get()
  async me(@CurrentUser() u: AuthUser) {
    const user = await this.db.user.findUniqueOrThrow({ where: { id: u.id }, include: { profile: true } });
    return { id: user.id, email: user.email, role: user.role, displayName: user.profile?.displayName, createdAt: user.createdAt };
  }
}
