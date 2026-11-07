"""Đóng gói bản phát hành thử nghiệm PAWBOUND + checksum + metadata.

Dùng: python scripts/package_release.py [--version 0.1.0-rc1]
Đầu vào: build/win-release/bin (Windows), build/linux-server-release/bin (Linux, build bằng WSL/Docker).
Đầu ra: dist/<ver>/  pawbound-<ver>-windows-x64.zip, pawbound-server-<ver>-linux-x64.tar.gz, SHA256SUMS.txt, release.json
KHÔNG đóng gói: khóa bí mật (config/dev_keys), .env, file build trung gian.
"""
import argparse
import datetime
import hashlib
import io
import json
import os
import subprocess
import sys
import tarfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
FORBIDDEN = ("dev_keys", ".env", "ticket_sk", ".pem", ".key")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def tree(rel):
    base = os.path.join(ROOT, rel)
    for d, _, files in os.walk(base):
        for f in files:
            full = os.path.join(d, f)
            yield full, os.path.relpath(full, ROOT).replace("\\", "/")


def check_safe(arc):
    low = arc.lower()
    if any(x in low for x in FORBIDDEN):
        raise SystemExit(f"TỪ CHỐI đóng gói file nhạy cảm: {arc}")


def git(*a):
    return subprocess.run(["git", *a], cwd=ROOT, capture_output=True, text=True).stdout.strip()


PLAY_README = """PAWBOUND {ver} — bản thử nghiệm cho Windows 10/11 x64 (đồ họa 3D)
=====================================================================
Yêu cầu: Windows 10/11 64-bit, card đồ họa hỗ trợ Vulkan 1.3 (cập nhật driver mới nhất).
Lần đầu chạy, Windows có thể cảnh báo SmartScreen: bấm "More info" -> "Run anyway".

1) CHƠI MỘT MÌNH: chạy  Choi-Offline.bat

2) CHƠI CHUNG QUA MẠNG (một người làm máy chủ):
   - Cả hai cài Tailscale (https://tailscale.com, miễn phí) và đăng nhập, để hai máy "thấy" nhau.
     (Hoặc chơi cùng mạng LAN/wifi.)
   - Người làm máy chủ chạy  Mo-May-Chu.bat : nó mở máy chủ, vào game cho bạn, và tạo VÉ cho người khác.
     Gửi cho bạn: địa chỉ IP (Tailscale hiện IP dạng 100.x.y.z) + chuỗi vé (đã tự copy, dán vào Zalo/Messenger).
     Windows Firewall hỏi thì bấm "Allow".
   - Người vào chơi chạy  Vao-Choi-Online.bat , nhập IP và dán vé.
   - Vé dùng 1 lần, hết hạn sau 15 phút. Thoát ra muốn vào lại thì xin vé mới.
   - Chế độ này chưa lưu tiến trình (dùng để test).

ĐIỀU KHIỂN
  Chuột: xoay góc nhìn (giữ Alt để hiện con trỏ)     V: đổi góc nhìn thứ nhất / thứ ba
  Cuộn chuột: kéo camera xa/gần (góc thứ ba)          Q / R: xoay camera 45 độ
  WASD: đi    Shift: chạy    Ctrl: đi chậm    Space: nhảy
  E: tương tác (ăn, uống, giải cứu)    G: nhặt đồ    1-6: dùng đồ    Z: nghỉ
  J: cắn    K: né    F (giữ): đánh hơi    Enter: chat (khi online)    F12: chụp màn hình    Esc: thoát

Báo lỗi: gửi kèm thư mục logs\\ (không chứa mật khẩu hay dữ liệu cá nhân).
Giấy phép bên thứ ba: THIRD_PARTY_LICENSES.md
"""

OFFLINE_BAT = """@echo off
rem PAWBOUND - choi mot minh (offline). Tao the gioi lan dau neu chua co.
cd /d "%~dp0"
if not exist world\\logical_hash.txt (
  echo Dang tao the gioi lan dau...
  bin\\pawbound.exe world generate --seed 20261106 --config data\\world\\demo.json --output world
)
start "" bin\\pawbound_client.exe --world world --asset-root . --config config\\client.json
"""

