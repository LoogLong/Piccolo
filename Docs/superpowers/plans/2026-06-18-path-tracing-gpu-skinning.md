# GPU Skinning for Path Tracing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make skinned meshes animate correctly in the path tracing pipeline using GPU compute-shader-based vertex skinning, with per-frame BLAS/TLAS rebuild for animated geometry.

**Architecture:** A compute shader pre-skins vertices on GPU each frame before BLAS construction. The compute shader reads rest-pose vertex data directly from existing `RenderMeshGPUResource` buffers (position, normal+tg, texcoord, joint bindings) and per-instance joint matrices from a new upload buffer. It writes skinned positions to a per-instance BLAS source buffer and skinned full vertex data directly into the flat `g_vertices` buffer at the correct per-instance offset. For skinned meshes, BLAS is built per-instance (not per-mesh) from the skinned position buffer, and TLAS is always rebuilt because animated geometry changes every frame.

**Tech Stack:** C++17, D3D12, HLSL SM 6.6, Piccolo RHI

## Global Constraints

- Must work within the existing RHI abstraction (no direct D3D12 calls in new code)
- Must not break static mesh path tracing (identical output for non-skinned scenes)
- Must not affect the rasterization pipeline (skinned mesh vertex buffers used by raster are untouched)
- Compute shader writes to DEDICATED output buffers; does NOT overwrite raster pipeline vertex buffers
- TLAS rebuild for animated scenes is acceptable (this is inherent to animated ray tracing)

---

## Root Cause Analysis

### Why skinned meshes appear static (bind pose) in path tracing

1. **Vertex data is bind-pose only**: `RenderMeshGPUResource::path_tracing_positions/normals/tangents` are populated once at mesh load from static mesh data. They are never updated when animation runs.

2. **No skinning in the path tracing shader**: `path_tracing.lib.hlsl` reads `g_vertices[i].position` directly and transforms with `ObjectToWorld3x4()` only. There is no joint matrix buffer, no joint binding buffer, and no skinning computation.

3. **BLAS is built from bind-pose vertices**: `ensurePathTracingBLAS()` uses `mesh_vertex_position_buffer` which contains rest-pose positions.

4. **BLAS is shared per-mesh, not per-instance**: Multiple instances of the same skinned mesh share one BLAS via `mesh->path_tracing_bottom_level_as`. But with animation, each instance has different joint matrices → different vertex positions → needs a different BLAS.

### Why the raster pipeline works but path tracing doesn't

| Aspect | Raster Pipeline | Path Tracing |
|--------|----------------|-------------|
| Skinning | Vertex shader applies `LoadSkinningMatrix()` for each vertex | No skinning code exists |
| Joint matrices | `g_joint_matrices` at t2, space0 (dynamic storage buffer) | Not bound |
| Joint bindings | `g_joint_bindings` at t0, space1 (storage buffer) | Not bound |
| BLAS | N/A (no ray tracing) | Built from bind-pose buffer |

### Design decision: Pre-skin via compute shader (not inline in hit shader)

**Why not inline skinning (skin in closest-hit shader)?**
- The BLAS would still represent bind-pose geometry
- Rays would intersect bind-pose triangles, producing incorrect hit points for animated meshes
- Characters in T-pose would cast shadows and occlude differently from their animated form

**Why compute pre-skin?**
- BLAS is built from GPU-skinned positions → ray intersections are correct
- Path tracing shader reads already-skinned vertex data → no shader changes needed for vertex access
- Compute runs once per frame per skinned mesh (not per ray hit)
- No CPU readback: everything stays on GPU

---

## File Map

| File | Responsibility | Modified? |
|------|---------------|-----------|
| `engine/shader/hlsl/path_tracing_skin.comp.hlsl` | Compute shader: skin vertices, write to output buffers | ✅ Create |
| `engine/shader/hlsl/path_tracing_common.hlsli` | Add `enable_vertex_blending` to `PathTracingInstanceData` | ✅ Modify |
| `engine/source/runtime/function/render/render_resource.h` | New structs: `SkinnedPathTracingResources`, modify `RenderMeshGPUResource` | ✅ Modify |
| `engine/source/runtime/function/render/render_resource.cpp` | `ensurePathTracingBLAS()` per-instance, `updatePathTracingSceneBuffers()` per-instance geometry, compute shader resource creation | ✅ Modify |
| `engine/source/runtime/function/render/passes/path_tracing_pass.h` | New members: joint matrix buffer, compute pipeline, compute descriptor set | ✅ Modify |
| `engine/source/runtime/function/render/passes/path_tracing_pass.cpp` | `buildTopLevelAS()`: compute dispatch, per-instance BLAS, TLAS dirty for skinned | ✅ Modify |
| `engine/source/runtime/function/render/render_scene.cpp` | `rebuildPathTracingInstances()`: collect joint matrix data, set TLAS dirty for animated | ✅ Modify |

---

### Task 1: Extend data structures for per-instance skinned mesh support

**Files:**
- Modify: `engine/source/runtime/function/render/render_resource.h`
- Modify: `engine/source/runtime/function/render/render_scene.cpp`
- Modify: `engine/shader/hlsl/path_tracing_common.hlsli`

**Interfaces:**
- Produces: `RenderPathTracingCollectedInstance::enable_vertex_blending`, `::joint_matrices`, `::joint_count`
- Produces: `RenderPathTracingCollectedInstance::vertex_offset_in_flat_buffer`
- Produces: `PathTracingInstanceGPUData::flags` bit 0 as `enable_vertex_blending` flag (HLSL + C++)
- Produces: `RenderMeshGPUResource::path_tracing_skinned_resources` (persistent per-instance BLAS + skinned position buffer, keyed by `instance_id`)

- [ ] **Step 1: Add skinning fields to `RenderPathTracingCollectedInstance` (transient per-frame data only)**

In `engine/source/runtime/function/render/render_resource.h`, find the struct `RenderPathTracingCollectedInstance` (around line 24). Add new fields:

