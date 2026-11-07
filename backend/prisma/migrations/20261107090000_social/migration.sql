-- CreateEnum
CREATE TYPE "FriendStatus" AS ENUM ('PENDING', 'ACCEPTED');

-- CreateEnum
CREATE TYPE "InviteStatus" AS ENUM ('PENDING', 'ACCEPTED', 'REJECTED', 'CANCELLED');

-- CreateEnum
CREATE TYPE "GuildRole" AS ENUM ('OWNER', 'OFFICER', 'MEMBER');

-- CreateEnum
CREATE TYPE "GuildRequestKind" AS ENUM ('INVITE', 'JOIN');

-- AlterTable
ALTER TABLE "User" ADD COLUMN     "chatMutedUntil" TIMESTAMP(3);

-- CreateTable
CREATE TABLE "Friendship" (
    "id" UUID NOT NULL,
    "pairKey" TEXT NOT NULL,
    "requesterId" UUID NOT NULL,
    "addresseeId" UUID NOT NULL,
    "status" "FriendStatus" NOT NULL DEFAULT 'PENDING',
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "respondedAt" TIMESTAMP(3),
    "expiresAt" TIMESTAMP(3) NOT NULL,

    CONSTRAINT "Friendship_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "UserBlock" (
    "blockerId" UUID NOT NULL,
    "blockedId" UUID NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "UserBlock_pkey" PRIMARY KEY ("blockerId","blockedId")
);

-- CreateTable
CREATE TABLE "Party" (
    "id" UUID NOT NULL,
    "leaderId" UUID NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "Party_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "PartyMember" (
    "userId" UUID NOT NULL,
    "partyId" UUID NOT NULL,
    "joinedAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "PartyMember_pkey" PRIMARY KEY ("userId")
);

-- CreateTable
CREATE TABLE "PartyInvite" (
    "id" UUID NOT NULL,
    "partyId" UUID NOT NULL,
    "inviterId" UUID NOT NULL,
    "inviteeId" UUID NOT NULL,
    "status" "InviteStatus" NOT NULL DEFAULT 'PENDING',
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "expiresAt" TIMESTAMP(3) NOT NULL,
    "respondedAt" TIMESTAMP(3),

    CONSTRAINT "PartyInvite_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "Guild" (
    "id" UUID NOT NULL,
    "name" TEXT NOT NULL,
    "nameKey" TEXT,
    "description" TEXT NOT NULL DEFAULT '',
    "emblem" INTEGER NOT NULL DEFAULT 0,
    "ownerId" UUID NOT NULL,
    "memberLimit" INTEGER NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "disbandedAt" TIMESTAMP(3),

    CONSTRAINT "Guild_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "GuildMember" (
    "userId" UUID NOT NULL,
    "guildId" UUID NOT NULL,
    "role" "GuildRole" NOT NULL DEFAULT 'MEMBER',
    "joinedAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "GuildMember_pkey" PRIMARY KEY ("userId")
);

-- CreateTable
CREATE TABLE "GuildRequest" (
    "id" UUID NOT NULL,
    "guildId" UUID NOT NULL,
    "userId" UUID NOT NULL,
    "actorId" UUID NOT NULL,
    "kind" "GuildRequestKind" NOT NULL,
    "status" "InviteStatus" NOT NULL DEFAULT 'PENDING',
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "expiresAt" TIMESTAMP(3) NOT NULL,
    "respondedAt" TIMESTAMP(3),

    CONSTRAINT "GuildRequest_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "ChatMessage" (
    "id" BIGSERIAL NOT NULL,
    "channel" TEXT NOT NULL,
    "senderId" UUID NOT NULL,
    "text" TEXT NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "ChatMessage_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "Notification" (
    "id" UUID NOT NULL,
    "userId" UUID NOT NULL,
    "type" TEXT NOT NULL,
    "payload" JSONB NOT NULL,
    "dedupeKey" TEXT NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "expiresAt" TIMESTAMP(3) NOT NULL,
    "readAt" TIMESTAMP(3),

    CONSTRAINT "Notification_pkey" PRIMARY KEY ("id")
);

-- CreateIndex
CREATE UNIQUE INDEX "Friendship_pairKey_key" ON "Friendship"("pairKey");

-- CreateIndex
CREATE INDEX "Friendship_requesterId_idx" ON "Friendship"("requesterId");

