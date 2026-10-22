"""把爆炸图模型（glb）导入成 UE 资产，并写出爆炸图零件清单。电机、阀门共用。

在编辑器里跑（命令行由 scripts/import_motor.bat、scripts/import_valve.bat 调用）：
    UnrealEditor-Cmd.exe VRToyBox.uproject -run=pythonscript -script=...
环境变量：
    MODEL_NAME           模型名，决定资产目录 /Game/Models/<名字> 和下面 MODELS 里的配置（Motor / Valve / Drone / Lathe / Multimeter）
    MODEL_GLB            glb 路径（电机兼容旧的 MOTOR_GLB）
    MODEL_MANIFEST_ONLY  =1 时不重新导入资产，只重写 parts.json（兼容旧的 MOTOR_MANIFEST_ONLY）

glb 的约定（ITBasics 的 Blender 脚本、Tools/export_valve_glb.mjs 都按这个出）：
    每个零件一个节点，节点名就是零件名，顶点在装配位置、单位米；extras 里有
    part_no / name_cn / material_spec / desc / ex（爆炸步骤，glTF 坐标、毫米：[gx, gy, gz, 出场时刻]）。

产出：
    /Game/Models/<名字>/...            每个零件一个静态网格（加上材质、贴图）
    Content/Models/<名字>/parts.json   零件 → 网格资产路径 + 爆炸步骤（UE 坐标，厘米）+ 抓取单元

坐标换算：
    UE 导入 glTF 时把 (x, y, z) 换成 (x, z, y)（右手 Y 朝上 → 左手 Z 朝上），米换成厘米（用电机的 84 个零件逐个核对过，误差 0）。
"""
import fnmatch
import json
import math
import os
import re
import struct
import sys
from array import array
from collections import Counter

import unreal

MODEL = os.environ.get('MODEL_NAME', 'Motor')
DEST = f'/Game/Models/{MODEL}'
MANIFEST = os.path.join(unreal.Paths.project_content_dir(), 'Models', MODEL, 'parts.json')

# 和 AToyStudio 里给模型的大小一致：装配状态最长边缩放到这么长（模型配置里写了 length_cm 的按它，比如万用表 40）。
# 小件用"捏"的判断按缩放后的尺寸算
TARGET_LENGTH_CM = 50.0
PINCH_MAX_CM = 7.0

