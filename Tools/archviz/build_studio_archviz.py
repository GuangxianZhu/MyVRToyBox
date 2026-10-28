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
  8. 存关卡

环境变量 ARCHVIZ_STEP=bake 时不做上面这些，只打开 L_StudioArchViz 用 CPU Lightmass（Production）烘焙光照、存关卡和 BuiltData。
烘焙要渲染（反射捕获、光照贴图编码），所以这一步不能带 -nullrhi，要带 -AllowCommandletRendering。
GPU Lightmass 在这台机器上会崩，所以烘焙走 CPU Lightmass。

ARCHVIZ_STEP=home（在 bake 之前跑）：Fab 上 Olga Chekunova 的 Home Interior（本机 Content/Home_Interior，同样不进仓库）接到样板间前门外，
走过去就到，不换关卡：
  1. 复制 Home_Interior → L_StudioArchViz_Home，当成 L_StudioArchViz 的子关卡（一直加载），整套房子平移到前门外：
     Home 入户走廊尽头对着样板间前门，中间 CORRIDOR_LENGTH 长的一段走廊（引擎方块拼的，材质用 Home 的墙面 / 地板）。
     不用转：Home 的窗户朝 +X、背对样板间；样板间的窗户朝 -X / -Y，两边的窗户互相看不到对方的外墙
  2. 只留样板间那一套天（太阳、天空球、高度雾）：Home 的太阳、天光、天空球、大气雾、窗外背景板（从样板间餐厅窗户斜着看得到）都删掉，
     窗外看到的是样板间的城市。Home 原来靠天光从窗户照进来，改成每扇窗里侧两块静态面光源（和样板间窗户的补光一个做法）
  3. 曝光统一：Home 原来是自动曝光、屋里暗，一直顶在 EV100 上限；样板间是固定 EV 5.5、补偿 -1.5。Home 的灯全部改静态，
     亮度乘 2^(差的档数)，后期体积只罩住 Home，曝光和样板间一样固定，色温、泛光还是 Home 自己的。这样触屏 / 信息牌的亮度两边一样
  4. Home 入户走廊尽头的墙上开门洞（复制一份 SM_Scene，用 Geometry Script 减掉一个门洞大小的盒子），装上样板间前门的门框；
     样板间前门的门板、把手和盖住门洞的阻挡体积删掉，门一直开着
  5. HOME_PROPS 表里的 20 件换成 AToyProp（标签 Home01_…），墙和家具改三角面碰撞；ToyStudio 的 ExtraWalkAreas 加上走廊和 Home，
     传送、摇杆走路都能过去；摆件复位管两边一共 40 件
