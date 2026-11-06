# PAWBOUND — Database Design (v0.1, Phase 0)

PostgreSQL là nguồn sự thật, Prisma lo migration (ADR-005). Chỉ Backend truy cập DB.
Quy ước: khóa chính `uuid` (v7 nếu Prisma/PG hỗ trợ, nếu không thì v4); `created_at`/`updated_at` là `timestamptz`; tiền lưu bằng `bigint` theo **đồng** (VND không có số lẻ).

## 1. Tài khoản và nhân vật
| Bảng | Cột chính | Ràng buộc |
|---|---|---|
| `users` | id, email (citext), password_hash (argon2id), status (active/banned/deleted), email_verified_at | unique(email) |
| `refresh_tokens` | id, user_id, token_hash, family_id, expires_at, revoked_at, replaced_by | index(user_id); phát hiện dùng lại token trong cùng family thì thu hồi cả family |
| `player_profiles` | user_id (PK/FK), display_name, locale, settings jsonb | unique(lower(display_name)) |
| `dog_characters` | id, user_id, name, breed_id, level, xp, skills jsonb, position (chunk, x_cm, z_cm), state_version | tối đa 3 nhân vật/user (kiểm tra ở service) |
| `character_appearance` | character_id, coat_color, pattern, size_variant, cosmetics jsonb | chỉ cosmetic, không ảnh hưởng chỉ số |
| `character_vitals` | character_id, hp, hunger, thirst, stamina, rest, updated_tick | ghi bởi game server qua internal API |

## 2. Vật phẩm
| Bảng | Cột chính |
|---|---|
| `item_definitions` | id (string key), category, max_stack, data jsonb (seed từ file config) |
| `inventory_items` | id, character_id, slot, item_def_id, quantity, instance_data jsonb · unique(character_id, slot) · check(quantity > 0) |
| `inventory_ops` | id, character_id, idempotency_key, op jsonb, applied_at · unique(character_id, idempotency_key) để chống ghi lặp và nhân bản item |

## 3. Phiên chơi và server
| Bảng | Cột chính |
|---|---|
| `server_instances` | id, region, address, port, capacity, current_players, status, last_heartbeat_at, build_version |
| `game_sessions` | id, user_id, character_id, server_id, started_at, ended_at, end_reason |
| `session_tickets` | jti, user_id, character_id, server_id, expires_at, consumed_at (ticket một lần; bản sao nonce còn có ở server) |
| `save_metadata` | character_id, version, saved_at, server_id, checksum · dùng optimistic concurrency theo `state_version` |

## 4. Xã hội
| Bảng | Cột chính |
|---|---|
| `friendships` | user_a, user_b (a < b), status (pending/accepted/blocked), requested_by |
| `blocks` | blocker_id, blocked_id |
| `parties` (Redis + bảng nhẹ nếu cần) | id, leader_id, max_size |
| `guilds` | id, name, name_normalized (unique), tag, description, emblem, owner_id, max_members, created_from_entitlement_id (unique) |
| `guild_members` | guild_id, user_id (unique, một người chỉ ở một bang), role (owner/officer/member), joined_at |
| `guild_invites` | id, guild_id, invitee_id, inviter_id, expires_at, status · unique(guild_id, invitee_id) khi pending |
| `guild_join_requests` | tương tự invites |
| `chat_moderation` | id, user_id, action (mute/ban), until, reason, by_admin |

## 5. Thanh toán và kinh tế
| Bảng | Cột chính |
|---|---|
| `payment_orders` | id, user_id, purpose (GUILD_CREATION), amount_vnd = 100000, currency='VND', provider, provider_ref, idempotency_key (unique), status, expires_at |
| `payment_events` | id, provider, provider_event_id (**unique**), order_id, raw_payload jsonb, signature_valid, received_at — chỉ thêm, không sửa |
| `ledger_entries` | id, order_id, type (charge/refund), amount_vnd, created_at — chỉ thêm, sửa bằng bút toán đảo |
| `entitlements` | id, user_id, type (GUILD_CREATE), source_order_id (**unique**), consumed_at, consumed_by_guild_id |
| `wallets` / `wallet_transactions` | (post-MVP) tiền trong game, có ledger, không cho client đặt số dư |

## 6. Thế giới và sự kiện
| Bảng | Cột chính |
|---|---|
| `world_instances` | id, seed, generator_version, config_hash, logical_hash |
| `world_state_deltas` | world_id, stable_entity_key, state jsonb, version |
| `world_events` | id, world_id, type, started_at, ended_at, outcome jsonb |
| `territories` | id, world_id, zone_key, owner_guild_id, claimed_at, upkeep_due_at |
| `capture_records` | id, character_id, state, started_at, expires_at, rescued_by |

## 7. Kiểm toán
`audit_logs`: id, actor_type (user/admin/server/system), actor_id, action, target, metadata jsonb, ip_hash, created_at. Chỉ thêm. Không chứa mật khẩu, token hay dữ liệu thẻ.

## 8. Lưu trữ và sao lưu
- `pg_dump` hằng ngày cùng WAL archiving (Phase 15). Kiểm thử restore là tiêu chí nghiệm thu.
- Dữ liệu cá nhân tối thiểu: email, display name. IP chỉ lưu dạng hash có salt xoay vòng.