```cpp
struct RenderPathTracingCollectedInstance
{
    RHIAccelerationStructure*       bottom_level_as {nullptr};
    std::array<float, 12>           row_major_3x4_transform {};
    uint32_t                        instance_id {0};
    uint32_t                        material_index {0};
    RenderMeshGPUResource*          mesh {nullptr};
    RenderPBRMaterialGPUResource*   material {nullptr};
    RenderEntity*                   entity {nullptr};
    uint32_t                        shader_instance_index {0};

    // --- NEW: skinning support ---
    bool                            enable_vertex_blending {false};
    uint32_t                        joint_count {0};
    const Matrix4x4*                joint_matrices {nullptr};  // pointer to entity's joint matrices (CPU-side, uploaded per frame)
    // Per-instance BLAS & buffers are stored persistently in RenderMeshGPUResource::path_tracing_skinned_resources
    // (keyed by instance_id). RenderPathTracingCollectedInstance is temporary per-frame — storing
    // GPU resources here would cause destroy+recreate every frame.
    uint32_t                        vertex_offset_in_flat_buffer {0};   // where this instance's vertices are in g_vertices
};
```

- [ ] **Step 2: Add per-instance skinned resources to `RenderMeshGPUResource`**

In `engine/source/runtime/function/render/render_gpu_resource.h`, modify the `RenderMeshGPUResource` struct. Add a nested struct for per-instance skinned resources:

```cpp
struct RenderMeshGPUResource
{
    // ... existing fields (enable_vertex_blending, mesh_vertex_position_buffer, etc.) ...

    // NEW: Per-instance skinned output buffers managed by PathTracingPass
    // Each skinned instance needs its own BLAS and position buffer.
    struct SkinnedPathTracingResources
    {
        RHIAccelerationStructure* blas {nullptr};
        RHIBuffer*  skinned_position_buffer {nullptr};
        RHIDeviceMemory* skinned_position_memory {nullptr};
        uint32_t    vertex_count {0};
        uint32_t    index_count {0};
    };
    // pool of per-instance resources (indexed by instance id)
    std::unordered_map<uint32_t, SkinnedPathTracingResources> path_tracing_skinned_resources;
};
```

- [ ] **Step 3: Add `enable_vertex_blending` to HLSL `PathTracingInstanceData`**

In `engine/shader/hlsl/path_tracing_common.hlsli`, modify the struct:

```hlsl
struct PathTracingInstanceData
{
    uint geometry_index;
    uint material_index;
    uint entity_instance_id;
    uint flags;                      // bit 0: enable_vertex_blending
};
```

The C++ side in `render_resource.h` should also be updated:

```cpp
struct RenderPathTracingInstanceGPUData
{
    uint32_t geometry_index {0};
    uint32_t material_index {0};
    uint32_t entity_instance_id {0};
    uint32_t flags {0};  // bit 0: enable_vertex_blending
};
```

- [ ] **Step 4: Populate skinning data in `rebuildPathTracingInstances()`**

In `engine/source/runtime/function/render/render_scene.cpp`, in `rebuildPathTracingInstances()` (around line 180), add skinning data collection for each entity:

```cpp
for (RenderEntity& entity : m_render_entities)
{
    // ... existing transparency/missing checks ...

    RenderPathTracingInstance instance;
    // ... existing fields ...
    instance.mesh     = mesh;
    instance.material = material;
    // ... existing fields ...
    m_path_tracing_instances.push_back(instance);

    // ... existing BLAS dirty check ...
}
```

Also add the `enable_vertex_blending` field to `RenderPathTracingInstance`:

In `engine/source/runtime/function/render/render_scene.h`, find `struct RenderPathTracingInstance` and add:
```cpp
struct RenderPathTracingInstance
{
    // ... existing fields ...
    bool        enable_vertex_blending {false};
    uint32_t    joint_count {0};
    // Note: joint matrices are read from RenderEntity at collect time, not stored here
};
```

- [ ] **Step 5: Populate skinning data in `collectPathTracingInstances()`**

In `engine/source/runtime/function/render/render_resource.cpp`, in `collectPathTracingInstances()` (around line 280), copy skinning fields from `RenderPathTracingInstance` to `RenderPathTracingCollectedInstance`:

```cpp
for (const RenderPathTracingInstance& pt_instance : scene.getPathTracingInstances())
{
    RenderPathTracingCollectedInstance collected_instance;
    // ... existing fields ...
    collected_instance.entity = pt_instance.entity;
    collected_instance.mesh   = pt_instance.mesh;
    collected_instance.material = pt_instance.material;

    // NEW: skinning data
    collected_instance.enable_vertex_blending = pt_instance.enable_vertex_blending;
    if (pt_instance.enable_vertex_blending && pt_instance.entity != nullptr)
    {
        collected_instance.joint_count    = static_cast<uint32_t>(pt_instance.entity->m_joint_matrices.size());
        collected_instance.joint_matrices = pt_instance.entity->m_joint_matrices.data();
    }

    collected_instances.push_back(collected_instance);
}
```

- [ ] **Step 6: Commit**

```bash
git add engine/source/runtime/function/render/render_resource.h \
        engine/source/runtime/function/render/render_gpu_resource.h \
        engine/source/runtime/function/render/render_scene.h \
        engine/source/runtime/function/render/render_scene.cpp \
        engine/source/runtime/function/render/render_resource.cpp \
        engine/shader/hlsl/path_tracing_common.hlsli
git commit -m "feat: add per-instance skinning data structures for path tracing"
```

---

### Task 2: Create GPU skinning compute shader

**Files:**
- Create: `engine/shader/hlsl/path_tracing_skin.comp.hlsl`

**Interfaces:**
- Consumes: `StructuredBuffer<float3>` at t0, space0 (rest-pose positions from `mesh_vertex_position_buffer`)
- Consumes: `StructuredBuffer<MeshVertexJointBindingData>` at t1, space0 (from `RenderMeshGPUResource` joint binding buffer)
- Consumes: `StructuredBuffer<float3>` at t2, space0 (rest-pose normals+tangents interleaved, stride=2 float3 per vertex; from `mesh_vertex_varying_enable_blending_buffer`)
- Consumes: `StructuredBuffer<float2>` at t3, space0 (rest-pose texcoords from `mesh_vertex_varying_buffer`)
- Consumes: `StructuredBuffer<JointMatrixData>` at t4, space0 (per-instance joint matrices, uploaded per frame)
- Consumes: `ConstantBuffer<SkinComputeConstants>` at b0, space0 (dispatch parameters)
- Produces: `RWStructuredBuffer<float3>` at u0, space0 (skinned positions — for BLAS geometry)
- Produces: `RWStructuredBuffer<PathTracingVertexData>` at u1, space0 (skinned vertex data — into the same flat buffer as `g_vertices` for path tracing)

