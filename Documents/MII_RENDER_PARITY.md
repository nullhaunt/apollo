# Mii head rendering parity

Apollo's Mii preview must show the same canonical 3D head on NX64 and x64. Front, three-quarter, profile, yaw, pitch, and distance must change the visible head on both platforms. A static image is not parity.

## Current boundary

- NX64 constructs `nn::mii::CharModel`, generates its faceline and normal mask textures, and records the head draw through `nn::gfx` views of Apollo's NVN device and command buffer. `nn::gfx` is the SDK layer over the same NVN backend here. The first on-device draw showed an upright head with inverted facial features. With the source vertical-flip flag enabled for both `Faceline` and `Mask`, the owner confirmed the features are upright on NX64 Debug from commit `907412d`.
- x64 enumerates the SDK Generic Mii defaults and loads the shape and texture inputs. It validates a cooked package for Generic default #0, uploads its geometry and textures into Vulkan, and draws an opaque/translucent head with depth and the shared preview camera. The camera controls are active when the head renderer is ready.
- The installed NintendoSDK 18.3.1 x64 gfx target defaults to GL4. Its Vulkan interop header and Vulkan Mii texture resources are absent. Apollo's x64 application stays on Vulkan; adding a live OpenGL context to the application would create a second runtime graphics backend.
- The installed SDK's Generic `MiiSimple` sample builds and runs after the owner's Smart App Control fix. Apollo's offline probe successfully exports the first Generic default's geometry and textures.
- The x64 Vulkan swapchain has a D32 depth attachment. Debug runs with Vulkan validation enabled showed front, three-quarter (35 degrees), and profile (90 degrees) views. A resize to a 984 x 611 drawable extent recreated the depth target and head renderer, kept both debug windows visible, and exited cleanly after a window close. No validation messages were reported. These are host runtime observations; an NX64 hardware comparison of the same default is still pending.

## Geometry feasibility probe

`Tools/CookMiiPreview.ps1 -SdkRoot <NintendoSDK>` generates an ignored local copy of the SDK Generic sample under `Build/MiiGeometryProbe`, builds it, clears prior probe outputs, runs it, and packs `Default0.apmp`. The SDK source and its resource binaries remain outside source control. The generated sample selects Generic default #0 and exports its opaque `CharInfo` snapshot, all present draw parts' public position, UV, and index buffers, modulation type, cull mode, and constant colors. It also reads back the generated faceline and normal-expression mask through `nn::gfx`, and each present static `TextureView` through the Generic GL4 context. This GL4 access occurs only inside the offline SDK sample; Apollo's x64 runtime remains Vulkan-only.

The first default exports six draw parts and three required textures: faceline 256 x 512, mask 512 x 512, and nose line 96 x 96. The independent `nn::gfx` and GL4 readbacks of faceline and mask match byte for byte. The package is 1,617,152 bytes. The packer rejects missing or malformed geometry and textures, nonfinite positions and UVs, and out-of-range indices. The package header records a format version, total length, part and texture counts, the captured `CharInfo`, a hash of the two Mii resource files, and a body hash. SDK Generic default snapshots vary in their first 16 bytes between process launches while the geometry and texture outputs remained identical across repeated probe runs. Therefore, the runtime must not compare all 88 snapshot bytes to a fresh catalog entry as an identity test.

## x64 implementation contract

An offline tool uses the SDK's Generic Mii path to construct the selected `CharModel` and generate its face textures. It writes an Apollo-owned preview package containing the draw-part positions, UVs, indices, cull and blend categories, modulation colors, and sampled RGBA textures. The package is versioned and records its input `CharInfo` snapshot and resource configuration. SDK resource files and generated shader binaries stay outside source control.

At runtime, Apollo loads the package into Vulkan vertex, index, and image resources and draws it with a depth target and the shared preview camera. Re-cooking a different catalog entry refreshes the package without changing the Vulkan backend. The renderer must preserve opaque/translucent draw order and the SDK's documented modulation modes.

## Acceptance gates

1. Prove the offline tool can read every required draw-part buffer and sample each required texture from one Generic default Mii. Reject incomplete or mismatched packages rather than showing a partial face. **Passed for default #0, including runtime rejection of a deliberately altered package.**
2. Draw that package on x64 with Vulkan. Verify visible camera motion, depth and translucency, resize, and clean shutdown with Vulkan validation enabled. **Passed for default #0 on the host; visual comparison with NX64 remains in gate 4.**
3. Switch among the six Generic defaults without stale geometry or face textures. Keep console-only Miis on NX64; do not substitute host defaults for them.
4. Compare the same default Mii at front, three-quarter, and profile on x64 and NX64. Record the two runtime results separately. Build success alone does not establish visual parity.
