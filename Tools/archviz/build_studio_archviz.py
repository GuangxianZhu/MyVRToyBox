"""把 ArchViz Interior 样板间改成我们的 VR 工作室：/Game/ArchViz/Maps/L_StudioArchViz。

ArchViz Interior 是 Epic 的免费示例，只装在本机的 Content/ArchViz 里（.gitignore 挡着，不进公开仓库），
这个脚本只引用 /Game/ArchViz 下的路径，自己不带任何 Epic 的资产。由 生成样板间工作室.bat 调用（命令行模式，-nullrhi）：

  1. 复制 /Game/ArchViz/Maps/ArchVis_Lightmap → L_StudioArchViz（已有就先删掉重来，所以可以反复跑）
  2. 删掉餐桌、餐椅、桌上的摆件和绿植、餐桌上方的两盏吊灯（会垂到模型上），以及其他会和我们桌子（160×70×75cm）重叠的东西
  3. 去掉 WorldSettings 里的 GameMode 覆盖（示例里是 FunctionalTestGameMode），用工程默认的 AToyBoxGameMode
  4. 删掉示例的过场动画（LevelSequenceActor）
  5. 在餐桌的位置摆一个 AToyStudio（bBuildRoom = false，只留桌子、触屏、模型支架和主灯）：
     桌子中心对准餐桌中心，面朝餐边柜和挂画那面墙，窗户在左右两侧；检查站立点周围 60cm 内没有家具；
     按房间内墙算 WalkArea（工作室坐标，往里缩 30cm）
  6. 调低后期体积的曝光补偿（示例是 +1.76，窗外一片白；-1.5 时看窗户的画面过曝 < 3%）
  7. 摆件（PROPS 表里挑的 20 件）换成能拿起来的 AToyProp：网格没有简单碰撞就加盒子 / 凸包，花盆里的植物挂上去跟着走；
     家具、墙这些只有三角面碰撞的网格改成"复杂碰撞当简单碰撞"，摆件才放得住、扔不穿（改的是本机 Content/ArchViz 里的网格）
  8. 装了 Home Interior 的话，前门里侧摆一个 AToyDoorway（走进门口 → 换到 L_HomeStudio，从那边的入户走廊进来）
  9. 存关卡

环境变量 ARCHVIZ_STEP=bake 时不做上面这些，只打开 L_StudioArchViz 用 CPU Lightmass（Production）烘焙光照、存关卡和 BuiltData。
烘焙要渲染（反射捕获、光照贴图编码），所以这一步不能带 -nullrhi，要带 -AllowCommandletRendering。
GPU Lightmass 在这台机器上会崩，所以烘焙走 CPU Lightmass。

ARCHVIZ_STEP=home / home_bake：第二个场景，Fab 上 Olga Chekunova 的 Home Interior（本机 Content/Home_Interior，同样不进仓库）。
复制 Home_Interior → L_HomeStudio，摆一个不带桌子的 AToyStudio（只管出生点、WalkArea 和摆件复位），入户走廊尽头装上和
ArchViz 前门同一套门框 / 门，摆一个 AToyDoorway 连回 L_StudioArchViz，HOME_PROPS 表里的小东西换成 AToyProp，然后同样 CPU 烘焙。
两个场景各自烘焙，穿门是换关卡（黑场 0.3 秒），不是拼成一张图：两套光照（各有太阳、天光、后期）放进一张图会互相打架。
"""
import math
import os
import time

import unreal

SOURCE_MAP = '/Game/ArchViz/Maps/ArchVis_Lightmap'
TARGET_MAP = '/Game/ArchViz/Maps/L_StudioArchViz'

# 和 ToyStudio.cpp 里的常量一致：桌子在工作室坐标 X 40..110、Y -80..80、高 75
TABLE_X = 75.0
TABLE_DEPTH = 70.0
TABLE_WIDTH = 160.0
TABLE_HEIGHT = 75.0

DELETE_FOLDERS = ('DiningRoom/DiningRoom-Table', 'DiningRoom/DiningRoom-Lights', 'Plants/DiningTablePlant')
DELETE_MESH_PREFIXES = ('SM_DiningTable', 'SM_DiningChair')
# 地面、地毯、墙、天花板这些不算"挡路的家具"
STRUCTURE_LABELS = ('SM_Floor', 'SM_Ceiling', 'SM_Wall', 'DiningRoom-Rug', 'LivingRoom-Rug')
STANDING_CLEARANCE = 60.0
WALK_MARGIN = 30.0
EXPOSURE_BIAS = float(os.environ.get('ARCHVIZ_EXPOSURE', '-1.5'))
# 触屏、信息牌这些不受光照的界面跟着曝光走：样板间是固定曝光再减 1.5 档，比自己房间的自动曝光暗，整体再提亮这么多倍
# 7 是量出来的：站位看触屏，屏幕区域平均亮度 34.8（L_Studio 里 33.7），4 只有 23.1。头显里还嫌暗 / 亮，游戏里 toy.UiBrightness <倍数> 现场试
UI_BRIGHTNESS = float(os.environ.get('ARCHVIZ_UI_BRIGHTNESS', '7'))