之后 bake 一步把两个关卡一起烘焙（子关卡一直加载，Lightmass 一次算完）。
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
HOME_WING = '/Game/Home_Interior/Maps/L_StudioArchViz_Home'
# Home 原来的打光比样板间的曝光换算出来的还亮一些（截图对比亮 1.5 档左右）：灯再整体调这么多档（2 的次方）
HOME_BRIGHTNESS_STOPS = float(os.environ.get('HOME_BRIGHTNESS_STOPS', '-1.5'))
# Home Interior 的入户走廊：在客厅西墙（世界 X ≈ -1607）开的 93cm 门洞，往西 1.2m 到尽头（量 SM_Scene 的碰撞盒得来）
HOME_CORRIDOR_Y = 1681.5
HOME_CORRIDOR_END_X = -1722.0      # 尽头墙面，生成时沿走廊打线再量一次
HOME_FLOOR_Z = 10.0
# Home 的客厅中间（原来单独一张图时的站立点）和能传送的范围（以它为原点）
HOME_ORIGIN = (-1250.0, 1700.0)
HOME_WALK = ((-480.0, -315.0), (480.0, 530.0))
# 两个门洞之间的走廊（厘米）：长度 = 样板间前墙外皮到 Home 外墙皮
CORRIDOR_LENGTH = float(os.environ.get('ARCHVIZ_CORRIDOR', '200'))
CORRIDOR_WIDTH = 110.0
CORRIDOR_HEIGHT = 250.0
CORRIDOR_FLOOR_MATERIAL = '/Game/Home_Interior/Materials/Wood/Mi_Wood_Laminate_01.Mi_Wood_Laminate_01'
CORRIDOR_WALL_MATERIAL = '/Game/Home_Interior/Materials/Plaster/Mi_Plaster_03.Mi_Plaster_03'
CORRIDOR_LIGHT_CD = float(os.environ.get('ARCHVIZ_CORRIDOR_LIGHT', '100'))
# Home 窗户里侧的补光（每扇窗两块）、Home 的灯乘的倍数（0 = 按两边曝光差自动算）、Home 后期的曝光补偿
HOME_WINDOW_CD = float(os.environ.get('HOME_WINDOW_LIGHT', '850'))
HOME_LIGHT_SCALE = float(os.environ.get('HOME_LIGHT_SCALE', '0'))
HOME_EXPOSURE_BIAS = float(os.environ.get('HOME_EXPOSURE', os.environ.get('ARCHVIZ_EXPOSURE', '-1.5')))
# ArchViz 前门的门框（里外两层），照搬到 Home 那头的门洞；门板、把手不要
FRAME_PARTS = ('SM_DoorFrame_Inner', 'SM_DoorFrame_Outer')
# 能拿起来的东西：(标签, 网格, 世界坐标 (X, Y, 中心高) 约数, 名字, 材料, 质量 kg, 碰撞, 挂在上面一起走的 Actor)
# Home Interior 的网格都自带凸包。没挑的：碗（凹的，上次在 ArchViz 里出过问题）、樱桃单个太小、竖着排的书（拿走一本旁边的倒）、
# 挤在一起互相碰着的蜡烛 / 盆栽、叠起来的盘子、挂在钩子上的手套和锅铲、两米高的吊柜和置物架顶层
# 接到样板间以后去掉的（PropTest 两套房子 40 件一起跑时不稳，原来单独一张图时也是擦线过的）：玻璃水杯（一醒歪 3°）、
# 灶台上的汤锅（一醒在灶架上滑 1~2cm）、置物架上的细高花瓶（松手有时落到上一层隔板）
HOME_PROPS = (
    ('SM_Jug2', 'SM_Jug', (-870, 1570, 106), '餐桌上的水壶', '陶瓷', 0.9, 'hull', ()),
    ('SM_Bottle5', 'SM_Bottle', (-872, 1594, 111), '红酒瓶', '玻璃', 1.2, 'hull', ()),
    ('SM_Wneglass_full3', 'SM_Wineglass_full', (-948, 1591, 103), '红酒杯', '玻璃', 0.25, 'hull', ()),
    ('SM_Wneglass_full4', 'SM_Wineglass_full', (-935, 1577, 103), '另一只红酒杯', '玻璃', 0.25, 'hull', ()),
    ('SM_Cup_6', 'SM_Cup_1', (-804, 1607, 100), '餐桌上的马克杯', '陶瓷', 0.3, 'hull', ()),
    ('SM_Dish_2', 'SM_Dish_01', (-897, 1579, 100), '樱桃果盘', '陶瓷', 0.6, 'hull',
     tuple(f'SM_Cherry{i}' for i in range(12, 23))),
    ('SM_Knife', 'SM_Knife', (-934, 1607, 95), '餐刀', '不锈钢', 0.08, 'hull', ()),
    ('SM_Cup_1', 'SM_Cup_1', (-1069, 1964, 117), '岛台上的马克杯', '陶瓷', 0.3, 'hull', ()),
    ('SM_Cup_2', 'SM_Cup_1', (-1132, 1964, 117), '岛台上的另一只马克杯', '陶瓷', 0.3, 'hull', ()),
    ('SM_Bucket2', 'SM_Bucket', (-1091, 2229, 105), '香草小盆栽', '陶瓷花盆', 0.6, 'hull',
     ('SM_Greenery', 'SM_Greenery2', 'SM_Greenery3', 'SM_Greenery4')),
    ('SM_Vase_01', 'SM_Vase_01', (-1566, 1465, 156), '置物架上的花瓶', '陶瓷', 0.6, 'hull', ()),
    ('SM_Box_2', 'SM_Box_01', (-1562, 1552, 99), '收纳小盒', '木', 0.3, 'hull', ()),
    ('SM_Candle_5', 'SM_Candle_02', (-1286, 1519, 69), '茶几上的蜡烛', '玻璃杯、蜡', 0.4, 'hull', ()),
    ('SM_Vase_5', 'SM_Vase_03', (-1026, 1382, 133), '斗柜上的花瓶', '陶瓷', 0.7, 'hull', ()),
    ('SM_Vase_2', 'SM_Vase_01', (-999, 1375, 133), '斗柜上的细花瓶', '陶瓷', 0.4, 'hull', ()),
    ('SM_Box_3', 'SM_Box_01', (-1562, 1455, 71), '置物架下层的收纳盒', '木', 0.8, 'hull', ()),
    ('SM_Dish_01', 'SM_Dish_01', (-1552, 1449, 149), '置物架上的浅盘', '陶瓷', 0.5, 'hull', ()),
    # 补的 4 件：底面平、平放、不在灶台和隔板边上；叠在上面的东西挂上去一起走（以前分开做摆件时一醒就歪）
    ('SM_Book_13', 'SM_Book_01', (-1271, 1537, 55), '茶几上的两本书', '纸', 0.6, 'hull', ('SM_Book_14',)),
    ('SM_Plate_3', 'SM_Plate_02', (-1342, 1503, 69), '小碟和咖啡杯', '陶瓷', 0.4, 'hull', ('SM_Cup_8',)),
    ('SM_Bucket3', 'SM_Bucket', (-1109, 2229, 104), '另一盆香草', '陶瓷花盆', 0.6, 'hull', ('SM_Greenery5', 'SM_Greenery6', 'SM_Greenery7')),
    ('SM_Bucket4', 'SM_Bucket', (-1126, 2228, 104), '第三盆香草', '陶瓷花盆', 0.6, 'hull', ('SM_Greenery8',)),
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
    # 调灯的时候 ARCHVIZ_BAKE_QUALITY=PREVIEW 先快速看个大概（几分钟），定下来再 Production
    quality = os.environ.get('ARCHVIZ_BAKE_QUALITY', 'PRODUCTION').upper()
    log(f'{map_path}（连同一直加载的子关卡）：CPU Lightmass（{quality}）烘焙光照，要几十分钟 ...')
    start = time.time()
    ok = level_sub.build_light_maps(getattr(unreal.LightingBuildQuality, f'QUALITY_{quality}'), True)
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

    level_sub.save_current_level()
    log(f'已保存 {TARGET_MAP}')


def build_wing():
    """Home Interior 接到样板间前门外：复制成子关卡 L_StudioArchViz_Home，整套房子平移到前门外，中间一段走廊，一起烘焙。"""
    # ---- 1. 样板间这边：前门的位置、门框那一套 ----
    level_sub.load_level(TARGET_MAP)
    studio = find_one(lambda a: isinstance(a, unreal.ToyStudio), 'L_StudioArchViz 里的 ToyStudio（先跑一遍不带 ARCHVIZ_STEP 的生成）')
    lining = find_one(lambda a: folder(a) == 'FrontDoor' and mesh_name(a) == 'SM_DoorFrame_Inner', '前门门框（FrontDoor/SM_DoorFrame_Inner）')
    wall = find_one(lambda a: a.get_actor_label() == 'SM_Wall_Front', '前门那面墙（SM_Wall_Front）')
    lining_center, lining_extent = lining.get_actor_bounds(False)
    wall_lo, wall_hi = bounds(wall)
    hole_width = lining_extent.y * 2.0
    hole_height = lining_center.z + lining_extent.z - studio.get_actor_location().z
    arch_pp = find_one(lambda a: isinstance(a, unreal.PostProcessVolume), '样板间的后期体积')
    arch_ev = arch_pp.settings.get_editor_property('auto_exposure_max_brightness')
    floor_z = studio.get_actor_location().z
    door_y = lining_center.y
    outer_x = wall_hi.x
    # 门框那一套相对"门洞中心、地面、朝屋里（-X）"的变换，照搬到 Home 那头的新门洞（门板、把手不要，门一直开着）
    arch_anchor = unreal.Transform(unreal.Vector(lining_center.x, door_y, floor_z), unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0), unreal.Vector(1, 1, 1))
    # 门框在墙里皮往屋里多少
    frame_inset = wall_lo.x - lining_center.x
    frame_parts = []
    for a in actors_sub.get_all_level_actors():
        if folder(a) == 'FrontDoor' and mesh_name(a) in FRAME_PARTS:
            c = a.static_mesh_component
            frame_parts.append((a.get_actor_label(), c.static_mesh, [c.get_material(i) for i in range(c.get_num_materials())],
                                a.get_actor_transform().make_relative(arch_anchor)))
    log(f'样板间前门：门洞 {hole_width:.0f}×{hole_height:.0f}cm，中心 Y {door_y:.1f}，墙外皮 X {outer_x:.1f}，地面 Z {floor_z:.2f}，'
        f'门框 {len(frame_parts)} 件；样板间曝光 EV {arch_ev:.1f}，补偿 {EXPOSURE_BIAS:+.1f}')

    # ---- 2. Home Interior → 子关卡，量门洞、地面 ----
    world = copy_map(HOME_SOURCE, HOME_WING)
    scene = find_one(lambda a: mesh_name(a) == 'SM_Scene', 'Home Interior 的房子（SM_Scene）')
    scene_lo, scene_hi = bounds(scene)
    _, floor_hit = trace_first(world, unreal.Vector(HOME_ORIGIN[0], HOME_ORIGIN[1], 200.0),
                               unreal.Vector(HOME_ORIGIN[0], HOME_ORIGIN[1], -100.0), lambda a: True)
    home_floor = floor_hit.z if floor_hit else HOME_FLOOR_Z
    _, end_hit = trace_first(world, unreal.Vector(-1650.0, HOME_CORRIDOR_Y, 120.0), unreal.Vector(-1900.0, HOME_CORRIDOR_Y, 120.0), lambda a: True)
    end_x = end_hit.x if end_hit else HOME_CORRIDOR_END_X
    # 平移量：Home 入户走廊尽头的外墙皮离样板间前门外墙皮 CORRIDOR_LENGTH，门洞中心对齐，地面对齐（不转：Home 的窗户朝 +X，背对样板间）
    shift = unreal.Vector(outer_x + CORRIDOR_LENGTH - scene_lo.x, door_y - HOME_CORRIDOR_Y, floor_z - home_floor)
    pp_actor = find_one(lambda a: isinstance(a, unreal.PostProcessVolume), 'Home Interior 的后期体积')
    home_pp = pp_actor.settings
    home_ev = home_pp.get_editor_property('auto_exposure_max_brightness') - home_pp.get_editor_property('auto_exposure_bias')
    # Home 原来是自动曝光，屋里很暗，一直顶在最亮的那头（EV100 上限减补偿）；样板间是固定 EV 减补偿。差几档，Home 的灯就乘 2 的几次方
    light_scale = HOME_LIGHT_SCALE or 2.0 ** ((arch_ev - EXPOSURE_BIAS) - home_ev + HOME_BRIGHTNESS_STOPS)
    log(f'Home Interior：房子 {fmt(scene_lo)}..{fmt(scene_hi)}，地面 Z {home_floor:.1f}，入户走廊尽头墙面 X {end_x:.1f}，整体平移 {fmt(shift)}')

    # ---- 3. 只留一套天：Home 的太阳、天光、天空球、大气雾、窗外背景板和照它的面光源都删掉，窗外看到的是样板间的城市 ----
    for actor in actors_sub.get_all_level_actors():
        cls = actor.get_class().get_name()
        lo, _ = bounds(actor)
        if isinstance(actor, (unreal.CineCameraActor, unreal.LevelSequenceActor)):
            delete(actor, '示例相机 / 过场动画')
        elif isinstance(actor, (unreal.DirectionalLight, unreal.SkyLight, unreal.LightmassPortal, unreal.PlanarReflection)) \
                or cls in ('AtmosphericFog', 'SkyAtmosphere') or cls.startswith('BP_Sky_Sphere'):
            delete(actor, '样板间那套天光 / 太阳已经有了（平面反射每帧多画一遍整个场景，也不要）')
        elif mesh_name(actor) == 'SM_Background':
            delete(actor, '窗外背景板（会从样板间餐厅窗户斜着看到）')
        elif isinstance(actor, unreal.RectLight) and lo.x > scene_hi.x:
            delete(actor, '窗外照背景板的面光源')
        elif mesh_name(actor) in NO_COLLISION_MESHES:
            actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)

    # ---- 4. 灯：全部改成静态（烘焙进光照贴图，运行时不花钱，也不占固定光的 4 个阴影通道），亮度换算到样板间的曝光 ----
    lights = [a for a in actors_sub.get_all_level_actors() if isinstance(a, unreal.Light)]
    for light in lights:
        comp = light.get_component_by_class(unreal.LightComponent)
        comp.set_mobility(unreal.ComponentMobility.STATIC)
        comp.set_editor_property('intensity', comp.get_editor_property('intensity') * light_scale)
    log(f'{len(lights)} 盏灯改成静态，亮度 ×{light_scale:.1f}（Home 原来等效 EV {home_ev:.2f}，样板间 EV {arch_ev - EXPOSURE_BIAS:.2f}，'
        f'再调 {HOME_BRIGHTNESS_STOPS:+.1f} 档）')
    # 原来靠天光从窗户照进来：每扇窗里侧摆两块朝屋里的面光源（和样板间窗户的补光一个做法）
    windows = [a for a in actors_sub.get_all_level_actors() if mesh_name(a) == 'SM_Window']
    for w in windows:
        lo, hi = bounds(w)
        for k in (0.25, 0.75):
            y = lo.y + (hi.y - lo.y) * k
            light = actors_sub.spawn_actor_from_class(unreal.RectLight, unreal.Vector(lo.x - 10.0, y, (lo.z + hi.z) * 0.5),
                                                      unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0))
            c = light.light_component
            c.set_mobility(unreal.ComponentMobility.STATIC)
            c.set_editor_property('source_width', (hi.y - lo.y) * 0.5 - 10.0)
            c.set_editor_property('source_height', hi.z - lo.z - 20.0)
            c.set_editor_property('barn_door_angle', 90.0)
            c.set_editor_property('barn_door_length', 1.0)
            c.set_editor_property('intensity', HOME_WINDOW_CD)
            c.set_editor_property('attenuation_radius', 1200.0)
            c.set_editor_property('use_temperature', True)
            c.set_editor_property('temperature', 6500.0)
            light.set_actor_label(f'WindowFill_{w.get_actor_label()}_{int(k * 4)}')
    log(f'{len(windows)} 扇窗户各加两块静态补光，每块 {HOME_WINDOW_CD:.0f} cd')

    # ---- 5. 墙和家具改三角面碰撞（同 L_HomeStudio），摆件 ----
    prop_meshes = {mesh for _, mesh, *_ in HOME_PROPS}
    changed = {}
    for actor in actors_sub.get_all_level_actors():
        if not isinstance(actor, unreal.StaticMeshActor):
            continue
        sm = actor.static_mesh_component.static_mesh
        if not sm or sm.get_name() in prop_meshes or sm.get_path_name() in changed:
            continue
        if unreal.ToyArchVizTools.use_complex_as_simple(sm):
            changed[sm.get_path_name()] = sm
        elif sm.get_editor_property('body_setup').get_editor_property('collision_trace_flag') == unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:
            changed[sm.get_path_name()] = None
    for sm in changed.values():
        if sm:
            unreal.EditorAssetLibrary.save_loaded_asset(sm, False)
    log(f'{len(changed)} 个墙 / 家具网格用三角面做物理碰撞')
    # 摆件的网格要用凸包（物理模拟不能用三角面）：表里后来加的摆件，网格以前可能被上面这步改成了三角面碰撞，改回来
    for actor in actors_sub.get_all_level_actors():
        sm = actor.static_mesh_component.static_mesh if isinstance(actor, unreal.StaticMeshActor) else None
        if sm and sm.get_name() in prop_meshes:
            body = sm.get_editor_property('body_setup')
            if body.get_editor_property('collision_trace_flag') == unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:
                body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_DEFAULT)
                unreal.EditorAssetLibrary.save_loaded_asset(sm, False)
                log(f'{sm.get_name()} 是摆件的网格，碰撞改回凸包')
    make_props(HOME_PROPS, lambda p: (p.x, p.y), with_folder=False, fix_supports=False, label_prefix='Home')

    # ---- 6. 入户走廊尽头的墙上开门洞：复制一份 SM_Scene，减掉一个门洞大小的盒子 ----
    cut_mesh = cut_door(scene.static_mesh_component.static_mesh, scene.get_actor_transform(),
                        unreal.Vector(scene_lo.x - 10.0, HOME_CORRIDOR_Y - hole_width * 0.5, home_floor + 0.05),
                        unreal.Vector(end_x + 3.0, HOME_CORRIDOR_Y + hole_width * 0.5, home_floor + hole_height))
    scene.static_mesh_component.set_static_mesh(cut_mesh)
    home_anchor = unreal.Transform(unreal.Vector(end_x + frame_inset, HOME_CORRIDOR_Y, home_floor), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0), unreal.Vector(1, 1, 1))
    for label, sm, materials, rel in frame_parts:
        t = rel.multiply(home_anchor) if hasattr(rel, 'multiply') else rel * home_anchor
        a = actors_sub.spawn_actor_from_class(unreal.StaticMeshActor, t.translation, t.rotation.rotator())
        a.set_actor_scale3d(t.scale3d)
        a.static_mesh_component.set_static_mesh(sm)
        for i, m in enumerate(materials):
            a.static_mesh_component.set_material(i, m)
        a.set_actor_label(f'EntryDoor_{label}')
        a.set_folder_path('EntryDoor')
    log(f'入户走廊尽头开了 {hole_width:.0f}×{hole_height:.0f}cm 的门洞，装上样板间前门的门框（{len(frame_parts)} 件）')

    # ---- 7. 后期：Home 自己的色温、泛光，曝光和样板间一样是固定的，盒子只罩住 Home（从走廊中间开始）----
    pp = pp_actor.settings
    pp.set_editor_property('override_auto_exposure_method', True)
    pp.set_editor_property('auto_exposure_method', unreal.AutoExposureMethod.AEM_HISTOGRAM)
    for name in ('auto_exposure_min_brightness', 'auto_exposure_max_brightness'):
        pp.set_editor_property('override_' + name, True)
        pp.set_editor_property(name, arch_ev)
    pp.set_editor_property('override_auto_exposure_bias', True)
    pp.set_editor_property('auto_exposure_bias', HOME_EXPOSURE_BIAS)
    pp_actor.set_editor_property('settings', pp)
    pp_actor.set_editor_property('unbound', False)
    pp_actor.set_editor_property('priority', 1.0)
    pp_actor.set_editor_property('blend_radius', 60.0)

    # ---- 8. 整体平移到样板间前门外 ----
    moved = 0
    for actor in actors_sub.get_all_level_actors():
        if actor.get_attach_parent_actor() is None and not isinstance(actor, unreal.WorldSettings):
            actor.set_actor_location(actor.get_actor_location() + shift, False, True)
            moved += 1
    log(f'平移了 {moved} 个 Actor')
    corridor_mid = outer_x + CORRIDOR_LENGTH * 0.5
    fit_box_x(pp_actor, corridor_mid, None)
    importance = find_one(lambda a: isinstance(a, unreal.LightmassImportanceVolume), 'Home Interior 的 Lightmass 重要体积')
    fit_box_x(importance, outer_x - 30.0, None)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    level_sub.save_current_level()
    log(f'已保存 {HOME_WING}')

    # ---- 9. 样板间：开前门、挂上子关卡、走廊、后期盒子、能走的范围 ----
    level_sub.load_level(TARGET_MAP)
    world = unreal.EditorLevelLibrary.get_editor_world()
    studio = find_one(lambda a: isinstance(a, unreal.ToyStudio), 'ToyStudio')
    for actor in actors_sub.get_all_level_actors():
        if folder(actor) == 'FrontDoor' and mesh_name(actor) in ('SM_Door', 'SM_DoorHandle'):
            delete(actor, '前门门板 / 把手（门一直开着）')
        elif isinstance(actor, unreal.BlockingVolume):
            lo, hi = bounds(actor)
            if lo.x < (lining_center.x + outer_x) * 0.5 < hi.x and lo.y < door_y < hi.y and lo.z < 100.0 < hi.z:
                delete(actor, '前墙的阻挡体积盖住了门洞（墙自己有三角面碰撞）')
        elif isinstance(actor, unreal.ToyDoorway):
            delete(actor, '不用换关卡了')
    # 前墙是一整块实心的墙板，门只是贴在墙上的门框和门板（原来门外什么都没有）：墙上开一个和门框一样大的洞，从门框里一直通到外皮
    wall = find_one(lambda a: a.get_actor_label() == 'SM_Wall_Front', '前门那面墙（SM_Wall_Front）')
    wall.static_mesh_component.set_static_mesh(cut_door(
        wall.static_mesh_component.static_mesh, wall.get_actor_transform(),
        unreal.Vector(wall_lo.x - 3.0, door_y - hole_width * 0.5, floor_z + 0.05),
        unreal.Vector(outer_x + 10.0, door_y + hole_width * 0.5, floor_z + hole_height)))
    # 走廊这些新东西放在样板间（persistent）里，所以子关卡最后再挂
    build_corridor(outer_x, outer_x + CORRIDOR_LENGTH, door_y, floor_z)

    # 后期：样板间的盒子原来伸到 X 990，盖住了 Home，收到走廊中间
    for volume in actors_sub.get_all_level_actors():
        if isinstance(volume, unreal.PostProcessVolume):
            fit_box_x(volume, None, corridor_mid)
    # 体积光照贴图的格子 10cm，房子大了一倍多，内存上限也放宽（不然自动降精度，样板间也跟着糊）
    settings = world.get_world_settings()
    lm = settings.get_editor_property('lightmass_settings')
    lm.set_editor_property('volumetric_lightmap_maximum_brick_memory_mb', 80.0)
    settings.set_editor_property('lightmass_settings', lm)

    # 能传送的范围（工作室坐标）：走廊（含两头门洞）、Home 里面
    st = studio.get_actor_transform()

    def box_local(x0, y0, x1, y1):
        a = st.inverse_transform_location(unreal.Vector(x0, y0, floor_z))
        b = st.inverse_transform_location(unreal.Vector(x1, y1, floor_z))
        return unreal.Box(min=unreal.Vector(min(a.x, b.x), min(a.y, b.y), -50.0), max=unreal.Vector(max(a.x, b.x), max(a.y, b.y), 50.0))

    corridor_box = box_local(lining_center.x - 60.0, door_y - CORRIDOR_WIDTH * 0.5 + 15.0,
                             outer_x + CORRIDOR_LENGTH + 40.0, door_y + CORRIDOR_WIDTH * 0.5 - 15.0)
    home_box = box_local(HOME_ORIGIN[0] + HOME_WALK[0][0] + shift.x, HOME_ORIGIN[1] + HOME_WALK[0][1] + shift.y,
                         HOME_ORIGIN[0] + HOME_WALK[1][0] + shift.x, HOME_ORIGIN[1] + HOME_WALK[1][1] + shift.y)
    studio.set_editor_property('extra_walk_areas', [corridor_box, home_box])
    log(f'能传送的范围加上走廊 X {corridor_box.min.x:.0f}..{corridor_box.max.x:.0f} Y {corridor_box.min.y:.0f}..{corridor_box.max.y:.0f}、'
        f'Home X {home_box.min.x:.0f}..{home_box.max.x:.0f} Y {home_box.min.y:.0f}..{home_box.max.y:.0f}（工作室坐标）')
    streaming = unreal.EditorLevelUtils.add_level_to_world(world, HOME_WING, unreal.LevelStreamingAlwaysLoaded)
    if not streaming:
        raise RuntimeError(f'挂不上子关卡 {HOME_WING}')
    log(f'挂上子关卡 {HOME_WING}（一直加载）')
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    level_sub.save_current_level()
    log(f'已保存 {TARGET_MAP}')


