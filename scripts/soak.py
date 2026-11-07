"""Soak test dedicated server: chạy lâu, người chơi ra/vào liên tục; theo dõi rò rỉ bộ nhớ, tick, crash.

Dùng: python scripts/soak.py [--preset win-release] [--minutes 20] [--out docs/operations/soak-results.md]
"""
import argparse
import json
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def exe(bindir, n):
    return os.path.join(bindir, n + (".exe" if os.name == "nt" else ""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", default="win-release")
    ap.add_argument("--minutes", type=float, default=20)
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "operations", "soak-results.md"))
    a = ap.parse_args()
    bindir = os.path.join(ROOT, "build", a.preset, "bin")
    world = os.path.join(ROOT, "build", a.preset, "out", "world_demo")
    keys = os.path.join(ROOT, "build", a.preset, "soak_keys")
    subprocess.run([exe(bindir, "pawbound"), "net", "keygen", "--out", keys, "--force", "yes"], check=True, capture_output=True)
    metrics = os.path.join(ROOT, "build", a.preset, "soak_metrics.jsonl")
    if os.path.exists(metrics):
        os.remove(metrics)
    port = "27420"
    total_s = a.minutes * 60
    srv = subprocess.Popen([exe(bindir, "pawbound_server"), "--world", world, "--data", ROOT, "--port", port, "--ticket-pubkey",
                            os.path.join(keys, "ticket_pk.b64"), "--no-console", "--metrics", metrics, "--metrics-interval", "30",
                            "--ticks", str(int(total_s * 30 + 300))], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2)
    bot = lambda count, base, dur: subprocess.Popen(
        [exe(bindir, "pawbound_bot"), "--count", str(count), "--duration", str(dur), "--port", port, "--world", world,
         "--sk", os.path.join(keys, "ticket_sk.b64"), "--char-base", str(base), "--ramp", "0.2"],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    steady = bot(10, 100000, total_s - 30)
    batches, waves = [], 0
    t0 = time.time()
    while time.time() - t0 < total_s - 150:
        # Mỗi đợt: 10 người MỚI vào 110 s rồi rời (mã nhân vật khác nhau mỗi đợt -> server phải dọn người cũ)
        batches.append(bot(10, 200000 + waves * 100, 110))
        waves += 1
        time.sleep(120)
    results = []
    for p in batches + [steady]:
        out, _ = p.communicate(timeout=total_s)
        line = next((l for l in out.splitlines() if l.startswith("{")), "{}")
        results.append(json.loads(line))
    srv.wait(timeout=120)
    rows = [json.loads(l) for l in open(metrics, encoding="utf-8") if l.strip()]
    warm = [r for r in rows if r["t"] >= 120] or rows
    rss0, rss1 = warm[0]["rss_mb"], rows[-1]["rss_mb"]
    rss_max = max(r["rss_mb"] for r in rows)
    p99_max = max(r["tick_p99"] for r in rows if not r.get("final"))
    welcomed = sum(r.get("welcomed", 0) for r in results)
    expected = sum(r.get("bots", 0) for r in results)
    dropped = sum(r.get("disconnected", 0) for r in results)
    ok = {
        "server thoát sạch (không crash)": srv.returncode == 0,
        f"mọi bot vào được ({welcomed}/{expected})": welcomed == expected and expected > 0,
        f"không bot nào bị rớt bất thường ({dropped})": dropped == 0,
        f"RAM không tăng đáng kể sau khởi động ({rss0:.1f} -> {rss1:.1f} MB, đỉnh {rss_max:.1f})": rss1 <= rss0 * 1.2 + 2,
        f"tick p99 tối đa mỗi cửa sổ 30 s < 50 ms ({p99_max:.2f} ms)": p99_max < 50,
    }
    lines = [f"# Soak test — {a.minutes:.0f} phút ({a.preset})", "",
             f"Máy dev Windows (laptop), server + bot cùng máy. 10 người chơi liên tục + {waves} đợt × 10 người mới vào/ra (mỗi đợt 110 s).", "",
             "| Kiểm tra | Kết quả |", "|---|---|"] + [f"| {k} | {'PASS' if v else 'FAIL'} |" for k, v in ok.items()]
    lines += ["", "| t (s) | người | NPC | tick p50 | p95 | p99 | RAM MB | out KB/s |", "|---|---|---|---|---|---|---|---|"]
    for r in rows:
        lines.append(f"| {r['t']:.0f} | {r['players']} | {r['npcs']} | {r['tick_p50']:.2f} | {r['tick_p95']:.2f} | {r['tick_p99']:.2f} | {r['rss_mb']:.1f} | {r['kb_out_s']:.1f} |")
    open(a.out, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    for k, v in ok.items():
        print(("PASS  " if v else "FAIL  ") + k)
    return 0 if all(ok.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
