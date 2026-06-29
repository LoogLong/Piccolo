# Fix GPU Skinning Pass Data-Flow and Buffer Ownership (v3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decouple `GpuSkinningPass` and `PathTracingPass` completely. `GpuSkinningPass` writes skinned vertex data to per-instance buffers on `RenderMeshGPUResource`. `PathTracingPass` reads them directly. No cross-pass function calls, no shared caches, no ordering dependencies — only buffer-level data exchange.

**Architecture:** `GpuSkinningPass` is a pure compute pass: collect animated meshes → upload joint matrices → dispatch compute per instance → write to per-instance skinned buffers in `RenderMeshGPUResource::path_tracing_skinned_resources`. It does NOT touch path-tracing-specific structures (`g_vertices`, descriptor sets, BLAS). `PathTracingPass` treats skinned instances as a separate data source: `updatePathTracingSceneBuffers()` includes only static meshes in the flat `g_vertices`; skinned instances get their vertex data from per-instance buffers via a new `g_skinned_vertices` binding in the path tracing shader. BLAS is built from per-instance skinned position buffers. The two passes have zero functional coupling — they share only the `RenderMeshGPUResource` buffer storage.

**Tech Stack:** C++17, D3D12, HLSL SM 6.6, Piccolo RHI

## Global Constraints

- Must not break static mesh path tracing
- Must not affect the rasterization pipeline
- `GpuSkinningPass` must NOT depend on path-tracing-specific data structures
- `PathTracingPass` must NOT call `GpuSkinningPass` methods
- Passes communicate exclusively through `RenderMeshGPUResource` buffers

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        RenderMeshGPUResource                     │
│  path_tracing_skinned_resources[instance_id]                    │
│    ├── .skinned_position_buffer   (float3, BLAS geometry src)   │
│    ├── .blas                      (RHIAccelerationStructure*)    │
│    └── .vertex_count / .index_count                             │
└──────┬──────────────────────────────────────┬───────────────────┘
       │ write                                  │ read
