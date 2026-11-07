# LS_2: 카메라가 LS_2 시작 위치에서 드론처럼 날아가 LS_3 첫 프레임 카메라 위치에 도착하게 키를 넣는다.
# 실행: 에디터 하단 Cmd 창에서  py "C:/AIBootcamp/CrackTwin/Tools/sequencer_drone_flyin.py"
# 되돌리기: Ctrl+Z 한 번. 원본 백업: Tools/SequenceBackup/LS_2_20261107_before_flyin.uasset
#
# 동작
#   - 시작값: LS_2 Transform 트랙의 첫 키(없으면 LS_2 시작 프레임 기준 카메라 현재값).
#   - 도착값: LS_3 Transform 트랙의 첫 키. LS_2 마지막 프레임이 LS_3 첫 프레임과 같아 컷이 이어진다.
#   - LS_2 위치/회전 채널의 기존 키는 지우고 다시 넣는다(Scale 채널은 건드리지 않는다).
#   - 렌즈 값(CurrentFocalLength·CurrentAperture·ManualFocusDistance 등)이 LS_3와 다르면 같은 곡선으로 맞춘다.
# 수치 근거
#   - 이동 곡선: smootherstep(가속→순항→감속). 드론이 출발과 도착에서 서서히 붙는 느낌을 준다.
#   - 상승 아치: 이동 거리의 4%(최대 150cm)만큼 중간에 살짝 떠올랐다 내려온다. 직선 보간의 기계적인 느낌을 줄인다.
#   - 비행 중 흔들림: LS_3 v2 호버 수준(위치 ±1.5cm, roll ±0.3도). sin(pi*t)로 감싸 시작·끝에서 0이라 도착값이 정확하다.
#   - 이동 방향으로 살짝 기울기(roll, 최대 1.5도): 드론이 옆으로 갈 때 기체가 기우는 느낌. 시작·끝에서 0.
#   - 키는 5프레임마다, Auto 보간.
import math
import unreal

SRC_PATH = "/Game/Video/LS_2"
DST_PATH = "/Game/Video/LS_3"
KEY_STEP = 5
ARC_RATIO = 0.04
ARC_MAX_CM = 150.0
BANK_MAX_DEG = 1.5

# LS_3 hover v1이 남아 있을 때 첫 키 보정(sequencer_drone_hover.py와 같은 값)
OLD_V1 = {
    "Location.X": [(2.0, 1, 0.0), (0.8, 3, 1.3)],
    "Location.Y": [(2.5, 2, 0.7), (0.8, 5, 2.1)],
    "Location.Z": [(3.5, 2, 1.9), (1.0, 4, 0.4)],
    "Rotation.X": [(0.5, 2, 0.3), (0.15, 5, 1.1)],
    "Rotation.Y": [(0.3, 1, 2.4), (0.1, 4, 0.9)],
    "Rotation.Z": [(0.3, 2, 1.6), (0.1, 3, 2.8)],
}
OLD_V1_KEY_COUNT = 31

# 비행 중 흔들림 (진폭, 주기 수). 전체에 sin(pi*t) 감쇠를 곱한다.
FLIGHT_WOBBLE = {
    "Location.X": [(1.5, 3)],
    "Location.Y": [(-1.2, 2)],
    "Location.Z": [(1.5, 4)],
    "Rotation.X": [(0.3, 2)],
    "Rotation.Y": [(0.15, 3)],
    "Rotation.Z": [(-0.15, 2)],
}

POS = ("Location.X", "Location.Y", "Location.Z")
ROT = ("Rotation.X", "Rotation.Y", "Rotation.Z")


def smootherstep(t):
    return t * t * t * (t * (t * 6 - 15) + 10)


def wrap180(a):
    return (a + 180.0) % 360.0 - 180.0


def all_bindings(seq):
    out = []

    def walk(b):
        out.append(b)
        for c in b.get_child_possessables():
            walk(c)

    for b in seq.get_bindings():
        walk(b)
    # get_bindings가 자식까지 돌려주는 버전이면 중복 제거
    seen, uniq = set(), []
    for b in out:
        key = str(b.get_id())
        if key not in seen:
            seen.add(key)
            uniq.append(b)
    return uniq


def camera_transform_section(seq, create):
    for b in all_bindings(seq):
        if "Camera" not in str(b.get_display_name()):
            continue
        for tr in b.get_tracks():
            if isinstance(tr, unreal.MovieScene3DTransformTrack):
                secs = tr.get_sections()
                return secs[0] if secs else (tr.add_section() if create else None)
        if create and isinstance(b, unreal.MovieSceneBindingProxy) and "Component" not in str(b.get_display_name()):
            tr = b.add_track(unreal.MovieScene3DTransformTrack)
            return tr.add_section()
    return None


def channels_by_name(section):
    return {str(ch.channel_name): ch for ch in section.get_all_channels()}


