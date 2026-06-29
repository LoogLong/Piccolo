# Extract GPU Skinning to Standalone Pass

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract GPU skinning (compute pipeline, joint matrix upload, compute dispatch) from `PathTracingPass` into a new standalone `GpuSkinningPass` that runs at the very beginning of the frame, providing skinned vertex data for all downstream passes.

**Architecture:** `GpuSkinningPass` inherits `RenderPass` and follows the pattern established by `ParticlePass`. It manages its own compute pipeline and descriptor set, uploads joint matrices each frame, and dispatches a compute shader per skinned mesh instance. Skinned output is written directly into persistent buffers in `RenderMeshGPUResource::path_tracing_skinned_resources` (position buffer) and the flat `g_vertices` buffer (vertex data). `PathTracingPass` is simplified to only consume pre-skinned data — building per-instance BLAS from skinned position buffers and tracing with skinned vertex data. All skinning logic is removed from `PathTracingPass`.

**Tech Stack:** C++17, D3D12, HLSL SM 6.6, Piccolo RHI

## Global Constraints

- Must not break static mesh path tracing (identical output for non-skinned scenes)
- Must not affect the rasterization pipeline
- Must follow existing pass patterns (`ParticlePass` is the reference for compute-based passes)
- `GpuSkinningPass` must run before `PathTracingPass` in the frame
- Existing HLSL compute shader (`path_tracing_skin.comp.hlsl`) must remain unchanged

---

## File Map

| File | Responsibility | Modified? |
|------|---------------|-----------|
| `engine/source/runtime/function/render/passes/gpu_skinning_pass.h` | `GpuSkinningPass` class declaration | ✅ Create |
| `engine/source/runtime/function/render/passes/gpu_skinning_pass.cpp` | Compute pipeline, joint upload, dispatch implementation | ✅ Create |
| `engine/source/runtime/function/render/render_pipeline_base.h` | Add `m_gpu_skinning_pass` member | ✅ Modify |
| `engine/source/runtime/function/render/render_pipeline_base.cpp` | Add `preparePassData()` call for gpu_skinning_pass | ✅ Modify |
| `engine/source/runtime/function/render/render_pipeline.h` | Add `initializeGpuSkinningPass()` declaration | ✅ Modify |
| `engine/source/runtime/function/render/render_pipeline.cpp` | Create, init, dispatch at frame start; wire to path tracing | ✅ Modify |
| `engine/source/runtime/function/render/passes/path_tracing_pass.h` | **Remove** skinning members and function declarations | ✅ Modify |
| `engine/source/runtime/function/render/passes/path_tracing_pass.cpp` | **Remove** `setupSkinComputePipeline()`, `uploadJointMatrices()`, `ensureSkinBuffers()`, `dispatchSkinCompute()`, `destroySkinComputeResources()`; keep per-instance BLAS build and orphan cleanup | ✅ Modify |
| `engine/source/runtime/function/render/render_resource.h` | **Keep** `buildPathTracingBLASFromSkinned()` declaration (used by PathTracingPass) | No change |
| `engine/source/runtime/function/render/render_resource.cpp` | **Keep** `buildPathTracingBLASFromSkinned()` impl; **Keep** per-instance geometry in `updatePathTracingSceneBuffers()`; **Keep** skinning fields in `collectPathTracingInstances()` | No change |
| `engine/shader/hlsl/path_tracing_skin.comp.hlsl` | Compute shader for skinning | No change |
| `engine/source/runtime/function/render/render_shader_bytecode.h` | Shader bytecode references | No change |
| `engine/source/runtime/function/render/render_gpu_resource.h` | `RenderMeshGPUResource::SkinnedPathTracingResources` (persistent per-instance buffers) | No change |
| `engine/shader/hlsl/path_tracing_common.hlsli` | `PathTracingInstanceData::flags` | No change |

---

### Task 1: Create `GpuSkinningPass` class

**Files:**
- Create: `engine/source/runtime/function/render/passes/gpu_skinning_pass.h`
- Create: `engine/source/runtime/function/render/passes/gpu_skinning_pass.cpp`

**Interfaces:**
- Consumes: `RenderResource` (via `m_render_resource`) for mesh buffer access
- Consumes: `RenderScene` (via `m_render_resource->getCurrentRenderScene()`) for entity/animation data
- Produces: Skinned position buffers in `RenderMeshGPUResource::path_tracing_skinned_resources`
- Produces: Skinned vertex data in the flat `g_vertices` buffer (via compute shader UAV write)

- [ ] **Step 1: Create header file**

Create `engine/source/runtime/function/render/passes/gpu_skinning_pass.h`:

