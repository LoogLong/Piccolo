# D3D12 Path Tracing Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the current first-boot D3D12 DXR path tracing integration into a usable renderer whose static opaque scene output, post processing, UI composition, resize behavior, and runtime fallback behavior are reliable enough to replace the placeholder path tracing result.

**Architecture:** The existing implementation already provides the RHI ray tracing contracts, D3D12 DXR backend, Vulkan unsupported stubs, `PathTracingPass`, a path tracing composite render pass, `RenderSceneMode`, and a minimal HLSL ray tracing library. The remaining work keeps that architecture: D3D12 path tracing is a scene-color producer that writes HDR color into `_main_camera_pass_backup_buffer_odd`, then reuses tone mapping, color grading, FXAA, UI clear, editor axis/debug overlay, ImGui, and combine UI. Vulkan remains interface-only unsupported.

**Tech Stack:** C++17, Piccolo RHI, D3D12/DXR, HLSL DXIL libraries (`lib_6_3`), existing render passes, existing PowerShell smoke scripts, optional PIX/D3D12 debug-layer captures for manual diagnosis. Do not add test code unless explicitly requested.

---

## Current Implemented Baseline

Already implemented on `main`:

- `engine/source/runtime/function/render/interface/rhi_ray_tracing.h` defines ray tracing capabilities, acceleration-structure descriptors, ray tracing pipeline descriptors, shader binding table descriptors, and dispatch descriptors.
- `RHI` exposes ray tracing capabilities, AS creation/build/destruction, RT pipeline creation, SBT creation/destruction, and `cmdTraceRays()`.
- `D3D12RHI` implements DXR support checks, BLAS/TLAS allocation/build, state object creation, SBT creation, acceleration-structure descriptor writes, ray tracing descriptor-table replay, and `DispatchRays()` outside render-pass scope.
- `VulkanRHI` implements unsupported/no-op ray tracing methods.
- `cmake/ShaderCompile.cmake` and `engine/shader/CMakeLists.txt` compile `*.lib.hlsl` through DXC without `-E main`.
- `engine/shader/hlsl/path_tracing.lib.hlsl` exists but currently renders only a simple gradient, miss color, and barycentric closest-hit color; it does not yet reconstruct hit normals, UVs, material textures, or PBR lighting inputs.
- `PathTracingPass` owns the current TLAS, RT pipeline, SBT, frame-data uniform buffer, accumulation UAV, scene output UAV descriptor, and dispatch path.
- `RenderScene` exports path tracing instances from `m_render_entities`, skipping skinned/vertex-blended and transparent/blended instances.
- `RenderResource` builds BLAS for static opaque meshes and collects TLAS instances.
- `MainCameraPass` owns a path tracing composite render pass and swapchain framebuffers, then runs tone mapping, color grading, optional FXAA, UI clear, axis/debug draw, ImGui, and combine UI.
- `RenderSceneMode=Raster|PathTracing` is parsed by `ConfigManager` and `RenderPipeline`; default config value is `Raster`.

Verified before this plan was written: Debug `PiccoloEditor` build, Release `PiccoloEditor` build, D3D12 boot smoke, and a temporary `RenderSceneMode=PathTracing` D3D12 boot smoke exited with code 0. The PathTracing boot log reached `RenderScene::rebuildPathTracingInstances`.

Known current gaps:

- Path tracing visual output is placeholder shading, not Vulkan/raster-equivalent scene rendering.
- Path tracing instance export logs skipped skinned/transparent counts every frame in PathTracing mode.
- The shader does not consume material factors, vertex normals, tangents, UVs, material textures, lights, shadows, IBL, or texture data.
- Static BLAS caching exists, but TLAS rebuild/refit behavior and repeated scene export logging need measurement and tightening.
- Resize/maximize/fullscreen stability was not revalidated after the first PathTracing merge.
- There is no automated visible check specifically proving PathTracing scene pixels are correct.

---

## Parallel Agent Map

- **Agent A: DXR Backend Hardening** owns D3D12 RHI resource states, descriptor heap replay, AS/SBT lifetime, debug-layer warnings, and resize/fullscreen crash risks.
- **Agent B: Scene Data Export** owns `RenderScene`, `RenderResource`, mesh/material GPU data structures, TLAS instance stability, BLAS dirtiness, and log throttling.
- **Agent C: Shader And Material Shading** owns HLSL path tracing shader inputs, material/light structs, closest-hit shading, miss/environment behavior, and shader compile wiring.
- **Agent D: Render Flow And Visual Parity** owns `PathTracingPass`, `MainCameraPass`, post/UI ordering, fallback behavior, manual visual comparison, and smoke-script operation.
- **Agent E: Verification Notes** owns final verification notes in this plan. Do not recreate deleted historical docs.

Agents may work in parallel only when their files do not overlap. Serialize any tasks that both touch `PathTracingPass` or the HLSL data contract.

---

## File Structure

Primary files for remaining implementation:

- `engine/source/runtime/function/render/passes/path_tracing_pass.h`
- `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`
- `engine/source/runtime/function/render/passes/main_camera_pass.h`
- `engine/source/runtime/function/render/passes/main_camera_pass.cpp`
- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h`
- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
- `engine/source/runtime/function/render/interface/rhi_ray_tracing.h`
- `engine/source/runtime/function/render/interface/rhi_struct.h`
- `engine/source/runtime/function/render/render_gpu_resource.h`
- `engine/source/runtime/function/render/render_entity.h`
- `engine/source/runtime/function/render/render_mesh.h`
- `engine/source/runtime/function/render/render_resource.h`
- `engine/source/runtime/function/render/render_resource.cpp`
- `engine/source/runtime/function/render/render_scene.h`
- `engine/source/runtime/function/render/render_scene.cpp`
- `engine/source/runtime/function/render/render_shader_bytecode.h`
- `engine/shader/hlsl/path_tracing_common.hlsli`
- `engine/shader/hlsl/path_tracing.lib.hlsl`

Files that should stay unchanged unless this plan is explicitly amended:

- `engine/source/runtime/function/render/passes/ui_pass.h`
- `engine/source/runtime/function/render/passes/ui_pass.cpp`
- `engine/source/runtime/function/render/passes/combine_ui_pass.h`
- `engine/source/runtime/function/render/passes/combine_ui_pass.cpp`
- Vulkan ray tracing behavior remains unsupported/no-op.

Do not add test code for this plan unless the user explicitly asks. Verification uses existing build and smoke scripts plus manual visual checks.

---

## Task Summary

1. Quiet stable path tracing scene export logs.
2. Add shader-visible path tracing scene data buffers.
3. Replace placeholder closest-hit shading with geometry and material shading.
4. Add raster-compatible material texture and direct-lighting inputs.
5. Harden resource states, descriptors, and resize behavior.
6. Verify runtime mode selection and backend fallbacks.
7. Perform manual visual parity pass.
8. Run performance sanity checks and stabilize static-scene caching.
9. Record final verification notes.

---

## Task 1: Stabilize Path Tracing Mode Signals And Logging

**Parallel owner:** Agent B
**Can start immediately:** Yes
**Depends on:** current merged baseline
**Unblocks:** performance diagnosis and readable smoke logs

**Files:**

- Modify: `engine/source/runtime/function/render/render_scene.h`
- Modify: `engine/source/runtime/function/render/render_scene.cpp`

- [ ] **Step 1: Add stable skip-count state to `RenderScene`**

Add these private members near `m_path_tracing_entity_signatures`:

```cpp
uint32_t m_last_path_tracing_skipped_skinned {UINT32_MAX};
uint32_t m_last_path_tracing_skipped_transparent {UINT32_MAX};
uint32_t m_last_path_tracing_skipped_missing {UINT32_MAX};
```

Use `UINT32_MAX` so the first export logs once.

- [ ] **Step 2: Log skipped instance counts only when counts change**

In `RenderScene::rebuildPathTracingInstances()`, replace the unconditional skipped-instance log block with:

```cpp
const bool skipped_counts_changed =
    skipped_skinned != m_last_path_tracing_skipped_skinned ||
    skipped_transparent != m_last_path_tracing_skipped_transparent ||
    skipped_missing != m_last_path_tracing_skipped_missing;

