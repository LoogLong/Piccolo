# D3D12 Path Tracing Renderer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an optional hardware path tracing renderer that runs on D3D12 when DXR is supported, automatically falls back to the existing raster path otherwise, leaves Vulkan with interface-only unsupported responses, and keeps the UI rendering/composition unchanged.

**Architecture:** The path tracing path is a scene-color producer, not a replacement for the whole frame. D3D12 builds BLAS/TLAS data, dispatches a DXR ray generation shader into an HDR scene output, and hands that output back to the existing tone/color/FXAA/UI/combine flow. Vulkan receives the same RHI-facing API shape but returns unsupported/no-op so future Vulkan work can fill the interface without affecting current behavior.

**Tech Stack:** C++17, Piccolo RHI, D3D12/DXR (`ID3D12Device5`, `ID3D12GraphicsCommandList4`, `D3D12_FEATURE_D3D12_OPTIONS5`, state objects, shader tables), HLSL/DXC DXIL libraries (`lib_6_3` minimum, `lib_6_6` when the local DXC supports it), existing Piccolo render passes and smoke scripts.

---

## Current Context

The existing frame flow is:

1. `RenderPipeline::deferredRender()` / `forwardRender()` in `engine/source/runtime/function/render/render_pipeline.cpp`
2. shadow passes
3. `MainCameraPass::draw()` or `drawForward()`
4. main-camera subpasses from `engine/source/runtime/function/render/render_pass.h`
5. `UIPass::draw()` into `_main_camera_pass_backup_buffer_even`
6. `CombineUIPass::draw()` composites scene `_main_camera_pass_backup_buffer_odd` with UI `_main_camera_pass_backup_buffer_even`

Current subpasses are:

1. `_main_camera_subpass_basepass`
2. `_main_camera_subpass_deferred_lighting`
3. `_main_camera_subpass_forward_lighting`
4. `_main_camera_subpass_tone_mapping`
5. `_main_camera_subpass_color_grading`
6. `_main_camera_subpass_fxaa`
7. `_main_camera_subpass_ui`
8. `_main_camera_subpass_combine_ui`

Path tracing must preserve the UI contract:

- `UIPass` still renders ImGui through the existing D3D12/Vulkan UI backend.
- `CombineUIPass` still reads scene from binding 0 and UI from binding 1.
- The path tracing renderer writes scene color to the same logical scene input used by `CombineUIPass`, eventually `_main_camera_pass_backup_buffer_odd`.
- Editor axis/debug UI are not path traced.

Important existing data sources:

- Scene entities: `RenderScene::m_render_entities` in `engine/source/runtime/function/render/render_scene.h`
- Per-frame camera/lights: `RenderResource::updatePerFrameBuffer()` in `engine/source/runtime/function/render/render_resource.cpp`
- Mesh GPU buffers: `RenderMeshGPUResource` in `engine/source/runtime/function/render/render_gpu_resource.h`
- Material GPU data: `RenderPBRMaterialGPUResource` in `engine/source/runtime/function/render/render_gpu_resource.h`
- Existing smoke scripts:
  - `scripts/tests/render_backend/smoke_backend_boot.ps1`
  - `scripts/tests/render_backend/smoke_d3d12_editor_visible.ps1`

Scope for the first usable version:

- Static meshes only for DXR geometry. Animated/skinned meshes are either skipped from TLAS or rendered by the existing raster path until a skinned-BLAS strategy is implemented.
- One progressive sample per frame with accumulation reset on camera, resize, and scene changes.
- Basic PBR inputs: base color, metallic, roughness, normal, emissive when already available; diffuse fallback when a texture/material field cannot be bound in the first pass.
- Sky/environment fallback from existing IBL/skybox resources when accessible, otherwise the current clear/environment color.
- Vulkan exposes capability/interface stubs and keeps raster rendering.
- No new test code unless the user asks for it.

---

## Parallel Agent Map

Wave 1 can run in parallel after each agent reads this plan:

- Agent A: RHI capability and ray tracing interface contracts.
- Agent B: D3D12 DXR core backend.
- Agent C: Shader build and HLSL path tracing library.
- Agent D: Scene geometry/material export and BLAS/TLAS input ownership.
- Agent E: Render pipeline path selection and UI-preserving data flow.

Wave 2 starts after Wave 1 interfaces compile:

- Agent F: Accumulation, resize/reset, and scene/camera dirtiness.
- Agent G: Vulkan unsupported stubs and fallback behavior polish.
- Agent H: Integration verification and diagnostics.
- Agent I: Documentation and runtime operator notes.

Every agent must work on its assigned file set, assume other agents may be editing adjacent files, and avoid reverting unrelated changes.

---

## File Structure

Create:

- `engine/source/runtime/function/render/interface/rhi_ray_tracing.h`
  Shared RHI ray tracing capability structs, acceleration-structure handles, shader table descriptors, dispatch descriptors, and path tracing resource descriptors.

- `engine/source/runtime/function/render/interface/d3d12/d3d12_ray_tracing.h`
  D3D12-only helper class declarations for DXR support query, acceleration structures, state object, shader tables, descriptor binding helpers, and resource teardown.

- `engine/source/runtime/function/render/interface/d3d12/d3d12_ray_tracing.cpp`
  D3D12-only helper implementation. This keeps `d3d12_rhi.cpp` from absorbing all DXR state-object and AS code.

- `engine/source/runtime/function/render/passes/path_tracing_pass.h`

- `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`

- `engine/shader/hlsl/path_tracing_common.hlsli`

- `engine/shader/hlsl/path_tracing.lib.hlsl`

Modify:

- `engine/source/runtime/function/render/interface/rhi.h`
- `engine/source/runtime/function/render/interface/rhi_struct.h`
- `engine/source/runtime/function/render/render_type.h`
- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h`
- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
- `engine/source/runtime/function/render/interface/vulkan/vulkan_rhi.h`
- `engine/source/runtime/function/render/interface/vulkan/vulkan_rhi.cpp`
- `engine/source/runtime/function/render/render_gpu_resource.h`
- `engine/source/runtime/function/render/render_resource.h`
- `engine/source/runtime/function/render/render_resource.cpp`
- `engine/source/runtime/function/render/render_scene.h`
- `engine/source/runtime/function/render/render_scene.cpp`
- `engine/source/runtime/function/render/render_system.cpp`
- `engine/source/runtime/function/render/render_pipeline_base.h`
- `engine/source/runtime/function/render/render_pipeline.h`
- `engine/source/runtime/function/render/render_pipeline.cpp`
- `engine/source/runtime/function/render/render_pass.h`
- `engine/source/runtime/function/render/passes/main_camera_pass.h`
- `engine/source/runtime/function/render/passes/main_camera_pass.cpp`
- `engine/source/runtime/function/render/render_shader_bytecode.h`
- `cmake/ShaderCompile.cmake`
- `engine/shader/CMakeLists.txt`
- `engine/source/runtime/CMakeLists.txt` if new D3D12 source files are not picked up by the existing recursive glob in the local generator.

Do not modify:

- `engine/source/runtime/function/render/passes/ui_pass.cpp`
- `engine/source/runtime/function/render/passes/ui_pass.h`
- `engine/source/runtime/function/render/passes/combine_ui_pass.cpp`
- `engine/source/runtime/function/render/passes/combine_ui_pass.h`

Those files are listed here because preserving them is a requirement.

---

## Task 1: Define Shared RHI Ray Tracing Contracts

**Parallel owner:** Agent A
**Can start immediately:** Yes
**Depends on:** none
**Unblocks:** Agents B, D, E, G

**Files:**

- Create: `engine/source/runtime/function/render/interface/rhi_ray_tracing.h`
- Modify: `engine/source/runtime/function/render/interface/rhi.h`
- Modify: `engine/source/runtime/function/render/interface/rhi_struct.h`
- Modify: `engine/source/runtime/function/render/render_type.h`

- [ ] **Step 1: Add opaque RHI ray tracing resource classes**

Add these forward declarations in `rhi_struct.h` near the existing opaque RHI class declarations, or include them from the new `rhi_ray_tracing.h` if the file is introduced first:

```cpp
class RHIAccelerationStructure { };
class RHIShaderBindingTable { };
```

- [ ] **Step 2: Create the ray tracing contract header**

Create `rhi_ray_tracing.h` and define these exact concepts:

```cpp
#pragma once

#include "runtime/function/render/interface/rhi_struct.h"

#include <cstdint>
#include <vector>

namespace Piccolo
{
    enum class RHIRayTracingSupportLevel : uint8_t
    {
        Unsupported = 0,
        Supported
    };

    enum class RHIAccelerationStructureType : uint8_t
    {
        BottomLevel = 0,
        TopLevel
    };

    struct RHIRayTracingCapabilities
    {
        RHIRayTracingSupportLevel support_level {RHIRayTracingSupportLevel::Unsupported};
        uint32_t max_recursion_depth {0};
        uint32_t shader_group_handle_size {0};
        uint32_t shader_group_handle_alignment {0};
        uint32_t shader_binding_table_alignment {0};
        bool supports_inline_ray_tracing {false};
    };

    struct RHIAccelerationStructureGeometryDesc
    {
        RHIBuffer* vertex_position_buffer {nullptr};
        RHIDeviceSize vertex_position_offset {0};
        uint32_t vertex_count {0};
        uint32_t vertex_stride {0};
        RHIBuffer* index_buffer {nullptr};
        RHIDeviceSize index_offset {0};
        uint32_t index_count {0};
        RHIIndexType index_type {RHI_INDEX_TYPE_UINT16};
        bool opaque {true};
    };

    struct RHIAccelerationStructureInstanceDesc
    {
        RHIAccelerationStructure* bottom_level_as {nullptr};
        const float* row_major_3x4_transform {nullptr};
        uint32_t instance_id {0};
        uint32_t hit_group_index {0};
        uint8_t instance_mask {0xFF};
        bool force_opaque {true};
    };

    struct RHIAccelerationStructureBuildDesc
    {
        RHIAccelerationStructureType type {RHIAccelerationStructureType::BottomLevel};
        const RHIAccelerationStructureGeometryDesc* geometries {nullptr};
        uint32_t geometry_count {0};
        const RHIAccelerationStructureInstanceDesc* instances {nullptr};
        uint32_t instance_count {0};
        bool prefer_fast_trace {true};
        bool allow_update {false};
        bool perform_update {false};
        RHIAccelerationStructure* source {nullptr};
    };

    struct RHIRayTracingShaderLibrary
    {
        const unsigned char* bytecode {nullptr};
        size_t bytecode_size {0};
        const wchar_t* raygen_export {nullptr};
        const wchar_t* miss_export {nullptr};
        const wchar_t* closest_hit_export {nullptr};
        const wchar_t* hit_group_export {nullptr};
    };

    struct RHIRayTracingPipelineCreateInfo
    {
        RHIRayTracingShaderLibrary shader_library {};
        RHIPipelineLayout* layout {nullptr};
        uint32_t max_recursion_depth {1};
    };

    struct RHIShaderBindingTableCreateInfo
    {
        RHIPipeline* ray_tracing_pipeline {nullptr};
        const wchar_t* raygen_export {nullptr};
        const wchar_t* miss_export {nullptr};
        const wchar_t* hit_group_export {nullptr};
    };

    struct RHIRayTracingDispatchDesc
    {
        RHIPipeline* ray_tracing_pipeline {nullptr};
        RHIPipelineLayout* layout {nullptr};
        RHIShaderBindingTable* shader_binding_table {nullptr};
        uint32_t width {0};
        uint32_t height {0};
        uint32_t depth {1};
    };
}
```

- [ ] **Step 3: Add virtual methods to `RHI`**

Add these methods after the existing compute pipeline and dispatch APIs in `rhi.h`:

```cpp
virtual RHIRayTracingCapabilities getRayTracingCapabilities() const = 0;
virtual bool createAccelerationStructure(const RHIAccelerationStructureBuildDesc* build_desc,
                                         RHIAccelerationStructure*& acceleration_structure) = 0;
virtual bool buildAccelerationStructure(RHICommandBuffer* command_buffer,
                                        const RHIAccelerationStructureBuildDesc* build_desc,
                                        RHIAccelerationStructure* acceleration_structure) = 0;
virtual bool createRayTracingPipeline(const RHIRayTracingPipelineCreateInfo* create_info,
                                      RHIPipeline*& pipeline) = 0;
virtual bool createShaderBindingTable(const RHIShaderBindingTableCreateInfo* create_info,
                                      RHIShaderBindingTable*& shader_binding_table) = 0;