# 能拿起来的摆件（AToyProp）：(文件夹, 标签, 网格, 工作室坐标 (X, Y, 底面高) 约数, 名字, 材料, 质量 kg, 碰撞, 挂在上面一起走的 Actor)
# 挑的是站在房间里伸手够得到、不超过 35cm、放在台面 / 架子上的：左手边餐边柜、正前方矮柜、边几、茶几、电视柜、梯架。
# 没挑的：边几下层隔板上的碗（提起来会穿过桌面）、茶几上挨着两个花瓶的碗（凸包和花瓶互相重叠，一碰就被挤下桌）、
# 梯架上的陀螺（侧躺着靠边沿立住，一碰就原地转半圈）、梯架中层的白瓷花瓶（放在隔板最外沿，提起来再放下就翻下去）
# 材料写法要让 UToyFeedback::ClassifyMaterial 认得出（钢 / 铜 → 重金属，陶瓷 / 大理石 → 脆性，塑料 → 硬塑料；木、纸、蜡按硬塑料）。
# 碰撞：网格没有简单碰撞时 'box' = 包围盒（书），'hull' = 整个网格顶点的凸包；已经有的不动
PROPS = (
    ('DiningRoom/DiningRoom-SideTable1', 'Hedra1', 'SM_Hedra_02', (-35, -227, 80), '镂空几何摆件', '钢', 0.35, 'hull', ()),
    ('DiningRoom/DiningRoom-SideTable1', 'Book2', 'SM_Book_03', (-35, -230, 78), '白色小书', '纸', 0.4, 'box', ()),
    ('DiningRoom/DiningRoom-SideTable1', 'Hedra2', 'SM_Hedra_03', (48, -229, 73), '木头多面体', '木', 0.6, 'hull', ()),
    ('DiningRoom/DiningRoom-SideTable1', 'Candle1', 'SM_CandleStick', (159, -229, 73), '白蜡烛', '蜡', 0.15, 'hull', ()),
    ('DiningRoom/DiningRoom-SideTable2', 'Vase3', 'SM_Vase_06', (320, -70, 69), '黄铜花瓶', '黄铜', 0.8, 'hull', ()),
    ('Plants/CoffeeTablePlant', 'Pot1', 'SM_Vase_03', (-476, -66, 36), '粗肋草小盆栽', '大理石花盆', 1.2, 'hull',
     ('AglaonemaPlant', 'Dirt')),
    ('Plants/SideTableFlower', 'Vase9', 'SM_Vase_02', (-279, -179, 52), '朱顶红插花', '陶瓷花瓶', 0.5, 'hull',
     ('AmaryllisFlower1', 'AmaryllisFlower2', 'Dirt2')),
    ('LivingRoom/TVStand', 'Vase5', 'SM_Vase_06', (-568, 246, 45), '白色大理石矮花瓶', '大理石', 0.7, 'hull', ()),
    ('LivingRoom/TVStand', 'TV Remote', 'SM_TVRemote', (-457, 248, 45), '电视遥控器', '塑料', 0.12, 'box', ()),
    ('LivingRoom/LadderShelf', 'Candle2', 'SM_Candle', (-151, 254, 60), '香薰蜡烛', '黄铜杯、蜡', 0.4, 'hull', ()),
    ('DiningRoom/DiningRoom-SideTable2', 'Book2', 'SM_Book_03', (329, 38, 73), '灰色硬皮书', '纸', 0.5, 'box', ()),
    ('LivingRoom/LadderShelf', 'Book1', 'SM_Book_03', (-151, 256, 108), '梯架上的小书', '纸', 0.3, 'box', ()),
    ('FrontDoor/FrontDoor-FloorShelf', 'Book6', 'SM_Book_03', (226, 344, 52), '矮书架上的大开本书', '纸', 0.7, 'box', ()),
    ('FrontDoor/FrontDoor-FloorShelf', 'Spring', 'SM_Spring_01', (67, 344, 55), '金属弹簧摆件', '钢', 0.3, 'hull', ()),
    ('FrontDoor/WallShelves/Shelf2', 'MarbleApple', 'SM_Apple', (137, 348, 128), '大理石苹果', '大理石', 1.5, 'hull', ()),
    ('FrontDoor/WallShelves/Shelf1', 'CandleStick1_Holder', 'SM_Vase_06', (173, 352, 101), '黄铜烛台', '黄铜、蜡', 0.3, 'hull',
     ('CandleStick1',)),
    ('LivingRoom/CoffeeTable', 'Book12', 'SM_Book_02', (-440, -69, 36), '茶几上的画册', '纸', 0.8, 'box', ()),
    ('LivingRoom/TVStand', 'Book2', 'SM_Book_02', (-569, 261, 132), '电视柜搁板上的书', '纸', 0.4, 'box', ()),
    ('LivingRoom/TVStand', 'Book1', 'SM_Book_01', (-528, 251, 45), '电视柜上的杂志', '纸', 0.3, 'box', ()),
    ('LivingRoom/TVStand', 'PictureFrame', 'SM_Picture_01', (-488, 268, 128), '小相框', '木框、玻璃', 0.5, 'box', ()),
)
# 只有一个整体凸包、但形状是凹的家具（梯架的隔板、木头多面体放在里面的篮子）：改成用三角面碰撞
CONCAVE_SUPPORTS = ('SM_LadderShelf', 'SM_Basket')

