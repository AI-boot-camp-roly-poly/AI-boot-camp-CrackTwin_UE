# 열려 있는 Level Sequence의 CineCameraActor Transform 트랙에 드론 호버링 흔들림 키를 넣는다.
# 실행: 에디터 하단 Cmd 창에서  py "C:/AIBootcamp/CrackTwin/Tools/sequencer_drone_hover.py"
# 되돌리기: Ctrl+Z 한 번 (트랜잭션 하나로 묶음). 원본 백업: Tools/SequenceBackup/LS_3_20261107_before_hover.uasset
#
# 수치 근거: 화면이 균열 근접 샷이라 큰 흔들림은 균열을 화면 밖으로 밀어낸다.
#   v1(위치 ±2~4cm, 회전 ±0.25~0.5도)은 어지럽다는 피드백으로 아래 v2 값으로 줄였다.
#   주파수는 시퀀스 길이 동안 정수 주기라 마지막 프레임이 첫 프레임과 같아 반복 재생 시 끊기지 않는다.
#   키는 10프레임마다(30fps 기준 1/3초), Auto 보간으로 부드럽게 잇는다.
import math
import unreal

KEY_STEP = 10  # 표시 프레임 단위

# v2 (2026-11-07, "덜 어지럽게"): 어지러움은 빠른 성분과 roll에서 온다.
#   빠른 성분(3~5주기)을 빼고 1~2주기(150프레임 기준 0.2~0.4Hz)만 남겼다.
#   진폭은 위치 약 절반(최대 ±2cm), 회전 약 1/3(roll ±0.15도)로 줄였다.
#   모든 성분 위상 0 → 첫·끝 프레임 오프셋 0이라 첫 키가 곧 원래 카메라 값이다(다시 실행해도 누적되지 않음).
# (진폭, 주기 수) 성분 합. 위치 cm, 회전 도.
WOBBLE = {
    "Location.X": [(1.2, 1)],
    "Location.Y": [(-1.0, 1), (0.4, 2)],
    "Location.Z": [(2.0, 1), (-0.6, 2)],   # 느린 상하 출렁임
    "Rotation.X": [(0.15, 1)],             # roll
    "Rotation.Y": [(0.1, 1)],              # pitch
    "Rotation.Z": [(-0.1, 1)],             # yaw
}

# v1 값. v1이 넣은 키(5프레임 간격, 31개)가 남아 있으면 첫 키에서 v1 오프셋을 빼 원래 값을 복원한다.
OLD_V1 = {
    "Location.X": [(2.0, 1, 0.0), (0.8, 3, 1.3)],
    "Location.Y": [(2.5, 2, 0.7), (0.8, 5, 2.1)],
    "Location.Z": [(3.5, 2, 1.9), (1.0, 4, 0.4)],
    "Rotation.X": [(0.5, 2, 0.3), (0.15, 5, 1.1)],
    "Rotation.Y": [(0.3, 1, 2.4), (0.1, 4, 0.9)],
    "Rotation.Z": [(0.3, 2, 1.6), (0.1, 3, 2.8)],
}
OLD_V1_KEY_COUNT = 31


def offset(name, t):
    return sum(a * math.sin(2.0 * math.pi * c * t) for a, c in WOBBLE.get(name, []))


def old_v1_offset0(name):
    return sum(a * math.sin(p) for a, c, p in OLD_V1.get(name, []))


def find_camera_binding(seq):
    for b in seq.get_bindings():
        for tr in b.get_tracks():
            if isinstance(tr, unreal.MovieScene3DTransformTrack) and "Camera" in str(b.get_display_name()):
                return b, tr
    for b in seq.get_bindings():
        if "Camera" in str(b.get_display_name()):
            return b, b.add_track(unreal.MovieScene3DTransformTrack)
    return None, None


def base_value(ch, actor_vals, name):
    keys = ch.get_keys()
    if keys:
        v = keys[0].get_value()
        if len(keys) == OLD_V1_KEY_COUNT:
            v -= old_v1_offset0(name)
        return v
    if ch.has_default():
        return ch.get_default()
    return actor_vals[name]


def run():
    seq = unreal.LevelSequenceEditorBlueprintLibrary.get_current_level_sequence()
    if not seq:
        raise RuntimeError("열린 Level Sequence가 없습니다. LS_3를 시퀀서에서 연 뒤 다시 실행하세요.")
    binding, track = find_camera_binding(seq)
    if not track:
        raise RuntimeError("Camera 바인딩을 찾지 못했습니다.")

    sections = track.get_sections()
    section = sections[0] if sections else track.add_section()
    section.set_range(seq.get_playback_start(), seq.get_playback_end())

    start = seq.get_playback_start()
    end = seq.get_playback_end()
    length = max(1, end - start)

    # 키가 없을 때 쓸 현재 액터 값
    actor_vals = {}
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if isinstance(a, unreal.CineCameraActor) and a.get_actor_label() == "CineCameraActor":
            l, r = a.get_actor_location(), a.get_actor_rotation()
            actor_vals = {"Location.X": l.x, "Location.Y": l.y, "Location.Z": l.z,
                          "Rotation.X": r.roll, "Rotation.Y": r.pitch, "Rotation.Z": r.yaw,
                          "Scale.X": 1.0, "Scale.Y": 1.0, "Scale.Z": 1.0}
            break

    unit = unreal.MovieSceneTimeUnit.DISPLAY_RATE
    interp = unreal.MovieSceneKeyInterpolation.AUTO
    report = []
    with unreal.ScopedEditorTransaction("Drone hover keys"):
        for ch in section.get_all_channels():
            name = str(ch.channel_name)
            if name not in WOBBLE:
                continue
            base = base_value(ch, actor_vals, name)
            for k in list(ch.get_keys()):
                ch.remove_key(k)
            frames = list(range(start, end, KEY_STEP)) + [end]
            for f in frames:
                t = (f - start) / length
                ch.add_key(unreal.FrameNumber(f), base + offset(name, t), 0.0, unit, interp)
            report.append(f"{name}: base={base:.2f}, keys={len(frames)}")

    unreal.LevelSequenceEditorBlueprintLibrary.refresh_current_level_sequence()
    for line in report:
        unreal.log(f"[DroneHover] {line}")
    unreal.log(f"[DroneHover] 완료: {seq.get_name()} frames {start}-{end}. 저장은 Ctrl+S.")


run()
