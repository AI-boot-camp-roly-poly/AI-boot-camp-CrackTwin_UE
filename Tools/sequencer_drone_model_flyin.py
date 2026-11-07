# LS_2: Drone 모델(Body_Main + 부착된 렌즈·프로펠러)이 화면 앞에서 날아가
#       LS_3 첫 프레임 카메라 위치에 "드론 렌즈"가 오도록 도착한다.
#       LS_2 카메라가 끝에서 LS_3 위치로 들어가면 드론 렌즈로 들어가는 전환이 되고, LS_3는 드론 시점이 된다.
# 실행: 에디터 하단 Cmd 창에서  py "C:/AIBootcamp/CrackTwin/Tools/sequencer_drone_model_flyin.py"
# 되돌리기: Ctrl+Z 한 번. 백업: Tools/SequenceBackup/ (LS_2, Main.umap)
#
# 전제 (2026-11-07 MCP로 설정·재조회 확인)
#   - Body_Main(StaticMeshActor_2, 사용자가 지운 _23 대신 같은 메시로 재배치)과 렌즈2·프로펠러4가 Movable, 6개 부품이 Body_Main.StaticMeshComponent0에 부착.
#   - 메시 기수(렌즈 쪽)는 로컬 +Y. 피벗은 기체 바닥 중앙. 스케일 2.5에서 렌즈 중심 = 피벗 + (0, 61.3, 26.6)cm.
#     그래서 액터 yaw = 카메라 yaw - 90 이면 기수가 카메라 정면을 본다.
# 수치 근거
#   - 출발: LS_2 카메라 첫 키에서 정면 600cm, 아래 80cm. 첫 화면에 드론이 화면 중앙 아래쪽에 크게 잡힌다.
#     카메라와 드론이 같은 곡선(smootherstep)으로 움직여 비행 내내 드론이 카메라 앞에 남고 거리만 줄어든다.
#   - 방향: 출발 때는 진행 방향을 보고, 도착 때 LS_3 카메라 방향으로 돈다.
#   - 기울기: 가속 때 기수 내림, 감속 때 기수 듦, 최대 8도. 실제 쿼드콥터가 앞으로 갈 때 기우는 모습.
#     UE roll 양수 = +Y(기수) 아래. 반대로 보이면 TILT_MAX_DEG 부호를 바꾼다.
#   - 호버 흔들림: 상하 ±3cm, 시작·끝 0.
import math
import unreal

SEQ_PATH = "/Game/Video/LS_2"
TARGET_PATH = "/Game/Video/LS_3"
DRONE_LABEL = "Body_Main"
LENS_OFFSET = (0.0, 61.3, 26.6)  # 액터 로컬(회전 0) 기준 cm, 스케일 포함
START_AHEAD_CM = 600.0
START_DROP_CM = 80.0
TILT_MAX_DEG = 8.0
BOB_CM = 3.0
KEY_STEP = 5

OLD_V1 = {  # LS_3 hover v1 보정 (sequencer_drone_hover.py)
    "Location.X": [(2.0, 1, 0.0), (0.8, 3, 1.3)],
    "Location.Y": [(2.5, 2, 0.7), (0.8, 5, 2.1)],
    "Location.Z": [(3.5, 2, 1.9), (1.0, 4, 0.4)],
    "Rotation.X": [(0.5, 2, 0.3), (0.15, 5, 1.1)],
    "Rotation.Y": [(0.3, 1, 2.4), (0.1, 4, 0.9)],
    "Rotation.Z": [(0.3, 2, 1.6), (0.1, 3, 2.8)],
}
NAMES = ("Location.X", "Location.Y", "Location.Z", "Rotation.X", "Rotation.Y", "Rotation.Z")


def smootherstep(t):
    return t * t * t * (t * (t * 6 - 15) + 10)


def wrap180(a):
    return (a + 180.0) % 360.0 - 180.0


def camera_section(seq):
    for b in seq.get_bindings():
        if "Camera" in str(b.get_display_name()):
            for tr in b.get_tracks():
                if isinstance(tr, unreal.MovieScene3DTransformTrack) and tr.get_sections():
                    return tr.get_sections()[0]
    return None


def first_values(section, correct_v1=False):
    out = {}
    for ch in section.get_all_channels():
        n = str(ch.channel_name)
        if n not in NAMES:
            continue
        keys = ch.get_keys()
        if keys:
            v = keys[0].get_value()
            if correct_v1 and len(keys) == 31:
                v -= sum(a * math.sin(p) for a, c, p in OLD_V1[n])
        elif ch.has_default():
            v = ch.get_default()
        else:
            raise RuntimeError(f"{n} 값이 없습니다.")
        out[n] = v
    return out


def rot_z(v, yaw_deg):
    r = math.radians(yaw_deg)
    c, s = math.cos(r), math.sin(r)
    return (v[0] * c - v[1] * s, v[0] * s + v[1] * c, v[2])


