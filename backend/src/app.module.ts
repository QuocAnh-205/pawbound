import { Controller, Get, INestApplication, Logger, Module, ServiceUnavailableException, ValidationPipe, VersioningType } from '@nestjs/common';
import { APP_GUARD } from '@nestjs/core';
import { JwtModule } from '@nestjs/jwt';
import { DocumentBuilder, SwaggerModule } from '@nestjs/swagger';
import { ThrottlerGuard, ThrottlerModule } from '@nestjs/throttler';
import { AdminController } from './admin/admin';
import { AuditService } from './audit/audit.service';
import { AuthController, MeController } from './auth/auth.controller';
import { AuthService } from './auth/auth.service';
import { CharactersController, CharactersService } from './characters/characters';
import { AllExceptionsFilter } from './common/errors';
import { JwtAuthGuard, RolesGuard, ServiceTokenGuard } from './common/security';
import { APP_ENV, AppEnv, loadEnv } from './config/env';
import { AdvancedService, InternalAdvancedController, TerritoryController } from './game/advanced';
import { InternalController, MatchmakingController, MatchmakingService } from './game/game';
import { PrismaService } from './prisma/prisma.service';
import { TicketService } from './tickets/ticket.service';
import { ChatController, ChatService } from './social/chat';
import { FriendsController, FriendsService } from './social/friends';
import { GuildController, GuildService } from './social/guild';
import { ModerationService } from './social/moderation';
import { NotificationsController, NotificationsService } from './social/notifications';
import { PartyController, PartyService } from './social/party';
import { loadSocialConfig, SOCIAL } from './social/social.config';
import { ECONOMY, loadEconomyConfig } from './payments/economy.config';
import { AdminPaymentsController, PaymentsController, PaymentsService } from './payments/payments';
import { MockPaymentProvider, PAYMENT_PROVIDER } from './payments/provider';
import { InternalWalletController, WalletController, WalletService } from './payments/wallet';

@Controller({ path: 'health', version: '1' })
class HealthController {
  constructor(private readonly db: PrismaService) {}
  @Get()
  live() {
    return { status: 'ok' };
  }
  @Get('ready')
  async ready() {
    try {
      await this.db.$queryRaw`SELECT 1`;
      return { status: 'ready', db: 'ok' };
    } catch {
      throw new ServiceUnavailableException({ code: 'DB_UNAVAILABLE', message: 'Database không sẵn sàng' });
    }
  }
}

export function buildModule(env: AppEnv) {
  @Module({
    imports: [
      JwtModule.register({ secret: env.jwtSecret, signOptions: { algorithm: 'HS256' }, verifyOptions: { algorithms: ['HS256'] } }),
      ThrottlerModule.forRoot([{ name: 'default', ttl: 60_000, limit: Number(process.env.THROTTLE_DEFAULT_PER_MIN ?? 120) }]),
    ],
    controllers: [HealthController, AuthController, MeController, CharactersController, MatchmakingController, InternalController, AdminController,
                  FriendsController, PartyController, GuildController, ChatController, NotificationsController,
                  PaymentsController, AdminPaymentsController, WalletController, InternalWalletController,
                  InternalAdvancedController, TerritoryController],
    providers: [
      { provide: APP_ENV, useValue: env },
      { provide: SOCIAL, useValue: loadSocialConfig() },
      { provide: ECONOMY, useValue: loadEconomyConfig() },
      { provide: PAYMENT_PROVIDER, useValue: new MockPaymentProvider(env.mockWebhookSecret) },  // loadEnv chỉ cho phép 'mock' (ngoài production)
      PaymentsService, WalletService, AdvancedService,
      NotificationsService, FriendsService, PartyService, GuildService, ChatService, ModerationService,
      { provide: APP_GUARD, useClass: ThrottlerGuard },
      PrismaService, AuditService, AuthService, CharactersService, TicketService, MatchmakingService,
      JwtAuthGuard, RolesGuard, ServiceTokenGuard,
    ],
  })
  class AppModule {}
  return AppModule;
}

// Cấu hình dùng chung cho main.ts và test e2e.
export function configureApp(app: INestApplication) {
  app.setGlobalPrefix('api');
  app.enableVersioning({ type: VersioningType.URI, defaultVersion: '1' });
  app.useGlobalPipes(new ValidationPipe({ whitelist: true, forbidNonWhitelisted: true, transform: true }));
  app.useGlobalFilters(new AllExceptionsFilter());
  // Log có cấu trúc: method, đường dẫn, mã, thời gian — KHÔNG log body (có mật khẩu/token).
  const log = new Logger('http');
  app.use((req: { method: string; originalUrl: string }, res: { statusCode: number; on: (e: string, f: () => void) => void },
           next: () => void) => {
    const t0 = process.hrtime.bigint();
    res.on('finish', () => {
      const ms = Number(process.hrtime.bigint() - t0) / 1e6;
      log.log(JSON.stringify({ method: req.method, path: req.originalUrl.split('?')[0], status: res.statusCode, ms: Math.round(ms) }));
    });
    next();
  });
  const doc = new DocumentBuilder().setTitle('PAWBOUND API').setVersion('1').addBearerAuth().build();
  SwaggerModule.setup('api/docs', app, SwaggerModule.createDocument(app, doc));
}

export { loadEnv };
