# Decouple GPU Skinning Architecture & Fix Offset Bugs

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate all "Path Tracing" contamination from `GpuSkinningPass` (types, methods, shader naming), and fix two offset bugs that prevent skinned meshes from animating correctly in path tracing.

**Status:** The current implementation has GpuSkinningPass depending on path-tracing types (`RenderPathTracingCollectedInstance`, `RenderPathTracingVertexGPUData`), calling path-tracing methods (`collectPathTracingInstances()`), and using a path-tracing-named shader (`path_tracing_skin.comp.hlsl`). Additionally, two offset calculation bugs cause skinned vertex data to be written/read at wrong positions.

**Tech Stack:** C++17, D3D12, HLSL SM 6.6, Piccolo RHI

---

## Problem Catalog

### A. Architectural Contamination

| # | Problem | Location | Detail |
|---|---------|----------|--------|
| A1 | Shader filename | `path_tracing_skin.comp.hlsl` | "path_tracing" in name — skinning is path-tracing-agnostic |
| A2 | Shader include | `#include "path_tracing_common.hlsli"` | Pulls in `PathTracingFrameData`, `PathTracingMaterialData`, `PathTracingGeometryData`, `PathTracingInstanceData` — all unused; only needs `MeshVertexJointBindingData` and `JointMatrixData` from `common.hlsli` |
| A3 | Shader output type | `RWStructuredBuffer<PathTracingVertexData>` | Output type defined by path tracing header |
| A4 | C++ instance type | `RenderPathTracingCollectedInstance` | Skinning pass depends on path-tracing-specific collection struct (carries BLAS, material_index, etc. — all unused by skinning) |
| A5 | C++ instance collection | `collectPathTracingInstances()` | Calls a path-tracing method to get mesh/joint data |
| A6 | C++ buffer sizing type | `sizeof(RenderPathTracingVertexGPUData)` | Output buffer element size defined by path tracing header |
| A7 | Storage map naming | `path_tracing_skinned_resources` | Map on `RenderMeshGPUResource` has "path_tracing" in name |
| A8 | Bytecode macro | `PICCOLO_D3D12_PATH_TRACING_SKIN_COMP` | Macro references path tracing |

**Root cause:** Skinning is a generic mesh deformation operation — it takes rest-pose vertices + joint matrices and outputs deformed vertices. The output format (positions + normals + tangents + texcoords) is not inherently path-tracing-specific. The naming and type dependencies create false coupling.

### B. Offset Bugs

| # | Bug | Location | Severity |
|---|-----|----------|----------|
| B1 | Per-instance position buffer written at wrong offset | `path_tracing_skin.comp.hlsl:78` | 🔴 All skinned instances after the 1st get out-of-bounds position writes → BLAS built from garbage |
| B2 | `geometry_data.vertex_offset` is `g_vertices` offset, not `g_skinned_vertices` offset | `render_resource.cpp:440` | 🔴 Shader reads skinned vertex attributes from wrong positions in `g_skinned_vertices` |

#### Bug B1 Detail

The compute shader writes to TWO output buffers:

| Buffer | Register | Type | Size |
|--------|----------|------|------|
| `g_skinned_positions` | u6 | Per-instance (BLAS geometry source) | `vertex_count` entries |
| `g_skinned_vertices` | u7 | Flat (all skinned instances concatenated) | `total_skinned_vertices` entries |

The shader uses the SAME index for both:
```hlsl
uint out_idx = g_constants.output_vertex_offset + vertex_id;
g_skinned_positions[out_idx] = skinned_position;   // BUG: OOB for 2nd+ instance
g_skinned_vertices[out_idx]  = v;                   // OK: flat buffer, cumulative offset correct
```

`output_vertex_offset` is cumulative across instances. For the 2nd skinned instance:
- `output_vertex_offset = vertex_count_of_first_instance` (e.g., 500)
- `g_skinned_positions` is a PER-INSTANCE buffer with only `vertex_count_of_second_instance` entries (e.g., 300)
- Writes at indices `[500..799]` on a buffer of size 300 → **out of bounds, garbage data**

#### Bug B2 Detail

