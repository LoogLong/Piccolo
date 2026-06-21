# Review: Fix GPU Skinning Pass Data-Flow and Buffer Ownership

> Review of `docs/superpowers/plans/2026-06-19-fix-gpu-skinning-dataflow.md`
> Date: 2026-06-19

## Verdict

The plan correctly identifies and structurally fixes the three critical dataflow issues from the extraction review. The architectural changes — caching instances on `RenderResource`, moving `updatePathTracingSceneBuffers()` into `GpuSkinningPass`, and binding per-instance position buffers directly at u0 — are the right solutions. However, **two new critical bugs are introduced** (constant buffer use-after-free and persistent binding number conflicts), and two pre-existing issues from the original GPU skinning plan review remain unfixed.

---

## 🔴 Critical Issues

### Issue 1 — Task 2 Step 3 lines 281–345: Constant buffer created and destroyed within same CPU call — GPU use-after-free

The `dispatchSkinCompute()` function creates a 16-byte constant buffer for `SkinComputeConstants`, uses it in a descriptor write, and then **destroys it immediately after `cmdDispatch()`**:

```cpp
// Line 283-287: CREATE
m_rhi->createBuffer(16, RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    constants_buffer, constants_memory);

// ... setup constants, map/memcpy/unmap ...

// Line 341-342: DISPATCH
m_rhi->cmdDispatch(command_buffer, group_count, 1, 1);

// Line 344-345: DESTROY — ON CPU TIMELINE
m_rhi->destroyBuffer(constants_buffer);
m_rhi->freeMemory(constants_memory);
```

D3D12 command execution is **deferred**. `cmdDispatch()` records the command into the command buffer — the GPU has not executed it yet. `destroyBuffer()` on the CPU timeline frees the resource immediately. When the GPU later processes the command buffer, it accesses a freed buffer → **undefined behavior, TDR, or validation error**.

This pattern also creates and destroys a buffer **per skinned instance per frame**. With multiple skinned instances, this means N buffer allocations and N frees per frame — unsustainable GPU memory churn.

**Required fix:** Replace with one of:
- **(a)** A persistent small constant buffer (allocated once, mapped+written per dispatch, never destroyed until pass teardown)
- **(b)** A ring-buffer pattern (allocate one large buffer, write to next available slot each dispatch)
- **(c)** Use push constants if supported (most efficient, avoids descriptor writes entirely)

