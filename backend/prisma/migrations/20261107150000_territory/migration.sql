-- CreateTable
CREATE TABLE "Territory" (
    "zoneId" INTEGER NOT NULL,
    "guildId" UUID,
    "claimedAt" TIMESTAMP(3),
    "expiresAt" TIMESTAMP(3),
    "protectedTill" TIMESTAMP(3),
    "serverId" TEXT,
    "updatedAt" TIMESTAMP(3) NOT NULL,

    CONSTRAINT "Territory_pkey" PRIMARY KEY ("zoneId")
);

-- CreateIndex
CREATE INDEX "Territory_guildId_idx" ON "Territory"("guildId");

