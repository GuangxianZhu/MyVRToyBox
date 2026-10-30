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
     Home 入户走廊尽头对着样板间前门，中间是一间 HALL_LENGTH × HALL_WIDTH 的门厅（build_hall：层高和样板间一样，地板、墙、天花板用
     Geometry Script 生成、样板间的材质；踢脚线、吸顶灯、门套、餐边柜和挂画、绿植、皮墩、地毯，侧墙上一扇假的不锈钢电梯门）。
     不用转：Home 的窗户朝 +X、背对样板间；样板间的窗户朝 -X / -Y，两边的窗户互相看不到对方的外墙
  2. 只留样板间那一套天（太阳、天空球、高度雾）：Home 的太阳、天光、天空球、大气雾、窗外背景板（从样板间餐厅窗户斜着看得到）都删掉，
     窗外看到的是样板间的城市。Home 原来靠天光从窗户照进来，改成每扇窗里侧两块静态面光源（和样板间窗户的补光一个做法）
  3. 曝光统一：Home 原来是自动曝光、屋里暗，一直顶在 EV100 上限；样板间是固定 EV 5.5、补偿 -1.5。Home 的灯全部改静态，
     亮度乘 2^(差的档数)，后期体积只罩住 Home，曝光和样板间一样固定，色温、泛光还是 Home 自己的。这样触屏 / 信息牌的亮度两边一样
  4. Home 入户走廊尽头的墙上开门洞（复制一份 SM_Scene，用 Geometry Script 减掉一个门洞大小的盒子），装上样板间前门的门框；
     样板间前门的门板、把手和盖住门洞的阻挡体积删掉，门一直开着
  5. HOME_PROPS 表里的 20 件换成 AToyProp（标签 Home01_…），墙和家具改三角面碰撞；ToyStudio 的 ExtraWalkAreas 加上门厅和 Home，
     传送、摇杆走路都能过去
  门框（SM_DoorFrame_Outer / Inner）自带的简单碰撞是盖住整个门洞的盒子，门开着以后会把人挡在门口：改成三角面碰撞。
表里没有的小东西（样板间、Home、门厅三处）由 auto_props 按大小、够不够得着、底下有没有台面自动挑出来，也换成 AToyProp
（标签 AutoA_ / AutoH_ / AutoHall_…，叠在上面的挂上去一起走）；toy.PropTest 测出来不稳的放进 ARCHVIZ_PROP_SKIP / AUTO_PROP_SKIP。
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
# 能传送的范围比内墙往里缩多少：只是挡住墙外的地面，贴墙那一圈站不站得下由传送时的胶囊体检查管（toy.ReachMap 量过：缩 30cm 时墙边 10cm 宽的一圈走得到、传送不了）
WALK_MARGIN = 10.0
# 挂在天花板上的骨骼网格（吊扇、吊饰）
CEILING_HANGERS = ('SK_FanBlades', 'SK_Mobile')
EXPOSURE_BIAS = float(os.environ.get('ARCHVIZ_EXPOSURE', '-1.5'))
# 触屏、信息牌这些不受光照的界面跟着曝光走：样板间是固定曝光再减 1.5 档，比自己房间的自动曝光暗，整体再提亮这么多倍
# 7 是量出来的：站位看触屏，屏幕区域平均亮度 34.8（L_Studio 里 33.7），4 只有 23.1。头显里还嫌暗 / 亮，游戏里 toy.UiBrightness <倍数> 现场试
UI_BRIGHTNESS = float(os.environ.get('ARCHVIZ_UI_BRIGHTNESS', '7'))
# 太阳（固定光）的亮度倍数：示例原来的太阳照进来的那几块地板、窗台过曝
SUN_SCALE = float(os.environ.get('ARCHVIZ_SUN_SCALE', '0.55'))

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
HOME_BRIGHTNESS_STOPS = float(os.environ.get('HOME_BRIGHTNESS_STOPS', '-2.3'))
# Home Interior 的入户走廊：在客厅西墙（世界 X ≈ -1607）开的 93cm 门洞，往西 1.2m 到尽头（量 SM_Scene 的碰撞盒得来）
HOME_CORRIDOR_Y = 1681.5
HOME_CORRIDOR_END_X = -1722.0      # 尽头墙面，生成时沿走廊打线再量一次
HOME_FLOOR_Z = 10.0
# Home 的客厅中间（原来单独一张图时的站立点）和能传送的范围（以它为原点）
HOME_ORIGIN = (-1250.0, 1700.0)
HOME_WALK = ((-500.0, -335.0), (500.0, 550.0))
# 两个门洞之间的门厅（厘米）：长度 = 样板间前墙外皮到 Home 外墙皮，宽度差不多一个房间，层高和样板间一样（生成时往上打线量）
HALL_LENGTH = float(os.environ.get('ARCHVIZ_HALL_LENGTH', '500'))
HALL_WIDTH = float(os.environ.get('ARCHVIZ_HALL_WIDTH', '350'))
# 三盏吸顶灯（静态面光源，烘焙进光照贴图），每盏的亮度
HALL_LIGHT_CD = float(os.environ.get('ARCHVIZ_HALL_LIGHT', '450'))
# 门厅的网格（地板、墙、天花板）是生成的，放在本机素材目录里（不进仓库）
HALL_MESH_DIR = '/Game/ArchViz/ToyHall'
# Home 窗户里侧的补光（每扇窗两块）、Home 的灯乘的倍数（0 = 按两边曝光差自动算）、Home 后期的曝光补偿
HOME_WINDOW_CD = float(os.environ.get('HOME_WINDOW_LIGHT', '500'))
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
# 墙上的开关、插座：凸出墙面一两厘米，摇杆走路的胶囊体贴着门边走会被它们卡住（门口的开关就在门框旁边）。只对 Pawn 不挡
WALL_FIXTURES = ('switch', 'socket', 'outlet', 'thermostat')


def pawn_ignore_fixtures():
    n = 0
    for a in actors_sub.get_all_level_actors():
        if isinstance(a, unreal.StaticMeshActor) and any(k in mesh_name(a).lower() for k in WALL_FIXTURES):
            a.static_mesh_component.set_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN, unreal.CollisionResponseType.ECR_IGNORE)
            n += 1
    log(f'墙上的开关 / 插座 {n} 个不挡走路')
    # 吊扇、吊饰是骨骼网格，物理资产的碰撞体是一大团（吊扇往下到 1.78m、宽 1.7m，吊饰到 1.4m），看不见却挡住传送抛物线
    # （toy.ReachMap 查出来的）。不挡射线（传送、手指），扔过去的摆件照样撞得到
    for a in actors_sub.get_all_level_actors():
        if isinstance(a, unreal.SkeletalMeshActor) and a.skeletal_mesh_component.skeletal_mesh_asset                 and a.skeletal_mesh_component.skeletal_mesh_asset.get_name() in CEILING_HANGERS:
            a.skeletal_mesh_component.set_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY, unreal.CollisionResponseType.ECR_IGNORE)
            log(f'{a.get_actor_label()} 不挡传送抛物线')

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
    pawn_ignore_fixtures()
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
    def wall_distance(along, offset, sign, quiet=False):
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
            if not quiet:
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

    # 左右墙不一定是直的：右边那面墙前半段（X > -120）往外多出 90cm，WalkArea 按最窄处算就把那条 90cm 宽的地方漏掉了（走得到、传送不了）。
    # 沿 X 每 20cm 再量一次，比最窄处宽出 10cm 以上（最多 150cm，防止从门洞穿到别的房间）的那几段各加一个能传送的范围
    def wall_steps(sign, narrowest):
        runs, run = [], None
        x = back + WALK_MARGIN
        while x <= front - WALK_MARGIN:
            d = wall_distance('y', (x,), sign, quiet=True)
            wider = d is not None and 10.0 < (d - narrowest) * sign < 150.0
            # 墙面又往里 / 往外错了一下就另起一段
            if run and (not wider or abs(d - run[2]) > 10.0):
                runs.append(run)
                run = None
            if wider:
                if run is None:
                    run = [x, x, d]
                else:
                    run[1] = x
                    run[2] = min(run[2], d) if sign > 0 else max(run[2], d)
            x += 20.0
        if run:
            runs.append(run)
        boxes = []
        for x0, x1, d in runs:
            y0, y1 = (narrowest - WALK_MARGIN, d - WALK_MARGIN) if sign > 0 else (d + WALK_MARGIN, narrowest + WALK_MARGIN)
            boxes.append(unreal.Box(min=unreal.Vector(max(x0 - 20.0, walk.min.x), y0, -50.0), max=unreal.Vector(min(x1 + 20.0, walk.max.x), y1, 50.0)))
        return boxes

    wall_jogs = wall_steps(1, right_wall) + wall_steps(-1, left_wall)
    for b in wall_jogs:
        log(f'墙往外凸的一段，加一个能传送的范围 X {b.min.x:.0f}..{b.max.x:.0f} Y {b.min.y:.0f}..{b.max.y:.0f}')

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
    studio.set_editor_property('extra_walk_areas', wall_jogs)
    studio.set_editor_property('ui_brightness_scale', UI_BRIGHTNESS)
    log(f'放置 AToyStudio 于 {fmt(origin)} yaw {yaw:.0f}，bBuildRoom = False')

    # ---- 8. 曝光、太阳 ----
    for sun in actors_sub.get_all_level_actors():
        if isinstance(sun, unreal.DirectionalLight) and SUN_SCALE != 1.0:
            c = sun.get_component_by_class(unreal.LightComponent)
            old = c.get_editor_property('intensity')
            c.set_editor_property('intensity', old * SUN_SCALE)
            log(f'太阳 {sun.get_actor_label()} 亮度 {old:.1f} → {old * SUN_SCALE:.1f}')
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
    add_archviz_decor(world, to_world)
    auto_props(world, '样板间', 'AutoA')

    level_sub.save_current_level()
    log(f'已保存 {TARGET_MAP}')