def first_value(ch, name, correct_v1=False):
    keys = ch.get_keys()
    if keys:
        v = keys[0].get_value()
        if correct_v1 and len(keys) == OLD_V1_KEY_COUNT:
            v -= sum(a * math.sin(p) for a, c, p in OLD_V1.get(name, []))
        return v
    if ch.has_default():
        return ch.get_default()
    return None


def float_tracks(seq):
    out = {}
    for b in all_bindings(seq):
        for tr in b.get_tracks():
            if isinstance(tr, unreal.MovieSceneFloatTrack):
                secs = tr.get_sections()
                if secs:
                    out[str(tr.get_property_name())] = secs[0]
    return out


def run():
    src = unreal.load_asset(SRC_PATH)
    dst = unreal.load_asset(DST_PATH)
    if not src or not dst:
        raise RuntimeError("LS_2 또는 LS_3를 불러오지 못했습니다.")

    dst_sec = camera_transform_section(dst, create=False)
    if not dst_sec:
        raise RuntimeError("LS_3에서 카메라 Transform 트랙을 찾지 못했습니다.")
    src_sec = camera_transform_section(src, create=True)
    if not src_sec:
        raise RuntimeError("LS_2에서 카메라 바인딩을 찾지 못했습니다.")

    start, end = src.get_playback_start(), src.get_playback_end()
    length = max(1, end - start)
    src_ch, dst_ch = channels_by_name(src_sec), channels_by_name(dst_sec)

    a, b = {}, {}
    for n in POS + ROT:
        b[n] = first_value(dst_ch[n], n, correct_v1=True)
        a[n] = first_value(src_ch[n], n)
        if b[n] is None:
            raise RuntimeError(f"LS_3 {n} 값이 없습니다.")
        if a[n] is None:
            a[n] = b[n]
    for n in ROT:  # 가까운 방향으로 회전
        b[n] = a[n] + wrap180(b[n] - a[n])

    dist = math.sqrt(sum((b[n] - a[n]) ** 2 for n in POS))
    arc = min(ARC_MAX_CM, dist * ARC_RATIO)
    # 이동 방향을 카메라 기준 좌우로 나눠 기울기 방향 결정
    yaw = math.radians(a["Rotation.Z"])
    right = (-math.sin(yaw), math.cos(yaw))
    lateral = (b["Location.X"] - a["Location.X"]) * right[0] + (b["Location.Y"] - a["Location.Y"]) * right[1]
    bank = BANK_MAX_DEG * max(-1.0, min(1.0, lateral / max(dist, 1.0)))

    def value(n, t):
        s = smootherstep(t)
        v = a[n] + (b[n] - a[n]) * s
        env = math.sin(math.pi * t)
        if n == "Location.Z":
            v += arc * env
        if n == "Rotation.X":
            # 가속 구간에 기울고 감속 구간에 반대로 살짝 세운다
            v += bank * math.sin(2 * math.pi * t) * env
        v += env * sum(amp * math.sin(2 * math.pi * c * t) for amp, c in FLIGHT_WOBBLE.get(n, []))
        return v

    unit = unreal.MovieSceneTimeUnit.DISPLAY_RATE
    interp = unreal.MovieSceneKeyInterpolation.AUTO
    frames = list(range(start, end, KEY_STEP)) + [end]
    report = []
    with unreal.ScopedEditorTransaction("Drone fly-in LS_2 -> LS_3"):
        src.modify(True)
        src_sec.set_range(start, end)
        for n in POS + ROT:
            ch = src_ch[n]
            for k in list(ch.get_keys()):
                ch.remove_key(k)
            for f in frames:
                ch.add_key(unreal.FrameNumber(f), value(n, (f - start) / length), 0.0, unit, interp)
            report.append(f"{n}: {a[n]:.2f} -> {b[n]:.2f}")

        # 렌즈 값 맞추기
        dst_f, src_f = float_tracks(dst), float_tracks(src)
        for prop, dsec in dst_f.items():
            ssec = src_f.get(prop)
            if not ssec:
                continue
            dch, sch = dsec.get_all_channels()[0], ssec.get_all_channels()[0]
            dv, sv = first_value(dch, prop), first_value(sch, prop)
            if dv is None or sv is None or abs(dv - sv) < 1e-3:
                continue
            for k in list(sch.get_keys()):
                sch.remove_key(k)
            for f in frames:
                sch.add_key(unreal.FrameNumber(f), sv + (dv - sv) * smootherstep((f - start) / length), 0.0, unit, interp)
            report.append(f"{prop}: {sv:.2f} -> {dv:.2f}")

    unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(src)
    unreal.LevelSequenceEditorBlueprintLibrary.refresh_current_level_sequence()
    unreal.log(f"[DroneFlyIn] 거리 {dist:.0f}cm, 아치 {arc:.0f}cm, 기울기 {bank:.2f}도, frames {start}-{end}")
    for line in report:
        unreal.log(f"[DroneFlyIn] {line}")
    unreal.log("[DroneFlyIn] 완료. 확인 후 Ctrl+S(또는 LS_2 저장).")


run()