- [ ] **Step 1: Define compute shader constants and output struct**

Create `engine/shader/hlsl/path_tracing_skin.comp.hlsl`:

```hlsl
#include "common.hlsli"
#include "path_tracing_common.hlsli"

// Input: rest-pose vertex data from mesh GPU buffers
StructuredBuffer<float3>                      g_rest_positions      : register(t0, space0);
StructuredBuffer<MeshVertexJointBindingData>  g_joint_bindings      : register(t1, space0);
// Interleaved normals and tangents: each vertex occupies 2 consecutive float3 elements
// Element [vertex_id * 2 + 0] = normal, [vertex_id * 2 + 1] = tangent
StructuredBuffer<float3>                      g_rest_normal_tangent : register(t2, space0);
StructuredBuffer<float2>                      g_rest_texcoords      : register(t3, space0);

// Input: per-instance joint matrices (M_MESH_VERTEX_BLENDING_MAX_JOINT_COUNT matrices per instance)
StructuredBuffer<JointMatrixData>             g_joint_matrices      : register(t4, space0);

// Constants
struct SkinComputeConstants
{
    uint vertex_count;
    uint joint_matrix_offset;  // index into g_joint_matrices for this instance's first matrix
    uint output_vertex_offset; // offset into output buffers
    uint _padding;
};
ConstantBuffer<SkinComputeConstants> g_constants : register(b0, space0);

// Output
RWStructuredBuffer<float3>                 g_skinned_positions : register(u0, space0);
RWStructuredBuffer<PathTracingVertexData>  g_skinned_vertices  : register(u1, space0);

[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint vertex_id = dispatch_id.x;
    if (vertex_id >= g_constants.vertex_count)
    {
        return;
    }

    // Load rest-pose data
    float3 rest_position = g_rest_positions[vertex_id];
    // Normals and tangents are interleaved in the same buffer (stride=2)
    float3 rest_normal   = g_rest_normal_tangent[vertex_id * 2 + 0];
    float3 rest_tangent  = g_rest_normal_tangent[vertex_id * 2 + 1];
    float2 texcoord      = g_rest_texcoords[vertex_id];

    // Compute skinning matrix (same algorithm as mesh.vert.hlsl:LoadSkinningMatrix)
    MeshVertexJointBindingData binding = g_joint_bindings[vertex_id];
    float4x4 skinning_matrix = (float4x4)0.0f;

    // Weighted accumulation of up to 4 joint matrices
    // NOTE: indices > 0 check mirrors mesh.vert.hlsl (index 0 = invalid joint)
    if (binding.weights.x > 0.0f && binding.indices.x > 0)
    {
        uint joint_idx = g_constants.joint_matrix_offset + uint(binding.indices.x);
        skinning_matrix += g_joint_matrices[joint_idx].joint_matrix * binding.weights.x;
    }
    if (binding.weights.y > 0.0f && binding.indices.y > 0)
    {
        uint joint_idx = g_constants.joint_matrix_offset + uint(binding.indices.y);
        skinning_matrix += g_joint_matrices[joint_idx].joint_matrix * binding.weights.y;
    }
    if (binding.weights.z > 0.0f && binding.indices.z > 0)
    {
        uint joint_idx = g_constants.joint_matrix_offset + uint(binding.indices.z);
        skinning_matrix += g_joint_matrices[joint_idx].joint_matrix * binding.weights.z;
    }
    if (binding.weights.w > 0.0f && binding.indices.w > 0)
    {
        uint joint_idx = g_constants.joint_matrix_offset + uint(binding.indices.w);
        skinning_matrix += g_joint_matrices[joint_idx].joint_matrix * binding.weights.w;
    }

    // Apply skinning
    float3 skinned_position = mul(skinning_matrix, float4(rest_position, 1.0f)).xyz;
    float3 skinned_normal   = normalize(mul((float3x3)skinning_matrix, rest_normal));
    float3 skinned_tangent  = normalize(mul((float3x3)skinning_matrix, rest_tangent));

    // Write outputs
    uint out_idx = g_constants.output_vertex_offset + vertex_id;
    g_skinned_positions[out_idx] = skinned_position;

    PathTracingVertexData v;
    v.position = float4(skinned_position, 1.0f);
    v.normal   = float4(skinned_normal, 0.0f);
    v.tangent  = float4(skinned_tangent, 0.0f);
    v.texcoord = float4(texcoord, 0.0f, 0.0f);
    g_skinned_vertices[out_idx] = v;
}
```

- [ ] **Step 2: Add compute shader to CMake shader compilation**

In the project's shader CMakeLists (search for existing `.hlsl` compilation rules), add the new compute shader target. Since the engine uses DXC for lib_6_6 targets but compute shaders need `cs_6_6`:

```cmake
# Add to existing shader compilation (follow the pattern for .hlsl → .spv_c)
set(SKIN_COMPUTE_SHADER "${CMAKE_CURRENT_SOURCE_DIR}/path_tracing_skin.comp.hlsl")
# ... compile with dxc -T cs_6_6 ...
```

> **Note:** The exact CMake integration depends on the project's shader build system. If the auto-discovery pattern picks up `*.comp.hlsl` files automatically, no CMake change is needed. If not, add the compile rule following the existing pattern for `path_tracing.lib.hlsl`.

- [ ] **Step 3: Commit**

```bash
git add engine/shader/hlsl/path_tracing_skin.comp.hlsl
git commit -m "feat: add GPU skinning compute shader for path tracing"
```

---

### Task 3: C++ host-side compute pipeline and dispatch

**Files:**
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.h`
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`