def build_wing():
    """Home Interior 接到样板间前门外：复制成子关卡 L_StudioArchViz_Home，整套房子平移到前门外，中间一段走廊，一起烘焙。"""
    # 门厅里能开合的柜子（Modern House 包）先从它的 Demo 关卡里抄下来
    kits = read_furniture_kits()
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
    # 门框（尤其外圈的门套 SM_DoorFrame_Outer）自带的简单碰撞是一整个盒子，把门洞整个堵住了：原来门一直关着看不出来，
    # 门开了以后摇杆走路的胶囊体在门口被它挡住、传送的落点也站不下。改成用三角面碰撞，门洞才是空的
    for sm in {part[1].get_path_name(): part[1] for part in frame_parts}.values():
        if unreal.ToyArchVizTools.use_complex_as_simple(sm):
            unreal.EditorAssetLibrary.save_loaded_asset(sm, False)
            log(f'{sm.get_name()} 的简单碰撞是盖住整个门洞的盒子 → 改成三角面碰撞')
    # 门厅那面的门套：外圈门套相对"墙里皮、门洞中心、朝屋里"的变换，照搬到门厅一侧
    face_anchor = unreal.Transform(unreal.Vector(wall_lo.x, door_y, floor_z), unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0), unreal.Vector(1, 1, 1))
    casing_parts = [(a.get_actor_label(), a.static_mesh_component.static_mesh,
                     [a.static_mesh_component.get_material(i) for i in range(a.static_mesh_component.get_num_materials())],
                     a.get_actor_transform().make_relative(face_anchor))
                    for a in actors_sub.get_all_level_actors() if folder(a) == 'FrontDoor' and mesh_name(a) == 'SM_DoorFrame_Outer']
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
    # 平移量：Home 入户走廊尽头的外墙皮离样板间前门外墙皮 HALL_LENGTH，门洞中心对齐，地面对齐（不转：Home 的窗户朝 +X，背对样板间）
    shift = unreal.Vector(outer_x + HALL_LENGTH - scene_lo.x, door_y - HOME_CORRIDOR_Y, floor_z - home_floor)
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
    pawn_ignore_fixtures()

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
    auto_props(world, 'Home Interior', 'AutoH', floor_z=home_floor)

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
    # Home 的后期从 Home 的门开始（门厅算样板间那边的风格）
    hall_end = outer_x + HALL_LENGTH
    fit_box_x(pp_actor, hall_end, None)
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
    # 门厅这些新东西放在样板间（persistent）里，所以子关卡最后再挂
    hall = build_hall(world, outer_x, outer_x + HALL_LENGTH, door_y, floor_z, hole_width, hole_height, casing_parts)
    hall_ex = (hall['lo'].x + hall['hi'].x) * 0.5
    place_furniture_kits(world, kits, hall, hall_ex, hall['hi'].y - 1.5, floor_z)
    add_hall_decor(world, hall_ex, hall['lo'].y, hall['hi'].y)
    place_ub_nightstand(world, hall['hi'].x - 75.0, hall['lo'].y + 1.5, -1, floor_z)
    nx = hall['hi'].x - 75.0
    add_pack_smalls(world, [
        (f'{UB}/ArchViz/MokaPot/SM_MokaPot', hall_ex + 28.0, hall['lo'].y + 18.0, 30.0),
        (f'{UB}/ArchViz/CoffeeGrinder/SM_CoffeeGrinder', hall_ex + 52.0, hall['lo'].y + 16.0, -20.0),
        (f'{UB}/ArchViz/ForestInAJar/SM_ForestInAJar', nx - 12.0, hall['lo'].y + 20.0, 0.0),
        (f'{MILRADIO}/SM_Telephone', nx + 12.0, hall['lo'].y + 18.0, 200.0),
    ], 'Hall')
    auto_props(world, '门厅', 'AutoHall', lambda a: folder(a).startswith('Hall'))

    # 后期：样板间的盒子原来伸到 X 990，盖住了 Home，收到走廊中间
    for volume in actors_sub.get_all_level_actors():
        if isinstance(volume, unreal.PostProcessVolume):
            fit_box_x(volume, None, hall_end)
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

    # 两个门洞（各往两边多伸一点，和屋里、门厅的范围接上）、门厅里面（离墙 25cm）、Home 里面
    door_half = hole_width * 0.5 - 15.0
    areas = [('样板间前门', box_local(lining_center.x - 60.0, door_y - door_half, outer_x + 40.0, door_y + door_half)),
             ('门厅', box_local(hall['lo'].x + WALK_MARGIN, hall['lo'].y + WALK_MARGIN, hall['hi'].x - WALK_MARGIN, hall['hi'].y - WALK_MARGIN)),
             ('Home 入户门', box_local(hall_end - 40.0, door_y - door_half, hall_end + 80.0, door_y + door_half)),
             ('Home', box_local(HOME_ORIGIN[0] + HOME_WALK[0][0] + shift.x, HOME_ORIGIN[1] + HOME_WALK[0][1] + shift.y,
                                HOME_ORIGIN[0] + HOME_WALK[1][0] + shift.x, HOME_ORIGIN[1] + HOME_WALK[1][1] + shift.y))]
    # 生成样板间时加的（墙往外凸的那几段，都在样板间里）留着，以前这一步加的去掉再加
    walk = studio.get_editor_property('walk_area')
    keep = [b for b in studio.get_editor_property('extra_walk_areas') if b.max.x <= walk.max.x + 1.0]
    studio.set_editor_property('extra_walk_areas', keep + [box for _, box in areas])
    log('能传送的范围加上 ' + '、'.join(f'{name} X {b.min.x:.0f}..{b.max.x:.0f} Y {b.min.y:.0f}..{b.max.y:.0f}' for name, b in areas) + '（工作室坐标）')
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


def transform_mul(a, b):
    return a.multiply(b) if hasattr(a, 'multiply') else a * b


def materials_of(comp):
    return [comp.get_material(i) for i in range(comp.get_num_materials())]


def find_mesh_component(mesh, min_height=0.0):
    """关卡里第一个用这个网格的组件（静态网格 Actor、摆件都算），返回 (Actor, 组件)；材质从它身上抄（素材里网格自带的材质很多是灰格子）。"""
    for a in actors_sub.get_all_level_actors():
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            if c.static_mesh and c.static_mesh.get_name() == mesh:
                _, e = a.get_actor_bounds(False)
                if e.z * 2.0 >= min_height:
                    return a, c
    return None, None


def back_direction(world, actor):
    """贴着墙放的家具 / 挂画：哪一面朝着墙（往前后左右打线，最近的墙那一面），返回单位向量；四面都没墙返回 None。"""
    o, e = actor.get_actor_bounds(False)
    best = None
    for d in (unreal.Vector(1, 0, 0), unreal.Vector(-1, 0, 0), unreal.Vector(0, 1, 0), unreal.Vector(0, -1, 0)):
        reach = e.x if d.x else e.y
        _, p = trace_first(world, o, o + d * (reach + 80.0), lambda a: a.get_actor_label().startswith(('SM_Wall', 'SM_Scene')))
        if p is not None:
            gap = (p - o).length() - reach
            if best is None or gap < best[0]:
                best = (gap, d)
    return best[1] if best else None


def spawn_mesh(sm, materials, label, rotation, scale, x, y, z_bottom, folder_path='Hall'):
    """摆一个静态网格：包围盒的底面中心放到 (x, y, z_bottom)。materials 为 None 用网格自带的。"""
    a = actors_sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, z_bottom), rotation)
    a.set_actor_scale3d(scale)
    c = a.static_mesh_component
    c.set_static_mesh(sm)
    for i, m in enumerate(materials or ()):
        c.set_material(i, m)
    lo, hi = bounds(a)
    a.set_actor_location(a.get_actor_location() + unreal.Vector(x - (lo.x + hi.x) * 0.5, y - (lo.y + hi.y) * 0.5, z_bottom - lo.z), False, True)
    a.set_actor_label(label)
    a.set_folder_path(folder_path)
    return a