# ---------------------------------------------------------------- 各模型的配置
# info：整机信息牌；units：多个零件合成的单元手写名字和说明（单个零件的单元直接用 glb 里的中文名、材料、作用）；
# named：含某个零件的单元起个好认的 id；check：导入后的方向检查（零件名 → 中心应该在的方向）
MODELS = {
    'Motor': dict(
        info=dict(
            title='三相鼠笼式异步电动机 YE4-132M-4',
            body='7.5 kW · 380 V △ · 4 极 · 1460 r/min\n装配状态：抓任意位置拿起整机看。点 EXPLODE 拆开后，每个零件都能单独抓。',
        ),
        named={'P08_Shaft': 'rotor', 'P23_TerminalBox': 'tbox'},
        units={
            'base': dict(name_cn='定子与机座', material='HT250 铸铁机座 + 50W470 硅钢片 + QZY-2 漆包铜线',
                         desc='机座、定子铁芯、18 个线圈、槽楔、引出线、铭牌。拆电机时它不动，其他零件都从它上面拆下来。'),
            'rotor': dict(name_cn='转子总成', material='45 钢轴 / 50W470 硅钢片 / 铸铜鼠笼 / 6308、6208 轴承',
                          desc='轴、叠片铁芯、28 根斜导条和端环、前后轴承一起抽出。旋转磁场切割导条感应出电流，'
                               '电流在磁场里受力产生转矩，转子就转起来。'),
            'tbox': dict(name_cn='接线盒（含接线板、电源线）', material='HT200 铸铁盒座 + DMC 接线板 + 黄铜接线柱',
                         desc='6 根引出线接到接线板上，3 片铜连接片接成 △；电源线 L1/L2/L3 接 U1/V1/W1，黄绿线接地。'),
        },
        # 转子：轴伸两头握轴（Φ38），中间握铁芯外圆
        rotor=('P08_Shaft', 'P10_RotorCore', 1.9),
        check={'P21_FanCover': (-1, 0, 0), 'P23_TerminalBox': (0, 0, 1)},
    ),
    'Valve': dict(
        info=dict(
            title='气动球阀（三片式球阀 + 齿轮齿条单作用执行器）',
            body='SUS316 阀体 · 镜面抛光球体 · PTFE 阀座 · 弹簧复位（断气即关）\n装配状态：抓任意位置拿起整个阀门看。点 EXPLODE 拆开后，每个零件都能单独抓。',
        ),
        named={},
        units={},
        rotor=None,
        # 网页版的爆炸距离是按整个屏幕来定的：执行器整体往上飞 15cm、里面的零件再往上飞，放大 2.5 倍摆在桌上会飞出视野，缩到 0.4
        explode_scale=0.4,
        check={'P16_ActuatorBody': (0, 0, 1)},
    ),
    'Drone': dict(
        info=dict(
            title='涵道无人机（3 寸涵道穿越机，原创设计）',
            body='4S 650mAh · 1404 外转子电机 × 4 · 3 寸三叶桨 · 数字图传\n装配状态：抓任意位置拿起整机看。点 EXPLODE 拆开后，每个零件都能单独抓。',
        ),
        named={},
        units={
            'base': dict(name_cn='机架与立柱', material='3 mm 碳纤维板 + 尼龙立柱',
                         desc='中央板、4 条机臂和电机座是一整块碳板；尼龙立柱撑起飞控塔。拆机时它不动，其他零件都从它上面拆下来。'),
        },
        # Blender 脚本没写材料，按零件号补上（信息牌第二行）
        materials={
            3: '橡胶', 4: 'PA 尼龙注塑', 5: '1404 外转子无刷 · 钕磁铁 + 漆包铜线', 6: 'PC 三叶桨（正 / 反桨）',
            7: '四合一电调 · 4 层 PCB + 24 颗 MOS 管', 8: '飞控 · F7 MCU + 陀螺仪 + 气压计', 9: '数字图传 · 铝散热片 + 屏蔽罩',
            10: 'TPU 3D 打印', 11: 'TPU 3D 打印', 12: '数字 FPV 相机（上仰 15°）', 13: '4S 650 mAh 锂聚合物电池 · XT30',
            14: '尼龙魔术贴绑带', 15: '偶极子天线 × 2 · MMCX',
        },
        rotor=None,
        # 实物约 19cm，放大 2.6 倍后原来的爆炸距离（绑带往上 16.5cm）会冲到 43cm 高，缩到 0.8
        explode_scale=0.8,
        check={'P13_Battery': (0, 0, 1)},
    ),
    'Lathe': dict(
        info=dict(
            title='迷你桌面金属车床（8 合 1 小车床，按产品图估算尺寸建模）',
            body='24 V 有刷直流电机 · 皮带减速约 2.7 : 1 · Φ50 三爪卡盘 · 中心高 87 mm · 整机长约 37 cm · 84 个零件\n装配状态：抓任意位置拿起整台车床看。点 EXPLODE 拆开后，每个零件都能单独抓（电机、卡盘、主轴箱都拆到了里面）。',
        ),
        named={'P27_Motor': 'motor'},
        units={
            'base': dict(name_cn='底板与床身', material='6061 铝合金底板 + 铝型材床身 + GCr15 直线导轨 + 电机座',
                         desc='底板、4 个脚垫、床身、直线导轨、电机座。拆车床时它不动，其他零件都从它上面拆下来。'
                              '导轨的直度决定车刀走得直不直。'),
            'motor': dict(name_cn='直流电机外壳（含电源线）', material='冷轧钢板机壳 · PVC 电源线 · DC 5.5 × 2.1 插头',
                          desc='机壳也是磁路的一部分：两块磁钢的磁力线穿过转子，再经机壳绕回来。'
                               '电压越高电机转得越快，调速器改电压就能无级调速。'),
        },
        rotor=None,
        check={'P07_Headstock': (1, 0, 0), 'P27_Motor': (0, 1, 0), 'P18_Tailstock': (-1, 0, 0)},
    ),
    'Multimeter': dict(
        info=dict(
            title='数字万用表 · 原理爆炸图（外观参考 SATA 世达 DO5054）',
            body='自动识别 · 零火线判断 · NCV 非接触测电 · 错档防烧 · 背光 LCD · 2 节 AAA\n'
                 '装配状态：抓任意位置拿起整台表看。点 EXPLODE 拆开后每个零件都能单独抓，信息牌讲它在电路里干什么。',
        ),
        named={},
        units={
            'base': dict(name_cn='底壳与电池弹片', material='黑色 ABS 注塑 + 镀镍弹簧钢片',
                         desc='底壳、电池弹簧和引线。拆表时它不动，其他零件都从它上面拿走。'
                              '两节 AAA 电池在这里串联成 3 V，经红黑引线送到主板。'),
        },
        rotor=None,
        # 产品要求桌上展示 40cm（其他模型 50cm）；C++ 读 parts.json 里的 length_cm
        length_cm=40.0,
        # 屏幕在模型 -Y（Blender +Y），表笔：红的在右（+X）
        check={'P07_LCD': (0, -1, 1), 'P31_ProbeRed': (1, 0, 0), 'P32_ProbeBlack': (-1, 0, 0)},
    ),
}
CONFIG = MODELS.get(MODEL, dict(info=dict(title=MODEL, body=''), named={}, units={}, rotor=None, check={}))


