import unreal


ACTOR_LABEL = "MMD_Reimport_CleanValidation"
PARENT_MATERIAL_PATH = "/Ue5MMDTools/Resources/MaterialInstance/M_MMD_UniversalAnimeToon"
TRANSLUCENT_PARENT_MATERIAL_PATH = (
    "/Ue5MMDTools/Resources/MaterialInstance/M_MMD_UniversalAnimeToon_Translucent"
)
MATERIAL_FOLDER = "/Game/MMDToonRebuild/CatMaid/Materials"
GLOBAL_PRESET = {
    "shadow_step": 0.00,
    "shadow_softness": 0.045,
    "shadow_strength": 0.64,
    "ambient_strength": 0.10,
    "light_color_influence": 0.32,
    "highlight_step": 0.82,
    "highlight_strength": 0.28,
    "rim_strength": 0.10,
    "rim_power": 3.2,
    "local_light_strength": 0.72,
    "local_specular_strength": 0.55,
    "glamour_rim_strength": 3.00,
    "directional_sheen_strength": 0.95,
}


def classify_surface_profile(source_material, diffuse_color):
    """可选档位只改变几何光照响应，不读取或依赖 UV/配套遮罩。"""
    if not source_material:
        return 0.0
    source_name = source_material.get_name().lower()
    base_material = ""
    try:
        base_material = source_material.get_base_material().get_name().lower()
    except Exception:
        pass
    texture_name = ""
    for parameter_name in ("BaseColorMap", "BaseColorTexture", "BaseColor"):
        try:
            texture = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
                source_material, parameter_name
            )
        except Exception:
            texture = None
        if texture:
            texture_name = texture.get_name().lower()
            break
    semantic_name = f"{source_name} {base_material}"
    combined_name = f"{semantic_name} {texture_name}"
    if any(token in semantic_name for token in (
        "skin", "face", "facial", "body", "hada", "肌", "顔", "脸", "身体"
    )):
        return 1.0
    if any(token in semantic_name for token in ("eye", "eyeball", "iris", "hl", "瞳", "目")):
        return 5.0
    if any(token in semantic_name for token in ("hairshadow", "hair_shadow", "髪影", "发影")):
        return 6.0
    if any(token in semantic_name for token in ("hair", "kami", "bang", "tail", "髪", "发")):
        return 2.0
    # 先信任材质语义，避免黑裙因共用一张带 white 字样的贴图被误判成白布。
    if any(token in semantic_name for token in (
        "black", "skirt", "dress", "ribbon", "shoe", "metal", "gold",
        "スカート", "リボン", "クロスタイ", "ボタン", "金属"
    )):
        return 4.0
    if any(token in semantic_name for token in (
        "white", "shirt", "blouse", "apron", "frill", "tops", "tooth",
        "围裙", "白", "フリル", "レース"
    )):
        return 3.0
    if any(token in texture_name for token in ("black", "dark")):
        return 4.0
    if any(token in texture_name for token in ("white", "shirt", "blouse", "cloth")):
        return 3.0

    maximum = max(diffuse_color.r, diffuse_color.g, diffuse_color.b)
    minimum = min(diffuse_color.r, diffuse_color.g, diffuse_color.b)
    saturation = maximum - minimum
    luminance = (
        diffuse_color.r * 0.299 + diffuse_color.g * 0.587 + diffuse_color.b * 0.114
    )
    # 颜色参数经常只是白色乘数，不能据此把未知材质强行分类。
    return 0.0


def get_texture(source_material):
    for parameter_name in ("BaseColorMap", "BaseColorTexture", "BaseColor"):
        texture = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
            source_material, parameter_name
        )
        if texture:
            return texture
    return unreal.load_asset("/Ue5MMDTools/Resources/DefaultTexture/T_White_1x1")


def get_vector(source_material, parameter_name, fallback):
    try:
        value = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(
            source_material, parameter_name
        )
        return value if value else fallback
    except Exception:
        return fallback


def get_scalar(source_material, parameter_name, fallback):
    try:
        value = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(
            source_material, parameter_name
        )
        return value if value is not None else fallback
    except Exception:
        return fallback


def create_or_load_instance(asset_name, parent_material, material_folder=MATERIAL_FOLDER):
    asset_path = f"{material_folder}/{asset_name}"
    material = unreal.load_asset(asset_path)
    if material:
        return material

    factory = unreal.MaterialInstanceConstantFactoryNew()
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name,
        material_folder,
        unreal.MaterialInstanceConstant,
        factory,
    )
    if not material:
        raise RuntimeError(f"创建材质实例失败: {asset_path}")
    unreal.MaterialEditingLibrary.set_material_instance_parent(material, parent_material)
    return material


