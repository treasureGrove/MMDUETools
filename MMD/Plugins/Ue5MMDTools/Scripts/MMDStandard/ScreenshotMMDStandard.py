# ScreenshotMMDStandard.py
# MMDStandard 截图验证闭环：
#   1. 父材质 recompile（编译即验证 HLSL）
#   2. 场景 MMD Actor 逐 slot 应用 MI_MMDStandard_NN（从现有 MI 迁移 base/sphere/颜色/高光参数）
#   3. 视口相机自动对准模型 → HighResShot 截图到 Saved/MMDStandardScreenshots/
#   4. 轮询落盘 → 解码 PNG 做像素校验（非全黑/非全白/亮度有分布）
#   5. 输出 PASS/FAIL 报告
# 前置：先跑 CreateToonRampTextures.py + CreateMMDStandardMaterials.py；场景中已有导入的 MMD 模型。

import os
import struct
import time
import zlib

import unreal


OPAQUE_PARENT = "/Ue5MMDTools/Resources/MaterialInstance/M_MMDStandard_Opaque"
TRANSLUCENT_PARENT = "/Ue5MMDTools/Resources/MaterialInstance/M_MMDStandard_Translucent"
DEFAULT_TOON = "/Ue5MMDTools/Resources/ToonRamps/T_MMDToon01"
BLACK_TEXTURE = "/Ue5MMDTools/Resources/DefaultTexture/T_Black_1x1"
OUTPUT_DIR = os.path.join(
    unreal.Paths.project_saved_dir(), "MMDStandardScreenshots"
)

ACTOR_LABEL = None        # None = 自动取第一个 AMMDActor；也可填角色名精确匹配
TOON_ASSET = DEFAULT_TOON
SHOT_WIDTH = 1920
SHOT_HEIGHT = 1080
FILE_TIMEOUT_SECONDS = 60

# 从源材质实例迁移的参数（旧名 → 新名/默认）。
TEXTURE_SOURCES = ("BaseColorMap", "BaseColorTexture", "BaseColor")
SPHERE_SOURCES = ("mat_cap", "SphereTex")


def log(message):
    unreal.log(f"[MMDStandard] {message}")


# ------------------------- PNG 像素校验 -------------------------

