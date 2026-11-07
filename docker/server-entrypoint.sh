#!/usr/bin/env bash
# World sinh lại từ seed (tất định, cùng hash với client) rồi exec server để SIGTERM tới thẳng tiến trình -> lưu hết rồi thoát.
set -euo pipefail
SEED="${PAWBOUND_WORLD_SEED:-20261106}"
WORLD=/tmp/world
/app/pawbound world generate --seed "$SEED" --config /app/data/world/demo.json --output "$WORLD"
ARGS=(--world "$WORLD" --data /app --ticket-pubkey "${PAWBOUND_TICKET_PUBKEY:-/keys/ticket_pk.b64}" --no-console
      --port "${PAWBOUND_PORT:-27015}" --server-id "${PAWBOUND_SERVER_ID:-srv-1}" --max-players "${PAWBOUND_MAX_PLAYERS:-32}"
      --health-port 8081 --health-bind 0.0.0.0)
if [[ -n "${PAWBOUND_BACKEND_URL:-}" ]]; then
  ARGS+=(--backend "$PAWBOUND_BACKEND_URL" --region "${PAWBOUND_REGION:-vn}" --public-address "${PAWBOUND_PUBLIC_ADDRESS:-127.0.0.1}")
fi
exec /app/pawbound_server "${ARGS[@]}" "$@"
