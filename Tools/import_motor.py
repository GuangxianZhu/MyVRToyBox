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


# ---------------------------------------------------------------- 抓取单元
# 84 个网格不是每个都值得单独抓（28 根导条、18 个线圈……）。爆炸路线完全相同的零件本来就一起动，
# 按路线分组就是天然的"抓取单元"；只有螺钉这类同样路线但各在各位置的紧固件要一颗一颗分开。
FASTENER_PART_NOS = (18, 22, 27)      # 端盖螺栓、风罩螺钉、盒盖螺钉
PINCH_MAX_CM = 7.0                     # 最长边不超过这么多（装配尺寸，未缩放）的零件用"捏"
MODEL_INFO = dict(
    title='三相鼠笼式异步电动机 YE4-132M-4',
    body='7.5 kW · 380 V △ · 4 极 · 1460 r/min\n装配状态：抓任意位置拿起整机看。点 EXPLODE 拆开后，每个零件都能单独抓。',
)
# 多个零件合成的单元，名字和说明手写；单个零件的单元直接用 glb 里的中文名、材料、作用
UNIT_INFO = {
    'base': dict(name_cn='定子与机座', material='HT250 铸铁机座 + 50W470 硅钢片 + QZY-2 漆包铜线',
                 desc='机座、定子铁芯、18 个线圈、槽楔、引出线、铭牌。拆电机时它不动，其他零件都从它上面拆下来。'),
    'rotor': dict(name_cn='转子总成', material='45 钢轴 / 50W470 硅钢片 / 铸铜鼠笼 / 6308、6208 轴承',
                  desc='轴、叠片铁芯、28 根斜导条和端环、前后轴承一起抽出。旋转磁场切割导条感应出电流，'
                       '电流在磁场里受力产生转矩，转子就转起来。'),
    'tbox': dict(name_cn='接线盒（含接线板、电源线）', material='HT200 铸铁盒座 + DMC 接线板 + 黄铜接线柱',
                 desc='6 根引出线接到接线板上，3 片铜连接片接成 △；电源线 L1/L2/L3 接 U1/V1/W1，黄绿线接地。'),
}


def ue_bounds(gltf, node):
    """节点网格在 UE 坐标里的包围盒（厘米，模型未缩放）。glTF (x, y, z) 米 → UE (x, z, y) 厘米。"""
    lo, hi = [1e9] * 3, [-1e9] * 3
    for prim in gltf['meshes'][node['mesh']]['primitives']:
        acc = gltf['accessors'][prim['attributes']['POSITION']]
        lo = [min(a, b) for a, b in zip(lo, acc['min'])]
        hi = [max(a, b) for a, b in zip(hi, acc['max'])]
    order = (0, 2, 1)
    return [lo[i] * 100.0 for i in order], [hi[i] * 100.0 for i in order]


def merge(boxes):
    lo = [min(b[0][i] for b in boxes) for i in range(3)]
    hi = [max(b[1][i] for b in boxes) for i in range(3)]
    return lo, hi


def grab_spec(unit_id, nodes, bounds):
    """抓点描述（模型坐标，厘米）。C++ 的 AToyPartUnit 按 type 选抓法。"""
    lo, hi = merge([bounds[n['name']] for n in nodes])
    size = [h - l for l, h in zip(lo, hi)]
    if unit_id == 'rotor':
        # 转子：轴伸两头握轴（Φ38），中间握铁芯外圆
        shaft = next(bounds[n['name']] for n in nodes if n['name'] == 'P08_Shaft')
        core = next(bounds[n['name']] for n in nodes if n['name'] == 'P10_RotorCore')
        r_core = (core[1][1] - core[0][1]) * 0.5
        return dict(type='cylinder', segments=[
            [round(shaft[0][0], 2), round(core[0][0], 2), 1.9],
            [round(core[0][0], 2), round(core[1][0], 2), round(r_core, 2)],
            [round(core[1][0], 2), round(shaft[1][0], 2), 1.9],
        ])
    if max(size) <= PINCH_MAX_CM:
        return dict(type='pinch')
    center_yz = [(lo[i] + hi[i]) * 0.5 for i in (1, 2)]
    round_yz = abs(size[1] - size[2]) <= 0.15 * max(size[1], size[2])
    if round_yz and abs(center_yz[0]) < 1.5 and abs(center_yz[1]) < 1.5:
        # 绕电机轴的回转体（端盖、风扇、风罩、波形垫圈……）：握外缘
        return dict(type='cylinder', segments=[[round(lo[0], 2), round(hi[0], 2), round(max(size[1], size[2]) * 0.5, 2)]])
    # 其他：按单元里第一个零件（"主零件"，比如接线盒的盒座，不是拖得很长的电源线）的包围盒，像方块一样手掌贴一个面
    mlo, mhi = bounds[nodes[0]['name']]
    return dict(type='box', center=[round((a + b) * 0.5, 2) for a, b in zip(mlo, mhi)],
                extent=[round((b - a) * 0.5, 2) for a, b in zip(mlo, mhi)])


