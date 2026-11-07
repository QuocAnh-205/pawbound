-- CreateEnum
CREATE TYPE "PaymentStatus" AS ENUM ('PENDING', 'PAID', 'FAILED', 'EXPIRED', 'CANCELLED', 'REFUNDED');

-- CreateEnum
CREATE TYPE "EntitlementStatus" AS ENUM ('AVAILABLE', 'CONSUMED', 'REVOKED');

-- CreateTable
CREATE TABLE "PaymentOrder" (
    "id" UUID NOT NULL,
    "userId" UUID NOT NULL,
    "purpose" TEXT NOT NULL,
    "amount" INTEGER NOT NULL,
    "currency" TEXT NOT NULL,
    "provider" TEXT NOT NULL,
    "providerRef" TEXT,
    "checkoutUrl" TEXT,
    "idempotencyKey" TEXT NOT NULL,
    "status" "PaymentStatus" NOT NULL DEFAULT 'PENDING',
    "expiresAt" TIMESTAMP(3) NOT NULL,
    "paidAt" TIMESTAMP(3),
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "updatedAt" TIMESTAMP(3) NOT NULL,

    CONSTRAINT "PaymentOrder_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "PaymentEvent" (
    "id" BIGSERIAL NOT NULL,
    "provider" TEXT NOT NULL,
    "providerEventId" TEXT,
    "orderId" UUID,
    "type" TEXT NOT NULL,
    "amount" INTEGER,
    "currency" TEXT,
    "signatureValid" BOOLEAN NOT NULL,
    "outcome" TEXT NOT NULL,
    "payloadSha256" TEXT NOT NULL,
    "receivedAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "PaymentEvent_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "Entitlement" (
    "id" UUID NOT NULL,
    "userId" UUID NOT NULL,
    "kind" TEXT NOT NULL,
    "orderId" UUID NOT NULL,
    "status" "EntitlementStatus" NOT NULL DEFAULT 'AVAILABLE',
    "guildId" UUID,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "consumedAt" TIMESTAMP(3),

    CONSTRAINT "Entitlement_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "Wallet" (
    "userId" UUID NOT NULL,
    "balance" INTEGER NOT NULL DEFAULT 0,
    "updatedAt" TIMESTAMP(3) NOT NULL,

    CONSTRAINT "Wallet_pkey" PRIMARY KEY ("userId")
);

-- CreateTable
CREATE TABLE "CurrencyTx" (
    "id" BIGSERIAL NOT NULL,
    "userId" UUID NOT NULL,
    "delta" INTEGER NOT NULL,
    "reason" TEXT NOT NULL,
    "refKey" TEXT NOT NULL,
    "balanceAfter" INTEGER NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "CurrencyTx_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "UserCosmetic" (
    "userId" UUID NOT NULL,
    "cosmeticId" TEXT NOT NULL,
    "acquiredAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "UserCosmetic_pkey" PRIMARY KEY ("userId","cosmeticId")
);

-- CreateIndex
CREATE INDEX "PaymentOrder_status_expiresAt_idx" ON "PaymentOrder"("status", "expiresAt");

-- CreateIndex
CREATE UNIQUE INDEX "PaymentOrder_userId_idempotencyKey_key" ON "PaymentOrder"("userId", "idempotencyKey");

-- CreateIndex
CREATE UNIQUE INDEX "PaymentOrder_provider_providerRef_key" ON "PaymentOrder"("provider", "providerRef");

-- CreateIndex
CREATE INDEX "PaymentEvent_orderId_idx" ON "PaymentEvent"("orderId");

-- CreateIndex
CREATE UNIQUE INDEX "PaymentEvent_provider_providerEventId_key" ON "PaymentEvent"("provider", "providerEventId");

-- CreateIndex
CREATE UNIQUE INDEX "Entitlement_orderId_key" ON "Entitlement"("orderId");

-- CreateIndex
CREATE UNIQUE INDEX "Entitlement_guildId_key" ON "Entitlement"("guildId");

-- CreateIndex
CREATE INDEX "Entitlement_userId_kind_status_idx" ON "Entitlement"("userId", "kind", "status");

-- CreateIndex
CREATE UNIQUE INDEX "CurrencyTx_refKey_key" ON "CurrencyTx"("refKey");

-- CreateIndex
CREATE INDEX "CurrencyTx_userId_createdAt_idx" ON "CurrencyTx"("userId", "createdAt");

-- AddForeignKey
ALTER TABLE "PaymentOrder" ADD CONSTRAINT "PaymentOrder_userId_fkey" FOREIGN KEY ("userId") REFERENCES "User"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "PaymentEvent" ADD CONSTRAINT "PaymentEvent_orderId_fkey" FOREIGN KEY ("orderId") REFERENCES "PaymentOrder"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "Entitlement" ADD CONSTRAINT "Entitlement_userId_fkey" FOREIGN KEY ("userId") REFERENCES "User"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "Entitlement" ADD CONSTRAINT "Entitlement_orderId_fkey" FOREIGN KEY ("orderId") REFERENCES "PaymentOrder"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "Wallet" ADD CONSTRAINT "Wallet_userId_fkey" FOREIGN KEY ("userId") REFERENCES "User"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "UserCosmetic" ADD CONSTRAINT "UserCosmetic_userId_fkey" FOREIGN KEY ("userId") REFERENCES "User"("id") ON DELETE CASCADE ON UPDATE CASCADE;


-- Viết tay: sổ cái thanh toán và sổ ví là CHỈ THÊM ở mức DB (kể cả code lỗi cũng không sửa/xóa được lịch sử).
CREATE OR REPLACE FUNCTION pawbound_append_only() RETURNS trigger AS $$
BEGIN
  RAISE EXCEPTION 'bang % chi cho phep them (append-only)', TG_TABLE_NAME;
END;
$$ LANGUAGE plpgsql;
CREATE TRIGGER "PaymentEvent_append_only" BEFORE UPDATE OR DELETE ON "PaymentEvent" FOR EACH ROW EXECUTE FUNCTION pawbound_append_only();
CREATE TRIGGER "CurrencyTx_append_only" BEFORE UPDATE OR DELETE ON "CurrencyTx" FOR EACH ROW EXECUTE FUNCTION pawbound_append_only();
-- Ràng buộc dữ liệu
ALTER TABLE "PaymentOrder" ADD CONSTRAINT "PaymentOrder_amount_check" CHECK ("amount" > 0);
ALTER TABLE "Wallet" ADD CONSTRAINT "Wallet_balance_check" CHECK ("balance" >= 0);
