# ADR-005 — Backend: NestJS + TypeScript, PostgreSQL, Prisma, Redis khi cần

- **Trạng thái:** Accepted (version pin ở Phase 11)
- **Ngày:** 2026-11-06

## Quyết định
- Modular monolith NestJS, API version `/api/v1`, OpenAPI.
- PostgreSQL là nguồn sự thật. Prisma lo schema, migration và client. Nếu phiên bản Prisma hiện hành không tương thích với NestJS ở Phase 11, ghi lại và cân nhắc Drizzle.
- Redis chỉ cho presence, rate-limit và session cache, không chứa dữ liệu duy nhất.
- Password: Argon2id. Access token ngắn hạn (JWT). Refresh token lưu dạng hash và xoay vòng.
- Game server giao tiếp với Backend qua HTTP API nội bộ có service credential (persistence interface), không truy cập DB trực tiếp.
- Môi trường máy dev: Node.js v24 (đã có). Chuyển cache npm sang ổ D (`npm config set cache D:\DevTools\npm-cache`).