def copy_against_wall(world, mesh, label, x, wall_y, toward, z_bottom, gap=0.5, folder_path='Hall'):
    """把关卡里的某件家具 / 挂画（材质照抄）复制一份，背面贴到门厅的墙上：wall_y 是墙面，toward = +1 / -1 是墙在哪一边（Y 方向）。"""
    src, comp = find_mesh_component(mesh)
    if not src:
        log(f'警告：关卡里没有 {mesh}，门厅里不摆')
        return None
    d = back_direction(world, src) or unreal.Vector(1, 0, 0)
    r = src.get_actor_rotation()
    yaw = r.yaw + math.degrees(math.atan2(toward, 0.0) - math.atan2(d.y, d.x))
    a = spawn_mesh(comp.static_mesh, materials_of(comp), label, unreal.Rotator(roll=r.roll, pitch=r.pitch, yaw=yaw),
                   src.get_actor_scale3d(), x, wall_y, z_bottom, folder_path)
    lo, hi = bounds(a)
    face = hi.y if toward > 0 else lo.y
    a.set_actor_location(a.get_actor_location() + unreal.Vector(0.0, wall_y - toward * gap - face, 0.0), False, True)
    lo, hi = bounds(a)
    log(f'门厅：{label} ← 关卡里的 {src.get_actor_label()} [{mesh}]（原来背面朝 {fmt(d)} 的墙）{fmt(lo)}..{fmt(hi)}')
    return a


def box_actor(label, lo, hi, material, folder_path='Hall', collide=True):
    """引擎方块拼的小件（踢脚线、电梯门这些），世界坐标 lo..hi。"""
    cube = unreal.load_asset('/Engine/BasicShapes/Cube.Cube')
    a = actors_sub.spawn_actor_from_class(unreal.StaticMeshActor, (lo + hi) * 0.5, unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    a.set_actor_scale3d((hi - lo) * 0.01)
    c = a.static_mesh_component
    c.set_static_mesh(cube)
    c.set_material(0, material)
    if not collide:
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    a.set_actor_label(label)
    a.set_folder_path(folder_path)
    return a


def hall_mesh(name, size, uv_cm, hole=None):
    """门厅的一块地板 / 墙 / 天花板：size 大小、中心在原点的盒子，hole = (中心, 大小) 时减掉一个门洞。
    UV 按每 uv_cm 厘米一格做盒子投影（引擎方块每个面都是 0..1，3~5 米的墙会把木纹、墙纹拉得很长），重新生成光照贴图 UV，三角面碰撞。
    网格存在本机素材目录 HALL_MESH_DIR（不进仓库）。"""
    path = f'{HALL_MESH_DIR}/SM_Hall_{name}'
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    asset = unreal.EditorAssetLibrary.duplicate_asset('/Engine/BasicShapes/Cube', path)
    if not asset:
        raise RuntimeError(f'建不了 {path}')
    mesh = unreal.DynamicMesh()
    unreal.GeometryScript_Primitives.append_box(mesh, unreal.GeometryScriptPrimitiveOptions(), unreal.Transform(), size.x, size.y, size.z,
                                                0, 0, 0, unreal.GeometryScriptPrimitiveOriginMode.CENTER)
    if hole:
        tool = unreal.DynamicMesh()
        unreal.GeometryScript_Primitives.append_box(tool, unreal.GeometryScriptPrimitiveOptions(),
                                                    unreal.Transform(hole[0], unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0), unreal.Vector(1, 1, 1)),
                                                    hole[1].x, hole[1].y, hole[1].z, 0, 0, 0, unreal.GeometryScriptPrimitiveOriginMode.CENTER)
        opts = unreal.GeometryScriptMeshBooleanOptions()
        opts.set_editor_property('fill_holes', True)
        opts.set_editor_property('simplify_output', False)
        unreal.GeometryScript_MeshBooleans.apply_mesh_boolean(mesh, unreal.Transform(), tool, unreal.Transform(),
                                                              unreal.GeometryScriptBooleanOperation.SUBTRACT, opts)
    unreal.GeometryScript_UVs.set_mesh_u_vs_from_box_projection(
        mesh, 0, unreal.Transform(unreal.Vector(0, 0, 0), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0), unreal.Vector(uv_cm, uv_cm, uv_cm)),
        unreal.GeometryScriptMeshSelection(), 2)
    write = unreal.GeometryScriptCopyMeshToAssetOptions()
    write.set_editor_property('enable_recompute_tangents', True)
    write.set_editor_property('generate_lightmap_u_vs', unreal.GeometryScriptGenerateLightmapUVOptions.GENERATE_LIGHTMAP_U_VS)
    write.set_editor_property('emit_transaction', False)
    unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(mesh, asset, write, unreal.GeometryScriptMeshWriteLOD(), True)
    unreal.ToyArchVizTools.use_complex_as_simple(asset)
    unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
    return asset