```cpp
#pragma once

#include "runtime/function/render/render_pass.h"

#include <cstdint>
#include <vector>

namespace Piccolo
{
    struct RenderPathTracingCollectedInstance;

    class GpuSkinningPass : public RenderPass
    {
    public:
        void initialize(const RenderPassInitInfo* init_info) override final;
        void preparePassData(std::shared_ptr<RenderResourceBase> render_resource) override final;

        bool setup();
        bool dispatch();

    private:
        bool setupSkinComputePipeline();
        bool ensureSkinBuffers(uint32_t total_skinned_vertices);
        bool uploadJointMatrices(const std::vector<RenderPathTracingCollectedInstance>& instances);
        void dispatchSkinCompute(RHICommandBuffer* command_buffer,
                                 const std::vector<RenderPathTracingCollectedInstance>& instances);

        std::shared_ptr<class RenderResource> m_render_resource_impl;

        // Compute pipeline resources
        RHIDescriptorSetLayout* m_skin_compute_descriptor_set_layout {nullptr};
        RHIPipelineLayout*      m_skin_compute_pipeline_layout {nullptr};
        RHIPipeline*            m_skin_compute_pipeline {nullptr};
        RHIDescriptorSet*       m_skin_compute_descriptor_set {nullptr};

        // Joint matrix upload buffer (host-visible, mapped per frame)
        RHIBuffer*       m_joint_matrix_buffer {nullptr};
        RHIDeviceMemory* m_joint_matrix_memory {nullptr};
        size_t           m_joint_matrix_buffer_capacity {0};

        // Flat output buffer for skinned positions (BLAS geometry source)
        RHIBuffer*       m_skinned_position_output_buffer {nullptr};
        RHIDeviceMemory* m_skinned_position_output_memory {nullptr};
        size_t           m_skinned_position_output_capacity {0};
    };
} // namespace Piccolo
```

- [ ] **Step 2: Create implementation file — `initialize()`, `preparePassData()`, `setup()`**

Create `engine/source/runtime/function/render/passes/gpu_skinning_pass.cpp`:

```cpp
#include "runtime/function/render/passes/gpu_skinning_pass.h"

#include "runtime/core/base/macro.h"
#include "runtime/function/render/render_resource.h"
#include "runtime/function/render/render_scene.h"
#include "runtime/function/render/render_shader_bytecode.h"

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace Piccolo
{
    void GpuSkinningPass::initialize(const RenderPassInitInfo* init_info)
    {
        RenderPass::initialize(nullptr);
    }

    void GpuSkinningPass::preparePassData(std::shared_ptr<RenderResourceBase> render_resource)
    {
        m_render_resource_impl = std::static_pointer_cast<RenderResource>(render_resource);
    }

    bool GpuSkinningPass::setup()
    {
        if (m_skin_compute_pipeline != nullptr) return true;
        if (m_rhi == nullptr) return false;
        if (m_rhi->getBackendType() != RHIBackendType::D3D12 ||
            m_rhi->getRayTracingCapabilities().support_level != RHIRayTracingSupportLevel::Supported)
        {
            return false;
        }
        return setupSkinComputePipeline();
    }

    // setupSkinComputePipeline(), ensureSkinBuffers(),
    // uploadJointMatrices(), dispatchSkinCompute()
    // — these are MOVED from PathTracingPass, implementations identical
    // except they use m_render_resource_impl (from this class) instead of
    // PathTracingPass's member.
```

- [ ] **Step 3: Port compute pipeline setup from PathTracingPass**

Copy `setupSkinComputePipeline()` from `path_tracing_pass.cpp` with these changes:
- `m_rhi` → same (inherited from `RenderPassBase`)
- `m_skin_compute_*` members → declared in this class
- `m_render_resource_impl` → declared in this class (set via `preparePassData()`)
- Shader bytecode reference: `PICCOLO_RENDER_SHADER_BYTECODE(m_rhi, PATH_TRACING_SKIN_COMP)` unchanged

- [ ] **Step 4: Port buffer allocation and dispatch from PathTracingPass**

Copy `ensureSkinBuffers()`, `uploadJointMatrices()`, `dispatchSkinCompute()` from `path_tracing_pass.cpp`. These functions are identical in logic. The only changes:
- Class name prefix: `PathTracingPass::` → `GpuSkinningPass::`
- In `dispatchSkinCompute()`, the UAV barrier at the end is the same