def build_units(gltf, part_nodes):
    """按爆炸路线分组，返回 (零件名 → 单元 id, 单元列表)。"""
    groups = {}
    for node in part_nodes:
        extras = node.get('extras', {})
        if extras.get('part_no') in FASTENER_PART_NOS:
            key = node['name']
        else:
            key = extras.get('ex', '[]')
        groups.setdefault(key, []).append(node)

    bounds = {n['name']: ue_bounds(gltf, n) for n in part_nodes}
    unit_of, units = {}, []
    for key, nodes in groups.items():
        names = [n['name'] for n in nodes]
        if key == '[]':
            unit_id = 'base'
        elif 'P08_Shaft' in names:
            unit_id = 'rotor'
        elif 'P23_TerminalBox' in names:
            unit_id = 'tbox'
        else:
            unit_id = names[0]
        for n in names:
            unit_of[n] = unit_id
        first = nodes[0].get('extras', {})
        info = UNIT_INFO.get(unit_id) or dict(name_cn=first.get('name_cn', ''), material=first.get('material_spec', ''),
                                              desc=first.get('desc', ''))
        units.append(dict(
            id=unit_id,
            grabbable=unit_id != 'base',
            name_cn=info['name_cn'],
            material=info['material'],
            desc=info['desc'],
            grab=None if unit_id == 'base' else grab_spec(unit_id, nodes, bounds),
        ))
    return unit_of, units


def main():
    glb = os.environ.get('MOTOR_GLB') or (sys.argv[1] if len(sys.argv) > 1 else '')
    if not glb or not os.path.exists(glb):
        raise RuntimeError(f'找不到 motor.glb：{glb!r}（设置环境变量 MOTOR_GLB）')

    gltf = read_glb_json(glb)
    # MOTOR_MANIFEST_ONLY=1：资产已经导入过、只想重写 parts.json（比如改了抓点规则），就不重新导入，
    # 免得 100 多个 uasset 全部变成新的二进制、在 git 里白白多出十几 MB
    manifest_only = os.environ.get('MOTOR_MANIFEST_ONLY') == '1'
    if not manifest_only:
        import_glb(glb)
    meshes = static_meshes()
    unreal.log(f'ToyBox: 导入了 {len(meshes)} 个静态网格')

    part_nodes = [n for n in gltf.get('nodes', []) if 'mesh' in n and not n.get('name', '').startswith('CAP')]
    unit_of, units = build_units(gltf, part_nodes)

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
            unit=unit_of.get(name, name),
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
        json.dump(dict(source=os.path.basename(glb), model=MODEL_INFO, units=units, parts=parts),
                  f, ensure_ascii=False, indent=1)
    kinds = {}
    for u in units:
        kind = u['grab']['type'] if u['grab'] else 'base'
        kinds[kind] = kinds.get(kind, 0) + 1
    unreal.log(f'ToyBox: 写出 {MANIFEST}，{len(parts)} 个零件，{len(units)} 个抓取单元 {kinds}')
    if missing:
        unreal.log_warning(f'ToyBox: 这些节点没找到对应网格：{missing}')


main()
