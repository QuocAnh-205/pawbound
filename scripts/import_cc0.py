"""Nhập asset CC0 (Quaternius) vào assets/cc0: glTF (+.bin +PNG) -> GLB tự chứa, texture thu về <= 512 px.

Dùng: python scripts/import_cc0.py <thư mục tải về> [--blender D:/DevTools/blender/blender.exe]
- Chỉ nhập danh sách đã chọn (SELECT) để repo và bộ nhớ điện thoại nhỏ gọn.
- File .blend (động vật) được chuyển sang GLB bằng Blender chạy nền, TẮT tự chạy script (--disable-autoexec).
- Ghi/ cập nhật assets/manifest.json (sha256, nguồn, giấy phép CC0).
"""
import argparse
import hashlib
import io
import json
import os
import struct
import subprocess
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "cc0")
MAX_TEX = 512
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

VILLAGE = "x_medieval_village_megakitstandard/glTF"
NATURE = "x_stylized_nature_megakitstandard/glTF"
SELECT = {
    "village": (VILLAGE, "https://quaternius.com/packs/medievalvillagemegakit.html", [
        "Wall_Plaster_Straight", "Wall_Plaster_Window_Wide_Round", "Wall_Plaster_Window_Thin_Round", "Wall_Plaster_Door_Round",
        "Wall_Plaster_WoodGrid", "Wall_UnevenBrick_Straight", "Wall_UnevenBrick_Window_Wide_Round", "Wall_UnevenBrick_Door_Round",
        "Corner_Exterior_Wood", "Corner_Exterior_Brick",
        "Roof_RoundTiles_4x4", "Roof_RoundTiles_4x6", "Roof_RoundTiles_4x8", "Roof_RoundTiles_6x6", "Roof_RoundTiles_6x8",
        "Roof_RoundTiles_6x10", "Roof_RoundTiles_6x12", "Roof_RoundTiles_8x8", "Roof_RoundTiles_8x10", "Roof_RoundTiles_8x12",
        "Roof_RoundTiles_8x14", "Prop_Chimney", "Prop_WoodenFence_Single", "Prop_Wagon", "Prop_Crate",
    ]),
    "nature": (NATURE, "https://quaternius.com/packs/stylizednaturemegakit.html", [
        "CommonTree_1", "CommonTree_2", "CommonTree_3", "CommonTree_4", "CommonTree_5", "Pine_1", "Pine_2", "Pine_3",
        "Bush_Common", "Bush_Common_Flowers", "Grass_Common_Short", "Grass_Common_Tall", "Grass_Wispy_Tall",
        "Flower_3_Group", "Flower_4_Group", "Clover_1", "Fern_1", "Plant_1", "Rock_Medium_1", "Rock_Medium_2", "Rock_Medium_3",
        "Pebble_Round_1", "Mushroom_Common",
    ]),
}
ANIMALS = [  # (gói, file .blend, id, bảng màu)
    ("x_Animal_Pack_Vol.2_by_Quaternius/Animal Pack Vol.2 by @Quaternius/Blends/Dog.blend", "Dog", "https://opengameart.org/content/animated-animales-low-poly", "shiba", 0.128),
    ("x_Animal_Pack_Vol.2_by_Quaternius/Animal Pack Vol.2 by @Quaternius/Blends/Wolf.blend", "Wolf", "https://opengameart.org/content/animated-animales-low-poly", "wolf", 0.14),
]

