"""建工程自己用的几个材质（只建一次，生成的 uasset 提交到仓库）。

在编辑器里跑（命令行由 scripts/import_motor.bat 调用）。已经存在就跳过，设置环境变量 TOYBOX_FORCE_MATERIALS=1 强制重建。

M_ToyGhost：半透明、不受光照、双面，参数 Color（向量）和 Opacity（标量）。
    - 零件抓在手里、靠近原位时，原位显示的"虚影"
    - 手靠近可抓零件时叠加在零件上的高亮（UMeshComponent 的 OverlayMaterial）
M_ToyArc：半透明、不受光照、双面，颜色 = Color × 顶点颜色，不透明度 = Opacity × 顶点 alpha，不受雾影响。
    - 传送抛物线的细管和落点圈（AToyBoxPawn 用程序网格画，渐隐、越往前越亮都写在顶点颜色里）
"""
import os

import unreal

FOLDER = '/Game/Materials'
GHOST = 'M_ToyGhost'
ARC = 'M_ToyArc'


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
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f'ToyBox: 建好了 {path}')


def create_arc():
    path = f'{FOLDER}/{ARC}'
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        if os.environ.get('TOYBOX_FORCE_MATERIALS') != '1':
            unreal.log(f'ToyBox: {path} 已存在，跳过')
            return
        unreal.EditorAssetLibrary.delete_asset(path)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(ARC, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)
    # 不让雾把抛物线染灰（远处的落点也要看得清）
    mat.set_editor_property('use_translucency_vertex_fog', False)

    mel = unreal.MaterialEditingLibrary
    color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -500, 0)
    color.set_editor_property('parameter_name', 'Color')
    color.set_editor_property('default_value', unreal.LinearColor(0.2, 0.85, 1.0, 1.0))
    opacity = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -500, 250)
    opacity.set_editor_property('parameter_name', 'Opacity')
    opacity.set_editor_property('default_value', 1.0)
    vertex = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -500, 400)

    lit = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 0)
    mel.connect_material_expressions(color, '', lit, 'A')
    mel.connect_material_expressions(vertex, '', lit, 'B')
    fade = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 250)
    mel.connect_material_expressions(opacity, '', fade, 'A')
    mel.connect_material_expressions(vertex, 'A', fade, 'B')

    mel.connect_material_property(lit, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(fade, '', unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log(f'ToyBox: 建好了 {path}')


create_ghost()
create_arc()