HOST_BAT = """@echo off
chcp 65001 >nul
rem PAWBOUND - mo may chu de choi chung (che do test, khong luu tien trinh)
cd /d "%~dp0"
if not exist world\\logical_hash.txt (
  echo Dang tao the gioi lan dau...
  bin\\pawbound.exe world generate --seed 20261106 --config data\\world\\demo.json --output world
)
if not exist keys\\ticket_sk.b64 bin\\pawbound.exe net keygen --out keys >nul
echo Dang mo may chu (cua so rieng, dong cua so do de tat may chu)...
start "PAWBOUND - MAY CHU (dong cua so nay de tat)" bin\\pawbound_server.exe --world world --data . --ticket-pubkey keys\\ticket_pk.b64
timeout /t 2 >nul
for /f "delims=" %%T in ('bin\\pawbound.exe net ticket --sk keys\\ticket_sk.b64 --user 1 --char 1 --ttl 900') do set MYTICKET=%%T
start "" bin\\pawbound_client.exe --world world --asset-root . --config config\\client.json --connect 127.0.0.1:27015 --ticket %MYTICKET%
echo.
echo Dia chi IP cua may nay (gui cho ban - uu tien IP Tailscale 100.x.y.z):
ipconfig | findstr /R /C:"IPv4"
set N=2
:loop
echo.
set /p OK=Nhan Enter de tao VE cho mot nguoi choi moi (go Q roi Enter de dung):
if /i "%OK%"=="Q" goto end
bin\\pawbound.exe net ticket --sk keys\\ticket_sk.b64 --user %N% --char %N% --ttl 900 > ve-nguoi-%N%.txt
type ve-nguoi-%N%.txt | clip
echo Da COPY ve cua nguoi choi so %N% vao clipboard - dan vao Zalo/Messenger gui cho ban (het han sau 15 phut).
set /a N=N+1
set OK=
goto loop
:end
"""