def needs_translucent_parent(source_material, diffuse_color):
    if diffuse_color.a < 0.98:
        return True
    try:
        base_material = source_material.get_base_material()
        return base_material.get_editor_property("blend_mode") in (
            unreal.BlendMode.BLEND_TRANSLUCENT,
            unreal.BlendMode.BLEND_ADDITIVE,
            unreal.BlendMode.BLEND_MODULATE,
            unreal.BlendMode.BLEND_ALPHA_COMPOSITE,
            unreal.BlendMode.BLEND_ALPHA_HOLDOUT,
        )
    except Exception:
        return False


def apply_materials(actor_label=ACTOR_LABEL, material_folder=MATERIAL_FOLDER, expected_slot_count=49):
    parent_material = unreal.load_asset(PARENT_MATERIAL_PATH)
    translucent_parent = unreal.load_asset(TRANSLUCENT_PARENT_MATERIAL_PATH)
    if not parent_material:
        raise RuntimeError(f"找不到独立父材质: {PARENT_MATERIAL_PATH}")
    if not translucent_parent:
        raise RuntimeError(f"找不到 Translucent 配套材质: {TRANSLUCENT_PARENT_MATERIAL_PATH}")

    compile_errors = unreal.MaterialEditingLibrary.recompile_material(parent_material)
    if compile_errors:
        raise RuntimeError("父材质编译失败: " + " | ".join(str(item) for item in compile_errors))

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = next(
        (
            item
            for item in actor_subsystem.get_all_level_actors()
            if item.get_actor_label() == actor_label
        ),
        None,
    )
    if not actor:
        raise RuntimeError(f"找不到重导验证角色: {actor_label}")

    components = actor.get_components_by_class(unreal.SkeletalMeshComponent)
    if not components:
        raise RuntimeError("重导验证角色没有 SkeletalMeshComponent")
    component = components[0]

    skeletal_mesh = component.get_editor_property("skeletal_mesh_asset")
    if not skeletal_mesh:
        raise RuntimeError("重导验证角色没有 SkeletalMesh 资产")
    source_materials = [
        entry.get_editor_property("material_interface")
        for entry in skeletal_mesh.get_editor_property("materials")
    ]
    if expected_slot_count is not None and len(source_materials) != expected_slot_count:
        raise RuntimeError(f"重导材质槽数量异常: {len(source_materials)}")

    saved_assets = []
    for slot_index, source_material in enumerate(source_materials):
        asset_name = f"MI_UniversalAnimeToon_{slot_index:02d}"
        texture = get_texture(source_material) if source_material else None
        diffuse_color = get_vector(
            source_material,
            "DiffuseColor",
            unreal.LinearColor(1.0, 1.0, 1.0, 1.0),
        ) if source_material else unreal.LinearColor(1.0, 1.0, 1.0, 1.0)
        opacity = get_scalar(source_material, "Opacity", 1.0) if source_material else 1.0
        selected_parent = (
            translucent_parent
            if source_material and needs_translucent_parent(source_material, diffuse_color)
            else parent_material
        )
        material = create_or_load_instance(asset_name, selected_parent, material_folder)
        unreal.MaterialEditingLibrary.set_material_instance_parent(material, selected_parent)

        if texture:
            unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
                material, "BaseColorMap", texture
            )
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            material, "DiffuseColor", diffuse_color
        )
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
            material, "Opacity", opacity
        )
        for parameter_name, parameter_value in GLOBAL_PRESET.items():
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                material, parameter_name, parameter_value
            )
        surface_profile = classify_surface_profile(source_material, diffuse_color)
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
            material, "surface_profile", surface_profile
        )
        unreal.log(
            f"[UniversalAnimeToon] slot={slot_index:02d}, "
            f"source={source_material.get_name() if source_material else 'None'}, "
            f"profile={surface_profile:.0f}"
        )
        unreal.MaterialEditingLibrary.update_material_instance(material)
        component.set_material(slot_index, material)
        saved_assets.append(material)

    component.set_visibility(False, True)
    component.set_visibility(True, True)
    for material in saved_assets:
        unreal.EditorAssetLibrary.save_loaded_asset(material, False)

    unreal.log(
        f"[UniversalAnimeToon] 重导模型验证材质应用完成: actor={actor_label}, "
        f"slots={len(saved_assets)}, folder={material_folder}"
    )


if __name__ == "__main__":
    try:
        apply_materials()
    except Exception as error:
        unreal.log_error(f"[UniversalAnimeToon] 验证应用失败: {error}")
        raise
