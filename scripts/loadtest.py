"""Load test PAWBOUND: dedicated server + bot ở các nấc 2/8/16/20/30 (+ mạng xấu).

Dùng: python scripts/loadtest.py [--preset win-release] [--duration 45] [--levels 2,8,16,20,30] [--out docs/networking/loadtest-results.md]
Yêu cầu: đã build preset, đã có out/world_demo trong thư mục build và config/dev_keys (pawbound net keygen).
Ngưỡng PASS lấy từ docs/architecture/multiplayer-design.md §7.
"""
import argparse
import json
import os
import platform
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# (tick p95 ms, tick p99 ms, RAM MB, băng thông ra Mbit/s)
THRESHOLDS = {2: (10, 20, 1024, 1), 8: (10, 20, 1024, 1), 16: (10, 20, 1024, 1), 20: (15, 25, 1536, 2), 30: (20, 30, 2048, 3)}


def run_level(binary_dir, world, n, duration, port, loss=0.0, latency=0.0, jitter=0.0):
    metrics = os.path.join(binary_dir, f"loadtest_metrics_{n}_{int(loss*100)}.jsonl")
    if os.path.exists(metrics):
        os.remove(metrics)
    srv_args = [os.path.join(binary_dir, "pawbound_server.exe" if os.name == "nt" else "pawbound_server"),
                "--world", world, "--data", ROOT, "--port", str(port), "--ticket-pubkey",
                os.path.join(ROOT, "config", "dev_keys", "ticket_pk.b64"), "--metrics", metrics,
                "--metrics-interval", "5", "--ticks", str(int((duration + 6) * 30)), "--no-console"]
    if loss or latency:
        srv_args += ["--sim-loss", str(loss), "--sim-latency", str(latency), "--sim-jitter", str(jitter)]
    srv = subprocess.Popen(srv_args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    bot_out = os.path.join(binary_dir, f"loadtest_bots_{n}_{int(loss*100)}.json")
    bot_args = [os.path.join(binary_dir, "pawbound_bot.exe" if os.name == "nt" else "pawbound_bot"),
                "--count", str(n), "--duration", str(duration), "--port", str(port), "--world", world,
                "--sk", os.path.join(ROOT, "config", "dev_keys", "ticket_sk.b64"), "--ramp", "0.1", "--out", bot_out]
    if loss or latency:
        bot_args += ["--sim-loss", str(loss), "--sim-latency", str(latency), "--sim-jitter", str(jitter)]
    bot_rc = subprocess.run(bot_args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
    srv.wait(timeout=60)
    rows = [json.loads(l) for l in open(metrics, encoding="utf-8") if l.strip()]
    # Bỏ cửa sổ đầu (đang ramp-up) và cửa sổ cuối (bot đã thoát)
    steady = [r for r in rows if r["players"] >= n and not r["final"]] or rows
    bots = json.load(open(bot_out, encoding="utf-8"))
    agg = {
        "players": n, "loss": loss, "latency": latency, "bot_rc": bot_rc,
        "tick_p50": max(r["tick_p50"] for r in steady), "tick_p95": max(r["tick_p95"] for r in steady),
        "tick_p99": max(r["tick_p99"] for r in steady), "tick_max": max(r["tick_max"] for r in steady),
        "rss_mb": max(r["rss_mb"] for r in rows), "kb_out_s": max(r["kb_out_s"] for r in steady),
        "kb_in_s": max(r["kb_in_s"] for r in steady), "pkts_out_s": max(r["pkts_out_s"] for r in steady),
        "npcs": max(r["npcs"] for r in steady), "kicked": rows[-1]["kicked"], "malformed": rows[-1]["malformed"],
        "bots": bots,
    }
    th = THRESHOLDS.get(n, THRESHOLDS[30])
    mbit = agg["kb_out_s"] * 1024 * 8 / 1e6  # KiB/s -> Mbit/s (sửa: lần chạy đầu dùng *8/1000, sai đơn vị)
    agg["mbit_out"] = mbit
    agg["pass"] = (agg["tick_p95"] < th[0] and agg["tick_p99"] < th[1] and agg["rss_mb"] < th[2] and mbit < th[3]
                   and bots["welcomed"] == n and bots["disconnected"] == 0 and agg["kicked"] == 0)
    return agg


sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # console Windows mặc định cp1252


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", default="win-release")
    ap.add_argument("--duration", type=float, default=45)
    ap.add_argument("--levels", default="2,8,16,20,30")
    ap.add_argument("--impaired", default="20,30")
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "networking", "loadtest-results.md"))
    a = ap.parse_args()
    bdir = os.path.join(ROOT, "build", a.preset, "bin")
    world = os.path.join(ROOT, "build", a.preset, "out", "world_demo")
    results = []
    port = 27200
    for n in [int(x) for x in a.levels.split(",")]:
        port += 1
        r = run_level(bdir, world, n, a.duration, port)
        results.append(r)
        print(json.dumps({k: v for k, v in r.items() if k != "bots"}), flush=True)
    for n in [int(x) for x in a.impaired.split(",") if x]:
        for loss in (0.02, 0.05):
            port += 1
            r = run_level(bdir, world, n, a.duration, port, loss, 50, 10)
            results.append(r)
            print(json.dumps({k: v for k, v in r.items() if k != "bots"}), flush=True)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as f:
        f.write("# Kết quả load test (tự sinh bởi scripts/loadtest.py)\n\n")
        f.write(f"- Thời điểm: {time.strftime('%Y-%m-%d %H:%M')}\n- Máy: {platform.platform()}, {platform.processor()}, "
                f"{os.cpu_count()} luồng CPU\n- Build: {a.preset}; mỗi nấc {a.duration:.0f} s; server + bot cùng một máy (localhost)\n"
                "- Ngưỡng: docs/architecture/multiplayer-design.md §7 (máy tham chiếu trong thiết kế là Linux 4 vCPU — máy này KHÁC)\n\n")
        f.write("| Người | Mất gói / trễ | Tick p50 | p95 | p99 | max (ms) | RAM (MB) | Ra (KiB/s) | Ra (Mbit/s) | Vào (KiB/s) | NPC | RTT p50/p95 (ms) | Snapshot/bot/s | Vào game | Rớt | Kết quả |\n")
        f.write("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n")
        for r in results:
            b = r["bots"]
            f.write(f"| {r['players']} | {r['loss']*100:.0f}% / {r['latency']:.0f} ms | {r['tick_p50']:.2f} | {r['tick_p95']:.2f} | "
                    f"{r['tick_p99']:.2f} | {r['tick_max']:.2f} | {r['rss_mb']:.0f} | {r['kb_out_s']:.1f} | {r['mbit_out']:.2f} | {r['kb_in_s']:.1f} | "
                    f"{r['npcs']} | {b['rtt_p50_ms']:.1f}/{b['rtt_p95_ms']:.1f} | {b['snapshots_per_bot_s']:.1f} | "
                    f"{b['welcomed']}/{b['bots']} | {b['disconnected']} | {'PASS' if r['pass'] else 'FAIL'} |\n")
    print("written", a.out)
    sys.exit(0 if all(r["pass"] for r in results) else 1)


if __name__ == "__main__":
    main()
