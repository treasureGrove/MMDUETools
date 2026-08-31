# AnimeToon Target v1 生成提示词

生成方式：Codex 内置 ImageGen。

参考图：

1. `参考/High_quality_MMD__MikuMikuDanc_2026-08-24T06-44-12.png`：二次元色块、柔和脸部、轮廓光与色彩关系。
2. `ShaderReferences/Miku_HighSculpt_Refs/03_miku_face_hair_material.png`：可实现的 3D 体积、头发结构和材质分区。

## Final prompt

```text
Use case: stylized-concept
Asset type: visual target reference for a universal Unreal Engine anime toon shader
Primary request: create one polished 3D anime character render that will serve as the exact look-development target for a new, separate cel shader
Input images: Image 1 is the reference for clean anime color blocks, soft face shading, gentle rim light, and appealing color harmony; Image 2 is the reference for plausible 3D form, hair volume, and readable material separation. Do not copy the exact character identity or outfit.
Scene/backdrop: simple neutral middle-gray studio cyclorama with a faint floor contact shadow, no props
Subject: original teal twin-tail virtual singer character, three-quarter full-body pose, face, hair, skin, white cloth, dark cloth, and small hard-surface accessories all clearly visible
Style/medium: high-quality real-time 3D cel shading, unmistakably anime, clean two-band diffuse shading driven by surface geometry, not painterly and not photorealistic PBR
Composition/framing: landscape look-development render, character occupies most of the frame, face large enough to judge skin/eyes/hair, full silhouette visible
Lighting/mood: one broad soft key plus restrained cool fill and a subtle rim; artistic non-physical light levels; neutral Unreal default-exposure appearance; stable midtones; no crushed blacks; no clipped whites
Color palette: saturated teal hair with preserved hue in shadow, warm natural skin, white and charcoal costume, small magenta accents
Materials/textures: mostly flat base colors; skin has a soft wide terminator and very restrained geometric highlight; hair has two clear shadow bands plus a broad directional highlight band; cloth has crisp toon bands; hard surfaces have one small sharp highlight; eyes use view/light-driven catchlights
Constraints: the intended shader must be achievable from base color, normals, world position, view direction, light direction, geometric tangent, and optional scene shadow only; no UV-specific masks, no SDF face texture, no companion threshold textures; clear readable cast shadow; no bloom, no lens flare, no depth-of-field blur, no text, no logo, no watermark
Avoid: photoreal skin pores, oily skin, metallic-looking hair, excessive gradients, glowing whites, cinematic overexposure, washed-out colors, black crushed shadows, painterly brushwork
```
