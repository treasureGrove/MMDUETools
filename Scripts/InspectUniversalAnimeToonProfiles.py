import unreal


ACTOR_LABEL = "MMD_Reimport_CleanValidation"
MATERIAL_FOLDER = "/Game/MMDToonRebuild/CatMaid/Materials"


def inspect_profiles():
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = next(
        (
            item
            for item in actor_subsystem.get_all_level_actors()
            if item.get_actor_label() == ACTOR_LABEL
        ),
        None,
    )
    if not actor:
        raise RuntimeError(f"找不到验证角色: {ACTOR_LABEL}")

    components = actor.get_components_by_class(unreal.SkeletalMeshComponent)
    if not components:
        raise RuntimeError("验证角色没有 SkeletalMeshComponent")
    skeletal_mesh = components[0].get_editor_property("skeletal_mesh_asset")
    if not skeletal_mesh:
        raise RuntimeError("验证角色没有 SkeletalMesh 资产")

    source_materials = [
        entry.get_editor_property("material_interface")
        for entry in skeletal_mesh.get_editor_property("materials")
    ]
    for slot_index, source_material in enumerate(source_materials):
        path = f"{MATERIAL_FOLDER}/MI_UniversalAnimeToon_{slot_index:02d}"
        material = unreal.load_asset(path)
        if not material:
            unreal.log_error(f"[UniversalAnimeToonInspect] missing={path}")
            continue

        profile = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(
            material, "surface_profile"
        )
        source_name = source_material.get_name() if source_material else "None"
        base_name = "None"
        texture_name = "None"
        if source_material:
            try:
                base_name = source_material.get_base_material().get_name()
            except Exception:
                pass
            for parameter_name in ("BaseColorMap", "BaseColorTexture", "BaseColor"):
                try:
                    texture = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
                        source_material, parameter_name
                    )
                except Exception:
                    texture = None
                if texture:
                    texture_name = texture.get_name()
                    break

        unreal.log(
            f"[UniversalAnimeToonInspect] slot={slot_index:02d}, "
            f"source={source_name}, base={base_name}, texture={texture_name}, "
            f"profile={profile:.0f}"
        )


if __name__ == "__main__":
    try:
        inspect_profiles()
    except Exception as error:
        unreal.log_error(f"[UniversalAnimeToonInspect] 诊断失败: {error}")
        raise
