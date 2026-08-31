import unreal


ACTOR_LABEL = "MMD_Reimport_CleanValidation"


def clear_validation_overlay():
    """仅清理验证角色的实验 Overlay，不改任何材质槽。"""
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

    cleared_count = 0
    for component in components:
        overlay_material = component.get_editor_property("overlay_material")
        if overlay_material:
            unreal.log(
                f"[UniversalAnimeToon] 清理 Overlay: component={component.get_name()}, "
                f"material={overlay_material.get_path_name()}"
            )
            component.set_editor_property("overlay_material", None)
            component.modify()
            cleared_count += 1

    actor.modify()
    unreal.EditorLevelLibrary.save_current_level()
    unreal.log(
        f"[UniversalAnimeToon] Overlay 恢复完成: actor={ACTOR_LABEL}, "
        f"cleared={cleared_count}"
    )


if __name__ == "__main__":
    try:
        clear_validation_overlay()
    except Exception as error:
        unreal.log_error(f"[UniversalAnimeToon] Overlay 恢复失败: {error}")
        raise
