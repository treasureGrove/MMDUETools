# LookDev Regression Matrix

> Template for per-experiment regression testing
> Each experiment must pass ALL rows in Section A before visual review

---

## Section A: Technical Gate (Auto-Reject if Failed)

### A1: NaN/Inf Check
| Check | Env | Camera | Threshold | Status |
|---|---|---|---|---|
| No NaN pixels | ALL | ALL | 0 NaN pixels | ☐ |

### A2: Brightness Extremes
| Check | Env | Camera | Threshold | Status |
|---|---|---|---|---|
| No black screen | ALL | ALL | Mean luminance > 0.01 | ☐ |
| No white screen | ALL | ALL | Mean luminance < 0.99 | ☐ |
| No overexposure | T09_HDR | ALL | Highlight clip < 5% | ☐ |
| No underexposure | T10_NoSky | ALL | Shadow clip < 90% | ☐ |

### A3: Per-Environment Baseline
| Environment | Key Validation | Auto-Reject Condition |
|---|---|---|
| T00_NeutralStudio | Gray card luminance ≈ 0.18 | Δ > ±0.05 |
| T01_DirOnly | Clean 2-tone face | Face completely black or white |
| T02_DirSoft | Soft gradient visible | Completely flat or harsh edges |
| T03_SkyOnly | Non-zero ambient | Completely black |
| T04_PointOnly | Distance falloff visible | No falloff (all lit) or all dark |
| T05_SpotOnly | Cone visible | No cone effect or all dark |
| T06_RectOnly | Front-face lit | All dark or no directional response |
| T07_WarmCool | Color mixing visible | Pure white or monochrome |
| T08_BackLight | Rim/silhouette visible | No rim or all black face |
| T09_HDR | Highlight preserved | All white or all black |
| T10_NoSky | Still readable | Completely black |
| T11_Shadow | Shadow visible | No shadow or all shadowed |
| T12_IBL | Environment response | Identical across all rotations |

---

## Section B: Per-Material Stability

### B1: Face
| Environment | Check | Baseline (score) | Candidate (score) | Δ Threshold |
|---|---|---|---|---|
| T01 | Face readability (0-5) | | | No regression |
| T01 | No ugly nose shadow | | | Binary pass/fail |
| T07 | Multi-light stability | | | No jumping |
| T08 | Face readable in backlight | | | Not all black |

### B2: Hair
| Environment | Check | Baseline (score) | Candidate (score) | Δ Threshold |
|---|---|---|---|---|
| T01 | Clean shadow blocks | | | No regression |
| T01 | Directional highlight | | | Present |
| T08 | Backlit rim glow | | | Visible |

### B3: Eye
| Environment | Check | Baseline (score) | Candidate (score) | Δ Threshold |
|---|---|---|---|---|
| T01 | Iris visible | | | Not all black |
| T10 | Iris readable in dark | | | Iris still visible |
| T01 | Catch light visible | | | Present |

### B4: Generic (Cloth/Skin)
| Environment | Check | Baseline (score) | Candidate (score) | Δ Threshold |
|---|---|---|---|---|
| T01 | Toon bands visible | | | Not flat |
| T07 | Color light response | | | Not monochrome |

---

## Section C: Multi-Environment Stress Test

Run after experiment accepted. Random sampling of light configurations:

| Test | Configuration | Expected |
|---|---|---|
| Random DirAngle | 5 random azimuth × 3 elevation | No NaN, face readable |
| Random DirColor | 5 random hue, 80-100% sat | No color cast explosion |
| Random DirIntensity | 0.5 - 10.0 | No over/underexposure |
| Random PointPos | 5 random positions | Distance falloff correct |
| Random SkyRotation | 0°, 90°, 180°, 270° | Ambient changes visible |

---

## Decision Flow

```
Experiment Change
  │
  ├─ Section A: Technical Gate
  │   ├─ ANY auto-reject triggered? → REJECT (no visual review needed)
  │   └─ All pass → continue
  │
  ├─ Section B: Per-Material Check
  │   ├─ 2+ materials regressed? → REJECT
  │   └─ 0-1 materials regressed → continue
  │
  ├─ Visual Review (MAIN_REASONER)
  │   ├─ 4 key screenshots reviewed
  │   ├─ ACCEPT or REJECT with reasoning
  │   └─ If REJECT: save experiment notes + revert
  │
  └─ Section C: Stress Test (post-acceptance)
      ├─ Pass → commit accepted
      └─ Fail → revert with regression report
```
