# VMD/PMX Animation Bake Notes

This document records the current VMD import design and the bugs found while aligning the plugin with MMD-style playback.

## Current Phase Task

- UI cleanup: replace the old Slate-composed tool surface with the UMG-based tool panel, including a clear layout, visual hierarchy, and RenderTarget preview area.
- Tool interaction optimization: refine the workflow for model import, motion loading, physics baking, preview playback, and status feedback.
- Algorithm work is not the main focus in this phase; only the integration needed to keep the tool UI and preview workflow usable should be changed.

## Goal

The plugin should import a VMD motion onto a PMX-derived skeletal mesh and bake the result into a UE `UAnimSequence`.

The intended behavior is closer to MikuMikuDance / MMD editor playback than to a generic UE retarget:

- VMD bone names are matched against original PMX bone names.
- PMX append/grant and leg IK are evaluated using PMX model data.
- The final result is written as UE animation tracks.

## Final Pipeline

1. PMX import creates a read-only `UMMDModelDataAsset`.
   - Stores PMX bone names, parent indices, flags, append/grant data, local axes, IK chains, and UE skeleton mapping.
   - Does not store the source file path as runtime model data.
   - `ModelId` is derived from the PMX asset/file name, not from VMD model name.

2. VMD import builds a PMX-indexed track map.
   - VMD bone names are resolved against PMX `NameJP` / `NameEN`.
   - This mirrors the idea of PMX bone map mode in MMD tools: VMD should target PMX model bones, not sanitized UE names first.

3. The evaluator builds a PMX-space pose.
   - Initial PMX local transform is built from PMX rest positions:
     `Bone.Position - Parent.Position`.
   - VMD translation is applied in MMD/PMX local motion semantics.
   - VMD FK rotation is kept as the original VMD quaternion in PMX space.

4. Only standard MMD leg IK is solved.
   - Enabled IK chains:
     - `右足ＩＫ`
     - `左足ＩＫ`
     - `右つま先ＩＫ`
     - `左つま先ＩＫ`
   - Hand/arm IK is not assumed for normal dance VMD files.

5. Append/grant is limited to leg IK support bones.
   - Global append/grant evaluation polluted unrelated bones such as hair accessories, wrists, and fingers.
   - The current path evaluates append/grant only for bones directly needed by standard leg IK support.

6. The PMX-space pose is converted to UE local tracks.
   - PMX component transforms are converted through the common MMD-to-UE coordinate conversion.
   - UE local transforms are then derived relative to the UE skeleton parent.

7. Only selected tracks are written.
   - Always write original VMD tracks.
   - Additionally write standard leg IK bones, targets, links, and directly required leg IK append/grant support bones.
   - Do not write all mapped PMX bones.

## Bugs Found

### Writing only VMD tracks is not enough

Symptom:

- CH4NGE motion caused both feet to merge or move incorrectly.
- Red Horse motion looked acceptable.

Cause:

- Some models require helper/deformation bones from the PMX leg IK chain.
- If only the raw VMD tracks are written, baked leg pose data is incomplete.

Fix:

- Include standard leg IK chain bones and directly required leg IK support bones in `TracksToWrite`.

### Writing all PMX bones is too broad

Symptom:

- Legs became correct.
- Head accessories, wrists, and fingers became visibly wrong.

Cause:

- The PMX-space evaluator computed all bones, but append/grant support was not accurate enough for every model feature.
- Writing all 400+ mapped bones forced bad results onto unrelated decorative and hand bones.

Fix:

- Keep full PMX-space computation available, but only write selected tracks needed for VMD and standard leg IK.

### Append/grant must not run globally

Symptom:

- Even after limiting written tracks, arms/hands could still be wrong.

Cause:

- Append/grant was still evaluated across the entire model before track selection.
- Hand/wrist/finger append bones were modified even when not intended.

Fix:

- Limit append/grant evaluation to the standard leg IK support set.

### VMD FK rotation must not be remapped through PMX local axis

Symptom:

- `ArmHandExtra=0`, but arms/hands were still wrong.
- This proved hand bones were not polluted by extra written helper bones.

Cause:

- VMD FK rotations were being remapped through PMX local axis.
- PMX local axis is not the general transform basis for VMD FK rotation tracks.
- Applying it to normal VMD FK tracks corrupts wrist/finger/arm rotation on models with custom local axes.

Fix:

- Keep VMD FK rotation as the original VMD quaternion in PMX space.
- Use the common PMX/MMD-to-UE conversion only when producing final UE transforms.
- Reserve local-axis handling for future IK/link-limit/axis-constraint work.

### VMD translation must not be remapped through PMX local axis

Symptom:

- IK target positions could be interpreted incorrectly on some models.

Cause:

- PMX local axis was being applied to VMD translation.

Fix:

- Keep VMD translation in raw MMD/PMX motion space.

## Debug Log Fields

Important log line:

```text
[MMD IK Debugger] MMD ModelData PMX-space evaluator | PMXBones=... | PMXTracks=... | MappedTracks=... | MissingUETracks=... | PMXRuntime=... | PMXIK=... | WriteSelectedBones=... | IKAppendBones=... | ArmHandTracks=... | ArmHandExtra=...
```

Field meanings:

- `PMXBones`: bones stored in `UMMDModelDataAsset`.
- `PMXTracks`: VMD tracks resolved to PMX bones.
- `MappedTracks`: PMX tracks mapped to UE skeleton bones.
- `MissingUETracks`: PMX tracks without UE skeleton mapping.
- `PMXRuntime`: PMX runtime bones available for evaluation.
- `PMXIK`: PMX IK chains found.
- `WriteSelectedBones`: final number of UE bone tracks to write.
- `IKAppendBones`: standard leg IK affected bones and support bones.
- `ArmHandTracks`: hand/arm/finger bones with original VMD tracks.
- `ArmHandExtra`: hand/arm/finger bones written without original VMD tracks. This should normally be `0`.

## Known Limitations

- Only standard MMD leg IK is enabled for bake.
- VMD IK enable/disable keyframes are parsed but not yet fully applied as per-frame IK switches.
- Full MMD append/grant semantics are not yet complete enough for safe whole-model bake.
- Morph curves are analyzed but not fully integrated into the UE animation playback path.
- PMX local-axis handling for IK link limits is intentionally conservative. It should not be reused for normal VMD FK tracks.

## Practical Rules

- Do not solve this by increasing IK iterations.
- Do not write every mapped PMX bone unless the full append/grant model is correct.
- Do not apply PMX local axis to VMD FK rotation or VMD translation.
- Treat VMD as targeting PMX model bones first, then convert the final pose into UE animation data.
- If legs break, inspect standard leg IK chain write coverage.
- If hands break and `ArmHandExtra=0`, inspect FK rotation conversion.
- If accessories break, inspect whether non-VMD decorative bones are being written.