┌──────┴──────────┐                   ┌────────┴──────────────────┐
│ GpuSkinningPass │                   │    PathTracingPass         │
│                 │                   │                            │
│ dispatch():     │                   │ buildTopLevelAS():         │
│  1. collect     │                   │  1. collect (static only)  │
│  2. uploadJoints│                   │  2. static BLAS ensure     │
│  3. per-instance│                   │  3. per-instance BLAS      │
│     compute     │                   │     (from skinned buffers) │
│     dispatch    │                   │  4. TLAS rebuild → trace   │
│  4. UAV barrier │                   │                            │
│                 │                   │ PathTracingClosestHit():   │
│                 │                   │  if (flags & 1)            │
│                 │                   │    read g_skinned_vertices │
│                 │                   │  else                      │
│                 │                   │    read g_vertices         │
└─────────────────┘                   └────────────────────────────┘
```

### Key design properties

1. **Zero coupling.** No function calls between passes. `GpuSkinningPass` never touches path-tracing types.
2. **Buffer-based contract.** The contract is the `RenderMeshGPUResource::SkinnedPathTracingResources` struct. Any consumer can read from these buffers.
3. **Static/skinned split.** `updatePathTracingSceneBuffers()` only handles static meshes. Skinned instances are excluded from the flat `g_vertices` buffer.
4. **Per-instance vertex data.** Each skinned instance gets its own vertex buffer (written by compute) and position buffer (for BLAS). No per-instance position buffer copies needed — compute writes directly to the BLAS source.

---

## File Map

| File | Responsibility | Modified? |
|------|---------------|-----------|
| `engine/source/runtime/function/render/passes/gpu_skinning_pass.h` | Remove flat position buffer; add `m_skin_constants_buffer`; `dispatch()` is self-contained | ✅ Modify |
| `engine/source/runtime/function/render/passes/gpu_skinning_pass.cpp` | `dispatch()`: collect → upload → compute per instance → barrier; per-instance buffer creation + write; fix constant buffer, bindings, stride | ✅ Modify |
| `engine/source/runtime/function/render/passes/path_tracing_pass.h` | Add `g_skinned_vertices` descriptor binding at t14 | ✅ Modify |
| `engine/source/runtime/function/render/passes/path_tracing_pass.cpp` | `updateDescriptorSet()`: bind `g_skinned_vertices` at new slot; `buildTopLevelAS()`: per-instance BLAS from skinned buffers, skinned TLAS dirty; `updatePathTracingSceneBuffers()` unchanged (handles static only) | ✅ Modify |
| `engine/source/runtime/function/render/render_resource.cpp` | `updatePathTracingSceneBuffers()`: skip skinned instances | ✅ Modify |
| `engine/source/runtime/function/render/render_resource.h` | Remove `m_cached_path_tracing_instances` and accessors (no longer needed) | ✅ Modify |
| `engine/source/runtime/function/render/render_gpu_resource.h` | Update `SkinnedPathTracingResources` (only skinned_position_buffer is per-instance) | ✅ Modify |
| `engine/source/runtime/function/render/render_pipeline.cpp` | No change (GpuSkinningPass dispatch at frame start, passes are independent) | No change |
| `engine/shader/hlsl/path_tracing_skin.comp.hlsl` | Fix register bindings to unique sequential 0–7 | ✅ Modify |
| `engine/shader/hlsl/path_tracing_common.hlsli` | No change (`flags` bit 0 already set for skinned) | No change |
| `engine/shader/hlsl/path_tracing.lib.hlsl` | Add `g_skinned_vertices` binding; conditional read in ClosestHit | ✅ Modify |

---

### Task 1: Fix compute shader and C++ compute pipeline binding conflicts

**Files:**
- Modify: `engine/shader/hlsl/path_tracing_skin.comp.hlsl`
- Modify: `engine/source/runtime/function/render/passes/gpu_skinning_pass.cpp`
- Modify: `engine/source/runtime/function/render/passes/gpu_skinning_pass.h`

- [ ] **Step 1: Fix HLSL register bindings to unique sequential 0–7**

In `engine/shader/hlsl/path_tracing_skin.comp.hlsl`, change:

```hlsl
// BEFORE (conflicts: b0 collides with t0 namespace, u0 collides with t0):
ConstantBuffer<SkinComputeConstants> g_constants : register(b0, space0);
RWStructuredBuffer<float3>          g_skinned_positions : register(u0, space0);
RWStructuredBuffer<PathTracingVertexData> g_skinned_vertices : register(u1, space0);

// AFTER (unique sequential):
ConstantBuffer<SkinComputeConstants> g_constants : register(b5, space0);
RWStructuredBuffer<float3>          g_skinned_positions : register(u6, space0);
RWStructuredBuffer<PathTracingVertexData> g_skinned_vertices : register(u7, space0);
```

Full binding layout after fix: `t0=0, t1=1, t2=2, t3=3, t4=4, b5=5, u6=6, u7=7`.

- [ ] **Step 2: Fix descriptor set layout bindings in `setupSkinComputePipeline()`**

In `engine/source/runtime/function/render/passes/gpu_skinning_pass.cpp`, in `GpuSkinningPass::setupSkinComputePipeline()`, change:

```cpp
// OLD:
bindings[5].binding = 0;  // b0
bindings[6].binding = 0;  // u0
bindings[7].binding = 1;  // u1

// NEW:
bindings[5].binding = 5;  // b5
bindings[6].binding = 6;  // u6
bindings[7].binding = 7;  // u7
```

- [ ] **Step 3: Fix descriptor writes in `dispatchSkinCompute()`**

In the per-instance dispatch loop, fix `dstBinding` values:

```cpp
// OLD:
writes[5].dstBinding = 0;  // b0
writes[6].dstBinding = 0;  // u0
writes[7].dstBinding = 1;  // u1