In `updatePathTracingSceneBuffers()`:
```cpp
// Skinned instances get their vertex_offset from g_vertices:
geometry_data.vertex_offset = m_path_tracing_vertex_data.size();  // e.g., 100
// Then placeholder vertices are pushed to m_path_tracing_vertex_data
```

`g_vertices` layout (with placeholders): `[StaticA:0..99][SkinnedB_placeholder:100..599][StaticC:600..799]`
`g_skinned_vertices` layout: `[SkinnedB_actual:0..499]`

Path tracing shader reads: `g_skinned_vertices[geometry_data.vertex_offset + local_i]` = `g_skinned_vertices[100 + local_i]`

Reads at offset 100-599 from a 500-element buffer that starts at 0 → **reads shifted/wrong data**.

---

## Target Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│                      RenderMeshGPUResource                            │
│                                                                       │
│  skinned_mesh_outputs[instance_id]         (GpuSkinningPass writes)  │
│    ├── .skinned_position_buffer  (float3, BLAS geometry source)      │
│    ├── .skinned_position_memory                                        │
│    ├── .skinned_vertex_offset    (offset into flat vertex buffer)    │
│    ├── .vertex_count                                                  │
│    └── .index_count                                                   │
│                                                                       │
│  path_tracing_skinned_resources[instance_id]  (PathTracingPass uses) │
│    └── .blas                    (BLAS built from position buffer)    │
└───────┬────────────────────────────────────────┬─────────────────────┘
        │ write (compute dispatch)               │ read (BLAS build, trace)
        │                                        │
┌───────┴──────────────┐              ┌──────────┴──────────────────────┐
│   GpuSkinningPass    │              │       PathTracingPass            │
│                      │              │                                  │
│ Types:               │              │ Types:                           │
│  CollectedSkinnedMesh│              │  PathTracingVertexData           │
│  SkinnedVertexData   │              │  RenderPathTracingVertexGPUData  │
│                      │              │                                  │
│ Shader:              │              │ Shader:                          │
│  gpu_skinning.comp   │              │  path_tracing.lib.hlsl           │
│  gpu_skinning.hlsli  │              │  path_tracing_common.hlsli       │
│                      │              │                                  │
│ dispatch():          │              │ buildTopLevelAS():               │
│  1. Collect skinned  │              │  1. Static BLAS ensure           │
│     meshes from      │              │  2. Per-instance BLAS            │
│     RenderScene      │   flat       │     from skinned_position_buffer │
│  2. Upload joint     │   buffer ←──→│  3. TLAS rebuild                 │
│     matrices         │   handle     │                                  │
│  3. Per-instance     │              │ updatePathTracingSceneBuffers(): │
│     compute dispatch │              │  Static: vertex_offset =         │
│  4. UAV barrier      │              │    m_path_tracing_vertex_data    │
│                      │              │    .size()                       │
│  Output:             │              │  Skinned: vertex_offset =        │
│  - Per-instance      │              │    mesh_output.skinned_vertex_   │
│    position buffer   │              │    offset (looked up from map)   │
│  - Flat vertex data  │              │  - NO placeholder push for       │
│    buffer (owned by  │              │    skinned vertices              │
│    GpuSkinningPass,  │              │                                  │
│    handle stored on  │              │ PathTracingClosestHit():         │
│    RenderResource)   │              │  if (flags & 1)                  │
│                      │              │    read g_skinned_vertices       │
│                      │              │  else                            │
│                      │              │    read g_vertices               │
└──────────────────────┘              └──────────────────────────────────┘

