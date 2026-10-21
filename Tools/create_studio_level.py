"""建默认关卡 /Game/Maps/L_Studio：里面只有一个 AToyStudio（原点）和一个 PlayerStart。

关卡只是个壳：房间、桌子、触屏都在 AToyStudio 的 C++ 构造函数里，改场景还是改 C++。
出生点由 AToyBoxGameMode 固定在工作室原点，这里的 PlayerStart 只是让关卡"看起来完整"，不起作用。

用编辑器命令行跑（-script= 遇到带空格的路径会被拆开，脚本路径走环境变量）：
  set MOTOR_IMPORT_SCRIPT=<工程>\\Tools\\create_studio_level.py
  UnrealEditor-Cmd.exe <工程>.uproject -run=pythonscript -script=__import__('runpy').run_path(__import__('os').environ['MOTOR_IMPORT_SCRIPT']) -unattended -nullrhi
"""
import unreal

LEVEL = '/Game/Maps/L_Studio'

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
    unreal.EditorAssetLibrary.delete_asset(LEVEL)
if not levels.new_level(LEVEL):
    raise RuntimeError(f'建不了关卡 {LEVEL}')

studio = actors.spawn_actor_from_class(unreal.ToyStudio, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
studio.set_actor_label('ToyStudio')
start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 100), unreal.Rotator(0, 0, 0))
start.set_actor_label('PlayerStart')

if not levels.save_current_level():
    raise RuntimeError('关卡保存失败')
names = [a.get_actor_label() for a in actors.get_all_level_actors()]
unreal.log(f'ToyBox: 建好 {LEVEL}，里面的 Actor：{names}')
