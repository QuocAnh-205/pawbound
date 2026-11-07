# Kết quả load test (tự sinh bởi scripts/loadtest.py)

- Thời điểm: 2026-11-07 14:25
- Máy: Windows-11-10.0.26200-SP0, Intel64 Family 6 Model 165 Stepping 2, GenuineIntel, 12 luồng CPU
- Build: win-release; mỗi nấc 45 s; server + bot cùng một máy (localhost)
- Ngưỡng: docs/architecture/multiplayer-design.md §7 (máy tham chiếu trong thiết kế là Linux 4 vCPU — máy này KHÁC)

| Người | Mất gói / trễ | Tick p50 | p95 | p99 | max (ms) | RAM (MB) | Ra (KiB/s) | Ra (Mbit/s) | Vào (KiB/s) | NPC | RTT p50/p95 (ms) | Snapshot/bot/s | Vào game | Rớt | Kết quả |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2 | 0% / 0 ms | 0.08 | 0.16 | 0.62 | 5.03 | 7 | 5.7 | 0.05 | 1.6 | 55 | 15.5/16.3 | 19.7 | 2/2 | 0 | PASS |
| 8 | 0% / 0 ms | 0.14 | 0.26 | 0.35 | 0.48 | 7 | 33.1 | 0.27 | 6.5 | 61 | 15.4/16.8 | 19.9 | 8/8 | 0 | PASS |
| 16 | 0% / 0 ms | 0.25 | 0.55 | 0.72 | 0.96 | 7 | 101.2 | 0.83 | 12.9 | 77 | 15.5/17.1 | 19.7 | 16/16 | 0 | PASS |
| 20 | 0% / 0 ms | 0.37 | 1.25 | 2.53 | 3.12 | 7 | 141.4 | 1.16 | 16.1 | 77 | 15.5/16.6 | 19.6 | 20/20 | 0 | PASS |
| 30 | 0% / 0 ms | 0.83 | 2.54 | 7.59 | 8.22 | 8 | 300.8 | 2.46 | 24.2 | 69 | 15.5/17.6 | 19.2 | 30/30 | 0 | PASS |