No PathTracing types in GpuSkinningPass.
No GpuSkinningPass method calls from PathTracingPass.
Communication: RenderMeshGPUResource buffers + RenderResource flat buffer handle.
```

### Key design properties

1. **Zero type coupling.** `GpuSkinningPass` defines its own types (`CollectedSkinnedMesh`, `GpuSkinnedVertexGPUData`). No `RenderPathTracing*` types appear in skinning pass code.
2. **Zero method coupling.** `GpuSkinningPass` does not call `collectPathTracingInstances()`. It collects skinned meshes directly from `RenderScene::m_render_entities`.
3. **Storage split.** `skinned_mesh_outputs` (skinned vertex data) is separate from `path_tracing_skinned_resources` (BLAS). Skinning writes to the former; path tracing creates the latter.
4. **Offset via lookup, not ordering.** `skinned_vertex_offset` is stored per-instance by GpuSkinningPass. PathTracingPass looks it up when creating geometry records — no ordering dependency between the two passes' collection logic.

---

## File Map

| File | Change | Description |
|------|--------|-------------|
| `engine/shader/hlsl/gpu_skinning.hlsli` | **NEW** | `SkinnedVertexData` struct + constants, self-contained skinning types |
| `engine/shader/hlsl/gpu_skinning.comp.hlsl` | **NEW** (rename from `path_tracing_skin.comp.hlsl`) | Renamed, removed `path_tracing_common.hlsli` include, uses `gpu_skinning.hlsli` |
| `engine/shader/hlsl/path_tracing_skin.comp.hlsl` | **DELETE** | Replaced by `gpu_skinning.comp.hlsl` |
| `engine/source/.../render/passes/gpu_skinning_pass.h` | Modify | New types (`CollectedSkinnedMesh`, `GpuSkinnedVertexGPUData`); remove path-tracing type dependencies |
| `engine/source/.../render/passes/gpu_skinning_pass.cpp` | Modify | Self-contained collection; fix offset bugs; use new types; reference new shader macro |
| `engine/source/.../render/render_gpu_resource.h` | Modify | Add `SkinnedMeshOutput` struct; split from `SkinnedPathTracingResources` |
| `engine/source/.../render/render_resource.h` | Modify | Add `GpuSkinnedVertexGPUData` struct (or reference from skinning pass); add `m_skinned_vertex_buffer` getter/setter |
| `engine/source/.../render/render_resource.cpp` | Modify | Fix B2: lookup `skinned_vertex_offset` from mesh output map; stop pushing placeholders for skinned instances |
| `engine/source/.../render/render_shader_bytecode.h` | Modify | Rename macros: `PATH_TRACING_SKIN_COMP` → `GPU_SKINNING_COMP` |
| `engine/source/.../render/passes/path_tracing_pass.cpp` | Modify | Use `skinned_mesh_outputs` map for BLAS buffer lookup |
| `engine/source/.../render/render_scene.h` | Modify | Add `getSkinnedRenderEntities()` or expose entity iteration |
| `engine/shader/hlsl/path_tracing.lib.hlsl` | No change | Already reads `g_skinned_vertices` correctly via `geometry_data.vertex_offset` |

---

## Tasks

### Task 1: Create self-contained skinning HLSL types (decouple shader)

**Files:**
- Create: `engine/shader/hlsl/gpu_skinning.hlsli`
- Create: `engine/shader/hlsl/gpu_skinning.comp.hlsl` (rename)
- Delete: `engine/shader/hlsl/path_tracing_skin.comp.hlsl`
- Modify: `engine/source/runtime/function/render/render_shader_bytecode.h`

- [ ] **Step 1: Create `gpu_skinning.hlsli`**

Self-contained header — no dependency on `path_tracing_common.hlsli`:

```hlsl
#ifndef PICCOLO_GPU_SKINNING_HLSLI
#define PICCOLO_GPU_SKINNING_HLSLI

// Output vertex data from GPU skinning compute shader.
// Layout matches PathTracingVertexData so consumers can reinterpret-cast the buffer.
struct SkinnedVertexData
{
    float4 position;
    float4 normal;
    float4 tangent;
    float4 texcoord;
};

struct SkinComputeConstants
{
    uint vertex_count;
    uint joint_matrix_offset;
    uint output_vertex_offset; // offset into flat g_skinned_vertices (u7 only)
    uint _padding;
};

#endif
```

- [ ] **Step 2: Create `gpu_skinning.comp.hlsl`** (rename + cleanup)

Copy from `path_tracing_skin.comp.hlsl`, then:
1. Replace `#include "path_tracing_common.hlsli"` → `#include "common.hlsli"` + `#include "gpu_skinning.hlsli"`
2. Replace `PathTracingVertexData` → `SkinnedVertexData` in output buffer declaration
3. **Fix Bug B1**: Write `g_skinned_positions[vertex_id]` instead of `g_skinned_positions[out_idx]`

