"""Run with UE 5.8 -run=pythonscript -script=<this file>."""
import unreal

LEVEL = "/Game/Maps/L_DemoBuilding"


def create_level():
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        raise RuntimeError(f"Refusing to overwrite existing level: {LEVEL}")
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    if not world:
        raise RuntimeError("Could not create blank map")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")

    def spawn(actor_class, label, location, rotation=None):
        actor = actors.spawn_actor_from_class(
            actor_class, unreal.Vector(*location),
            unreal.Rotator(*(rotation or (0, 0, 0))))
        actor.set_actor_label(label)
        return actor

    def box(label, location, scale):
        actor = spawn(unreal.StaticMeshActor, label, location)
        component = actor.static_mesh_component
        component.set_static_mesh(cube)
        component.set_collision_profile_name("BlockAll")
        actor.set_actor_scale3d(unreal.Vector(*scale))
        return actor

    # UE cube is 100 cm. Structure footprint is centered on the world origin;
    # the mesh center is at z=750 so the bottom is exactly z=0.
    structure = box("DemoBuilding", (0, 0, 750), (20, 10, 15))
    structure.tags = ["Structure_1"]
    origin = spawn(unreal.Actor, "StructureOrigin", (0, 0, 0))
    origin.tags = ["StructureOrigin_1"]
    box("Ground", (0, 0, -25), (100, 100, 0.5))
    sun = spawn(unreal.DirectionalLight, "Sun", (0, 0, 2500), (-40, -30, 0))
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    sun.light_component.set_editor_property("intensity", 5.0)
    spawn(unreal.SkyAtmosphere, "SkyAtmosphere", (0, 0, 0))
    sky = spawn(unreal.SkyLight, "SkyLight", (0, 0, 2000))
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    # Front is the -X facade at x=-1000. Start 500 cm away, facing +X.
    start = spawn(unreal.PlayerStart, "DroneStart", (-1500, 0, 400))

    center, extent = structure.get_actor_bounds(False)
    assert (center - unreal.Vector(0, 0, 750)).length() < 0.1, center
    assert (extent - unreal.Vector(1000, 500, 750)).length() < 0.1, extent
    assert structure.static_mesh_component.get_collision_profile_name() == "BlockAll"
    assert (start.get_actor_location() - unreal.Vector(-1500, 0, 400)).length() < 0.1
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, LEVEL), "save_map failed"
    unreal.log("CRACKTWIN_LEVEL_CREATED: " + LEVEL)
    unreal.log(f"CRACKTWIN_LEVEL_VERIFIED: center={center}, extent={extent}, collision=BlockAll, start={start.get_actor_location()}")


create_level()
