"""玩家手里的三指抓取器（参考 Red Matter 2 的抓取器，外形原创）：Blender 建模 → claw_tool.glb + rig.json。

跑法（Blender 5.x，或者 pip 装的 bpy 5.2）：
    python build_claw_tool.py [输出目录] [--render]
    blender --background --python build_claw_tool.py -- [输出目录] [--render]
输出：
    claw_tool.glb   每个零件一个节点，顶点在"张开"姿势的位置，原点在世界原点（和 Tools/import_model.py 的约定一样）
    rig.json        三根手指的关节表（虚幻坐标、厘米），AToyHand 按扳机在张开 / 合拢两个姿势之间插值
    --render        另外渲染几张预览图（张开、合拢、侧面），云端没有 UE 时先看外形用

坐标：下面所有尺寸都按"虚幻里手 Actor 的坐标系"写（厘米、右手）：+X = 指向前方（以前手指伸直的方向），
+Y = 手背一侧，+Z = 拇指一侧；原点 = 控制器握把姿态（拳头里握着的那一点）。左手在 C++ 里整体沿 Y 镜像。
虚幻导入 glb 时 (x, y, z)_Blender 变成 (x, -y, z)_UE（米 → 厘米），所以建模时用 ue() 把虚幻坐标换成 Blender 坐标。

零件：
    ClawBody        不动的部分：握柄、握把套、底盖、圆形头、发光环、前端安装环、护手环、三个手指底座
    F{k}S{j}        第 k 根手指（0 = 上，1、2 = 下面两根）的第 j 节（0 = 根节 … 2 = 指尖节），关节轴、橙色关节灯跟着这一节动
"""
import json
import math
import os
import sys

import bpy
import bmesh
from mathutils import Matrix, Vector

# Blender 命令行：blender --background --python build_claw_tool.py -- 输出目录；直接用 python 跑时参数就是 sys.argv[1:]
ARGS = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:]
OUT = next((a for a in ARGS if not a.startswith('--')), os.path.dirname(os.path.abspath(__file__)))
RENDER = '--render' in ARGS

# ------------------------------------------------------------------ 尺寸（虚幻坐标，厘米）
# 整个抓取器按"手套握住它"来摆（Red Matter 那种：工具迁就手，不是手迁就工具）。下面的数都是在游戏里量 MannyXR 手套
# （扳机 0.55、三指 0.95 弯曲时的蒙皮顶点）以后算出来的，见 docs 里的 clawfit 截图：
# 握柄：圆柱贴着掌心、被中指 / 无名指 / 小指包住；食指不握柄，搭在握柄前面的扳机上；拇指压在头顶的面板上。
GRIP_POINT = Vector((0.42, -2.05, -2.10))
GRIP_AXIS = Vector((0.168, -0.052, 0.984)).normalized()
GRIP_CORE_R = 1.33        # 白色芯
GRIP_R = 1.5              # 深蓝握把套（手指包住的表面）
GRIP_S0, GRIP_S1 = -4.3, 3.6
NECK_R, NECK_S1 = 0.8, 5.6

# 三根手指的底座圈（rig 和以前一样，只是整体抬高 1cm，让头底下离食指指节留出空隙）
RIG_CENTER = Vector((4.6, 0.0, 6.0))
COLLAR_X1 = RIG_CENTER.x
HEAD_CENTER = RIG_CENTER   # finger_frame 用它的 z
COLLAR_C = Vector((4.2, 0.0, 6.0))
COLLAR_R = 1.5

# 头：从顶面板（朝玩家眼睛，拇指压着）弯到前面的手指底座圈，像个弯头
PANEL_C = Vector((3.2, -2.2, 6.8))
PANEL_TILT = 25.0         # 面板法线 = 握柄轴往 -X 倒 25°
PANEL_N = (GRIP_AXIS * math.cos(math.radians(PANEL_TILT)) - Vector((1, 0, 0)) * math.sin(math.radians(PANEL_TILT))).normalized()
PANEL_R = 1.8
HEAD_DEPTH = 1.0
THUMB_TARGET = PANEL_C + PANEL_N * 1.4   # 拇指指腹落点（面板上方，手套拇指的厚度）

