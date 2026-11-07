import 'reflect-metadata';
import { Logger } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { readFileSync, existsSync } from 'node:fs';
import { buildModule, configureApp, loadEnv } from './app.module';

// Nạp .env (không cần thư viện dotenv): chỉ dòng KEY=VALUE, không ghi đè biến đã có.
function loadDotEnv(file = '.env') {
  if (!existsSync(file)) return;
  for (const line of readFileSync(file, 'utf8').split(/\r?\n/)) {
    const m = /^([A-Z0-9_]+)=(.*)$/.exec(line.trim());
    if (m && process.env[m[1]] === undefined) process.env[m[1]] = m[2];
  }
}

async function bootstrap() {
  loadDotEnv();
  const env = loadEnv();
  const app = await NestFactory.create(buildModule(env), { logger: ['log', 'warn', 'error'], rawBody: true });
  configureApp(app);
  app.enableShutdownHooks();
  await app.listen(env.port, '127.0.0.1');
  new Logger('main').log(`PAWBOUND backend http://127.0.0.1:${env.port}/api/v1 — tài liệu: /api/docs`);
}

bootstrap().catch((e) => {
  // eslint-disable-next-line no-console
  console.error(e instanceof Error ? e.message : e);
  process.exit(1);
});
