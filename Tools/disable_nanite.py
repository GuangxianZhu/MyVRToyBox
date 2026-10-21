"""把 /Game/Models 下所有静态网格的 Nanite 关掉。

为什么：glTF 导入时 UE 5.8 默认给每个静态网格开 Nanite。这个工程为了 VR 帧率用前向渲染、只编 SM5 着色器，
Nanite 在前向渲染下不支持，编辑器会弹"使用 Nanite 资产需要 SM6"，网格退回备用网格显示，点光源阴影也不稳定。
我们的模型每个零件只有几千个三角形，用不着 Nanite。

import_model.py 导入后会调用 disable_nanite(DEST)；已经导入过的模型用编辑器命令行跑一次（编辑器要先关掉）：
    UnrealEditor-Cmd.exe VRToyBox.uproject -run=pythonscript -script=...（同 scripts/import_*.bat 的写法，脚本路径走环境变量）
"""
import unreal


def disable_nanite(folder='/Game/Models'):
    changed = 0
    total = 0
    for path in unreal.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(asset, unreal.StaticMesh):
            continue
        total += 1
        settings = asset.get_editor_property('nanite_settings')
        if not settings.get_editor_property('enabled'):
            continue
        settings.set_editor_property('enabled', False)
        asset.set_editor_property('nanite_settings', settings)
        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
        changed += 1
    unreal.log(f'ToyBox: {folder} 下 {total} 个静态网格，关掉了 {changed} 个的 Nanite')
    return changed


if __name__ == '__main__' or __name__ == '<run_path>':
    disable_nanite()