BLENDER_EXPORT = r'''
import bpy, sys, mathutils
args = sys.argv[sys.argv.index("--") + 1:]
out, palette = args[0], args[1]
PAL = {  # (lưng/thân, bụng-mặt-chân, mũi/tai sẫm)
    "shiba": ((0.80, 0.42, 0.14), (0.97, 0.91, 0.80), (0.08, 0.06, 0.05)),
    "wolf":  ((0.42, 0.40, 0.38), (0.82, 0.80, 0.76), (0.10, 0.10, 0.10)),
    "pug":   ((0.86, 0.72, 0.52), (0.92, 0.84, 0.70), (0.12, 0.10, 0.09)),
}
body, cream, dark = PAL[palette]
for o in [o for o in bpy.data.objects if o.type == 'MESH']:
    me = o.data
    # Làm mịn: chia nhỏ 1 cấp + shade smooth (giữ skinning: modifier đặt TRƯỚC armature)
    sub = o.modifiers.new("Subd", 'SUBSURF'); sub.levels = 1; sub.render_levels = 1
    while o.modifiers.find("Subd") > 0:
        bpy.context.view_layer.objects.active = o
        bpy.ops.object.modifier_move_up(modifier="Subd")
    for poly in me.polygons: poly.use_smooth = True
    vs = [v.co for v in me.vertices]
    mn = mathutils.Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    mx = mathutils.Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    up = 2  # Blender: Z lên
    top = max(vs, key=lambda v: v[up])          # điểm cao nhất ~ đầu/tai
    # trục dài của thân (X hoặc Y trong Blender) và phía đầu
    ax = 0 if (mx.x - mn.x) >= (mx.y - mn.y) else 1
    head_sign = 1.0 if top[ax] > (mn[ax] + mx[ax]) / 2 else -1.0
    H = mx[up] - mn[up]; L = mx[ax] - mn[ax]
    col = me.color_attributes.new("Col", 'FLOAT_COLOR', 'POINT')
    for i, v in enumerate(me.vertices):
        p = v.co; n = v.normal
        h = (p[up] - mn[up]) / H
        f = ((p[ax] - mn[ax]) / L) if head_sign > 0 else ((mx[ax] - p[ax]) / L)  # 1 = mũi
        c = body
        if n[up] < -0.25 or h < 0.12: c = cream                       # bụng, tất chân
        if f > 0.78 and h < 0.80 and n[up] < 0.6: c = cream            # mõm, má, cằm
        if f > 0.80 and 0.45 < h < 0.75 and abs(p[1 - ax]) < 0.6 * abs(mx[1 - ax] - mn[1 - ax]) and n[up] < -0.1: c = cream  # ngực
        if f > 0.965: c = dark                                         # mũi
        col.data[i].color = (c[0], c[1], c[2], 1.0)
    for m in me.materials:
        m.use_nodes = True
        nt = m.node_tree
        nt.nodes.clear()  # vật liệu cũ (không có Principled): dựng lại tối thiểu
        bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
        outn = nt.nodes.new('ShaderNodeOutputMaterial')
        nt.links.new(bsdf.outputs['BSDF'], outn.inputs['Surface'])
        bsdf.inputs['Metallic'].default_value = 0.0
        bsdf.inputs['Roughness'].default_value = 0.85
        attr = nt.nodes.new('ShaderNodeVertexColor'); attr.layer_name = "Col"
        nt.links.new(attr.outputs['Color'], bsdf.inputs['Base Color'])
bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', export_animations=True, export_apply=True,
                          export_yup=True, export_normals=True, export_materials='EXPORT', export_vertex_color='ACTIVE')
'''


def shrink_png(data):
    im = Image.open(io.BytesIO(data))
    im.load()
    if max(im.size) > MAX_TEX:
        s = MAX_TEX / max(im.size)
        im = im.resize((max(1, round(im.size[0] * s)), max(1, round(im.size[1] * s))), Image.LANCZOS)
    if im.mode not in ("RGB", "RGBA"):
        im = im.convert("RGBA")
    buf = io.BytesIO()
    # Không dùng alpha (hoặc alpha toàn 255) -> JPEG nhỏ hơn ~8 lần; ảnh cần trong suốt (lá, cỏ) giữ PNG
    if im.mode == "RGB" or im.getchannel("A").getextrema()[0] == 255:
        im.convert("RGB").save(buf, "JPEG", quality=88, optimize=True)
        return buf.getvalue(), "image/jpeg"
    im.save(buf, "PNG", optimize=True)
    return buf.getvalue(), "image/png"


