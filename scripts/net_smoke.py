"""Smoke test online: dedicated server + 3 bot + client thật (renderer, validation) kết nối, chụp ảnh.

Dùng: python scripts/net_smoke.py <thư mục bin> <thư mục world> [ảnh.png]
Exit 0 nếu client vào được game, chạy hết số frame và thoát sạch (validation không lỗi).
"""
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def exe(bindir, name):
    return os.path.join(bindir, name + (".exe" if os.name == "nt" else ""))


sys.stdout.reconfigure(encoding="utf-8", errors="replace")  # console Windows mặc định cp1252


def main():
    bindir, world = sys.argv[1], sys.argv[2]
    shot = sys.argv[3] if len(sys.argv) > 3 else os.path.join(bindir, "..", "screenshots", "online.png")
    port = "27390"
    keys = os.path.join(ROOT, "config", "dev_keys")
    if not os.path.exists(os.path.join(keys, "ticket_pk.b64")):
        subprocess.run([exe(bindir, "pawbound"), "net", "keygen", "--out", keys], check=True)
    srv = subprocess.Popen([exe(bindir, "pawbound_server"), "--world", world, "--data", ROOT, "--port", port,
                            "--ticket-pubkey", os.path.join(keys, "ticket_pk.b64"), "--no-console", "--ticks", "900"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(1.5)
        bots = subprocess.Popen([exe(bindir, "pawbound_bot"), "--count", "3", "--duration", "20", "--port", port,
                                 "--world", world, "--sk", os.path.join(keys, "ticket_sk.b64"), "--char-base", "50000"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        ticket = subprocess.run([exe(bindir, "pawbound"), "net", "ticket", "--sk", os.path.join(keys, "ticket_sk.b64"),
                                 "--user", "1", "--char", "1", "--ttl", "120"], capture_output=True, text=True,
                                check=True).stdout.strip()
        time.sleep(2.0)
        r = subprocess.run([exe(bindir, "pawbound_client"), "--world", world, "--connect", "127.0.0.1:" + port,
                            "--ticket", ticket, "--frames", "240", "--validation", "--zoom", "9", "--asset-root", ROOT,
                            "--say", "Xin chào cả đàn!",
                            "--config", os.path.join(ROOT, "config", "client.json"), "--screenshot", shot],
                           capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=90)
        out = (r.stdout or "") + (r.stderr or "")
        for line in out.splitlines():
            if "[net]" in line or "validation" in line or "ERROR" in line or "FATAL" in line:
                print(line)
        bots.wait(timeout=60)
        print("client exit", r.returncode)
        return 0 if r.returncode == 0 and "đã vào server" in out else 1
    finally:
        srv.terminate()
        srv.wait(timeout=10)


if __name__ == "__main__":
    sys.exit(main())
