#!/usr/bin/env bash
# Smoke test triển khai bằng docker compose (chạy trên máy có Docker, ví dụ distro WSL build trên ổ D).
# Tạo bí mật NGẪU NHIÊN tạm (docker/secrets, đã .gitignore), dựng cả stack, kiểm tra end-to-end, rồi xóa sạch.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$(pwd)
BOT=${BOT:-$ROOT/build/linux-server-release/bin/pawbound_bot}
TOOL=${TOOL:-$ROOT/build/linux-server-release/bin/pawbound}
fail=0
check() { if eval "$2"; then echo "PASS  $1"; else echo "FAIL  $1"; fail=$((fail+1)); fi; }
rnd() { head -c 48 /dev/urandom | base64 | tr -d '/+=\n' | head -c 48; }

mkdir -p docker/secrets
rnd > docker/secrets/db_password.txt
"$TOOL" net keygen --out /tmp/pb-keys --force yes >/dev/null
cp /tmp/pb-keys/ticket_sk.b64 docker/secrets/ticket_sk.b64
cp /tmp/pb-keys/ticket_pk.b64 docker/secrets/ticket_pk.b64
chmod 644 docker/secrets/*   # container chạy user không phải root cần đọc được (production: dùng secret store)
cat > docker/stack.env <<EOF
DATABASE_URL=postgresql://pawbound:$(cat docker/secrets/db_password.txt)@db:5432/pawbound?schema=public
JWT_SECRET=$(rnd)
INTERNAL_SERVICE_TOKEN=$(rnd)
MOCK_PAYMENT_WEBHOOK_SECRET=$(rnd)
PAWBOUND_PUBLIC_ADDRESS=127.0.0.1
APP_ENV=development
EOF
DC="docker compose -f docker/compose.yml --env-file docker/stack.env"
cleanup() {
  if [[ ${fail:-0} != 0 || ${UP_FAILED:-0} != 0 ]]; then for s in migrate backend gameserver; do echo "--- log $s"; $DC logs --tail 25 $s 2>&1; done; fi
  $DC down -v --remove-orphans >/dev/null 2>&1 || true; rm -rf docker/secrets docker/stack.env /tmp/pb-keys
}
trap cleanup EXIT

UP_FAILED=0
$DC up -d 2>&1 | tail -3 || UP_FAILED=1
for i in $(seq 1 60); do
  st=$($DC ps --format '{{.Service}}={{.Health}}' 2>/dev/null | tr '\n' ' ')
  [[ "$st" == *"gameserver=healthy"* && "$st" == *"backend=healthy"* ]] && break
  sleep 3
done
echo "trạng thái: $st"
check "db, backend, gameserver đều healthy" '[[ "$st" == *"db=healthy"* && "$st" == *"backend=healthy"* && "$st" == *"gameserver=healthy"* ]]'
check "migrate chạy xong (exit 0)" '[[ "$($DC ps -a --format "{{.Service}} {{.ExitCode}}" | grep ^migrate)" == "migrate 0" ]]'
check "container chạy user KHÔNG phải root" '[[ "$($DC exec -T gameserver id -u)" != "0" && "$($DC exec -T backend id -u)" != "0" ]]'
API=http://127.0.0.1:3000/api/v1
check "backend /health/ready" 'curl -sf $API/health/ready | grep -q ready'
check "game server /metrics" 'curl -sf http://127.0.0.1:8081/metrics | grep -q pawbound_players_online'
sleep 2
REG=$(curl -sf -X POST $API/auth/register -H 'content-type: application/json' -d '{"email":"smoke@test.vn","password":"mat-khau-dai-123","displayName":"Smoke"}')
TOK=$(echo "$REG" | python3 -c 'import sys,json;print(json.load(sys.stdin)["accessToken"])')
CH=$(curl -sf -X POST $API/characters -H "authorization: Bearer $TOK" -H 'content-type: application/json' -d '{"name":"Muc","breedId":"shiba"}')
CID=$(echo "$CH" | python3 -c 'import sys,json;print(json.load(sys.stdin)["id"])')
check "game server tự đăng ký (heartbeat) -> ONLINE" 'curl -sf $API/servers -H "authorization: Bearer $TOK" | grep -q "\"status\":\"ONLINE\""'
JOIN=$(curl -sf -X POST $API/matchmaking/join -H "authorization: Bearer $TOK" -H 'content-type: application/json' -d "{\"characterId\":\"$CID\"}")
TICKET=$(echo "$JOIN" | python3 -c 'import sys,json;print(json.load(sys.stdin)["ticket"])')
"$TOOL" world generate --seed 20261106 --config data/world/demo.json --output /tmp/pb-world >/dev/null
OUT=$("$BOT" --host 127.0.0.1 --port 27015 --count 1 --duration 6 --world /tmp/pb-world --server-id srv-1 --ticket "$TICKET" | tail -1)
echo "bot: $OUT"
check "người chơi vào game qua UDP bằng ticket Backend thật" '[[ "$OUT" == *"\"welcomed\":1"* ]]'
sleep 4
$DC stop -t 30 gameserver >/dev/null 2>&1
check "tắt game server nhẹ nhàng (SIGTERM -> lưu -> exit 0)" '[[ "$($DC ps -a --format "{{.Service}} {{.ExitCode}}" | grep ^gameserver)" == "gameserver 0" ]]'
check "tắt server -> trạng thái nhân vật được lưu vào Postgres (đúng thiết kế: lưu khi tắt/hết giữ chỗ/autosave)" '$DC exec -T db psql -U pawbound -d pawbound -tAc "SELECT \"stateVersion\" FROM \"DogCharacter\"" | grep -qE "^[1-9]"'
check "server báo DRAINING khi tắt" '$DC exec -T db psql -U pawbound -d pawbound -tAc "SELECT status FROM \"ServerInstance\"" | grep -q DRAINING'
check "log có xoay vòng (json-file max-size)" '[[ "$(docker inspect $($DC ps -aq gameserver) --format "{{.HostConfig.LogConfig.Config}}")" == *"max-size:10m"* ]]'
echo; [[ $fail == 0 ]] && echo "TẤT CẢ PASS" || echo "$fail kiểm tra THẤT BẠI"
exit $fail