def read_glb(path):
    """返回 (glTF JSON, 二进制块)。"""
    with open(path, 'rb') as f:
        data = f.read()
    magic, _version, _length = struct.unpack_from('<4sII', data, 0)
    if magic != b'glTF':
        raise RuntimeError(f'{path} 不是 glb 文件')
    chunk_len, chunk_type = struct.unpack_from('<II', data, 12)
    if chunk_type != 0x4E4F534A:  # 'JSON'
        raise RuntimeError('glb 第一个块不是 JSON')
    gltf = json.loads(data[20:20 + chunk_len].decode('utf-8'))
    off = 20 + chunk_len
    binary = b''
    if off + 8 <= len(data):
        bin_len, _ = struct.unpack_from('<II', data, off)
        binary = data[off + 8: off + 8 + bin_len]
    return gltf, binary


def node_geometry(gltf, binary, node):
    """节点网格的顶点和三角形，UE 坐标、厘米、模型未缩放（加上节点平移）。只认 float VEC3 的 POSITION。"""
    t = node.get('translation', [0.0, 0.0, 0.0])
    verts, tris = [], []
    for prim in gltf['meshes'][node['mesh']]['primitives']:
        acc = gltf['accessors'][prim['attributes']['POSITION']]
        view = gltf['bufferViews'][acc['bufferView']]
        start = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
        stride = view.get('byteStride', 12)
        base = len(verts)
        for i in range(acc['count']):
            x, y, z = struct.unpack_from('<3f', binary, start + i * stride)
            verts.append(((x + t[0]) * 100.0, (z + t[2]) * 100.0, (y + t[1]) * 100.0))
        if 'indices' in prim:
            iacc = gltf['accessors'][prim['indices']]
            iview = gltf['bufferViews'][iacc['bufferView']]
            fmt, width = {5121: ('B', 1), 5123: ('H', 2), 5125: ('I', 4)}[iacc['componentType']]
            istart = iview.get('byteOffset', 0) + iacc.get('byteOffset', 0)
            idx = struct.unpack_from(f'<{iacc["count"]}{fmt}', binary, istart)
        else:
            idx = range(acc['count'])
        tris.extend((base + idx[i], base + idx[i + 1], base + idx[i + 2]) for i in range(0, len(idx) - 2, 3))
    return verts, tris


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
# 网格不是每个都值得单独抓（电机的 28 根导条、18 个线圈……）。爆炸路线完全相同的零件本来就一起动，
# 按路线分组就是天然的"抓取单元"。例外：同一种零件有 2~8 个（螺钉、端盖、弹簧……），各在各位置，一个一个分开抓；
# 超过 8 个的（导条）当成一组。爆炸路线为空（不动）的零件合成基座，不能抓。
SPLIT_MAX_INSTANCES = 8


