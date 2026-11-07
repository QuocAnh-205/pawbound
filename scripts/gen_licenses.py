"""Sinh THIRD_PARTY_LICENSES.md: nguyên văn giấy phép của mọi thư viện ĐƯỢC ĐÓNG GÓI vào bản phát hành + ghi công asset.

Dùng: python scripts/gen_licenses.py   (cần đã build win-release để có mã nguồn dependency trong build/win-release/_deps)
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEPS = os.path.join(ROOT, "build", "win-release", "_deps")
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

# (tên, phiên bản, license, đường dẫn file license, dùng trong)
NATIVE = [
    ("SDL3", "3.4.20", "Zlib", "sdl3-src/LICENSE.txt", "client"),
    ("Vulkan Memory Allocator", "3.5.0", "MIT", "vma-src/LICENSE.txt", "client"),
    ("GLM", "1.0.3", "MIT (Happy Bunny / MIT)", "glm-src/copying.txt", "client, server"),
    ("nlohmann/json", "3.12.0", "MIT", "nlohmann_json-src/LICENSE.MIT", "client, server, tool"),
    ("stb (stb_image, stb_easy_font)", "d14d46f6b444", "MIT hoặc Public Domain", "stb-src/LICENSE", "client"),
    ("cgltf", "1.15", "MIT", "cgltf-src/LICENSE", "client, tool"),
    ("ENet", "1.3.18", "MIT", "enet-src/LICENSE", "client, server"),
    ("libsodium", "1.0.22", "ISC", os.path.join(ROOT, "third_party_licenses", "libsodium-LICENSE.txt"), "client, server, tool"),
]


def read(p):
    full = p if os.path.isabs(p) else os.path.join(DEPS, p)
    with open(full, encoding="utf-8", errors="replace") as f:
        return f.read().strip()


def main():
    out = ["# Giấy phép bên thứ ba (THIRD_PARTY_LICENSES)", "",
           "Tệp này được sinh bởi `scripts/gen_licenses.py`. Gồm nguyên văn giấy phép của thư viện được biên dịch vào bản phát hành",
           "và ghi công asset. Công cụ chỉ dùng khi phát triển/test (doctest, jest…) không được phân phối.", ""]
    out += ["## Tóm tắt", "", "| Thành phần | Phiên bản | Giấy phép | Dùng trong |", "|---|---|---|---|"]
    for n, v, lic, _, use in NATIVE:
        out.append(f"| {n} | {v} | {lic} | {use} |")
    out += ["| Backend Node.js (NestJS, Prisma, argon2…) | xem `backend/package-lock.json` | MIT / Apache-2.0 | máy chủ backend (không nằm trong gói client) |",
            "| Vulkan loader (`vulkan-1.dll`) | của driver GPU | Apache-2.0 | KHÔNG đóng gói — có sẵn với driver |", ""]
    out += ["## Ghi công asset", ""]
    m = json.load(open(os.path.join(ROOT, "assets", "manifest.json"), encoding="utf-8"))
    for a in m.get("assets", []):
        out.append(f"- **{a.get('id')}** — {a.get('license')} — {a.get('author')} — nguồn: {a.get('source')}")
    out += ["", "CC-BY-4.0: https://creativecommons.org/licenses/by/4.0/ · CC0-1.0: https://creativecommons.org/publicdomain/zero/1.0/", ""]
    for n, v, lic, path, _ in NATIVE:
        out += [f"## {n} {v} — {lic}", "", "```", read(path), "```", ""]
    with open(os.path.join(ROOT, "THIRD_PARTY_LICENSES.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))
    print(f"THIRD_PARTY_LICENSES.md: {len(NATIVE)} thư viện, {len(m.get('assets', []))} asset")


if __name__ == "__main__":
    main()