// NEW:
writes[5].dstBinding = 5;  // b5
writes[6].dstBinding = 6;  // u6
writes[7].dstBinding = 7;  // u7
```

- [ ] **Step 4: Fix joint matrix stride in `dispatchSkinCompute()` and `uploadJointMatrices()`**

In `dispatchSkinCompute()`:
```cpp
// OLD: joint_matrix_offset += inst.joint_count;
// NEW:
joint_matrix_offset += s_mesh_vertex_blending_max_joint_count;  // 1024
```

In `uploadJointMatrices()`, fix the packing stride to leave 1024 slots per instance:
```cpp
size_t offset = 0;
for (const auto& inst : instances)
{
    if (inst.enable_vertex_blending && inst.joint_count > 0 && inst.joint_matrices != nullptr)
    {
        size_t bytes = inst.joint_count * sizeof(Matrix4x4);
        std::memcpy(static_cast<uint8_t*>(mapped) + offset, inst.joint_matrices, bytes);
        offset += s_mesh_vertex_blending_max_joint_count * sizeof(Matrix4x4);
    }
}
```

- [ ] **Step 5: Fix constant buffer use-after-free — use persistent buffer**

Add `m_skin_constants_buffer` + `m_skin_constants_memory` members to `GpuSkinningPass` header. Allocate in `setup()`:

```cpp
m_rhi->createBuffer(
    16, RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    m_skin_constants_buffer, m_skin_constants_memory);
```

In `dispatchSkinCompute()`, replace the `createBuffer(16)→use→destroyBuffer` pattern with a map/write/unmap of the persistent buffer:

```cpp
void* mapped_cb = nullptr;
m_rhi->mapMemory(m_skin_constants_memory, 0, 16, 0, &mapped_cb);
std::memcpy(mapped_cb, &constants, sizeof(constants));
m_rhi->unmapMemory(m_skin_constants_memory);

RHIDescriptorBufferInfo constants_info {};
constants_info.buffer = m_skin_constants_buffer;  // persistent, not local
```

Remove the `destroyBuffer(constants_buffer); m_rhi->freeMemory(constants_memory);` lines.

- [ ] **Step 6: Commit**

```bash
git add engine/shader/hlsl/path_tracing_skin.comp.hlsl \
        engine/source/runtime/function/render/passes/gpu_skinning_pass.h \
        engine/source/runtime/function/render/passes/gpu_skinning_pass.cpp
git commit -m "fix: GPU skin compute — binding conflicts, constant UAF, joint stride"
```

---

### Task 2: Make `GpuSkinningPass` self-contained — write per-instance skinned buffers

**Files:**
- Modify: `engine/source/runtime/function/render/passes/gpu_skinning_pass.h`
- Modify: `engine/source/runtime/function/render/passes/gpu_skinning_pass.cpp`
- Modify: `engine/source/runtime/function/render/render_gpu_resource.h`

- [ ] **Step 1: Update `SkinnedPathTracingResources` (only skinned_position_buffer is per-instance)**

In `engine/source/runtime/function/render/render_gpu_resource.h`, add to the nested struct:

```cpp
struct SkinnedPathTracingResources
{
    RHIAccelerationStructure* blas {nullptr};
    RHIBuffer*  skinned_position_buffer {nullptr};     // float3 positions, for BLAS (u6)
    RHIDeviceMemory* skinned_position_memory {nullptr};
    // Skinned vertex data (PathTracingVertexData) for path tracing lives in a FLAT
    // buffer (m_skinned_vertex_output_buffer) owned by GpuSkinningPass, exposed via
    // RenderResource. Per-instance data is at computed offsets within that buffer.
    // Only skinned_position_buffer is per-instance (needed for BLAS geometry).
    uint32_t    vertex_count {0};
    uint32_t    index_count {0};
};
```

- [ ] **Step 2: Remove flat `m_skinned_position_output_buffer` from `GpuSkinningPass`**

In `engine/source/runtime/function/render/passes/gpu_skinning_pass.h`, delete:
```cpp
RHIBuffer*       m_skinned_position_output_buffer {nullptr};
RHIDeviceMemory* m_skinned_position_output_memory {nullptr};
size_t           m_skinned_position_output_capacity {0};
```

Also delete `ensureSkinBuffers()` declaration.

- [ ] **Step 3: Rewrite `dispatchSkinCompute()` to write to per-instance buffers**

For each skinned instance, the compute dispatch binds:

- **u6** → per-instance `resources.skinned_position_buffer` at offset 0 (BLAS geometry source)
- **u7** → flat `m_skinned_vertex_output_buffer` at the instance's computed offset (PathTracingVertexData for path tracing)

```cpp
uint32_t skinned_vertex_acc_offset = 0; // tracks cumulative offset across instances