if (log_skipped_instances && skipped_counts_changed)
{
    if (skipped_skinned > 0 || skipped_transparent > 0)
    {
        LOG_INFO("Path tracing scene export skipped {} skinned/vertex-blended and {} transparent/blended instances",
                 skipped_skinned,
                 skipped_transparent);
    }
    if (skipped_missing > 0)
    {
        LOG_WARN("Path tracing scene export skipped {} instances with missing mesh or material GPU resources",
                 skipped_missing);
    }
}

m_last_path_tracing_skipped_skinned = skipped_skinned;
m_last_path_tracing_skipped_transparent = skipped_transparent;
m_last_path_tracing_skipped_missing = skipped_missing;
```

- [ ] **Step 3: Reset skip-count state when scene is cleared**

In `RenderScene::clear()` and `RenderScene::clearForLevelReloading()`, set all three last skip-count members back to `UINT32_MAX` after clearing instance vectors.

- [ ] **Step 4: Verify collection is reachable only from PathTracing dispatch**

Run:

```powershell
rg -n "collectPathTracingInstances|buildTopLevelAS|dispatch\(\)" engine/source/runtime/function/render
```

Expected: `collectPathTracingInstances()` is only called from `PathTracingPass::buildTopLevelAS()`.

- [ ] **Step 5: Build and PathTracing boot smoke**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /v:minimal /clp:ErrorsOnly
```

Temporarily set `build/engine/source/editor/Debug/PiccoloEditor.ini` to `RenderSceneMode=PathTracing`, then run:

```powershell
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
```

Expected: build succeeds, smoke succeeds, and `build/test_d3d12_boot.log` contains at most one unchanged skipped-instance report for the stable scene.

- [ ] **Step 6: Commit**

```powershell
git add engine/source/runtime/function/render/render_scene.h `
        engine/source/runtime/function/render/render_scene.cpp
git commit -m "fix: quiet stable path tracing scene export logs"
```

---

## Task 2: Add Shader-Visible Path Tracing Scene Data Buffers

**Parallel owner:** Agent B
**Can start after:** Task 1, or in parallel if Agent B owns both
**Depends on:** current merged baseline
**Unblocks:** real closest-hit shading, material parity work, and stable `InstanceIndex()`-based shader lookup

**Files:**

- Modify: `engine/source/runtime/function/render/render_gpu_resource.h`
- Modify: `engine/source/runtime/function/render/render_entity.h`
- Modify: `engine/source/runtime/function/render/render_mesh.h`
- Modify: `engine/source/runtime/function/render/render_resource.h`
- Modify: `engine/source/runtime/function/render/render_resource.cpp`
- Modify: `engine/source/runtime/function/render/render_scene.h`
- Modify: `engine/source/runtime/function/render/render_scene.cpp`
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.h`
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`
- Modify: `engine/shader/hlsl/path_tracing_common.hlsli`

- [ ] **Step 1: Add CPU mesh copies needed by closest-hit shading**

In `render_gpu_resource.h`, add the missing includes:

```cpp
#include "runtime/core/math/vector2.h"
#include "runtime/core/math/vector3.h"

#include <vector>
```

Then add these fields to `RenderMeshGPUResource` after `path_tracing_static_opaque_supported`:

```cpp
std::vector<Vector3>  path_tracing_positions;
std::vector<Vector3>  path_tracing_normals;
std::vector<Vector3>  path_tracing_tangents;
std::vector<Vector2>  path_tracing_texcoords;
std::vector<uint32_t> path_tracing_indices;
bool                  path_tracing_geometry_dirty {true};
```

These CPU copies are filled only from the existing static mesh upload path and are not used by raster rendering.

- [ ] **Step 2: Populate CPU mesh copies during static mesh upload**

In both `RenderResource::updateVertexBuffer(...)` overloads, fill the path tracing vectors while the existing staging arrays are being filled:

```cpp
now_mesh.path_tracing_positions.resize(vertex_count);
now_mesh.path_tracing_normals.resize(vertex_count);
now_mesh.path_tracing_tangents.resize(vertex_count);
now_mesh.path_tracing_texcoords.resize(vertex_count);

for (uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index)
{
    now_mesh.path_tracing_positions[vertex_index] = mesh_vertex_positions[vertex_index].position;
    now_mesh.path_tracing_normals[vertex_index]   = mesh_vertex_blending_varyings[vertex_index].normal;
    now_mesh.path_tracing_tangents[vertex_index]  = mesh_vertex_blending_varyings[vertex_index].tangent;
    now_mesh.path_tracing_texcoords[vertex_index] = mesh_vertex_varyings[vertex_index].texcoord;
}

now_mesh.path_tracing_indices.resize(index_count);
for (uint32_t index_index = 0; index_index < index_count; ++index_index)
{
    now_mesh.path_tracing_indices[index_index] = static_cast<uint32_t>(index_buffer_data[index_index]);
}
now_mesh.path_tracing_geometry_dirty = true;
```

In `RenderResource::updateIndexBuffer(...)`, refresh `path_tracing_indices` with the same `uint32_t` conversion and set `path_tracing_geometry_dirty = true`.

- [ ] **Step 3: Define the shader-visible path tracing data contract**

Add these structures in `render_resource.h` near `RenderPathTracingCollectedInstance`:

```cpp
struct RenderPathTracingVertexGPUData
{
    Vector4 position {0.0f, 0.0f, 0.0f, 1.0f};
    Vector4 normal {0.0f, 1.0f, 0.0f, 0.0f};
    Vector4 tangent {1.0f, 0.0f, 0.0f, 0.0f};
    Vector4 texcoord {0.0f, 0.0f, 0.0f, 0.0f};
};