**Interfaces:**
- Consumes: Compiled compute shader (from Task 2)
- Consumes: `RenderMeshGPUResource` vertex buffers (from existing infrastructure)
- Produces: `setupSkinComputePipeline()` — creates compute pipeline and descriptor set layout
- Produces: `dispatchSkinCompute()` — dispatches skinning compute for all skinned instances
- Produces: `ensureSkinBuffers()` — creates per-instance skinned output buffers

- [ ] **Step 1: Add compute-related members to `PathTracingPass`**

In `engine/source/runtime/function/render/passes/path_tracing_pass.h`, add to the class:

```cpp
class PathTracingPass : public RenderPassBase
{
    // ... existing members ...

    // GPU skinning compute
    RHIDescriptorSetLayout* m_skin_compute_descriptor_set_layout {nullptr};
    RHIPipelineLayout*      m_skin_compute_pipeline_layout {nullptr};
    RHIPipeline*            m_skin_compute_pipeline {nullptr};
    RHIDescriptorSet*       m_skin_compute_descriptor_set {nullptr};

    // Joint matrix upload buffer
    RHIBuffer*       m_joint_matrix_buffer {nullptr};
    RHIDeviceMemory* m_joint_matrix_memory {nullptr};
    size_t           m_joint_matrix_buffer_capacity {0};

    // Skinned output: positions (for BLAS) + vertex data (for g_vertices)
    RHIBuffer*       m_skinned_vertex_output_buffer {nullptr};
    RHIDeviceMemory* m_skinned_vertex_output_memory {nullptr};
    size_t           m_skinned_vertex_output_capacity {0};
    RHIBuffer*       m_skinned_position_output_buffer {nullptr};
    RHIDeviceMemory* m_skinned_position_output_memory {nullptr};
    size_t           m_skinned_position_output_capacity {0};

    void destroySkinComputeResources();
    bool ensureSkinComputeResources();
    bool setupSkinComputePipeline();
    bool ensureSkinBuffers(uint32_t total_skinned_vertices);
    bool uploadJointMatrices(const std::vector<RenderPathTracingCollectedInstance>& instances);
    void dispatchSkinCompute(RHICommandBuffer* command_buffer,
                             const std::vector<RenderPathTracingCollectedInstance>& instances);
};
```

- [ ] **Step 2: Implement `setupSkinComputePipeline()`**

In `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`, add:

```cpp
bool PathTracingPass::setupSkinComputePipeline()
{
    if (m_rhi == nullptr) return false;

    // Descriptor set layout (bindings match path_tracing_skin.comp.hlsl — 8 bindings: t0-t4, b0, u0, u1)
    {
        std::array<RHIDescriptorSetLayoutBinding, 8> bindings {};

        // Binding 0: rest-pose positions (t0, space0) — read-only SRV
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

        // Binding 1: joint bindings (t1, space0) — read-only SRV
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

        // Binding 2: rest-pose normal+tangent interleaved (t2, space0) — read-only SRV
        bindings[2].binding         = 2;
        bindings[2].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

        // Binding 3: rest-pose texcoords (t3, space0) — read-only SRV
        bindings[3].binding         = 3;
        bindings[3].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

        // Binding 4: joint matrices (t4, space0) — read-only SRV
        bindings[4].binding         = 4;
        bindings[4].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

        // Binding 5: uniform buffer for SkinComputeConstants (b0, space0)
        bindings[5].binding         = 0;
        bindings[5].descriptorType  = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

        // Binding 6: skinned positions output (u0, space0) — write-only UAV
        bindings[6].binding         = 0;
        bindings[6].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[6].descriptorCount = 1;
        bindings[6].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

        // Binding 7: skinned vertex data output (u1, space0) — write-only UAV
        // This is the SAME buffer as the path tracing g_vertices (t4 in path_tracing.lib.hlsl)
        bindings[7].binding         = 1;
        bindings[7].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[7].descriptorCount = 1;
        bindings[7].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

        RHIDescriptorSetLayoutCreateInfo layout_info {};
        layout_info.sType        = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
        layout_info.pBindings    = bindings.data();

        if (!m_rhi->createDescriptorSetLayout(&layout_info, m_skin_compute_descriptor_set_layout))
        {
            return false;
        }
    }

    // Pipeline layout
    {
        RHIPipelineLayoutCreateInfo layout_info {};
        layout_info.sType          = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts    = &m_skin_compute_descriptor_set_layout;

        if (!m_rhi->createPipelineLayout(&layout_info, m_skin_compute_pipeline_layout))
        {
            return false;
        }
    }

    // Compute pipeline
    {
        // Load compiled shader binary
        // The compiled .comp.hlsl produces a binary; load it here
        // (Following the same pattern as setupRayTracingPipeline())

        RHIPipelineShaderStageCreateInfo stage {};
        stage.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage  = RHI_SHADER_STAGE_COMPUTE_BIT;
        stage.module = /* loaded shader module */;
        stage.pName  = "main";

        RHIComputePipelineCreateInfo pipeline_info {};
        pipeline_info.sType   = RHI_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_info.pStages = &stage;
        pipeline_info.layout  = m_skin_compute_pipeline_layout;

        if (!m_rhi->createComputePipelines(nullptr, 1, &pipeline_info, m_skin_compute_pipeline))
        {
            return false;
        }
    }

    return true;
}
```

> **Note:** The exact shader module loading mechanism depends on the engine's shader binary format. Follow the pattern used in `setupRayTracingPipeline()` for loading compiled HLSL binaries.

- [ ] **Step 3: Implement buffer allocation helpers**

