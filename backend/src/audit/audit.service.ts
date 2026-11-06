import { Injectable, Logger } from '@nestjs/common';
import { Prisma } from '@prisma/client';
import { PrismaService } from '../prisma/prisma.service';

// Chỉ thêm (append-only). KHÔNG ghi mật khẩu, token, ticket.
@Injectable()
export class AuditService {
  private readonly log = new Logger('audit');
  constructor(private readonly db: PrismaService) {}

  async record(actorType: 'user' | 'admin' | 'server' | 'system', actorId: string | null, action: string, target?: string,
                metadata?: Record<string, unknown>, tx?: Prisma.TransactionClient) {
    await (tx ?? this.db).auditLog.create({
      data: { actorType, actorId, action, target, metadata: (metadata ?? undefined) as Prisma.InputJsonValue | undefined },
    });
    this.log.log(JSON.stringify({ actorType, actorId, action, target }));
  }
}