def build_hall(world, x0, x1, door_y, floor_z, hole_width, hole_height, casing_parts):
    """样板间前门和 Home 入户门之间的门厅：差不多一个房间宽，层高和样板间一样。墙、地板、天花板用样板间的材质，
    踢脚线、吸顶灯（静态面光源）、两边门洞的门套、餐边柜和挂画、两盆绿植（一盆 1.3m 左右的高的）、两个皮墩、地毯，
    侧墙上一扇假的不锈钢电梯门（双开拉丝门、召唤按钮、楼层显示，不能交互，挡路、不能传送进去）。"""
    def mat(path):
        m = unreal.load_asset(path)
        if not m:
            raise RuntimeError(f'没有材质 {path}')
        return m

    V = unreal.Vector
    # 层高：样板间门口往上打线量天花板
    _, ceil = trace_first(world, V(x0 - 80.0, door_y, floor_z + 100.0), V(x0 - 80.0, door_y, floor_z + 800.0), lambda a: True)
    height = (ceil.z - floor_z) if ceil else 300.0
    yl, yr = door_y - HALL_WIDTH * 0.5, door_y + HALL_WIDTH * 0.5
    mid = (x0 + x1) * 0.5
    # 先看看门厅这块地方原来有没有东西（不能和两边房子穿插）
    hits = set()
    for z in (20.0, 120.0, height - 20.0):
        for y in (yl + 5.0, door_y, yr - 5.0):
            a, _ = trace_first(world, V(x0 + 5.0, y, floor_z + z), V(x1 - 5.0, y, floor_z + z), lambda a: True)
            if a:
                hits.add(a.get_actor_label())
    if hits:
        log(f'警告：门厅范围里原来就有东西：{", ".join(sorted(hits))}')
    log(f'门厅 X {x0:.0f}..{x1:.0f}（长 {x1 - x0:.0f}）、Y {yl:.0f}..{yr:.0f}（宽 {HALL_WIDTH:.0f}）、层高 {height:.0f}cm')

    floor_mat = mat('/Game/ArchViz/Materials/MI_Floor')
    wall_mat = mat('/Game/ArchViz/Materials/MI_Wall1')
    accent_mat = mat('/Game/ArchViz/Materials/MI_Wall_Accent')
    ceiling_mat = mat('/Game/ArchViz/Materials/MI_Ceiling')
    trim_mat = mat('/Game/ArchViz/Materials/MI_WhiteSemiMatte')
    steel = mat('/Game/Home_Interior/Materials/Metal/M_Metal_Steel')
    steel_dark = mat('/Game/ArchViz/Materials/MI_SteelDark')
    black = mat('/Game/Home_Interior/Materials/Metal/M_Metal_Black')
    glow = mat('/Game/Home_Interior/Materials/Mi_Emissive')

    # ---- 地板、天花板、四面墙（两头的墙贴在两边房子的外墙皮上，开门洞）----
    t = 10.0
    end_t = 3.0
    pieces = [
        ('Floor', V(x0 - 2.0, yl - t, floor_z - t), V(x1 + 2.0, yr + t, floor_z), floor_mat, 1100.0, 512, False),
        ('Ceiling', V(x0 - 2.0, yl - t, floor_z + height), V(x1 + 2.0, yr + t, floor_z + height + t), ceiling_mat, 400.0, 256, False),
        ('WallConsole', V(x0 - 2.0, yl - t, floor_z - t), V(x1 + 2.0, yl, floor_z + height + t), accent_mat, 400.0, 256, False),
        ('WallElevator', V(x0 - 2.0, yr, floor_z - t), V(x1 + 2.0, yr + t, floor_z + height + t), wall_mat, 400.0, 256, False),
        ('WallArchViz', V(x0, yl, floor_z), V(x0 + end_t, yr, floor_z + height), wall_mat, 400.0, 128, True),
        ('WallHome', V(x1 - end_t, yl, floor_z), V(x1, yr, floor_z + height), wall_mat, 400.0, 128, True),
    ]
    for name, lo, hi, material, uv_cm, lightmap, with_hole in pieces:
        center = (lo + hi) * 0.5
        cut = None
        if with_hole:
            # 门洞底边比地面低 5cm（布尔减干净），中心对准门洞
            cut = (V(0.0, door_y - center.y, floor_z + (hole_height - 5.0) * 0.5 - center.z), V(end_t * 4.0, hole_width, hole_height + 5.0))
        sm = hall_mesh(name, hi - lo, uv_cm, cut)
        a = actors_sub.spawn_actor_from_class(unreal.StaticMeshActor, center, unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
        c = a.static_mesh_component
        c.set_static_mesh(sm)
        c.set_material(0, material)
        c.set_editor_property('override_light_map_res', True)
        c.set_editor_property('overridden_light_map_res', lightmap)
        a.set_actor_label(f'Hall_{name}')
        a.set_folder_path('Hall')

    # ---- 两个门洞门厅这一面的门套（照搬样板间前门屋里那面的外圈门套）----
    for anchor_x, yaw, side in ((x0 + end_t, 0.0, 'ArchViz'), (x1 - end_t, 180.0, 'Home')):
        anchor = unreal.Transform(V(anchor_x, door_y, floor_z), unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw), V(1, 1, 1))
        for label, sm, materials, rel in casing_parts:
            tr = transform_mul(rel, anchor)
            a = actors_sub.spawn_actor_from_class(unreal.StaticMeshActor, tr.translation, tr.rotation.rotator())
            a.set_actor_scale3d(tr.scale3d)
            a.static_mesh_component.set_static_mesh(sm)
            for i, m in enumerate(materials):
                a.static_mesh_component.set_material(i, m)
            a.set_actor_label(f'Hall_{side}Door_{label}')
            a.set_folder_path('Hall')

    # ---- 电梯（+Y 那面墙正中）：不锈钢门套、双开拉丝门、门槛、召唤按钮、楼层显示 ----
    ex = mid
    ef = 'Hall/Elevator'
    box_actor('Elevator_Back', V(ex - 55.0, yr - 1.0, floor_z), V(ex + 55.0, yr, floor_z + 212.0), black, ef)
    box_actor('Elevator_DoorL', V(ex - 55.0, yr - 3.0, floor_z + 0.5), V(ex - 0.4, yr - 1.0, floor_z + 212.0), steel, ef)
    box_actor('Elevator_DoorR', V(ex + 0.4, yr - 3.0, floor_z + 0.5), V(ex + 55.0, yr - 1.0, floor_z + 212.0), steel, ef)
    box_actor('Elevator_JambL', V(ex - 67.0, yr - 5.0, floor_z), V(ex - 55.0, yr, floor_z + 227.0), steel_dark, ef)
    box_actor('Elevator_JambR', V(ex + 55.0, yr - 5.0, floor_z), V(ex + 67.0, yr, floor_z + 227.0), steel_dark, ef)
    box_actor('Elevator_Header', V(ex - 67.0, yr - 5.0, floor_z + 212.0), V(ex + 67.0, yr, floor_z + 227.0), steel_dark, ef)
    box_actor('Elevator_Sill', V(ex - 62.0, yr - 15.0, floor_z), V(ex + 62.0, yr - 3.0, floor_z + 0.6), steel_dark, ef)
    box_actor('Elevator_Display', V(ex - 20.0, yr - 1.5, floor_z + 236.0), V(ex + 20.0, yr, floor_z + 252.0), black, ef)
    box_actor('Elevator_CallPanel', V(ex + 82.0, yr - 1.5, floor_z + 98.0), V(ex + 95.0, yr, floor_z + 132.0), steel, ef)
    for i, z in enumerate((122.0, 108.0)):
        box_actor(f'Elevator_Button{i + 1}', V(ex + 86.0, yr - 2.5, floor_z + z - 2.5), V(ex + 91.0, yr - 1.5, floor_z + z + 2.5), glow, ef, collide=False)
    for text, dx, size in (('1', 0.0, 12.0),):
        tr = actors_sub.spawn_actor_from_class(unreal.TextRenderActor, V(ex + dx, yr - 1.7, floor_z + 244.0), unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0))
        tc = tr.text_render
        tc.set_editor_property('text', text)
        tc.set_editor_property('world_size', size)
        tc.set_editor_property('horizontal_alignment', unreal.HorizTextAligment.EHTA_CENTER)
        tc.set_editor_property('vertical_alignment', unreal.VerticalTextAligment.EVRTA_TEXT_CENTER)
        tc.set_editor_property('text_render_color', unreal.Color(r=255, g=110, b=40, a=255))
        tr.set_actor_label('Elevator_FloorNumber')
        tr.set_folder_path(ef)

    # ---- 踢脚线（两面侧墙、两头墙门洞两边；电梯那一段让开）----
    sk_h, sk_t = 10.0, 1.5
    casing_half = hole_width * 0.5 + 9.0
    skirting = [
        ('Console', V(x0 + end_t, yl, floor_z), V(x1 - end_t, yl + sk_t, floor_z + sk_h)),
        ('Elevator1', V(x0 + end_t, yr - sk_t, floor_z), V(ex - 67.0, yr, floor_z + sk_h)),
        ('Elevator2', V(ex + 67.0, yr - sk_t, floor_z), V(x1 - end_t, yr, floor_z + sk_h)),
    ]
    for side, xa, xb in (('ArchViz', x0 + end_t, x0 + end_t + sk_t), ('Home', x1 - end_t - sk_t, x1 - end_t)):
        skirting.append((f'{side}1', V(xa, yl + sk_t, floor_z), V(xb, door_y - casing_half, floor_z + sk_h)))
        skirting.append((f'{side}2', V(xa, door_y + casing_half, floor_z), V(xb, yr - sk_t, floor_z + sk_h)))
    for name, lo, hi in skirting:
        box_actor(f'Hall_Skirting{name}', lo, hi, trim_mat)

    # ---- 吸顶灯：三盏，灯罩是发光的圆片，光是静态面光源（烘焙）----
    cylinder = unreal.load_asset('/Engine/BasicShapes/Cylinder.Cylinder')
    for i, k in enumerate((1.0 / 6.0, 0.5, 5.0 / 6.0)):
        x = x0 + (x1 - x0) * k
        a = actors_sub.spawn_actor_from_class(unreal.StaticMeshActor, V(x, door_y, floor_z + height - 0.6), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
        a.set_actor_scale3d(V(0.32, 0.32, 0.012))
        a.static_mesh_component.set_static_mesh(cylinder)
        a.static_mesh_component.set_material(0, glow)
        a.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        a.static_mesh_component.set_editor_property('cast_shadow', False)
        a.set_actor_label(f'Hall_CeilingLamp{i + 1}')
        a.set_folder_path('Hall')
        light = actors_sub.spawn_actor_from_class(unreal.RectLight, V(x, door_y, floor_z + height - 1.5), unreal.Rotator(roll=0.0, pitch=-90.0, yaw=0.0))
        c = light.light_component
        c.set_mobility(unreal.ComponentMobility.STATIC)
        c.set_editor_property('source_width', 30.0)
        c.set_editor_property('source_height', 30.0)
        c.set_editor_property('intensity', HALL_LIGHT_CD)
        c.set_editor_property('attenuation_radius', 700.0)
        c.set_editor_property('use_temperature', True)
        c.set_editor_property('temperature', 4500.0)
        light.set_actor_label(f'Hall_Light{i + 1}')
        light.set_folder_path('Hall')

    # ---- 家具和装饰（样板间 / Home 的素材，材质照抄关卡里的）----
    # 餐边柜放在电梯对面那面墙正中，上面摆一只花瓶、一本画册、一个圆环摆件（后面会变成能拿起来的）；墙上两幅画
    console = copy_against_wall(world, 'SM_SideTable_02', 'Hall_Console', ex, yl + sk_t, -1, floor_z)
    if console:
        lo, hi = bounds(console)
        cy = (lo.y + hi.y) * 0.5
        for mesh, dx, label in (('SM_Vase_01', -45.0, 'Hall_Vase'), ('SM_Book_05', 10.0, 'Hall_Book'), ('SM_Torus', 48.0, 'Hall_Torus')):
            src, comp = find_mesh_component(mesh)
            if src:
                r = src.get_actor_rotation()
                spawn_mesh(comp.static_mesh, materials_of(comp), label, unreal.Rotator(roll=r.roll, pitch=r.pitch, yaw=r.yaw + 90.0),
                           src.get_actor_scale3d(), ex + dx, cy, hi.z + 0.2)
    # 挑平贴在墙上的画（有的是斜靠在架子上的，挂到墙上会歪着）：复制过来厚度超过 12cm 的不要，换下一个
    frames = []
    for mesh in ('SM_Frame_01', 'SM_Frame_03', 'SM_Picture_04', 'SM_Picture_03', 'SM_Picture_01'):
        if len(frames) == 2:
            break
        a = copy_against_wall(world, mesh, f'Hall_Frame{len(frames) + 1}', ex + (-32.0 if not frames else 32.0), yl, -1, floor_z + 128.0, gap=0.3)
        if a:
            lo, hi = bounds(a)
            if hi.y - lo.y > 12.0:
                log(f'  {mesh} 复制过来厚 {hi.y - lo.y:.0f}cm（原来是斜靠着的），不要')
                actors_sub.destroy_actor(a)
            else:
                frames.append(a)
    # 高的那盆：样板间素材里的白瓷盆 + 土 + 龙血树拼一盆（总高 1.3m 左右），放在样板间门口那头、电梯这面墙的墙角
    no_rot, one = unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0), V(1, 1, 1)
    px, py = x0 + end_t + 45.0, yr - 45.0
    p = spawn_mesh(unreal.load_asset('/Game/ArchViz/Meshes/SM_Pot_03'), None, 'Hall_TallPlant_Pot', no_rot, one, px, py, floor_z)
    _, phi = bounds(p)
    spawn_mesh(unreal.load_asset('/Game/ArchViz/Meshes/SM_Soil'), None, 'Hall_TallPlant_Soil', no_rot, one, px, py, phi.z - 5.0)
    leaves = spawn_mesh(unreal.load_asset('/Game/ArchViz/Plants/SM_Dracaena_04'), None, 'Hall_TallPlant',
                        unreal.Rotator(roll=0.0, pitch=0.0, yaw=35.0), one, px, py, phi.z - 8.0)
    _, lhi = bounds(leaves)
    log(f'门厅：高的绿植（SM_Pot_03 + SM_Dracaena_04）总高 {lhi.z - floor_z:.0f}cm，放在 ({px:.0f}, {py:.0f})')
    # 矮的那盆（Home 的盆栽）在 Home 门口那头，两个皮墩在餐边柜两边
    spawn_mesh(unreal.load_asset('/Game/Home_Interior/Meshes/SM_Plant'), None, 'Hall_Plant2', unreal.Rotator(roll=0.0, pitch=0.0, yaw=200.0), one,
               x1 - end_t - 50.0, yl + 50.0, floor_z)
    puff = unreal.load_asset('/Game/Home_Interior/Meshes/SM_Puff')
    for i, dx in enumerate((-110.0, 110.0)):
        spawn_mesh(puff, None, f'Hall_Puff{i + 1}', unreal.Rotator(roll=0.0, pitch=0.0, yaw=30.0 * i), one, ex + dx, yl + sk_t + 30.0, floor_z)
    # 地毯：客厅那块，长边顺着门厅
    src, comp = find_mesh_component('SM_Rug_01')
    if src:
        r = src.get_actor_rotation()
        rug = spawn_mesh(comp.static_mesh, materials_of(comp), 'Hall_Rug', r, src.get_actor_scale3d(), ex, door_y, floor_z + 0.1)
        lo, hi = bounds(rug)
        if hi.y - lo.y > hi.x - lo.x:
            rug.set_actor_rotation(unreal.Rotator(roll=r.roll, pitch=r.pitch, yaw=r.yaw + 90.0), False)
            lo, hi = bounds(rug)
            rug.set_actor_location(rug.get_actor_location() + V(ex - (lo.x + hi.x) * 0.5, door_y - (lo.y + hi.y) * 0.5, floor_z + 0.1 - lo.z), False, True)

    # ---- 反射球（不锈钢电梯门要有东西可反射），Lightmass 重要体积伸过来 ----
    cap = actors_sub.spawn_actor_from_class(unreal.BoxReflectionCapture, V(mid, door_y, floor_z + height * 0.5), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    _, e = cap.get_actor_bounds(False)
    if e.x > 1.0:
        cap.set_actor_scale3d(V((x1 - x0) * 0.5 / e.x, HALL_WIDTH * 0.5 / e.y, height * 0.5 / e.z))
    cap.set_actor_label('Hall_ReflectionCapture')
    cap.set_folder_path('Hall')
    for v in actors_sub.get_all_level_actors():
        if isinstance(v, unreal.LightmassImportanceVolume):
            fit_box_x(v, None, x1 + 30.0)
    return {'lo': V(x0, yl, floor_z), 'hi': V(x1, yr, floor_z + height), 'height': height}


def make_articulated(part, body_center, front, kind, name, label, material='木', max_value=None):
    """柜子上单独的一块门板 / 抽屉（StaticMeshActor）换成 AToyArticulated（能捏住拉开，松手停在那里，摆件复位时关上）。
    body_center：柜子的中心（世界坐标），front：柜子正面朝哪（世界坐标，水平单位向量）。
    kind = 'hinge'：铰链在门板离柜子中心线远的那条竖边、门板正面那一侧，往外开 0~100°；'slide'：沿 front 拉出 max_value 厘米。"""
    comp = part.static_mesh_component
    tr = part.get_actor_transform()
    lo, hi = bounds(part)
    center = (lo + hi) * 0.5
    a = actors_sub.spawn_actor_from_class(unreal.ToyArticulated, part.get_actor_location(), part.get_actor_rotation())
    a.set_actor_scale3d(part.get_actor_scale3d())
    m = a.get_editor_property('mesh')
    m.set_static_mesh(comp.static_mesh)
    for i, mat in enumerate(materials_of(comp)):
        m.set_material(i, mat)
    if kind == 'hinge':
        side = unreal.Vector(-front.y, front.x, 0.0)
        # 铰链那条边：门板中心在柜子中心线哪一边，就在那一边的外沿（双开门各开各的）
        s = 1.0 if (center - body_center).dot(side) >= 0.0 else -1.0
        half_side = abs(side.x) * (hi.x - lo.x) * 0.5 + abs(side.y) * (hi.y - lo.y) * 0.5
        half_front = abs(front.x) * (hi.x - lo.x) * 0.5 + abs(front.y) * (hi.y - lo.y) * 0.5
        pivot_world = center + side * (s * half_side) + front * half_front
        pivot = tr.inverse_transform_location(pivot_world)
        axis = tr.inverse_transform_direction(unreal.Vector(0.0, 0.0, 1.0))
        axis = axis * (1.0 / max(axis.length(), 1e-6))
        # 正转要往外开：试转 10°，门板中心要往 front 那边走，不然轴反过来
        mesh_center = comp.static_mesh.get_bounds().origin
        v = mesh_center - pivot
        c, sn = math.cos(math.radians(10.0)), math.sin(math.radians(10.0))
        # 罗德里格斯公式：v 绕单位轴 axis 转 10°
        turned = v * c + axis.cross(v) * sn + axis * (axis.dot(v) * (1.0 - c))
        moved = tr.transform_location(pivot + turned)
        if (moved - center).dot(front) < 0.0:
            axis = axis * -1.0
        a.set_editor_property('joint_type', unreal.ToyJointType.HINGE)
        a.set_editor_property('pivot', pivot)
        a.set_editor_property('max_value', max_value or 100.0)
    else:
        axis = tr.inverse_transform_direction(front)
        axis = axis * (1.0 / max(axis.length(), 1e-6))
        a.set_editor_property('joint_type', unreal.ToyJointType.SLIDE)
        a.set_editor_property('max_value', max_value or 30.0)
    a.set_editor_property('axis', axis)
    a.set_editor_property('display_name', name)
    a.set_editor_property('material_text', material)
    a.set_actor_label(label)
    a.set_folder_path(str(part.get_folder_path()) if str(part.get_folder_path()) not in ('', 'None') else 'Articulated')
    actors_sub.destroy_actor(part)
    log(f'能开合：{label} {name}（{"柜门，铰链" if kind == "hinge" else "抽屉，往外拉"}，最多 {a.get_editor_property("max_value"):.0f}'
        f'{"°" if kind == "hinge" else "cm"}）← {comp.static_mesh.get_name()} {fmt(lo)}..{fmt(hi)}')
    return a


# ---- Fab 免费素材包（本机 Content/FabPacks/<包名>，不进仓库；没装就跳过）----
FAB = '/Game/FabPacks'
# Modern House V1 里柜子的门板 / 抽屉是单独的网格：照它 Demo 关卡里的摆法（关着的位置）搬到门厅，门 / 抽屉换成 AToyArticulated
# (柜子在 Demo 里的标签, [(零件标签, 'hinge' / 'slide', 名字)], 门厅里放哪面墙的 X（门厅坐标比例 0..1）, 柜子名字)
FURNITURE_KITS = (
    ('SM_Cabinet_8', (('SM_Cabinet_7_Door', 'slide', '矮柜上面的抽屉'), ('SM_Cabinet_7_Door2', 'slide', '矮柜下面的抽屉')), 'elevator_right', '矮柜'),
    ('SM_Cabinet_16', (('SM_Cabinet_10_Door', 'hinge', '小柜子的门'),), 'elevator_left', '小柜子'),
)
FURNITURE_MAP = f'{FAB}/Modern_House_V1/Maps/Demo'


def read_furniture_kits():
    """打开 Modern House 的 Demo 关卡，记下每套柜子的网格、材质和门板 / 抽屉相对柜子的变换。没装这个包返回空。"""
    if not unreal.EditorAssetLibrary.does_asset_exist(FURNITURE_MAP):
        log(f'没有 {FURNITURE_MAP}（Fab 的 Modern House V1 没装），门厅不放能开的柜子')
        return []
    level_sub.load_level(FURNITURE_MAP)
    by_label = {a.get_actor_label(): a for a in actors_sub.get_all_level_actors() if isinstance(a, unreal.StaticMeshActor)}
    kits = []
    for body_label, parts, where, body_name in FURNITURE_KITS:
        body = by_label.get(body_label)
        if not body or not all(p[0] in by_label for p in parts):
            log(f'警告：Modern House 的 Demo 里找不到 {body_label} 那一套，不放')
            continue
        bt = body.get_actor_transform()
        blo, bhi = bounds(body)
        bc = (blo + bhi) * 0.5
        recs = []
        offset = unreal.Vector(0, 0, 0)
        for label, kind, name in parts:
            p = by_label[label]
            plo, phi = bounds(p)
            offset = offset + ((plo + phi) * 0.5 - bc)
            recs.append((p.static_mesh_component.static_mesh, materials_of(p.static_mesh_component), p.get_actor_transform().make_relative(bt), kind, name))
        # 正面朝哪：柜子水平方向上窄的那个轴，门板 / 抽屉偏向的那一边
        if (bhi.x - blo.x) < (bhi.y - blo.y):
            front = unreal.Vector(1.0 if offset.x >= 0 else -1.0, 0.0, 0.0)
        else:
            front = unreal.Vector(0.0, 1.0 if offset.y >= 0 else -1.0, 0.0)
        kits.append({'mesh': body.static_mesh_component.static_mesh, 'materials': materials_of(body.static_mesh_component),
                     'rotation': body.get_actor_rotation(), 'scale': body.get_actor_scale3d(), 'front': front, 'parts': recs,
                     'where': where, 'name': body_name, 'label': body_label})
        log(f'Modern House：{body_label} [{body.static_mesh_component.static_mesh.get_name()}] 正面朝 {fmt(front)}，{len(recs)} 块能开的')
    return kits


def place_furniture_kits(world, kits, hall, ex, wall_y, floor_z):
    """门厅电梯那面墙（+Y）上、电梯两边各放一套柜子，正面朝 -Y，门 / 抽屉换成能开合的。"""
    spots = {'elevator_left': (hall['lo'].x + ex) * 0.5 + 20.0, 'elevator_right': (ex + 67.0 + hall['hi'].x) * 0.5 - 5.0}
    placed = []
    for kit in kits:
        f = kit['front']
        dyaw = math.degrees(math.atan2(-1.0, 0.0) - math.atan2(f.y, f.x))
        r = kit['rotation']
        rot = unreal.Rotator(roll=r.roll, pitch=r.pitch, yaw=r.yaw + dyaw)
        x = spots[kit['where']]
        body = spawn_mesh(kit['mesh'], kit['materials'], f'Hall_{kit["label"]}', rot, kit['scale'], x, wall_y, floor_z)
        lo, hi = bounds(body)
        body.set_actor_location(body.get_actor_location() + unreal.Vector(0.0, wall_y - 0.5 - hi.y, 0.0), False, True)
        lo, hi = bounds(body)
        center = (lo + hi) * 0.5
        depth = hi.y - lo.y
        bt = body.get_actor_transform()
        for i, (sm, mats, rel, kind, name) in enumerate(kit['parts']):
            tr = transform_mul(rel, bt)
            p = actors_sub.spawn_actor_from_class(unreal.StaticMeshActor, tr.translation, tr.rotation.rotator())
            p.set_actor_scale3d(tr.scale3d)
            p.static_mesh_component.set_static_mesh(sm)
            for k, m in enumerate(mats):
                p.static_mesh_component.set_material(k, m)
            p.set_folder_path('Hall')
            make_articulated(p, center, unreal.Vector(0.0, -1.0, 0.0), kind, name, f'Hall_{kit["label"]}_Part{i + 1}',
                             max_value=100.0 if kind == 'hinge' else round(depth * 0.7))
        log(f'门厅：{kit["name"]}（Modern House {kit["mesh"].get_name()}）{fmt(lo)}..{fmt(hi)}，{len(kit["parts"])} 块能开合')
        placed.append(body)
    return placed


def place_on_surface(world, path, label, x, y, yaw=0.0, folder_path='Decor', lift=0.05, top=170.0):
    """把素材包里的一个网格放到 (x, y) 正下方第一个面上（材质用网格自带的）。网格不在就跳过。
    从 top（离地）往下找：再高就先打到吊扇、吊灯了。"""
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return None
    _, hit = trace_first(world, unreal.Vector(x, y, top), unreal.Vector(x, y, -20.0), lambda a: True)
    z = (hit.z if hit else 0.0) + lift
    return spawn_mesh(unreal.load_asset(path), None, label, unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw), unreal.Vector(1, 1, 1), x, y, z, folder_path)


