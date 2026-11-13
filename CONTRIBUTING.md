# Contributing to Ue5MMDTools

Thanks for helping improve Ue5MMDTools. The project targets Unreal Engine 5.8.3 and Win64 first.

## Before opening an issue

- Check the latest release notes and existing issues.
- Confirm whether you are using the source `dev` branch or a released plugin package.
- Do not upload third-party PMX models, textures, motions, songs, or other assets unless you have permission to redistribute them.
- For a rendering report, include the GPU, RHI, shader model, material path, and a small reproducible description.

## Bug reports

Use the bug template and include:

- UE version and plugin version;
- Windows version and GPU;
- source branch or release tag;
- model type and whether the model was imported or loaded from an existing asset;
- exact steps and expected/actual result;
- log excerpt or screenshot when available.

## Pull requests

1. Make changes on a branch based on `dev`.
2. Keep commits focused and explain user-visible behavior.
3. Do not commit `Binaries`, `Intermediate`, `Saved`, `DerivedDataCache`, local model assets, or generated caches.
4. For shader changes, follow the project's Custom node and universal geometry-derived shading constraints.
5. Run the narrowest relevant validation and report what could not be run because of local UE/toolchain limits.
6. Update the relevant documentation and changelog for user-visible changes.

The `main` branch is a release distribution branch. Changes normally land through `dev` and are exported into `main` for a release.

## Asset and license policy

The MIT license applies to project code. It does not apply to third-party MMD models, motions, textures, music or images. Keep original notices and link to the author source. Do not add an asset to the repository merely because it is usable in a local test.