virtual void cmdTraceRays(RHICommandBuffer* command_buffer,
                          const RHIRayTracingDispatchDesc* dispatch_desc) = 0;
virtual void destroyAccelerationStructure(RHIAccelerationStructure*& acceleration_structure) = 0;
virtual void destroyShaderBindingTable(RHIShaderBindingTable*& shader_binding_table) = 0;
```

Add `#include "rhi_ray_tracing.h"` from `rhi.h` after `rhi_struct.h`.

- [ ] **Step 4: Verify existing ray tracing enum coverage**

Confirm `render_type.h` already contains:

- `RHI_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR`
- `RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE`
- `RHI_IMAGE_LAYOUT_GENERAL`
- `RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR`
- `RHI_SHADER_STAGE_RAYGEN_BIT_KHR`
- `RHI_SHADER_STAGE_MISS_BIT_KHR`
- `RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR`
- `RHI_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR`
- `RHI_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR`
- `RHI_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR`
- `RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR`
- `RHI_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR`
- `RHI_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR`

If any item is missing, add it with the Vulkan numeric value already used by adjacent RHI enums.

- [ ] **Step 5: Compile the interface after backend stubs exist**

This task cannot compile alone because `RHI` is pure virtual. Coordinate with Task 2 and Task 5 so D3D12 and Vulkan implement the new methods.

Run after Task 2 and Task 5:

```powershell
cmake --build build --config Debug --target PiccoloRuntime
```

Expected: `PiccoloRuntime` builds without pure-virtual or missing-type errors.

- [ ] **Step 6: Commit**

```powershell
git add engine/source/runtime/function/render/interface/rhi_ray_tracing.h `
        engine/source/runtime/function/render/interface/rhi.h `
        engine/source/runtime/function/render/interface/rhi_struct.h `
        engine/source/runtime/function/render/render_type.h
git commit -m "feat: add rhi ray tracing contracts"
```

---

## Task 2: Add Vulkan Interface Stubs Only

**Parallel owner:** Agent G
**Can start after:** Task 1 method signatures are agreed
**Depends on:** Task 1 header names and signatures
**Unblocks:** fallback verification

**Files:**

- Modify: `engine/source/runtime/function/render/interface/vulkan/vulkan_rhi.h`
- Modify: `engine/source/runtime/function/render/interface/vulkan/vulkan_rhi.cpp`

- [ ] **Step 1: Add Vulkan method declarations**

In `VulkanRHI`, add overrides for every method introduced by Task 1:

```cpp
RHIRayTracingCapabilities getRayTracingCapabilities() const override;
bool createAccelerationStructure(const RHIAccelerationStructureBuildDesc* build_desc,
                                 RHIAccelerationStructure*& acceleration_structure) override;
bool buildAccelerationStructure(RHICommandBuffer* command_buffer,
                                const RHIAccelerationStructureBuildDesc* build_desc,
                                RHIAccelerationStructure* acceleration_structure) override;
bool createRayTracingPipeline(const RHIRayTracingPipelineCreateInfo* create_info,
                              RHIPipeline*& pipeline) override;
bool createShaderBindingTable(const RHIShaderBindingTableCreateInfo* create_info,
                              RHIShaderBindingTable*& shader_binding_table) override;
void cmdTraceRays(RHICommandBuffer* command_buffer,
                  const RHIRayTracingDispatchDesc* dispatch_desc) override;
void destroyAccelerationStructure(RHIAccelerationStructure*& acceleration_structure) override;
void destroyShaderBindingTable(RHIShaderBindingTable*& shader_binding_table) override;
```

- [ ] **Step 2: Implement unsupported Vulkan behavior**

In `vulkan_rhi.cpp`, implement exact behavior:

- `getRayTracingCapabilities()` returns `Unsupported`, zeros, and false.
- Create/build/pipeline/SBT methods return `false` and leave output pointers `nullptr`.
- `cmdTraceRays()` does nothing.
- Destroy methods set the passed pointer to `nullptr`.

Do not enable Vulkan ray tracing extensions, do not add Vulkan RT function pointers, and do not add SPIR-V ray shader requirements.

- [ ] **Step 3: Verify Vulkan still boots on the raster path**

```powershell
cmake --build build --config Debug --target PiccoloEditor
powershell -ExecutionPolicy Bypass -File scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -RenderBackend Vulkan -ExpectedBackend Vulkan
```

Expected log checks:

- `Initialized RHI backend: Vulkan`
- `engine start`
- no fatal message about ray tracing setup

- [ ] **Step 4: Commit**

```powershell
git add engine/source/runtime/function/render/interface/vulkan/vulkan_rhi.h `
        engine/source/runtime/function/render/interface/vulkan/vulkan_rhi.cpp
git commit -m "feat: add vulkan ray tracing unsupported stubs"
```

---

## Task 3: Add D3D12 DXR Capability and Core Resource Support

**Parallel owner:** Agent B
**Can start after:** Task 1 method signatures are agreed
**Depends on:** Task 1
**Unblocks:** Tasks 4, 6, 7

**Files:**

- Create: `engine/source/runtime/function/render/interface/d3d12/d3d12_ray_tracing.h`
- Create: `engine/source/runtime/function/render/interface/d3d12/d3d12_ray_tracing.cpp`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
- Modify: `engine/source/runtime/CMakeLists.txt` only if local generation does not pick up the new files

- [ ] **Step 1: Add DXR COM members and support flags**

In `D3D12RHI`, add private members guarded by `_WIN32`:

```cpp
ComPtr<ID3D12Device5> m_d3d12_device5;
ComPtr<ID3D12GraphicsCommandList4> m_d3d12_command_list4;
RHIRayTracingCapabilities m_ray_tracing_capabilities {};
```

If the helper class owns these values instead, keep public behavior identical through `D3D12RHI::getRayTracingCapabilities()`.

- [ ] **Step 2: Query DXR support during device creation**

After `m_d3d12_device` is created:

- Query `ID3D12Device5`.
- Query `D3D12_FEATURE_D3D12_OPTIONS5`.
- Set `support_level = Supported` only when `RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED` and `ID3D12Device5` is available.
- Fill shader identifier sizes with D3D12 constants:
  - `D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES`
  - `D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT`
  - `D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT`