def add_archviz_decor(world, to_world):
    """样板间里加的素材包小东西（工作室坐标）：茶几上一盘国际象棋（Classic Office，棋子能一个个拿），窗下长柜上一副耳机和一卷胶带（Deko 的公寓包）。"""
    co = f'{FAB}/Classic_Office/Meshes/DecorativeElements'
    deko = f'{FAB}/Deko_MatrixDemo/Apartment/Meshes'
    n = 0
    p = to_world(-406.0, -70.0)
    if place_on_surface(world, f'{co}/SM_Board', 'Decor_ChessBoard', p.x, p.y, 0.0):
        n += 1
        step = 43.0 / 8.0
        for i in range(6):
            for side, first, dy in ((0, 1, -1.5), (1, 7, 1.5)):
                q = to_world(-406.0 + (i - 2.5) * step, -70.0 + dy * step)
                if place_on_surface(world, f'{co}/SM_Chess_Piece_{first + i:02d}', f'Decor_Chess{first + i:02d}', q.x, q.y, 180.0 * side):
                    n += 1
    for path, lx, ly, yaw in ((f'{deko}/SM_Headset_A01_N1', 105.0, -229.0, 20.0), (f'{deko}/SM_OfficeTape_A01_N1', 132.0, -231.0, 0.0),
                              (f'{MILRADIO}/SM_HandRadio', 74.0, -230.0, -15.0)):
        q = to_world(lx, ly)
        if place_on_surface(world, path, f'Decor_{path.rsplit("/", 1)[1]}', q.x, q.y, yaw):
            n += 1
    log(f'样板间：素材包的小东西加了 {n} 件')


