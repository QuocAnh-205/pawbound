import { ArgumentsHost, Catch, ExceptionFilter, HttpException, HttpStatus, Logger } from '@nestjs/common';
import { Prisma } from '@prisma/client';

// Ánh xạ lỗi thống nhất: { statusCode, code, message }. Lỗi không lường trước -> 500 KHÔNG lộ chi tiết nội bộ.
@Catch()
export class AllExceptionsFilter implements ExceptionFilter {
  private readonly log = new Logger('http');
  catch(err: unknown, host: ArgumentsHost) {
    const res = host.switchToHttp().getResponse();
    const req = host.switchToHttp().getRequest();
    if (err instanceof HttpException) {
      const body = err.getResponse();
      const status = err.getStatus();
      const message = typeof body === 'string' ? body : (body as { message?: unknown }).message ?? err.message;
      const code = typeof body === 'object' && body && 'code' in body ? (body as { code: string }).code : HttpStatus[status];
      // Trường bổ sung có chủ đích do code ném ra (vd amount/currency cho 402, mutedUntil) — chỉ kiểu nguyên thủy, không lộ stack.
      const extra: Record<string, string | number | boolean | null> = {};
      if (typeof body === 'object' && body)
        for (const [k, v] of Object.entries(body))
          if (!['statusCode', 'code', 'message', 'error'].includes(k) && (v === null || ['string', 'number', 'boolean'].includes(typeof v))) extra[k] = v as never;
      return res.status(status).json({ statusCode: status, code, message, ...extra });
    }
    if (err instanceof Prisma.PrismaClientKnownRequestError) {
      if (err.code === 'P2002') return res.status(409).json({ statusCode: 409, code: 'CONFLICT', message: 'Dữ liệu đã tồn tại' });
      if (err.code === 'P2025') return res.status(404).json({ statusCode: 404, code: 'NOT_FOUND', message: 'Không tìm thấy' });
    }
    this.log.error(`${req.method} ${req.url} lỗi không xử lý: ${err instanceof Error ? err.stack : String(err)}`);
    return res.status(500).json({ statusCode: 500, code: 'INTERNAL', message: 'Lỗi máy chủ' });
  }
}
