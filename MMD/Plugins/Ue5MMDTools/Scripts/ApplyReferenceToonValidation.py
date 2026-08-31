import unreal


ACTOR_LABEL = "MMD_ReferenceToon_RawValidation"
PARENT_MATERIAL_PATH = "/Ue5MMDTools/Resources/MaterialInstance/M_MMD_ReferenceToon"
MATERIAL_FOLDER = "/Game/MMDReferenceToon/ValidationMaterials"
SKIP_SLOTS = set()
SKIN_SLOTS = {0, 1, 2, 31, 35}
METAL_SLOTS = {6, 9, 11, 12, 13, 24, 29}
HAIR_SLOTS = {41, 42, 43}
GLOBAL_PRESET = {
    "shadow_threshold": 0.05,
    "shadow_softness": 0.025,
    "midtone_width": 0.30,
    "shadow_strength": 0.56,
    "ambient_strength": 0.12,
    "light_saturation": 1.0,
    "specular_threshold": 0.92,
    "specular_strength": 0.24,
    "rim_power": 3.6,
    "rim_strength": 0.27,
}


def compile_parent_revision():
    material = unreal.load_asset(PARENT_MATERIAL_PATH)
    if not material:
        raise RuntimeError(f"找不到参考父材质: {PARENT_MATERIAL_PATH}")

    custom_nodes = [
        node
        for node in unreal.MaterialEditingLibrary.get_material_expressions(material)
        if isinstance(node, unreal.MaterialExpressionCustom)
    ]
    if len(custom_nodes) != 1:
        raise RuntimeError(f"参考父材质 Custom 节点数量错误: {len(custom_nodes)}")

    custom_nodes[0].set_editor_property(
        "code",
        'float reference_toon_glamorous = 0.0;\n'
        '#include "/Plugin/Ue5MMDTools/TMMDShader/MMDReferenceToonGlamorous.usf"\n'
        "return float3(reference_toon_glamorous, reference_toon_glamorous, "
        "reference_toon_glamorous);",
    )
    errors = unreal.MaterialEditingLibrary.recompile_material(material)
    if errors:
        raise RuntimeError("V2 父材质编译失败: " + " | ".join(str(item) for item in errors))
    unreal.log("[MMDReferenceToon] Glamorous 当前基线入口编译完成")


def apply_validation_materials():
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

    component = components[0]
    applied_count = 0
    for slot_index in range(component.get_num_materials()):
        if slot_index in SKIP_SLOTS:
            continue

        asset_name = f"MI_ReferenceToon_{slot_index:02d}"
        material = unreal.load_asset(f"{MATERIAL_FOLDER}/{asset_name}")
        if not material:
            unreal.log_warning(f"[MMDReferenceToon] 缺少验证实例: {asset_name}")
            continue

        for parameter_name, parameter_value in GLOBAL_PRESET.items():
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                material, parameter_name, parameter_value
            )
        if slot_index in SKIN_SLOTS:
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                material, "shadow_threshold", -0.05
            )
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                material, "shadow_softness", 0.13
            )
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                material, "shadow_strength", 0.12
            )
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                material, "ambient_strength", 0.28
            )
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                material, "light_saturation", 0.32
            )
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                material, "specular_strength", 0.08
            )
        elif slot_index in METAL_SLOTS:
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                material, "specular_threshold", 0.86
            )
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                material, "specular_strength", 0.46
            )
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                material, "rim_strength", 0.34
            )
        elif slot_index in HAIR_SLOTS:
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                material, "shadow_strength", 0.48
            )
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                material, "anisotropy_strength", 0.52
            )
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                material, "specular_strength", 0.34
            )
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                material, "rim_strength", 0.36
            )
        unreal.MaterialEditingLibrary.update_material_instance(material)
        component.set_material(slot_index, material)
        applied_count += 1

    # 直接写 OverrideMaterials 不会刷新场景代理；显隐切换强制当前编辑器视口重建代理。
    component.set_visibility(False, True)
    component.set_visibility(True, True)
    unreal.log(
        f"[MMDReferenceToon] SetMaterial 应用完成: actor={ACTOR_LABEL}, "
        f"slots={applied_count}"
    )


try:
    compile_parent_revision()
    apply_validation_materials()
except Exception as error:
    unreal.log_error(f"[MMDReferenceToon] 验证材质应用失败: {error}")
    raise