- Set `max_recursion_depth` to `1` for the first implementation.

- [ ] **Step 3: Query command list DXR interface**

Where `m_d3d12_command_list` is created/reset, query `ID3D12GraphicsCommandList4` into `m_d3d12_command_list4`. If unavailable, keep capability unsupported and raster rendering functional.

- [ ] **Step 4: Add D3D12 acceleration-structure wrapper**

Create a backend wrapper that derives from `RHIAccelerationStructure` and owns:

- `ComPtr<ID3D12Resource> result`
- `ComPtr<ID3D12Resource> scratch`
- `ComPtr<ID3D12Resource> instance_upload`
- `D3D12_GPU_VIRTUAL_ADDRESS gpu_address`
- `RHIAccelerationStructureType type`
- build sizes from `GetRaytracingAccelerationStructurePrebuildInfo`

- [ ] **Step 5: Add buffer usage and resource-state mapping**

In `d3d12_rhi.cpp`, update these helpers:

- `bufferResourceFlags`
- `toD3D12BufferState`
- descriptor update helpers for acceleration-structure SRVs

Required mapping:

- `RHI_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR` creates buffers with `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS`.
- AS build input buffers can transition to `D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE`.
- AS result buffers transition to `D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE`.
- SBT buffers transition to `D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE`.

- [ ] **Step 6: Implement D3D12 AS create/build methods**

Implement Task 1 methods with D3D12 behavior:

- BLAS uses `D3D12_RAYTRACING_GEOMETRY_DESC` with triangle geometry.
- Vertex format is `DXGI_FORMAT_R32G32B32_FLOAT`.
- Index format is `DXGI_FORMAT_R16_UINT`.
- TLAS uses `D3D12_RAYTRACING_INSTANCE_DESC`.
- Static mesh BLAS uses `D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE`.
- Add UAV barriers after AS builds.

- [ ] **Step 7: Implement ray tracing state object and shader table support**

Implement:

- `createRayTracingPipeline()`
- `createShaderBindingTable()`
- `cmdTraceRays()`

Minimum state object subobjects:

- DXIL library from `RHIRayTracingShaderLibrary`
- raygen export
- miss export
- closest-hit export
- hit group export
- shader config
- local root signature if needed
- global root signature from `RHIPipelineLayout`
- pipeline config with recursion depth 1

SBT layout:

- one raygen record
- one miss record
- one hit group record
- aligned to `D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT`
- table start addresses aligned to `D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT`

- [ ] **Step 8: Clean up resources**

Update `D3D12RHI::clear()` and explicit destroy methods so AS and SBT resources release before the D3D12 device is destroyed.

- [ ] **Step 9: Verify D3D12 still boots when path tracing is disabled**

```powershell
cmake --build build --config Debug --target PiccoloEditor
powershell -ExecutionPolicy Bypass -File scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
```

Expected:

- D3D12 backend initializes.
- No crash on GPUs without DXR support.
- Logs identify unsupported DXR as a fallback condition, not an engine failure.

- [ ] **Step 10: Commit**

```powershell
git add engine/source/runtime/function/render/interface/d3d12/d3d12_ray_tracing.h `
        engine/source/runtime/function/render/interface/d3d12/d3d12_ray_tracing.cpp `
        engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h `
        engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp `
        engine/source/runtime/CMakeLists.txt
git commit -m "feat: add d3d12 dxr core support"
```

---

## Task 4: Add DXR HLSL Library Build Support

**Parallel owner:** Agent C
**Can start immediately:** Yes
**Depends on:** none
**Unblocks:** Task 7

**Files:**

- Modify: `cmake/ShaderCompile.cmake`
- Modify: `engine/shader/CMakeLists.txt`
- Modify: `engine/source/runtime/function/render/render_shader_bytecode.h`
- Create: `engine/shader/hlsl/path_tracing_common.hlsli`
- Create: `engine/shader/hlsl/path_tracing.lib.hlsl`

- [ ] **Step 1: Extend HLSL shader discovery**

In `engine/shader/CMakeLists.txt`, add `*.lib.hlsl` to `HLSL_SHADER_FILES` or create a separate `HLSL_DXR_SHADER_FILES` glob. Keep Vulkan GLSL ray-stage files out of the first D3D12 implementation.

- [ ] **Step 2: Add DXR library profile selection**

In `cmake/ShaderCompile.cmake`, update HLSL profile logic:

- `.vert.hlsl` remains `vs_6_0`
- `.frag.hlsl` remains `ps_6_0`
- `.comp.hlsl` remains `cs_6_0`
- `.geom.hlsl` remains `gs_6_0`
- `.lib.hlsl` compiles as `lib_6_3` by default

Do not use `-E main` for `.lib.hlsl`.

- [ ] **Step 3: Generate DXIL and embedded headers for libraries**

For `.lib.hlsl`, output:

- `engine/shader/generated/dxil/path_tracing.lib.dxil`
- `engine/shader/generated/dxil_cpp/path_tracing_lib.h`
- generated global variable: `D3D12_PATH_TRACING_LIB`

The CMake path should still call `GenerateShaderCPPFile.cmake`.

- [ ] **Step 4: Add bytecode macros**

In `render_shader_bytecode.h`, add D3D12 include and macro handling for:

```cpp
PICCOLO_D3D12_PATH_TRACING_LIB
PICCOLO_RENDER_D3D12_SHADER_BYTECODE(PATH_TRACING_LIB)
```

For Vulkan, add `PICCOLO_VULKAN_PATH_TRACING_LIB` as empty bytecode only. Do not require a Vulkan ray shader to exist.

- [ ] **Step 5: Add HLSL exports**

Create `path_tracing.lib.hlsl` with these exported function names:

- `PathTracingRayGen`
- `PathTracingMiss`
- `PathTracingClosestHit`

Use `path_tracing_common.hlsli` for shared structs. Match CPU-side bindings from Task 7:

- TLAS SRV
- HDR scene output UAV
- accumulation UAV
- camera/per-frame constant buffer or storage buffer
- mesh/material buffers
- textures and samplers where available

- [ ] **Step 6: Keep first shader physically simple**

First shader behavior:

- raygen emits primary camera rays
- closest hit resolves triangle barycentrics, base color, normal, roughness, metallic, emissive
- miss returns sky/environment color
- one bounce diffuse/specular approximation is enough for the first usable mode
- output is HDR linear color before tone mapping