def fit_box_x(volume, min_x, max_x):
    """把体积沿 X 缩放 / 平移到 [min_x, max_x]（None = 那一头不动）。"""
    lo, hi = bounds(volume)
    new_lo = lo.x if min_x is None else min_x
    new_hi = hi.x if max_x is None else max_x
    scale = volume.get_actor_scale3d()
    volume.set_actor_scale3d(unreal.Vector(scale.x * (new_hi - new_lo) / (hi.x - lo.x), scale.y, scale.z))
    lo2, hi2 = bounds(volume)
    loc = volume.get_actor_location()
    volume.set_actor_location(unreal.Vector(loc.x + (new_lo - lo2.x), loc.y, loc.z), False, True)
    lo3, hi3 = bounds(volume)
    log(f'{volume.get_actor_label()} X {lo.x:.0f}..{hi.x:.0f} → {lo3.x:.0f}..{hi3.x:.0f}')


def cut_door(source_mesh, transform, lo, hi):
    """source_mesh 复制成同目录的 <名字>_ToyDoor（本机素材目录，不进仓库），减掉世界坐标 lo..hi 的盒子（盒子转到网格自己的坐标里），重新生成光照贴图 UV。"""
    path = f'{source_mesh.get_path_name().rsplit("/", 1)[0]}/{source_mesh.get_name()}_ToyDoor'
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    copy = unreal.EditorAssetLibrary.duplicate_asset(source_mesh.get_path_name(), path)
    if not copy:
        raise RuntimeError(f'复制 {source_mesh.get_path_name()} 失败')
    target = unreal.DynamicMesh()
    read = unreal.GeometryScriptCopyMeshFromAssetOptions()
    lod = unreal.GeometryScriptMeshReadLOD()
    target, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(copy, target, read, lod)
    if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
        raise RuntimeError(f'读不出 {path} 的网格')
    tris_before = unreal.GeometryScript_MeshQueries.get_num_triangle_i_ds(target)
    tool = unreal.DynamicMesh()
    center = (lo + hi) * 0.5
    # 盒子转到网格自己的坐标里（SM_Scene 转了 -90°）
    local_center = transform.inverse_transform_location(center)
    s = unreal.MathLibrary.inverse_transform_direction(transform, hi - lo)
    size = unreal.Vector(abs(s.x), abs(s.y), abs(s.z))
    unreal.GeometryScript_Primitives.append_box(tool, unreal.GeometryScriptPrimitiveOptions(),
                                                unreal.Transform(local_center, unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0), unreal.Vector(1, 1, 1)),
                                                size.x, size.y, size.z, 0, 0, 0, unreal.GeometryScriptPrimitiveOriginMode.CENTER)
    opts = unreal.GeometryScriptMeshBooleanOptions()
    opts.set_editor_property('fill_holes', True)
    opts.set_editor_property('simplify_output', False)
    identity = unreal.Transform()
    unreal.GeometryScript_MeshBooleans.apply_mesh_boolean(target, identity, tool, identity, unreal.GeometryScriptBooleanOperation.SUBTRACT, opts)
    tris_after = unreal.GeometryScript_MeshQueries.get_num_triangle_i_ds(target)
    write = unreal.GeometryScriptCopyMeshToAssetOptions()
    write.set_editor_property('enable_recompute_tangents', True)
    write.set_editor_property('generate_lightmap_u_vs', unreal.GeometryScriptGenerateLightmapUVOptions.GENERATE_LIGHTMAP_U_VS)
    write.set_editor_property('emit_transaction', False)
    unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(target, copy, write, unreal.GeometryScriptMeshWriteLOD(), True)
    unreal.ToyArchVizTools.use_complex_as_simple(copy)
    unreal.EditorAssetLibrary.save_loaded_asset(copy, False)
    log(f'{path}：减掉门洞 {fmt(lo)}..{fmt(hi)}，三角面 {tris_before} → {tris_after}，重新生成光照贴图 UV')
    return copy


