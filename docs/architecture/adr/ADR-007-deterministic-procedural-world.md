# ADR-007 — Thế giới sinh tự động, deterministic theo seed

- **Trạng thái:** Accepted
- **Ngày:** 2026-11-06

## Quyết định
- PRNG: **PCG32** tự cài đặt (thuật toán công khai, vài chục dòng). Không dùng `std::mt19937` + `std::uniform_*_distribution` vì các distribution cho kết quả khác nhau giữa các implementation thư viện chuẩn (libstdc++ khác libc++/MSVC).
- Mỗi bước generator có sub-seed riêng: `hash(world_seed, generator_version, stage_name, chunk_x, chunk_z)`. Thêm hoặc sửa một bước không làm xáo trộn các bước khác.
- Noise: tự cài đặt OpenSimplex2 hoặc value/Perlin noise (public domain hoặc thuật toán công khai). Không phụ thuộc thư viện có license không rõ.
- Logic world (đường, lô đất, vị trí nhà/cây, POI) dùng **số nguyên/fixed-point** hoặc làm tròn về lưới 1 cm trước khi hash. Mục đích là logical hash giống nhau trên Windows/MinGW và Linux/GCC.
- Output: thư mục world gồm `world.json` (manifest + version + seed + config hash) và `chunks/x_z.bin` (dữ liệu nhị phân có version), kèm `logical_hash.txt`.
- Generator chạy offline bằng CLI. Server và client load cùng dữ liệu đã sinh, kiểm tra khớp bằng `world_hash` trong handshake.