- [ ] **Step 7: Verify shader generation**

```powershell
cmake --build build --config Debug --target PiccoloShaderCompile
```

Expected generated files:

- `engine/shader/generated/dxil/path_tracing.lib.dxil`
- `engine/shader/generated/dxil_cpp/path_tracing_lib.h`

- [ ] **Step 8: Commit**

```powershell
git add cmake/ShaderCompile.cmake `
        engine/shader/CMakeLists.txt `
        engine/source/runtime/function/render/render_shader_bytecode.h `
        engine/shader/hlsl/path_tracing_common.hlsli `
        engine/shader/hlsl/path_tracing.lib.hlsl
git commit -m "feat: compile d3d12 path tracing shader library"
```

---

## Task 5: Export Scene Geometry and Material Data for Path Tracing

**Parallel owner:** Agent D
**Can start after:** Task 1 contracts are agreed
**Depends on:** Task 1 for AS handle fields
**Unblocks:** Task 7

**Files:**

- Modify: `engine/source/runtime/function/render/render_gpu_resource.h`
- Modify: `engine/source/runtime/function/render/render_resource.h`
- Modify: `engine/source/runtime/function/render/render_resource.cpp`
- Modify: `engine/source/runtime/function/render/render_scene.h`
- Modify: `engine/source/runtime/function/render/render_scene.cpp`
- Modify: `engine/source/runtime/function/render/render_system.cpp`

- [ ] **Step 1: Add path tracing mesh metadata**

Extend `RenderMeshGPUResource` with:

```cpp
RHIAccelerationStructure* bottom_level_as {nullptr};
bool path_tracing_geometry_dirty {true};
bool path_tracing_supported_static_geometry {true};
```

Do not store D3D12 objects here; keep this RHI-facing.

- [ ] **Step 2: Add path tracing scene instance data**

Add a render-facing struct in `render_scene.h`:

```cpp
struct RenderPathTracingInstance
{
    RenderEntity* entity {nullptr};
    RenderMeshGPUResource* mesh {nullptr};
    RenderPBRMaterialGPUResource* material {nullptr};
    uint32_t instance_id {0};
    uint32_t material_index {0};
    bool enabled {true};
};
```

Add `std::vector<RenderPathTracingInstance> m_path_tracing_instances;` to `RenderScene`.

- [ ] **Step 3: Build TLAS source from authoritative entities**

In `render_scene.cpp`, add a method that rebuilds `m_path_tracing_instances` from `m_render_entities`, not from `m_main_camera_visible_mesh_nodes`. This ensures off-camera geometry can still contribute through reflections and secondary rays.

Rules:

- include only entities with a valid mesh and material
- skip `m_enable_vertex_blending == true` in the first implementation
- skip transparent/blended materials in the first implementation
- keep `instance_id` equal to `RenderEntity::m_instance_id`

- [ ] **Step 4: Add scene dirty flags**

Track these flags:

```cpp
bool m_path_tracing_tlas_dirty {true};
bool m_path_tracing_accumulation_dirty {true};
```

Set both flags when:

- object is added
- object is deleted
- object transform changes
- material changes
- mesh upload changes

- [ ] **Step 5: Mark dirty from swap data**

In `RenderSystem::processSwapData()`:

- mark scene TLAS and accumulation dirty when `m_game_object_resource_desc` changes scene entities
- mark scene TLAS and accumulation dirty when `m_game_object_to_delete` deletes entities
- mark accumulation dirty when camera swap data changes view, FOV, or camera type

- [ ] **Step 6: Update mesh buffer usages for AS input**

In `RenderResource::updateVertexBuffer()` and `updateIndexBuffer()`, include AS/device-address usage bits when D3D12 path tracing support is compiled:

```cpp
RHI_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
RHI_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
```

Keep existing `VERTEX_BUFFER`, `INDEX_BUFFER`, and `TRANSFER_DST` bits.

- [ ] **Step 7: Add BLAS build helper**

Add `RenderResource::ensurePathTracingBLAS(std::shared_ptr<RHI> rhi, RenderMeshGPUResource& mesh)`.

Behavior:

- return immediately if `rhi->getRayTracingCapabilities().support_level == Unsupported`
- return immediately for meshes with `path_tracing_supported_static_geometry == false`
- build BLAS once for static geometry
- set `path_tracing_geometry_dirty = false` after a successful build
- destroy stale `bottom_level_as` before rebuilding

- [ ] **Step 8: Add TLAS input helper**

Add `RenderResource::collectPathTracingInstances(RenderScene& scene)` or equivalent so `PathTracingPass` can obtain:

- BLAS handles
- row-major 3x4 transforms
- instance IDs
- material indices
- mesh/material buffer references

- [ ] **Step 9: Verify raster rendering is unaffected**

```powershell
cmake --build build --config Debug --target PiccoloEditor
powershell -ExecutionPolicy Bypass -File scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
```

Expected: D3D12 boot smoke passes while path tracing selection remains off or unsupported.

- [ ] **Step 10: Commit**

```powershell
git add engine/source/runtime/function/render/render_gpu_resource.h `
        engine/source/runtime/function/render/render_resource.h `
        engine/source/runtime/function/render/render_resource.cpp `
        engine/source/runtime/function/render/render_scene.h `
        engine/source/runtime/function/render/render_scene.cpp `
        engine/source/runtime/function/render/render_system.cpp
