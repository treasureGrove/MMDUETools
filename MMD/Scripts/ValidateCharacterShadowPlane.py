"""验证角色接收平面阴影的物理不变量，直接运行本文件即可。

深度由独立的三维光线与平面求交产生，而非由待测梯度公式生成。
覆盖同一平面不自遮挡、真实前置遮挡仍生效、PCF 权重和导数退化。
这验证采样数学；真实脸部曲率和 GPU 导数仍需 MainView 截图回归。
"""

import math
import random


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def scale(a, amount):
    return tuple(x * amount for x in a)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def normalized(value):
    return scale(value, 1.0 / math.sqrt(dot(value, value)))


def light_frame(rng):
    forward = normalized(tuple(rng.uniform(-1.0, 1.0) for _ in range(3)))
    preferred_up = (0.0, 1.0, 0.0) if abs(forward[2]) > 0.95 else (0.0, 0.0, 1.0)
    right = normalized(cross(preferred_up, forward))
    up = cross(forward, right)
    return right, up, forward


def solve_receiver_gradient(uv_dx, uv_dy, depth_dx, depth_dy, width):
    determinant = uv_dx[0] * uv_dy[1] - uv_dx[1] * uv_dy[0]
    jacobian_scale = math.hypot(*uv_dx) * math.hypot(*uv_dy)
    if not abs(determinant) > max(jacobian_scale * 0.015, 1e-12):
        return (0.0, 0.0)
    gradient = ((depth_dx * uv_dy[1] - depth_dy * uv_dx[1]) / determinant,
                (depth_dy * uv_dx[0] - depth_dx * uv_dy[0]) / determinant)
    if not all(math.isfinite(value) and abs(value) <= width * 8.0 for value in gradient):
        return (0.0, 0.0)
    return gradient


def bilinear_compare(depth_at, tap_uv, receiver_uv, receiver_depth, bias, gradient, resolution):
    texel = tuple(value * resolution - 0.5 for value in tap_uv)
    base = tuple(math.floor(value) for value in texel)
    phase = tuple(value - index for value, index in zip(texel, base))
    comparisons = []
    for y in (base[1], base[1] + 1):
        row = []
        for x in (base[0], base[0] + 1):
            x = min(max(x, 0), resolution - 1)
            y_clamped = min(max(y, 0), resolution - 1)
            texel_uv = ((x + 0.5) / resolution, (y_clamped + 0.5) / resolution)
            expected = receiver_depth + dot(gradient, tuple(a - b for a, b in zip(texel_uv, receiver_uv)))
            row.append(float(expected <= depth_at(texel_uv) + bias))
        comparisons.append(row[0] * (1.0 - phase[0]) + row[1] * phase[0])
    return comparisons[0] * (1.0 - phase[1]) + comparisons[1] * phase[1]


def high_tent(depth_at, receiver_uv, receiver_depth, bias, gradient, resolution):
    total = 0.0
    for oy, wy in ((-1.0, 1.0), (0.0, 2.0), (1.0, 1.0)):
        for ox, wx in ((-1.0, 1.0), (0.0, 2.0), (1.0, 1.0)):
            tap = (receiver_uv[0] + ox * 1.35 / resolution,
                   receiver_uv[1] + oy * 1.35 / resolution)
            total += bilinear_compare(depth_at, tap, receiver_uv, receiver_depth, bias, gradient, resolution) * wx * wy
    return total / 16.0


def low_tent(depth_at, receiver_uv, receiver_depth, bias, gradient, resolution, optimized=True):
    if not optimized:
        total = 0.0
        for oy, wy in ((-1.0, 1.0), (0.0, 2.0), (1.0, 1.0)):
            for ox, wx in ((-1.0, 1.0), (0.0, 2.0), (1.0, 1.0)):
                tap = (receiver_uv[0] + ox / resolution, receiver_uv[1] + oy / resolution)
                total += bilinear_compare(depth_at, tap, receiver_uv, receiver_depth, bias, gradient, resolution) * wx * wy
        return total / 16.0
    texel = tuple(value * resolution - 0.5 for value in receiver_uv)
    base = tuple(math.floor(value) for value in texel)
    phase = tuple(value - index for value, index in zip(texel, base))
    negative_weights = tuple(3.0 - 2.0 * value for value in phase)
    positive_weights = tuple(1.0 + 2.0 * value for value in phase)
    negative_uv = tuple((index - 1.0 + (2.0 - value) / weight + 0.5) / resolution
                        for index, value, weight in zip(base, phase, negative_weights))
    positive_uv = tuple((index + 1.0 + value / weight + 0.5) / resolution
                        for index, value, weight in zip(base, phase, positive_weights))
    total = 0.0
    for y, wy in ((negative_uv[1], negative_weights[1]), (positive_uv[1], positive_weights[1])):
        for x, wx in ((negative_uv[0], negative_weights[0]), (positive_uv[0], positive_weights[0])):
            total += bilinear_compare(depth_at, (x, y), receiver_uv, receiver_depth, bias, gradient, resolution) * wx * wy
    return total / 16.0


