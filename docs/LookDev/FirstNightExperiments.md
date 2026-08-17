# First Night Experiments

> Experiments for validating the P0 lighting contract findings
> Each experiment: ONE hypothesis, ONE primary change

---

## E001: Weak Auxiliary Light Ramp Influence (H002)

### Hypothesis
A 10% intensity auxiliary directional light, added to a 100% main directional, will produce an asymmetric contribution to the Toon Ramp because DirLight is additive and excludes intensity.

### Experiment Design
- **Environment**: Custom two-directional setup
  - Main: 100% white directional at (-55, -25, 0) rotation
  - Auxiliary: 10% warm-white (1, 0.9, 0.8) directional at (0, 90, 0) rotation
- **Character**: Standard MMD character, default pose
- **Camera**: C01_Full3Quarter

### Expected Results
- Baseline (main only): Clean 2-tone face with shadow on right side
- Candidate (main + 10%): Left side gets slightly warmer. Toon Ramp on face shifts slightly toward lit on the auxiliary-light side.
- **Key question**: Does the 10% auxiliary light create a noticeable ramp shift, or is it negligible?

### Accept Criteria
- Ramp shift exists but is subtle (< 10% of ramp width)
- No ugly artifacts on face (no random dark bands)
- Color temperature difference is pleasant

### Reject Criteria
- Ramp shifts > 30% (auxiliary light dominates)
- Visible dark artifacts or banding
- Face shadow jumps between main and auxiliary

---

## E002: Face Main-Light Stability (H003)

### Hypothesis
When two lights have similar intensities, `GetMainLightDirection()` may flip between them, causing Face SDF shadow to jump discontinuously.

### Experiment Design
- **Environment**: Two directional lights at equal intensity (50% each)
  - Light A: Front-right (-45, -30, 0)
  - Light B: Front-left (45, -30, 0)
- **Character**: Standard MMD character with Face material
- **Camera**: C10_FaceFront, C11_Face3Quarter
- **Test**: Rotate character slowly (if possible) or observe face shadow from different angles

### Expected Results
- With equal-intensity lights, GetMainLightDirection picks the one with lower slot index (Light A, as it's first found)
- If character rotates, the shadow should track one light consistently, not flip

### Accept Criteria
- Face shadow remains stable across camera angles
- Shadow tracks a single "main" light direction

### Reject Criteria
- Visible shadow jumping/flipping between lights
- Shadow oscillates between two positions

---

## E003: SkyAmbient Preview vs World (H004/H005)

### Hypothesis
PreviewScene lighting produces different SkyAmbient than World lighting because PreviewScene may lack a proper SkyLight actor, resulting in zero SH coefficients.

### Experiment Design
- **Environment**: T03_SkyOnly (once built) or Daylight preset
- **Test A**: Open in editor viewport (World path) — screenshot
- **Test B**: Apply same environment to PreviewScene — screenshot
- **Compare**: Mean luminance, color balance

### Expected Results
- If SkyLight is properly set up in both: Results should match within 5%
- If Preview lacks SkyLight: Preview will be significantly darker

### Accept Criteria
- Luminance difference < 10%
- Color balance matches (no hue shift)

### Reject Criteria
- One path is completely dark
- Color shifts > 0.1 in any channel

---

## E004: Skin/Cloth Shader Dead Code (H006)

### Hypothesis
TMMDAnimeSkin.usf and TMMDAnimeCloth.usf are never included in any material because no parent material is wired to them.

### Experiment Design
- **Test**: Import a PMX model, check material assignments
- **Verify**: grep for TMMDAnimeSkin / TMMDAnimeCloth in material assets
- **Action**: None required — just documenting

### Expected Results
- TMMDAnimeSkin.usf is standalone and duplicates MMDToonLighting.ush functions
- No parent material (M_MMD_Base_Opaque etc.) references these shaders
- All opaque materials use MMDBaseToon.usf

### Decision
- If confirmed dead code: Document as known issue, do not delete (may want to integrate later)
- If actually used somewhere: Update MaterialMatrix.md

---

## Experiment Execution Order

1. **E003** (SkyAmbient Preview vs World) — lowest risk, most diagnostic
2. **E001** (Weak auxiliary ramp) — validates H002
3. **E002** (Face main-light stability) — validates H003
4. **E004** (Dead code check) — documentation only

Each experiment follows the strict flow:
1. Read NightlyState
2. Collect baseline screenshot
3. Apply change (if code change needed)
4. Build + shader compile
5. Launch editor
6. Capture screenshots
7. Generate A/B comparison
8. Technical Gate (auto-reject if metrics fail)
9. MAIN_REASONER visual review
10. Accept/Reject
11. Commit or revert
12. Update NightlyState