# ---- 第二个场景：Home Interior ----
HOME_SOURCE = '/Game/Home_Interior/Maps/Home_Interior'
HOME_MAP = '/Game/Home_Interior/Maps/L_HomeStudio'
DOOR_NAME = 'Front'
# Home Interior 的入户走廊：在客厅西墙（世界 X ≈ -1607）开的 93cm 门洞，往西 1.5m 到尽头（量 SM_Scene 的碰撞盒得来）
HOME_CORRIDOR_Y = 1681.5
HOME_CORRIDOR_END_X = -1735.0      # 尽头墙面，生成时沿走廊打线再量一次
HOME_FLOOR_Z = 10.0
# 不从门进来、直接打开这张图时的站立点（客厅中间，面朝窗户）和能传送的范围（工作室坐标）
HOME_ORIGIN = (-1250.0, 1700.0)
HOME_WALK = ((-480.0, -315.0), (480.0, 530.0))
# ArchViz 前门那一套（门框里外两层、门、把手），照搬到 Home 的走廊尽头
DOOR_PARTS = ('SM_DoorFrame_Inner', 'SM_DoorFrame_Outer', 'SM_Door', 'SM_DoorHandle')
# 能拿起来的东西：(标签, 网格, 世界坐标 (X, Y, 中心高) 约数, 名字, 材料, 质量 kg, 碰撞, 挂在上面一起走的 Actor)
# Home Interior 的网格都自带凸包。没挑的：碗（凹的，上次在 ArchViz 里出过问题）、樱桃单个太小、竖着排的书（拿走一本旁边的倒）、
# 挤在一起互相碰着的蜡烛 / 盆栽、叠起来的盘子、挂在钩子上的手套和锅铲、两米高的吊柜和置物架顶层
HOME_PROPS = (
    ('SM_Jug2', 'SM_Jug', (-870, 1570, 106), '餐桌上的水壶', '陶瓷', 0.9, 'hull', ()),
    ('SM_Bottle5', 'SM_Bottle', (-872, 1594, 111), '红酒瓶', '玻璃', 1.2, 'hull', ()),
    ('SM_Wneglass_full3', 'SM_Wineglass_full', (-948, 1591, 103), '红酒杯', '玻璃', 0.25, 'hull', ()),
    ('SM_Wneglass_full4', 'SM_Wineglass_full', (-935, 1577, 103), '另一只红酒杯', '玻璃', 0.25, 'hull', ()),
    ('SM_Glass_01', 'SM_Glass_01', (-849, 1575, 100), '玻璃水杯', '玻璃', 0.2, 'hull', ()),
    ('SM_Cup_6', 'SM_Cup_1', (-804, 1607, 100), '餐桌上的马克杯', '陶瓷', 0.3, 'hull', ()),
    ('SM_Dish_2', 'SM_Dish_01', (-897, 1579, 100), '樱桃果盘', '陶瓷', 0.6, 'hull',
     tuple(f'SM_Cherry{i}' for i in range(12, 23))),
    ('SM_Knife', 'SM_Knife', (-934, 1607, 95), '餐刀', '不锈钢', 0.08, 'hull', ()),
    ('SM_Cup_1', 'SM_Cup_1', (-1069, 1964, 117), '岛台上的马克杯', '陶瓷', 0.3, 'hull', ()),
    ('SM_Cup_2', 'SM_Cup_1', (-1132, 1964, 117), '岛台上的另一只马克杯', '陶瓷', 0.3, 'hull', ()),
    ('SM_Pan_13', 'SM_Pan_02', (-1060, 2226, 108), '灶台上的汤锅', '不锈钢', 1.2, 'hull', ()),
    ('SM_Bucket2', 'SM_Bucket', (-1091, 2229, 105), '香草小盆栽', '陶瓷花盆', 0.6, 'hull',
     ('SM_Greenery', 'SM_Greenery2', 'SM_Greenery3', 'SM_Greenery4')),
    ('SM_Vase_01', 'SM_Vase_01', (-1566, 1465, 156), '置物架上的花瓶', '陶瓷', 0.6, 'hull', ()),
    ('SM_Box_2', 'SM_Box_01', (-1562, 1552, 99), '收纳小盒', '木', 0.3, 'hull', ()),
    ('SM_Candle_5', 'SM_Candle_02', (-1286, 1519, 69), '茶几上的蜡烛', '玻璃杯、蜡', 0.4, 'hull', ()),
    ('SM_Vase_5', 'SM_Vase_03', (-1026, 1382, 133), '斗柜上的花瓶', '陶瓷', 0.7, 'hull', ()),
    ('SM_Vase_2', 'SM_Vase_01', (-999, 1375, 133), '斗柜上的细花瓶', '陶瓷', 0.4, 'hull', ()),
    ('SM_Vase_3', 'SM_Vase_01', (-1562, 1429, 159), '置物架上的细高花瓶', '陶瓷', 0.4, 'hull', ()),
    ('SM_Box_3', 'SM_Box_01', (-1562, 1455, 71), '置物架下层的收纳盒', '木', 0.8, 'hull', ()),
    ('SM_Dish_01', 'SM_Dish_01', (-1552, 1449, 149), '置物架上的浅盘', '陶瓷', 0.5, 'hull', ()),
)
# 餐巾薄得只有几毫米，杯子、刀叉压在上面会被它的三角面顶歪：餐巾不要碰撞
NO_COLLISION_MESHES = ('SM_Napkin',)

actors_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def log(msg):
    unreal.log(f'ArchVizStudio: {msg}')


def folder(actor):
    return str(actor.get_folder_path())


def mesh_name(actor):
    if isinstance(actor, unreal.StaticMeshActor):
        mesh = actor.static_mesh_component.static_mesh
        return mesh.get_name() if mesh else ''
    return ''


def bounds(actor):
    origin, extent = actor.get_actor_bounds(False)
    return origin - extent, origin + extent


def fmt(v):
    return f'({v.x:.0f}, {v.y:.0f}, {v.z:.0f})'


