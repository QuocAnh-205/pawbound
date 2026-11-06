#!/usr/bin/env bash
# Build + test Dedicated Server trên Linux (không cần SDL video/Vulkan).
# Dùng: ./scripts/build-server-linux.sh [linux-server-release|linux-server-debug]
set -euo pipefail
PRESET="${1:-linux-server-release}"
cd "$(dirname "$0")/.."
cmake --preset "$PRESET"
cmake --build --preset "$PRESET" -j"$(nproc)"
ctest --preset "$PRESET"
