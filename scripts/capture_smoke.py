"""Test tích hợp Phase 14 qua mạng thật: kẻ trộm cảnh báo -> bắt -> đồng đội tới khu giữ giải cứu -> thưởng.

Dùng: python scripts/capture_smoke.py <thư mục bin> <thư mục world>
Cấu hình test: tests/data/advanced_fast.json (kẻ trộm xuất hiện ngay, giữ tối đa 30 s, cứu 2 s).
"""
import os
import re
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
failures = 0


def exe(bindir, name):
    return os.path.join(bindir, name + (".exe" if os.name == "nt" else ""))


def check(cond, msg):
    global failures
    print(("PASS  " if cond else "FAIL  ") + msg)
    failures += 0 if cond else 1


class Proc:
    def __init__(self, args):
        self.p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                  encoding="utf-8", errors="replace")
        self.lines = []
        threading.Thread(target=lambda: [self.lines.append(l.rstrip("\n")) for l in self.p.stdout], daemon=True).start()

    def wait_for(self, pattern, timeout=20.0):
        end = time.time() + timeout
        while time.time() < end:
            for l in list(self.lines):
                m = re.search(pattern, l)
                if m:
                    return m
            time.sleep(0.05)
        return None

    def cmd(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()


def main():
    bindir, world = sys.argv[1], sys.argv[2]
    port = "27398"
    keys = os.path.join(os.path.dirname(os.path.abspath(bindir)), "capture_keys")
    subprocess.run([exe(bindir, "pawbound"), "net", "keygen", "--out", keys, "--force", "yes"], check=True, capture_output=True)
    sk = os.path.join(keys, "ticket_sk.b64")
    srv = Proc([exe(bindir, "pawbound_server"), "--world", world, "--data", ROOT, "--port", port, "--ticket-pubkey",
                os.path.join(keys, "ticket_pk.b64"), "--advanced", os.path.join(ROOT, "tests", "data", "advanced_fast.json")])
    bot = lambda char, *extra, dur="40": Proc([exe(bindir, "pawbound_bot"), "--count", "1", "--duration", dur, "--port", port, "--world", world,
                                               "--sk", sk, "--char-base", str(char), "--still", "--print-events", *extra])
    try:
        hold = srv.wait_for(r"khu giữ \((-?[\d.]+), (-?[\d.]+)\)")
        check(hold is not None, "server khởi tạo hệ thống bắt chó (có khu giữ)")
        hx, hz = (float(hold.group(1)), float(hold.group(2))) if hold else (0, 0)
        victim = bot(81000)
        vm = srv.wait_for(r"người chơi #(\d+) \(user 81000", 10)
        check(vm is not None, "nạn nhân vào game (ở điểm spawn = khu an toàn: kẻ trộm KHÔNG được nhắm)")
        time.sleep(2.0)
        check(not any(l.startswith("EVENT 0 40 ") for l in victim.lines), "ở khu an toàn không bị cảnh báo/nhắm")
        srv.cmd(f"tp {vm.group(1) if vm else 1} {hx:.1f} {hz + 8:.1f}")  # ra khu không an toàn (gần khu công nghiệp)
        warn = victim.wait_for(r"^EVENT 0 40 ", 20)
        check(warn is not None, "nạn nhân nhận CẢNH BÁO kẻ trộm")
        threatened = victim.wait_for(r"^CAPTURE 0 1 (\d+)", 5)
        check(threatened is not None, "HUD nhận trạng thái 'đang bị đe dọa' kèm đồng hồ")
        cap = victim.wait_for(r"^CAPTURE 0 2 (\d+)", 30)
        check(cap is not None, "nạn nhân bị bắt sau khi đã được cảnh báo")
        lines = victim.lines
        i_warn = next((i for i, l in enumerate(lines) if l.startswith("EVENT 0 40 ")), 10**9)
        i_cap = next((i for i, l in enumerate(lines) if l.startswith("CAPTURE 0 2")), -1)
        check(i_warn < i_cap, "thứ tự: cảnh báo TRƯỚC khi bị bắt")
        check(cap is not None and 0 < int(cap.group(1)) <= 30, f"thời gian giữ có trần ({cap.group(1) if cap else '?'} s ≤ 30 s)")
        check(victim.wait_for(r"^EVENT 0 43 .*Không mất đồ", 3) is not None, "thông báo bị bắt nói rõ không mất đồ")
        srv.cmd("capture off")  # không để kẻ trộm nhắm người cứu trong test
        rescuer = bot(82000, "--interact-every", "0.5", dur="15")
        m = srv.wait_for(r"người chơi #(\d+) \(user 82000", 10)
        check(m is not None, "người cứu vào game")
        if m:
            srv.cmd(f"tp {m.group(1)} {hx + 1.5:.1f} {hz:.1f}")
        check(rescuer.wait_for(r"^EVENT 0 44 ", 10) is not None, "người cứu bắt đầu giải cứu (đứng gần chuồng + tương tác)")
        freed = victim.wait_for(r"^EVENT 0 46 ", 15)
        check(freed is not None, "nạn nhân được giải cứu")
        check(victim.wait_for(r"^CAPTURE 0 0 ", 3) is not None, "trạng thái nạn nhân về bình thường")
        check(rescuer.wait_for(r"^EVENT 0 47 ", 3) is not None, "người cứu nhận xác nhận")
        check(rescuer.wait_for(r"^EVENT 0 60 \+80 xương \(rescue\)", 3) is not None, "người cứu nhận thưởng (đúng một lần)")
        time.sleep(1.5)
        check(sum(1 for l in rescuer.lines if l.startswith("EVENT 0 60 +80")) == 1, "thưởng giải cứu không nhân đôi khi bấm tương tác liên tục")
        srv.cmd("quit")
        srv.p.wait(timeout=20)
        check(srv.p.returncode == 0, "server thoát sạch")
    finally:
        for p in (srv,):
            if p.p.poll() is None:
                p.p.kill()
    if failures:
        for name, pr in (("victim", locals().get("victim")), ("rescuer", locals().get("rescuer"))):
            if pr:
                print(f"--- {name} ---")
                print("\n".join(l for l in pr.lines if not l.startswith("{")))
        print("--- server ---")
        print("\n".join(l for l in srv.lines if "metrics" not in l)[-3000:])
    print("TẤT CẢ PASS" if failures == 0 else f"{failures} kiểm tra THẤT BẠI")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