The raster pipeline uses a ring-buffer for per-drawcall constant data ([main_camera_pass.cpp:2538-2544](engine/source/runtime/function/render/passes/main_camera_pass.cpp#L2538-L2544)) — follow that pattern.

---

### Issue 2 — Task 2 Step 3 lines 220–329: Descriptor set binding number conflicts still present

The descriptor writes have duplicate `dstBinding` values:

```cpp
writes[0].dstBinding = 0;   // t0
writes[1].dstBinding = 1;   // t1
writes[2].dstBinding = 2;   // t2
writes[3].dstBinding = 3;   // t3
writes[4].dstBinding = 4;   // t4
writes[5].dstBinding = 0;   // b0   ← DUPLICATE of binding 0!
writes[6].dstBinding = 0;   // u0   ← DUPLICATE of binding 0!
writes[7].dstBinding = 1;   // u1   ← DUPLICATE of binding 1!
```

This is the same Issue 1 from the [original GPU skinning plan review](docs/superpowers/plans/2026-06-18-path-tracing-gpu-skinning-review.md). The `setupSkinComputePipeline()` layout (ported from the original plan) has matching duplicate binding numbers. This is illegal — each binding within a descriptor set must be unique.

**Required fix:** Use sequential unique binding numbers 0–7:

```cpp
writes[0].dstBinding = 0;  // t0
writes[1].dstBinding = 1;  // t1
writes[2].dstBinding = 2;  // t2
writes[3].dstBinding = 3;  // t3
writes[4].dstBinding = 4;  // t4
writes[5].dstBinding = 5;  // b0  (was 0)
writes[6].dstBinding = 6;  // u0  (was 0)
writes[7].dstBinding = 7;  // u1  (was 1)
```

Apply the same fix to `setupSkinComputePipeline()` layout bindings. Add `[[vk::binding(N)]]` annotations to the HLSL shader for explicit mapping.

---

### Issue 3 — Task 2 Step 3 line 347: Joint matrix stride uses actual `joint_count`, not `MAX_JOINTS`

```cpp
joint_matrix_offset += inst.joint_count;
```

This is the same Issue 2 from the [original GPU skinning plan review](docs/superpowers/plans/2026-06-18-path-tracing-gpu-skinning-review.md). The raster pipeline stores joint matrices in the GPU buffer with `s_mesh_vertex_blending_max_joint_count * instance_index` stride ([main_camera_pass.cpp:2568](engine/source/runtime/function/render/passes/main_camera_pass.cpp#L2568)):

```cpp
.joint_matrices[s_mesh_vertex_blending_max_joint_count * i + j] =
```

The HLSL compute shader accesses via `g_constants.joint_matrix_offset + joint_idx`. If the stride is `inst.joint_count` (e.g., 128) instead of `MAX_JOINTS` (1024), instance 1's matrices land at index 128 instead of 1024 — the compute shader reads wrong matrices for all subsequent instances.

**Required fix:**

```cpp
joint_matrix_offset += s_mesh_vertex_blending_max_joint_count;  // not inst.joint_count
```

Also update `uploadJointMatrices()` to use the same stride when copying joint matrix data to the GPU buffer.

---

## 🟡 Medium Issues

### Issue 4 — Task 3 Step 5: No error propagation from `GpuSkinningPass` to `PathTracingPass`

In `render_pipeline.cpp`, the sequence is:

```cpp
m_gpu_skinning_pass->dispatch();   // return value NOT checked!
// ...
PathTracingPass::dispatch();       // runs regardless of GpuSkinningPass failure
```

If `updatePathTracingSceneBuffers()` fails in `GpuSkinningPass` (returns false), the vertex/index/material buffers are not created/updated. `PathTracingPass` then tries to use null or stale buffers, leading to a crash or D3D12 validation error.

**Required fix:** Check the return value and skip path tracing if skinning failed:

```cpp
if (!m_gpu_skinning_pass->dispatch())
{
    return false;
}
```

---

### Issue 5 — Task 2 Step 5: `getCachedPathTracingInstances()` returns `const&`, but `PathTracingPass` may need mutable elements

The plan's Step 5 shows `PathTracingPass::buildTopLevelAS()` doing:

```cpp
std::vector<RenderPathTracingCollectedInstance> collected_instances =
    m_render_resource_impl->getCachedPathTracingInstances();
```

This performs a **copy** of the entire vector (copy-assignment from const reference). For large scenes with many instances, this is reasonable (the original `collectPathTracingInstances()` also allocated a fresh vector). However, the plan should explicitly note that this is a copy, not a reference, since PathTracingPass modifies `collected_instances` (sets `bottom_level_as`, `shader_instance_index`, etc.). Using a non-const accessor that returns by value and clears the cache would avoid the copy:

```cpp
std::vector<RenderPathTracingCollectedInstance> takeCachedPathTracingInstances()
{
    return std::move(m_cached_path_tracing_instances);
}
```

---

### Issue 6 — Task 2 Step 3: Per-instance buffer creation in both `GpuSkinningPass` and `PathTracingPass`

`GpuSkinningPass::dispatchSkinCompute()` creates `resources.skinned_position_buffer` at lines 197–215. `PathTracingPass::buildTopLevelAS()` (from the original plan) also contains identical creation logic for the same buffer. Since `GpuSkinningPass` runs first and creates the buffer, `PathTracingPass` finds it already exists and skips creation. But the duplicate code is a maintenance hazard — if creation parameters diverge, the two passes will disagree on buffer properties.

**Recommendation:** Remove the buffer creation from `PathTracingPass` and keep it only in `GpuSkinningPass`. `PathTracingPass` should assert that the buffer exists (since GpuSkinningPass ran first).

---

### Issue 7 — Task 1 Step 1: `getCachedPathTracingInstances()` returns reference to potentially stale data

If `GpuSkinningPass::dispatch()` fails or is never called (e.g., first frame initialization, or raster-only scene), `m_cached_path_tracing_instances` is empty. `PathTracingPass` would get an empty vector and either:
- Build TLAS with zero instances (harmless — returns false)
- Or crash during per-instance BLAS construction

The plan should add a safety check: `PathTracingPass` should fall back to `collectPathTracingInstances()` if the cache is empty, or `GpuSkinningPass` should always populate the cache (even for non-skinned scenes, as the plan does at line 128).

Looking at Task 2 Step 2 line 127–129:
```cpp
if (!has_skinned)
{
    m_render_resource_impl->setCachedPathTracingInstances(std::move(collected_instances));
    return true;
}
```

The cache IS populated for non-skinned scenes. ✓ But if `GpuSkinningPass` is skipped entirely (e.g., path tracing not active), the cache is empty.

---

## 🟢 Minor Issues

### Issue 8 — Task 2 Step 2 line 276–278: `inst.vertex_offset_in_flat_buffer` used in `dispatchSkinCompute()` but offset calculation runs in parallel `for` loop

At line 151–159, offsets are computed in a separate loop, then stored on `collected_instances`. Then `dispatchSkinCompute()` reads them (line 290). Since the offset calculation loop runs before `dispatchSkinCompute()`, and the vector is the same mutable reference, the offsets are available. ✓ The plan just needs to ensure `dispatchSkinCompute` uses `inst.vertex_offset_in_flat_buffer` from the instance reference, not from the local counter.

### Issue 9 — Task 3 Step 1: Build command uses `--config Debug`

For a GPU compute pass, Release configuration is more representative for performance validation.

### Issue 10 — Task 2 Step 3 line 322: `getPathTracingVertexBuffer()` accessed via `m_render_resource_impl`

The `getPathTracingVertexBuffer()` ([render_resource.h:217](engine/source/runtime/function/render/render_resource.h#L217)) returns `m_path_tracing_vertex_buffer`, which is created by `updatePathTracingSceneBuffers()`. Since Step 2 reorders the flow to call `updatePathTracingSceneBuffers()` before `dispatchSkinCompute()`, the buffer exists by the time it's accessed. ✓

---

## Architecture Validation

| Fix Claim | Target Issue | Verdict |
|-----------|-------------|---------|
| Cache instances on RenderResource | vertex_offset lost across passes | ✅ Correct — `setCachedPathTracingInstances()` after offsets computed, `getCachedPathTracingInstances()` in PathTracingPass |
| Reorder: buffer update before compute | Compute output overwritten | ✅ Correct — `updatePathTracingSceneBuffers()` at Step 4, `dispatchSkinCompute()` at Step 6 |
| Per-instance u0 binding | Flat→per-instance buffer gap | ✅ Correct — `resources.skinned_position_buffer` bound directly, offset 0 |
| Remove `collectPathTracingInstances()` from PathTracingPass | Double collection | ✅ Correct — replaced with cache read |
| Remove `updatePathTracingSceneBuffers()` from PathTracingPass | Compute-before-buffer-update ordering | ✅ Correct — moved to GpuSkinningPass |

---

## Issue Summary

| # | Severity | Task | Summary |
|---|----------|------|---------|
| 1 | 🔴 Critical | Task 2 Step 3 | Constant buffer created and destroyed within same CPU call — GPU use-after-free per instance |
| 2 | 🔴 Critical | Task 2 Step 3 | Descriptor binding number conflicts (0,0,0,1) — same as original GPU skinning Issue 1 |
| 3 | 🔴 Critical | Task 2 Step 3 | Joint matrix stride uses `inst.joint_count` instead of `s_mesh_vertex_blending_max_joint_count` |
| 4 | 🟡 Medium | Task 3 Step 5 | `GpuSkinningPass::dispatch()` return value not checked before running PathTracingPass |
| 5 | 🟡 Medium | Task 2 Step 5 | Cache accessor returns const ref; PathTracingPass copies entire vector (acceptable but could be move) |
| 6 | 🟡 Medium | Task 2 Step 3 | Per-instance buffer creation duplicated in both passes |
| 7 | 🟡 Medium | Task 1 Step 1 | Cache may be empty if GpuSkinningPass never runs |
| 8 | 🟢 Minor | Task 2 | Offset loop ordering — verified correct |
| 9 | 🟢 Minor | Task 3 | Debug config for build verification |
| 10 | 🟢 Minor | Task 2 | Buffer access via m_render_resource_impl — verified safe |

---

## Recommendation

**Fix Issues 1, 2, and 3 before implementation.** Issue 1 (constant buffer use-after-free) is a new bug introduced by this plan and would cause GPU crashes or validation errors. Issues 2 and 3 are pre-existing bugs from the original GPU skinning plan that this plan inherits — fix them now rather than deferring further.

The architectural direction is correct — the three core dataflow fixes (caching, reordering, per-instance u0 binding) properly resolve the problems identified in the extraction review.