def paeth_predictor(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png_pixels(png_bytes, sample_stride=16):
    """返回 (亮度采样列表, width, height)；支持 8bit 灰度/RGB/RGBA 非交错 PNG。"""
    if png_bytes[:8] != b"\x89PNG\r\n\x1a\n":
        raise RuntimeError("不是有效 PNG 文件")

    width = height = 0
    bit_depth = color_type = 0
    idat = bytearray()
    offset = 8
    while offset < len(png_bytes):
        (length,) = struct.unpack_from(">I", png_bytes, offset)
        tag = png_bytes[offset + 4:offset + 8]
        data = png_bytes[offset + 8:offset + 8 + length]
        if tag == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack_from(">IIBB", data)
        elif tag == b"IDAT":
            idat.extend(data)
        elif tag == b"IEND":
            break
        offset += 12 + length

    if bit_depth != 8 or color_type not in (0, 2, 6):
        raise RuntimeError(f"不支持的 PNG 格式: depth={bit_depth}, color={color_type}")

    channels = {0: 1, 2: 3, 6: 4}[color_type]
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    pixels = []
    previous_row = bytearray(stride)
    row_buffer = bytearray(stride)
    row_index = 0
    position = 0
    while position < len(raw):
        filter_type = raw[position]
        position += 1
        current = raw[position:position + stride]
        position += stride
        if len(current) < stride:
            break
        for x in range(stride):
            left = row_buffer[x - channels] if x >= channels else 0
            up = previous_row[x]
            upper_left = previous_row[x - channels] if x >= channels else 0
            if filter_type == 0:
                value = current[x]
            elif filter_type == 1:
                value = (current[x] + left) & 0xFF
            elif filter_type == 2:
                value = (current[x] + up) & 0xFF
            elif filter_type == 3:
                value = (current[x] + (left + up) // 2) & 0xFF
            elif filter_type == 4:
                value = (current[x] + paeth_predictor(left, up, upper_left)) & 0xFF
            else:
                raise RuntimeError(f"未知 PNG filter: {filter_type}")
            row_buffer[x] = value
        if row_index % sample_stride == 0:
            for x in range(0, stride, sample_stride * channels):
                if channels == 1:
                    pixels.append(row_buffer[x])
                else:
                    pixels.append(
                        (row_buffer[x] * 299 + row_buffer[x + 1] * 587 + row_buffer[x + 2] * 114) // 1000
                    )
        previous_row[:] = row_buffer
        row_index += 1

    if not pixels:
        raise RuntimeError("PNG 解码后没有像素")
    return pixels, width, height


def verify_screenshot(png_path):
    with open(png_path, "rb") as handle:
        png_bytes = handle.read()
    pixels, width, height = decode_png_pixels(png_bytes)

    pixel_count = len(pixels)
    dark_ratio = sum(1 for value in pixels if value < 8) / pixel_count
    bright_ratio = sum(1 for value in pixels if value > 247) / pixel_count
    average = sum(pixels) / pixel_count
    log(f"截图统计: {width}x{height}, 采样={pixel_count}, 平均亮度={average:.1f}, "
        f"暗占比={dark_ratio:.3f}, 亮占比={bright_ratio:.3f}")

    failures = []
    if width < SHOT_WIDTH // 2 or height < SHOT_HEIGHT // 2:
        failures.append(f"分辨率异常: {width}x{height}")
    if average <= 5.0:
        failures.append("画面全黑")
    if average >= 250.0:
        failures.append("画面全白")
    if dark_ratio > 0.985:
        failures.append("暗像素占比过高")
    if bright_ratio > 0.985:
        failures.append("亮像素占比过高")
    return failures


# ------------------------- 材质应用 -------------------------

def find_target_actor():
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = actor_subsystem.get_all_level_actors()

    def is_mmd_actor(item):
        # 导入流程会把角色另存为蓝图（BP_..._C），is_a 能覆盖派生类；API 未暴露时退类名匹配。
        try:
            if hasattr(unreal, "MMDActor") and item.is_a(unreal.MMDActor):
                return True
        except Exception:
            pass
        return item.get_class().get_name() == "MMDActor"

    mmd_actors = [item for item in actors if is_mmd_actor(item)]
    if ACTOR_LABEL:
        for item in mmd_actors:
            if item.get_actor_label() == ACTOR_LABEL:
                return item
        raise RuntimeError(f"找不到指定 MMDActor: {ACTOR_LABEL}")
    if not mmd_actors:
        raise RuntimeError("场景中没有 MMDActor，请先导入/加载 MMD 模型")
    return mmd_actors[0]


def read_texture_parameter(source_material, names):
    for name in names:
        try:
            texture = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
                source_material, name
            )
            if texture:
                return texture
        except Exception:
            pass
    return None


def read_vector_parameter(source_material, name, fallback):
    try:
        value = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(
            source_material, name
        )
        return value if value else fallback
    except Exception:
        return fallback


def read_scalar_parameter(source_material, name, fallback):
    try:
        value = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(
            source_material, name
        )
        return value if value is not None else fallback
    except Exception:
        return fallback


def apply_mmd_standard_materials(actor):
    opaque_parent = unreal.load_asset(OPAQUE_PARENT)
    translucent_parent = unreal.load_asset(TRANSLUCENT_PARENT)
    default_toon = unreal.load_asset(TOON_ASSET)
    black_texture = unreal.load_asset(BLACK_TEXTURE)
    for required in (opaque_parent, translucent_parent, default_toon, black_texture):
        if not required:
            raise RuntimeError("父材质或默认贴图缺失，请先跑创建脚本")

    components = actor.get_components_by_class(unreal.SkeletalMeshComponent)
    if not components:
        raise RuntimeError("MMDActor 没有 SkeletalMeshComponent")
    component = components[0]
    skeletal_mesh = component.get_editor_property("skeletal_mesh_asset")
    if not skeletal_mesh:
        raise RuntimeError("SkeletalMeshComponent 没有 SkeletalMesh")
    source_materials = [
        entry.get_editor_property("material_interface")
        for entry in skeletal_mesh.get_editor_property("materials")
    ]

    material_folder = f"/Game/MMDStandard/{actor.get_actor_label()}/Materials"
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    for slot_index, source_material in enumerate(source_materials):
        asset_name = f"MI_MMDStandard_{slot_index:02d}"
        material = unreal.load_asset(f"{material_folder}/{asset_name}")
        if not material:
            material = asset_tools.create_asset(
                asset_name, material_folder,
                unreal.MaterialInstanceConstant,
                unreal.MaterialInstanceConstantFactoryNew(),
            )
            if not material:
                raise RuntimeError(f"创建 MI 失败: {asset_name}")

        diffuse_color = unreal.LinearColor(1.0, 1.0, 1.0, 1.0)
        opacity = 1.0
        sphere_texture = None
        sphere_mode = 0.0
        specular_power = 5.0
        if source_material:
            diffuse_color = read_vector_parameter(
                source_material, "DiffuseColor", diffuse_color)
            opacity = read_scalar_parameter(source_material, "Opacity", 1.0)
            sphere_texture = read_texture_parameter(source_material, SPHERE_SOURCES)
            sphere_mode = read_scalar_parameter(source_material, "sphere_mode", 0.0)
            specular_power = read_scalar_parameter(source_material, "specular_power", 5.0)

        selected_parent = translucent_parent if opacity < 0.98 else opaque_parent
        unreal.MaterialEditingLibrary.set_material_instance_parent(material, selected_parent)

        base_texture = read_texture_parameter(source_material, TEXTURE_SOURCES) if source_material else None
        if base_texture:
            unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
                material, "BaseColorMap", base_texture)
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
            material, "ToonTex", default_toon)
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
            material, "SphereTex", sphere_texture if sphere_texture else black_texture)

        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            material, "DiffuseColor", diffuse_color)
        ambient_color = read_vector_parameter(
            source_material, "AmbientColor", unreal.LinearColor(0.5, 0.5, 0.5, 0.5)
        ) if source_material else unreal.LinearColor(0.5, 0.5, 0.5, 0.5)
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            material, "AmbientColor", ambient_color)
        specular_color = read_vector_parameter(
            source_material, "SpecularColor", unreal.LinearColor(0.0, 0.0, 0.0, 1.0)
        ) if source_material else unreal.LinearColor(0.0, 0.0, 0.0, 1.0)
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            material, "SpecularColor", specular_color)

        for name, value in (
            ("specular_power", specular_power),
            ("toon_use_texture", 1.0),
            ("toon_strength", 1.0),
            ("sphere_mode", sphere_mode),
            ("shadow_strength", 0.8),
            ("ambient_strength", 1.0),
            ("other_lights_strength", 0.35),
            ("Opacity", opacity),
        ):
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
                material, name, value)

        unreal.MaterialEditingLibrary.update_material_instance(material)
        component.set_material(slot_index, material)
        unreal.EditorAssetLibrary.save_loaded_asset(material, False)

    component.set_visibility(False, True)
    component.set_visibility(True, True)
    log(f"材质应用完成: actor={actor.get_actor_label()}, slots={len(source_materials)}")


