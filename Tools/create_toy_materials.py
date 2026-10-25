"""建工程自己用的几个材质（只建一次，生成的 uasset 提交到仓库）。

在编辑器里跑（命令行由 scripts/import_motor.bat 调用）。已经存在就跳过，设置环境变量 TOYBOX_FORCE_MATERIALS=1 强制重建。

M_ToyGhost：半透明、不受光照、双面，参数 Color（向量）和 Opacity（标量）。
    - 零件抓在手里、靠近原位时，原位显示的"虚影"
    - 手靠近可抓零件时叠加在零件上的高亮（UMeshComponent 的 OverlayMaterial）
    - 刻蚀机放大腔的半透明内衬、介质窗（内衬是实例化网格，所以要勾 used_with_instanced_static_meshes）

刻蚀机演示模式（AToyEtcherRig）另外用到四个，参数都是 Color（向量），辉光多一个 Opacity（标量）：
M_ToyUnlit：不透明、不受光照，Color 直接当发光色。放大腔里的粒子（实例化网格）。
M_ToyLit：不透明、受光照，Color 当底色。放大腔上面的铜线圈（实例化网格）。
M_ToyGlow：半透明叠加（越叠越亮）、不受光照，发光 = Color × Opacity。放大腔里的辉光。
M_ToyGlowXRay：和 M_ToyGlow 一样，但不做深度测试：桌上小模型腔里的辉光隔着腔壁也看得见。
"""
import os

import unreal

FOLDER = '/Game/Materials'
GHOST = 'M_ToyGhost'


def create_ghost():
    path = f'{FOLDER}/{GHOST}'
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        if os.environ.get('TOYBOX_FORCE_MATERIALS') != '1':
            unreal.log(f'ToyBox: {path} 已存在，跳过')
            return
        unreal.EditorAssetLibrary.delete_asset(path)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(GHOST, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)

    mel = unreal.MaterialEditingLibrary
    color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -500, 0)
    color.set_editor_property('parameter_name', 'Color')
    color.set_editor_property('default_value', unreal.LinearColor(0.3, 0.85, 1.0, 1.0))

    opacity = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -500, 250)
    opacity.set_editor_property('parameter_name', 'Opacity')
    opacity.set_editor_property('default_value', 0.35)

    # 边缘更亮一点（菲涅尔），虚影的轮廓更清楚
    fresnel = mel.create_material_expression(mat, unreal.MaterialExpressionFresnel, -500, 400)
    edge = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -250, 300)
    edge.set_editor_property('const_a', 0.6)
    edge.set_editor_property('const_b', 1.6)
    mel.connect_material_expressions(fresnel, '', edge, 'Alpha')
    lit = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -100, 0)
    mel.connect_material_expressions(color, '', lit, 'A')
    mel.connect_material_expressions(edge, '', lit, 'B')
    fade = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -100, 250)
    mel.connect_material_expressions(opacity, '', fade, 'A')
    mel.connect_material_expressions(edge, '', fade, 'B')

    mel.connect_material_property(lit, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(fade, '', unreal.MaterialProperty.MP_OPACITY)
    mat.set_editor_property('used_with_instanced_static_meshes', True)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f'ToyBox: 建好了 {path}')


def ensure_ghost_instancing():
    """早先建的 M_ToyGhost 没勾实例化网格：编辑器里会自己补上但每次都报警告，这里补勾一次存盘。"""
    path = f'{FOLDER}/{GHOST}'
    mat = unreal.EditorAssetLibrary.load_asset(path)
    if mat is None or mat.get_editor_property('used_with_instanced_static_meshes'):
        return
    mat.set_editor_property('used_with_instanced_static_meshes', True)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f'ToyBox: {path} 补勾了 used_with_instanced_static_meshes')


def new_material(name, blend, shading):
    """建一个空材质；已经存在就返回 None（除非 TOYBOX_FORCE_MATERIALS=1）。"""
    path = f'{FOLDER}/{name}'
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        if os.environ.get('TOYBOX_FORCE_MATERIALS') != '1':
            unreal.log(f'ToyBox: {path} 已存在，跳过')
            return None
        unreal.EditorAssetLibrary.delete_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property('blend_mode', blend)
    mat.set_editor_property('shading_model', shading)
    return mat


def color_param(mat, default):
    mel = unreal.MaterialEditingLibrary
    color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -500, 0)
    color.set_editor_property('parameter_name', 'Color')
    color.set_editor_property('default_value', default)
    return color


def save_material(mat, name):
    unreal.MaterialEditingLibrary.recompile_material(mat)
    path = f'{FOLDER}/{name}'
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f'ToyBox: 建好了 {path}')


def create_unlit():
    mat = new_material('M_ToyUnlit', unreal.BlendMode.BLEND_OPAQUE, unreal.MaterialShadingModel.MSM_UNLIT)
    if mat is None:
        return
    color = color_param(mat, unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mat.set_editor_property('used_with_instanced_static_meshes', True)
    save_material(mat, 'M_ToyUnlit')


def create_lit():
    mat = new_material('M_ToyLit', unreal.BlendMode.BLEND_OPAQUE, unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    if mat is None:
        return
    mel = unreal.MaterialEditingLibrary
    color = color_param(mat, unreal.LinearColor(0.75, 0.35, 0.12, 1.0))
    mel.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -500, 250)
    rough.set_editor_property('r', 0.45)
    mel.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    mat.set_editor_property('used_with_instanced_static_meshes', True)
    save_material(mat, 'M_ToyLit')


def create_glow(name, xray):
    # 叠加混合不看 Opacity 引脚，所以淡入淡出靠发光色乘 Opacity
    mat = new_material(name, unreal.BlendMode.BLEND_ADDITIVE, unreal.MaterialShadingModel.MSM_UNLIT)
    if mat is None:
        return
    mel = unreal.MaterialEditingLibrary
    color = color_param(mat, unreal.LinearColor(0.56, 0.42, 1.0, 1.0))
    opacity = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -500, 250)
    opacity.set_editor_property('parameter_name', 'Opacity')
    opacity.set_editor_property('default_value', 0.2)
    glow = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, 100)
    mel.connect_material_expressions(color, '', glow, 'A')
    mel.connect_material_expressions(opacity, '', glow, 'B')
    mel.connect_material_property(glow, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if xray:
        mat.set_editor_property('disable_depth_test', True)
    save_material(mat, name)


create_ghost()
ensure_ghost_instancing()
create_unlit()
create_lit()
create_glow('M_ToyGlow', False)
create_glow('M_ToyGlowXRay', True)
