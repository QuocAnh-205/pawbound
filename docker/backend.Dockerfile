# Backend PAWBOUND (NestJS). Build:  docker build -f docker/backend.Dockerfile -t pawbound-backend:0.1.0 .
# Migration chạy riêng trước khi rollout:  docker run --rm --env-file backend.env pawbound-backend:0.1.0 npx prisma migrate deploy
FROM node:24-bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends openssl && rm -rf /var/lib/apt/lists/*
WORKDIR /app/backend
COPY backend/package.json backend/package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY backend/tsconfig.json backend/tsconfig.build.json ./
COPY backend/prisma prisma
COPY backend/src src
RUN npx prisma generate && npm run build && npm prune --omit=dev

FROM node:24-bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends openssl && rm -rf /var/lib/apt/lists/*
WORKDIR /app/backend
ENV NODE_ENV=production PAWBOUND_DATA_ROOT=/app HOST=0.0.0.0
COPY --from=build --chown=node:node /app/backend/node_modules node_modules
COPY --from=build --chown=node:node /app/backend/dist dist
COPY --chown=node:node backend/prisma prisma
COPY --chown=node:node backend/package.json ./
COPY --chown=node:node data /app/data
USER node
EXPOSE 3000
HEALTHCHECK --interval=15s --timeout=3s --start-period=20s --retries=3 \
  CMD node -e "fetch('http://127.0.0.1:'+(process.env.PORT||3000)+'/api/v1/health/ready').then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))"
STOPSIGNAL SIGTERM
CMD ["node", "dist/main.js"]