```hlsl
#include "common.hlsli"
#include "gpu_skinning.hlsli"

// Input: rest-pose vertex data from mesh GPU buffers
StructuredBuffer<float3>                     g_rest_positions      : register(t0, space0);
StructuredBuffer<MeshVertexJointBindingData> g_joint_bindings      : register(t1, space0);
StructuredBuffer<float3>                     g_rest_normal_tangent : register(t2, space0);
StructuredBuffer<float2>                     g_rest_texcoords      : register(t3, space0);
StructuredBuffer<JointMatrixData>            g_joint_matrices      : register(t4, space0);

ConstantBuffer<SkinComputeConstants> g_constants : register(b5, space0);

// u6: per-instance skinned positions (BLAS geometry source)
RWStructuredBuffer<float3>            g_skinned_positions : register(u6, space0);
// u7: flat skinned vertex data (all instances concatenated)
RWStructuredBuffer<SkinnedVertexData> g_skinned_vertices  : register(u7, space0);

[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint vertex_id = dispatch_id.x;
    if (vertex_id >= g_constants.vertex_count)
        return;

    // ... load rest-pose data, compute skinning (same as current) ...

    // FIXED (Bug B1): per-instance position buffer always at local vertex_id
    g_skinned_positions[vertex_id] = skinned_position;

    // Flat vertex data at cumulative offset
    uint out_idx = g_constants.output_vertex_offset + vertex_id;
    SkinnedVertexData v;
    v.position = float4(skinned_position, 1.0f);
    v.normal   = float4(skinned_normal, 0.0f);
    v.tangent  = float4(skinned_tangent, 0.0f);
    v.texcoord = float4(texcoord, 0.0f, 0.0f);
    g_skinned_vertices[out_idx] = v;
}
```

- [ ] **Step 3: Delete `path_tracing_skin.comp.hlsl`**

```bash
rm engine/shader/hlsl/path_tracing_skin.comp.hlsl
```

- [ ] **Step 4: Update bytecode macros in `render_shader_bytecode.h`**

Replace all `PATH_TRACING_SKIN_COMP` → `GPU_SKINNING_COMP` and `path_tracing_skin_comp.h` → `gpu_skinning_comp.h`:

```cpp
// Vulkan:
#if __has_include(<gpu_skinning_comp.h>)
#include <gpu_skinning_comp.h>
#define PICCOLO_VULKAN_GPU_SKINNING_COMP GPU_SKINNING_COMP
#else
#define PICCOLO_VULKAN_GPU_SKINNING_COMP ::Piccolo::emptyVulkanShaderBytecode()
#endif

// D3D12:
#if __has_include(<dxil_cpp/gpu_skinning_comp.h>)
#include <dxil_cpp/gpu_skinning_comp.h>
#define PICCOLO_D3D12_GPU_SKINNING_COMP D3D12_GPU_SKINNING_COMP
#else
#define PICCOLO_D3D12_GPU_SKINNING_COMP ::Piccolo::emptyD3D12ShaderBytecode()
#endif
```

- [ ] **Step 5: Commit**

```
git add engine/shader/hlsl/gpu_skinning.hlsli \
        engine/shader/hlsl/gpu_skinning.comp.hlsl \
        engine/shader/hlsl/path_tracing_skin.comp.hlsl \
        engine/source/runtime/function/render/render_shader_bytecode.h
git commit -m "refactor: rename path_tracing_skin → gpu_skinning, remove path_tracing_common.hlsli dependency, fix B1 (per-instance position offset)"
```

---

### Task 2: Decouple C++ types — GpuSkinningPass owns its types

**Files:**
- Modify: `engine/source/runtime/function/render/passes/gpu_skinning_pass.h`
- Modify: `engine/source/runtime/function/render/passes/gpu_skinning_pass.cpp`
- Modify: `engine/source/runtime/function/render/render_gpu_resource.h`
- Modify: `engine/source/runtime/function/render/render_resource.h`

- [ ] **Step 1: Define `GpuSkinnedVertexGPUData` in skinning pass header** (or a neutral location)

Replace all `sizeof(RenderPathTracingVertexGPUData)` usage with `sizeof(GpuSkinnedVertexGPUData)`:

```cpp
// In gpu_skinning_pass.h:
struct GpuSkinnedVertexGPUData
{
    Vector4 position {0.0f, 0.0f, 0.0f, 1.0f};
    Vector4 normal   {0.0f, 1.0f, 0.0f, 0.0f};
    Vector4 tangent  {1.0f, 0.0f, 0.0f, 0.0f};
    Vector4 texcoord {0.0f, 0.0f, 0.0f, 0.0f};
};
static_assert(sizeof(GpuSkinnedVertexGPUData) == 64, "Must match SkinnedVertexData in HLSL");
```

- [ ] **Step 2: Define skinning-specific collection struct**

GpuSkinningPass should NOT use `RenderPathTracingCollectedInstance`. Define its own minimal struct:

```cpp
// In gpu_skinning_pass.h:
struct CollectedSkinnedMesh
{
    RenderMeshGPUResource* mesh {nullptr};
    uint32_t               instance_id {0};
    uint32_t               joint_count {0};
    const Matrix4x4*       joint_matrices {nullptr};
};
```

- [ ] **Step 3: Split `RenderMeshGPUResource` storage maps**

In `render_gpu_resource.h`, rename and split the maps:

```cpp
// NEW: Skinning output — written by GpuSkinningPass, read by any consumer
struct SkinnedMeshOutput
{
    RHIBuffer*       skinned_position_buffer {nullptr};
    RHIDeviceMemory* skinned_position_memory {nullptr};
    uint32_t         skinned_vertex_offset {0};  // offset into flat g_skinned_vertices
    uint32_t         vertex_count {0};
    uint32_t         index_count {0};
};
std::unordered_map<uint32_t, SkinnedMeshOutput> skinned_mesh_outputs;

// Existing: Path tracing BLAS — built by PathTracingPass from SkinnedMeshOutput
struct SkinnedPathTracingResources
{
    RHIAccelerationStructure* blas {nullptr};
};
std::unordered_map<uint32_t, SkinnedPathTracingResources> path_tracing_skinned_resources;
```

`SkinnedMeshOutput` stores the per-instance offset into the flat vertex buffer. This removes ordering coupling between the two passes' collection logic.

- [ ] **Step 4: Update GpuSkinningPass to use new types**

In `gpu_skinning_pass.cpp`:
- Replace `collectPathTracingInstances()` call with direct collection from `RenderScene::m_render_entities`
- Use `CollectedSkinnedMesh` instead of `RenderPathTracingCollectedInstance`
- Use `GpuSkinnedVertexGPUData` instead of `RenderPathTracingVertexGPUData`
- Store output in `mesh->skinned_mesh_outputs[inst_id]`
- Store `skinned_vertex_offset` (cumulative) in the output struct for each instance

```cpp
// dispatch(): collect skinned meshes directly from render entities
std::vector<CollectedSkinnedMesh> skinned_meshes;
auto render_scene = m_render_resource_impl->getCurrentRenderScene();
for (auto& entity : render_scene->getRenderEntities())  // need to expose this
{
    if (!entity.m_enable_vertex_blending) continue;
    if (entity.m_blend || entity.m_base_color_factor.w < 1.0f) continue; // skip transparent
    
    RenderMeshGPUResource* mesh = nullptr;
    try { mesh = &m_render_resource_impl->getEntityMesh(entity); }
    catch (...) { continue; }
    
    if (mesh == nullptr || mesh->mesh_vertex_count == 0) continue;
    
    CollectedSkinnedMesh csm;
    csm.mesh           = mesh;
    csm.instance_id    = entity.m_instance_id;
    csm.joint_count    = static_cast<uint32_t>(entity.m_joint_matrices.size());
    csm.joint_matrices = entity.m_joint_matrices.data();
    skinned_meshes.push_back(csm);
}
```

- [ ] **Step 5: Update `dispatchSkinCompute()` to store `skinned_vertex_offset`**

After computing per-instance offset, store it on `SkinnedMeshOutput`:

```cpp
// In dispatchSkinCompute():
uint32_t skinned_vertex_acc_offset = 0;
for (const auto& csm : skinned_meshes)
{
    // ... setup per-instance buffers ...
    
    // Store offset for consumers
    auto& output = mesh->skinned_mesh_outputs[csm.instance_id];
    output.skinned_vertex_offset = skinned_vertex_acc_offset;
    // ... dispatch compute ...
    skinned_vertex_acc_offset += vertex_count;
}
```

- [ ] **Step 6: Commit**

