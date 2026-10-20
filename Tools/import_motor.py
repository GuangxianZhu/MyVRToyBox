"""把 ITBasics 生成的 motor.glb 导入成 UE 资产，并写出爆炸图零件清单。

在编辑器里跑（命令行由 scripts/import_motor.bat 调用）：
    UnrealEditor-Cmd.exe VRToyBox.uproject -run=pythonscript -script="Tools/import_motor.py"
glb 路径从环境变量 MOTOR_GLB 读（也可以作为第一个参数传进来）。

产出：
    /Game/Models/Motor/...            每个零件一个静态网格（加上材质、贴图）
    Content/Models/Motor/parts.json   零件名 → 网格资产路径 + 爆炸步骤（UE 坐标，厘米）

坐标换算：
    build_motor.py 把爆炸步骤写进每个节点的 extras["ex"]，用的是 glTF 坐标、毫米：[gx, gy, gz, 出场时刻]。
    UE 导入 glTF 时把 (x, y, z) 换成 (x, z, y)（右手 Y 朝上 → 左手 Z 朝上），米换成厘米。
    这里用同样的换算，再用两个已知方向的零件（风罩在 -X、接线盒在 +Z）检查一遍。
"""
import json
import os
import re
import struct
import sys

import unreal

DEST = '/Game/Models/Motor'
MANIFEST = os.path.join(unreal.Paths.project_content_dir(), 'Models', 'Motor', 'parts.json')


def read_glb_json(path):
    with open(path, 'rb') as f:
        magic, _version, _length = struct.unpack('<4sII', f.read(12))
        if magic != b'glTF':
            raise RuntimeError(f'{path} 不是 glb 文件')
        chunk_len, chunk_type = struct.unpack('<II', f.read(8))
        if chunk_type != 0x4E4F534A:  # 'JSON'
            raise RuntimeError('glb 第一个块不是 JSON')
        return json.loads(f.read(chunk_len).decode('utf-8'))


def normalize(name):
    name = re.sub(r'^(SM|S)_', '', name)
    return re.sub(r'[^0-9a-z]', '', name.lower())


def to_ue_cm(step):
    gx, gy, gz, start = step
    return [gx * 0.1, gz * 0.1, gy * 0.1, start]


def import_glb(glb):
    if unreal.EditorAssetLibrary.does_directory_exist(DEST):
        unreal.EditorAssetLibrary.delete_directory(DEST)
    task = unreal.AssetImportTask()
    task.filename = glb
    task.destination_path = DEST
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])


def static_meshes():
    meshes = {}
    for path in unreal.EditorAssetLibrary.list_assets(DEST, recursive=True, include_folder=False):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if isinstance(asset, unreal.StaticMesh):
            meshes[normalize(asset.get_name())] = asset
    return meshes


def center(mesh):
    box = mesh.get_bounding_box()
    return (box.min + box.max) * 0.5


def main():
    glb = os.environ.get('MOTOR_GLB') or (sys.argv[1] if len(sys.argv) > 1 else '')
    if not glb or not os.path.exists(glb):
        raise RuntimeError(f'找不到 motor.glb：{glb!r}（设置环境变量 MOTOR_GLB）')

    gltf = read_glb_json(glb)
    import_glb(glb)
    meshes = static_meshes()
    unreal.log(f'ToyBox: 导入了 {len(meshes)} 个静态网格')

    parts, missing, used = [], [], set()
    for node in gltf.get('nodes', []):
        name = node.get('name', '')
        if 'mesh' not in node:
            continue
        mesh_name = gltf['meshes'][node['mesh']].get('name', name)
        if name.startswith('CAP'):
            continue  # 剖切盖面只给网页的"剖开"模式用
        asset = meshes.get(normalize(name)) or meshes.get(normalize(mesh_name))
        if asset is None:
            missing.append(name)
            continue
        used.add(normalize(asset.get_name()))
        extras = node.get('extras', {})
        ex = json.loads(extras.get('ex', '[]'))
        parts.append(dict(
            name=name,
            name_cn=extras.get('name_cn', ''),
            mesh=asset.get_path_name(),
            spin=int(extras.get('spin', 0)),
            ex=[to_ue_cm(e) for e in ex],
        ))

    # 剖切盖面网格用不上，删掉，免得资产越堆越多
    for key, asset in meshes.items():
        if key not in used and key.startswith('cap'):
            unreal.EditorAssetLibrary.delete_asset(asset.get_path_name())

    # 方向检查：风罩应该在 -X（非轴伸端），接线盒在 +Z（顶上）
    by_name = {p['name']: unreal.load_asset(p['mesh']) for p in parts}
    fan, box = by_name.get('P21_FanCover'), by_name.get('P23_TerminalBox')
    if fan and box:
        fc, bc = center(fan), center(box)
        ok = fc.x < 0 and bc.z > 0
        unreal.log(f'ToyBox: 方向检查 风罩中心 {fc} 接线盒中心 {bc} -> {"OK" if ok else "不对，检查坐标换算"}')
        if not ok:
            unreal.log_error('ToyBox: 坐标方向和预期不一致，爆炸方向可能是反的')

    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    with open(MANIFEST, 'w', encoding='utf-8') as f:
        json.dump(dict(source=os.path.basename(glb), parts=parts), f, ensure_ascii=False, indent=1)
    unreal.log(f'ToyBox: 写出 {MANIFEST}，{len(parts)} 个零件')
    if missing:
        unreal.log_warning(f'ToyBox: 这些节点没找到对应网格：{missing}')


main()