# 扳机：食指放松（弯 0.5）时离握柄最近的那块手套表面，拨片前面留 1.5mm
TRIGGER_FINGER = Vector((4.04, -2.76, -0.37))
TRIGGER_S = 2.35
TRIGGER_SIZE = (0.8, 1.5, 1.6)   # 厚（朝握柄）、宽、高（顺着握柄）
TRIGGER_LIFT = 0.4               # 往上挪一点，别碰到下面包着握柄的中指

# 手套姿势（AToyHand 用）：中指 / 无名指 / 小指包住握柄的弯曲程度，食指放在扳机上的弯曲程度、扣扳机时多弯一点
GLOVE = dict(finger_curl=0.95, index_curl=0.5, index_pull=0.08, thumb_curl=0.35, thumb_root_limit=30.0)

# 护手环：从头部手背一侧绕过手背（离手套 ≥1cm），落到握柄底部
HOOP_CTRL = [Vector((3.2, 1.3, 6.6)), Vector((1.0, 9.0, 6.0)), Vector((0.8, 9.0, -10.5)), None]

FINGER_BASE_R = 2.0       # 手指根关节离中轴多远
FINGER_ANGLES = [0.0, 120.0, 240.0]   # 绕 X 轴，0 = 正上方（+Z）
SEG_LEN = [3.2, 3.0, 2.6]
SEG_WIDTH = [1.35, 1.2, 1.05]         # 沿关节轴
SEG_THICK = [0.9, 0.8, 0.7]           # 沿手指平面里的法向
# 每节相对上一节的角度（度，正 = 往外张），第 0 节相对 +X
OPEN_REL = [55.0, -15.0, -30.0]
CLOSED_REL = [22.0, -38.0, -28.0]
TIP_RADIUS = 0.45         # 指尖接触扫描时当成的小球半径

WHITE = (0.82, 0.84, 0.87, 1.0)
BLUE = (0.03, 0.09, 0.32, 1.0)
GREY = (0.32, 0.33, 0.36, 1.0)
DARK = (0.03, 0.03, 0.035, 1.0)
ORANGE = (1.0, 0.12, 0.0, 1.0)


def ue(v):
    """虚幻坐标（厘米）→ Blender 坐标（米）。"""
    return Vector((v[0], -v[1], v[2])) * 0.01


def ue_dir(v):
    return Vector((v[0], -v[1], v[2])).normalized()


# ------------------------------------------------------------------ 材质
def material(name, color, emission=0.0, metallic=0.0, rough=0.45):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes['Principled BSDF']
    bsdf.inputs['Base Color'].default_value = color
    bsdf.inputs['Metallic'].default_value = metallic
    bsdf.inputs['Roughness'].default_value = rough
    if emission > 0:
        bsdf.inputs['Emission Color'].default_value = color
        bsdf.inputs['Emission Strength'].default_value = emission
    return mat


MATS = {
    'white': material('M_ClawWhite', WHITE, rough=0.35),
    'blue': material('M_ClawBlue', BLUE, rough=0.6),
    'grey': material('M_ClawMetal', GREY, metallic=0.9, rough=0.3),
    'dark': material('M_ClawRubber', DARK, rough=0.85),
    # 发光强度按虚幻房间亮度定（自动曝光下房间几十尼特，1 倍看着就是平的橙色）
    'glow': material('M_ClawGlow', ORANGE, emission=8.0),
}


