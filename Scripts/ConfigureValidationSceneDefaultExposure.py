import unreal


def configure_default_exposure():
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    volumes = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if isinstance(actor, unreal.PostProcessVolume)
    ]
    changed = 0
    for volume in volumes:
        settings = volume.get_editor_property("settings")
        for property_name in (
            "override_camera_shutter_speed",
            "override_camera_iso",
            "override_auto_exposure_method",
            "override_auto_exposure_bias",
            "override_auto_exposure_apply_physical_camera_exposure",
        ):
            try:
                settings.set_editor_property(property_name, False)
            except Exception:
                unreal.log_warning(
                    f"[UniversalAnimeToon] 曝光字段不可写，已跳过: {property_name}"
                )
        volume.set_editor_property("settings", settings)
        changed += 1

    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log(
        f"[UniversalAnimeToon] 已恢复 UE 项目默认曝光路径: post_process_volumes={changed}"
    )


if __name__ == "__main__":
    configure_default_exposure()