git commit -m "feat: prepare scene geometry for path tracing"
```

---

## Task 6: Add Path Tracing Mode Selection Without Changing UI

**Parallel owner:** Agent E
**Can start after:** Task 1 contracts are agreed
**Depends on:** Task 1
**Unblocks:** Task 7

**Files:**

- Modify: `engine/source/runtime/function/render/render_pipeline_base.h`
- Modify: `engine/source/runtime/function/render/render_pipeline.h`
- Modify: `engine/source/runtime/function/render/render_pipeline.cpp`
- Modify: `engine/source/runtime/function/render/render_pass.h`
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.h`
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp`

- [ ] **Step 1: Add render mode state**

Add a render mode enum in a render-owned header:

```cpp
enum class RenderSceneRenderMode : uint8_t
{
    Raster = 0,
    PathTracing
};
```

Add selected and effective mode state to `RenderPipeline` or `MainCameraPass`:

```cpp
RenderSceneRenderMode m_requested_scene_render_mode {RenderSceneRenderMode::Raster};
RenderSceneRenderMode m_effective_scene_render_mode {RenderSceneRenderMode::Raster};
```

- [ ] **Step 2: Select path tracing only when supported**

At render-time:

- requested `PathTracing` + D3D12 + `getRayTracingCapabilities().Supported` => effective `PathTracing`
- requested `PathTracing` + unsupported backend/GPU => effective `Raster`
- requested `Raster` => effective `Raster`

No UI toggle is required for the first implementation. Use a config value or a hardcoded developer setting following the existing render settings pattern.

- [ ] **Step 3: Preserve UI and combine pass contracts**

Do not change:

- `UIPass::initializeUIRenderBackend()`
- `UIPass::draw()`
- `CombineUIPass::setupDescriptorSetLayout()`
- `CombineUIPass::updateAfterFramebufferRecreate()`
- `CombineUIPass::draw()`

Keep scene input image view as `_main_camera_pass_backup_buffer_odd` and UI input image view as `_main_camera_pass_backup_buffer_even`.

- [ ] **Step 4: Add a path tracing draw branch**

In `MainCameraPass`, add a draw branch that:

- begins the existing main render pass if the integration remains subpass-based, or records path tracing before the UI/combine render pass if Task 7 chooses an out-of-render-pass dispatch
- produces final scene color in `_main_camera_pass_backup_buffer_odd`
- skips gbuffer/deferred lighting/forward scene lighting when effective mode is `PathTracing`
- still executes UI and combine subpasses
- still draws editor axis/debug with the existing raster path if axis visibility is enabled

- [ ] **Step 5: Keep raster paths byte-for-byte close**

Do not refactor the raster draw sequence except for a clear branch point. Existing `draw()` and `drawForward()` behavior must remain the fallback path.

- [ ] **Step 6: Add resize hook**

Update `RenderPipeline::passUpdateAfterRecreateSwapchain()` so the future `PathTracingPass` receives the recreated scene output image view or image handle. Keep existing calls for tone mapping, color grading, FXAA, combine UI, pick, particle, and debug draw.

- [ ] **Step 7: Verify UI still appears on raster D3D12**

```powershell
cmake --build build --config Debug --target PiccoloEditor
powershell -ExecutionPolicy Bypass -File scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10
```

Expected:

- D3D12 editor is visible.
- UI overlay remains visible.
- No crash on resize/maximize.

- [ ] **Step 8: Commit**

```powershell
git add engine/source/runtime/function/render/render_pipeline_base.h `
        engine/source/runtime/function/render/render_pipeline.h `
        engine/source/runtime/function/render/render_pipeline.cpp `
        engine/source/runtime/function/render/render_pass.h `
        engine/source/runtime/function/render/passes/main_camera_pass.h `
        engine/source/runtime/function/render/passes/main_camera_pass.cpp
git commit -m "feat: select optional path tracing scene mode"
```

---

## Task 7: Implement `PathTracingPass`

**Parallel owner:** Agent F after Wave 1 merge
**Can start after:** Tasks 1, 3, 4, 5, and 6 compile together
**Depends on:** RHI RT contracts, D3D12 DXR core, HLSL library, scene AS data, render mode selection
**Unblocks:** final integration verification

**Files:**

- Create: `engine/source/runtime/function/render/passes/path_tracing_pass.h`
- Create: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`
- Modify: `engine/source/runtime/function/render/render_pipeline_base.h`
- Modify: `engine/source/runtime/function/render/render_pipeline.cpp`
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.h`
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp`
- Modify: `engine/source/runtime/function/render/render_shader_bytecode.h`

- [ ] **Step 1: Define pass init info**

Create:

```cpp
struct PathTracingPassInitInfo : RenderPassInitInfo
{
    RHIImage* scene_color_image {nullptr};
    RHIImageView* scene_color_image_view {nullptr};
    RHIFormat scene_color_format {RHI_FORMAT_R16G16B16A16_SFLOAT};
};
```

- [ ] **Step 2: Define pass owned resources**

`PathTracingPass` owns:

- ray tracing pipeline
- shader binding table
- TLAS
- accumulation image/buffer
- descriptor layout/set for TLAS, scene output UAV, accumulation UAV, per-frame data, mesh buffers, material data, textures
- sample index
- last camera state hash
- last scene state version

- [ ] **Step 3: Initialize only when supported**

`PathTracingPass::initialize()`:

- queries `m_rhi->getRayTracingCapabilities()`
- returns with `m_supported = false` when unsupported
- creates descriptor set layout
- creates ray tracing pipeline from `PICCOLO_RENDER_D3D12_SHADER_BYTECODE(PATH_TRACING_LIB)`
- creates SBT
- creates accumulation resource matching swapchain extent

- [ ] **Step 4: Build/update AS before dispatch**

Before tracing each frame:

- call `RenderResource::ensurePathTracingBLAS()` for each static path tracing mesh
- rebuild or refit TLAS when scene TLAS dirty flag is true
- skip dispatch if TLAS has zero instances and clear scene output to black/environment

- [ ] **Step 5: Reset accumulation correctly**

Reset `sample_index` to `0` when:

- camera view matrix changes
- camera FOV changes
- swapchain extent changes
- TLAS changes
- material/texture resources change
- path tracing mode is toggled on

Increment `sample_index` after a successful dispatch.

- [ ] **Step 6: Record trace commands**

`PathTracingPass::draw()`:

- transitions scene color image to UAV/general state
- binds ray tracing pipeline descriptors
- calls `cmdTraceRays()` with width/height/depth
- transitions scene color image to the layout expected by the next post/UI stage
- keeps all UI rendering out of the pass

- [ ] **Step 7: Integrate with `MainCameraPass`**

When effective mode is `PathTracing`:

- path trace scene color into `_main_camera_pass_backup_buffer_odd`
- execute tone mapping/color grading/FXAA if the path traced output is HDR and the existing chain is enabled for the selected route
- execute `_main_camera_subpass_ui`
- execute `_main_camera_subpass_combine_ui`

If the final design dispatches DXR outside the existing render pass, split the command recording so D3D12 dispatch occurs outside render-pass scope, then begin the existing render pass at the UI/combine section with scene input already readable.

- [ ] **Step 8: Handle resize**

On `passUpdateAfterRecreateSwapchain()`:

- release and recreate accumulation resource
- update output UAV descriptor
- update any image view references to `_main_camera_pass_backup_buffer_odd`
- reset sample index

- [ ] **Step 9: Verify path tracing on supported D3D12 hardware**

```powershell
cmake --build build --config Debug --target PiccoloEditor
powershell -ExecutionPolicy Bypass -File scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
powershell -ExecutionPolicy Bypass -File scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10
```

Manual checks on a DXR-capable GPU:

- path tracing mode logs as active
- editor scene renders with non-black scene color
- UI is visible and unchanged
- moving the camera resets accumulation
- standing still accumulates progressively
- resizing and maximizing the editor does not crash

- [ ] **Step 10: Commit**

```powershell
git add engine/source/runtime/function/render/passes/path_tracing_pass.h `
        engine/source/runtime/function/render/passes/path_tracing_pass.cpp `
        engine/source/runtime/function/render/render_pipeline_base.h `
        engine/source/runtime/function/render/render_pipeline.cpp `
        engine/source/runtime/function/render/passes/main_camera_pass.h `
        engine/source/runtime/function/render/passes/main_camera_pass.cpp `
        engine/source/runtime/function/render/render_shader_bytecode.h