-- CreateIndex
CREATE INDEX "Friendship_addresseeId_idx" ON "Friendship"("addresseeId");

-- CreateIndex
CREATE INDEX "UserBlock_blockedId_idx" ON "UserBlock"("blockedId");

-- CreateIndex
CREATE INDEX "PartyMember_partyId_idx" ON "PartyMember"("partyId");

-- CreateIndex
CREATE INDEX "PartyInvite_inviteeId_idx" ON "PartyInvite"("inviteeId");

-- CreateIndex
CREATE UNIQUE INDEX "Guild_nameKey_key" ON "Guild"("nameKey");

-- CreateIndex
CREATE INDEX "GuildMember_guildId_idx" ON "GuildMember"("guildId");

-- CreateIndex
CREATE INDEX "GuildRequest_userId_idx" ON "GuildRequest"("userId");

-- CreateIndex
CREATE INDEX "GuildRequest_guildId_status_idx" ON "GuildRequest"("guildId", "status");

-- CreateIndex
CREATE INDEX "ChatMessage_channel_id_idx" ON "ChatMessage"("channel", "id");

-- CreateIndex
CREATE INDEX "ChatMessage_senderId_createdAt_idx" ON "ChatMessage"("senderId", "createdAt");

-- CreateIndex
CREATE INDEX "Notification_userId_createdAt_idx" ON "Notification"("userId", "createdAt");

-- CreateIndex
CREATE UNIQUE INDEX "Notification_userId_dedupeKey_key" ON "Notification"("userId", "dedupeKey");

-- AddForeignKey
ALTER TABLE "Friendship" ADD CONSTRAINT "Friendship_requesterId_fkey" FOREIGN KEY ("requesterId") REFERENCES "User"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "Friendship" ADD CONSTRAINT "Friendship_addresseeId_fkey" FOREIGN KEY ("addresseeId") REFERENCES "User"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "UserBlock" ADD CONSTRAINT "UserBlock_blockerId_fkey" FOREIGN KEY ("blockerId") REFERENCES "User"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "UserBlock" ADD CONSTRAINT "UserBlock_blockedId_fkey" FOREIGN KEY ("blockedId") REFERENCES "User"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "PartyMember" ADD CONSTRAINT "PartyMember_userId_fkey" FOREIGN KEY ("userId") REFERENCES "User"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "PartyMember" ADD CONSTRAINT "PartyMember_partyId_fkey" FOREIGN KEY ("partyId") REFERENCES "Party"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "PartyInvite" ADD CONSTRAINT "PartyInvite_partyId_fkey" FOREIGN KEY ("partyId") REFERENCES "Party"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "GuildMember" ADD CONSTRAINT "GuildMember_userId_fkey" FOREIGN KEY ("userId") REFERENCES "User"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "GuildMember" ADD CONSTRAINT "GuildMember_guildId_fkey" FOREIGN KEY ("guildId") REFERENCES "Guild"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "GuildRequest" ADD CONSTRAINT "GuildRequest_guildId_fkey" FOREIGN KEY ("guildId") REFERENCES "Guild"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "ChatMessage" ADD CONSTRAINT "ChatMessage_senderId_fkey" FOREIGN KEY ("senderId") REFERENCES "User"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "Notification" ADD CONSTRAINT "Notification_userId_fkey" FOREIGN KEY ("userId") REFERENCES "User"("id") ON DELETE CASCADE ON UPDATE CASCADE;


-- Viết tay: index UNIQUE một phần — mỗi (party, người được mời) / (bang, người, loại) chỉ có MỘT yêu cầu PENDING.
-- Hai request mời đồng thời: DB chặn cái thứ hai (Prisma schema chưa diễn tả được partial index).
CREATE UNIQUE INDEX "PartyInvite_pending_unique" ON "PartyInvite"("partyId", "inviteeId") WHERE "status" = 'PENDING';
CREATE UNIQUE INDEX "GuildRequest_pending_unique" ON "GuildRequest"("guildId", "userId", "kind") WHERE "status" = 'PENDING';
-- Ràng buộc dữ liệu ở mức DB (phòng lỗi code)
ALTER TABLE "Guild" ADD CONSTRAINT "Guild_memberLimit_check" CHECK ("memberLimit" BETWEEN 1 AND 500);
ALTER TABLE "ChatMessage" ADD CONSTRAINT "ChatMessage_text_check" CHECK (char_length("text") BETWEEN 1 AND 500);