```
git add engine/source/runtime/function/render/passes/gpu_skinning_pass.h \
        engine/source/runtime/function/render/passes/gpu_skinning_pass.cpp \
        engine/source/runtime/function/render/render_gpu_resource.h \
        engine/source/runtime/function/render/render_resource.h
git commit -m "refactor: GpuSkinningPass owns its types — zero PathTracing dependency; split mesh output from BLAS storage"
```

---

### Task 3: Fix Bug B2 — vertex_offset lookup in PathTracingPass

**Files:**
- Modify: `engine/source/runtime/function/render/render_resource.cpp`
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`

- [ ] **Step 1: Fix `updatePathTracingSceneBuffers()`**

For skinned instances, look up `skinned_vertex_offset` from `mesh->skinned_mesh_outputs` instead of using `m_path_tracing_vertex_data.size()`. **Stop pushing placeholder vertices** for skinned instances.

```cpp
// In updatePathTracingSceneBuffers():
uint32_t static_vertex_offset = 0; // tracks offset within g_vertices (static only)

for (uint32_t instance_index = 0; ...)
{
    const bool is_skinned = source_instance.enable_vertex_blending;

    if (!is_skinned)
    {
        // --- Static mesh (existing logic, but using static_vertex_offset) ---
        auto geometry_it = geometry_indices.find(source_instance.mesh);
        if (geometry_it != geometry_indices.end())
        {
            geometry_index = geometry_it->second;
            // skip vertex/index append for dedup
        }
        else
        {
            geometry_index = static_cast<uint32_t>(m_path_tracing_geometry_data.size());
            geometry_indices[source_instance.mesh] = geometry_index;

            RenderPathTracingGeometryGPUData geometry_data{};
            geometry_data.vertex_offset = static_vertex_offset;  // offset into g_vertices
            geometry_data.index_offset  = static_cast<uint32_t>(m_path_tracing_index_data.size());
            geometry_data.index_count   = static_cast<uint32_t>(source_instance.mesh->path_tracing_indices.size());

            // Push real vertex data to g_vertices
            for (size_t v = 0; v < source_instance.mesh->path_tracing_positions.size(); ++v)
            {
                RenderPathTracingVertexGPUData vertex{};
                vertex.position  = Vector4(source_instance.mesh->path_tracing_positions[v], 1.0f);
                vertex.normal    = Vector4(source_instance.mesh->path_tracing_normals[v], 0.0f);
                vertex.tangent   = Vector4(source_instance.mesh->path_tracing_tangents[v], 0.0f);
                vertex.texcoord  = Vector4(source_instance.mesh->path_tracing_texcoords[v].x,
                                          source_instance.mesh->path_tracing_texcoords[v].y,
                                          0.0f, 0.0f);
                m_path_tracing_vertex_data.push_back(vertex);
            }

            // Append indices
            for (uint32_t idx : source_instance.mesh->path_tracing_indices)
                m_path_tracing_index_data.push_back(idx);

            m_path_tracing_geometry_data.push_back(geometry_data);
            static_vertex_offset += static_cast<uint32_t>(source_instance.mesh->path_tracing_positions.size());
        }
    }
    else
    {
        // --- Skinned mesh: lookup vertex_offset from GpuSkinningPass output ---
        geometry_index = static_cast<uint32_t>(m_path_tracing_geometry_data.size());

        uint32_t skinned_vertex_offset = 0;
        auto& outputs = source_instance.mesh->skinned_mesh_outputs;
        auto it = outputs.find(source_instance.instance_id);
        if (it != outputs.end())
        {
            skinned_vertex_offset = it->second.skinned_vertex_offset;
        }
        // If not found (GpuSkinningPass hasn't run yet), offset stays 0.
        // This is a soft dependency — path tracing will read vertex 0 data.

        RenderPathTracingGeometryGPUData geometry_data{};
        geometry_data.vertex_offset = skinned_vertex_offset;  // offset into g_skinned_vertices
        geometry_data.index_offset  = static_cast<uint32_t>(m_path_tracing_index_data.size());
        geometry_data.index_count   = static_cast<uint32_t>(source_instance.mesh->path_tracing_indices.size());

        // NO placeholder push to m_path_tracing_vertex_data

        // Append indices
        for (uint32_t idx : source_instance.mesh->path_tracing_indices)
            m_path_tracing_index_data.push_back(idx);

        m_path_tracing_geometry_data.push_back(geometry_data);
    }

    // ... material record, instance record (unchanged) ...
}
```

- [ ] **Step 2: Update `PathTracingPass::buildTopLevelAS()`**

Use `skinned_mesh_outputs` (skinning output) instead of `path_tracing_skinned_resources` for the position buffer:

```cpp
for (auto& instance : collected_instances)
{
    if (!instance.enable_vertex_blending || instance.mesh == nullptr) continue;

    auto& outputs = instance.mesh->skinned_mesh_outputs;
    auto it = outputs.find(instance.instance_id);
    if (it == outputs.end()) continue;

    auto& skinned_output = it->second;

    // Per-instance BLAS — stored in path_tracing_skinned_resources
    auto& pt_resources = instance.mesh->path_tracing_skinned_resources[instance.instance_id];

    if (pt_resources.blas != nullptr)
    {
        m_rhi->destroyAccelerationStructure(pt_resources.blas);
        pt_resources.blas = nullptr;
    }

    pt_resources.blas = m_render_resource_impl->buildPathTracingBLASFromSkinned(
        m_rhi, command_buffer,
        skinned_output.skinned_position_buffer,  // from skinning output
        skinned_output.vertex_count,
        sizeof(float) * 3,
        instance.mesh->mesh_index_buffer,
        skinned_output.index_count,
        instance.mesh->mesh_index_type);

    instance.bottom_level_as = pt_resources.blas;
}
```

- [ ] **Step 3: Commit**

```
git add engine/source/runtime/function/render/render_resource.cpp \
        engine/source/runtime/function/render/passes/path_tracing_pass.cpp
