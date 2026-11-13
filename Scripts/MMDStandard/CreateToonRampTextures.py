# CreateToonRampTextures.py
# 生成 MMD 共享 toon01~10 等效 ramp 贴图（256×1 灰度 PNG → 引擎资产），输出到 /Ue5MMDTools/Resources/ToonRamps/。
# 曲线为程序化近似（非 MMD 原版拷贝，规避版权）；PMX UseSharedToon 引用 toon01.bmp~toon10.bmp 时按编号取用。
# 如需精确原版观感，可在编辑器中用真实 MMD toon 贴图替换同名资产。
# 采样约定：u = Half-Lambert = n_dot_l * 0.5 + 0.5（与 MMDStandardCore.ush 的 SampleToonRamp 一致）。

import os
import struct
import zlib

import unreal


ASSET_FOLDER = "/Ue5MMDTools/Resources/ToonRamps"
TEMP_DIR = os.path.join(
    unreal.Paths.project_saved_dir(), "MMDStandardTemp"
)
RAMP_WIDTH = 256


def smoothstep(edge_low, edge_high, value):
    t = max(0.0, min(1.0, (value - edge_low) / max(edge_high - edge_low, 0.0001)))
    return t * t * (3.0 - 2.0 * t)


# 每条曲线：u ∈ [0,1]（Half-Lambert 域）→ 灰度 [0,1]。
# 03 是经典硬两段；10 全白（等效无 toon 阴影）。
RAMP_CURVES = {
    "T_MMDToon01": lambda u: 0.25 + 0.75 * smoothstep(0.15, 0.85, u),
    "T_MMDToon02": lambda u: 0.35 + 0.65 * smoothstep(0.10, 0.75, u),
    "T_MMDToon03": lambda u: smoothstep(0.47, 0.53, u),
    "T_MMDToon04": lambda u: 0.40 + 0.60 * smoothstep(0.44, 0.56, u),
    "T_MMDToon05": lambda u: 0.30 + 0.70 * smoothstep(0.38, 0.50, u),
    "T_MMDToon06": lambda u: 0.20 + 0.80 * smoothstep(0.32, 0.44, u),
    "T_MMDToon07": lambda u: 0.50 + 0.50 * smoothstep(0.54, 0.66, u),
    "T_MMDToon08": lambda u: 0.70 + 0.30 * smoothstep(0.60, 0.72, u),
    "T_MMDToon09": lambda u: 0.85 + 0.15 * smoothstep(0.65, 0.75, u),
    "T_MMDToon10": lambda u: 1.0,
}


def png_chunk(tag, data):
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def write_gray_png(path, pixels):
    # 8-bit 灰度 PNG（color type 0），每行 filter=0。
    width = len(pixels)
    raw = b"\x00" + bytes(pixels)
    ihdr = struct.pack(">IIBBBBB", width, 1, 8, 0, 0, 0, 0)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(png_chunk(b"IHDR", ihdr))
        handle.write(png_chunk(b"IDAT", zlib.compress(raw, 9)))
        handle.write(png_chunk(b"IEND", b""))


def set_texture_property(texture, names, value):
    # 不同引擎版本属性名大小写/下划线不一致（SRGB/s_rgb/srgb），逐个尝试。
    for name in names:
        try:
            texture.set_editor_property(name, value)
            return True
        except Exception:
            continue
    return False


def configure_ramp_texture(texture, name):
    # 线性灰度数据：关 sRGB、关 mip、无损压缩，保证 ramp 精度。幂等，可对已存在资产重跑。
    if not set_texture_property(texture, ("srgb", "s_rgb", "SRGB"), False):
        unreal.log_warning(f"[MMDStandard] {name}: 未找到 sRGB 属性，沿用默认")
    if not set_texture_property(
        texture, ("compression_settings",), unreal.TextureCompressionSettings.TC_MASKS
    ):
        unreal.log_warning(f"[MMDStandard] {name}: 设置压缩失败，沿用默认")
    if not set_texture_property(
        texture, ("mip_gen_settings", "mip_gen_settings_no_mipmaps"),
        unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
    ):
        unreal.log_warning(f"[MMDStandard] {name}: mip 设置失败，沿用默认")
    unreal.EditorAssetLibrary.save_loaded_asset(texture, False)


def create_ramp(name, curve):
    asset_path = f"{ASSET_FOLDER}/{name}"
    existing = unreal.load_asset(asset_path)
    if existing:
        configure_ramp_texture(existing, name)
        unreal.log_warning(f"[MMDStandard] ramp 已存在，仅校正属性: {asset_path}")
        return

    os.makedirs(TEMP_DIR, exist_ok=True)
    png_path = os.path.join(TEMP_DIR, f"{name}.png")
    pixels = [int(round(max(0.0, min(1.0, curve(x / (RAMP_WIDTH - 1.0)))) * 255))
              for x in range(RAMP_WIDTH)]
    write_gray_png(png_path, pixels)

    task = unreal.AssetImportTask()
    task.filename = png_path
    task.destination_path = ASSET_FOLDER
    task.destination_name = name
    task.automated = True
    task.save = True
    task.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    texture = unreal.load_asset(asset_path)
    if not texture:
        raise RuntimeError(f"ramp 贴图导入失败: {asset_path}")
    configure_ramp_texture(texture, name)
    unreal.log(f"[MMDStandard] ramp 生成完成: {asset_path}")


def main():
    for name, curve in RAMP_CURVES.items():
        create_ramp(name, curve)
    unreal.log(
        f"[MMDStandard] 共享 toon ramp 全部就绪: {len(RAMP_CURVES)} 张 → {ASSET_FOLDER}"
    )


try:
    main()
except Exception as error:
    unreal.log_error(f"[MMDStandard] ramp 生成失败: {error}")
    raise