def add_hall_decor(world, ex, yl, yr):
    """门厅：矮柜上一套茶具（Classic Office），小柜子上一匹马的摆件和笔筒（Office Pack），餐边柜上一只玻璃杯（Office Pack）。"""
    co = f'{FAB}/Classic_Office/Meshes/DecorativeElements'
    op = f'{FAB}/Office_Pack_Vol_1/Models'
    right = ex + 67.0 + 85.0
    left = ex - 115.0
    items = ((f'{co}/SM_Kettle', right - 25.0, yr - 18.0, 200.0), (f'{co}/SM_Sugar_bowl', right + 2.0, yr - 16.0, 0.0),
             (f'{co}/SM_Cup_01', right + 20.0, yr - 20.0, 30.0), (f'{co}/SM_Cup_02', right + 32.0, yr - 14.0, 110.0),
             (f'{co}/SM_Horse', left - 6.0, yr - 22.0, 160.0), (f'{op}/SM_Pen_Holder_1', left + 14.0, yr - 16.0, 0.0),
             (f'{op}/SM_glass_1', ex - 25.0, yl + 20.0, 0.0))
    n = sum(1 for path, x, y, yaw in items if place_on_surface(world, path, f'Hall_{path.rsplit("/", 1)[1]}', x, y, yaw, 'Hall'))
    log(f'门厅：素材包的小东西加了 {n} 件')


UB = f'{FAB}/UnrealBucket/Library'
MILRADIO = f'{FAB}/Military_Radio/StaticMesh'