def build_corridor(x0, x1, door_y, floor_z):
    """两个门洞之间的走廊：地板、两面墙、天花板，用引擎的方块拼（自带盒子碰撞和光照贴图 UV），材质用 Home 的墙面 / 地板。"""
    cube = unreal.load_asset('/Engine/BasicShapes/Cube.Cube')
    floor_mat = unreal.load_asset(CORRIDOR_FLOOR_MATERIAL)
    wall_mat = unreal.load_asset(CORRIDOR_WALL_MATERIAL)
    half = CORRIDOR_WIDTH * 0.5
    t = 10.0
    lo_x, hi_x = x0 - 2.0, x1 + 2.0        # 两头各插进墙里 2cm，接缝不漏光
    pieces = []
    # 地板分两段，木纹不至于拉得太长（方块每个面 UV 是 0..1）
    n = max(1, round((hi_x - lo_x) / 100.0))
    for i in range(n):
        a = lo_x + (hi_x - lo_x) * i / n
        b = lo_x + (hi_x - lo_x) * (i + 1) / n
        pieces.append((f'Floor{i + 1}', (a, door_y - half - t), (b, door_y + half + t), (floor_z - t, floor_z), floor_mat))
    pieces.append(('WallLeft', (lo_x, door_y - half - t), (hi_x, door_y - half), (floor_z - t, floor_z + CORRIDOR_HEIGHT + t), wall_mat))
    pieces.append(('WallRight', (lo_x, door_y + half), (hi_x, door_y + half + t), (floor_z - t, floor_z + CORRIDOR_HEIGHT + t), wall_mat))
    pieces.append(('Ceiling', (lo_x, door_y - half - t), (hi_x, door_y + half + t), (floor_z + CORRIDOR_HEIGHT, floor_z + CORRIDOR_HEIGHT + t), wall_mat))
    for label, (ax, ay), (bx, by), (az, bz), mat in pieces:
        center = unreal.Vector((ax + bx) * 0.5, (ay + by) * 0.5, (az + bz) * 0.5)
        a = actors_sub.spawn_actor_from_class(unreal.StaticMeshActor, center, unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
        a.set_actor_scale3d(unreal.Vector((bx - ax) / 100.0, (by - ay) / 100.0, (bz - az) / 100.0))
        c = a.static_mesh_component
        c.set_static_mesh(cube)
        c.set_material(0, mat)
        c.set_editor_property('override_light_map_res', True)
        c.set_editor_property('overridden_light_map_res', 128)
        a.set_actor_label(f'Corridor_{label}')
        a.set_folder_path('Corridor')
    light = actors_sub.spawn_actor_from_class(unreal.RectLight, unreal.Vector((x0 + x1) * 0.5, door_y, floor_z + CORRIDOR_HEIGHT - 2.0),
                                              unreal.Rotator(roll=0.0, pitch=-90.0, yaw=0.0))
    c = light.light_component
    c.set_mobility(unreal.ComponentMobility.STATIC)
    c.set_editor_property('source_width', 40.0)
    c.set_editor_property('source_height', 40.0)
    c.set_editor_property('intensity', CORRIDOR_LIGHT_CD)
    c.set_editor_property('attenuation_radius', 400.0)
    c.set_editor_property('use_temperature', True)
    c.set_editor_property('temperature', 4000.0)
    light.set_actor_label('Corridor_Light')
    light.set_folder_path('Corridor')
    # 走廊也要在 Lightmass 重要体积里
    for v in actors_sub.get_all_level_actors():
        if isinstance(v, unreal.LightmassImportanceVolume):
            fit_box_x(v, None, x1 + 30.0)
    log(f'走廊 X {x0:.0f}..{x1:.0f}，宽 {CORRIDOR_WIDTH:.0f}、高 {CORRIDOR_HEIGHT:.0f}cm，{len(pieces)} 块 + 顶灯 {CORRIDOR_LIGHT_CD:.0f} cd')


def make_props(props, to_local, with_folder=True, fix_supports=True, label_prefix='Prop'):
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
        prop.set_actor_label(f'{label_prefix}{index:02d}_{label.replace(" ", "")}')
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
    build_wing()
else:
    build()
