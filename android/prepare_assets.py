"""Chép assets/, data/, config (trừ khóa bí mật) vào build/android-assets + danh mục pawbound_files.txt cho APK."""
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "android-assets")
shutil.rmtree(OUT, ignore_errors=True)
files = []
for rel in ("assets", "data"):
    for d, _, fs in os.walk(os.path.join(ROOT, rel)):
        for f in fs:
            full = os.path.join(d, f)
            files.append(os.path.relpath(full, ROOT).replace("\\", "/"))
files += ["config/client.json", "config/input.json"]
for r in files:
    if any(x in r.lower() for x in ("dev_keys", ".env", "ticket_sk")):
        sys.exit("từ chối chép file nhạy cảm: " + r)
    dst = os.path.join(OUT, r)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(os.path.join(ROOT, r), dst)
with open(os.path.join(OUT, "pawbound_files.txt"), "w", newline="\n") as f:
    f.write("\n".join(files) + "\n")
print(f"{len(files)} file -> {OUT}")