```cpp
bool PathTracingPass::ensureSkinBuffers(uint32_t total_skinned_vertices)
{
    if (total_skinned_vertices == 0) return true;

    // Ensure skinned vertex output buffer (PathTracingVertexData per vertex)
    size_t vertex_data_size = total_skinned_vertices * sizeof(RenderPathTracingVertexGPUData);
    if (vertex_data_size > m_skinned_vertex_output_capacity)
    {
        if (m_skinned_vertex_output_buffer) m_rhi->destroyBuffer(m_skinned_vertex_output_buffer);
        if (m_skinned_vertex_output_memory) m_rhi->freeMemory(m_skinned_vertex_output_memory);

        m_skinned_vertex_output_capacity = vertex_data_size * 2; // 2x padding for growth

        RHIBufferCreateInfo buffer_info {};
        buffer_info.sType = RHI_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size  = m_skinned_vertex_output_capacity;
        buffer_info.usage = RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT | RHI_BUFFER_USAGE_TRANSFER_DST_BIT;
        buffer_info.memoryProperties = RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        if (!m_rhi->createBuffer(&buffer_info, m_skinned_vertex_output_buffer) ||
            !m_rhi->allocateMemory(m_skinned_vertex_output_buffer, m_skinned_vertex_output_memory))
        {
            return false;
        }
    }

    // Ensure skinned position output buffer (float3 per vertex, for BLAS geometry)
    size_t position_data_size = total_skinned_vertices * sizeof(float) * 3;
    if (position_data_size > m_skinned_position_output_capacity)
    {
        if (m_skinned_position_output_buffer) m_rhi->destroyBuffer(m_skinned_position_output_buffer);
        if (m_skinned_position_output_memory) m_rhi->freeMemory(m_skinned_position_output_memory);

        m_skinned_position_output_capacity = position_data_size * 2;

        RHIBufferCreateInfo buffer_info {};
        buffer_info.sType = RHI_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size  = m_skinned_position_output_capacity;
        buffer_info.usage = RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                            RHI_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                            RHI_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        buffer_info.memoryProperties = RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        if (!m_rhi->createBuffer(&buffer_info, m_skinned_position_output_buffer) ||
            !m_rhi->allocateMemory(m_skinned_position_output_buffer, m_skinned_position_output_memory))
        {
            return false;
        }
    }

    return true;
}
```

- [ ] **Step 4: Implement joint matrix upload**

```cpp
bool PathTracingPass::uploadJointMatrices(const std::vector<RenderPathTracingCollectedInstance>& instances)
{
    // Calculate total joint matrix count
    uint32_t total_joint_count = 0;
    for (const auto& inst : instances)
    {
        if (inst.enable_vertex_blending && inst.joint_count > 0)
        {
            total_joint_count += inst.joint_count;
        }
    }
    if (total_joint_count == 0) return true;

    size_t data_size = total_joint_count * sizeof(Matrix4x4);
    if (data_size > m_joint_matrix_buffer_capacity)
    {
        if (m_joint_matrix_buffer) m_rhi->destroyBuffer(m_joint_matrix_buffer);
        if (m_joint_matrix_memory) m_rhi->freeMemory(m_joint_matrix_memory);

        m_joint_matrix_buffer_capacity = data_size * 2;

        RHIBufferCreateInfo buffer_info {};
        buffer_info.sType = RHI_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size  = m_joint_matrix_buffer_capacity;
        buffer_info.usage = RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        buffer_info.memoryProperties = RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        if (!m_rhi->createBuffer(&buffer_info, m_joint_matrix_buffer) ||
            !m_rhi->allocateMemory(m_joint_matrix_buffer, m_joint_matrix_memory))
        {
            return false;
        }
    }

    // Map and copy joint matrices
    void* mapped = nullptr;
    if (!m_rhi->mapMemory(m_joint_matrix_memory, 0, data_size, &mapped) || mapped == nullptr)
    {
        return false;
    }

    uint32_t offset = 0;
    for (const auto& inst : instances)
    {
        if (inst.enable_vertex_blending && inst.joint_count > 0 && inst.joint_matrices != nullptr)
        {
            size_t bytes = inst.joint_count * sizeof(Matrix4x4);
            std::memcpy(static_cast<uint8_t*>(mapped) + offset, inst.joint_matrices, bytes);
            offset += bytes;
        }
    }
    m_rhi->unmapMemory(m_joint_matrix_memory);
    return true;
}
```

- [ ] **Step 5: Implement compute dispatch**