# ------------------------- 相机与截图 -------------------------

def focus_viewport_on_actor(actor):
    origin, extent = actor.get_actor_bounds(False)
    distance = max(extent.x, extent.y, extent.z) * 1.9
    camera_location = unreal.Vector(
        origin.x + distance * 0.42,
        origin.y + distance * 0.86,
        origin.z + distance * 0.30,
    )
    camera_rotation = unreal.MathLibrary.find_look_at_rotation(
        camera_location, origin)
    try:
        unreal.EditorLevelLibrary.set_level_viewport_camera_info(
            camera_location, camera_rotation)
        log(f"视口对准: origin={origin}, distance={distance:.0f}")
    except Exception as error:
        log_warning(f"视口对焦失败（沿用当前视角）: {error}")


def log_warning(message):
    unreal.log_warning(f"[MMDStandard] {message}")


def take_screenshot(png_path):
    world = unreal.EditorLevelLibrary.get_editor_world()
    command = f'HighResShot {SHOT_WIDTH}x{SHOT_HEIGHT} filename="{png_path}"'
    unreal.SystemLibrary.execute_console_command(world, command)
    log(f"已发送截图命令: {command}")

    deadline = time.time() + FILE_TIMEOUT_SECONDS
    while time.time() < deadline:
        time.sleep(1.0)
        if os.path.exists(png_path) and os.path.getsize(png_path) > 4096:
            return True
    return False


# ------------------------- 主流程 -------------------------

def main():
    failures = []
    screenshot_path = None

    for parent_path in (OPAQUE_PARENT, TRANSLUCENT_PARENT):
        parent = unreal.load_asset(parent_path)
        if not parent:
            raise RuntimeError(f"父材质缺失: {parent_path}，请先跑 CreateMMDStandardMaterials.py")
        compile_errors = unreal.MaterialEditingLibrary.recompile_material(parent)
        if compile_errors:
            raise RuntimeError(f"{parent_path} 编译失败: " + " | ".join(str(e) for e in compile_errors))

    actor = find_target_actor()
    apply_mmd_standard_materials(actor)
    focus_viewport_on_actor(actor)

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    screenshot_path = os.path.join(OUTPUT_DIR, f"MMDStandard_{stamp}.png").replace("\\", "/")

    if not take_screenshot(screenshot_path):
        failures.append(f"截图文件未生成（超时 {FILE_TIMEOUT_SECONDS}s）: {screenshot_path}")
    else:
        failures.extend(verify_screenshot(screenshot_path))

    report_path = os.path.join(OUTPUT_DIR, f"report_{stamp}.txt")
    with open(report_path, "w", encoding="utf-8") as handle:
        handle.write(f"actor: {actor.get_actor_label()}\n")
        handle.write(f"screenshot: {screenshot_path}\n")
        handle.write(f"toon: {TOON_ASSET}\n")
        handle.write("result: " + ("PASS" if not failures else "FAIL") + "\n")
        for item in failures:
            handle.write(f"issue: {item}\n")

    if failures:
        for item in failures:
            log_warning(f"闭环失败项: {item}")
        raise RuntimeError(f"截图闭环 FAIL，详见 {report_path}")
    log(f"截图闭环 PASS ✓ 截图: {screenshot_path} 报告: {report_path}")


try:
    main()
except Exception as error:
    unreal.log_error(f"[MMDStandard] 截图闭环失败: {error}")
    raise
