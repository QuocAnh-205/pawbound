import { Controller, DefaultValuePipe, Get, HttpCode, Param, ParseIntPipe, ParseUUIDPipe, Post, Query, UseGuards } from '@nestjs/common';
import { ApiBearerAuth, ApiTags } from '@nestjs/swagger';
import { AuditService } from '../audit/audit.service';
import { AuthUser, CurrentUser, JwtAuthGuard, Roles, RolesGuard } from '../common/security';
import { PrismaService } from '../prisma/prisma.service';

@ApiTags('admin')
@ApiBearerAuth()
@UseGuards(JwtAuthGuard, RolesGuard)
@Roles('ADMIN')
@Controller({ path: 'admin', version: '1' })
export class AdminController {
  constructor(private readonly db: PrismaService, private readonly audit: AuditService) {}

  @Get('users')
  async users(@Query('page', new DefaultValuePipe(1), ParseIntPipe) page: number,
              @Query('size', new DefaultValuePipe(20), ParseIntPipe) size: number) {
    const take = Math.min(Math.max(size, 1), 100);
    const skip = (Math.max(page, 1) - 1) * take;
    const [total, rows] = await Promise.all([
      this.db.user.count(),
      this.db.user.findMany({ skip, take, orderBy: { createdAt: 'asc' }, include: { profile: true } }),
    ]);
    return { page: Math.max(page, 1), size: take, total,
             items: rows.map((u) => ({ id: u.id, email: u.email, status: u.status, role: u.role, displayName: u.profile?.displayName })) };
  }

  @Post('users/:id/ban')
  @HttpCode(200)
  async ban(@CurrentUser() admin: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    await this.db.$transaction([
      this.db.user.update({ where: { id }, data: { status: 'BANNED' } }),
      this.db.refreshToken.updateMany({ where: { userId: id, revokedAt: null }, data: { revokedAt: new Date() } }),
    ]);
    await this.audit.record('admin', admin.id, 'admin.ban', id);
    return { ok: true };
  }
}
