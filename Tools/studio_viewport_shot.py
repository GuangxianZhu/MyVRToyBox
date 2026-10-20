"""在编辑器里（不是命令行模式）跑：把关卡视口镜头摆到工作室后角、看着桌子，等着色器编译完，
从同一个位置用一个临时的 SceneCapture2D 拍一张图（Saved/Screenshots/editor_viewport.png；编辑器最小化时视口不重绘，
HighResShot 拍不出来，所以用场景捕获），存关卡（镜头位置跟着关卡保存，下次打开编辑器就是这个视角），然后退出编辑器。

  UnrealEditor.exe <工程>.uproject "-ExecCmds=py <本文件的绝对路径>"
"""
import math
import unreal

CAMERA = unreal.Vector(-250.0, -200.0, 230.0)     # 房间后左角、比人眼高一点
TARGET = unreal.Vector(75.0, 0.0, 80.0)           # 桌子中间
WAIT_SECONDS = 90.0                               # 第一次打开要编译着色器（灰色棋盘格），等它编完、自动曝光稳定

d = TARGET - CAMERA
yaw = math.degrees(math.atan2(d.y, d.x))
pitch = math.degrees(math.atan2(d.z, math.hypot(d.x, d.y)))
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
state = {'t': 0.0, 'step': 0}


def capture():
    world = editor.get_editor_world()
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    rotation = unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw)
    cam = actors.spawn_actor_from_class(unreal.SceneCapture2D, CAMERA, rotation, transient=True)
    rt = unreal.RenderingLibrary.create_render_target2d(world, 1920, 1080, unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB)
    comp = cam.capture_component2d
    comp.set_editor_property('fov_angle', 90.0)
    comp.set_editor_property('texture_target', rt)
    comp.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_TONE_CURVE_HDR)
    comp.capture_scene()
    folder = unreal.Paths.project_saved_dir() + 'Screenshots'
    unreal.RenderingLibrary.export_render_target(world, rt, folder, 'editor_viewport.png')
    actors.destroy_actor(cam)
    unreal.log(f'ToyBox: 视口截图 {folder}/editor_viewport.png')


def tick(delta):
    state['t'] += delta
    editor.set_level_viewport_camera_info(CAMERA, unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw))
    if state['step'] == 0 and state['t'] > WAIT_SECONDS:
        state['step'] = 1
        capture()
        state['t'] = 0.0
    elif state['step'] == 1 and state['t'] > 3.0:
        state['step'] = 2
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
        unreal.log('ToyBox: 关卡已保存（带视口镜头位置）')
        state['t'] = 0.0
    elif state['step'] == 2 and state['t'] > 3.0:
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.SystemLibrary.quit_editor()


handle = unreal.register_slate_post_tick_callback(tick)
unreal.log(f'ToyBox: 视口镜头 {CAMERA} 看向 {TARGET}（yaw {yaw:.1f} pitch {pitch:.1f}），{WAIT_SECONDS:.0f} 秒后截图')
