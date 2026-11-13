import importlib.util
import unreal


SOURCE_SCRIPT = (
    "Z:/Project/UEProject/MMDUETools/MMD/Plugins/Ue5MMDTools/Scripts/"
    "ApplyUniversalAnimeToonValidation.py"
)
ACTOR_LABEL = "MMD_UniversalToon_SecondValidation"
MATERIAL_FOLDER = "/Game/MMDToonRebuild/Miku16th/Materials"


def run():
    spec = importlib.util.spec_from_file_location("mmd_universal_toon_apply", SOURCE_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.apply_materials(ACTOR_LABEL, MATERIAL_FOLDER, 39)
    unreal.log(
        f"[UniversalAnimeToon] 第二模型交叉验证材质完成: actor={ACTOR_LABEL}, slots=39"
    )


try:
    run()
except Exception as error:
    unreal.log_error(f"[UniversalAnimeToon] 第二模型交叉验证失败: {error}")
    raise
