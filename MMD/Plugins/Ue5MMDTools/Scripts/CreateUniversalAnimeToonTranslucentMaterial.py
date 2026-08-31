import unreal


SOURCE_PATH = "/Ue5MMDTools/Resources/MaterialInstance/M_MMD_UniversalAnimeToon"
ASSET_PATH = "/Ue5MMDTools/Resources/MaterialInstance/M_MMD_UniversalAnimeToon_Translucent"


def parameter_name(expression):
    try:
        return str(expression.get_editor_property("parameter_name"))
    except Exception:
        return ""


def create_translucent_material():
    source = unreal.load_asset(SOURCE_PATH)
    if not source:
        raise RuntimeError(f"找不到 Opaque 主材质: {SOURCE_PATH}")

    material = unreal.load_asset(ASSET_PATH)
    if not material:
        if not unreal.EditorAssetLibrary.duplicate_asset(SOURCE_PATH, ASSET_PATH):
            raise RuntimeError(f"复制 Translucent 材质失败: {ASSET_PATH}")
        material = unreal.load_asset(ASSET_PATH)
    if not material:
        raise RuntimeError(f"加载 Translucent 材质失败: {ASSET_PATH}")

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("two_sided", True)

    expressions = unreal.MaterialEditingLibrary.get_material_expressions(material)
    base_texture = next(
        (
            node
            for node in expressions
            if isinstance(node, unreal.MaterialExpressionTextureSampleParameter2D)
            and parameter_name(node) == "BaseColorMap"
        ),
        None,
    )
    diffuse_color = next(
        (
            node
            for node in expressions
            if isinstance(node, unreal.MaterialExpressionVectorParameter)
            and parameter_name(node) == "DiffuseColor"
        ),
        None,
    )
    opacity = next(
        (
            node
            for node in expressions
            if isinstance(node, unreal.MaterialExpressionScalarParameter)
            and parameter_name(node) == "Opacity"
        ),
        None,
    )
    if not base_texture or not diffuse_color or not opacity:
        raise RuntimeError("Translucent 材质缺少透明度输入节点")

    alpha_product = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, 620, 170
    )
    alpha_product_2 = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, 840, 170
    )
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        base_texture, "A", alpha_product, "A"
    ):
        raise RuntimeError("BaseColorMap Alpha 连接失败")
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        diffuse_color, "A", alpha_product, "B"
    ):
        raise RuntimeError("DiffuseColor Alpha 连接失败")
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        alpha_product, "", alpha_product_2, "A"
    ):
        raise RuntimeError("透明度乘法连接失败")
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        opacity, "", alpha_product_2, "B"
    ):
        raise RuntimeError("Opacity 参数连接失败")
    if not unreal.MaterialEditingLibrary.connect_material_property(
        alpha_product_2, "", unreal.MaterialProperty.MP_OPACITY
    ):
        raise RuntimeError("透明度输出连接失败")

    compile_errors = unreal.MaterialEditingLibrary.recompile_material(material)
    if compile_errors:
        raise RuntimeError("Translucent 材质编译失败: " + " | ".join(str(item) for item in compile_errors))
    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    unreal.log(f"[UniversalAnimeToon] Translucent 配套材质完成: {ASSET_PATH}")


try:
    create_translucent_material()
except Exception as error:
    unreal.log_error(f"[UniversalAnimeToon] Translucent 创建失败: {error}")
    raise