for (const auto& inst : instances)
{
    // ...
    uint32_t vertex_count = mesh->mesh_vertex_count;

    // u6: skinned positions → per-instance buffer, offset 0
    RHIDescriptorBufferInfo skinned_positions_info {};
    skinned_positions_info.buffer = resources.skinned_position_buffer;
    skinned_positions_info.offset = 0;
    skinned_positions_info.range  = RHI_WHOLE_SIZE;
    writes[6].pBufferInfo = &skinned_positions_info;

    // u7: skinned vertex data → FLAT buffer at per-instance offset
    RHIDescriptorBufferInfo skinned_vertices_info {};
    skinned_vertices_info.buffer = m_skinned_vertex_output_buffer;
    skinned_vertices_info.offset = skinned_vertex_acc_offset * sizeof(RenderPathTracingVertexGPUData);
    skinned_vertices_info.range  = RHI_WHOLE_SIZE;
    writes[7].pBufferInfo = &skinned_vertices_info;

    // ... dispatch ...

    skinned_vertex_acc_offset += vertex_count;
}

The per-instance buffers are allocated in `dispatchSkinCompute()` if not present or if vertex count changed:

```cpp
// Create per-instance position buffer (for BLAS)
if (resources.skinned_position_buffer == nullptr || resources.vertex_count != vertex_count)
{
    if (resources.skinned_position_buffer != nullptr)
        m_rhi->destroyBuffer(resources.skinned_position_buffer);
    if (resources.skinned_position_memory != nullptr)
        m_rhi->freeMemory(resources.skinned_position_memory);

    m_rhi->createBuffer(
        vertex_count * sizeof(float) * 3,
        RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            RHI_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            RHI_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        resources.skinned_position_buffer,
        resources.skinned_position_memory);

- [ ] **Step 4: Make `GpuSkinningPass::dispatch()` self-contained**

The `dispatch()` function collects instances, uploads joint matrices, and dispatches compute. It does NOT call `updatePathTracingSceneBuffers()`, does NOT touch `m_render_resource_impl->getPathTracingVertexBuffer()`, and does NOT cache anything on `RenderResource`.

```cpp
bool GpuSkinningPass::dispatch()
{
    if (m_rhi == nullptr || m_render_resource_impl == nullptr) return false;
    if (m_skin_compute_pipeline == nullptr)
    {
        if (!setup()) return false;
        if (m_skin_compute_pipeline == nullptr) return false;
    }

    auto render_scene = m_render_resource_impl->getCurrentRenderScene();
    if (render_scene == nullptr) return false;

    auto collected_instances = m_render_resource_impl->collectPathTracingInstances(*render_scene);

    const bool has_skinned = std::any_of(collected_instances.begin(), collected_instances.end(),
        [](const RenderPathTracingCollectedInstance& i) { return i.enable_vertex_blending; });

    if (!has_skinned) return true;

    if (!uploadJointMatrices(collected_instances))
    {
        LOG_WARN("GpuSkinningPass: failed to upload joint matrices");
        return false;
    }

    RHICommandBuffer* command_buffer = m_rhi->getCurrentCommandBuffer();
    if (command_buffer == nullptr) return false;

    dispatchSkinCompute(command_buffer, collected_instances);
    return true;
}
```

- [ ] **Step 5: Commit**

```bash
git add engine/source/runtime/function/render/passes/gpu_skinning_pass.h \
        engine/source/runtime/function/render/passes/gpu_skinning_pass.cpp \
        engine/source/runtime/function/render/render_gpu_resource.h