- [ ] **Step 5: Implement `dispatch()` — the main entry point**

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

    // Collect path tracing instances to know which meshes need skinning
    auto collected_instances = m_render_resource_impl->collectPathTracingInstances(*render_scene);

    // Check if any instances need skinning
    const bool has_skinned = std::any_of(collected_instances.begin(), collected_instances.end(),
        [](const RenderPathTracingCollectedInstance& i) { return i.enable_vertex_blending; });

    if (!has_skinned) return true;  // Nothing to skin this frame

    // Upload joint matrices for all skinned instances
    if (!uploadJointMatrices(collected_instances))
    {
        LOG_WARN("GpuSkinningPass: failed to upload joint matrices");
        return false;
    }

    RHICommandBuffer* command_buffer = m_rhi->getCurrentCommandBuffer();
    if (command_buffer == nullptr) return false;

    // Allocate output buffers if needed
    uint32_t total_skinned_vertices = 0;
    for (const auto& inst : collected_instances)
    {
        if (inst.enable_vertex_blending && inst.mesh != nullptr)
        {
            total_skinned_vertices += inst.mesh->mesh_vertex_count;
        }
    }
    if (!ensureSkinBuffers(total_skinned_vertices))
    {
        LOG_WARN("GpuSkinningPass: failed to allocate skinning buffers");
        return false;
    }

    // Assign vertex offsets in flat output buffer
    uint32_t current_vertex_offset = 0;
    for (auto& inst : collected_instances)
    {
        if (inst.enable_vertex_blending && inst.mesh != nullptr)
        {
            inst.vertex_offset_in_flat_buffer = current_vertex_offset;
            current_vertex_offset += inst.mesh->mesh_vertex_count;
        }
    }

    dispatchSkinCompute(command_buffer, collected_instances);
    return true;
}
```

- [ ] **Step 6: Commit**

```bash
git add engine/source/runtime/function/render/passes/gpu_skinning_pass.h \
        engine/source/runtime/function/render/passes/gpu_skinning_pass.cpp
git commit -m "feat: add standalone GpuSkinningPass for GPU vertex skinning"
```

---

### Task 2: Remove skinning from `PathTracingPass`

**Files:**
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.h`
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`

**What to remove:**
- Member variables: `m_skin_compute_descriptor_set_layout`, `m_skin_compute_pipeline_layout`, `m_skin_compute_pipeline`, `m_skin_compute_descriptor_set`, `m_joint_matrix_buffer`, `m_joint_matrix_memory`, `m_joint_matrix_buffer_capacity`, `m_skinned_position_output_buffer`, `m_skinned_position_output_memory`, `m_skinned_position_output_capacity`
- Function declarations: `destroySkinComputeResources()`, `setupSkinComputePipeline()`, `ensureSkinBuffers()`, `uploadJointMatrices()`, `dispatchSkinCompute()`
- Function implementations: full bodies of all above functions
- Forward declaration: `struct RenderPathTracingCollectedInstance;` (no longer needed in header)
- In `buildTopLevelAS()`: remove `setupSkinComputePipeline()`, `uploadJointMatrices()`, `ensureSkinBuffers()`, `dispatchSkinCompute()` calls — these are now handled by `GpuSkinningPass`

**What to keep in `buildTopLevelAS()`:**
- `has_skinned` check (still needed for TLAS dirty logic)
- Per-instance BLAS construction from `resources.skinned_position_buffer` (the buffer was written by GpuSkinningPass)
- Orphan cleanup of `path_tracing_skinned_resources`
- The existing static mesh BLAS dedup logic

- [ ] **Step 1: Remove skinning members from `path_tracing_pass.h`**

Delete these lines from the class:
```
        // GPU skinning compute resources
        RHIDescriptorSetLayout* m_skin_compute_descriptor_set_layout {nullptr};
        ... (all 9 skinning members)
```
Delete these function declarations:
```
        void destroySkinComputeResources();
        bool setupSkinComputePipeline();
        bool ensureSkinBuffers(...);
        bool uploadJointMatrices(...);
        void dispatchSkinCompute(...);
```
Delete the forward declaration:
```
        struct RenderPathTracingCollectedInstance;
