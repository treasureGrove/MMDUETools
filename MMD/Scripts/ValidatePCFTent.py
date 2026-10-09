import random


def clamp(value, low, high):
    return min(max(value, low), high)


def bilinear(values, x, y):
    height = len(values)
    width = len(values[0])
    base_x = int(x // 1)
    base_y = int(y // 1)
    phase_x = x - base_x
    phase_y = y - base_y
    x0 = clamp(base_x, 0, width - 1)
    x1 = clamp(base_x + 1, 0, width - 1)
    y0 = clamp(base_y, 0, height - 1)
    y1 = clamp(base_y + 1, 0, height - 1)
    row0 = values[y0][x0] * (1.0 - phase_x) + values[y0][x1] * phase_x
    row1 = values[y1][x0] * (1.0 - phase_x) + values[y1][x1] * phase_x
    return row0 * (1.0 - phase_y) + row1 * phase_y


def naive_tent3(values, x, y):
    weights = (1.0, 2.0, 1.0)
    total = 0.0
    for offset_y, weight_y in zip((-1, 0, 1), weights):
        for offset_x, weight_x in zip((-1, 0, 1), weights):
            total += bilinear(values, x + offset_x, y + offset_y) * weight_x * weight_y
    return total / 16.0


def optimized_tent3(values, x, y):
    base_x = int(x // 1)
    base_y = int(y // 1)
    phase_x = x - base_x
    phase_y = y - base_y

    negative_weight_x = 3.0 - 2.0 * phase_x
    positive_weight_x = 1.0 + 2.0 * phase_x
    negative_weight_y = 3.0 - 2.0 * phase_y
    positive_weight_y = 1.0 + 2.0 * phase_y
    negative_x = base_x - 1.0 + (2.0 - phase_x) / negative_weight_x
    positive_x = base_x + 1.0 + phase_x / positive_weight_x
    negative_y = base_y - 1.0 + (2.0 - phase_y) / negative_weight_y
    positive_y = base_y + 1.0 + phase_y / positive_weight_y

    total = 0.0
    total += bilinear(values, negative_x, negative_y) * negative_weight_x * negative_weight_y
    total += bilinear(values, positive_x, negative_y) * positive_weight_x * negative_weight_y
    total += bilinear(values, negative_x, positive_y) * negative_weight_x * positive_weight_y
    total += bilinear(values, positive_x, positive_y) * positive_weight_x * positive_weight_y
    return total / 16.0


def main():
    random.seed(0x3A17)
    max_error = 0.0
    for _ in range(20000):
        comparisons = [[float(random.randrange(2)) for _ in range(11)] for _ in range(9)]
        # 包含内部、小数相位和超出边界的 clamp 情况。
        x = random.uniform(-1.5, 11.5)
        y = random.uniform(-1.5, 9.5)
        expected = naive_tent3(comparisons, x, y)
        actual = optimized_tent3(comparisons, x, y)
        max_error = max(max_error, abs(expected - actual))
    assert max_error < 1e-12, max_error
    print(f"PASS continuous 3x3 tent: random_cases=20000 max_error={max_error:.3g}")
    print("manual_depth_loads_per_cascade: high=36 low=16 blend_high=72 blend_low=32")


if __name__ == "__main__":
    main()