git commit -m "feat: GpuSkinningPass writes to per-instance skinned buffers, zero path-tracing coupling"
```

---

### Task 3: Update `PathTracingPass` to read skinned data from per-instance buffers

**Files:**
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.h`
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`
- Modify: `engine/source/runtime/function/render/render_resource.cpp`
- Modify: `engine/source/runtime/function/render/render_resource.h`
- Modify: `engine/shader/hlsl/path_tracing.lib.hlsl`

- [ ] **Step 1: Remove `RenderResource` instance cache (no longer needed)**

In `engine/source/runtime/function/render/render_resource.h`, remove:
```cpp
void setCachedPathTracingInstances(std::vector<RenderPathTracingCollectedInstance> instances) { ... }
std::vector<RenderPathTracingCollectedInstance> takeCachedPathTracingInstances() { ... }
std::vector<RenderPathTracingCollectedInstance> m_cached_path_tracing_instances;
```

`PathTracingPass::buildTopLevelAS()` calls `collectPathTracingInstances()` directly (as it always did before the extraction).

- [ ] **Step 2: Skip skinned instances in `updatePathTracingSceneBuffers()`**

In `engine/source/runtime/function/render/render_resource.cpp`, in `updatePathTracingSceneBuffers()`, skip skinned instances entirely. They are not part of the flat `g_vertices` buffer.

```cpp
for (uint32_t instance_index = 0; instance_index < collected_instances.size(); ++instance_index)
{
    const RenderPathTracingCollectedInstance& source_instance = collected_instances[instance_index];

    // Skinned instances: geometry/material/instance records and index data
    // are created normally. Only vertex data push-back is skipped — the flat
    // g_skinned_vertices buffer (populated by GpuSkinningPass via compute)
    // provides skinned vertex data for these instances.
    const bool is_skinned = source_instance.enable_vertex_blending;
    // (vertex push-back skipped for is_skinned; all other records created)
    if (!is_skinned)
    {
        // ... existing vertex push-back code for static meshes ...
    }
    // ... existing code for static meshes ...
}
```

- [ ] **Step 3: Add `g_skinned_vertices` binding to path tracing shader**

In `engine/shader/hlsl/path_tracing.lib.hlsl`, add a new binding at `t13` (the next available register in the current layout: t0-t12 are already used, t13 is available before g_accumulation_prev at t1035):

```hlsl
// Per-instance skinned vertex data — indexed by [geometry_data.vertex_offset + vertex_index]
StructuredBuffer<PathTracingVertexData> g_skinned_vertices : register(t14, space0);
```

In `PathTracingClosestHit()`, after reading `instance_data`, branch on `flags & 1`:

```hlsl
const PathTracingInstanceData instance_data = g_instances[instance_index];
const PathTracingGeometryData geometry_data = g_geometries[instance_data.geometry_index];
const PathTracingMaterialData material_data = g_materials[instance_data.material_index];

const uint primitive_index = PrimitiveIndex();
const uint index_base  = geometry_data.index_offset + primitive_index * 3u;
uint local_i0 = g_indices[index_base + 0u];
uint local_i1 = g_indices[index_base + 1u];
uint local_i2 = g_indices[index_base + 2u];

PathTracingVertexData v0, v1, v2;

