import { Body, Controller, Delete, Get, HttpCode, Injectable, NotFoundException, Param, ParseUUIDPipe, Patch, Post,
  UnprocessableEntityException, UseGuards } from '@nestjs/common';
import { ApiBearerAuth, ApiProperty, ApiPropertyOptional, ApiTags } from '@nestjs/swagger';
import { Type } from 'class-transformer';
import { IsIn, IsInt, IsNumber, IsOptional, IsString, Length, Matches, Max, Min, ValidateNested } from 'class-validator';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { AuditService } from '../audit/audit.service';
import { AuthUser, CurrentUser, JwtAuthGuard } from '../common/security';
import { PrismaService } from '../prisma/prisma.service';

export const MAX_CHARACTERS = 3;

// Breed hợp lệ đọc từ cùng file data mà game dùng (data/dogs/breeds.json, cho phép comment //).
function loadBreeds(): string[] {
  const file = resolve(process.env.PAWBOUND_DATA_ROOT ?? resolve(__dirname, '../../..'), 'data/dogs/breeds.json');
  const text = readFileSync(file, 'utf8').replace(/^\s*\/\/.*$/gm, '').replace(/\s\/\/[^"\n]*$/gm, '');
  return (JSON.parse(text).breeds as { id: string }[]).map((b) => b.id);
}
const BREEDS = loadBreeds();

export class AppearanceDto {
  @ApiPropertyOptional({ minimum: 0, maximum: 4 })
  @IsOptional() @IsInt() @Min(0) @Max(4)
  coatColor?: number;
  @ApiPropertyOptional({ minimum: 0.9, maximum: 1.1, description: 'Chỉ cosmetic' })
  @IsOptional() @IsNumber() @Min(0.9) @Max(1.1)
  sizeVariant?: number;
}

export class CreateCharacterDto {
  @ApiProperty({ example: 'Mực' })
  @IsString() @Length(2, 20) @Matches(/^[\p{L}\p{N} _-]+$/u)
  name: string;
  @ApiProperty({ enum: BREEDS })
  @IsIn(BREEDS)
  breedId: string;
  @ApiPropertyOptional({ type: AppearanceDto })
  @IsOptional() @ValidateNested() @Type(() => AppearanceDto)
  appearance?: AppearanceDto;
}

export class RenameCharacterDto {
  @ApiProperty()
  @IsString() @Length(2, 20) @Matches(/^[\p{L}\p{N} _-]+$/u)
  name: string;
}

const view = (c: { id: string; numericId: bigint; name: string; breedId: string; level: number; xp: number; createdAt: Date;
  appearance: { coatColor: number; sizeVariant: number } | null }) => ({
  id: c.id, numericId: c.numericId.toString(), name: c.name, breedId: c.breedId, level: c.level, xp: c.xp,
  appearance: c.appearance ? { coatColor: c.appearance.coatColor, sizeVariant: c.appearance.sizeVariant } : null, createdAt: c.createdAt,
});

@Injectable()
export class CharactersService {
  constructor(private readonly db: PrismaService, private readonly audit: AuditService) {}

  // Luôn lọc theo userId: người khác không thấy/sửa được nhân vật (404 thay vì 403 để không lộ tồn tại).
  async owned(userId: string, id: string) {
    const c = await this.db.dogCharacter.findFirst({ where: { id, userId, deletedAt: null }, include: { appearance: true } });
    if (!c) throw new NotFoundException({ code: 'CHARACTER_NOT_FOUND', message: 'Không tìm thấy nhân vật' });
    return c;
  }

  async create(userId: string, dto: CreateCharacterDto) {
    return this.db.$transaction(async (tx) => {
      // Khóa hàng user để hai request song song không cùng vượt giới hạn 3 nhân vật
      await tx.$queryRaw`SELECT id FROM "User" WHERE id = ${userId}::uuid FOR UPDATE`;
      const n = await tx.dogCharacter.count({ where: { userId, deletedAt: null } });
      if (n >= MAX_CHARACTERS)
        throw new UnprocessableEntityException({ code: 'CHARACTER_LIMIT', message: `Tối đa ${MAX_CHARACTERS} nhân vật` });
      const c = await tx.dogCharacter.create({
        data: { userId, name: dto.name.trim(), breedId: dto.breedId,
                appearance: { create: { coatColor: dto.appearance?.coatColor ?? 0, sizeVariant: dto.appearance?.sizeVariant ?? 1 } } },
        include: { appearance: true },
      });
      await this.audit.record('user', userId, 'character.create', c.id, undefined, tx);
      return view(c);
    });
  }
  async list(userId: string) {
    const cs = await this.db.dogCharacter.findMany({ where: { userId, deletedAt: null }, include: { appearance: true }, orderBy: { createdAt: 'asc' } });
    return cs.map(view);
  }
}

@ApiTags('characters')
@ApiBearerAuth()
@UseGuards(JwtAuthGuard)
@Controller({ path: 'characters', version: '1' })
export class CharactersController {
  constructor(private readonly svc: CharactersService, private readonly db: PrismaService, private readonly audit: AuditService) {}

  @Get()
  list(@CurrentUser() u: AuthUser) {
    return this.svc.list(u.id);
  }
  @Post()
  create(@CurrentUser() u: AuthUser, @Body() dto: CreateCharacterDto) {
    return this.svc.create(u.id, dto);
  }
  @Get(':id')
  async get(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    return view(await this.svc.owned(u.id, id));
  }
  @Patch(':id')
  async rename(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string, @Body() dto: RenameCharacterDto) {
    await this.svc.owned(u.id, id);
    const c = await this.db.dogCharacter.update({ where: { id }, data: { name: dto.name.trim() }, include: { appearance: true } });
    return view(c);
  }
  @Delete(':id')
  @HttpCode(204)
  async remove(@CurrentUser() u: AuthUser, @Param('id', ParseUUIDPipe) id: string) {
    await this.svc.owned(u.id, id);
    await this.db.dogCharacter.update({ where: { id }, data: { deletedAt: new Date() } });  // xóa mềm
    await this.audit.record('user', u.id, 'character.delete', id);
  }
}
