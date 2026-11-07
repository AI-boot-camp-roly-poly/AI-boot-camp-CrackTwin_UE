"""Run in the Unreal Cmd console: py "C:/AIBootcamp/CrackTwin/Tools/render_level_sequences.py"

Exports FinalImage PNGs directly from the renderer, bypassing FrameGrabber.
Convert the resulting frames with Tools/encode_sequence_videos.py afterwards.
Duplicates the current editor world into PIE, including unsaved actor changes.
Does not save or modify the source level or sequence assets.
"""
import datetime
import json
from pathlib import Path
import sys
import types

import unreal


class SequenceVideoBatch:
    def __init__(self):
        self.names = ["LS_1", "LS_2", "LS_3"]
        self.index = 0
        self.capture = None
        self.tick = None
        self.callback = unreal.OnRenderMovieStopped()
        self.callback.bind_callable(self.finished)
        self.root = Path(unreal.Paths.project_dir()).resolve() / "Exports" / (
            "SequencesFixed_" + datetime.datetime.now().strftime("%Y%m%d_%H%M%S_%f"))
        self.report = {"status": "preflight", "protocol": "CompositionGraph.FinalImage",
                       "resolution": [1920, 1080], "jobs": []}

    def write_report(self):
        (self.root / "render_status.json").write_text(
            json.dumps(self.report, ensure_ascii=False, indent=2), encoding="utf-8")

    def start(self):
        if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
            raise RuntimeError("Stop PIE before rendering.")
        if unreal.SequencerTools.is_rendering_movie():
            raise RuntimeError("Another movie render is already running.")
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        self.report["map"] = world.get_path_name()
        for name in self.names:
            seq = unreal.load_asset("/Game/Video/" + name)
            if not isinstance(seq, unreal.LevelSequence):
                raise RuntimeError("Missing LevelSequence: " + name)
            if seq.get_playback_end() <= seq.get_playback_start():
                raise RuntimeError("Empty playback range: " + name)
            if not any(isinstance(t, unreal.MovieSceneCameraCutTrack) and t.get_sections()
                       for t in seq.get_tracks()):
                raise RuntimeError("Missing Camera Cuts: " + name)
            rate = seq.get_display_rate()
            self.report["jobs"].append({
                "name": name, "status": "queued",
                "fps": [rate.numerator, rate.denominator],
                "start": seq.get_playback_start(), "end": seq.get_playback_end()})
        self.root.mkdir(parents=True, exist_ok=False)
        self.next_job()

    def next_job(self):
        if self.index == len(self.names):
            self.report["status"] = "frames_rendered"
            self.write_report()
            unreal.log("SEQUENCE_FRAMES_COMPLETE: " + str(self.root))
            return
        name = self.names[self.index]
        cap = unreal.AutomatedLevelSequenceCapture()
        cap.level_sequence_asset = unreal.SoftObjectPath("/Game/Video/" + name)
        settings = cap.settings
        settings.output_directory = unreal.DirectoryPath(str(self.root / name))
        settings.output_format = name + "_{frame}"
        settings.overwrite_existing = False
        settings.use_custom_frame_rate = False
        settings.use_relative_frame_numbers = True
        settings.handle_frames = 0
        settings.resolution.res_x = 1920
        settings.resolution.res_y = 1080
        settings.enable_texture_streaming = True
        # Keep current quality to avoid unexpectedly raising this city's GPU load.
        settings.cinematic_engine_scalability = False
        settings.cinematic_mode = True
        settings.allow_movement = False
        settings.allow_turning = False
        settings.show_player = False
        settings.show_hud = False
        settings.use_path_tracer = False
        cap.use_separate_process = False
        cap.close_editor_when_capture_starts = False
        cap.use_custom_start_frame = False
        cap.use_custom_end_frame = False
        cap.warm_up_frame_count = 120
        cap.delay_before_warm_up = 2.0
        cap.delay_before_shot_warm_up = 1.0
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
        protocol.disable_screen_percentage = False
        protocol.post_processing_material = unreal.SoftObjectPath()
        self.capture = cap
        self.report["status"] = "rendering"
        self.report["jobs"][self.index]["status"] = "rendering"
        self.write_report()
        if not unreal.SequencerTools.render_movie(cap, self.callback):
            self.finished(False)

    def finished(self, success):
        job = self.report["jobs"][self.index]
        files = sorted((self.root / job["name"]).glob("*.png"))
        valid = bool(success and files)
        job.update(status="frames_rendered" if valid else "failed",
                   frame_directory=str(self.root / job["name"]),
                   png_count_at_callback=len(files))
        if not valid:
            self.report["status"] = "failed"
            self.write_report()
            unreal.log_error("SEQUENCE_VIDEO_FAILED: " + job["name"] + "; " + str(self.root))
            return
        self.index += 1
        self.write_report()
        # Allow the previous PIE session/capture to finish tearing down first.
        self.tick = unreal.register_slate_post_tick_callback(self.resume)

    def resume(self, delta_seconds):
        if unreal.SequencerTools.is_rendering_movie():
            return
        if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
            return
        unreal.unregister_slate_post_tick_callback(self.tick)
        self.tick = None
        self.next_job()


key = "_cracktwin_sequence_video_batch"
previous = sys.modules.get(key)
if previous and previous.batch.report["status"] == "rendering":
    raise RuntimeError("The sequence batch is already rendering.")
state = types.ModuleType(key)
sys.modules[key] = state  # Keep callbacks/settings alive across console script execution.
state.batch = SequenceVideoBatch()
state.batch.start()
