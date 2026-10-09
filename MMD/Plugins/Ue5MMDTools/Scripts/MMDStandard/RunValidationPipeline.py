# RunValidationPipeline.py
# 一次编辑器会话内跑完 MMDStandard 验证管线：
#   CreateToonRampTextures → CreateMMDStandardMaterials → ValidateMMDStandardMaterials
# 用法：
#   UnrealEditor-Cmd.exe MMD.uproject -ExecutePythonScript=RunValidationPipeline.py -unattended -nopause -nosplash
# 全部通过则日志出现 [MMDStandard][Pipeline] ALL PASS，进程退出码 0。

import os

import unreal


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PIPELINE = (
    "CreateToonRampTextures.py",
    "CreateMMDStandardMaterials.py",
    "ValidateMMDStandardMaterials.py",
)


def run_script(name):
    path = os.path.join(SCRIPT_DIR, name)
    unreal.log(f"[MMDStandard][Pipeline] 开始: {name}")
    with open(path, encoding="utf-8") as handle:
        source = handle.read()
    code = compile(source, path, "exec")
    exec(code, {"__name__": "__main__"})
    unreal.log(f"[MMDStandard][Pipeline] 完成: {name}")


def main():
    for name in PIPELINE:
        run_script(name)
    unreal.log("[MMDStandard][Pipeline] ALL PASS")


try:
    main()
except Exception as error:
    unreal.log_error(f"[MMDStandard][Pipeline] 失败: {error}")
    raise