if (instance_data.flags & 1u) // enable_vertex_blending
{
    // Skinned: read from per-instance buffer
    v0 = g_skinned_vertices[geometry_data.vertex_offset + local_i0];
    v1 = g_skinned_vertices[geometry_data.vertex_offset + local_i1];
    v2 = g_skinned_vertices[geometry_data.vertex_offset + local_i2];
}
else
{
    // Static: read from flat g_vertices buffer (existing behavior)
    const uint i0 = local_i0 + geometry_data.vertex_offset;
    const uint i1 = local_i1 + geometry_data.vertex_offset;
    const uint i2 = local_i2 + geometry_data.vertex_offset;
    v0 = g_vertices[i0];
    v1 = g_vertices[i1];
    v2 = g_vertices[i2];
}
```

Note: The index buffer for skinned meshes still comes from `g_indices`. The `vertex_offset` in `geometry_data` for skinned instances is the per-instance vertex buffer offset (since each skinned instance has its own geometry). The compute shader writes `g_skinned_vertices` starting at offset 0 per instance, so the per-instance geometry's `vertex_offset` is used as a logical index within the per-instance buffer.

- [ ] **Step 4: Add `g_skinned_vertices` to descriptor set layout and `updateDescriptorSet()`**

Add a new descriptor set layout binding at slot **13** (the next available binding):

In `setupDescriptorSetLayout()`, add binding 13 (shifting existing binding 13 for `g_accumulation_prev` to 1035 as before — or add a 15th binding):

```cpp
// Binding 13: per-instance skinned vertex data
bindings[13].binding         = 13;
bindings[13].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
bindings[13].descriptorCount = 1;
bindings[13].stageFlags      = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
```

In `updateDescriptorSet()`, add `writes[13]` for the skinned vertex buffer. The actual buffer is a flat GPU buffer containing all per-instance skinned vertex data concatenated. It is populated by `GpuSkinningPass` via compute dispatch.

However, there's a problem: `g_skinned_vertices` requires a single flat buffer binding, but `GpuSkinningPass` writes to **per-instance** buffers. Two options:

**Option A:** Add a `cmdCopyBuffer` pass after compute that concatenates all per-instance `skinned_vertex_buffer`s into a single flat `g_skinned_vertices` buffer. This is a copy operation, but it's on GPU and the data sizes are modest.

**Option B:** Use a single flat buffer for skinned vertex data (similar to the current `g_vertices` pattern). `GpuSkinningPass` writes to per-instance offsets within this flat buffer.

**Choose Option B** — simpler, no extra copy. The flat `g_skinned_vertices` buffer is treated like `g_vertices` but only contains skinned instances. The buffer is created by `GpuSkinningPass` as a member (`m_skinned_vertex_output_buffer`) and written at per-instance offsets by the compute shader. `PathTracingPass` binds it at descriptor slot 13.

In `GpuSkinningPass::dispatchSkinCompute()`, u7 binds the flat output buffer at the instance's offset:
```cpp
RHIDescriptorBufferInfo skinned_vertices_info {};
skinned_vertices_info.buffer = m_skinned_vertex_output_buffer;
skinned_vertices_info.offset = skinned_vertex_offset * sizeof(RenderPathTracingVertexGPUData);
skinned_vertices_info.range  = RHI_WHOLE_SIZE;
writes[7].pBufferInfo = &skinned_vertices_info;
```

`PathTracingPass` binds `m_skinned_vertex_output_buffer` at descriptor slot 13 via a getter on `RenderResource` (or `GpuSkinningPass` exposes it).

Since passes are independent, `GpuSkinningPass` stores this buffer handle on `RenderResource`:
```cpp
// In render_resource.h:
RHIBuffer* getSkinnedVertexBuffer() const { return m_skinned_vertex_buffer; }
void setSkinnedVertexBuffer(RHIBuffer* buf) { m_skinned_vertex_buffer = buf; }
RHIBuffer* m_skinned_vertex_buffer {nullptr};
```

`GpuSkinningPass` sets it during first `dispatch()`. `PathTracingPass` reads it during `updateDescriptorSet()`.

- [ ] **Step 5: Update `PathTracingPass::buildTopLevelAS()`**

Per-instance BLAS construction stays in `PathTracingPass` (BLAS is ray-tracing-specific). It builds BLAS from `resources.skinned_position_buffer` (already populated by `GpuSkinningPass`). Since the two passes are independent, `buildTopLevelAS()` should not assume `GpuSkinningPass` already ran — it checks if the buffer exists and skips if not ready.

```cpp
// Per-instance BLAS for skinned instances
for (RenderPathTracingCollectedInstance& instance : collected_instances)
{
    if (!instance.enable_vertex_blending || instance.mesh == nullptr)
    {
        continue;
    }

    RenderMeshGPUResource* mesh = instance.mesh;
    uint32_t inst_id = instance.instance_id;
    auto it = mesh->path_tracing_skinned_resources.find(inst_id);
    if (it == mesh->path_tracing_skinned_resources.end())
    {
        continue; // GpuSkinningPass hasn't created this yet
    }
    auto& resources = it->second;

    // Destroy old BLAS before building new one
    if (resources.blas != nullptr)
    {
        m_rhi->destroyAccelerationStructure(resources.blas);
        resources.blas = nullptr;
    }

    resources.blas = m_render_resource_impl->buildPathTracingBLASFromSkinned(
        m_rhi, command_buffer,
        resources.skinned_position_buffer, // written by GpuSkinningPass
        mesh->mesh_vertex_count,
        sizeof(float) * 3,
        mesh->mesh_index_buffer,
        mesh->mesh_index_count,
        mesh->mesh_index_type);

    instance.bottom_level_as = resources.blas;
    if (resources.blas != nullptr)
    {
        ++m_last_blas_build_count;
    }
}
```

TLAS dirty logic: always rebuild if any skinned instance exists (their BLAS changes every frame):
```cpp
const bool has_skinned = std::any_of(collected_instances.begin(), collected_instances.end(),
    [](const RenderPathTracingCollectedInstance& i) { return i.enable_vertex_blending; });

