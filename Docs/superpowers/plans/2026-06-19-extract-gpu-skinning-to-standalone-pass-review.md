# Review: Extract GPU Skinning to Standalone Pass

> Review of `docs/superpowers/plans/2026-06-19-extract-gpu-skinning-to-standalone-pass.md`
> Date: 2026-06-19

## Verdict

The architectural direction — extracting skinning into a standalone pass — is sound. However, **three critical data-flow and ordering bugs** mean the plan cannot work as written. These stem from the tight coupling between the compute dispatch and buffer upload that was embedded in `PathTracingPass::buildTopLevelAS()`, which the extraction breaks.

---

## 🔴 Critical Issues

### Issue 1 — Task 1 Step 5 / Task 3 Step 5: `vertex_offset_in_flat_buffer` computed in `GpuSkinningPass` but lost across pass boundary

In `GpuSkinningPass::dispatch()` (Step 5, lines 220–226):

```cpp
uint32_t current_vertex_offset = 0;
for (auto& inst : collected_instances)             // ← LOCAL variable
{
    if (inst.enable_vertex_blending && inst.mesh != nullptr)
    {
        inst.vertex_offset_in_flat_buffer = current_vertex_offset;
        current_vertex_offset += inst.mesh->mesh_vertex_count;
    }
}
dispatchSkinCompute(command_buffer, collected_instances);
```