def place_ub_nightstand(world, x, wall_y, toward, floor_z):
    """门厅侧墙边一个床头柜（Unreal Bucket 的 Nightstand_01：柜体、两个抽屉是分开的网格，水平原点相同，抽屉原点在抽屉底；
    包里的 Showcase 关卡把上面的抽屉 A 放在柜体原点上 50cm、下面的 B 在 20cm），两个抽屉能拉开。
    wall_y 是墙面，toward = +1 / -1 是墙在哪一边。返回柜体，没装这个包返回 None。"""
    d = f'{UB}/ArchViz/Nightstand_01'
    paths = [f'{d}/SM_Nightstand_01_Base', f'{d}/SM_Nightstand_01_Drawer_A', f'{d}/SM_Nightstand_01_Drawer_B']
    if not all(unreal.EditorAssetLibrary.does_asset_exist(p) for p in paths):
        log('没有 Unreal Bucket 的 Nightstand_01，门厅不放床头柜')
        return None
    base_sm, *drawer_sms = [unreal.load_asset(p) for p in paths]
    # 网格坐标里正面朝哪：抽屉的包围盒中心相对柜体偏向的那个水平方向
    bb, db = base_sm.get_bounding_box(), drawer_sms[0].get_bounding_box()
    off = (db.min + db.max) * 0.5 - (bb.min + bb.max) * 0.5
    local_yaw = math.degrees(math.atan2(off.y, off.x)) if abs(off.x) > abs(off.y) * 0.2 or abs(off.y) > abs(off.x) * 0.2 else 0.0
    local_yaw = round(local_yaw / 90.0) * 90.0
    front = unreal.Vector(0.0, -float(toward), 0.0)
    yaw = math.degrees(math.atan2(front.y, front.x)) - local_yaw
    rot = unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw)
    body = spawn_mesh(base_sm, None, 'Hall_Nightstand', rot, unreal.Vector(1, 1, 1), x, wall_y, floor_z)
    lo, hi = bounds(body)
    face = hi.y if toward > 0 else lo.y
    body.set_actor_location(body.get_actor_location() + unreal.Vector(0.0, wall_y - toward * 0.5 - face, 0.0), False, True)
    lo, hi = bounds(body)
    center = (lo + hi) * 0.5
    depth = hi.y - lo.y
    n = 0
    for i, (sm, dz) in enumerate(zip(drawer_sms, (50.0, 20.0))):
        p = actors_sub.spawn_actor_from_class(unreal.StaticMeshActor, body.get_actor_location() + unreal.Vector(0.0, 0.0, dz), body.get_actor_rotation())
        p.static_mesh_component.set_static_mesh(sm)
        p.set_folder_path('Hall')
        plo, phi = bounds(p)
        # 原点不同的话抽屉会飘在柜子外面：那就不放这个抽屉（柜体自己有抽屉面的话看起来也还是一个柜子）
        if plo.x < lo.x - 5.0 or phi.x > hi.x + 5.0 or plo.z < lo.z - 5.0 or phi.z > hi.z + 5.0:
            log(f'警告：{sm.get_name()} 和柜体原点对不上 {fmt(plo)}..{fmt(phi)}，不放')
            actors_sub.destroy_actor(p)
            continue
        make_articulated(p, center, front, 'slide', f'床头柜{"上面" if i == 0 else "下面"}的抽屉', f'Hall_Nightstand_Drawer{i + 1}',
                         max_value=round(depth * 0.65))
        n += 1
    log(f'门厅：床头柜（Unreal Bucket Nightstand_01）{fmt(lo)}..{fmt(hi)}，{n} 个抽屉能拉开')
    return body


def add_pack_smalls(world, items, folder_path):
    """新素材包（Unreal Bucket、Military Radio）里的小东西放到台面上：items = (网格路径, x, y, yaw)，世界坐标。之后 auto_props 会把它们挑成能拿的。"""
    n = sum(1 for path, x, y, yaw in items if place_on_surface(world, path, f'{folder_path}_{path.rsplit("/", 1)[1]}', x, y, yaw, folder_path))
    log(f'{folder_path}：新素材包的小东西加了 {n} 件')


# ---- 自动挑能拿起来的小东西 ----
# 表里（PROPS / HOME_PROPS）挑剩下的：包围盒最长边不超过这么多（厘米），连同叠在上面的东西不超过 AUTO_PROP_MAX_STACK，
# 底面离地不超过 AUTO_PROP_MAX_REACH（站着伸手够得到），底下 AUTO_PROP_SUPPORT_GAP 以内有朝上的台面（挂在墙上、钩子上的不算）
AUTO_PROP_MAX_SIZE = 45.0
AUTO_PROP_MAX_STACK = 60.0
AUTO_PROP_MAX_REACH = 185.0
AUTO_PROP_SUPPORT_GAP = 4.0
# 网格名里带这些的不挑：开关插座、挂画、窗帘百叶、灯、钩子挂杆、家具的零件、绿植的叶子（叶子挂在花盆上一起走）、门厅的电梯和踢脚线
AUTO_PROP_EXCLUDE = ('switch', 'socket', 'frame', 'picture', 'curtain', 'blind', 'lamp', 'light', 'bulb', 'hook', 'rail', 'panel', 'wire',
                     'napkin', 'soil', 'dirt', 'handle', 'door', 'window', 'sill', 'shelf', 'shelves', 'cupboard', 'glassholder', 'control',
                     'kitchen', 'tap', 'sink', 'radiator', 'fan', 'cradle', 'blackboard', 'clock', 'mirror', 'towel', 'glove', 'background',
                     'scene', 'carpet', 'rug', 'floor', 'wall', 'ceiling', 'skirting', 'elevator', 'cube', 'cylinder', 'mobile', 'greenery',
                     'aglaonema', 'amaryllis', 'dracaena', 'snakeplant', 'chlorophytum', 'var3', 'cherry', 'cornice', 'luster', 'tv',
                     'monitor', 'printer', 'tower', 'stand', 'leaf', 'branch', 'ficus', 'puff', 'ottoman', 'chair', 'table', 'desk', 'board', 'cabinet')
# 这些就算压在摆件底下 / 套在摆件上也不跟着走（餐巾、托盘、地毯留在原处）
AUTO_RIDER_EXCLUDE = ('napkin', 'tray', 'carpet', 'rug', 'mat', 'cloth')
# PropTest 跑出来不稳的（标签）：一醒就滑 / 歪、松手落不回原处、丢到地上穿下去的，下次生成不挑
AUTO_PROP_SKIP = set(os.environ.get('ARCHVIZ_PROP_SKIP', '').split(',')) - {''}
_skip_file = os.path.join(os.path.dirname(os.path.abspath(os.environ.get('ARCHVIZ_SCRIPT', __file__))), 'prop_skip.txt')
if os.path.exists(_skip_file):
    AUTO_PROP_SKIP |= {line.strip() for line in open(_skip_file, encoding='utf-8') if line.strip() and not line.startswith('#')}
# 网格名 → 信息牌上的名字（先匹配到的算）
AUTO_PROP_NAMES = (
    ('wineglass', '酒杯'), ('glass', '玻璃杯'), ('cup', '杯子'), ('mug', '马克杯'), ('jug', '水壶'), ('kettle', '水壶'), ('bottle', '瓶子'),
    ('jar', '罐子'), ('plate', '盘子'), ('dish', '碟子'), ('bowl', '碗'), ('bawl', '碗'), ('tray', '托盘'), ('candlestick', '烛台'),
    ('candle', '蜡烛'), ('books', '一摞书'), ('book', '书'), ('notebook', '笔记本'), ('manuscript', '手稿'), ('newspaper', '报纸'),
    ('vase', '花瓶'), ('pot', '花盆'), ('bucket', '小桶'), ('plant', '小盆栽'), ('flower', '插花'), ('box', '收纳盒'), ('basket', '篮子'),
    ('apple', '苹果摆件'), ('torus', '圆环摆件'), ('hedra', '多面体摆件'), ('spring', '弹簧摆件'), ('top', '陀螺'), ('remote', '遥控器'),
    ('knife', '刀'), ('fork', '叉子'), ('spoon', '勺子'), ('scapula', '锅铲'), ('pan', '锅'), ('colander', '沥水篮'), ('mixer', '打蛋器'),
    ('cutting', '砧板'), ('pillow', '抱枕'), ('cushion', '靠垫'), ('puff', '皮墩'), ('globe', '地球仪'), ('chess', '棋子'),
    ('horse', '马的摆件'), ('bird', '小鸟摆件'), ('bust', '半身像'), ('ballerina', '芭蕾舞者摆件'), ('gramophone', '留声机'),
    ('pen', '笔'), ('phone', '电话'), ('keyboard', '键盘'), ('mouse', '鼠标'), ('headset', '耳机'), ('speaker', '音箱'),
    ('tape', '胶带'), ('pliers', '钳子'), ('hammer', '锤子'), ('wrench', '扳手'), ('toolbox', '工具箱'), ('card', '电路板'),
    ('parts', '电脑配件'), ('solution', '清洁剂'), ('lunchbox', '饭盒'), ('hardhat', '安全帽'), ('canister', '油桶'), ('decoration', '摆件'),
    ('bag', '包'), ('jelly', '果冻'),
)
# 材料（UToyFeedback::ClassifyMaterial 认的字）：先看网格名，再看材质名；密度（g/cm³，按包围盒体积 × 0.3 估质量）
AUTO_PROP_MATERIALS = (
    (('wineglass', 'glass'), '玻璃', 1.0), (('cup', 'mug', 'plate', 'dish', 'bowl', 'bawl', 'jug', 'porcelain', 'ceramic'), '陶瓷', 0.9),
    (('marble',), '大理石', 1.2), (('candle',), '蜡', 0.5), (('book', 'paper', 'page', 'manuscript', 'newspaper', 'notebook'), '纸', 0.7),
    (('pan_', 'bucket', 'scapula', 'mixer', 'colander', 'knife', 'fork', 'spoon', 'pliers', 'hammer', 'wrench'), '不锈钢', 1.5),
    (('plant',), '陶瓷花盆', 0.9),
    (('chrome', 'steel', 'silver', 'metal', 'iron'), '不锈钢', 1.5),
    (('brass', 'gold', 'bronze', 'copper'), '黄铜', 1.5),
    (('cloth', 'fabric', 'velvet', 'pillow', 'cushion', 'leather', 'suede', 'cashmere', 'cell', 'denim'), '布', 0.15),
    (('wood', 'oak', 'cutting'), '木', 0.5), (('rubber',), '橡胶', 0.9), (('plastic', 'keyboard', 'mouse', 'phone', 'tape', 'headset'), '塑料', 0.4),
)