```cpp
void PathTracingPass::dispatchSkinCompute(RHICommandBuffer* command_buffer,
                                          const std::vector<RenderPathTracingCollectedInstance>& instances)
{
    if (m_skin_compute_pipeline == nullptr) return;
    if (command_buffer == nullptr) return;

    uint32_t joint_matrix_offset = 0;
    uint32_t skinned_vertex_offset = 0;

    for (const auto& inst : instances)
    {
        if (!inst.enable_vertex_blending || inst.mesh == nullptr) continue;
        if (inst.joint_count == 0 || inst.joint_matrices == nullptr) continue;

        RenderMeshGPUResource* mesh = inst.mesh;
        uint32_t vertex_count = mesh->mesh_vertex_count;
        if (vertex_count == 0) continue;

        // Build descriptor writes for this instance's dispatch
        // 8 writes: 6 read-only inputs (t0-t4, b0) + 2 write outputs (u0, u1)
        std::array<RHIWriteDescriptorSet, 8> writes {};

        // Write 0: rest-pose positions (t0)
        RHIDescriptorBufferInfo rest_positions_info {};
        rest_positions_info.buffer = mesh->mesh_vertex_position_buffer;
        rest_positions_info.offset = 0;
        rest_positions_info.range  = RHI_WHOLE_SIZE;
        writes[0].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo     = &rest_positions_info;

        // Write 1: joint bindings (t1)
        RHIDescriptorBufferInfo joint_bindings_info {};
        joint_bindings_info.buffer = mesh->mesh_vertex_joint_binding_buffer;
        joint_bindings_info.offset = 0;
        joint_bindings_info.range  = RHI_WHOLE_SIZE;
        writes[1].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo     = &joint_bindings_info;

        // Write 2: rest-pose normal+tangent interleaved (t2)
        // Same buffer, stride=2 float3 per vertex: [normal_0, tangent_0, normal_1, tangent_1, ...]
        RHIDescriptorBufferInfo normal_tangent_info {};
        normal_tangent_info.buffer = mesh->mesh_vertex_varying_enable_blending_buffer;
        normal_tangent_info.offset = 0;
        normal_tangent_info.range  = RHI_WHOLE_SIZE;
        writes[2].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstBinding      = 2;
        writes[2].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo     = &normal_tangent_info;

        // Write 3: rest-pose texcoords (t3)
        RHIDescriptorBufferInfo texcoords_info {};
        texcoords_info.buffer = mesh->mesh_vertex_varying_buffer;
        texcoords_info.offset = 0;
        texcoords_info.range  = RHI_WHOLE_SIZE;
        writes[3].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstBinding      = 3;
        writes[3].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo     = &texcoords_info;

        // Write 4: joint matrices (t4)
        RHIDescriptorBufferInfo joint_matrices_info {};
        joint_matrices_info.buffer = m_joint_matrix_buffer;
        joint_matrices_info.offset = joint_matrix_offset * sizeof(Matrix4x4);
        joint_matrices_info.range  = RHI_WHOLE_SIZE;
        writes[4].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstBinding      = 4;
        writes[4].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].pBufferInfo     = &joint_matrices_info;

        // Write 5: SkinComputeConstants uniform buffer (b0)
        struct { uint32_t vc; uint32_t jmo; uint32_t ovo; uint32_t pad; }
            constants = { vertex_count, joint_matrix_offset, skinned_vertex_offset, 0 };
        // Upload constants to a small host-visible buffer and bind it here
        // (use a ring-buffer or dedicated small buffer, following engine patterns)
        RHIDescriptorBufferInfo constants_info {};
        constants_info.buffer = /* temp uniform buffer with constants */;
        constants_info.offset = 0;
        constants_info.range  = sizeof(constants);
        writes[5].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstBinding      = 0;  // b0
        writes[5].descriptorType  = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[5].descriptorCount = 1;
        writes[5].pBufferInfo     = &constants_info;

        // Write 6: skinned positions output (u0)
        RHIDescriptorBufferInfo skinned_positions_info {};
        skinned_positions_info.buffer = m_skinned_position_output_buffer;
        skinned_positions_info.offset = skinned_vertex_offset * sizeof(float) * 3;
        skinned_positions_info.range  = RHI_WHOLE_SIZE;
        writes[6].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstBinding      = 0;  // u0
        writes[6].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[6].descriptorCount = 1;
        writes[6].pBufferInfo     = &skinned_positions_info;

        // Write 7: skinned vertex data output (u1)
        // This is the same buffer as g_vertices (t4) in the path tracing descriptor set
        RHIDescriptorBufferInfo skinned_vertices_info {};
        skinned_vertices_info.buffer = m_render_resource_impl->getPathTracingVertexBuffer();
        skinned_vertices_info.offset = skinned_vertex_offset * sizeof(RenderPathTracingVertexGPUData);
        skinned_vertices_info.range  = RHI_WHOLE_SIZE;
        writes[7].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstBinding      = 1;  // u1
        writes[7].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[7].descriptorCount = 1;
        writes[7].pBufferInfo     = &skinned_vertices_info;

        m_rhi->updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        // Bind and dispatch
        m_rhi->cmdBindPipelinePFN(command_buffer, RHI_PIPELINE_BIND_POINT_COMPUTE, m_skin_compute_pipeline);
        m_rhi->cmdBindDescriptorSetsPFN(command_buffer, RHI_PIPELINE_BIND_POINT_COMPUTE,
                                         m_skin_compute_pipeline_layout, 0, 1,
                                         &m_skin_compute_descriptor_set, 0, nullptr);

        uint32_t group_count = (vertex_count + 63) / 64;
        m_rhi->cmdDispatch(command_buffer, group_count, 1, 1);

        joint_matrix_offset += inst.joint_count;
        skinned_vertex_offset += vertex_count;
    }

    // UAV barrier: compute writes (u0, u1) → AS build reads and path tracing reads
    // This must be inserted BEFORE BLAS construction in buildTopLevelAS()
    // RHI barrier pattern: storage buffer write → acceleration structure read
    m_rhi->cmdPipelineBarrier(command_buffer,
                               RHI_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               RHI_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT,
                               /* memory barrier for UAV→AS transition */);
}
```

- [ ] **Step 6: Commit**

```bash
git add engine/source/runtime/function/render/passes/path_tracing_pass.h \
        engine/source/runtime/function/render/passes/path_tracing_pass.cpp
git commit -m "feat: add GPU skin compute pipeline and dispatch infrastructure"
```

---

### Task 4: Per-instance BLAS and modified scene buffer update