def main():
    rng = random.Random(6719)
    cases = 20000
    legacy_self_occluded = 0
    max_gradient_error = 0.0
    max_tent_error = 0.0
    for case in range(cases):
        right, up, forward = light_frame(rng)
        width = rng.uniform(150.0, 1800.0)
        resolution = rng.choice((1024, 2048, 4096))
        world_texel = width / resolution
        slope_x, slope_y = rng.uniform(-4.0, 4.0), rng.uniform(-4.0, 4.0)
        normal = normalized(add(add(scale(right, slope_x), scale(up, slope_y)), scale(forward, -1.0)))
        tangent_x = add(right, scale(forward, slope_x))
        tangent_y = add(up, scale(forward, slope_y))
        # 模拟不同主相机的屏幕导数，包含旋转和斜切，不假定屏幕轴等于光空间轴。
        footprint = rng.uniform(0.03, 0.8)
        while True:
            a, b, c, d = (rng.uniform(-1.0, 1.0) for _ in range(4))
            if abs(a * d - b * c) > 0.12:
                break
        world_dx = scale(add(scale(tangent_x, a), scale(tangent_y, b)), footprint)
        world_dy = scale(add(scale(tangent_x, c), scale(tangent_y, d)), footprint)
        uv_dx = (dot(world_dx, right) / width, -dot(world_dx, up) / width)
        uv_dy = (dot(world_dy, right) / width, -dot(world_dy, up) / width)
        gradient = solve_receiver_gradient(uv_dx, uv_dy, dot(world_dx, forward), dot(world_dy, forward), width)
        expected_gradient = (slope_x * width, -slope_y * width)
        error = max(abs(x - y) for x, y in zip(gradient, expected_gradient))
        max_gradient_error = max(max_gradient_error, error)
        assert error < 1e-7, (case, gradient, expected_gradient)

        initial_uv = (rng.uniform(0.1, 0.9), rng.uniform(0.1, 0.9))
        initial_depth = rng.uniform(100.0, 2000.0)
        point_on_plane = add(add(scale(right, (initial_uv[0] - 0.5) * width),
                                 scale(up, (0.5 - initial_uv[1]) * width)), scale(forward, initial_depth))
        normal_offset = rng.uniform(0.0, world_texel * 0.5)
        receiver_point = add(point_on_plane, scale(normal, normal_offset))
        receiver_uv = (dot(receiver_point, right) / width + 0.5, 0.5 - dot(receiver_point, up) / width)
        receiver_depth = dot(receiver_point, forward)
        bias = 0.4 + min(world_texel * 0.5, 0.25)

        def plane_depth(texel_uv):
            # 独立几何真值：每个阴影 texel 的光线与原三维接收平面求交。
            ray_origin = add(scale(right, (texel_uv[0] - 0.5) * width),
                             scale(up, (0.5 - texel_uv[1]) * width))
            return dot(normal, add(point_on_plane, scale(ray_origin, -1.0))) / dot(normal, forward)

        for filtering in (high_tent, low_tent):
            visibility = filtering(plane_depth, receiver_uv, receiver_depth, bias, gradient, resolution)
            assert abs(visibility - 1.0) < 1e-12, (case, "接收平面自遮挡", visibility)
        legacy_visibility = high_tent(plane_depth, receiver_uv, receiver_depth, bias, (0.0, 0.0), resolution)
        legacy_self_occluded += legacy_visibility < 1.0 - 1e-12

        # 前置平面必须仍能挡光，距离超过深度偏移和法线偏移的总容差。
        normal_tolerance = normal_offset / abs(dot(normal, forward))
        blocker_gap = bias + normal_tolerance + rng.uniform(0.01, 5.0)

        def blocker_depth(texel_uv):
            return plane_depth(texel_uv) - blocker_gap

        for filtering in (high_tent, low_tent):
            visibility = filtering(blocker_depth, receiver_uv, receiver_depth, bias, gradient, resolution)
            assert visibility == 0.0, (case, "真实遮挡漏光", visibility)

        # 部分覆盖的真实遮挡边缘：优化低档 PCF 必须等价于完整 3x3 tent。
        edge = receiver_uv[0] + rng.uniform(-1.5, 1.5) / resolution

        def partial_blocker(texel_uv):
            return blocker_depth(texel_uv) if texel_uv[0] < edge else plane_depth(texel_uv)

        naive = low_tent(partial_blocker, receiver_uv, receiver_depth, bias, gradient, resolution, optimized=False)
        optimized = low_tent(partial_blocker, receiver_uv, receiver_depth, bias, gradient, resolution)
        max_tent_error = max(max_tent_error, abs(naive - optimized))
        assert abs(naive - optimized) < 1e-10, (case, naive, optimized)

    # 退化导数不能产生无穷梯度；极端坡度不能放大成跨物体漏光。
    invalid_cases = (
        ((0.0, 0.0), (0.0, 0.0), 0.0, 0.0),
        ((1e-8, 0.0), (0.0, 1e-8), 1.0, 1.0),
        ((0.001, 0.0), (0.002, 0.0), 1.0, 2.0),
        ((0.001, 0.0), (0.0, 0.001), float("nan"), 1.0),
        ((0.001, 0.0), (0.0, 0.001), float("inf"), 1.0),
        ((0.001, 0.0), (0.0, 0.001), 100000.0, 1.0),
    )
    for uv_dx, uv_dy, depth_dx, depth_dy in invalid_cases:
        assert solve_receiver_gradient(uv_dx, uv_dy, depth_dx, depth_dy, 600.0) == (0.0, 0.0)
    assert legacy_self_occluded > cases // 2, "测试未覆盖原中心深度比较的自遮挡"
    print(f"PASS receiver-plane: cases={cases} random_light_frames=20000 normal_bias_enabled=true")
    print(f"PASS self_shadow: corrected_self_occluded=0 legacy_self_occluded={legacy_self_occluded}")
    print(f"PASS real_blockers: incorrect_occluder_lit=0 max_gradient_error={max_gradient_error:.3g}")
    print(f"PASS low_tent_partial_edges: max_error={max_tent_error:.3g}")
    print(f"PASS degenerate_jacobian: cases={len(invalid_cases)} zero_gradient_fallback=true")


if __name__ == "__main__":
    main()
