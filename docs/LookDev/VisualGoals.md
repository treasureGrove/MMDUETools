# Visual Goals — Modern Anime UE Rendering

> Based on analysis of high-quality modern anime game rendering
> Silver Palace (白银之城) as key visual reference

---

## Face Rendering Goals

### Visual Characteristics to Achieve
- **Clean value grouping**: Face has clear lit/shadow division with NO ugly nose ridge or mouth shadows from raw NdotL
- **Smooth SDF transition**: Face shadow from SDF (head bone based) rotates smoothly as light direction changes
- **Stable main light**: Multiple lights don't cause shadow to jump/flip between them
- **Tinted shadows**: Shadows have hue (e.g., warm skin shadow shifts slightly purple/warm red), NOT pure black
- **HDR face readability**: Even in high-dynamic-range scenes (bright sky + dark interior), face remains readable
- **Subtle specular**: Nose bridge and forehead get a soft, CONSERVATIVE highlight — face should NOT look oily

### Technical Requirements
- Face shader uses SDF-based shadow (not NdotL) — currently using `GetMainLightDirection` + `TMMDAnimeFace`
- Shadow softness: 0.15 (smoother than base toon 0.10)
- Shadow lift: +0.12 × BaseColor (prevents face going too dark)
- Specular: very restrained (SpecStep 0.85, power 64, intensity 0.5×)
- Rim light: essential for face silhouette glow (strength 0.6)

---

## Skin Rendering Goals

### Visual Characteristics
- **Anime shape preservation**: Clear toon bands, NOT smooth gradient
- **No plastic look**: Avoid too-perfect specular that makes skin look like plastic
- **Soft, moderate highlights**: Gentle glow on shoulders, collarbone, nose bridge
- **Clean shadow boundary**: No dirty/muddy transition between lit and shadow
- **Hue preservation in shadow**: Dark areas maintain skin tone, don't go gray or muddy
- **Environmental color influence**: Sky ambient affects skin but doesn't overwhelm natural skin tone

### Technical Note
TMMDAnimeSkin.usf is dead code — all skin falls through to MMDBaseToon.usf. This is actually acceptable since BaseToon has good skin response. Consider integrating TMMDAnimeSkin's per-light approach into a future dedicated skin parent material if needed.

---

## Hair Rendering Goals

### Visual Characteristics
- **Large, clean shadow blocks**: Hair shadow should be chunky and anime-like, not gradient
- **Clear silhouette**: Hair edge reads clearly against any background
- **Directional highlights**: Specular follows hair tangent direction (Kajiya-Kay anisotropic)
- **"Angel ring" secondary highlight**: A broader secondary specular creates the characteristic anime hair glow
- **Backlight silhouette**: Under rim/back light, hair edges glow beautifully
- **NOT matcap-dependent**: Highlights should respond to real lights, not fixed camera-space matcap

### Current Implementation Status
- Kajiya-Kay anisotropic specular: ✓ (implemented in TMMDAnimeHair.usf)
- Primary + secondary highlight: ✓ (Spec1 + Spec2×SecondaryStrength)
- Core shadow at boundary: ✓ (gaussian, 0.25 intensity)
- Cool-tinted rim: ✓ (0.75, 0.85, 1.0 × 0.22)
- MMD ShadowMap integration: ✓

### Improvement Candidates
- Anisotropic highlight may need tangent direction verification (model tangents must follow hair strand)
- Secondary highlight width may need tuning per-hair-style
- Backlit silhouette intensity may be too low

---

## Eye Rendering Goals

### Visual Characteristics
- **Clear sclera/iris separation**: White of eye and colored iris have distinct rendering
- **Stable specular highlights**: Catch lights (キャッチライト) stay in consistent position
- **Iris self-glow**: Iris remains visible even in deep shadow (anime eyes "glow")
- **Wet/specular feel**: Eye has a moist, alive quality
- **Dark-environment readability**: Iris color reads even when surrounding face is dark

### Current Implementation Status
- IrisMask: ✓ (luma + saturation based separation)
- Iris self-glow: ✓ (IrisGlow parameter, default 1.0)
- Glint (catch light): ✓ (Gaussian at GlintPos, Fresnel fade at edges)
- Eyelash shadow: ✓ (top-down gradient)
- Iris bottom gradient: ✓ (upper-dark/lower-bright)
- Wet specular: ✓ (ToonSpecularLight with high power 100)

### Known Limitation
- Glint position is in UV space — different eye models may need different positions
- No eye-tracking or parallax (single UV lookup)

---

## Cloth Rendering Goals

### Material Differentiation (NOT all "toon plastic")

| Material Type | Roughness | Specular Shape | Highlight Control | Example |
|---|---|---|---|---|
| Cotton | High | Broad, soft diffuse | Minimal specular | School uniform |
| Leather | Medium | Moderate, tighter | Some specular + matcap potential | Boots, jacket |
| Silk-like | Low | Tight, directional | Strong specular, anisotropic potential | Dress, ribbon |
| Metal Decoration | Very low | Sharp, mirror-like | Strong specular + environment | Buttons, accessories |

### Current Implementation Status
TMMDAnimeCloth.usf simply includes MMDBaseToon.usf — all cloth uses identical shading. No material differentiation exists yet. This is a **P8+ improvement target**.

### Future Direction
- Cloth differentiation through SpecularPower, ShadowStep, and HighlightStep parameters
- Per-material roughness estimation from PMX material properties
- Possible cloth-specific shader with roughness-aware specular

---

## Shadow Color Goals

### Visual Characteristics
- Shadows are **never pure black** — always tinted
- Shadow hue follows the "opposite warm/cool" principle:
  - Warm light → slightly cool-purple shadow
  - Cool light → slightly warm shadow
  - Neutral light → slightly desaturated shadow
- Shadow color is per-material (face gets different shadow than hair/cloth)
- Scene shadows (MMD ShadowMap) have clean edges with PCF softness
- No peter-panning or shadow acne

### Current Implementation Status
- ShadowColor parameter per material: ✓
- MMD ShadowMap with 4-tap PCF: ✓
- Shadow color is multiplicative (`BaseColor × ShadowColor`): preserves hue
- **Gap**: No automatic warm/cool shadow color adaptation based on light color

---

## Highlight Control Goals

### Visual Characteristics
- Highlights follow the "less is more" principle
- Hair: directional, controlled brightness
- Face: very subtle, NOT oily
- Eyes: sharp, wet
- Cloth: varies by material type
- No highlight explosion (NaN, clamping issues)
- Highlight intensity scales with actual light intensity (not constant)

### Current Implementation Status
- Soft specular (pow-based) gated to lit area: ✓
- Sharp toon specular (smoothstep-based) gated to lit area: ✓
- Both scale with light intensity via `C.w/PI`: ✓
- **Gap**: No per-material highlight intensity scaling
- **Gap**: Matcap can override real highlight response (H008)