struct RenderPathTracingMaterialGPUData
{
    Vector4 base_color_factor {1.0f, 1.0f, 1.0f, 1.0f};
    Vector4 emissive_factor {0.0f, 0.0f, 0.0f, 0.0f};
    Vector4 metallic_roughness_normal_occlusion {1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t base_color_texture_index {UINT32_MAX};
    uint32_t metallic_roughness_texture_index {UINT32_MAX};
    uint32_t normal_texture_index {UINT32_MAX};
    uint32_t emissive_texture_index {UINT32_MAX};
    uint32_t flags {0};
    uint32_t _padding[3] {0, 0, 0};
};

struct RenderPathTracingGeometryGPUData
{
    uint32_t vertex_offset {0};
    uint32_t index_offset {0};
    uint32_t index_count {0};
    uint32_t _padding {0};
};

struct RenderPathTracingInstanceGPUData
{
    uint32_t geometry_index {0};
    uint32_t material_index {0};
    uint32_t entity_instance_id {0};
    uint32_t flags {0};
};
```

Use these flag bits in C++ and HLSL:

```cpp
static constexpr uint32_t kPathTracingMaterialFlagDoubleSided = 1u << 0;
```

- [ ] **Step 4: Track shader instance index separately from entity instance id**

Forward declare `RenderEntity` near the existing `class RenderScene;` declaration in `render_resource.h`, then extend `RenderPathTracingCollectedInstance` with the source entity pointer and a contiguous shader index:

```cpp
class RenderEntity;
```

```cpp
RenderEntity* entity {nullptr};
uint32_t shader_instance_index {0};
```

In `RenderResource::collectPathTracingInstances(RenderScene& scene)`, copy the pointer from the already-filtered scene instance:

```cpp
collected_instance.entity = source_instance.entity;
```

Keep `instance_id` as the engine entity id used by `InstanceID()`. In `PathTracingPass::buildTopLevelAS()`, after filtering out instances with null BLAS, assign `shader_instance_index` from the TLAS instance order:

```cpp
for (uint32_t instance_index = 0; instance_index < collected_instances.size(); ++instance_index)
{
    collected_instances[instance_index].shader_instance_index = instance_index;
}
```

Do not use `entity.m_instance_id` as an array index in HLSL. Closest-hit shader code must use `InstanceIndex()` for `g_instances[]` and may use `InstanceID()` only as the original entity id.

- [ ] **Step 5: Extend path tracing scene signatures for material changes**

In `RenderScene::PathTracingEntitySignature`, add the material fields currently missing from the dirty check:

```cpp
bool    double_sided {false};
float   metallic_factor {1.0f};
float   roughness_factor {1.0f};
float   normal_scale {1.0f};
float   occlusion_strength {1.0f};
Vector3 emissive_factor {0.0f, 0.0f, 0.0f};
```

Fill them from `RenderEntity` in `RenderScene::rebuildPathTracingInstances()`:

```cpp
signature.double_sided       = entity.m_double_sided;
signature.metallic_factor    = entity.m_metallic_factor;
signature.roughness_factor   = entity.m_roughness_factor;
signature.normal_scale       = entity.m_normal_scale;
signature.occlusion_strength = entity.m_occlusion_strength;
signature.emissive_factor    = entity.m_emissive_factor;
```

Include all six fields in `PathTracingEntitySignature::operator==()`. This makes material edits reset the TLAS/material buffer state and accumulation instead of blending stale samples with new material values.

- [ ] **Step 6: Add buffers and data vectors to `RenderResource`**

Add members to `RenderResource`:

```cpp
std::vector<RenderPathTracingVertexGPUData> m_path_tracing_vertex_data;
std::vector<uint32_t> m_path_tracing_index_data;
std::vector<RenderPathTracingMaterialGPUData> m_path_tracing_material_data;
std::vector<RenderPathTracingGeometryGPUData> m_path_tracing_geometry_data;
std::vector<RenderPathTracingInstanceGPUData> m_path_tracing_instance_data;
RHIBuffer* m_path_tracing_vertex_buffer {nullptr};
RHIDeviceMemory* m_path_tracing_vertex_buffer_memory {nullptr};
RHIBuffer* m_path_tracing_index_buffer {nullptr};
RHIDeviceMemory* m_path_tracing_index_buffer_memory {nullptr};
RHIBuffer* m_path_tracing_material_buffer {nullptr};
RHIDeviceMemory* m_path_tracing_material_buffer_memory {nullptr};
RHIBuffer* m_path_tracing_geometry_buffer {nullptr};
RHIDeviceMemory* m_path_tracing_geometry_buffer_memory {nullptr};
RHIBuffer* m_path_tracing_instance_buffer {nullptr};
RHIDeviceMemory* m_path_tracing_instance_buffer_memory {nullptr};
size_t m_path_tracing_vertex_buffer_capacity {0};
size_t m_path_tracing_index_buffer_capacity {0};
size_t m_path_tracing_material_buffer_capacity {0};
size_t m_path_tracing_geometry_buffer_capacity {0};
size_t m_path_tracing_instance_buffer_capacity {0};
```

- [ ] **Step 7: Build compact path tracing arrays from filtered TLAS instances**

Add this method to `RenderResource`:

```cpp
bool updatePathTracingSceneBuffers(std::shared_ptr<RHI> rhi,
                                   const std::vector<RenderPathTracingCollectedInstance>& collected_instances);
```

At the top of the method, clear all five vectors. Use one compact geometry record per unique `RenderMeshGPUResource*` in `collected_instances`, append that mesh's CPU path tracing vertices and indices into the global arrays, and use one material record per filtered TLAS instance. Per-instance material records intentionally avoid incorrect sharing when two entities use the same material asset but different `RenderEntity` factors.

Use this structure inside the method:

```cpp
std::map<RenderMeshGPUResource*, uint32_t> geometry_indices;
```

For each source instance, create `material_data` from the copied `RenderEntity*`, append it, and use its vector index as the shader-visible material index:

```cpp
RenderPathTracingMaterialGPUData material_data {};
material_data.base_color_factor = source_instance.entity->m_base_color_factor;
material_data.emissive_factor = Vector4(source_instance.entity->m_emissive_factor, 0.0f);
material_data.metallic_roughness_normal_occlusion = Vector4(source_instance.entity->m_metallic_factor,
                                                            source_instance.entity->m_roughness_factor,
                                                            source_instance.entity->m_normal_scale,
                                                            source_instance.entity->m_occlusion_strength);
material_data.flags = source_instance.entity->m_double_sided ? kPathTracingMaterialFlagDoubleSided : 0u;
const uint32_t shader_material_index = static_cast<uint32_t>(m_path_tracing_material_data.size());
m_path_tracing_material_data.push_back(material_data);
```

Use `shader_material_index` everywhere shader-visible data needs to index `g_materials[]`. Do not reuse `source_instance.material_index` for shader indexing because it was assigned before null-BLAS filtering and can be sparse.

Use the compact material index for every shader-visible instance record:

```cpp
RenderPathTracingInstanceGPUData instance_data {};
instance_data.geometry_index = geometry_index;
instance_data.material_index = shader_material_index;
instance_data.entity_instance_id = source_instance.instance_id;
instance_data.flags = 0;
```

Material texture indices are filled by Task 4. Until Task 4 lands, leave them as `UINT32_MAX` and use factor-only shading.

- [ ] **Step 8: Upload path tracing arrays to storage buffers**

Inside `updatePathTracingSceneBuffers(...)`, allocate or reallocate each storage buffer when its required byte size exceeds the current capacity. Use host-visible coherent memory to avoid adding a new staging/upload path in this step:

```cpp
const RHIBufferUsageFlags usage = RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT;
const RHIMemoryPropertyFlags memory_properties = RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT;
```

For each non-empty vector, map the matching memory object, copy `vector.data()` for `vector.size() * sizeof(T)`, unmap, and return `false` if any required map call fails. Destroy old buffers with `destroyBuffer()` and `freeMemory()` before replacing them.

- [ ] **Step 9: Extend `PathTracingPass` descriptor layout and writes**

Expand descriptor bindings from 4 to 9:

- binding 0: TLAS SRV
- binding 1: scene output UAV
- binding 2: frame uniform buffer
- binding 3: accumulation UAV
- binding 4: path tracing vertex storage buffer
- binding 5: path tracing index storage buffer
- binding 6: path tracing material storage buffer
- binding 7: path tracing geometry storage buffer
- binding 8: path tracing instance storage buffer

Use `RHI_SHADER_STAGE_RAYGEN_BIT_KHR | RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR` for binding 2. Use `RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR` for bindings 4 through 8. In `PathTracingPass::buildTopLevelAS()`, call:

```cpp
if (!m_render_resource_impl->updatePathTracingSceneBuffers(m_rhi, collected_instances))
{
    return false;
}
```

after null-BLAS filtering and shader-index assignment, before building or reusing the TLAS. Mark `m_descriptor_set_dirty = true` when any path tracing scene buffer is recreated.

- [ ] **Step 10: Mirror structs in HLSL**

In `path_tracing_common.hlsli`, add matching structs with the same field order and 16-byte alignment:

```hlsl
static const uint PICCOLO_PATH_TRACING_INVALID_INDEX = 0xffffffffu;
static const uint PICCOLO_PATH_TRACING_MATERIAL_FLAG_DOUBLE_SIDED = 1u;

struct PathTracingVertexData
{
    float4 position;
    float4 normal;
    float4 tangent;
    float4 texcoord;
};

struct PathTracingMaterialData
{
    float4 base_color_factor;
    float4 emissive_factor;
    float4 metallic_roughness_normal_occlusion;
    uint base_color_texture_index;
    uint metallic_roughness_texture_index;
    uint normal_texture_index;
    uint emissive_texture_index;
    uint flags;
    uint3 _padding;
};

struct PathTracingGeometryData
{
    uint vertex_offset;
    uint index_offset;
    uint index_count;
    uint _padding;
};

struct PathTracingInstanceData
{
    uint geometry_index;
    uint material_index;
    uint entity_instance_id;
    uint flags;
};
```

- [ ] **Step 11: Build and commit**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /v:minimal /clp:ErrorsOnly
```

Expected: C++ and HLSL generated headers build successfully.

Commit:

```powershell
git add engine/source/runtime/function/render/render_gpu_resource.h `
        engine/source/runtime/function/render/render_entity.h `
        engine/source/runtime/function/render/render_mesh.h `
        engine/source/runtime/function/render/render_resource.h `
        engine/source/runtime/function/render/render_resource.cpp `
        engine/source/runtime/function/render/render_scene.h `
        engine/source/runtime/function/render/render_scene.cpp `
        engine/source/runtime/function/render/passes/path_tracing_pass.h `
        engine/source/runtime/function/render/passes/path_tracing_pass.cpp `
        engine/shader/hlsl/path_tracing_common.hlsli
git commit -m "feat: export path tracing scene data"
```

---

## Task 3: Replace Placeholder Closest-Hit Shading With Geometry And Material Shading

**Parallel owner:** Agent C
**Can start after:** Task 2
**Depends on:** shader-visible scene buffers
**Unblocks:** visual parity work

**Files:**

- Modify: `engine/shader/hlsl/path_tracing.lib.hlsl`
- Modify: `engine/shader/hlsl/path_tracing_common.hlsli`
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp` only if descriptor binding flags need adjustment

- [ ] **Step 1: Bind path tracing scene buffers in HLSL**

Add declarations to `path_tracing.lib.hlsl`:

```hlsl
StructuredBuffer<PathTracingVertexData> g_vertices : register(t4, space0);
StructuredBuffer<uint> g_indices : register(t5, space0);
StructuredBuffer<PathTracingMaterialData> g_materials : register(t6, space0);
StructuredBuffer<PathTracingGeometryData> g_geometries : register(t7, space0);
StructuredBuffer<PathTracingInstanceData> g_instances : register(t8, space0);
```

Keep existing bindings `t0`, `u1`, `b2`, and `u3` unchanged.

- [ ] **Step 2: Use `InstanceIndex()` for shader array lookup**

In `PathTracingClosestHit()`, use the TLAS-local `InstanceIndex()` to select `g_instances`, then `material_index` to select `g_materials`:

```hlsl
const uint instance_index = InstanceIndex();
const uint safe_instance_index = min(instance_index, max(g_frame_data.instance_count, 1u) - 1u);
const PathTracingInstanceData instance_data = g_instances[safe_instance_index];
const PathTracingMaterialData material_data = g_materials[instance_data.material_index];
```

Do not use `InstanceID()` as an array index. `InstanceID()` is the engine entity id and can be sparse.

- [ ] **Step 3: Reconstruct triangle attributes from `PrimitiveIndex()`**

Fetch the hit triangle indices and interpolate vertex data:

```hlsl
const PathTracingGeometryData geometry_data = g_geometries[instance_data.geometry_index];
const uint primitive_index = PrimitiveIndex();
const uint index_base = geometry_data.index_offset + primitive_index * 3u;
const uint i0 = g_indices[index_base + 0u] + geometry_data.vertex_offset;
const uint i1 = g_indices[index_base + 1u] + geometry_data.vertex_offset;
const uint i2 = g_indices[index_base + 2u] + geometry_data.vertex_offset;

const PathTracingVertexData v0 = g_vertices[i0];
const PathTracingVertexData v1 = g_vertices[i1];
const PathTracingVertexData v2 = g_vertices[i2];

const float3 barycentric = float3(1.0f - attributes.barycentrics.x - attributes.barycentrics.y,
                                 attributes.barycentrics.x,
                                 attributes.barycentrics.y);
const float3 normal_os = normalize(v0.normal.xyz * barycentric.x +
                                   v1.normal.xyz * barycentric.y +
                                   v2.normal.xyz * barycentric.z);
const float3 tangent_os = normalize(v0.tangent.xyz * barycentric.x +
                                    v1.tangent.xyz * barycentric.y +
                                    v2.tangent.xyz * barycentric.z);
const float2 texcoord = v0.texcoord.xy * barycentric.x +
                        v1.texcoord.xy * barycentric.y +
                        v2.texcoord.xy * barycentric.z;
```

- [ ] **Step 4: Transform hit data into world space**

Use DXR object/world intrinsics so the shader follows the TLAS transform that D3D12 actually used:

```hlsl
const float3 world_position = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
const float3x4 object_to_world = ObjectToWorld3x4();
float3 normal_ws = normalize(mul((float3x3)object_to_world, normal_os));
float3 tangent_ws = normalize(mul((float3x3)object_to_world, tangent_os));
if (dot(normal_ws, -WorldRayDirection()) < 0.0f &&
    (material_data.flags & PICCOLO_PATH_TRACING_MATERIAL_FLAG_DOUBLE_SIDED) != 0u)
{
    normal_ws = -normal_ws;
}
```

- [ ] **Step 5: Produce factor-only material shading as the first non-placeholder output**

Use the material factors from Task 2 and keep output linear/HDR:

```hlsl
const float metallic = saturate(material_data.metallic_roughness_normal_occlusion.x);
const float roughness = saturate(material_data.metallic_roughness_normal_occlusion.y);
const float3 base_color = material_data.base_color_factor.rgb;
const float3 emissive = material_data.emissive_factor.rgb;
const float3 view_dir = normalize(g_frame_data.camera_position - world_position);
const float3 light_dir = normalize(float3(0.4f, 0.8f, 0.2f));
const float ndotl = saturate(dot(normal_ws, light_dir));
const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), base_color, metallic);
payload.radiance = base_color * (0.08f + 0.92f * ndotl) + emissive + f0 * (1.0f - roughness) * 0.02f;
payload.hit = 1;
```

This is the transition point away from debug gradient/barycentric color. Raster-compatible BRDF, texture sampling, and real lights are added in Task 4.

- [ ] **Step 6: Preserve HDR output before post processing**

Do not tone-map in `path_tracing.lib.hlsl`. Leave `g_scene_output[pixel] = accumulated;` as linear/HDR scene color so existing tone mapping and color grading remain responsible for display conversion.

- [ ] **Step 7: Build and commit**

Run:

```powershell
cmake --build build --config Debug --target PiccoloRuntime -- /v:minimal /clp:ErrorsOnly
cmake --build build --config Debug --target PiccoloEditor -- /v:minimal /clp:ErrorsOnly
```

Expected: both targets build and DXC accepts `path_tracing.lib.hlsl`.

Commit:

```powershell
git add engine/shader/hlsl/path_tracing.lib.hlsl `
        engine/shader/hlsl/path_tracing_common.hlsli `
        engine/source/runtime/function/render/passes/path_tracing_pass.cpp
git commit -m "feat: shade path tracing hits from scene data"
```

---

## Task 4: Add Raster-Compatible Material Textures And Direct Lighting Inputs

**Parallel owner:** Agent C
**Can start after:** Task 3
**Depends on:** geometry/material shading
**Unblocks:** visual parity with raster materials and raster lights

**Files:**

- Modify: `engine/source/runtime/function/render/render_resource.h`
- Modify: `engine/source/runtime/function/render/render_resource.cpp`
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.h`
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`
- Modify: `engine/shader/hlsl/path_tracing_common.hlsli`
- Modify: `engine/shader/hlsl/path_tracing.lib.hlsl`

- [ ] **Step 1: Extend path tracing frame data with raster light fields**

Add these fields to `PathTracingPass::FrameData` after `instance_count`:

```cpp
Vector4 ambient_light {0.02f, 0.02f, 0.02f, 0.0f};
RenderScenePointLight scene_point_lights[s_max_point_light_count] {};
RenderSceneDirectionalLight scene_directional_light {};
Matrix4x4 directional_light_proj_view {Matrix4x4::IDENTITY};
uint32_t point_light_count {0};
uint32_t _padding_light[3] {0, 0, 0};
```

- [ ] **Step 2: Fill light fields from existing raster per-frame data**

In `PathTracingPass::updateFrameData()`, read fields already written in `RenderResource::updatePerFrameBuffer()`:

```cpp
const MeshPerframeStorageBufferObject& raster_frame =
    m_render_resource_impl->m_mesh_perframe_storage_buffer_object;
frame_data.ambient_light = Vector4(raster_frame.ambient_light, 0.0f);
frame_data.scene_directional_light = raster_frame.scene_directional_light;
frame_data.directional_light_proj_view = raster_frame.directional_light_proj_view;
frame_data.point_light_count = std::min(raster_frame.point_light_num, s_max_point_light_count);
for (uint32_t light_index = 0; light_index < frame_data.point_light_count; ++light_index)
{
    frame_data.scene_point_lights[light_index] = raster_frame.scene_point_lights[light_index];
}
```

- [ ] **Step 3: Track material texture image views during scene buffer build**

Add this helper struct to `render_resource.h` near the path tracing GPU-data structs:

```cpp
struct RenderPathTracingMaterialTextureViews
{
    RHIImageView* base_color_image_view {nullptr};
    RHIImageView* metallic_roughness_image_view {nullptr};
    RHIImageView* normal_image_view {nullptr};
    RHIImageView* emissive_image_view {nullptr};
};
```

Add a vector to `RenderResource`:

```cpp
std::vector<RenderPathTracingMaterialTextureViews> m_path_tracing_material_texture_views;
```

In `RenderResource::updatePathTracingSceneBuffers(...)`, append one texture-view record at the same time each per-instance material record is appended. The texture-view vector index must match `shader_material_index`:

```cpp
RenderPathTracingMaterialTextureViews texture_views {};
texture_views.base_color_image_view = source_instance.material->base_color_image_view;
texture_views.metallic_roughness_image_view = source_instance.material->metallic_roughness_image_view;
texture_views.normal_image_view = source_instance.material->normal_image_view;
texture_views.emissive_image_view = source_instance.material->emissive_image_view;
m_path_tracing_material_texture_views.push_back(texture_views);

material_data.base_color_texture_index = shader_material_index;
material_data.metallic_roughness_texture_index = shader_material_index;
material_data.normal_texture_index = shader_material_index;
material_data.emissive_texture_index = shader_material_index;
```

Set those four texture indices before the `m_path_tracing_material_data.push_back(material_data)` call added by Task 2.

- [ ] **Step 4: Add fixed-size material texture descriptor arrays**

Add this constant in `path_tracing_pass.h` private scope or in an anonymous namespace in `path_tracing_pass.cpp`:

```cpp
static constexpr uint32_t kPathTracingMaxMaterialTextureCount = 1024;
```

Expand `PathTracingPass` descriptor bindings from 9 to 13. D3D12 maps `binding` directly to `BaseShaderRegister`, so array ranges must not overlap:

- binding 9: base color texture array, descriptor count `kPathTracingMaxMaterialTextureCount`, register range `t9..t1032`
- binding 1033: metallic roughness texture array, descriptor count `kPathTracingMaxMaterialTextureCount`, register range `t1033..t2056`
- binding 2057: normal texture array, descriptor count `kPathTracingMaxMaterialTextureCount`, register range `t2057..t3080`
- binding 3081: emissive texture array, descriptor count `kPathTracingMaxMaterialTextureCount`, register range `t3081..t4104`

Use `RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` and `RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR` for all four bindings. In `PathTracingPass::updateDescriptorSet()`, create four `std::vector<RHIDescriptorImageInfo>` arrays with exactly `kPathTracingMaxMaterialTextureCount` entries. Fill real entries from `m_render_resource_impl->m_path_tracing_material_texture_views`.

Do not write null image views into D3D12 descriptor arrays. Select a fallback image view for each texture kind from the first valid path tracing material texture view:

```cpp
RHIImageView* fallback_base_color = first_valid_texture_views.base_color_image_view;
RHIImageView* fallback_metallic_roughness = first_valid_texture_views.metallic_roughness_image_view;
RHIImageView* fallback_normal = first_valid_texture_views.normal_image_view;
RHIImageView* fallback_emissive = first_valid_texture_views.emissive_image_view;
```

If any fallback is null, return `false` from `updateDescriptorSet()` so the pipeline falls back to raster instead of writing an invalid descriptor range. Fill unused entries, and any missing per-material entry, with that texture kind's fallback view, `sampler = m_rhi->getOrCreateDefaultSampler(Default_Sampler_Linear)`, and `imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`.

- [ ] **Step 5: Bind material texture arrays in HLSL**

Add matching declarations to `path_tracing_common.hlsli` and `path_tracing.lib.hlsl`:

```hlsl
#define PICCOLO_PATH_TRACING_MAX_MATERIAL_TEXTURES 1024
```

```hlsl
Texture2D<float4> g_base_color_textures[PICCOLO_PATH_TRACING_MAX_MATERIAL_TEXTURES] : register(t9, space0);
Texture2D<float4> g_metallic_roughness_textures[PICCOLO_PATH_TRACING_MAX_MATERIAL_TEXTURES] : register(t1033, space0);
Texture2D<float4> g_normal_textures[PICCOLO_PATH_TRACING_MAX_MATERIAL_TEXTURES] : register(t2057, space0);
Texture2D<float4> g_emissive_textures[PICCOLO_PATH_TRACING_MAX_MATERIAL_TEXTURES] : register(t3081, space0);
SamplerState g_base_color_sampler : register(s9, space0);
SamplerState g_metallic_roughness_sampler : register(s1033, space0);
SamplerState g_normal_sampler : register(s2057, space0);
SamplerState g_emissive_sampler : register(s3081, space0);
```

Use `SampleLevel(..., 0.0f)` in ray tracing shaders because no screen-space derivatives are available in closest-hit shaders.

- [ ] **Step 6: Apply raster material texture semantics in closest-hit**

Replace factor-only material values with texture-aware values matching `mesh_gbuffer.frag.hlsl`:

```hlsl
float4 base_color_sample = float4(1.0f, 1.0f, 1.0f, 1.0f);
if (material_data.base_color_texture_index != PICCOLO_PATH_TRACING_INVALID_INDEX)
{
    base_color_sample = g_base_color_textures[material_data.base_color_texture_index].SampleLevel(g_base_color_sampler, texcoord, 0.0f);
}

float4 mr_sample = float4(1.0f, 1.0f, 1.0f, 1.0f);
if (material_data.metallic_roughness_texture_index != PICCOLO_PATH_TRACING_INVALID_INDEX)
{
    mr_sample = g_metallic_roughness_textures[material_data.metallic_roughness_texture_index].SampleLevel(g_metallic_roughness_sampler, texcoord, 0.0f);
}

float3 tangent_normal = float3(0.0f, 0.0f, 1.0f);
if (material_data.normal_texture_index != PICCOLO_PATH_TRACING_INVALID_INDEX)
{
    tangent_normal = g_normal_textures[material_data.normal_texture_index].SampleLevel(g_normal_sampler, texcoord, 0.0f).xyz * 2.0f - 1.0f;
}

float3 emissive_sample = float3(1.0f, 1.0f, 1.0f);
if (material_data.emissive_texture_index != PICCOLO_PATH_TRACING_INVALID_INDEX)
{
    emissive_sample = g_emissive_textures[material_data.emissive_texture_index].SampleLevel(g_emissive_sampler, texcoord, 0.0f).rgb;
}

const float3 base_color = base_color_sample.rgb * material_data.base_color_factor.rgb;
const float metallic = mr_sample.b * material_data.metallic_roughness_normal_occlusion.x;
const float roughness = max(0.04f, mr_sample.g * material_data.metallic_roughness_normal_occlusion.y);
const float3 emissive = emissive_sample * material_data.emissive_factor.rgb;
```

Use `CalculateTangentNormal(normal_ws, tangent_ws, tangent_normal)` from `common.hlsli` to match raster normal-map behavior.

- [ ] **Step 7: Use raster BRDF and direct lights in closest-hit**

Mirror the new frame-data fields in HLSL with the same field order and 16-byte alignment. Then reuse `BRDF(...)` from `common.hlsli` for ambient, point lights, and the directional light. Start with unshadowed direct lighting so the path tracing output matches raster material response before shadow-ray recursion is added:

```hlsl
const float3 n = normalize(CalculateTangentNormal(normal_ws, tangent_ws, tangent_normal));
const float3 v = normalize(g_frame_data.camera_position - world_position);
const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), base_color, metallic);

float3 lo = float3(0.0f, 0.0f, 0.0f);
for (uint light_index = 0; light_index < g_frame_data.point_light_count && light_index < M_MAX_POINT_LIGHT_COUNT; ++light_index)
{
    const float3 light_position = g_frame_data.scene_point_lights[light_index].position;
    const float light_radius = g_frame_data.scene_point_lights[light_index].radius;
    const float3 l = normalize(light_position - world_position);
    const float nol = min(dot(n, l), 1.0f);
    const float distance_to_light = length(light_position - world_position);
    const float distance_attenuation = 1.0f / (distance_to_light * distance_to_light + 1.0f);
    const float radius_attenuation = 1.0f - ((distance_to_light * distance_to_light) /
                                            (light_radius * light_radius));
    const float light_attenuation = radius_attenuation * distance_attenuation * nol;
    if (light_attenuation > 0.0f)
    {
        const float3 en = g_frame_data.scene_point_lights[light_index].intensity * light_attenuation;
        lo += BRDF(l, v, n, f0, base_color, metallic, roughness) * en;
    }
}

const float3 directional_l = normalize(g_frame_data.scene_directional_light.direction);
const float directional_nol = max(dot(n, directional_l), 0.0f);
lo += BRDF(directional_l, v, n, f0, base_color, metallic, roughness) *
      g_frame_data.scene_directional_light.color * directional_nol;

const float3 ambient = base_color * g_frame_data.ambient_light.xyz;
payload.radiance = ambient + lo + emissive;
```

This task intentionally does not add recursive shadow rays. After this lands, Task 7 decides whether shadow parity is required for the target scene; if required, extend `RHIRayTracingPipelineCreateInfo` and SBT creation to support a second miss record and a shadow-ray payload before changing shading.

- [ ] **Step 8: Build, manually check, and commit**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /v:minimal /clp:ErrorsOnly
```

Manual check with `RenderSceneMode=PathTracing`: changing a static object's base color texture, roughness factor, metallic factor, normal texture, or directional light color should change the path traced object in the same direction as raster mode.

Commit:

```powershell
git add engine/source/runtime/function/render/passes/path_tracing_pass.h `
        engine/source/runtime/function/render/passes/path_tracing_pass.cpp `
        engine/source/runtime/function/render/render_resource.h `
        engine/source/runtime/function/render/render_resource.cpp `
        engine/shader/hlsl/path_tracing_common.hlsli `
        engine/shader/hlsl/path_tracing.lib.hlsl
git commit -m "feat: feed raster material lighting into path tracing"
```

---

## Task 5: Harden Resource States, Descriptors, And Resize Behavior

**Parallel owner:** Agent A
**Can start after:** Task 1; coordinate with Tasks 2-4 if descriptors are changing
**Depends on:** current merged baseline
**Unblocks:** stable editor use

**Files:**

- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.h`
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp`
- Modify: `engine/source/runtime/function/render/render_pipeline.cpp`

- [ ] **Step 1: Verify scene output usage flags**

Confirm `_main_camera_pass_backup_buffer_odd` retains these usage bits in `MainCameraPass::setupAttachments()`:

```cpp
RHI_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
RHI_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
RHI_IMAGE_USAGE_STORAGE_BIT
```

The current D3D12 SRV path creates shader-resource descriptors for images with `RHI_IMAGE_USAGE_INPUT_ATTACHMENT_BIT`, so do not add `RHI_IMAGE_USAGE_SAMPLED_BIT` unless a later renderer change actually samples this image as a sampled texture. If `RHI_IMAGE_USAGE_STORAGE_BIT` is missing, add it without removing existing raster/postprocess usage.

- [ ] **Step 2: Track accumulation image old layout explicitly**

`PathTracingPass::dispatch()` currently transitions `m_accumulation_image` from `RHI_IMAGE_LAYOUT_UNDEFINED` every frame. Add a member:

```cpp
RHIImageLayout m_accumulation_image_layout {RHI_IMAGE_LAYOUT_UNDEFINED};
```

Transition from `m_accumulation_image_layout` to `RHI_IMAGE_LAYOUT_GENERAL`, then set `m_accumulation_image_layout = RHI_IMAGE_LAYOUT_GENERAL` after the barrier. Reset it to `RHI_IMAGE_LAYOUT_UNDEFINED` in `destroyAccumulationImage()` and after framebuffer recreation.

- [ ] **Step 3: Recheck descriptor heap replay around DXR dispatch and UI**

Run:

```powershell
rg -n "bindEngineDescriptorHeaps|replayRootDescriptorTables|DispatchRays|cmdTraceRays" engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp
```

Expected: `D3D12RHI::cmdTraceRays()` calls `bindEngineDescriptorHeaps(..., RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR)` and `replayRootDescriptorTables(..., RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR)` before `DispatchRays()`. If a regression is found, restore that ordering in `D3D12RHI::cmdTraceRays()`; do not modify `ui_pass.*`.

- [ ] **Step 4: Run resize and visible checks**

Build and run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /v:minimal /clp:ErrorsOnly
.\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10
```

Manual actions with `RenderSceneMode=PathTracing`:

- maximize the editor window
- restore the window
- resize rapidly for 10 seconds
- restart with `RenderSceneMode=Raster`

Expected: no device removal, no stale image view crash, UI remains visible, and PathTracing either renders static scene color or falls back to raster without a black frame.

- [ ] **Step 5: Commit**

```powershell
git add engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp `
        engine/source/runtime/function/render/passes/path_tracing_pass.h `
        engine/source/runtime/function/render/passes/path_tracing_pass.cpp `
        engine/source/runtime/function/render/passes/main_camera_pass.cpp `
        engine/source/runtime/function/render/render_pipeline.cpp
git commit -m "fix: harden d3d12 path tracing resource states"
```

---

## Task 6: Verify Runtime Mode Selection And Backend Fallbacks

**Parallel owner:** Agent D
**Can start after:** Tasks 1 and 5
**Depends on:** stable startup
**Unblocks:** release confidence

**Files:**

- Modify: `engine/source/runtime/function/render/render_pipeline.cpp` only if verification exposes mode selection bugs
- Modify: `engine/source/runtime/resource/config_manager/config_manager.cpp` only if config parsing bugs are found
- Modify: editor `.ini` files only if defaults drift

- [ ] **Step 1: Confirm defaults remain raster**

Run:

```powershell
rg -n "RenderSceneMode" engine/configs
```

Expected: development, D3D12 deployment, Vulkan deployment, and non-Windows deployment configs all contain `RenderSceneMode=Raster`.

- [ ] **Step 2: Verify raster boot on D3D12**

Run:

```powershell
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
```

Expected: initialized backend is D3D12 and log contains `engine start`.

- [ ] **Step 3: Verify PathTracing requested mode on D3D12**

Temporarily edit `build/engine/source/editor/Debug/PiccoloEditor.ini`:

```ini
RenderBackend=D3D12
RenderSceneMode=PathTracing
```

Run the same D3D12 boot smoke. Expected: initialized backend is D3D12, log contains `engine start`, log reaches a path tracing line such as `Path tracing scene export`, and the process does not exit before timeout. Restore the ini after the run.

- [ ] **Step 4: Verify Vulkan and Auto**

Run:

```powershell
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -RenderBackend Vulkan -ExpectedBackend Vulkan
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -RenderBackend Auto -ExpectedBackend D3D12
```

Expected: Vulkan starts as Vulkan with ray tracing unsupported/no-op; Auto starts as D3D12 on the current Windows machine.

- [ ] **Step 5: Commit only if code or config changed**

If verification required fixes:

```powershell
git add engine/source/runtime/function/render/render_pipeline.cpp `
        engine/source/runtime/resource/config_manager/config_manager.cpp `
        engine/configs/development/PiccoloEditor.ini `
        engine/configs/deployment/PiccoloEditor.ini `
        engine/configs/deployment/PiccoloEditorVulkan.ini `
        engine/configs/deployment/PiccoloEditorNonWindows.ini
git commit -m "fix: verify path tracing runtime mode selection"
```

Do not create an empty commit if no files changed.

---

## Task 7: Manual Visual Parity Pass

**Parallel owner:** Agent D
**Can start after:** Tasks 3, 4, and 5
**Depends on:** material and lighting bridge
**Unblocks:** deciding whether more shader work is required

**Files:**

- Modify: `engine/shader/hlsl/path_tracing.lib.hlsl` if visual mismatch is shader-side
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp` if mismatch is frame data or descriptor-side
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp` if mismatch is post/UI flow-side
- Modify: `engine/source/runtime/function/render/render_resource.cpp` if mismatch is transform or instance data-side

- [ ] **Step 1: Capture raster reference**

Set `RenderSceneMode=Raster` in the Debug editor ini and run:

```powershell
.\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10
```

Save the generated capture path from script output for comparison.

- [ ] **Step 2: Capture PathTracing output**

Set `RenderSceneMode=PathTracing` in the Debug editor ini and run the visible smoke again:

```powershell
.\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10
```

Expected: capture is non-black in the central scene region; UI and editor overlays remain visible.

- [ ] **Step 3: Compare required invariants**

Manually inspect both captures and the live editor:

- static opaque meshes appear in the same world positions
- camera orientation is not flipped
- output is not obviously double tone-mapped
- UI is unchanged
- axis/debug overlay is above scene and below ImGui
- skipped skinned/transparent objects are documented and not treated as regressions in this first pass

- [ ] **Step 4: Fix one mismatch category at a time**

Use this mapping:

- flipped image or wrong ray direction: fix ray generation in `path_tracing.lib.hlsl`
- wrong object transforms: fix `RenderResource::collectPathTracingInstances()` transform packing or D3D12 `D3D12_RAYTRACING_INSTANCE_DESC::Transform`
- black output with TLAS present: inspect descriptors in `PathTracingPass::updateDescriptorSet()` and D3D12 AS SRV writes
- UI missing or wrong order: inspect `MainCameraPass::drawPathTracing()` subpass order only
- color too dark/bright before post: inspect shader HDR values and avoid shader-side tone mapping

- [ ] **Step 5: Re-run visible smoke after each fix**

Run:

```powershell
.\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10
```

Expected: non-black capture, no early editor exit, and improved parity for the targeted mismatch.

- [ ] **Step 6: Commit visual parity fixes**

```powershell
git add engine/shader/hlsl/path_tracing.lib.hlsl `
        engine/source/runtime/function/render/passes/path_tracing_pass.cpp `
        engine/source/runtime/function/render/passes/main_camera_pass.cpp `
        engine/source/runtime/function/render/render_resource.cpp
git commit -m "fix: align d3d12 path tracing visual output"
```

Only include files that were actually modified.

---

## Task 8: Performance Sanity And Static Scene Caching

**Parallel owner:** Agent A or Agent B
**Can start after:** Tasks 1, 2, and 5
**Depends on:** stable PathTracing startup
**Unblocks:** usable editor framerate

**Files:**

- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`
- Modify: `engine/source/runtime/function/render/render_resource.cpp`
- Modify: `engine/source/runtime/function/render/render_scene.cpp`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp` only if GPU barriers or heap replay cause stalls

- [ ] **Step 1: Add temporary frame-local diagnostic counters**

Log these values once every 120 frames in PathTracing mode:

- number of collected path tracing instances
- number of BLAS builds this frame
- whether TLAS rebuilt this frame
- whether accumulation image recreated this frame
- sample index

Remove or reduce the diagnostic log before committing if it is too noisy.

- [ ] **Step 2: Run a static scene for 30 seconds**

Set `RenderSceneMode=PathTracing`, start the editor, do not move the camera, and observe logs.

Expected after the first few frames: BLAS builds stop, TLAS rebuilds stop unless scene signatures change, accumulation image is not recreated, and sample index increases.

- [ ] **Step 3: Fix repeated BLAS, TLAS, or accumulation work**

If BLAS builds continue every frame, inspect `RenderMeshGPUResource::path_tracing_blas_dirty`, `RenderResource::ensurePathTracingBLAS()`, and mesh upload paths that recreate buffers. If TLAS rebuilds continue every frame, inspect `RenderScene::PathTracingEntitySignature::operator==()` and remove transient values from the signature. If accumulation recreates every frame, inspect `PathTracingPass::ensureAccumulationImage()` and `m_extent`.

- [ ] **Step 4: Commit performance fixes**

```powershell
git add engine/source/runtime/function/render/passes/path_tracing_pass.cpp `
        engine/source/runtime/function/render/render_resource.cpp `
        engine/source/runtime/function/render/render_scene.cpp `
        engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp
git commit -m "fix: stabilize d3d12 path tracing static-scene caching"
```

Only include files that were actually modified.

---

## Task 9: Final Verification And Handoff Notes

**Parallel owner:** Agent E
**Can start after:** Tasks 1-8
**Depends on:** integrated branch
**Unblocks:** merge readiness

**Files:**

- Modify: `Docs/superpowers/plans/2026-06-12-d3d12-path-tracing-completion.md`

- [ ] **Step 1: Build Debug and Release**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /v:minimal /clp:ErrorsOnly
cmake --build build --config Release --target PiccoloEditor -- /v:minimal /clp:ErrorsOnly
```

Expected: both builds exit with code 0.

- [ ] **Step 2: Run backend boot smokes**

Run:

```powershell
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -RenderBackend Vulkan -ExpectedBackend Vulkan
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -RenderBackend Auto -ExpectedBackend D3D12
```

Expected: all three scripts exit with code 0.

- [ ] **Step 3: Run D3D12 visible smoke in Raster and PathTracing modes**

Run the visible smoke once with `RenderSceneMode=Raster` and once with `RenderSceneMode=PathTracing`:

```powershell
.\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10
```

Expected: both modes produce non-black captures with visible UI and no early editor exit.

- [ ] **Step 4: Record verification results in this plan**

Append a `## Verification Notes` section to this file containing exact command lines, exit codes, capture paths, and manual notes. Do not recreate the deleted historical docs.

- [ ] **Step 5: Commit verification notes**

```powershell
git add Docs/superpowers/plans/2026-06-12-d3d12-path-tracing-completion.md
git commit -m "docs: record d3d12 path tracing completion verification"
```

---

## Acceptance Criteria

The next implementation pass is complete when all of these are true:

- `RenderSceneMode=Raster` remains the default in all editor config files.
- `RenderSceneMode=PathTracing` on D3D12/DXR hardware boots and reaches the path tracing dispatch path.
- Vulkan builds and runs with ray tracing unsupported/no-op.
- Path tracing renders static opaque geometry in the same world positions as raster rendering.
- Path tracing closest-hit shading uses interpolated vertex normals, tangents, UVs, material factors, and material textures for static opaque meshes.
- Path tracing direct lighting uses the same ambient light, point light list, directional light, and BRDF helper as the raster material path for the first visual parity milestone.
- Path tracing output is linear/HDR before the existing tone mapping and color grading chain.
- UI, ImGui, combine UI, and editor axis/debug overlay remain raster-rendered and visible in the same order as raster mode.
- Static scenes stop rebuilding BLAS/TLAS after initial stabilization unless transforms, scene membership, mesh data, or material data changes.
- Accumulation resets on camera changes, scene changes, material changes, resize, maximize, restore, and framebuffer recreation.
- Resize, maximize, restore, and rapid resize do not crash.
- Existing Debug and Release editor builds pass.
- Existing D3D12, Vulkan, Auto boot smokes pass.
- Existing D3D12 visible smoke passes in Raster mode and PathTracing mode.
- No new test code was added.

---

## Known Non-Goals For This Plan

- Do not implement Vulkan path tracing.
- Do not path trace ImGui or editor UI.
- Do not add a raster overlay for skipped skinned or transparent objects.
- Do not implement full multi-bounce global illumination before first-pass visual parity is achieved.
- Do not add new automated test code unless the user explicitly asks.

---

## Suggested Execution Order

1. Task 1: quiet and stabilize logs so verification is readable.
2. Task 2: export shader-visible scene data.
3. Task 3: replace placeholder closest-hit shading with geometry/material shading.
4. Task 4: add raster-compatible material texture and direct-lighting inputs.
5. Task 5: harden resource states and resize handling.
6. Task 6: verify backend/mode fallback.
7. Task 7: perform visual parity pass and fix focused mismatch categories.
8. Task 8: stabilize performance and static-scene caching.
9. Task 9: final verification and handoff notes.

If execution uses subagents, run Tasks 1 and 5 in parallel only if each agent stays inside its file ownership. Run Tasks 2, 3, and 4 sequentially because they intentionally change the shader/RHI data contract.

## Verification Notes

- Build verification
    - Command: `"C:\Program Files\CMake\bin\cmake.exe" --build d:\program\Piccolo\build --config Debug --target PiccoloEditor -- /v:minimal /clp:ErrorsOnly`
    - Exit: 0
    - Note: MSBuild banner printed, no errors.
    - Command: `"C:\Program Files\CMake\bin\cmake.exe" --build d:\program\Piccolo\build --config Release --target PiccoloEditor -- /v:minimal /clp:ErrorsOnly`
    - Exit: 0
    - Note: MSBuild banner printed, no errors.

- Backend boot smoke verification (Debug)
    - Command: `"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe" -ExecutionPolicy Bypass -File d:\program\Piccolo\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir d:\program\Piccolo\build -Configuration Debug -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback`
    - Exit: 0
    - Log: `build/test_d3d12_boot.log`
    - Manual log check: contains `Initialized RHI backend: D3D12` and `engine start`.
    - Command: `"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe" -ExecutionPolicy Bypass -File d:\program\Piccolo\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir d:\program\Piccolo\build -Configuration Debug -RenderBackend Vulkan -ExpectedBackend Vulkan`
    - Exit: 0
    - Log: `build/test_vulkan_boot.log`
    - Manual log check: contains `Initialized RHI backend: Vulkan` and `engine start`.
    - Note: Vulkan validation messages are present in log; engine still starts and smoke exits 0.
    - Command: `"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe" -ExecutionPolicy Bypass -File d:\program\Piccolo\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir d:\program\Piccolo\build -Configuration Debug -RenderBackend Auto -ExpectedBackend D3D12`
    - Exit: 0
    - Log: `build/test_auto_d3d12_boot.log`
    - Manual log check: contains `Initialized RHI backend: D3D12` and `engine start`.

- D3D12 visible smoke verification (Debug)
    - Raster command: `powershell -ExecutionPolicy Bypass -File d:\program\Piccolo\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir d:\program\Piccolo\build -Configuration Debug -WarmupSeconds 20`
    - Exit: 0
    - Capture: `build/test_d3d12_editor_visible_raster.png`
    - Log: `build/test_d3d12_editor_visible_raster.log`
    - Script metric: non-black sampled pixel ratio 100.0000%.
    - Manual note: scene, UI, and overlays are visible and consistent with expected raster output.
    - PathTracing command: `powershell -ExecutionPolicy Bypass -File d:\program\Piccolo\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir d:\program\Piccolo\build -Configuration Debug -WarmupSeconds 20`
    - Exit: 0
    - Capture: `build/test_d3d12_editor_visible_pathtracing.png`
    - Log: `build/test_d3d12_editor_visible_pathtracing.log`
    - Script metric: non-black sampled pixel ratio 100.0000%.
    - Manual note: scene content and UI are visible; no early editor exit observed during run.

- Task 8 static-scene caching sanity (PathTracing)
    - Command: `"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe" -ExecutionPolicy Bypass -File d:\program\Piccolo\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir d:\program\Piccolo\build -Configuration Debug -WarmupSeconds 30`
    - Exit: 0
    - Log source: `build/test_d3d12_editor_visible.stdout.log`
    - Diagnostic note: periodic `Path tracing perf` lines show stable `collected_instances=29`, `blas_builds=0`, `tlas_rebuilt=0`, `accumulation_recreated=0`, and monotonic sample index growth after warmup, indicating no repeated BLAS/TLAS or accumulation image churn in static scene.
