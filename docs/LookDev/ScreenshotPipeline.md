# Screenshot & A/B Pipeline Specification

> Generated for the LookDev automated screenshot system

---

## Screenshot Naming Convention

```
E{###}_{SHA}_{ENV}_{CAM}_{VARIANT}.png
```

| Component | Example | Notes |
|---|---|---|
| Experiment ID | `E014` | Sequential, 3 digits, zero-padded |
| Git SHA | `a31f52` | First 6 chars of HEAD |
| Environment | `T01_DirectionalOnly` | Technical env ID + short name |
| Camera | `C11_Face3Quarter` | Camera ID from spec |
| Variant | `A` or `B` | A=baseline, B=candidate |

Example: `E014_a31f52_T01_DirectionalOnly_C11_Face3Q_A.png`

## Screenshot Metadata (JSON sidecar)

Each screenshot set has a `.meta.json`:

```json
{
    "experiment_id": "E014",
    "git_sha": "a31f52c",
    "branch": "agent/lookdev-nightly-20260816",
    "environment": "T01_DirectionalOnly",
    "camera": "C11_Face3Quarter",
    "character": "default_mmd_character",
    "pose": "T-Pose",
    "exposure": { "ev100": 0, "auto": false },
    "shader_variant": "A",
    "timestamp": "2026-08-16T03:45:00Z",
    "resolution": { "w": 1920, "h": 1080 }
}
```

## A/B Contact Sheet Layout

```
┌──────────────────┬──────────────────┐
│   Baseline (A)   │  Candidate (B)   │
│   C01_3Quarter   │   C01_3Quarter   │
├──────────────────┼──────────────────┤
│   Baseline (A)   │  Candidate (B)   │
│  C11_FaceCloseup │  C11_FaceCloseup │
├──────────────────┼──────────────────┤
│   Baseline (A)   │  Candidate (B)   │
│  C20_HairDetail  │  C20_HairDetail  │
├──────────────────┼──────────────────┤
│   Baseline (A)   │  Candidate (B)   │
│  C30_MatBalls    │  C30_MatBalls    │
└──────────────────┴──────────────────┘
        [Difference Heatmap]
```

## Metrics Per Screenshot

| Metric | Formula | Purpose |
|---|---|---|
| Mean Luminance | `mean(0.299R + 0.587G + 0.114B)` | Overall brightness |
| Highlight Clipping | `% pixels where all(R,G,B) > 0.95` | Overexposure detection |
| Shadow Clipping | `% pixels where all(R,G,B) < 0.02` | Underexposure detection |
| RGB Mean | `(meanR, meanG, meanB)` | Color balance |
| SSIM (A vs B) | Structural similarity index | Perceptual regression |

## Regression Thresholds

| Metric | Threshold | Action |
|---|---|---|
| Mean Luminance Δ | ±0.15 | Flag for review |
| Highlight Clipping Δ | +5% | Auto-reject |
| Shadow Clipping Δ | +5% | Auto-reject |
| SSIM | < 0.90 | Flag for review |
| NaN pixels | > 0 | Auto-reject |
| Black screen (mean < 0.01) | any | Auto-reject |
| White screen (mean > 0.99) | any | Auto-reject |

## Technical Gate (before MAIN_REASONER review)

For each technical environment:
1. Run screenshot comparison against baseline
2. Compute all metrics
3. If any **auto-reject** threshold triggered → REJECT immediately
4. If all pass → send 4 key screenshots to MAIN_REASONER for visual review
5. MAIN_REASONER votes ACCEPT / REJECT with reasoning
