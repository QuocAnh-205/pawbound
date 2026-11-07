# Dedicated server PAWBOUND (Linux, headless). Multi-stage: build + test trong image, runtime tối giản, user không phải root.
# Build:  docker build -f docker/server.Dockerfile -t pawbound-server:0.1.0 .
# Chạy:   docker run --init --rm -p 27015:27015/udp -p 127.0.0.1:8081:8081 \
#           -v /duong/dan/keys:/keys:ro -e PAWBOUND_SERVICE_TOKEN=... pawbound-server:0.1.0
FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
      g++ cmake ninja-build libsodium-dev python3 ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt CMakePresets.json ./
COPY cmake cmake
COPY src src
COPY tests tests
COPY shaders shaders
COPY scripts scripts
COPY data data
COPY assets assets
COPY config config
COPY .deps-cache/archives .deps-cache/archives
# Build + chạy toàn bộ test Linux (unit, world tất định, chat, bắt chó qua mạng) — test lỗi thì image không được tạo
RUN cmake --preset linux-server-release && cmake --build --preset linux-server-release -j"$(nproc)" \
 && ctest --preset linux-server-release --output-on-failure

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends libsodium23 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 10001 --home-dir /app pawbound
WORKDIR /app
COPY --from=build /src/build/linux-server-release/bin/pawbound_server /src/build/linux-server-release/bin/pawbound /app/
COPY data /app/data
COPY docker/server-entrypoint.sh /app/entrypoint.sh
USER pawbound
EXPOSE 27015/udp 8081/tcp
# Health: HTTP nội bộ của server (vòng lặp game treo > 10 s -> 503). Không cần curl: dùng /dev/tcp của bash.
HEALTHCHECK --interval=15s --timeout=3s --start-period=20s --retries=3 \
  CMD bash -c 'exec 3<>/dev/tcp/127.0.0.1/8081 && printf "GET /health HTTP/1.0\r\n\r\n" >&3 && grep -q "200 OK" <&3'
STOPSIGNAL SIGTERM
ENTRYPOINT ["/app/entrypoint.sh"]