def ue_bounds(gltf, node):
    """节点网格在 UE 坐标里的包围盒（厘米，模型未缩放）。glTF (x, y, z) 米 → UE (x, z, y) 厘米。
    节点自己的平移也算进去（无人机的零件节点放在各自的安装位置；UE 导入时会把节点变换烘进网格，两边一致）。
    旋转 / 缩放没处理：现在的模型零件节点都没有。"""
    lo, hi = [1e9] * 3, [-1e9] * 3
    t = node.get('translation', [0.0, 0.0, 0.0])
    for prim in gltf['meshes'][node['mesh']]['primitives']:
        acc = gltf['accessors'][prim['attributes']['POSITION']]
        lo = [min(a, b + o) for a, b, o in zip(lo, acc['min'], t)]
        hi = [max(a, b + o) for a, b, o in zip(hi, acc['max'], t)]
    order = (0, 2, 1)
    return [lo[i] * 100.0 for i in order], [hi[i] * 100.0 for i in order]


def merge(boxes):
    lo = [min(b[0][i] for b in boxes) for i in range(3)]
    hi = [max(b[1][i] for b in boxes) for i in range(3)]
    return lo, hi


ROUND_BINS = 36


def round_axis(verts, lo, hi):
    """零件是不是绕 X / Y / Z 某根轴的回转体（圆心不要求在原点）。
    是：返回 (轴序号, 圆心, 外圆半径)；否：None。
    判定：垂直于轴的两个方向一样宽（差 15% 以内）；绕轴分成 36 个角度格，每格取最远的顶点，
    至少 60% 的格子离中位数不超过 8% 才算圆。正方形截面的角比边远 41%，所以方形板子、芯片不会被当成圆的；
    电机端盖这种圆盘上凸出 4 个螺栓耳朵的（36 格里 12 格偏大），仍然算圆，半径取中位数（手掌贴在圆盘边上，不贴耳朵）。"""
    best = None
    size = [h - l for l, h in zip(lo, hi)]
    for axis in range(3):
        a, b = [i for i in range(3) if i != axis]
        if max(size[a], size[b]) < 1e-3 or abs(size[a] - size[b]) > 0.15 * max(size[a], size[b]):
            continue
        c = [(lo[i] + hi[i]) * 0.5 for i in range(3)]
        bins = [0.0] * ROUND_BINS
        for v in verts:
            da, db = v[a] - c[a], v[b] - c[b]
            k = int((math.atan2(db, da) + math.pi) / (2 * math.pi) * ROUND_BINS) % ROUND_BINS
            bins[k] = max(bins[k], math.hypot(da, db))
        filled = sorted(r for r in bins if r > 0)
        if len(filled) < ROUND_BINS * 0.75:
            continue
        median = filled[len(filled) // 2]
        good = sum(1 for r in bins if abs(r - median) <= 0.08 * median)
        score = good / ROUND_BINS
        if score >= 0.6 and (best is None or score > best[3]):
            best = (axis, c, median, score)
    return best[:3] if best else None


def ray_hits(verts, tris, axis, pa, pb, a, b):
    """沿 axis 方向穿过 (pa, pb) 的直线和三角形的交点坐标（axis 方向上的值）列表。"""
    hits = []
    for i0, i1, i2 in tris:
        p0, p1, p2 = verts[i0], verts[i1], verts[i2]
        # 投影到 (a, b) 平面做二维点在三角形内判断（重心坐标）
        x0, y0 = p0[a], p0[b]
        x1, y1 = p1[a], p1[b]
        x2, y2 = p2[a], p2[b]
        if pa < min(x0, x1, x2) or pa > max(x0, x1, x2) or pb < min(y0, y1, y2) or pb > max(y0, y1, y2):
            continue
        det = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(det) < 1e-12:
            continue
        l0 = ((y1 - y2) * (pa - x2) + (x2 - x1) * (pb - y2)) / det
        l1 = ((y2 - y0) * (pa - x2) + (x0 - x2) * (pb - y2)) / det
        l2 = 1.0 - l0 - l1
        if min(l0, l1, l2) < -1e-6:
            continue
        hits.append(l0 * p0[axis] + l1 * p1[axis] + l2 * p2[axis])
    return hits


def solid_box(verts, tris, lo, hi):
    """从包围盒中心沿 X / Y / Z 各打一条穿透射线，碰到的第一层和最后一层真实三角形围成"实心盒"，返回 (lo, hi)。
    任何一个方向没碰到东西、或者实心盒不到包围盒的一半（圆环、一对天线、绕一圈的绑带、中间空的支架），
    手掌按包围盒贴上去会抓在空气里，返回 None，改用"贴实物表面"。
    拖出去的电线、凸出的插头不在中心线上，不会把盒子撑大（电池、接线盒的手掌贴在盒体上，不贴在电线末端）。"""
    out_lo, out_hi = list(lo), list(hi)
    for axis in range(3):
        a, b = [i for i in range(3) if i != axis]
        hits = ray_hits(verts, tris, axis, (lo[a] + hi[a]) * 0.5, (lo[b] + hi[b]) * 0.5, a, b)
        if len(hits) < 2:
            return None
        out_lo[axis], out_hi[axis] = min(hits), max(hits)
        if out_hi[axis] - out_lo[axis] < 0.5 * (hi[axis] - lo[axis]):
            return None
    return out_lo, out_hi


def resolve_point(point, nodes, bounds, verts):
    """覆盖文件里 pinch 的 point、cylinder 的 center 可以写坐标，也可以写 "top"（最高的顶点）或 "node"（第一个零件节点的原点）。"""
    if point == 'top':
        v = max(verts, key=lambda q: q[2])
        return [round(x, 2) for x in v]
    if point == 'node':
        return [round(x, 2) for x in NODE_ORIGIN[nodes[0]['name']]]
    return point


def grab_spec(unit_id, nodes, bounds, scale, verts):
    """抓点描述（模型坐标，厘米，未缩放）。C++ 的 AToyPartUnit 按 type 选抓法。
    顺序：覆盖文件 > 转子 > 小件捏 > 任意轴回转体握外圆 > 实心块状贴面 > 贴实物表面。"""
    lo, hi = merge([bounds[n['name']] for n in nodes])
    size = [h - l for l, h in zip(lo, hi)]
    for pattern, entry in OVERRIDES.items():
        if fnmatch.fnmatchcase(unit_id, pattern):
            grab = dict(entry['grab'])
            for key in ('point', 'center'):
                if key in grab:
                    grab[key] = resolve_point(grab[key], nodes, bounds, verts)
            grab['override'] = pattern
            return grab
    rotor = CONFIG.get('rotor')
    if unit_id == 'rotor' and rotor:
        shaft = bounds[rotor[0]]
        core = bounds[rotor[1]]
        r_core = (core[1][1] - core[0][1]) * 0.5
        return dict(type='cylinder', segments=[
            [round(shaft[0][0], 2), round(core[0][0], 2), rotor[2]],
            [round(core[0][0], 2), round(core[1][0], 2), round(r_core, 2)],
            [round(core[1][0], 2), round(shaft[1][0], 2), rotor[2]],
        ])
    if max(size) * scale <= PINCH_MAX_CM:
        return dict(type='pinch')
    found = round_axis(verts, lo, hi)
    if found:
        # 回转体（端盖、风罩、涵道、电机钟罩、阀杆……）：手掌贴在外圆上，轴可以是 X / Y / Z，圆心取包围盒中心
        axis, c, r = found
        direction = [0.0, 0.0, 0.0]
        direction[axis] = 1.0
        return dict(type='cylinder', axis=direction, center=[round(x, 2) for x in c],
                    segments=[[round(lo[axis] - c[axis], 2), round(hi[axis] - c[axis], 2), round(r, 2)]])
    # 块状：按单元里第一个零件（"主零件"，比如接线盒的盒座，不是拖得很长的电源线）的实心盒，像方块一样手掌贴一个面
    mlo, mhi = bounds[nodes[0]['name']]
    box = solid_box(VERTS[nodes[0]['name']], TRIS[nodes[0]['name']], mlo, mhi)
    if box:
        blo, bhi = box
        return dict(type='box', center=[round((a + b) * 0.5, 2) for a, b in zip(blo, bhi)],
                    extent=[round((b - a) * 0.5, 2) for a, b in zip(blo, bhi)])
    # 兜底：抓的那一刻从指尖往零件打一条射线，手掌贴在碰到的真实表面上（C++ 里算）
    return dict(type='surface')


VERTS = {}
TRIS = {}
NODE_ORIGIN = {}


def load_overrides():
    """Tools/grab_overrides/<模型名>.json：{单元 id 或通配符: {"grab": {...}, "why": "为什么要手写"}}。"""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'grab_overrides', f'{MODEL}.json')
    if not os.path.exists(path):
        return {}
    with open(path, encoding='utf-8') as f:
        data = json.load(f)
    return {k: v for k, v in data.items() if not k.startswith('_')}


