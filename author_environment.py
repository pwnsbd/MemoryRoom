"""Run in Unreal Editor Python; author only the MemoryRoom environment.

Photo actors, game mode, Blueprint classes, input and C++ remain untouched.
The original map backup is in Z:/Applications/MemoryRoom-environment/original.
"""
import unreal, math, random, json

ROOT = '/Game/MemoryRoomEnvironment'
VEGETATION = '/Game/StylizedlogCabin_A1/Landscape/Vegetation/'
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_name() == 'MemoryRoom', 'Open MemoryRoom before running this script.'
tools = unreal.AssetToolsHelpers.get_asset_tools()
report = {'assets': [], 'foliage': [], 'clear_radius_cm': 900}

def find(label):
    matches = [a for a in actors.get_all_level_actors() if a.get_actor_label() == label]
    assert len(matches) <= 1, 'Ambiguous actor: '+label
    return matches[0] if matches else None

def asset(name, cls, factory):
    path = ROOT+'/'+name
    value = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset(name, ROOT, cls, factory)
    assert value, path
    report['assets'].append(path)
    return value

def actor(label, cls, location=unreal.Vector()):
    a = find(label)
    if not a:
        a = actors.spawn_actor_from_class(cls, location)
        a.set_actor_label(label)
    a.set_folder_path('Environment')
    return a

floor = find('Floor')
assert isinstance(floor, unreal.StaticMeshActor), 'Expected the existing Floor actor.'
assert not any(a.get_actor_label() == 'MemoryRoom_MeadowAuthored' for a in actors.get_all_level_actors()), 'Environment already authored; inspect before regenerating.'

