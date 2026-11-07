// Dựng app trên database PostgreSQL TẠM (tạo -> migrate deploy -> xóa). Dùng chung cho các file e2e mới.
import { INestApplication } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { PrismaClient } from '@prisma/client';
import { execSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import request from 'supertest';
import { buildModule, configureApp, loadEnv } from '../src/app.module';
import { PrismaService } from '../src/prisma/prisma.service';

export const ROOT = resolve(__dirname, '..');

export interface Harness {
  app: INestApplication;
  db: PrismaService;
  http: ReturnType<typeof request>;
  close(): Promise<void>;
}

export async function startHarness(tag: string, extraEnv: Record<string, string> = {}): Promise<Harness> {
  for (const line of readFileSync(resolve(ROOT, '.env'), 'utf8').split(/\r?\n/)) {
    const m = /^([A-Z0-9_]+)=(.*)$/.exec(line.trim());
    if (m) process.env[m[1]] = m[2];
  }
  Object.assign(process.env, extraEnv);
  const tempDb = `pawbound_${tag}_${Date.now()}`;
  const baseUrl = new URL(process.env.DATABASE_URL_TEST!);
  const tempUrl = new URL(baseUrl.toString());
  tempUrl.pathname = `/${tempDb}`;
  process.env.DATABASE_URL = tempUrl.toString();
  const admin = new PrismaClient({ datasources: { db: { url: baseUrl.toString() } } });
  await admin.$executeRawUnsafe(`CREATE DATABASE "${tempDb}"`);
  execSync('npx prisma migrate deploy', { cwd: ROOT, stdio: 'pipe', env: process.env });
  const app = await NestFactory.create(buildModule(loadEnv()), { logger: false, rawBody: true });
  configureApp(app);
  await app.init();
  return {
    app,
    db: app.get(PrismaService),
    http: request(app.getHttpServer()),
    async close() {
      await app.close();
      await admin.$executeRawUnsafe(`DROP DATABASE IF EXISTS "${tempDb}" WITH (FORCE)`);  // chỉ xóa DB tạm của chính lần chạy này
      await admin.$disconnect();
    },
  };
}