JOIN_BAT = """@echo off
chcp 65001 >nul
rem PAWBOUND - vao choi online voi ban
cd /d "%~dp0"
if not exist world\\logical_hash.txt (
  echo Dang tao the gioi lan dau...
  bin\\pawbound.exe world generate --seed 20261106 --config data\\world\\demo.json --output world
)
set /p IP=Nhap dia chi IP may chu ban gui (vd 100.101.2.3):
set /p TICKET=Dan VE ban gui vao day roi Enter:
start "" bin\\pawbound_client.exe --world world --asset-root . --config config\\client.json --connect %IP%:27015 --ticket %TICKET%
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="0.2.0-test1")
    a = ap.parse_args()
    ver = a.version
    out = os.path.join(ROOT, "dist", ver)
    os.makedirs(out, exist_ok=True)
    commit = git("rev-parse", "HEAD")
    dirty = bool(git("status", "--porcelain", "--untracked-files=no"))
    with open(os.path.join(ROOT, "build", "win-release", "out", "world_demo", "logical_hash.txt")) as f:
        world_hash = f.read().strip()
    meta = {"name": "PAWBOUND", "version": ver, "git_commit": commit, "git_dirty": dirty,
            "built_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "protocol_version": 2, "graphics": "3D (góc thứ ba + góc thứ nhất)", "world_seed": 20261106, "world_logical_hash": world_hash,
            "status": "release candidate (alpha) — xem docs/phases/phase-15-report.md"}
    artifacts = []

    # ---- Windows: client + server + công cụ
    win = os.path.join(out, f"pawbound-{ver}-windows-x64.zip")
    with zipfile.ZipFile(win, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        def add(full, arc):
            check_safe(arc)
            z.write(full, f"PAWBOUND-{ver}/{arc}")
        for exe in ("pawbound_client", "pawbound_server", "pawbound"):
            add(os.path.join(ROOT, "build", "win-release", "bin", exe + ".exe"), f"bin/{exe}.exe")
        for rel in ("assets", "data"):
            for full, arc in tree(rel):
                add(full, arc)
        for f in ("client.json", "input.json"):
            add(os.path.join(ROOT, "config", f), f"config/{f}")
        for f in ("THIRD_PARTY_LICENSES.md", "LICENSE", "CHANGELOG.md"):
            if os.path.exists(os.path.join(ROOT, f)):
                add(os.path.join(ROOT, f), f)
        z.writestr(f"PAWBOUND-{ver}/DOC-TRUOC-KHI-CHOI.txt", PLAY_README.format(ver=ver).replace("\n", "\r\n"))
        z.writestr(f"PAWBOUND-{ver}/Choi-Offline.bat", OFFLINE_BAT.replace("\n", "\r\n"))
        z.writestr(f"PAWBOUND-{ver}/Mo-May-Chu.bat", HOST_BAT.replace("\n", "\r\n"))
        z.writestr(f"PAWBOUND-{ver}/Vao-Choi-Online.bat", JOIN_BAT.replace("\n", "\r\n"))
        z.writestr(f"PAWBOUND-{ver}/release.json", json.dumps(meta, ensure_ascii=False, indent=2))
    artifacts.append(win)

    # ---- Linux dedicated server
    lbin = os.path.join(ROOT, "build", "linux-server-release", "bin")
    if os.path.exists(os.path.join(lbin, "pawbound_server")):
        lin = os.path.join(out, f"pawbound-server-{ver}-linux-x64.tar.gz")
        with tarfile.open(lin, "w:gz") as t:
            def tadd(full, arc, mode=None):
                check_safe(arc)
                ti = t.gettarinfo(full, f"pawbound-server-{ver}/{arc}")
                if mode:
                    ti.mode = mode
                with open(full, "rb") as fh:
                    t.addfile(ti, fh)
            for exe in ("pawbound_server", "pawbound"):
                tadd(os.path.join(lbin, exe), f"bin/{exe}", 0o755)
            for full, arc in tree("data"):
                tadd(full, arc)
            tadd(os.path.join(ROOT, "docker", "server-entrypoint.sh"), "entrypoint.sh", 0o755)
            for f in ("THIRD_PARTY_LICENSES.md", "CHANGELOG.md"):
                if os.path.exists(os.path.join(ROOT, f)):
                    tadd(os.path.join(ROOT, f), f)
            data = json.dumps(meta, ensure_ascii=False, indent=2).encode()
            ti = tarfile.TarInfo(f"pawbound-server-{ver}/release.json")
            ti.size = len(data)
            t.addfile(ti, io.BytesIO(data))
        artifacts.append(lin)
    else:
        print("CẢNH BÁO: chưa có build Linux (build/linux-server-release) — bỏ qua gói server Linux")

    # ---- Android APK (đã ký bằng khóa phát hành nằm NGOÀI repo)
    apk = os.path.join(ROOT, "android", "app", "build", "outputs", "apk", "release", "app-release.apk")
    if os.path.exists(apk):
        dst = os.path.join(out, f"pawbound-{ver}-android-arm64.apk")
        import shutil
        shutil.copyfile(apk, dst)
        artifacts.append(dst)
    else:
        print("CẢNH BÁO: chưa có APK release (android/) — bỏ qua")

    sums = os.path.join(out, "SHA256SUMS.txt")
    with open(sums, "w", newline="\n") as f:
        for p in artifacts:
            f.write(f"{sha256(p)}  {os.path.basename(p)}\n")
    meta["artifacts"] = [{"file": os.path.basename(p), "bytes": os.path.getsize(p), "sha256": sha256(p)} for p in artifacts]
    with open(os.path.join(out, "release.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(meta, f, ensure_ascii=False, indent=2)
    for p in artifacts:
        print(f"{os.path.getsize(p) / 1e6:7.2f} MB  {os.path.relpath(p, ROOT)}")
    print(open(sums).read())


if __name__ == "__main__":
    main()
