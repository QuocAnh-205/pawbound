"""Test tích hợp chat gần (Phase 12): dedicated server thật + bot qua UDP/ENet.

Kiểm tra: phát trong bán kính (người ở xa không nhận), người gửi nhận lại tin của mình, rate limit (3 tin liền),
lọc từ cấm, server không ghi nội dung chat vào log.
Dùng: python scripts/chat_smoke.py <thư mục bin> <thư mục world>
"""
import os
import re
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def exe(bindir, name):
    return os.path.join(bindir, name + (".exe" if os.name == "nt" else ""))


failures = 0


def check(cond, msg):
    global failures
    print(("PASS  " if cond else "FAIL  ") + msg)
    if not cond:
        failures += 1


def main():
    bindir, world = sys.argv[1], sys.argv[2]
    port = "27396"
    keys = os.path.join(os.path.dirname(os.path.abspath(bindir)), "chat_keys")  # trong thư mục build, không commit
    subprocess.run([exe(bindir, "pawbound"), "net", "keygen", "--out", keys, "--force", "yes"], check=True, capture_output=True)
    sk = os.path.join(keys, "ticket_sk.b64")
    srv = subprocess.Popen([exe(bindir, "pawbound_server"), "--world", world, "--data", ROOT, "--port", port,
                            "--ticket-pubkey", os.path.join(keys, "ticket_pk.b64")],
                           stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                           encoding="utf-8", errors="replace")
    log = []
    threading.Thread(target=lambda: [log.append(l) for l in srv.stdout], daemon=True).start()

    def bot(char_base, *extra, duration="5"):
        return subprocess.Popen([exe(bindir, "pawbound_bot"), "--count", "1", "--duration", duration, "--port", port,
                                 "--world", world, "--sk", sk, "--char-base", str(char_base), "--still", *extra],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")

    def wait_log(pattern, timeout=8.0):
        end = time.time() + timeout
        while time.time() < end:
            for l in list(log):
                m = re.search(pattern, l)
                if m:
                    return m
            time.sleep(0.05)
        return None

    try:
        time.sleep(1.5)
        # 1) Bot "xa": vào game rồi bị dịch chuyển ra xa 200 m
        far = bot(71000, "--chat", "toi o xa", "--chat-delay", "4", duration="7")
        m = wait_log(r"người chơi #(\d+) \(user 71000")
        check(m is not None, "bot xa vào game")
        far_id = m.group(1) if m else "1"
        srv.stdin.write(f"tp {far_id} 450 450\n")
        srv.stdin.flush()
        check(wait_log(rf"tp #{far_id} ->") is not None, "lệnh console tp hoạt động")
        # 2) Hai bot gần spawn: một gửi 1 tin, một gửi 6 tin liên tiếp (rate limit) có từ cấm
        near1 = bot(72000, "--chat", "xin chào cả đàn", duration="5")
        near2 = bot(73000, "--chat", "đồ shit", "--chat-count", "6", duration="5")
        outs = {}
        for name, p in (("near1", near1), ("near2", near2), ("far", far)):
            out, _ = p.communicate(timeout=30)
            outs[name] = out
        chats = {k: [l.split(" ", 3)[3] for l in v.splitlines() if l.startswith("CHAT ")] for k, v in outs.items()}
        events = {k: [l for l in v.splitlines() if l.startswith("EVENT ")] for k, v in outs.items()}
        print("  nhận:", {k: len(v) for k, v in chats.items()})
        check(chats["near1"].count("xin chào cả đàn") == 1, "người gửi nhận lại tin của mình (UTF-8 nguyên vẹn)")
        check(chats["near2"].count("xin chào cả đàn") == 1, "người gần nhận tin")
        check(chats["far"].count("xin chào cả đàn") == 0, "người ở xa 200 m KHÔNG nhận tin")
        check(chats["near1"].count("đồ ****") == 3, f"rate limit: chỉ 3/6 tin được phát ({chats['near1'].count('đồ ****')})")
        check(not any("shit" in c for v in chats.values() for c in v), "từ cấm bị che (đồ ****)")
        check(sum("gui qua nhanh" in e for e in events["near2"]) == 3, "người spam nhận 3 cảnh báo gửi quá nhanh")
        check(chats["far"].count("toi o xa") == 1 and chats["near1"].count("toi o xa") == 0,
              "tin của người ở xa chỉ chính họ nhận")
        srv.stdin.write("chatstats\n")
        srv.stdin.flush()
        st = wait_log(r"chat gần đã phát: (\d+)")
        check(st is not None and st.group(1) == "5", f"server đếm 5 tin đã phát ({st.group(1) if st else '?'})")
        srv.stdin.write("quit\n")
        srv.stdin.flush()
        srv.wait(timeout=20)
        check(srv.returncode == 0, "server thoát sạch")
        text = "".join(log)
        check("xin chào cả đàn" not in text and "toi o xa" not in text, "log server không chứa nội dung chat")
    finally:
        if srv.poll() is None:
            srv.kill()
    print("TẤT CẢ PASS" if failures == 0 else f"{failures} kiểm tra THẤT BẠI")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