# ------------------------------------------------------------------ 几何（全部在 Blender 世界坐标里直接生成）
class Builder:
    """往一个 bmesh 里加各种形状，每个形状带一个材质槽，最后生成一个物体。"""

    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.slots = []

    def slot(self, key):
        if key not in self.slots:
            self.slots.append(key)
        return self.slots.index(key)

    def _tag(self, faces, key, smooth):
        index = self.slot(key)
        for f in faces:
            f.material_index = index
            if smooth is not None:
                f.smooth = smooth

    def _add(self, temp, matrix, key, smooth, bevel=0.0):
        if bevel > 0:
            bmesh.ops.bevel(temp, geom=list(temp.edges), offset=bevel, segments=2, profile=0.5, affect='EDGES')
        temp.normal_update()
        smooth_flags = [f.smooth for f in temp.faces] if smooth is None else None
        temp.transform(matrix)
        temp.normal_update()
        mesh = bpy.data.meshes.new('tmp')
        temp.to_mesh(mesh)
        temp.free()
        before = set(self.bm.faces)
        self.bm.from_mesh(mesh)
        bpy.data.meshes.remove(mesh)
        new_faces = [f for f in self.bm.faces if f not in before]
        self._tag(new_faces, key, smooth)
        if smooth_flags is not None:
            for f, flag in zip(new_faces, smooth_flags):
                f.smooth = flag

    def box(self, center_ue, axes_ue, size_ue, key, bevel=0.15):
        """axes_ue：三个单位向量（虚幻坐标），size_ue：沿三个轴的长度（厘米）。"""
        temp = bmesh.new()
        bmesh.ops.create_cube(temp, size=1.0)
        scale = Matrix.Diagonal((size_ue[0], size_ue[1], size_ue[2], 1.0))
        bevel_unit = bevel / max(min(size_ue), 0.01)
        if bevel > 0:
            # 先缩放再倒角，倒角宽度才是真实厘米
            temp.transform(scale)
            bmesh.ops.bevel(temp, geom=list(temp.edges), offset=bevel, segments=2, profile=0.5, affect='EDGES')
            scale = Matrix.Identity(4)
        del bevel_unit
        self._add(temp, frame_matrix(center_ue, axes_ue) @ scale, key, False)

    def cylinder(self, a_ue, b_ue, r1, r2, key, segments=32, bevel=0.0, smooth=True):
        a, b = Vector(a_ue), Vector(b_ue)
        axis = b - a
        temp = bmesh.new()
        bmesh.ops.create_cone(temp, cap_ends=True, cap_tris=False, segments=segments,
                              radius1=r1, radius2=r2, depth=axis.length)
        if bevel > 0:
            rims = [e for e in temp.edges if abs(e.verts[0].co.z) > axis.length / 2 - 1e-4 and abs(e.verts[1].co.z) > axis.length / 2 - 1e-4]
            bmesh.ops.bevel(temp, geom=rims, offset=bevel, segments=3, profile=0.5, affect='EDGES')
        # 只让侧面平滑，端面保持平的（端面是多边形，平滑着色会出锯齿）
        for f in temp.faces:
            f.smooth = smooth and abs(f.normal.z) < 0.9
        z = axis.normalized()
        x = z.orthogonal().normalized()
        y = z.cross(x)
        self._add(temp, frame_matrix((a + b) / 2, (x, y, z)), key, None)

    def sphere(self, c_ue, r, key, scale=(1, 1, 1), axes=None):
        temp = bmesh.new()
        bmesh.ops.create_uvsphere(temp, u_segments=32, v_segments=16, radius=1.0)
        axes = axes or (Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1)))
        self._add(temp, frame_matrix(c_ue, axes) @ Matrix.Diagonal((r * scale[0], r * scale[1], r * scale[2], 1.0)), key, True)

    def ring(self, center_ue, normal_ue, r_out, r_in, depth, key, segments=48):
        """扁圆环（垫圈形），中心面法线 normal_ue。"""
        temp = bmesh.new()
        n = Vector(normal_ue).normalized()
        x = n.orthogonal().normalized()
        y = n.cross(x)
        outer, inner = [], []
        for zsign in (-0.5, 0.5):
            o, i = [], []
            for s in range(segments):
                ang = 2 * math.pi * s / segments
                d = Vector((math.cos(ang), math.sin(ang), zsign * depth))
                o.append(temp.verts.new((d.x * r_out, d.y * r_out, d.z)))
                i.append(temp.verts.new((d.x * r_in, d.y * r_in, d.z)))
            outer.append(o)
            inner.append(i)
        for s in range(segments):
            t = (s + 1) % segments
            temp.faces.new((outer[0][s], outer[0][t], outer[1][t], outer[1][s]))
            temp.faces.new((inner[0][t], inner[0][s], inner[1][s], inner[1][t]))
            temp.faces.new((outer[1][s], outer[1][t], inner[1][t], inner[1][s]))
            temp.faces.new((outer[0][t], outer[0][s], inner[0][s], inner[0][t]))
        bmesh.ops.recalc_face_normals(temp, faces=temp.faces)
        self._add(temp, frame_matrix(center_ue, (x, y, n)), key, True)

    def sweep(self, points_ue, width, thick, side_ue, key):
        """沿折线扫一条扁带：宽 width（垂直于走向和 side），厚 thick（沿 side 方向）。"""
        temp = bmesh.new()
        side = Vector(side_ue).normalized()
        rings = []
        for i, p in enumerate(points_ue):
            p = Vector(p)
            prev = Vector(points_ue[max(i - 1, 0)])
            nxt = Vector(points_ue[min(i + 1, len(points_ue) - 1)])
            t = (nxt - prev).normalized()
            w = t.cross(side).normalized()
            n = w.cross(t).normalized()
            corners = [p + w * (sx * width / 2) + n * (sy * thick / 2) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
            rings.append([temp.verts.new(c) for c in corners])
        for a, b in zip(rings, rings[1:]):
            for k in range(4):
                temp.faces.new((a[k], a[(k + 1) % 4], b[(k + 1) % 4], b[k]))
        temp.faces.new(list(reversed(rings[0])))
        temp.faces.new(rings[-1])
        bmesh.ops.recalc_face_normals(temp, faces=temp.faces)
        self._add(temp, ue_matrix(), key, True)

    def loft(self, rings_ue, key, segments=48, cap_start=True, cap_end=True):
        """rings_ue: [(centre, axis_a, axis_b, radius)]，相邻圈之间连成管子，两头可以封口。"""
        temp = bmesh.new()
        loops = []
        for c, a, b, r in rings_ue:
            loops.append([temp.verts.new(Vector(c) + (Vector(a) * math.cos(2 * math.pi * k / segments) + Vector(b) * math.sin(2 * math.pi * k / segments)) * r)
                          for k in range(segments)])
        for l0, l1 in zip(loops, loops[1:]):
            for k in range(segments):
                temp.faces.new((l0[k], l0[(k + 1) % segments], l1[(k + 1) % segments], l1[k]))
        if cap_start:
            temp.faces.new(list(reversed(loops[0])))
        if cap_end:
            temp.faces.new(loops[-1])
        bmesh.ops.recalc_face_normals(temp, faces=temp.faces)
        for f in temp.faces:
            f.smooth = len(f.verts) == 4
        self._add(temp, ue_matrix(), key, None)

    def build(self):
        mesh = bpy.data.meshes.new(self.name)
        self.bm.to_mesh(mesh)
        self.bm.free()
        for key in self.slots:
            mesh.materials.append(MATS[key])
        obj = bpy.data.objects.new(self.name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        return obj


def ue_matrix():
    """把虚幻坐标（厘米）的顶点换到 Blender（米）：翻 Y、乘 0.01。"""
    return Matrix.Diagonal((0.01, -0.01, 0.01, 1.0))


def frame_matrix(center_ue, axes_ue):
    """局部坐标 (x, y, z) → 虚幻坐标 center + x·a0 + y·a1 + z·a2，再换到 Blender。"""
    a0, a1, a2 = (Vector(a) for a in axes_ue)
    m = Matrix((
        (a0.x, a1.x, a2.x, center_ue[0]),
        (a0.y, a1.y, a2.y, center_ue[1]),
        (a0.z, a1.z, a2.z, center_ue[2]),
        (0, 0, 0, 1),
    ))
    return ue_matrix() @ m


# ------------------------------------------------------------------ 手指运动学（虚幻坐标；C++ 里同样的算法）
X = Vector((1.0, 0.0, 0.0))


def finger_frame(k):
    a = math.radians(FINGER_ANGLES[k])
    u = Vector((0.0, math.sin(a), math.cos(a)))       # 手指往外张的方向
    t = X.cross(u).normalized()                        # 关节轴
    base = Vector((COLLAR_X1, 0.0, HEAD_CENTER.z)) + u * FINGER_BASE_R
    return base, u, t


def solve(k, rel):
    """返回每节的 (关节点, 方向 d, 法向 n)，再加指尖的末端点。"""
    base, u, _ = finger_frame(k)
    out = []
    p = base.copy()
    phi = 0.0
    for j in range(3):
        phi += math.radians(rel[j])
        d = X * math.cos(phi) + u * math.sin(phi)
        n = -X * math.sin(phi) + u * math.cos(phi)
        out.append((p.copy(), d, n))
        p = p + d * SEG_LEN[j]
    return out, p


def tip_core(k, rel):
    segs, end = solve(k, rel)
    _, d, n = segs[2]
    # 指尖节内侧（朝中轴那一面）的接触点：离末端 0.5cm，往里收半个厚度
    return end - d * 0.5 - n * (SEG_THICK[2] / 2 - TIP_RADIUS * 0.5)


# ------------------------------------------------------------------ 建模
def qrot_between(a, b):
    return Vector(a).normalized().rotation_difference(Vector(b).normalized())


def head_rings(n=12):
    """面板圈（法线 PANEL_N）→ 底座圈（法线 +X），中间按最小旋转把两根基向量带过去，半径中间鼓一点。"""
    a0 = (Vector((1, 0, 0)) - PANEL_N * PANEL_N.x).normalized()
    b0 = PANEL_N.cross(a0)
    back = PANEL_C - PANEL_N * HEAD_DEPTH
    out = []
    for i in range(n):
        t = i / (n - 1)
        nrm = (PANEL_N * (1 - t) + Vector((1, 0, 0)) * t).normalized()
        q = qrot_between(PANEL_N, nrm)
        c = back * (1 - t) + COLLAR_C * t
        r = PANEL_R * (1 - t) + COLLAR_R * t + 0.1 * math.sin(math.pi * t)
        out.append((c, q @ a0, q @ b0, r))
    return out


def trigger_frame():
    foot = GRIP_POINT + GRIP_AXIS * TRIGGER_S
    u = (foot - TRIGGER_FINGER).normalized()          # 从指腹指向握柄
    w = GRIP_AXIS                                      # 扳机长边顺着握柄
    v = u.cross(w).normalized()
    centre = TRIGGER_FINGER + u * (0.15 + TRIGGER_SIZE[0] / 2) + w * TRIGGER_LIFT
    return centre, u, v, w, foot


def build_body():
    b = Builder('ClawBody')
    g, p0 = GRIP_AXIS, GRIP_POINT
    at = lambda s: p0 + g * s
    # 握柄：白芯 + 深蓝握把套 + 三道防滑环；底盖压扁的球 + 橙色尾灯；上面细一点的颈连到头
    b.cylinder(at(GRIP_S0), at(GRIP_S1), GRIP_CORE_R, GRIP_CORE_R, 'white')
    b.cylinder(at(GRIP_S0 + 0.8), at(GRIP_S1 - 0.6), GRIP_R, GRIP_R, 'blue')
    for s in (-2.2, -0.4, 1.4):
        b.cylinder(at(s - 0.12), at(s + 0.12), GRIP_R + 0.06, GRIP_R + 0.06, 'dark')
    ox = g.orthogonal().normalized()
    oy = g.cross(ox)
    b.sphere(at(GRIP_S0), GRIP_CORE_R + 0.3, 'white', scale=(1, 1, 0.6), axes=(ox, oy, g))
    b.ring(at(GRIP_S0 - 0.75), g, 0.9, 0.5, 0.15, 'glow')
    b.cylinder(at(GRIP_S1 - 0.2), at(NECK_S1), NECK_R + 0.25, NECK_R, 'white')

    # 头：面板 → 底座圈的弯头，面板一端封口做成显示面
    rings = head_rings()
    b.loft(rings, 'white')
    c, a, bb, r = rings[0]
    n = PANEL_N
    face = c + n * HEAD_DEPTH          # 面板表面 = PANEL_C
    b.cylinder(face - n * 0.15, face + n * 0.05, PANEL_R - 0.25, PANEL_R - 0.25, 'blue', segments=64)
    b.ring(face + n * 0.08, n, PANEL_R - 0.45, PANEL_R - 0.8, 0.1, 'glow', segments=64)
    b.ring(face + n * 0.08, n, 0.85, 0.6, 0.1, 'glow', segments=32)
    for ang in (40, 160, 280):
        q = face + n * 0.06 + (a * math.cos(math.radians(ang)) + bb * math.sin(math.radians(ang))) * (PANEL_R - 1.3)
        b.cylinder(q, q + n * 0.08, 0.2, 0.2, 'glow', segments=16)
    # 拇指摇杆：面板正中的小黑帽
    b.cylinder(face, face + n * 0.45, 0.5, 0.42, 'dark', segments=24)

    # 前端安装圈（金属）+ 三个手指底座
    b.cylinder(Vector((COLLAR_C.x - 0.4, 0, COLLAR_C.z)), Vector((COLLAR_X1, 0, COLLAR_C.z)), COLLAR_R + 0.1, COLLAR_R - 0.1, 'grey', segments=48)
    b.cylinder(Vector((COLLAR_X1 - 0.05, 0, COLLAR_C.z)), Vector((COLLAR_X1 + 0.15, 0, COLLAR_C.z)), 0.9, 0.9, 'glow', segments=24)
    for k in range(3):
        base, u, t = finger_frame(k)
        b.box(base - X * 0.35 - u * 0.2, (X, u, t), (1.1, 1.5, 1.7), 'white', bevel=0.12)

    # 扳机：深色拨片 + 正面一条橙色灯，背后一根短杆连到握柄
    tc, u, v, w, foot = trigger_frame()
    b.box(tc, (u, v, w), TRIGGER_SIZE, 'dark', bevel=0.18)
    b.box(tc - u * (TRIGGER_SIZE[0] / 2 + 0.01), (u, v, w), (0.05, 0.45, 1.2), 'glow', bevel=0.02)
    b.cylinder(tc + u * (TRIGGER_SIZE[0] / 2 - 0.1), foot + w * TRIGGER_LIFT - u * (GRIP_R - 0.2), 0.3, 0.3, 'grey', segments=16)

    # 护手环：头的手背一侧 → 绕过手背 → 握柄底部
    end = at(GRIP_S0) + Vector((0.0, 0.6, -1.0))
    ctrl = HOOP_CTRL[:3] + [end]
    pts = []
    for i in range(41):
        s = i / 40
        pts.append(ctrl[0] * (1 - s) ** 3 + ctrl[1] * 3 * s * (1 - s) ** 2 + ctrl[2] * 3 * s * s * (1 - s) + ctrl[3] * s ** 3)
    b.sweep(pts, 1.5, 0.5, (0.0, 1.0, 0.0), 'white')
    b.sweep([p - Vector((0, 0.27, 0)) for p in pts[4:37]], 0.35, 0.1, (0.0, 1.0, 0.0), 'glow')
    return b.build()


def build_segment(k, j, segs):
    p, d, n = segs[j]
    _, _, t = finger_frame(k)
    b = Builder(f'F{k}S{j}')
    L, W, T = SEG_LEN[j], SEG_WIDTH[j], SEG_THICK[j]
    # 主体：从关节往前一节长；外侧白色、内侧（朝中轴）一条深蓝
    body_len = L - (0.15 if j < 2 else 0.0)
    b.box(p + d * (body_len / 2), (d, n, t), (body_len, T, W), 'white', bevel=0.14)
    b.box(p + d * (body_len / 2) - n * (T / 2 + 0.02), (d, n, t), (body_len * 0.8, 0.12, W * 0.55), 'blue', bevel=0.03)
    # 关节轴（金属）和两头的橙色灯
    b.cylinder(p - t * (W / 2 + 0.15), p + t * (W / 2 + 0.15), 0.5, 0.5, 'grey', segments=24)
    for s in (-1, 1):
        q = p + t * s * (W / 2 + 0.15)
        b.cylinder(q, q + t * s * 0.06, 0.28, 0.28, 'glow', segments=16)
    if j == 2:
        # 指尖：内侧黑色橡胶垫 + 前端圆头
        b.box(p + d * (L - 0.9) - n * (T / 2 + 0.06), (d, n, t), (1.4, 0.18, W * 0.9), 'dark', bevel=0.05)
        b.sphere(p + d * (L - 0.05), T / 2, 'white', scale=(0.6, 1, W / T))
    return b.build()


def clear_scene():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)


def main():
    clear_scene()
    objects = [build_body()]
    for k in range(3):
        segs, _ = solve(k, OPEN_REL)
        for j in range(3):
            objects.append(build_segment(k, j, segs))

    os.makedirs(OUT, exist_ok=True)
    glb = os.path.join(OUT, 'claw_tool.glb')
    for obj in bpy.data.objects:
        obj.select_set(obj in objects)
    bpy.ops.export_scene.gltf(filepath=glb, export_format='GLB', use_selection=True, export_apply=True,
                              export_yup=True)

    fingers = []
    for k in range(3):
        base, u, t = finger_frame(k)
        fingers.append(dict(
            base=[round(v, 4) for v in base], radial=[round(v, 6) for v in u], axis=[round(v, 6) for v in t],
            segments=[f'F{k}S{j}' for j in range(3)],
        ))
    pinch = sum((tip_core(k, CLOSED_REL) for k in range(3)), Vector()) / 3
    rig = dict(
        note='build_claw_tool.py 生成。虚幻手坐标（厘米，右手；左手整体沿 Y 镜像）。手指在 base 处绕 axis 转，'
             '第 j 节方向 = cos(φj)·X + sin(φj)·radial，φj = 前 j+1 个相对角度之和（度）。网格是 open 姿势。',
        forward=[1.0, 0.0, 0.0],
        lengths=SEG_LEN, thickness=SEG_THICK, width=SEG_WIDTH,
        open=OPEN_REL, closed=CLOSED_REL,
        tip_radius=TIP_RADIUS,
        pinch=[round(v, 3) for v in pinch],
        glove=dict(GLOVE, thumb_target=[round(v, 3) for v in THUMB_TARGET]),
        grip=dict(point=[round(v, 3) for v in GRIP_POINT], axis=[round(v, 5) for v in GRIP_AXIS], radius=GRIP_R),
        trigger=[round(v, 3) for v in trigger_frame()[0]],
        panel=dict(centre=[round(v, 3) for v in PANEL_C], normal=[round(v, 5) for v in PANEL_N]),
        fingers=fingers,
    )
    with open(os.path.join(OUT, 'rig.json'), 'w', encoding='utf-8') as f:
        json.dump(rig, f, ensure_ascii=False, indent=1)
    print(f'claw: wrote {glb} ({len(objects)} parts), pinch at {tuple(round(v, 2) for v in pinch)}')
    print('closed tips:', [tuple(round(v, 2) for v in tip_core(k, CLOSED_REL)) for k in range(3)])

    if RENDER:
        render_previews(objects)


# ------------------------------------------------------------------ 预览图
def pose(objects, rel_open, rel_now):
    """把手指各节摆到 rel_now（网格是 rel_open 姿势）：每节变换 = T(now) · T(open)⁻¹。"""
    by_name = {o.name: o for o in objects}
    for k in range(3):
        _, _, t = finger_frame(k)
        s_open, _ = solve(k, rel_open)
        s_now, _ = solve(k, rel_now)
        for j in range(3):
            def frame(seg):
                p, d, n = seg
                return frame_matrix(p * 1.0, (d, n, t)) @ Matrix.Diagonal((100, 100, 100, 1)) @ Matrix.Identity(4)
            m_open = frame_matrix(s_open[j][0], (s_open[j][1], s_open[j][2], t))
            m_now = frame_matrix(s_now[j][0], (s_now[j][1], s_now[j][2], t))
            by_name[f'F{k}S{j}'].matrix_world = m_now @ m_open.inverted()


def render_previews(objects):
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 48
    scene.cycles.device = 'CPU'
    scene.render.resolution_x = 960
    scene.render.resolution_y = 720
    scene.render.film_transparent = False
    scene.view_settings.view_transform = 'Standard'
    world = bpy.data.worlds.new('W')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.2, 0.22, 0.26, 1)
    world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.4
    scene.world = world
    for name, loc, energy in (('Key', (0.3, -0.4, 0.5), 30), ('Fill', (-0.4, 0.3, 0.2), 12)):
        light = bpy.data.lights.new(name, 'AREA')
        light.energy = energy
        light.size = 0.4
        lo = bpy.data.objects.new(name, light)
        lo.location = loc
        lo.rotation_euler = (Vector((0.05, 0, 0.05)) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
        scene.collection.objects.link(lo)
    cam_data = bpy.data.cameras.new('Cam')
    cam_data.lens = 50
    cam = bpy.data.objects.new('Cam', cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam

    target = ue(Vector((5.5, 0.0, 3.5)))
    views = {
        # 玩家视角：从手后上方看（虚幻：后 -X、上 +Z、稍偏左 -Y）
        'player': ue(Vector((-22.0, -9.0, 20.0))),
        'side': ue(Vector((6.0, -34.0, 6.0))),
        'front': ue(Vector((40.0, -12.0, 10.0))),
    }
    for pose_name, rel in (('open', OPEN_REL), ('closed', CLOSED_REL)):
        pose(objects, OPEN_REL, rel)
        for view, loc in views.items():
            cam.location = loc
            cam.rotation_euler = (target - loc).to_track_quat('-Z', 'Y').to_euler()
            scene.render.filepath = os.path.join(OUT, f'preview_{pose_name}_{view}.png')
            bpy.ops.render.render(write_still=True)
    pose(objects, OPEN_REL, OPEN_REL)


main()