git commit -m "feat: add d3d12 path tracing pass"
```

---

## Task 8: Add Resource State, Descriptor, and Resize Hardening

**Parallel owner:** Agent H
**Can start after:** Task 7 first compile
**Depends on:** Tasks 3 and 7
**Unblocks:** stable editor use

**Files:**

- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h`
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`
- Modify: `engine/source/runtime/function/render/render_pipeline.cpp`

- [ ] **Step 1: Audit command-list scope**

Ensure `cmdTraceRays()` is never called while `D3D12RHI` believes it is inside a graphics render pass that maps to active RTV/DSV state. If needed, place the DXR dispatch before `cmdBeginRenderPassPFN()` for UI/combine and keep explicit transitions around it.

- [ ] **Step 2: Add descriptor heap stability checks**

Confirm D3D12 descriptor heap binding survives:

- ray tracing descriptors
- ImGui descriptor heap switch in `UIPass::draw()`
- combine UI graphics descriptors after UI

If a descriptor heap switch invalidates cached root descriptor tables, mark command-buffer descriptor state dirty through the existing D3D12 helper pattern.

- [ ] **Step 3: Add UAV and AS barriers**

After AS build:

```cpp
D3D12_RESOURCE_BARRIER barrier {};
barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
barrier.UAV.pResource = acceleration_structure_result_resource;
```

After path tracing dispatch, add a UAV barrier for scene output/accumulation before post/UI reads.

- [ ] **Step 4: Validate image usage flags**

Ensure `_main_camera_pass_backup_buffer_odd` and any path tracing output image are created with usage that maps to UAV/storage:

```cpp
RHI_IMAGE_USAGE_STORAGE_BIT
```

Keep existing color/input/sampled usage bits needed by raster/post/combine.

- [ ] **Step 5: Verify fullscreen/maximize**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor
powershell -ExecutionPolicy Bypass -File scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10
```

Manual checks:

- maximize editor window
- restore editor window
- resize rapidly for 10 seconds
- switch path tracing off/on through the selected config path

Expected:

- no device removal
- no descriptor heap assertion
- no stale image view crash
- UI remains visible

- [ ] **Step 6: Commit**

```powershell
git add engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp `
        engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h `
        engine/source/runtime/function/render/passes/path_tracing_pass.cpp `
        engine/source/runtime/function/render/render_pipeline.cpp
git commit -m "fix: harden d3d12 path tracing resource states"
```

---

## Task 9: Verify Fallbacks and Performance Envelope

**Parallel owner:** Agent H or a dedicated verification agent
**Can start after:** Task 8
**Depends on:** integrated branch
**Unblocks:** completion

**Files:**

- Modify docs only if verification uncovers operator notes.
- Do not add test code.

- [ ] **Step 1: Build Debug and Release**

```powershell
cmake --build build --config Debug --target PiccoloEditor
cmake --build build --config Release --target PiccoloEditor
```

Expected: both configurations build.

- [ ] **Step 2: Run backend boot smokes**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
powershell -ExecutionPolicy Bypass -File scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -RenderBackend Vulkan -ExpectedBackend Vulkan
powershell -ExecutionPolicy Bypass -File scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -RenderBackend Auto -ExpectedBackend D3D12
```

Expected:

- D3D12 boots.
- Vulkan boots and reports path tracing unsupported.
- Auto keeps existing backend selection behavior.

- [ ] **Step 3: Run visible D3D12 smoke**

```powershell
powershell -ExecutionPolicy Bypass -File scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10
```

Expected: non-black capture and visible editor UI.

- [ ] **Step 4: Manual visual parity checks**

Compare raster D3D12 and path tracing D3D12 on the same scene:

- camera position unchanged
- UI unchanged
- axis/debug overlay unchanged
- no inverted image
- no gamma double-application
- scene color is HDR before tone mapping
- path traced static geometry appears in the same world positions as raster geometry

- [ ] **Step 5: Manual unsupported-GPU fallback**

On a GPU or adapter without DXR:

- request path tracing mode
- start D3D12
- confirm log says path tracing unsupported and raster path selected
- confirm no crash and no black scene

- [ ] **Step 6: Performance sanity**

On a DXR-capable Windows GPU:

- path tracing should not drop to 1 FPS from CPU-side per-frame full rebuilds in a static scene
- static BLAS should build once per mesh
- TLAS should refit/rebuild only when transforms or scene membership change
- accumulation should not recreate resources every frame

Capture PIX or D3D12 debug-layer evidence if performance is far below expected.

- [ ] **Step 7: Commit verification notes if docs changed**

```powershell
git add Docs
git commit -m "docs: record d3d12 path tracing verification"
```

Only run this commit if verification notes were added.

---

## Task 10: Document Limitations and Operator Controls

**Parallel owner:** Agent I
**Can start after:** Task 7 behavior is known
**Depends on:** implemented mode selection and verification results
**Unblocks:** handoff

**Files:**

- Create or modify: `Docs/d3d12-path-tracing.md`
- Modify: `ReleaseNotes.md` if release notes are being maintained for the branch