const bool tlas_dirty =
    has_skinned ||
    scene.isPathTracingTLASDirty() ||
    m_top_level_as == nullptr ||
    m_tlas_instance_count != collected_instances.size();
```

- [ ] **Step 6: Commit**

```bash
git add engine/source/runtime/function/render/passes/path_tracing_pass.h \
        engine/source/runtime/function/render/passes/path_tracing_pass.cpp \
        engine/source/runtime/function/render/render_resource.cpp \
        engine/source/runtime/function/render/render_resource.h \
        engine/shader/hlsl/path_tracing.lib.hlsl
git commit -m "feat: PathTracingPass reads skinned data from per-instance buffers via g_skinned_vertices"
```

---

### Task 4: Build verification

- [ ] **Step 1: Build**

```bash
cd d:/program/Piccolo/build
cmake --build . --config Debug --target PiccoloRuntime 2>&1 | tail -5
```

Expected: `PiccoloRuntime.lib` built successfully. No new errors.

- [ ] **Step 2: Commit any final fixes**

---

## Review Issues Fixed

References: [extraction review](docs/superpowers/plans/2026-06-19-extract-gpu-skinning-to-standalone-pass-review.md), [fix-plan review v1](docs/superpowers/plans/2026-06-19-fix-gpu-skinning-dataflow-review.md), [fix-plan review v2](docs/superpowers/plans/2026-06-19-fix-gpu-skinning-dataflow-review-v2.md).

| # | Source | Severity | Issue | Fix |
|---|--------|----------|-------|-----|
| 1 | Ext | 🔴 | `vertex_offset_in_flat_buffer` lost across passes | Eliminated — passes have zero functional coupling; each pass collects its own instances |
| 2 | Ext | 🔴 | Compute output overwritten by CPU | Eliminated — compute writes to per-instance buffers; CPU buffer update only touches static meshes |
| 3 | Ext | 🔴 | Flat buffer → per-instance BLAS gap | Compute writes directly to `resources.skinned_position_buffer` at u6; BLAS reads the same buffer |
| 4 | Fix#1 | 🔴 | Constant buffer UAF | Persistent `m_skin_constants_buffer` allocated once, mapped per dispatch |
| 5 | Fix#2 | 🔴 | Descriptor binding conflicts | HLSL registers + layout + writes all use unique sequential 0–7 |
| 6 | Fix#3 | 🔴 | Joint matrix stride | `s_mesh_vertex_blending_max_joint_count` (1024) in upload and dispatch |
| 7 | — | — | GpuSkinningPass coupled to path tracing | Removed — `dispatch()` only touches per-instance buffers on `RenderMeshGPUResource` |
| 8 | — | — | Skinned vertices mixed with static in g_vertices | Split — static in `g_vertices` (t4), skinned in `g_skinned_vertices` (t13) |