def auto_material(mesh_lower, material_names):
    for text in (mesh_lower, ' '.join(material_names).lower()):
        for words, name, density in AUTO_PROP_MATERIALS:
            if any(w in text for w in words):
                return name, density
    return '塑料', 0.5


def auto_props(world, area, label_prefix, accept=lambda a: True, floor_z=None):
    """把 area 里没在表里的小东西自动换成 AToyProp（凸包碰撞，叠在上面的东西挂上去一起走），返回换了几件。"""
    studio = next((a for a in actors_sub.get_all_level_actors() if isinstance(a, unreal.ToyStudio)), None)
    smas = [a for a in actors_sub.get_all_level_actors()
            if isinstance(a, unreal.StaticMeshActor) and a.get_attach_parent_actor() is None and accept(a)]
    info = {}
    for a in smas:
        sm = a.static_mesh_component.static_mesh
        if not sm or a.static_mesh_component.get_editor_property('mobility') == unreal.ComponentMobility.MOVABLE:
            continue
        lo, hi = bounds(a)
        info[a] = (sm.get_name().lower(), lo, hi)
    # 地面高度：样板间是工作室站立点，Home / 门厅也一样平（地面对齐过）
    if floor_z is None:
        floor_z = studio.get_actor_location().z if studio else 0.0
    candidates = []
    for a, (name, lo, hi) in info.items():
        size = max(hi.x - lo.x, hi.y - lo.y, hi.z - lo.z)
        if size > AUTO_PROP_MAX_SIZE or size < 3.0 or lo.z - floor_z > AUTO_PROP_MAX_REACH or any(w in name for w in AUTO_PROP_EXCLUDE):
            continue
        candidates.append(a)
    cand_set = set(candidates)
    # 每件下面是什么：往下打一条短线，打到别的候选 = 叠在它上面（跟着它走）；打到朝上的台面 = 自己是一件；什么都没打到 = 挂着的，不要
    support = {}
    hanging = []
    for a in candidates:
        _, lo, hi = info[a]
        c = (lo + hi) * 0.5
        hit, point = trace_first(world, unreal.Vector(c.x, c.y, lo.z + 2.0), unreal.Vector(c.x, c.y, lo.z - AUTO_PROP_SUPPORT_GAP),
                                 lambda o, me=a: o is not me)
        if hit is None:
            hanging.append(a)
        else:
            support[a] = hit
    # 叶子、土、花这些（不是候选，但底面在候选的包围盒里、中心在它的脚底范围里）也挂上去
    parent = {}
    for a in candidates:
        s = support.get(a)
        if s in cand_set and s is not a and not any(w in info[s][0] for w in AUTO_RIDER_EXCLUDE):
            parent[a] = s
    for o, (name, lo, hi) in info.items():
        if o in cand_set or any(w in name for w in AUTO_RIDER_EXCLUDE):
            continue
        c = (lo + hi) * 0.5
        for a in candidates:
            _, alo, ahi = info[a]
            if alo.x <= c.x <= ahi.x and alo.y <= c.y <= ahi.y and alo.z - 1.0 <= lo.z <= ahi.z - 1.0 and max(hi.x - lo.x, hi.y - lo.y) < 150.0:
                parent[o] = a
                break

    def root(a):
        seen = set()
        while a in parent and a not in seen:
            seen.add(a)
            a = parent[a]
        return a

    groups = {}
    for a in candidates:
        if a in hanging:
            continue
        groups.setdefault(root(a), []).append(a)
    for o in parent:
        if o not in cand_set:
            r = root(o)
            if r in groups:
                groups[r].append(o)
    made = 0
    skipped = {'太大': 0, '挂着的': len(hanging), '表里排除': 0}
    used_labels = set(a.get_actor_label() for a in actors_sub.get_all_level_actors())
    changed_meshes = {}
    for top in sorted(groups, key=lambda a: (folder(a), a.get_actor_label())):
        if top not in cand_set or top in hanging:
            continue
        members = groups[top]
        # 一叠里最下面那件当摆件本体（抓的是它），其他的挂上去
        base = min((m for m in members if m in cand_set), key=lambda m: info[m][1].z)
        lo = unreal.Vector(min(info[m][1].x for m in members), min(info[m][1].y for m in members), min(info[m][1].z for m in members))
        hi = unreal.Vector(max(info[m][2].x for m in members), max(info[m][2].y for m in members), max(info[m][2].z for m in members))
        if max(hi.x - lo.x, hi.y - lo.y, hi.z - lo.z) > AUTO_PROP_MAX_STACK:
            skipped['太大'] += 1
            continue
        label = f'{label_prefix}_{(folder(base).replace("/", "-") + "-") if folder(base) and folder(base) != "None" else ""}{base.get_actor_label()}'.replace(' ', '')
        if label in AUTO_PROP_SKIP:
            skipped['表里排除'] += 1
            continue
        while label in used_labels:
            label += '_'
        used_labels.add(label)
        comp = base.static_mesh_component
        sm = comp.static_mesh
        mesh_lower = sm.get_name().lower()
        display = next((n for k, n in AUTO_PROP_NAMES if k in mesh_lower), '摆件')
        mats = materials_of(comp)
        material, density = auto_material(mesh_lower, [m.get_name() if m else '' for m in mats])
        blo, bhi = info[base][1], info[base][2]
        volume = (bhi.x - blo.x) * (bhi.y - blo.y) * (bhi.z - blo.z)
        # 包围盒体积 × 密度 × 0.12（花瓶、杯子大多是空心的，书是实心但很薄），限制在 50g ~ 2.5kg
        mass = min(2.5, max(0.05, volume * density * 0.12 / 1000.0))
        # 网格用凸包（物理模拟不能用三角面）：以前被改成"三角面当简单碰撞"的改回来，没有简单碰撞的加一个整体凸包
        body = sm.get_editor_property('body_setup')
        if body and body.get_editor_property('collision_trace_flag') == unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:
            body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_DEFAULT)
            changed_meshes[sm.get_path_name()] = sm
        how = unreal.ToyArchVizTools.ensure_prop_collision(sm, False)
        if '加了' in how:
            changed_meshes[sm.get_path_name()] = sm
        prop = actors_sub.spawn_actor_from_class(unreal.ToyProp, base.get_actor_location(), base.get_actor_rotation())
        prop.set_actor_scale3d(base.get_actor_scale3d())
        pm = prop.get_editor_property('mesh')
        pm.set_static_mesh(sm)
        for slot, m in enumerate(mats):
            pm.set_material(slot, m)
        prop.set_editor_property('display_name', display)
        prop.set_editor_property('material_text', material)
        prop.set_editor_property('mass_kg', round(mass, 2))
        prop.set_actor_label(label)
        prop.set_folder_path('Props')
        riders = []
        for r in members:
            if r is base:
                continue
            rc = r.static_mesh_component
            rc.set_mobility(unreal.ComponentMobility.MOVABLE)
            rc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            r.attach_to_actor(prop, '', unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
            riders.append(f'{r.get_actor_label()}[{mesh_name(r)}]')
        actors_sub.destroy_actor(base)
        made += 1
        log(f'自动摆件 {label}：{display}（{material}，{mass:.2f}kg）[{sm.get_name()}] {bhi.x - blo.x:.0f}×{bhi.y - blo.y:.0f}×{bhi.z - blo.z:.0f}cm，'
            f'底面离地 {blo.z - floor_z:.0f}cm' + (f'，带上 {"、".join(riders)}' if riders else ''))
    for sm in changed_meshes.values():
        unreal.EditorAssetLibrary.save_loaded_asset(sm, False)
    log(f'{area}：自动挑了 {made} 件能拿起来的小东西（候选 {len(candidates)}，没挑：' + '，'.join(f'{k} {v}' for k, v in skipped.items()) + '）')
    return made


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