def forward(yaw_deg, pitch_deg):
    y, p = math.radians(yaw_deg), math.radians(pitch_deg)
    return (math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), math.sin(p))


def run():
    seq = unreal.load_asset(SEQ_PATH)
    tgt = unreal.load_asset(TARGET_PATH)
    cam2, cam3 = camera_section(seq), camera_section(tgt)
    if not cam2 or not cam3:
        raise RuntimeError("LS_2 또는 LS_3 카메라 Transform 트랙을 찾지 못했습니다.")
    c2, c3 = first_values(cam2), first_values(cam3, correct_v1=True)

    drone = None
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if a.get_actor_label() == DRONE_LABEL:
            drone = a
            break
    if not drone:
        raise RuntimeError(f"레벨에서 {DRONE_LABEL} 액터를 찾지 못했습니다.")
    if drone.root_component.mobility != unreal.ComponentMobility.MOVABLE:
        raise RuntimeError(f"{DRONE_LABEL} Mobility가 Movable이 아닙니다.")

    # 도착: 렌즈 중심이 LS_3 카메라 위치, 기수가 LS_3 카메라 yaw
    end_yaw = c3["Rotation.Z"] - 90.0
    off = rot_z(LENS_OFFSET, end_yaw)
    end = (c3["Location.X"] - off[0], c3["Location.Y"] - off[1], c3["Location.Z"] - off[2])

    # 출발: LS_2 첫 카메라 정면
    f = forward(c2["Rotation.Z"], c2["Rotation.Y"])
    start = (c2["Location.X"] + f[0] * START_AHEAD_CM,
             c2["Location.Y"] + f[1] * START_AHEAD_CM,
             c2["Location.Z"] + f[2] * START_AHEAD_CM - START_DROP_CM)
    travel_yaw = math.degrees(math.atan2(end[1] - start[1], end[0] - start[0]))
    start_yaw = travel_yaw - 90.0
    end_yaw = start_yaw + wrap180(end_yaw - start_yaw)

    # 바인딩: 같은 이름의 옛 바인딩은 지운다(Body_Main을 지우고 다시 배치하면 옛 바인딩이 빈 채로 남는다).
    for b in list(seq.get_bindings()):
        if str(b.get_display_name()) == DRONE_LABEL:
            b.remove()
    binding = seq.add_possessable(drone)
    track = None
    for tr in binding.get_tracks():
        if isinstance(tr, unreal.MovieScene3DTransformTrack):
            track = tr
            break
    if not track:
        track = binding.add_track(unreal.MovieScene3DTransformTrack)
    section = track.get_sections()[0] if track.get_sections() else track.add_section()

    s0, s1 = seq.get_playback_start(), seq.get_playback_end()
    length = max(1, s1 - s0)

    def pose(t):
        s = smootherstep(t)
        env = math.sin(math.pi * t)
        x = start[0] + (end[0] - start[0]) * s
        y = start[1] + (end[1] - start[1]) * s
        z = start[2] + (end[2] - start[2]) * s + BOB_CM * env * math.sin(2 * math.pi * 3 * t)
        yaw = start_yaw + (end_yaw - start_yaw) * s
        tilt = TILT_MAX_DEG * math.sin(2 * math.pi * t) * env  # 가속(+) 감속(-)
        return {"Location.X": x, "Location.Y": y, "Location.Z": z,
                "Rotation.X": tilt, "Rotation.Y": 0.0, "Rotation.Z": yaw}

    unit = unreal.MovieSceneTimeUnit.DISPLAY_RATE
    interp = unreal.MovieSceneKeyInterpolation.AUTO
    frames = list(range(s0, s1, KEY_STEP)) + [s1]
    with unreal.ScopedEditorTransaction("Drone model fly-in LS_2"):
        seq.modify(True)
        section.set_range(s0, s1)
        chans = {str(ch.channel_name): ch for ch in section.get_all_channels()}
        for n in NAMES:
            ch = chans[n]
            for k in list(ch.get_keys()):
                ch.remove_key(k)
            for fr in frames:
                ch.add_key(unreal.FrameNumber(fr), pose((fr - s0) / length)[n], 0.0, unit, interp)

    unreal.LevelSequenceEditorBlueprintLibrary.open_level_sequence(seq)
    unreal.LevelSequenceEditorBlueprintLibrary.refresh_current_level_sequence()
    dist = math.dist(start, end)
    unreal.log(f"[DroneModel] start={tuple(round(v, 1) for v in start)} end={tuple(round(v, 1) for v in end)} "
               f"dist={dist:.0f}cm yaw {start_yaw:.1f}->{end_yaw:.1f} frames {s0}-{s1}")
    unreal.log("[DroneModel] 완료. 확인 후 LS_2와 Main 레벨 저장(Ctrl+Shift+S 또는 모두 저장).")


run()
