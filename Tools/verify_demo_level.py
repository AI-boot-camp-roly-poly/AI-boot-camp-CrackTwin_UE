"""Use full editor -ExecutePythonScript (commandlets lack the needed physics scene).

Reload the saved level and verify geometry and capture traces; no PIE input.
"""
import json
import unreal

LEVEL = "/Game/Maps/L_DemoBuilding"
world = unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
assert world, "Saved level failed to load"
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
by_label = {actor.get_actor_label(): actor for actor in actors.get_all_level_actors()}
for label in ["DemoBuilding", "StructureOrigin", "Ground", "Sun", "SkyAtmosphere", "SkyLight", "DroneStart"]:
    assert label in by_label, f"Missing actor: {label}"
center, extent = by_label["DemoBuilding"].get_actor_bounds(False)
assert (center - unreal.Vector(0, 0, 750)).length() < 0.1
assert (extent - unreal.Vector(1000, 500, 750)).length() < 0.1
assert by_label["DemoBuilding"].static_mesh_component.get_collision_profile_name() == "BlockAll"
assert by_label["StructureOrigin"].get_actor_location().length() < 0.1
start = by_label["DroneStart"]
assert (start.get_actor_location() - unreal.Vector(-1500, 0, 400)).length() < 0.1
assert abs(start.get_actor_rotation().yaw) < 0.1

pawn_class = unreal.load_class(None, "/Script/CrackTwin.CrackDronePawn")
pawn = actors.spawn_actor_from_class(pawn_class, start.get_actor_location(), start.get_actor_rotation())
try:
    wall = json.loads(pawn.build_capture_result_json("verify-wall"))
    unreal.log("CRACKTWIN_TRACE_WALL: " + json.dumps(wall))
    assert wall["hitOk"] and abs(wall["distanceCm"] - 500) < 0.1, wall
    pawn.camera.set_world_rotation(unreal.Rotator(pitch=80, yaw=0, roll=0), False, False)
    unreal.log(f"CRACKTWIN_SKY_CAMERA: {pawn.camera.get_world_rotation()}")
    sky = json.loads(pawn.build_capture_result_json("verify-sky"))
    unreal.log("CRACKTWIN_TRACE_SKY: " + json.dumps(sky))
    assert not sky["hitOk"] and sky["distanceCm"] == 0, sky
    assert all(sky["hit"][axis] == 0 for axis in ["x", "y", "z"]), sky
    pawn.camera.set_world_rotation(unreal.Rotator(pitch=0, yaw=0, roll=0), False, False)
    pawn.set_actor_location(unreal.Vector(0, 0, 400), True, False)
    stopped = pawn.get_actor_location()
    unreal.log(f"CRACKTWIN_SWEEP_STOP: {stopped}")
    assert -1040 < stopped.x <= -1034.9, stopped
finally:
    actors.destroy_actor(pawn)
unreal.log("CRACKTWIN_SAVED_LEVEL_VERIFIED: geometry, origin, start, wall/sky traces, pawn sweep passed; input not tested")