OVERRIDES = load_overrides()


def part_no(node):
    extras = node.get('extras', {})
    return extras.get('part_no', extras.get('no'))


def build_units(gltf, binary, part_nodes):
    """按爆炸路线分组，返回 (零件名 → 单元 id, 单元列表, 缩放倍数)。"""
    bounds = {n['name']: ue_bounds(gltf, n) for n in part_nodes}
    VERTS.clear()
    TRIS.clear()
    for n in part_nodes:
        VERTS[n['name']], TRIS[n['name']] = node_geometry(gltf, binary, n)
        t = n.get('translation', [0.0, 0.0, 0.0])
        NODE_ORIGIN[n['name']] = (t[0] * 100.0, t[2] * 100.0, t[1] * 100.0)
    lo, hi = merge(list(bounds.values()))
    scale = CONFIG.get('length_cm', TARGET_LENGTH_CM) / max(h - l for l, h in zip(lo, hi))
    instances = Counter(part_no(n) for n in part_nodes)

    groups = {}
    for node in part_nodes:
        extras = node.get('extras', {})
        ex = extras.get('ex', '[]')
        if json.loads(ex) and 1 < instances[part_no(node)] <= SPLIT_MAX_INSTANCES:
            key = node['name']
        else:
            key = ex if json.loads(ex) else '[]'
        groups.setdefault(key, []).append(node)

    unit_of, units = {}, []
    for key, nodes in groups.items():
        names = [n['name'] for n in nodes]
        unit_id = 'base' if key == '[]' else next((CONFIG['named'][n] for n in names if n in CONFIG['named']), names[0])
        for n in names:
            unit_of[n] = unit_id
        first = nodes[0].get('extras', {})
        material = first.get('material_spec') or CONFIG.get('materials', {}).get(part_no(nodes[0]), '')
        info = CONFIG['units'].get(unit_id) or dict(name_cn=first.get('name_cn', ''), material=material,
                                                    desc=first.get('desc', ''))
        units.append(dict(
            id=unit_id,
            grabbable=unit_id != 'base',
            name_cn=info['name_cn'],
            material=info['material'],
            desc=info['desc'],
            grab=None if unit_id == 'base' else grab_spec(unit_id, nodes, bounds, scale,
                                                           [v for n in nodes for v in VERTS[n['name']]]),
        ))
    return unit_of, units, scale, [round(h - l, 1) for l, h in zip(lo, hi)]


