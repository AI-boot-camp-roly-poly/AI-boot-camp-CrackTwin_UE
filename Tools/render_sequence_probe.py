"""Diagnostic B: export LS_1 frames 0..2 directly from the renderer.

Cmd: py "C:/AIBootcamp/CrackTwin/Tools/render_sequence_probe.py"
Bypasses FFrameGrabber, shared by the broken AVI and standard PNG exporters.
No source level/sequence changes; no plugin enablement or editor restart.
"""
import datetime
from pathlib import Path
import sys
import types
import unreal

if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
    raise RuntimeError("Stop PIE before running the capture probe.")
if unreal.SequencerTools.is_rendering_movie():
    raise RuntimeError("Another movie capture is running.")

state = types.ModuleType("_cracktwin_capture_probe")
sys.modules[state.__name__] = state
state.root = Path(unreal.Paths.project_dir()).resolve() / "Exports" / (
    "RenderPassProbe_" + datetime.datetime.now().strftime("%Y%m%d_%H%M%S_%f"))
state.root.mkdir(parents=True, exist_ok=False)
state.capture = unreal.AutomatedLevelSequenceCapture()
cap = state.capture
cap.level_sequence_asset = unreal.SoftObjectPath("/Game/Video/LS_1")
settings = cap.settings
settings.output_directory = unreal.DirectoryPath(str(state.root))
settings.output_format = "LS_1_{frame}"
settings.overwrite_existing = False
settings.use_custom_frame_rate = True
settings.custom_frame_rate = unreal.FrameRate(30, 1)
settings.use_relative_frame_numbers = True
settings.handle_frames = 0
settings.resolution.res_x = 1920
settings.resolution.res_y = 1080
settings.enable_texture_streaming = True
settings.cinematic_engine_scalability = False
settings.cinematic_mode = True
settings.allow_movement = False
settings.allow_turning = False
settings.show_player = False
settings.show_hud = False
settings.use_path_tracer = False
cap.use_separate_process = False
cap.close_editor_when_capture_starts = False
cap.use_custom_start_frame = True
cap.use_custom_end_frame = True
cap.custom_start_frame = unreal.FrameNumber(0)
cap.custom_end_frame = unreal.FrameNumber(3)
cap.warm_up_frame_count = 30
cap.delay_before_warm_up = 1.0
cap.delay_before_shot_warm_up = 0.0
cap.write_edit_decision_list = False
burn = unreal.LevelSequenceBurnInOptions()
burn.use_burn_in = False
cap.burn_in_options = burn
cap.set_image_capture_protocol_type(unreal.load_class(
    None, "/Script/MovieSceneCapture.CompositionGraphCaptureProtocol"))
protocol = cap.get_image_capture_protocol()
passes = unreal.CompositionGraphCapturePasses()
passes.value = ["FinalImage"]
protocol.include_render_passes = passes
protocol.capture_frames_in_hdr = False
# Preserve screen percentage behavior while isolating the capture method.
protocol.disable_screen_percentage = False
protocol.post_processing_material = unreal.SoftObjectPath()


def probe_finished(success):
    files = list(state.root.rglob("*.png"))
    message = "RENDER_PASS_PROBE: success=" + str(success) + "; PNG=" + str(len(files))
    message += "; " + str(state.root)
    (state.root / "capture_status.txt").write_text(message, encoding="utf-8")
    unreal.log(message)


state.callback = unreal.OnRenderMovieStopped()
state.callback.bind_callable(probe_finished)
if not unreal.SequencerTools.render_movie(cap, state.callback):
    raise RuntimeError("Capture probe could not start.")
