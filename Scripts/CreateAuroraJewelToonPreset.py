import unreal


PRESET_NAME = "TMMD Aurora Jewel Toon"
PRESET_VERSION = "R56"
SOURCE_ASSETS = {
    "/Ue5MMDTools/Resources/MaterialInstance/M_MMD_UniversalAnimeToon":
        "/Ue5MMDTools/Resources/MaterialPresets/M_TMMD_AuroraJewelToon",
    "/Ue5MMDTools/Resources/MaterialInstance/M_MMD_UniversalAnimeToon_Translucent":
        "/Ue5MMDTools/Resources/MaterialPresets/M_TMMD_AuroraJewelToon_Translucent",
}


def create_preset_assets():
    created_assets = []
    existing_assets = []

    for source_path, preset_path in SOURCE_ASSETS.items():
        source_asset = unreal.load_asset(source_path)
        if not source_asset:
            raise RuntimeError(f"找不到 R56 源材质: {source_path}")

        preset_asset = unreal.load_asset(preset_path)
        if preset_asset:
            existing_assets.append(preset_path)
        else:
            if not unreal.EditorAssetLibrary.duplicate_asset(source_path, preset_path):
                raise RuntimeError(f"创建预设资产失败: {preset_path}")
            preset_asset = unreal.load_asset(preset_path)
            if not preset_asset:
                raise RuntimeError(f"预设资产创建后无法加载: {preset_path}")
            created_assets.append(preset_path)

        unreal.EditorAssetLibrary.set_metadata_tag(preset_asset, "TMMDShaderPreset", PRESET_NAME)
        unreal.EditorAssetLibrary.set_metadata_tag(preset_asset, "TMMDShaderPresetVersion", PRESET_VERSION)
        compile_errors = unreal.MaterialEditingLibrary.recompile_material(preset_asset)
        if compile_errors:
            raise RuntimeError(
                f"预设材质编译失败: {preset_path}: "
                + " | ".join(str(item) for item in compile_errors)
            )
        if not unreal.EditorAssetLibrary.save_loaded_asset(preset_asset, False):
            raise RuntimeError(f"保存预设资产失败: {preset_path}")

    unreal.log(
        f"[AuroraJewelToon] preset={PRESET_NAME}, version={PRESET_VERSION}, "
        f"created={len(created_assets)}, existing={len(existing_assets)}"
    )
    for asset_path in created_assets:
        unreal.log(f"[AuroraJewelToon] created: {asset_path}")
    for asset_path in existing_assets:
        unreal.log(f"[AuroraJewelToon] kept existing: {asset_path}")


if __name__ == "__main__":
    try:
        create_preset_assets()
    except Exception as error:
        unreal.log_error(f"[AuroraJewelToon] 创建预设失败: {error}")
        raise