```

- [ ] **Step 2: Remove skinning function implementations from `path_tracing_pass.cpp`**

Delete the full implementations of:
- `PathTracingPass::destroySkinComputeResources()` (~40 lines)
- `PathTracingPass::setupSkinComputePipeline()` (~100 lines)
- `PathTracingPass::ensureSkinBuffers()` (~40 lines)
- `PathTracingPass::uploadJointMatrices()` (~50 lines)
- `PathTracingPass::dispatchSkinCompute()` (~130 lines)

- [ ] **Step 3: Simplify `buildTopLevelAS()`**

Remove these blocks from the function:
1. The `has_skinned` check with `setupSkinComputePipeline()` and `uploadJointMatrices()` — moved to GpuSkinningPass
2. The `ensureSkinBuffers()` call and vertex offset assignment — moved to GpuSkinningPass
3. The `dispatchSkinCompute()` call — moved to GpuSkinningPass

Keep:
1. Collection of instances via `collectPathTracingInstances()`
2. `has_skinned` computation for TLAS dirty logic
3. Static mesh BLAS ensure loop
4. TLAS dirty check (including `has_skinned`)
5. `updatePathTracingSceneBuffers()` call (fills `g_vertices`, includes placeholder vertices for skinned)
6. Per-instance BLAS build from `resources.skinned_position_buffer`
7. Orphan cleanup
8. Instance filtering and TLAS rebuild

The simplified `buildTopLevelAS()` flow:
```
1. collectPathTracingInstances()
2. Ensure BLAS for static meshes (per-mesh dedup)
3. Compute has_skinned flag
4. Check tlas_dirty (always true if has_skinned)
5. updatePathTracingSceneBuffers()
6. Build per-instance BLAS for skinned instances
7. Clean up orphaned per-instance resources
8. Filter null BLAS, assign shader indices
9. Rebuild TLAS
```

- [ ] **Step 4: Commit**

```bash
git add engine/source/runtime/function/render/passes/path_tracing_pass.h \
        engine/source/runtime/function/render/passes/path_tracing_pass.cpp
git commit -m "refactor: remove GPU skinning from PathTracingPass"
```

---

### Task 3: Wire `GpuSkinningPass` into the render pipeline

**Files:**
- Modify: `engine/source/runtime/function/render/render_pipeline_base.h`
- Modify: `engine/source/runtime/function/render/render_pipeline_base.cpp`
- Modify: `engine/source/runtime/function/render/render_pipeline.h`
- Modify: `engine/source/runtime/function/render/render_pipeline.cpp`

- [ ] **Step 1: Add `m_gpu_skinning_pass` to `RenderPipelineBase`**

In `engine/source/runtime/function/render/render_pipeline_base.h`, add alongside the other pass members:

```cpp
std::shared_ptr<class GpuSkinningPass> m_gpu_skinning_pass;
```

- [ ] **Step 2: Add `preparePassData()` call**

In `engine/source/runtime/function/render/render_pipeline_base.cpp`, in `RenderPipelineBase::preparePassData()`, add:

```cpp
m_gpu_skinning_pass->preparePassData(m_render_resource);
```

before the existing calls for other passes.

- [ ] **Step 3: Declare `initializeGpuSkinningPass()` in `RenderPipeline`**

In `engine/source/runtime/function/render/render_pipeline.h`:

```cpp
void initializeGpuSkinningPass();
```

- [ ] **Step 4: Instantiate and initialize in `RenderPipeline::initialize()`**

In `engine/source/runtime/function/render/render_pipeline.cpp`, in `RenderPipeline::initialize()`:

Add to pass instantiation section:
```cpp
m_gpu_skinning_pass = std::make_shared<GpuSkinningPass>();
```

Add to `setCommonInfo()` section:
```cpp
m_gpu_skinning_pass->setCommonInfo(pass_common_info);
```

Add initialization call (after all passes are created but before PathTracingPass):
```cpp
m_gpu_skinning_pass->initialize(nullptr);
m_gpu_skinning_pass->setup();
```

The `setup()` call creates the compute pipeline. It should be called during initialization, not during frame dispatch.

- [ ] **Step 5: Dispatch at frame start in `pathTracingRender()`**

In `engine/source/runtime/function/render/render_pipeline.cpp`, in `RenderPipeline::pathTracingRender()`, add as the very first step:

```cpp
void RenderPipeline::pathTracingRender()
{
    // NEW: GPU skinning runs first — produces skinned vertex data for path tracing
    m_gpu_skinning_pass->dispatch();

    // ... existing shadow pass dispatch ...
    DirectionalLightShadowPass::draw();
    PointLightShadowPass::draw();
    PathTracingPass::dispatch();
    MainCameraPass::drawPathTracing();
    // ...
}
```

- [ ] **Step 6: Include header in `render_pipeline.cpp`**

Add at the top of `render_pipeline.cpp`:
```cpp
#include "runtime/function/render/passes/gpu_skinning_pass.h"
```

- [ ] **Step 7: Commit**

```bash
git add engine/source/runtime/function/render/render_pipeline_base.h \
        engine/source/runtime/function/render/render_pipeline_base.cpp \
        engine/source/runtime/function/render/render_pipeline.h \
        engine/source/runtime/function/render/render_pipeline.cpp
git commit -m "feat: wire GpuSkinningPass into render pipeline"
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

## Verification Checklist (runtime)

1. Static scene (no skinned meshes): identical output, no performance regression
2. Scene with skinned mesh: mesh animates correctly in path tracing
3. `GpuSkinningPass::dispatch()` runs before `PathTracingPass::dispatch()`
4. No D3D12 validation errors
5. Per-instance BLAS built from pre-skinned positions
