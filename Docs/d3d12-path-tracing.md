# D3D12 Path Tracing

This document describes the first Piccolo D3D12 path tracing mode and the operator limits that matter when enabling it.

## Runtime Selection

Path tracing is selected through the editor config key:

```ini
RenderSceneMode=Raster
```

Accepted values:

- `Raster`: use the existing raster renderer.
- `PathTracing`: request the D3D12 DXR path tracing renderer.

The default value is `Raster`. Keep this default in development and deployment configs unless a machine is known to support the D3D12 DXR path tracing path.

`RenderSceneMode` is the only runtime control for the first version. There is no editor UI toggle in this implementation. Unknown values should be treated as `Raster`.

## Backend Capability Behavior

D3D12 path tracing requires DXR-capable D3D12 hardware and driver support. On D3D12 devices that do not report ray tracing support, requesting `RenderSceneMode=PathTracing` automatically falls back to the raster renderer. This fallback should leave the editor usable and should not produce a black frame.

Vulkan exposes the shared ray tracing RHI interface shape only. In this implementation, Vulkan returns unsupported capability responses and ray tracing creation/build/dispatch calls fail or no-op safely. Requesting `PathTracing` on Vulkan therefore uses raster rendering.

If the Windows SDK or build environment does not expose the required DXR headers/types, D3D12 must still build and run as raster-only.

## Frame Integration

The path tracing renderer is a scene-color producer, not a replacement for the whole frame graph.

In effective `PathTracing` mode, D3D12 dispatches DXR outside graphics render-pass scope and writes HDR scene color to `_main_camera_pass_backup_buffer_odd`. That output then flows through the existing post and UI sequence:

1. tone mapping
2. color grading
3. FXAA, when enabled
4. clear UI attachment
5. editor axis/debug raster overlay
6. ImGui UI
7. combine UI

`UIPass` and `CombineUIPass` stay raster-based and keep their existing contract. ImGui is rendered through the existing UI backend, and `CombineUIPass` still composites scene color with the UI attachment. The path tracing path must not require changes to the UI pass or combine UI pass.

## First-Version Rendering Scope

The first usable version supports static opaque meshes only.

Excluded from the path traced TLAS:

- skinned or animated meshes
- transparent or blended materials
- any future overlay-only geometry that has not been exported as a static opaque path tracing instance

Excluded geometry is not drawn back over the path traced scene by a raster scene overlay in this plan. Operators should expect unsupported objects to be missing in `PathTracing` mode rather than composited from the raster renderer.

Materials use the data already available in the existing GPU resources. When a path tracing material input is unavailable, the renderer should use a simple fallback rather than blocking the frame. Sky or environment lighting may fall back to the existing clear/environment color when the existing IBL or skybox handles are not available to the path tracing pass.

## Progressive Accumulation

The first version traces one progressive sample per frame. A still camera should progressively accumulate over time.

Accumulation resets when any of these change:

- camera view or projection
- swapchain size, resize, maximize, restore, or framebuffer recreation
- scene membership, transforms, mesh data, or material data that affects the path traced scene
- switching path tracing mode on or off

After a reset, the sample index starts again from the first sample.

## Operator Notes And Limits

- Prefer `RenderSceneMode=Raster` for default configs and automated fallback coverage.
- Use `RenderSceneMode=PathTracing` only on Windows D3D12 systems with DXR support.
- Check logs for requested mode, effective mode, DXR support, and fallback messages when validating a machine.
- On unsupported D3D12 hardware, Vulkan, or raster-only SDK builds, the expected effective renderer is raster.
- Missing skinned, animated, transparent, or blended scene objects in path tracing mode are expected for the first version.
- There is no raster overlay for unsupported scene geometry in path tracing mode.
- UI, ImGui, combine UI, and editor axis/debug overlay should remain visible and raster-rendered.
- If camera motion, resize, or scene edits do not reset accumulation, treat that as a path tracing integration bug.
- If a static scene rebuilds BLAS/TLAS or recreates accumulation resources every frame, treat that as a performance bug.
- If path tracing output appears tone mapped twice or appears LDR before post processing, verify that `_main_camera_pass_backup_buffer_odd` still receives HDR scene color before the existing post chain.