def is_structure(actor):
    # 房子本身和房子外面的远景（SM_BackgroundGeometry 有 400m 见方）
    label = actor.get_actor_label()
    _, extent = actor.get_actor_bounds(False)
    return folder(actor) == 'BuildingMeshes' or label.startswith(STRUCTURE_LABELS) or max(extent.x, extent.y) > 400.0


def find_one(pred, what):
    found = [a for a in actors_sub.get_all_level_actors() if pred(a)]
    if not found:
        raise RuntimeError(f'找不到{what}')
    return found[0]


def delete(actor, reason):
    lo, hi = bounds(actor)
    log(f'删除 {actor.get_actor_label()} [{mesh_name(actor) or actor.get_class().get_name()}] {fmt(lo)}..{fmt(hi)}（{reason}）')
    actors_sub.destroy_actor(actor)


def trace_first(world, start, end, accept):
    """沿线找第一个 accept(actor) 的命中（挡在前面的别的东西跳过），返回 (actor, 命中点)。"""
    ignored = []
    for _ in range(20):
        hit = unreal.SystemLibrary.line_trace_single(world, start, end, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, ignored,
                                                     unreal.DrawDebugTrace.NONE, True)
        if hit is None:
            return None, None
        fields = hit.to_tuple()
        actor = next((f for f in fields if isinstance(f, unreal.Actor)), None)
        point = next((f for f in fields if isinstance(f, unreal.Vector)), None)
        if actor is None or point is None:
            return None, None
        if accept(actor):
            return actor, point
        ignored.append(actor)
    return None, None


def bake(map_path):
    level_sub.load_level(map_path)
    log(f'{map_path}：CPU Lightmass（Production）烘焙光照，要几十分钟 ...')
    start = time.time()
    ok = level_sub.build_light_maps(unreal.LightingBuildQuality.QUALITY_PRODUCTION, True)
    log(f'烘焙{"完成" if ok else "失败"}，用时 {(time.time() - start) / 60.0:.1f} 分钟')
    if not ok:
        raise RuntimeError('烘焙失败')
    level_sub.save_current_level()
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log(f'已保存 {map_path} 和 BuiltData')


def copy_map(source, target):
    """source → target（已有就先删掉重来），打开 target。"""
    if unreal.EditorAssetLibrary.does_asset_exist(target):
        level_sub.load_level(source)          # 不能删当前打开的关卡
        unreal.EditorAssetLibrary.delete_asset(target)
        built = target + '_BuiltData'
        if unreal.EditorAssetLibrary.does_asset_exist(built):
            unreal.EditorAssetLibrary.delete_asset(built)
    if not unreal.EditorAssetLibrary.duplicate_asset(source, target):
        raise RuntimeError(f'复制 {source} 失败')
    level_sub.load_level(target)
    log(f'已复制 {source} → {target}，共 {len(actors_sub.get_all_level_actors())} 个 Actor')
    return unreal.EditorLevelLibrary.get_editor_world()