`collected_instances` is a **local variable** allocated on the stack in `GpuSkinningPass::dispatch()`. It is destroyed when the function returns. When `PathTracingPass::buildTopLevelAS()` runs next, it calls `collectPathTracingInstances()` **again** (see [path_tracing_pass.cpp:777-778](engine/source/runtime/function/render/passes/path_tracing_pass.cpp#L777-L778)), producing a fresh vector with `vertex_offset_in_flat_buffer = 0` (the default).

**Impact:** The compute shader writes to correct offsets in the flat buffer (determined by GpuSkinningPass), but `PathTracingPass` has zero offsets and builds BLAS/geometry records pointing to wrong buffer regions.

**Required fix:** The collected instances must be shared between passes. Options:
- **(a)** Store the collected instances on `RenderResource` as a cached per-frame member (e.g., `m_cached_path_tracing_instances`)
- **(b)** Have `PathTracingPass::buildTopLevelAS()` accept a pre-collected instance vector instead of recollecting
- **(c)** Compute vertex offsets in `PathTracingPass` (where `updatePathTracingSceneBuffers()` runs and determines geometry layout), not in `GpuSkinningPass`

Option (c) is the simplest: keep offset computation in `PathTracingPass` (where it was in the original plan), and have `GpuSkinningPass` only handle the compute dispatch (which needs offsets set by PathTracingPass — creating a chicken-and-egg problem). This suggests the two passes are **not as separable as the plan assumes**.

---

### Issue 2 — Task 1 Step 5 / Task 3 Step 5: Compute writes to `g_vertices` are overwritten by `updatePathTracingSceneBuffers()`

The established execution order in the original GPU skinning plan:

```
updatePathTracingSceneBuffers()   ← CPU uploads data (incl. placeholders for skinned)
dispatchSkinCompute()              ← GPU overwrites placeholders with skinned data
build per-instance BLAS            ← reads skinned positions
path tracing dispatch              ← reads g_vertices (with skinned data)
```

The extraction plan inverts this for steps 1-2:

```
[GpuSkinningPass::dispatch()]
  dispatchSkinCompute()             ← writes to getPathTracingVertexBuffer()

[PathTracingPass::dispatch()]
  buildTopLevelAS()
    updatePathTracingSceneBuffers() ← CPU map/memcpy/unmap OVERWRITES compute results
```

At line 475 of `updatePathTracingSceneBuffers()` ([render_resource.cpp:465-473](engine/source/runtime/function/render/render_resource.cpp#L465-L473)), the `update_buffer` lambda calls:
```cpp
rhi->mapMemory(buffer_memory, 0, data_size, 0, &mapped_ptr);
std::memcpy(mapped_ptr, data, data_size);  // overwrites what compute just wrote
rhi->unmapMemory(buffer_memory);
```

The placeholder zero data for skinned meshes **destroys** the skinned vertex data the compute shader just wrote.

**Root cause:** The plan assumes `GpuSkinningPass` and `PathTracingPass` are independent, but they are tightly coupled — the compute shader writes to buffers whose layout is determined by the CPU-side scene buffer update. The skinning compute must run **after** the buffer update (to overwrite placeholders), not before.

**Required fix:** Reorder the operations. The correct frame ordering is:

```
1. All CPU-side buffer updates (updatePathTracingSceneBuffers, updateFrameData, etc.)
2. GPU skinning compute dispatch (overwrites placeholder regions)
3. UAV barrier
4. Per-instance BLAS build from skinned positions
5. TLAS rebuild
6. Path tracing dispatch
```

This could be achieved by:
- **(a)** Moving `updatePathTracingSceneBuffers()` to `GpuSkinningPass` (so it runs compute AFTER buffer upload)
- **(b)** Having `GpuSkinningPass::dispatch()` return early, with the actual compute dispatch triggered by `PathTracingPass` after buffer upload
- **(c)** Retaining the compute dispatch inside `PathTracingPass` (defeating the purpose of extraction)

---

### Issue 3 — Task 1 / Task 4 (original GPU skinning plan): No data transfer from flat position buffer to per-instance BLAS buffers

The compute shader writes skinned positions to `m_skinned_position_output_buffer` (a **flat, shared** buffer with per-instance offsets). However, `PathTracingPass::buildTopLevelAS()` builds per-instance BLAS from `resources.skinned_position_buffer` (a **per-instance** buffer, stored in `RenderMeshGPUResource::path_tracing_skinned_resources[instance_id]`).

These are **different buffers**:
- Flat buffer: `m_skinned_position_output_buffer` — compute shader UAV at u0
- Per-instance buffer: `resources.skinned_position_buffer` — BLAS geometry source

The status: Compute writes → flat buffer → (nothing copies data) → per-instance buffer → BLAS

There is no `cmdCopyBuffer` or equivalent to transfer skinned positions from the flat output to each instance's dedicated position buffer. The BLAS would be built from uninitialized memory.

This issue existed in the original GPU skinning plan and is inherited by the extraction. The extraction plan's architecture section claims "Skinned output is written directly into persistent buffers in `RenderMeshGPUResource::path_tracing_skinned_resources`" — but the actual code paths don't do this.

**Required fix:** Either:
- **(a)** Add `cmdCopyBuffer` calls after compute dispatch to copy data from flat buffer (at per-instance offsets) to per-instance buffers
- **(b)** Change the compute shader to write directly to per-instance buffers (bind different UAV per instance dispatch)
- **(c)** Use a single flat position buffer for BLAS (requires `RHIAccelerationStructureGeometryDesc::vertex_position_offset` support — currently absent per the original plan's design note)

---

## 🟡 Medium Issues

### Issue 4 — Task 1 Step 5: `collectPathTracingInstances()` called twice per frame for skinned scenes

`GpuSkinningPass::dispatch()` calls `collectPathTracingInstances()` (line 185), and `PathTracingPass::buildTopLevelAS()` calls it again (line 777). For scenes with skinned meshes, this duplicates all work: iterating entities, building instance records, deduplicating meshes.

**Impact:** Minor CPU overhead — doubled instance collection work for skinned scenes. For static scenes, `GpuSkinningPass` returns early (has_skinned = false, line 191), so no double collection occurs.

**Fix:** Share collected instances via a `RenderResource` member (same as Issue 1 fix).

---

### Issue 5 — Task 3 Step 1: Pointer type inconsistency with existing pass pattern

The plan declares:
```cpp
std::shared_ptr<class GpuSkinningPass> m_gpu_skinning_pass;
```

All existing passes in `RenderPipelineBase` ([render_pipeline_base.h:58-68](engine/source/runtime/function/render/render_pipeline_base.h#L58-L68)) use:
```cpp
std::shared_ptr<RenderPassBase> m_particle_pass;
std::shared_ptr<RenderPassBase> m_path_tracing_pass;
// ... all 9 passes use RenderPassBase
```

Using the concrete type `GpuSkinningPass` breaks the convention and requires a forward declaration of `GpuSkinningPass` in `render_pipeline_base.h`. The access pattern in `pathTracingRender()` should use `static_cast`:

```cpp
static_cast<GpuSkinningPass*>(m_gpu_skinning_pass.get())->dispatch();
```

**Fix:** Use `std::shared_ptr<RenderPassBase> m_gpu_skinning_pass;` to match existing conventions.

---

### Issue 6 — Task 1 Step 5: `dispatchSkinCompute` accesses `getPathTracingVertexBuffer()` before it may exist

On the first frame, `getPathTracingVertexBuffer()` returns a buffer created by `updatePathTracingSceneBuffers()`. But `GpuSkinningPass::dispatch()` runs BEFORE `PathTracingPass::buildTopLevelAS()` which calls `updatePathTracingSceneBuffers()`. If the vertex buffer hasn't been created yet (first frame), the compute shader would write to a null buffer.

This is a race between:
- `updatePathTracingSceneBuffers()` creating the buffer (in `PathTracingPass`)
- `dispatchSkinCompute()` writing to it (in `GpuSkinningPass`, which runs first)

**Fix:** Call `updatePathTracingSceneBuffers()` before `dispatchSkinCompute()`. This is the same ordering fix as Issue 2.

---

### Issue 7 — Task 2 Step 3: Simplified `buildTopLevelAS()` flow omits shader instance index assignment before early return

The original plan's `buildTopLevelAS()` has:
```
8. Filter null BLAS, assign shader indices
```

But per the [perf optimization plan](docs/superpowers/plans/2026-06-18-path-tracing-eliminate-per-frame-buffer-rebuild.md), the shader instance index assignment (lines 825–828 in [path_tracing_pass.cpp](engine/source/runtime/function/render/passes/path_tracing_pass.cpp#L824-L828)) runs before the TLAS dirty check. Step 3 of Task 2 doesn't explicitly state whether this assignment is kept in the correct position relative to the tlas_dirty early-return.

**Clarification needed:** The shader instance index assignment should remain before the early return (as in current code), or be moved inside the tlas_dirty block. The current behavior is safe (setting indices on a discarded local vector), and the plan should explicitly preserve or move it.

---

## 🟢 Minor Issues

### Issue 8 — Task 1 Step 2: `RenderPass::initialize(nullptr)` without init info

```cpp
void GpuSkinningPass::initialize(const RenderPassInitInfo* init_info)
{
    RenderPass::initialize(nullptr);
}
```

While nullptr is valid if no init info is needed (the pass doesn't require external image/view handles at init time), the plan should verify that `RenderPass::initialize(nullptr)` doesn't have side effects. Examining `RenderPass::initialize()` would clarify.

### Issue 9 — Task 1 Step 5: Missing `<algorithm>` include for `std::any_of`

Line 188 uses `std::any_of` but the plan doesn't show an `#include <algorithm>`. Minor — the existing includes may transitively provide it.

### Issue 10 — Task 4 Step 1: Build command uses `--config Debug`

For a compute shader pass, Release configuration is more representative for performance validation.

---

## Issue Summary

| # | Severity | Task | Summary |
|---|----------|------|---------|
| 1 | 🔴 Critical | Task 1/3 | `vertex_offset_in_flat_buffer` set on local vector, lost when PathTracingPass recollects |
| 2 | 🔴 Critical | Task 1/3 | Compute writes to g_vertices → overwritten by `updatePathTracingSceneBuffers()` map/memcpy |
| 3 | 🔴 Critical | Task 1 | No data transfer from flat position buffer to per-instance BLAS buffers |
| 4 | 🟡 Medium | Task 1 | `collectPathTracingInstances()` called twice per frame for skinned scenes |
| 5 | 🟡 Medium | Task 3 | `shared_ptr<GpuSkinningPass>` vs convention of `shared_ptr<RenderPassBase>` |
| 6 | 🟡 Medium | Task 1 | `getPathTracingVertexBuffer()` may not exist when compute shader runs (first frame) |
| 7 | 🟡 Medium | Task 2 | Shader instance index position relative to tlas_dirty early return not specified |
| 8 | 🟢 Minor | Task 1 | `RenderPass::initialize(nullptr)` needs verification |
| 9 | 🟢 Minor | Task 1 | Missing `<algorithm>` include |
| 10 | 🟢 Minor | Task 4 | Debug build config for performance validation |

---

## Recommendation

**Do not implement as written.** The three critical issues (1–3) indicate that `PathTracingPass::buildTopLevelAS()` is too tightly coupled to simply split the compute dispatch into a separate pass running beforehand. The correct frame ordering is:

```
updatePathTracingSceneBuffers → dispatchSkinCompute → per-instance BLAS → TLAS → trace
```

All of these share the same `collected_instances` data and the same GPU buffers. The extraction should either:

**Option A:** Make `GpuSkinningPass` responsible for the ENTIRE pre-trace pipeline (buffer update + compute + BLAS), leaving only TLAS build and trace dispatch in `PathTracingPass`. This is a larger refactor but cleanly separates concerns.

**Option B:** Keep the compute dispatch inside `PathTracingPass` (abandon extraction) and instead just extract the joint matrix upload to `GpuSkinningPass` (which has no ordering dependency). The compute pipeline remains in `PathTracingPass`.

**Option C:** Add an explicit dependency: `GpuSkinningPass` collects instances, sets offsets, and stores them on `RenderResource`. `PathTracingPass` reuses them (no recollect). `GpuSkinningPass::dispatch()` only uploads joint matrices (not dispatch compute). The actual compute dispatch is triggered by `PathTracingPass` after `updatePathTracingSceneBuffers()`. This keeps skinning data management in `GpuSkinningPass` but respects the ordering constraint.

**Recommendation: Option C** — it achieves the separation of concerns while respecting the tight ordering dependency between buffer update and skinning compute.
