# Chạy headless: blender --background --factory-startup --python normalize_export.py -- <input> <output.glb> [--scale S] [--prefix P]
# - Nhập .glb/.gltf/.fbx/.obj
# - (tùy chọn) nhân scale cho các object gốc rồi apply transform cho mesh không có armature
# - (tùy chọn) đổi tên object theo tiền tố quy ước
# - Xuất GLB (+Y up theo glTF), giữ animation
# Không dùng UI Blender. Exit code khác 0 nếu lỗi.
import sys
import os
import bpy


def fail(msg):
    print(f"[normalize_export] LỖI: {msg}", file=sys.stderr)
    sys.exit(1)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(argv) < 2:
        fail("cần <input> <output.glb>")
    src, dst = os.path.abspath(argv[0]), os.path.abspath(argv[1])
    scale = 1.0
    prefix = ""
    if "--scale" in argv:
        scale = float(argv[argv.index("--scale") + 1])
    if "--prefix" in argv:
        prefix = argv[argv.index("--prefix") + 1]
    if not os.path.exists(src):
        fail(f"không tìm thấy {src}")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    ext = os.path.splitext(src)[1].lower()
    if ext in (".glb", ".gltf"):
        bpy.ops.import_scene.gltf(filepath=src)
    elif ext == ".fbx":
        bpy.ops.import_scene.fbx(filepath=src)
    elif ext == ".obj":
        bpy.ops.wm.obj_import(filepath=src)
    else:
        fail(f"định dạng không hỗ trợ: {ext}")

    roots = [o for o in bpy.context.scene.objects if o.parent is None]
    if scale != 1.0:
        for o in roots:
            o.scale = (o.scale[0] * scale, o.scale[1] * scale, o.scale[2] * scale)
    bpy.context.view_layer.update()

    if prefix:
        for o in bpy.context.scene.objects:
            if not o.name.startswith(prefix):
                o.name = prefix + o.name

    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    tris = 0
    for o in meshes:
        o.data.calc_loop_triangles()
        tris += len(o.data.loop_triangles)
    print(f"[normalize_export] objects={len(bpy.context.scene.objects)} meshes={len(meshes)} tris={tris} "
          f"actions={len(bpy.data.actions)} scale={scale}")

    os.makedirs(os.path.dirname(dst), exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", export_yup=True, export_animations=True)
    if not os.path.exists(dst):
        fail("export không tạo file")
    print(f"[normalize_export] đã ghi {dst}")


try:
    main()
except SystemExit:
    raise
except Exception as e:  # noqa: BLE001
    fail(repr(e))