**Files:**
- Modify: `engine/source/runtime/function/render/render_resource.cpp`
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`

**Interfaces:**
- Consumes: Per-instance skinned position buffers (from Task 3 compute output)
- Consumes: `RenderPathTracingCollectedInstance::enable_vertex_blending` (from Task 1)
- Produces: Per-instance BLAS for skinned meshes (built from skinned positions)
- Produces: Per-instance geometry in `g_vertices` flat buffer (no dedup for skinned)

- [ ] **Step 1: Modify `updatePathTracingSceneBuffers()` for per-instance geometry**

In `engine/source/runtime/function/render/render_resource.cpp`, in `updatePathTracingSceneBuffers()` (around line 328), change the geometry dedup logic. Skinned instances must NOT share geometry:

```cpp
for (uint32_t instance_index = 0; instance_index < collected_instances.size(); ++instance_index)
{
    const RenderPathTracingCollectedInstance& source_instance = collected_instances[instance_index];

    uint32_t geometry_index = 0;

    // Skinned meshes: each instance gets its own geometry (no dedup)
    // Static meshes: dedup by mesh pointer (existing behavior)
    const bool is_skinned = source_instance.enable_vertex_blending;
    if (!is_skinned)
    {
        auto geometry_it = geometry_indices.find(source_instance.mesh);
        if (geometry_it != geometry_indices.end())
        {
            geometry_index = geometry_it->second;
            // ... build material/instance records (existing code path) ...
            continue; // skip vertex/index append for dedup'd static meshes
        }
    }

    geometry_index = static_cast<uint32_t>(m_path_tracing_geometry_data.size());
    if (!is_skinned)
    {
        geometry_indices[source_instance.mesh] = geometry_index;
    }

    RenderPathTracingGeometryGPUData geometry_data{};
    geometry_data.vertex_offset = static_cast<uint32_t>(m_path_tracing_vertex_data.size());
    geometry_data.index_offset  = static_cast<uint32_t>(m_path_tracing_index_data.size());
    geometry_data.index_count   = static_cast<uint32_t>(source_instance.mesh->path_tracing_indices.size());

    // For skinned instances, the vertex data will be written by the compute shader.
    // Reserve space in the flat buffer by appending placeholder vertices.
    uint32_t vertex_count = static_cast<uint32_t>(source_instance.mesh->path_tracing_positions.size());
    if (is_skinned)
    {
        // Reserve space; compute shader fills it later
        for (uint32_t v = 0; v < vertex_count; ++v)
        {
            m_path_tracing_vertex_data.push_back(RenderPathTracingVertexGPUData{});
        }
    }
    else
    {
        // Static mesh: copy rest-pose data (existing behavior)
        for (uint32_t v = 0; v < vertex_count; ++v)
        {
            RenderPathTracingVertexGPUData vertex{};
            vertex.position = Vector4(source_instance.mesh->path_tracing_positions[v], 1.0f);
            vertex.normal   = Vector4(source_instance.mesh->path_tracing_normals[v], 0.0f);
            vertex.tangent  = Vector4(source_instance.mesh->path_tracing_tangents[v], 0.0f);
            vertex.texcoord = Vector4(source_instance.mesh->path_tracing_texcoords[v].x,
                                      source_instance.mesh->path_tracing_texcoords[v].y, 0.0f, 0.0f);
            m_path_tracing_vertex_data.push_back(vertex);
        }
    }

    // Append indices (shared for both skinned and static)
    for (uint32_t idx : source_instance.mesh->path_tracing_indices)
    {
        m_path_tracing_index_data.push_back(idx);
    }
    m_path_tracing_geometry_data.push_back(geometry_data);

    // ... build material and instance records as before ...
    // For skinned instances, set flags bit 0:
    RenderPathTracingInstanceGPUData instance_data{};
    instance_data.geometry_index = geometry_index;
    instance_data.material_index = shader_material_index;
    instance_data.entity_instance_id = source_instance.instance_id;
    instance_data.flags = is_skinned ? 1u : 0u;  // bit 0: enable_vertex_blending
    m_path_tracing_instance_data.push_back(instance_data);
}
```

- [ ] **Step 2: Modify `ensurePathTracingBLAS()` for per-instance BLAS**

In `engine/source/runtime/function/render/render_resource.cpp`, add a new overload or modify `ensurePathTracingBLAS()` to accept a skinned position buffer for per-instance BLAS:

```cpp
// New overload: build BLAS from caller-provided skinned position buffer
RHIAccelerationStructure* RenderResource::buildPathTracingBLASFromSkinned(
    std::shared_ptr<RHI> rhi,
    RHICommandBuffer* command_buffer,
    RHIBuffer* skinned_position_buffer,
    uint32_t vertex_count,
    uint32_t vertex_stride,
    RHIBuffer* index_buffer,
    uint32_t index_count,
    RHIIndexType index_type)
{
    if (skinned_position_buffer == nullptr || vertex_count == 0) return nullptr;

    RHIAccelerationStructureGeometryDesc geometry;
    geometry.vertex_position_buffer = skinned_position_buffer;
    geometry.vertex_position_offset = 0;
    geometry.vertex_count           = vertex_count;
    geometry.vertex_stride          = vertex_stride;
    geometry.index_buffer           = index_buffer;
    geometry.index_offset           = 0;
    geometry.index_count            = index_count;
    geometry.index_type             = index_type;
    geometry.opaque                 = true;

    RHIAccelerationStructureBuildDesc build_desc;
    build_desc.type              = RHIAccelerationStructureType::BottomLevel;
    build_desc.geometries        = &geometry;
    build_desc.geometry_count    = 1;
    build_desc.prefer_fast_trace = true;
    build_desc.allow_update      = false;  // full rebuild for now
    build_desc.perform_update    = false;
    build_desc.source            = nullptr;

    RHIAccelerationStructure* new_blas = nullptr;
    if (!rhi->createAccelerationStructure(&build_desc, new_blas) || new_blas == nullptr)
    {
        return nullptr;
    }

    if (!rhi->buildAccelerationStructure(command_buffer, &build_desc, new_blas))
    {
        rhi->destroyAccelerationStructure(new_blas);
        return nullptr;
    }

    return new_blas;
}
```

- [ ] **Step 3: Wire per-instance BLAS into `buildTopLevelAS()`**

In `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`, modify `buildTopLevelAS()` (around line 777). After compute dispatch, build per-instance BLAS for skinned meshes:

```cpp
// After dispatchSkinCompute(), build per-instance BLAS for skinned meshes.
// Use persistent storage in RenderMeshGPUResource::path_tracing_skinned_resources
// to avoid recreating BLAS every frame.

// Track active instance IDs for this frame to clean up orphaned entries
std::unordered_set<uint32_t> active_skinned_instance_ids;