def main():
    glb = os.environ.get('MODEL_GLB') or os.environ.get('MOTOR_GLB') or (sys.argv[1] if len(sys.argv) > 1 else '')
    if not glb or not os.path.exists(glb):
        raise RuntimeError(f'找不到 glb：{glb!r}（设置环境变量 MODEL_GLB）')

    gltf, binary = read_glb(glb)
    # MODEL_MANIFEST_ONLY=1：资产已经导入过、只想重写 parts.json（比如改了抓点规则），就不重新导入，
    # 免得几十上百个 uasset 全部变成新的二进制、在 git 里白白多出十几 MB
    manifest_only = '1' in (os.environ.get('MODEL_MANIFEST_ONLY'), os.environ.get('MOTOR_MANIFEST_ONLY'))
    if not manifest_only:
        import_glb(glb)
        # glTF 导入默认给每个网格开 Nanite；前向渲染 + SM5 不支持，关掉（原因见 disable_nanite.py）
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from disable_nanite import disable_nanite
        disable_nanite(DEST)
    meshes = static_meshes()
    unreal.log(f'ToyBox: {MODEL} 导入了 {len(meshes)} 个静态网格')

    # 不是零件的节点：CAP 开头的是电机网页"剖开"模式用的剖切盖面，FX_ 开头的是无人机飞行时的桨叶模糊特效盘
    part_nodes = [n for n in gltf.get('nodes', []) if 'mesh' in n and not n.get('name', '').startswith(('CAP', 'FX_'))]
    unit_of, units, scale, size = build_units(gltf, binary, part_nodes)

    parts, missing, used = [], [], set()
    for node in part_nodes:
        name = node['name']
        mesh_name = gltf['meshes'][node['mesh']].get('name', name)
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

    for key, asset in meshes.items():
        if key not in used and key.startswith(('cap', 'fx')):
            unreal.EditorAssetLibrary.delete_asset(asset.get_path_name())

    # 方向检查：几个已知零件的中心应该在模型原点的哪一侧
    by_name = {p['name']: unreal.load_asset(p['mesh']) for p in parts}
    for part_name, want in CONFIG['check'].items():
        mesh = by_name.get(part_name)
        if not mesh:
            continue
        c = center(mesh)
        ok = all((w == 0) or (w * v > 0) for w, v in zip(want, (c.x, c.y, c.z)))
        unreal.log(f'ToyBox: 方向检查 {part_name} 中心 ({c.x:.1f}, {c.y:.1f}, {c.z:.1f}) 应在 {want} 一侧 -> {"OK" if ok else "不对"}')
        if not ok:
            unreal.log_error('ToyBox: 坐标方向和预期不一致，爆炸方向可能是反的')

    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    with open(MANIFEST, 'w', encoding='utf-8') as f:
        model = dict(CONFIG['info'], explode_scale=CONFIG.get('explode_scale', 1.0))
        if 'length_cm' in CONFIG:
            model['length_cm'] = CONFIG['length_cm']
        json.dump(dict(source=os.path.basename(glb), model=model,
                       units=units, parts=parts),
                  f, ensure_ascii=False, indent=1)
    kinds = Counter(u['grab']['type'] if u['grab'] else 'base' for u in units)
    unreal.log(f'ToyBox: 写出 {MANIFEST}，{len(parts)} 个零件，{len(units)} 个抓取单元 {dict(kinds)}，'
               f'原始尺寸 {size} cm，缩放约 {scale:.2f} 倍')
    if missing:
        unreal.log_warning(f'ToyBox: 这些节点没找到对应网格：{missing}')


main()