with unreal.ScopedEditorTransaction('Create MemoryRoom meadow and distant pines'):
    material = asset('MI_MeadowFloor', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(material, unreal.load_asset('/Game/StylizedlogCabin_A1/Landscape/Materials/MI_Grass_A1'))
    # 160m plane with a grass texture repeated every 2m, rather than stretched across it.
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(material, 'UVs', 80.0)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(material, 'PDO', 0.0)
    unreal.MaterialEditingLibrary.update_material_instance(material)
    floor.modify()
    floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Plane'))
    floor.static_mesh_component.set_material(0, material)
    floor.static_mesh_component.set_collision_profile_name('BlockAll')
    floor.static_mesh_component.set_cast_shadow(False)
    tags = list(floor.static_mesh_component.get_editor_property('component_tags'))
    if 'MemoryRoomAuthoredGround' not in [str(t) for t in tags]:
        floor.static_mesh_component.set_editor_property('component_tags',tags+['MemoryRoomAuthoredGround'])
    floor.set_actor_scale3d(unreal.Vector(160,160,1))
    floor.set_folder_path('Environment')
    floor_z = floor.get_actor_location().z

    # Retain the existing floor-dressing system but use authored grass instances so
    # foliage is visible/editable before Play and never duplicated on BeginPlay.
    ground = actor('MemoryRoom_MeadowGround', unreal.load_class(None, '/Script/MemoryRoom.MemoryRoomGroundActor'))
    ground.set_editor_property('grass_count', 0)

    rng = random.Random(20260906)
    def foliage(mesh_name, transforms):
        mesh = unreal.load_asset(VEGETATION+mesh_name)
        ft = asset('FT_Meadow_'+mesh_name, unreal.FoliageType_InstancedStaticMesh, unreal.FoliageType_InstancedStaticMeshFactory())
        ft.set_editor_property('mesh', mesh)
        ft.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
        ft.set_editor_property('cast_shadow', False)
        ft.set_editor_property('cast_dynamic_shadow', False)
        ft.set_editor_property('cast_static_shadow', False)
        ft.set_editor_property('affect_distance_field_lighting', False)
        ft.set_editor_property('enable_density_scaling', False)
        body = ft.get_editor_property('body_instance')
        body.set_editor_property('collision_profile_name', 'NoCollision')
        body.set_editor_property('collision_enabled', unreal.CollisionEnabled.NO_COLLISION)
        ft.set_editor_property('body_instance', body)
        unreal.InstancedFoliageActor.add_instances(world, ft, transforms)
        report['foliage'].append({'mesh': mesh_name, 'count':len(transforms)})

    grass = [[] for _ in range(4)]
    for i in range(1500):
        low, high = (950,2400) if i < 1000 else (2400,5200)
        radius = math.sqrt(rng.uniform(low*low, high*high))
        angle = rng.uniform(0,math.tau)
        # Pack clumps are 2m tall; use 20-38cm meadow grass.
        scale = rng.uniform(0.10,0.19)
        transform = unreal.Transform(location=unreal.Vector(radius*math.cos(angle),radius*math.sin(angle),floor_z+0.1), rotation=unreal.Rotator(pitch=0,yaw=rng.uniform(0,360),roll=0), scale=unreal.Vector(scale,scale,scale))
        grass[i%4].append(transform)
    for i, transforms in enumerate(grass):
        foliage('SM_Grass_A'+str(i+1), transforms)

    trees = [[] for _ in range(3)]
    for i in range(27):
        # Irregular angular spacing and two depths leave glimpses of the sky.
        angle = math.tau*i/27 + rng.uniform(-0.055,0.055)
        radius = rng.uniform(3200,4000) if i%3 else rng.uniform(4400,5000)
        variant = i%3
        mesh = unreal.load_asset(VEGETATION+'SM_Pine_A'+str(variant+1))
        bounds = mesh.get_bounds()
        height = rng.uniform(800,1250)
        scale = height/(2*bounds.box_extent.z)
        footprint = (max(bounds.box_extent.x,bounds.box_extent.y)+max(abs(bounds.origin.x),abs(bounds.origin.y)))*scale
        assert radius-footprint > 2400, 'Tree canopy too close to the gallery.'
        z = floor_z - (bounds.origin.z-bounds.box_extent.z)*scale
        trees[variant].append(unreal.Transform(location=unreal.Vector(radius*math.cos(angle),radius*math.sin(angle),z), rotation=unreal.Rotator(pitch=0,yaw=rng.uniform(0,360),roll=0), scale=unreal.Vector(scale,scale,scale)))
    for i, transforms in enumerate(trees):
        foliage('SM_Pine_A'+str(i+1), transforms)

    # Use the same sky Blueprint and its existing time-of-day/focus behavior.
    sky_cls = unreal.EditorAssetLibrary.load_blueprint_class('/Game/BPMemoryRoomSkyDome')
    assert sky_cls, 'Missing existing sky Blueprint'
    sky = actor('MemoryRoom_MeadowSky', sky_cls)
    sky_light = sky.get_component_by_class(unreal.SkyLightComponent)
    # Default capture cutoff is farther away than this project's 120m dome.
    sky_light.set_editor_property('sky_distance_threshold',8000.0)
    sky_light.set_editor_property('intensity',1.0)
    sun = sky.get_component_by_class(unreal.DirectionalLightComponent)
    sun.set_editor_property('intensity',3.0)
    sun.set_world_rotation(unreal.Rotator(pitch=-28,yaw=20,roll=0),False,False)
    sun.set_light_color(unreal.LinearColor(1.0,0.92,0.80))

    marker = actor('MemoryRoom_MeadowAuthored', unreal.Actor)
    marker.set_editor_property('is_editor_only_actor',True)
    marker.set_editor_property('tags',['MemoryRoomEnvironment','Seed20260906'])
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(unreal.Vector(0,0,170),unreal.Rotator(pitch=-8,yaw=20,roll=0))

for path in report['assets']:
    assert unreal.EditorAssetLibrary.save_asset(path,only_if_is_dirty=False), 'Failed saving '+path
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level(), 'Failed saving MemoryRoom'
report['floor_material'] = floor.static_mesh_component.get_material(0).get_path_name()
report['floor_scale'] = str(floor.get_actor_scale3d())
report['sky_capture_distance_cm'] = sky_light.get_editor_property('sky_distance_threshold')
with open('Z:/Applications/MemoryRoom-environment/authored-report.json','w') as f: json.dump(report,f,indent=2)
unreal.log('MEMORYROOM_MEADOW_AUTHORING_COMPLETE')