for (RenderPathTracingCollectedInstance& instance : collected_instances)
{
    if (!instance.enable_vertex_blending || instance.mesh == nullptr)
    {
        // Static mesh: use shared mesh-level BLAS (existing logic)
        if (instance.mesh != nullptr &&
            !processed_meshes.insert(instance.mesh).second)
        {
            continue;
        }
        const bool was_dirty = instance.mesh->path_tracing_blas_dirty;
        m_render_resource_impl->ensurePathTracingBLAS(m_rhi, command_buffer, *instance.mesh);
        if (was_dirty && !instance.mesh->path_tracing_blas_dirty &&
            instance.mesh->path_tracing_bottom_level_as != nullptr)
        {
            ++m_last_blas_build_count;
        }
        instance.bottom_level_as = instance.mesh->path_tracing_bottom_level_as;
    }
    else
    {
        // Skinned mesh: build per-instance BLAS, stored persistently
        RenderMeshGPUResource* mesh = instance.mesh;
        uint32_t inst_id = instance.instance_id;
        active_skinned_instance_ids.insert(inst_id);

        auto& resources = mesh->path_tracing_skinned_resources[inst_id];

        // Ensure per-instance skinned position buffer is allocated
        uint32_t vertex_count = mesh->mesh_vertex_count;
        if (resources.skinned_position_buffer == nullptr ||
            resources.vertex_count != vertex_count)
        {
            if (resources.skinned_position_buffer != nullptr)
                m_rhi->destroyBuffer(resources.skinned_position_buffer);
            if (resources.skinned_position_memory != nullptr)
                m_rhi->freeMemory(resources.skinned_position_memory);

            RHIBufferCreateInfo buffer_info {};
            buffer_info.sType = RHI_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer_info.size  = vertex_count * sizeof(float) * 3;
            buffer_info.usage = RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                RHI_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                RHI_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            buffer_info.memoryProperties = RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

            if (!m_rhi->createBuffer(&buffer_info, resources.skinned_position_buffer) ||
                !m_rhi->allocateMemory(resources.skinned_position_buffer, resources.skinned_position_memory))
            {
                continue;
            }
            resources.vertex_count = vertex_count;
            resources.index_count  = mesh->mesh_index_count;
        }

        // Destroy old BLAS before building new one (vertex positions changed)
        if (resources.blas != nullptr)
        {
            m_rhi->destroyAccelerationStructure(resources.blas);
            resources.blas = nullptr;
        }

        resources.blas = m_render_resource_impl->buildPathTracingBLASFromSkinned(
            m_rhi,
            command_buffer,
            resources.skinned_position_buffer,
            vertex_count,
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
}

// Clean up orphaned per-instance resources (instances that disappeared)
for (auto& [mesh_ptr, instances] : /* iterate all meshes in scene */)
{
    auto& map = mesh_ptr->path_tracing_skinned_resources;
    for (auto it = map.begin(); it != map.end(); )
    {
        if (active_skinned_instance_ids.count(it->first) == 0)
        {
            if (it->second.blas != nullptr)
                m_rhi->destroyAccelerationStructure(it->second.blas);
            if (it->second.skinned_position_buffer != nullptr)
                m_rhi->destroyBuffer(it->second.skinned_position_buffer);
            if (it->second.skinned_position_memory != nullptr)
                m_rhi->freeMemory(it->second.skinned_position_memory);
            it = map.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
```

> **Design note:** The per-instance skinned position buffer is a dedicated buffer per instance (not a flat shared buffer), because `RHIAccelerationStructureGeometryDesc` currently does not support a `vertex_position_offset` field. Each instance gets its own small `float3` buffer sized to its vertex count. If the `vertex_position_offset` field is later added to `RHIAccelerationStructureGeometryDesc`, all instances could share one large skinned position buffer with per-instance offsets.

- [ ] **Step 4: Commit**

```bash
git add engine/source/runtime/function/render/render_resource.cpp \
        engine/source/runtime/function/render/render_resource.h \
        engine/source/runtime/function/render/passes/path_tracing_pass.cpp
git commit -m "feat: per-instance BLAS for skinned meshes in path tracing"
```

---

### Task 5: TLAS dirty handling and integration

**Files:**
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`
- Modify: `engine/source/runtime/function/render/render_scene.cpp`

**Interfaces:**
- Consumes: Per-instance BLAS (from Task 4)
- Consumes: Compute skin dispatch (from Task 3)
- Produces: Correct TLAS dirty logic for animated scenes
- Produces: Full compute → BLAS → TLAS → trace pipeline

- [ ] **Step 1: Always mark TLAS dirty when skinned instances exist**

In `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`, in `buildTopLevelAS()`, modify the `tlas_dirty` check:

```cpp
// After compute dispatch and BLAS ensure:

const bool has_skinned = std::any_of(collected_instances.begin(), collected_instances.end(),
    [](const RenderPathTracingCollectedInstance& i) { return i.enable_vertex_blending; });

const bool tlas_dirty =
    has_skinned ||                              // <-- NEW: always dirty for animated scenes
    scene.isPathTracingTLASDirty() ||
    m_top_level_as == nullptr ||
    m_tlas_instance_count != collected_instances.size();
m_last_tlas_rebuilt = tlas_dirty;
if (!tlas_dirty)
{
    return true;
}
```

- [ ] **Step 2: Wire the full pipeline flow in `buildTopLevelAS()`**

The correct order of operations in `buildTopLevelAS()` (around line 777):

```
1. collectPathTracingInstances()
2. For each mesh: ensure BLAS for static meshes (existing per-mesh dedup)
3. uploadJointMatrices()           ← NEW
4. updatePathTracingSceneBuffers() ← per-instance geometry for skinned
5. updateDescriptorSet()           ← bind new g_vertices (includes skinned)
6. ensureSkinBuffers()             ← NEW: allocate compute output buffers
7. dispatchSkinCompute()           ← NEW: GPU skin all skinned instances
8. Insert UAV barrier (compute write → AS build read)
9. Build per-instance BLAS for skinned instances  ← NEW
10. Clean up orphaned per-instance BLAS (instances that disappeared)  ← NEW
11. Check tlas_dirty (always true if has_skinned)
12. Rebuild TLAS from all BLAS instances
```

- [ ] **Step 3: Build and verify compilation**

```bash
cd d:/program/Piccolo/build
cmake --build . --config Debug --target PiccoloRuntime 2>&1 | tail -10
```

Expected: Build succeeds. No errors.

- [ ] **Step 4: Commit**

```bash
git add engine/source/runtime/function/render/passes/path_tracing_pass.cpp \
        engine/source/runtime/function/render/passes/path_tracing_pass.h \
        engine/source/runtime/function/render/render_scene.cpp \
        engine/source/runtime/function/render/render_resource.cpp \
        engine/source/runtime/function/render/render_resource.h
git commit -m "feat: integrate GPU skinning into path tracing build pipeline"
```

---

## Verification Checklist (runtime, post-implementation)

1. **Static scene (no skinned meshes):** Identical output to before, no performance regression
2. **Scene with skinned mesh:** Mesh appears animated (not bind pose), follows skeleton animation
3. **Animation correctness:** Vertex positions match what the raster pipeline renders (within precision)
4. **BLAS count:** Each skinned instance has its own BLAS rebuilt every frame
5. **TLAS rebuild:** TLAS rebuilds every frame when skinned instances present
6. **No crash or D3D12 validation error:** Clean debug layer output