- [ ] **Step 1: Document capability behavior**

Record:

- D3D12 path tracing requires DXR support.
- Vulkan exposes interface-only unsupported responses.
- Unsupported devices automatically use raster rendering.
- UI remains raster ImGui and is composited after scene rendering.

- [ ] **Step 2: Document first-version rendering scope**

Record:

- static opaque meshes supported
- skinned/animated meshes excluded from path traced TLAS in the first version
- transparent materials excluded or raster-only
- one progressive sample per frame
- accumulation reset conditions

- [ ] **Step 3: Document runtime selection**

Document the selected control path:

- config key name and accepted values, or
- developer runtime flag if no user-facing config is added

Use exact names from Task 6 implementation.

- [ ] **Step 4: Commit**

```powershell
git add Docs/d3d12-path-tracing.md ReleaseNotes.md
git commit -m "docs: describe d3d12 path tracing mode"
```

---

## Integration Order

1. Merge Task 1 first or keep it in a shared integration branch.
2. Merge Tasks 2, 3, 4, 5, and 6 after they compile against Task 1.
3. Resolve any interface naming conflicts once at the integration branch, not separately in every agent branch.
4. Implement Task 7 after Wave 1 is merged.
5. Run Task 8 hardening before judging visual correctness.
6. Run Task 9 verification.
7. Finish with Task 10 documentation.

Recommended branch/commit cadence:

- one commit per task
- no generated shader binary/header commits unless this repo already tracks generated shader outputs for the changed shader
- keep generated build output out of git

---

## Acceptance Criteria

The work is complete when all of these are true:

- D3D12 reports path tracing support only on DXR-capable hardware.
- Requesting path tracing on unsupported D3D12 hardware falls back to raster rendering without crashing.
- Vulkan builds and runs with path tracing unsupported.
- Path tracing renders static opaque scene geometry through D3D12.
- UI rendering and `CombineUIPass` behavior remain unchanged.
- Resize, maximize, restore, and swapchain recreation do not crash.
- Accumulation resets on camera/scene/resize changes and accumulates when the camera is still.
- Existing D3D12/Vulkan boot smoke scripts pass.
- Existing D3D12 visible smoke script passes.
- No new test code was added.

---

## Known Risks

- D3D12 DXR dispatch generally must happen outside the current render-pass abstraction. If the first integration keeps it inside `MainCameraPass`, explicitly verify the backend command-list state allows it.
- Current D3D12 descriptor cache was built for graphics/compute descriptor sets. Ray tracing descriptors and ImGui descriptor heap switches can invalidate cached tables.
- Existing mesh vertex/index buffers are created without AS input/device-address usage bits. Task 5 must update creation flags before BLAS builds can be correct.
- Skinned meshes are shader-deformed today. Path tracing bind-pose BLAS would be visually wrong, so the first implementation must skip them or keep them raster-only.
- TLAS must be based on `RenderScene::m_render_entities`, not only camera-visible draw lists, because reflections and indirect rays can hit off-camera geometry.
- The current HLSL build flow uses `-E main`; DXR libraries require no entry point and a `lib_6_x` profile.
- The path traced output must match the existing HDR/postprocess expectations. If it writes LDR after tone mapping, the current post chain will double-process color.

---

## Subagent Prompts

Use these prompts when dispatching agents.

### Agent A Prompt

Implement Task 1 from `Docs/superpowers/plans/2026-06-11-d3d12-path-tracing-renderer.md`. Own only RHI shared contract files. Do not implement D3D12/Vulkan internals beyond signatures needed for compilation. Return changed files, exact signatures added, and any compile blockers caused by backend pure virtual methods.

### Agent B Prompt

Implement Task 3 from `Docs/superpowers/plans/2026-06-11-d3d12-path-tracing-renderer.md`. Own D3D12 DXR backend files. You are not alone in the codebase; do not revert edits from other agents. Keep DXR helper code in new `d3d12_ray_tracing.*` files where possible and minimize growth in `d3d12_rhi.cpp`. Return changed files, DXR support query behavior, AS/SBT/state-object implementation notes, and verification output.

### Agent C Prompt

Implement Task 4 from `Docs/superpowers/plans/2026-06-11-d3d12-path-tracing-renderer.md`. Own shader build and HLSL files. Do not implement runtime rendering. Ensure `.lib.hlsl` compiles without `-E main` and generated bytecode is selectable as `PATH_TRACING_LIB`. Return changed files, generated header names, and shader compile command output.

### Agent D Prompt

Implement Task 5 from `Docs/superpowers/plans/2026-06-11-d3d12-path-tracing-renderer.md`. Own scene/resource data flow. You are not alone in the codebase; do not revert edits from other agents. Build path tracing instance data from authoritative render entities, skip skinned/transparent geometry in first version, and add dirty flags for TLAS/accumulation. Return changed files and how BLAS/TLAS dirtiness is triggered.

### Agent E Prompt

Implement Task 6 from `Docs/superpowers/plans/2026-06-11-d3d12-path-tracing-renderer.md`. Own render pipeline selection and main camera branch points. Do not edit UI pass or combine UI pass. Preserve raster fallback behavior. Return changed files, effective mode selection rules, and D3D12 visible smoke output.

### Agent F Prompt

Implement Task 7 from `Docs/superpowers/plans/2026-06-11-d3d12-path-tracing-renderer.md` after Wave 1 is merged. Own `PathTracingPass` and its integration. You are not alone in the codebase; accommodate existing edits. Keep UI raster/combine unchanged, produce scene color into the existing scene input, and reset accumulation on camera/scene/resize changes. Return changed files, manual DXR hardware results, and unsupported fallback behavior.

### Agent H Prompt

Implement Tasks 8 and 9 from `Docs/superpowers/plans/2026-06-11-d3d12-path-tracing-renderer.md`. Own hardening and verification. Do not add test code. Focus on D3D12 resource states, descriptor heap invalidation, resize/fullscreen behavior, and existing smoke scripts. Return commands run, logs/capture paths, and remaining risks.

### Agent I Prompt

Implement Task 10 from `Docs/superpowers/plans/2026-06-11-d3d12-path-tracing-renderer.md`. Own documentation only. Record exact runtime controls and limitations from the implementation branch. Return docs changed and any discrepancies between plan and implemented behavior.