def spawn_doorway(location, yaw, target_map, display_name):
    """门口：Actor 位置 = 从另一边过来时站的地方（地面，面朝屋里），Trigger 盒在它身后 40~110cm 贴着门。"""
    door = actors_sub.spawn_actor_from_class(unreal.ToyDoorway, location, unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    door.set_actor_label(f'Doorway_{DOOR_NAME}')
    door.set_editor_property('door_name', DOOR_NAME)
    door.set_editor_property('target_map', target_map)
    door.set_editor_property('display_name', display_name)
    log(f'门口 {DOOR_NAME}：站位 {fmt(location)} yaw {yaw:.0f}，走进去 → {target_map}')
    return door


def build():
    global origin, world
    # ---- 1. 复制关卡 ----
    world = copy_map(SOURCE_MAP, TARGET_MAP)

    # ---- 2. 找餐桌和餐边柜，算工作室的位置和朝向 ----
    dining = find_one(lambda a: a.get_actor_label() == 'DiningRoomTable' or mesh_name(a) == 'SM_DiningTable', '餐桌')
    sideboard = find_one(lambda a: mesh_name(a) == 'SM_SideTable_02', '餐边柜（SM_SideTable_02）')
    table_lo, table_hi = bounds(dining)
    table_center = (table_lo + table_hi) * 0.5
    to_sideboard = sideboard.get_actor_location() - table_center
    yaw = round(math.degrees(math.atan2(to_sideboard.y, to_sideboard.x)) / 90.0) * 90.0    # 贴着墙的方向取整
    forward = unreal.Vector(math.cos(math.radians(yaw)), math.sin(math.radians(yaw)), 0.0)
    right = unreal.Vector(-forward.y, forward.x, 0.0)
    log(f'餐桌 {fmt(table_lo)}..{fmt(table_hi)}，餐边柜 {fmt(sideboard.get_actor_location())} → 工作室朝向 yaw {yaw:.0f}')


    def to_world(local_x, local_y):
        return unreal.Vector(origin.x + forward.x * local_x + right.x * local_y, origin.y + forward.y * local_x + right.y * local_y, origin.z)


    def to_local(p):
        d = p - origin
        return d.x * forward.x + d.y * forward.y, d.x * right.x + d.y * right.y


    # 桌子中心（工作室坐标 (TABLE_X, 0)）对准餐桌中心
    origin = unreal.Vector(table_center.x - forward.x * TABLE_X, table_center.y - forward.y * TABLE_X, 0.0)
    _, floor_hit = trace_first(world, unreal.Vector(origin.x, origin.y, 200.0), unreal.Vector(origin.x, origin.y, -100.0),
                               lambda a: a.get_actor_label() == 'SM_Floor')
    if floor_hit is None:
        raise RuntimeError('站立点下面没打到 SM_Floor（地面没有碰撞？传送会落不了地）')
    origin.z = floor_hit.z
    log(f'站立点（工作室原点）{fmt(origin)}，地面 SM_Floor 有碰撞，Z = {floor_hit.z:.2f}')

    # ---- 3. 删餐桌一套 + 和桌子重叠的东西 ----
    for actor in actors_sub.get_all_level_actors():
        if folder(actor).startswith(DELETE_FOLDERS) or mesh_name(actor).startswith(DELETE_MESH_PREFIXES):
            delete(actor, '餐桌一套')
    corners = [to_world(TABLE_X + sx * TABLE_DEPTH * 0.5, sy * TABLE_WIDTH * 0.5) for sx in (-1, 1) for sy in (-1, 1)]
    desk_lo = unreal.Vector(min(c.x for c in corners) - 5.0, min(c.y for c in corners) - 5.0, origin.z + 3.0)
    desk_hi = unreal.Vector(max(c.x for c in corners) + 5.0, max(c.y for c in corners) + 5.0, origin.z + TABLE_HEIGHT + 90.0)
    log(f'桌子占的地方（世界坐标，四周留 5cm、往上留 90cm 放模型）{fmt(desk_lo)}..{fmt(desk_hi)}')
    for actor in actors_sub.get_all_level_actors():
        if not isinstance(actor, (unreal.StaticMeshActor, unreal.SkeletalMeshActor)) or is_structure(actor):
            continue
        lo, hi = bounds(actor)
        if lo.x < desk_hi.x and hi.x > desk_lo.x and lo.y < desk_hi.y and hi.y > desk_lo.y and lo.z < desk_hi.z and hi.z > desk_lo.z:
            delete(actor, '和桌子重叠')

    # ---- 4. GameMode、过场动画 ----
    settings = world.get_world_settings()
    log(f'WorldSettings GameMode 覆盖 {settings.get_editor_property("default_game_mode")} → 清空（用工程默认的 AToyBoxGameMode）')
    settings.set_editor_property('default_game_mode', None)
    for actor in actors_sub.get_all_level_actors():
        if isinstance(actor, unreal.LevelSequenceActor):
            delete(actor, '示例过场动画')

    # ---- 5. 站立点周围的家具 ----
    clear = True
    for actor in actors_sub.get_all_level_actors():
        if not isinstance(actor, (unreal.StaticMeshActor, unreal.SkeletalMeshActor)) or is_structure(actor):
            continue
        lo, hi = bounds(actor)
        if lo.z > origin.z + 200.0:
            continue     # 头顶以上的（吊饰、灯）
        dx = max(lo.x - origin.x, 0.0, origin.x - hi.x)
        dy = max(lo.y - origin.y, 0.0, origin.y - hi.y)
        gap = math.hypot(dx, dy)
        if gap < STANDING_CLEARANCE:
            clear = False
            log(f'警告：站立点 {gap:.0f}cm 内有 {actor.get_actor_label()} [{mesh_name(actor)}]')
    if clear:
        log(f'站立点周围 {STANDING_CLEARANCE:.0f}cm 内没有家具')

    # ---- 6. 房间内墙 → WalkArea ----
    # 在家具以上、天花板以下（Z 260）往前后左右打线，只认墙；左右两面墙不一定是直的，所以沿着房间多打几条取最窄的
    def wall_distance(along, offset, sign):
        best = None
        for o in offset:
            start_xy = (o, 0.0) if along == 'y' else (0.0, o)
            start = to_world(*start_xy)
            start.z = origin.z + 260.0
            d = (right if along == 'y' else forward) * (sign * 2000.0)
            actor, point = trace_first(world, start, start + d, lambda a: a.get_actor_label().startswith('SM_Wall'))
            if point is None:
                continue
            lx, ly = to_local(point)
            value = ly if along == 'y' else lx
            log(f'  墙线 {along}{"+" if sign > 0 else "-"} 偏移 {o:.0f} → {actor.get_actor_label()} {value:.0f}')
            best = value if best is None else (min(best, value) if sign > 0 else max(best, value))
        return best


    front = wall_distance('x', (-150.0, 0.0, 150.0), 1)
    back = wall_distance('x', (-150.0, 0.0, 150.0), -1)
    right_wall = wall_distance('y', (-400.0, -200.0, 0.0, 200.0), 1)
    left_wall = wall_distance('y', (-400.0, -200.0, 0.0, 200.0), -1)
    if None in (front, back, right_wall, left_wall):
        raise RuntimeError(f'没打到四面墙：前 {front} 后 {back} 左 {left_wall} 右 {right_wall}')
    walk = unreal.Box(min=unreal.Vector(back + WALK_MARGIN, left_wall + WALK_MARGIN, -50.0),
                      max=unreal.Vector(front - WALK_MARGIN, right_wall - WALK_MARGIN, 50.0))
    log(f'内墙（工作室坐标）前 {front:.0f} 后 {back:.0f} 左 {left_wall:.0f} 右 {right_wall:.0f} → WalkArea X {walk.min.x:.0f}..{walk.max.x:.0f} Y {walk.min.y:.0f}..{walk.max.y:.0f}')

    # 窗户应该在左右两侧
    for actor in actors_sub.get_all_level_actors():
        if mesh_name(actor) == 'SM_Windowframe':
            lx, ly = to_local(actor.get_actor_location())
            side = '前面' if lx > abs(ly) else '后面' if -lx > abs(ly) else '右边' if ly > 0 else '左边'
            log(f'窗户 {actor.get_actor_label()} 在玩家{side}（工作室坐标 {lx:.0f}, {ly:.0f}）')

    # ---- 7. 摆工作室 ----
    studio = actors_sub.spawn_actor_from_class(unreal.ToyStudio, origin, unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    studio.set_actor_label('ToyStudio')
    studio.set_editor_property('build_room', False)
    studio.set_editor_property('walk_area', walk)
    studio.set_editor_property('ui_brightness_scale', UI_BRIGHTNESS)
    log(f'放置 AToyStudio 于 {fmt(origin)} yaw {yaw:.0f}，bBuildRoom = False')

    # ---- 8. 曝光 ----
    for volume in actors_sub.get_all_level_actors():
        if isinstance(volume, unreal.PostProcessVolume):
            pp = volume.settings
            old = pp.get_editor_property('auto_exposure_bias')
            pp.set_editor_property('override_auto_exposure_bias', True)
            pp.set_editor_property('auto_exposure_bias', EXPOSURE_BIAS)
            volume.set_editor_property('settings', pp)
            log(f'{volume.get_actor_label()} 曝光补偿 {old:.2f} → {EXPOSURE_BIAS:.2f}')

    # ---- 9. 摆件 ----
    make_props(PROPS, to_local)

    # ---- 10. 前门 → Home Interior ----
    if unreal.EditorAssetLibrary.does_asset_exist(HOME_SOURCE):
        door = find_one(lambda a: folder(a) == 'FrontDoor' and mesh_name(a) == 'SM_Door', '前门（FrontDoor/SM_Door）')
        door_center, _ = door.get_actor_bounds(False)
        dx, dy = to_local(door_center)
        # 站位在门里侧 120cm，Trigger（站位身后 40~110cm）正好盖住门前那一块；面朝屋里（背对门）
        stand = to_world(dx - 120.0, dy)
        spawn_doorway(stand, yaw + 180.0, HOME_MAP, '样板间前门 → Home Interior 入户走廊')
        log(f'前门中心（工作室坐标）({dx:.0f}, {dy:.0f})；WalkArea Y 到 {walk.max.y:.0f}，门口能走到')
    else:
        log(f'没装 Home Interior（{HOME_SOURCE}），前门不连别的场景')

    level_sub.save_current_level()
    log(f'已保存 {TARGET_MAP}')


def build_home():
    """第二个场景：Home Interior → L_HomeStudio。"""
    # 上次生成的先删掉：要在打开 L_StudioArchViz 之前删，之后再删会被引擎拒绝（"An asset already exists"）
    if unreal.EditorAssetLibrary.does_asset_exist(HOME_MAP):
        level_sub.load_level(HOME_SOURCE)
        for path in (HOME_MAP + '_BuiltData', HOME_MAP):
            if unreal.EditorAssetLibrary.does_asset_exist(path) and not unreal.EditorAssetLibrary.delete_asset(path):
                raise RuntimeError(f'删不掉上次生成的 {path}（生成样板间工作室.bat 会先删掉文件再跑这一步）')
    # ArchViz 前门那一套网格相对"门洞中心、地面、朝屋里"的变换，照搬到这边走廊尽头
    level_sub.load_level(TARGET_MAP)
    door_actors = [a for a in actors_sub.get_all_level_actors()
                   if folder(a) == 'FrontDoor' and mesh_name(a) in DOOR_PARTS]
    if len(door_actors) != len(DOOR_PARTS):
        raise RuntimeError(f'L_StudioArchViz 的前门只找到 {[mesh_name(a) for a in door_actors]}（先跑一遍不带 ARCHVIZ_STEP 的生成）')
    slab = next(a for a in door_actors if mesh_name(a) == 'SM_Door')
    slab_center, slab_extent = slab.get_actor_bounds(False)
    studio = find_one(lambda a: isinstance(a, unreal.ToyStudio), 'L_StudioArchViz 里的 ToyStudio')
    # ArchViz 的前门在工作室 +X 那面墙上，朝屋里 = 工作室朝向转 180°
    arch_anchor = unreal.Transform(unreal.Vector(slab_center.x, slab_center.y, studio.get_actor_location().z),
                                   unreal.Rotator(roll=0.0, pitch=0.0, yaw=studio.get_actor_rotation().yaw + 180.0), unreal.Vector(1, 1, 1))
    door_parts = []
    for a in door_actors:
        rel = a.get_actor_transform().make_relative(arch_anchor)
        door_parts.append((a.get_actor_label(), a.static_mesh_component.static_mesh,
                           [a.static_mesh_component.get_material(i) for i in range(a.static_mesh_component.get_num_materials())], rel))
    door_width = slab_extent.y * 2.0 if abs(math.sin(math.radians(studio.get_actor_rotation().yaw))) < 0.5 else slab_extent.x * 2.0
    log(f'ArchViz 前门：门板宽 {door_width:.0f}cm，{len(door_parts)} 个网格')

    world = copy_map(HOME_SOURCE, HOME_MAP)
    settings = world.get_world_settings()
    settings.set_editor_property('default_game_mode', None)
    for actor in actors_sub.get_all_level_actors():
        if isinstance(actor, (unreal.CineCameraActor, unreal.LevelSequenceActor)):
            delete(actor, '示例相机 / 过场动画')
        elif isinstance(actor, unreal.RectLight) and actor.light_component.mobility == unreal.ComponentMobility.MOVABLE:
            # 吊柜下面的两条可移动面光源：每帧画动态阴影，VR 里不划算，只关阴影（照明不变）
            actor.light_component.set_cast_shadows(False)
            log(f'{actor.get_actor_label()}（可移动面光源）关掉阴影')
        elif mesh_name(actor) in NO_COLLISION_MESHES:
            actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)

    # 墙、地板、家具：大多只有一个整体凸包（整面 L 形墙就是几个盒子，走廊两边的墙没有碰撞），全改成三角面碰撞，
    # 摆件放在架子里、走廊里才挡得住。摆件自己的网格保留凸包（物理模拟只能用简单碰撞）
    prop_meshes = {mesh for _, mesh, *_ in HOME_PROPS}
    changed = {}
    for actor in actors_sub.get_all_level_actors():
        if not isinstance(actor, unreal.StaticMeshActor):
            continue
        sm = actor.static_mesh_component.static_mesh
        if not sm or sm.get_name() in prop_meshes or sm.get_name() == 'SM_Background' or sm.get_path_name() in changed:
            continue
        if unreal.ToyArchVizTools.use_complex_as_simple(sm):
            changed[sm.get_path_name()] = sm
        elif sm.get_editor_property('body_setup').get_editor_property('collision_trace_flag') == unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:
            changed[sm.get_path_name()] = None      # 上次已经改过
    for sm in changed.values():
        if sm:
            unreal.EditorAssetLibrary.save_loaded_asset(sm, False)
    log(f'{len(changed)} 个墙 / 家具网格用三角面做物理碰撞')

    # 地面高度、走廊尽头的墙面：沿走廊打线量（打不到就用表里量好的数）
    _, floor_hit = trace_first(world, unreal.Vector(HOME_ORIGIN[0], HOME_ORIGIN[1], 200.0),
                               unreal.Vector(HOME_ORIGIN[0], HOME_ORIGIN[1], -100.0), lambda a: True)
    floor_z = floor_hit.z if floor_hit else HOME_FLOOR_Z
    _, end_hit = trace_first(world, unreal.Vector(-1650.0, HOME_CORRIDOR_Y, 120.0),
                             unreal.Vector(-1900.0, HOME_CORRIDOR_Y, 120.0), lambda a: True)
    end_x = end_hit.x if end_hit else HOME_CORRIDOR_END_X
    log(f'地面 Z {floor_z:.1f}（{"量的" if floor_hit else "表里的"}），走廊尽头墙面 X {end_x:.0f}（{"量的" if end_hit else "表里的"}）')

    # 走廊尽头装上同一套门：朝屋里 = +X
    home_anchor = unreal.Transform(unreal.Vector(end_x, HOME_CORRIDOR_Y, floor_z), unreal.Rotator(0.0, 0.0, 0.0), unreal.Vector(1, 1, 1))
    for label, sm, materials, rel in door_parts:
        t = rel.multiply(home_anchor) if hasattr(rel, 'multiply') else rel * home_anchor
        a = actors_sub.spawn_actor_from_class(unreal.StaticMeshActor, t.translation, t.rotation.rotator())
        a.set_actor_scale3d(t.scale3d)
        a.static_mesh_component.set_static_mesh(sm)
        for i, m in enumerate(materials):
            a.static_mesh_component.set_material(i, m)
        a.set_actor_label(f'EntryDoor_{label}')
        a.set_folder_path('EntryDoor')
    log(f'走廊尽头装上 ArchViz 前门那一套（{len(door_parts)} 个网格），走廊宽约 93cm，门板 {door_width:.0f}cm')
    spawn_doorway(unreal.Vector(end_x + 120.0, HOME_CORRIDOR_Y, floor_z), 0.0, TARGET_MAP, 'Home Interior 入户门 → 样板间')

    # 工作室：不带桌子，只管出生点、WalkArea、摆件复位
    origin_v = unreal.Vector(HOME_ORIGIN[0], HOME_ORIGIN[1], floor_z)
    studio = actors_sub.spawn_actor_from_class(unreal.ToyStudio, origin_v, unreal.Rotator(0.0, 0.0, 0.0))
    studio.set_actor_label('ToyStudio')
    studio.set_editor_property('build_room', False)
    studio.set_editor_property('with_desk', False)
    studio.set_editor_property('walk_area', unreal.Box(min=unreal.Vector(HOME_WALK[0][0], HOME_WALK[0][1], -50.0),
                                                       max=unreal.Vector(HOME_WALK[1][0], HOME_WALK[1][1], 50.0)))
    studio.set_editor_property('ui_brightness_scale', float(os.environ.get('HOME_UI_BRIGHTNESS', '1')))
    log(f'放置 AToyStudio（不带桌子）于 {fmt(origin_v)}，WalkArea {HOME_WALK}')
    for volume in actors_sub.get_all_level_actors():
        if isinstance(volume, unreal.PostProcessVolume):
            pp = volume.settings
            log(f'{volume.get_actor_label()} 曝光：补偿 {pp.get_editor_property("auto_exposure_bias"):.2f}'
                f'（覆盖 {pp.get_editor_property("override_auto_exposure_bias")}），方式 {pp.get_editor_property("auto_exposure_method")}')

    make_props(HOME_PROPS, lambda p: (p.x, p.y), with_folder=False, fix_supports=False)
    level_sub.save_current_level()
    log(f'已保存 {HOME_MAP}')


def make_props(props, to_local, with_folder=True, fix_supports=True):
    """挑出来的小摆件换成 AToyProp（能拿起来），再给家具、墙加上物理能用的碰撞。
    with_folder = False 时表里没有文件夹那一列（Home Interior 的 Actor 都不在文件夹里，标签本身就不重复）。"""
    actors = [a for a in actors_sub.get_all_level_actors() if isinstance(a, unreal.StaticMeshActor)]
    changed_meshes = {}
    converted = set()
    for index, row in enumerate(props, 1):
        fold, label, mesh, where, name, material, mass, shape, riders = row if with_folder else (None,) + tuple(row)
        found = [a for a in actors if (fold is None or folder(a) == fold) and a.get_actor_label() == label and mesh_name(a) == mesh]
        if len(found) != 1:
            raise RuntimeError(f'摆件 {name}：{fold}/{label} [{mesh}] 找到 {len(found)} 个')
        old = found[0]
        comp = old.static_mesh_component
        sm = comp.static_mesh
        lx, ly = to_local(old.get_actor_location())
        center, extent = old.get_actor_bounds(False)
        drift = math.dist((lx, ly), where[:2])
        if drift > 20.0:
            raise RuntimeError(f'摆件 {name} 离预期位置 {drift:.0f}cm（样板间换了版本？）')
        # 网格加简单碰撞（同一个网格的其他实例也跟着有了碰撞，不影响它们）
        # 我们加的碰撞每次重新算（反复跑结果一样），网格自带的不动
        how = unreal.ToyArchVizTools.ensure_prop_collision(sm, shape == 'box')
        if '加了' in how:
            changed_meshes[sm.get_path_name()] = sm
        # 生成 AToyProp：网格、每个材质槽、位置朝向缩放照搬
        prop = actors_sub.spawn_actor_from_class(unreal.ToyProp, old.get_actor_location(), old.get_actor_rotation())
        prop.set_actor_scale3d(old.get_actor_scale3d())
        prop_mesh = prop.get_editor_property('mesh')
        prop_mesh.set_static_mesh(sm)
        for slot in range(comp.get_num_materials()):
            prop_mesh.set_material(slot, comp.get_material(slot))
        prop.set_editor_property('display_name', name)
        prop.set_editor_property('material_text', material)
        prop.set_editor_property('mass_kg', mass)
        prop.set_actor_label(f'Prop{index:02d}_{label.replace(" ", "")}')
        prop.set_folder_path('Props')
        # 盆里的植物、瓶里的花：改成可移动、关掉碰撞，挂到摆件上跟着走
        rider_names = []
        for rider_label in riders:
            near = [a for a in actors if a.get_actor_label() == rider_label
                    and math.dist((a.get_actor_location().x, a.get_actor_location().y), (center.x, center.y)) < 25.0]
            if len(near) != 1:
                raise RuntimeError(f'摆件 {name} 的 {rider_label} 找到 {len(near)} 个')
            rider = near[0]
            rider.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
            rider.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            rider.attach_to_actor(prop, '', unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD,
                                  unreal.AttachmentRule.KEEP_WORLD, False)
            rider_names.append(f'{rider_label}[{mesh_name(rider)}]')
            converted.add(rider)
        actors_sub.destroy_actor(old)
        size = sorted((extent.x * 2, extent.y * 2, extent.z * 2), reverse=True)
        log(f'摆件 {index:02d} {name}（{material}，{mass}kg）← {fold}/{label} [{mesh}]，{size[0]:.0f}×{size[1]:.0f}×{size[2]:.0f}cm，'
            f'工作室坐标 ({lx:.0f}, {ly:.0f}, {center.z - extent.z:.0f})，碰撞：{how}' + (f'，带上 {"、".join(rider_names)}' if rider_names else ''))

    # 家具、墙、地毯：示例里大多只有复杂碰撞（三角面），物理模拟碰不到，摆件放上去 / 扔过去会穿过去。
    # 没有简单碰撞的都改成"复杂碰撞当简单碰撞"；梯架只有一个整体凸包（斜着的梯子会被包成一个楔子），也改
    actors = [a for a in actors_sub.get_all_level_actors() if isinstance(a, unreal.StaticMeshActor) and a not in converted]
    solid = []
    for actor in actors if fix_supports else ():
        sm = actor.static_mesh_component.static_mesh
        if not sm or sm.get_name() == 'SM_BackgroundGeometry' or sm.get_name() in solid:
            continue
        flag = sm.get_editor_property('body_setup').get_editor_property('collision_trace_flag')
        if flag == unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:
            solid.append(sm.get_name())       # 上次生成已经改过
        elif unreal.ToyArchVizTools.get_simple_collision_count(sm) == 0 or sm.get_name() in CONCAVE_SUPPORTS:
            if unreal.ToyArchVizTools.use_complex_as_simple(sm):
                changed_meshes[sm.get_path_name()] = sm
                solid.append(sm.get_name())
    if fix_supports:
        log(f'{len(solid)} 个家具 / 墙网格用三角面做物理碰撞：{", ".join(sorted(solid))}')
    for sm in changed_meshes.values():
        unreal.EditorAssetLibrary.save_loaded_asset(sm, False)
    log(f'已保存 {len(changed_meshes)} 个改了碰撞的网格（只在本机，不进仓库）')


STEP = os.environ.get('ARCHVIZ_STEP')
if STEP == 'bake':
    bake(TARGET_MAP)
elif STEP == 'home':
    build_home()
elif STEP == 'home_bake':
    bake(HOME_MAP)
else:
    build()
