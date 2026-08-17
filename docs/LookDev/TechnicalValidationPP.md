# Technical Validation Post-Process Settings

> Generated for P2: Lock Camera / Exposure / PP
> These settings ensure deterministic shader evaluation without artistic interference.

---

## Manual Exposure Settings

| Parameter | Value | Notes |
|---|---|---|
| Auto Exposure | **OFF** | `bOverride_AutoExposureMethod = true, AutoExposureMethod = AEM_Manual` |
| EV100 | **0.0** | Neutral exposure (ISO 100, f/1, 1/100s equivalent) |
| Exposure Compensation | **0.0** | No offset |
| Min/Max EV100 | **0.0 / 0.0** | Fixed at 0 |

## Disabled Post-Process Effects

| Effect | Status | Reason |
|---|---|---|
| Bloom | **OFF** | `bOverride_BloomIntensityScale = true, BloomIntensityScale = 0` |
| Depth of Field | **OFF** | `bOverride_DepthOfFieldFocalDistance = true, FocalDistance = 0` (no blur) |
| Vignette | **OFF** | `bOverride_VignetteIntensity = true, VignetteIntensity = 0` |
| Chromatic Aberration | **OFF** | `bOverride_ChromaticAberrationStartOffset = true, StartOffset = 0` |
| Local Exposure | **OFF** | `bOverride_LocalExposureContrastScale = true, = 1.0` (neutral) |
| Lens Flares | **OFF** | `bOverride_LensFlareIntensity = true, LensFlareIntensity = 0` |
| Screen Space Reflections | **OFF** | `bOverride_ScreenSpaceReflectionIntensity = true, = 0` |
| Ambient Occlusion | **OFF** | `bOverride_AmbientOcclusionIntensity = true, = 0` |
| Motion Blur | **OFF** | `bOverride_MotionBlurAmount = true, MotionBlurAmount = 0` |
| Film Grain | **OFF** | `bOverride_FilmGrainIntensity = true, FilmGrainIntensity = 0` |

## Preserved Effects

| Effect | Status | Notes |
|---|---|---|
| Tonemapper | **Project Default** | Do NOT change per-environment. Use project's ACES or filmic curve. |
| Color Grading | **Neutral** | No LUT, no color tint. White balance = 6500K (D65). |

## Camera Settings

| Parameter | Value | Notes |
|---|---|---|
| FOV | **35°** | Standard portrait/closeup FOV (avoids wide-angle distortion) |
| Aspect Ratio | **16:9** | 1920×1080 canonical screenshot resolution |

## Standard Camera Positions

| ID | Name | Position (cm) | LookAt (cm) | Notes |
|---|---|---|---|---|
| C00 | FullFront | (0, -240, 110) | (0, 0, 80) | Full body, front view |
| C01 | Full3Quarter | (160, -190, 110) | (0, 0, 80) | 3/4 view, standard anime angle |
| C02 | Profile | (240, 0, 100) | (0, 0, 80) | Side profile |
| C03 | Back | (0, 240, 110) | (0, 0, 80) | Back view (rim/silhouette) |
| C10 | FaceFront | (0, -120, 150) | (0, 0, 150) | Face closeup, straight on |
| C11 | Face3Quarter | (80, -100, 150) | (0, 0, 150) | Face 3/4 closeup |
| C20 | HairCloseup | (60, -80, 180) | (0, 0, 180) | Hair detail |
| C21 | EyeCloseup | (0, -80, 155) | (0, 0, 155) | Eye detail |
| C30 | MaterialBalls | (0, -150, 80) | (0, 0, 60) | Gray/white/black spheres + character |

## Gray Card / Reference Objects

Each technical level should include:
- **18% Gray Card** (sphere or plane, neutral gray 0.18)
- **Matte White** sphere (0.90 reflectance)
- **Matte Black** sphere (0.05 reflectance)
- **Standard MMD Character** (fixed pose)
- **Glossy/Metallic spheres** (for specular reference)