git commit -m "fix: Bug B2 — geometry vertex_offset for skinned instances uses skinned_mesh_outputs lookup instead of g_vertices offset"
```

---

### Task 4: Build verification

- [ ] **Step 1: Build**

```bash
cd d:/program/Piccolo/build
cmake --build . --config Debug --target PiccoloRuntime 2>&1 | tail -20
```

Expected: `PiccoloRuntime.lib` built successfully. No new errors.

- [ ] **Step 2: Resolve any compilation errors**

Common expected issues:
- DXC build needs to pick up new shader filename → check CMakeLists.txt for shader compile rules
- Missing `getRenderEntities()` accessor on `RenderScene` → add if needed

- [ ] **Step 3: Commit any final fixes**

---

## Appendix A: Type Equivalence

The skinning output type `GpuSkinnedVertexGPUData` has the same memory layout as `RenderPathTracingVertexGPUData`:

| Offset | Skinning (`GpuSkinnedVertexGPUData`) | Path Tracing (`RenderPathTracingVertexGPUData`) |
|--------|--------------------------------------|------------------------------------------------|
| 0      | `Vector4 position` (16 bytes)       | `Vector4 position` (16 bytes)                  |
| 16     | `Vector4 normal` (16 bytes)         | `Vector4 normal` (16 bytes)                    |
| 32     | `Vector4 tangent` (16 bytes)        | `Vector4 tangent` (16 bytes)                   |
| 48     | `Vector4 texcoord` (16 bytes)       | `Vector4 texcoord` (16 bytes)                  |

Both are 64 bytes. Path tracing can bind the skinning output buffer as `StructuredBuffer<PathTracingVertexData>` without conversion — it's the same bytes, different type name. This is intentional: the data format is universal, only the _ownership_ of the type definition changes.

## Appendix B: Instance Ordering Independence

The key design change for ordering: GpuSkinningPass stores `skinned_vertex_offset` per-instance in `SkinnedMeshOutput`. PathTracingPass looks it up by `instance_id`. This eliminates the need for both passes to iterate instances in the same order.

```
GpuSkinningPass:                    PathTracingPass:
  iterate m_render_entities →         iterate collected_instances →
    instance A (skinned) →              instance B (static) →
      offset = 0, store                  (uses g_vertices, offset from static counter)
    instance B (static) → skip         instance A (skinned) →
    instance C (skinned) →              lookup mesh_outputs[A.instance_id] →
      offset = vertex_count_A, store      vertex_offset = 0 ✓ (matches skinning order)
```

The lookup decouples the collection order between the two passes.
