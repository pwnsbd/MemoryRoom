import unreal,json
ROOT='/Game/MemoryRoomEnvironment'
tools=unreal.AssetToolsHelpers.get_asset_tools()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report=[]
cache={}
def variant(original):
    name=original.get_name()
    if name in cache:return cache[name]
    if 'PineLeaf' not in name and 'Grass_A' not in name:return original
    new_name='MI_Meadow_'+name.removeprefix('MI_')
    path=ROOT+'/'+new_name
    mi=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset(new_name,ROOT,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(mi,original)
    pine='PineLeaf' in name
    colors=[(0.035,0.115,0.045),(0.045,0.145,0.050),(0.050,0.130,0.030),(0.070,0.160,0.045)]
    idx=int(name[-1])-1
    base=(0.028,0.110,0.042) if pine else colors[idx]
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi,'Base Color',unreal.LinearColor(*base,1))
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi,'Sub Color' if pine else 'Base Color SS',unreal.LinearColor(0.06,0.19,0.055,1))
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi,'Emissive Amount' if pine else 'Emissive',0.0)
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi,'Roughness',0.9)
    if not pine:
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi,'Sub',0.4)
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi,'PDO',0.0)
    unreal.MaterialEditingLibrary.update_material_instance(mi)
    unreal.EditorAssetLibrary.save_asset(path,only_if_is_dirty=False)
    cache[name]=mi; report.append(path)
    return mi
with unreal.ScopedEditorTransaction('Soften meadow foliage materials'):
    for group in ['Grass_A1','Grass_A2','Grass_A3','Grass_A4','Pine_A1','Pine_A2','Pine_A3']:
        ft=unreal.load_asset(ROOT+'/FT_Meadow_SM_'+group)
        mesh=ft.get_editor_property('mesh')
        mats=[variant(mesh.get_material(i)) for i in range(len(mesh.static_materials))]
        ft.set_editor_property('override_materials',mats)
        unreal.EditorAssetLibrary.save_loaded_asset(ft,only_if_is_dirty=False)
    # Ensure the current editor components reflect the foliage-type material overrides.
    for a in actors.get_all_level_actors():
        if isinstance(a,unreal.InstancedFoliageActor):
            for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
                if not c.static_mesh:continue
                ft=unreal.load_asset(ROOT+'/FT_Meadow_'+c.static_mesh.get_name())
                if ft:
                    for i,m in enumerate(ft.get_editor_property('override_materials')):c.set_material(i,m)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(unreal.Vector(0,0,170),unreal.Rotator(pitch=-5,yaw=20,roll=0))
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
with open('Z:/Applications/MemoryRoom-environment/tuned-materials.json','w') as f:json.dump(report,f,indent=2)
unreal.log('MEMORYROOM_MEADOW_MATERIALS_TUNED')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
