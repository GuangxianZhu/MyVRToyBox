"""把 Tools/audio/out/*.wav 导入成 /Game/Audio 下的 SoundWave（导入音效.bat 调用）。

在编辑器里跑：UnrealEditor-Cmd.exe VRToyBox.uproject -run=pythonscript -script=...
文件名里带 Loop 的设成循环播放（伺服电机声）。已有的同名资产直接覆盖。
"""
import glob
import os

import unreal

SRC = os.path.join(os.path.dirname(os.path.abspath(os.environ.get('SOUND_IMPORT_SCRIPT', __file__))), 'out')
DEST = '/Game/Audio'

files = sorted(glob.glob(os.path.join(SRC, '*.wav')))
if not files:
    raise RuntimeError(f'没有 wav：{SRC}（先跑 python Tools/audio/make_sounds.py）')

tasks = []
for path in files:
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = DEST
    task.automated = True
    task.replace_existing = True
    task.save = False
    tasks.append(task)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

count = 0
for path in files:
    name = os.path.splitext(os.path.basename(path))[0]
    asset_path = f'{DEST}/{name}'
    sound = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not sound:
        unreal.log_error(f'导入失败：{name}')
        continue
    sound.set_editor_property('looping', 'Loop' in name)
    unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
    count += 1
unreal.log(f'ToyBox 音效：导入 {count} / {len(files)} 个到 {DEST}')
