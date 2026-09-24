# Mii head rendering parity

Apollo's Mii preview must show the same canonical 3D head on NX64 and x64. Front, three-quarter, profile, yaw, pitch, and distance must change the visible head on both platforms. A static image is not parity.

## Current boundary

- NX64 constructs `nn::mii::CharModel`, generates its faceline and normal mask textures, and records the head draw through `nn::gfx` views of Apollo's NVN device and command buffer. `nn::gfx` is the SDK layer over the same NVN backend here. The first on-device draw showed an upright head with inverted facial features. With the source vertical-flip flag enabled for both `Faceline` and `Mask`, the owner confirmed the features are upright on NX64 Debug from commit `907412d`.
- x64 enumerates the SDK Generic Mii defaults and loads the shape and texture inputs, but does not yet construct or draw a Mii head. The camera controls are hidden on x64 until they affect a visible preview.
- The installed NintendoSDK 18.3.1 x64 gfx target defaults to GL4. Its Vulkan interop header and Vulkan Mii texture resources are absent. Apollo's x64 application stays on Vulkan; adding a live OpenGL context to the application would create a second runtime graphics backend.
- The installed SDK's Generic `MiiSimple` sample builds with the installed Windows SDK and MSVC overrides. Application Control blocked launching that newly built sample from this shell, so its runtime Mii output is not yet verified.

## Geometry feasibility probe

`Tools/BuildMiiGeometryProbe.ps1` generates an ignored local copy of the SDK Generic sample under `Build/MiiGeometryProbe`, inserts Apollo's `MiiGeometryProbe.inl` hook, and builds it. The SDK source remains outside source control. The hook writes `MiiGeometryProbe.apmg` in the executable's working directory after the sample constructs its normal-expression model. It records each present draw part's public position, UV, and index buffers, modulation type, cull mode, and constant colors. The probe validates buffer sizes and index bounds. This is a geometry feasibility artifact, not the final preview package: textures and Vulkan rendering are still pending.

The probe compiles with the installed SDK and toolchain. Windows Application Control blocked launching the generated executable both in the normal shell and with temporary full access. No geometry file was produced, so draw-buffer mapping remains unverified at runtime.

## x64 implementation contract

An offline tool uses the SDK's Generic Mii path to construct the selected `CharModel` and generate its face textures. It writes an Apollo-owned preview package containing the draw-part positions, UVs, indices, cull and blend categories, modulation colors, and sampled RGBA textures. Static SDK textures can be sampled into a known RGBA target during cooking; the tool does not need to inspect their private storage. The package is versioned and tied to the input `CharInfo` snapshot and resource configuration. SDK resource files and generated shader binaries stay outside source control.

At runtime, Apollo loads the package into Vulkan vertex, index, and image resources and draws it with a depth target and the shared preview camera. Re-cooking a different catalog entry refreshes the package without changing the Vulkan backend. The renderer must preserve opaque/translucent draw order and the SDK's documented modulation modes.

## Acceptance gates

1. Prove the offline tool can read every required draw-part buffer and sample each required texture from one Generic default Mii. Reject incomplete or mismatched packages rather than showing a partial face.
2. Draw that package on x64 with Vulkan. Verify visible camera motion, depth and translucency, resize, and clean shutdown with Vulkan validation enabled.
3. Switch among the six Generic defaults without stale geometry or face textures. Keep console-only Miis on NX64; do not substitute host defaults for them.
4. Compare the same default Mii at front, three-quarter, and profile on x64 and NX64. Record the two runtime results separately. Build success alone does not establish visual parity.