def find_image(gltf_dir, uri):
    for cand in (os.path.join(gltf_dir, uri), os.path.join(gltf_dir, "..", "Textures", uri), os.path.join(gltf_dir, "Textures", uri)):
        if os.path.exists(cand):
            return cand
    raise FileNotFoundError(uri)


def gltf_to_glb(path, out_path):
    d = os.path.dirname(path)
    j = json.load(open(path, encoding="utf-8"))
    blob = bytearray()

    def add(data):
        while len(blob) % 4:
            blob.append(0)
        off = len(blob)
        blob.extend(data)
        return off

    # buffer gốc -> nối vào blob, dịch byteOffset của bufferView
    base = []
    for b in j["buffers"]:
        data = open(os.path.join(d, b["uri"]), "rb").read()
        base.append(add(data))
    for bv in j.get("bufferViews", []):
        bv["byteOffset"] = bv.get("byteOffset", 0) + base[bv.get("buffer", 0)]
        bv["buffer"] = 0
    for img in j.get("images", []):
        if "uri" in img:
            data, mime = shrink_png(open(find_image(d, img.pop("uri")), "rb").read())
            off = add(data)
            j["bufferViews"].append({"buffer": 0, "byteOffset": off, "byteLength": len(data)})
            img["bufferView"] = len(j["bufferViews"]) - 1
            img["mimeType"] = mime
    while len(blob) % 4:
        blob.append(0)
    j["buffers"] = [{"byteLength": len(blob)}]
    js = json.dumps(j, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(blob)
    with open(out_path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(blob), 0x004E4942) + bytes(blob))


def sha256(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("--blender", default="D:/DevTools/blender/blender.exe")
    a = ap.parse_args()
    man_path = os.path.join(ROOT, "assets", "manifest.json")
    man = json.load(open(man_path, encoding="utf-8"))
    man["assets"] = [x for x in man["assets"] if not x["id"].startswith("cc0_")]
    for pack, (sub, url, names) in SELECT.items():
        os.makedirs(os.path.join(OUT, pack), exist_ok=True)
        for n in names:
            dst = os.path.join(OUT, pack, n + ".glb")
            gltf_to_glb(os.path.join(a.src, sub, n + ".gltf"), dst)
            man["assets"].append({
                "id": f"cc0_{pack}_{n.lower()}", "path": f"cc0/{pack}/{n}.glb", "type": "prop", "quality": "production",
                "source": url, "author": "Quaternius", "license": "CC0-1.0", "scale": 1.0, "sha256": sha256(dst),
                "height_m": 0, "tags": [pack], "required_animations": []})
    os.makedirs(os.path.join(OUT, "animals"), exist_ok=True)
    script = os.path.join(OUT, "_export.py")
    open(script, "w", encoding="utf-8").write(BLENDER_EXPORT)
    try:
        for rel, n, url, pal, scale in ANIMALS:
            dst = os.path.join(OUT, "animals", n + ".glb")
            r = subprocess.run([a.blender, "--background", "--factory-startup", "--disable-autoexec", os.path.join(a.src, rel),
                                "--python", script, "--", dst, pal], capture_output=True, text=True, encoding="utf-8", errors="replace")
            if r.returncode != 0 or not os.path.exists(dst):
                print(r.stdout[-2000:], r.stderr[-2000:])
                sys.exit(f"Blender lỗi khi chuyển {n}")
            man["assets"].append({
                "id": f"cc0_animal_{n.lower()}", "path": f"cc0/animals/{n}.glb", "type": "character", "quality": "production",
                "source": url + " (tô màu vertex + subdivision bởi scripts/import_cc0.py)", "author": "Quaternius",
                "license": "CC0-1.0", "scale": scale, "sha256": sha256(dst),
                "height_m": 0.6, "tags": ["quadruped"], "required_animations": ["Idle", "Walking"]})
    finally:
        os.remove(script)
    json.dump(man, open(man_path, "w", encoding="utf-8", newline="\n"), ensure_ascii=False, indent=2)
    total = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(OUT) for f in fs)
    print(f"đã nhập {sum(len(v[2]) for v in SELECT.values())} mô hình + {len(ANIMALS)} động vật; tổng {total / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
