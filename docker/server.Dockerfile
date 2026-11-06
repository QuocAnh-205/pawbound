# Dedicated server PAWBOUND trên Linux (headless, không SDL video/Vulkan).
# Build:  docker build -f docker/server.Dockerfile -t pawbound-server:dev .
# Chạy:   docker run --rm -p 27015:27015/udp -v %CD%/config/dev_keys:/keys:ro pawbound-server:dev
# LƯU Ý (máy dev hiện tại): Docker Desktop lưu image trên ổ C (gần đầy) — chuyển "Disk image location"
# sang ổ D trước khi build. Dockerfile này CHƯA được build thử (xem phase-10-report).

FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
      g++ cmake ninja-build libsodium-dev ca-certificates git && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt CMakePresets.json ./
COPY cmake cmake
COPY src src
COPY tests tests
COPY shaders shaders
COPY .deps-cache/archives .deps-cache/archives
# GCC 12 của bookworm hỗ trợ C++20 nhưng <format> cần GCC 13+: dùng trixie/testing nếu build lỗi.
RUN cmake --preset linux-server-release && cmake --build --preset linux-server-release \
 && ctest --preset linux-server-release -E "client|renderer|gpu"

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends libsodium23 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 10001 pawbound
WORKDIR /app
COPY --from=build /src/build/linux-server-release/bin/pawbound_server /src/build/linux-server-release/bin/pawbound /app/
COPY data /app/data
USER pawbound
# World được sinh lại từ seed lúc khởi động (deterministic) — không cần đóng gói dữ liệu world.
EXPOSE 27015/udp
ENTRYPOINT ["/bin/sh", "-c", "/app/pawbound world generate --seed 20261106 --config /app/data/world/demo.json --output /tmp/world && exec /app/pawbound_server --world /tmp/world --data /app --ticket-pubkey /keys/ticket_pk.b64 --no-console"]
