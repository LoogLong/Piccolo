# Review: Fix GPU Skinning Pass Data-Flow and Buffer Ownership (v3)

> Review of `docs/superpowers/plans/2026-06-19-fix-gpu-skinning-dataflow.md`
> Date: 2026-06-19
> Version reviewed: v3 — complete decoupling via per-instance buffers

## Verdict

The v3 architectural direction — zero-coupling via per-instance buffers — is the correct long-term solution. Task 1 (binding/UAF/stride fixes) is clean and complete. However, **Task 2 and Task 3 contradict each other** on the vertex buffer design, and **Task 3 Step 2's `continue` is too aggressive** — it breaks shader-side instance lookup.

---

## 🔴 Critical: Design Inconsistency Between Tasks 2 and 3

### Task 2 Step 3 creates per-instance `skinned_vertex_buffer`

Lines 242–247, 276–281:

```cpp
// u7: skinned vertex data → per-instance buffer, offset 0
skinned_vertices_info.buffer = resources.skinned_vertex_buffer;
skinned_vertices_info.offset = 0;
```

```cpp
// Create per-instance vertex buffer
m_rhi->createBuffer(vertex_count * sizeof(RenderPathTracingVertexGPUData), ...,
                    resources.skinned_vertex_buffer, resources.skinned_vertex_memory);
```

### Task 3 Step 4 chooses flat `m_skinned_vertex_output_buffer` (Option B)

Line 446–454:

> Choose Option B — simpler, no extra copy. The flat g_skinned_vertices buffer is treated like g_vertices but only contains skinned instances.

```cpp
// In GpuSkinningPass::dispatchSkinCompute(), u7 binds the flat output buffer at the instance's offset:
skinned_vertices_info.buffer = m_skinned_vertex_output_buffer;
skinned_vertices_info.offset = skinned_vertex_offset * sizeof(RenderPathTracingVertexGPUData);
```

**These are incompatible.** Task 2 creates N per-instance buffers with u7 at offset 0. Task 3 wants one flat buffer with u7 at per-instance offsets. The path tracing shader (Step 3) uses a single `g_skinned_vertices` binding, which requires a flat buffer, not per-instance buffers.

**Required fix:** Align the two tasks. Recommended approach (matching Option B):

- Task 2 Step 1: Remove `skinned_vertex_buffer` / `skinned_vertex_memory` from `SkinnedPathTracingResources` — only keep `skinned_position_buffer` (needed for BLAS)
- Task 2 Step 3: u7 binds `m_skinned_vertex_output_buffer` at per-instance offsets (NOT per-instance `resources.skinned_vertex_buffer`)
- Task 2 Step 3: Remove creation of per-instance `skinned_vertex_buffer` — only create `skinned_position_buffer`
- GpuSkinningPass owns `m_skinned_vertex_output_buffer`; PathTracingPass reads it via `RenderResource` getter (as described in Task 3 Step 4)

---

## 🟡 Medium: Task 3 Step 2 — `continue` is too aggressive, breaks shader lookup

The proposed fix at line 371–373:

```cpp
if (source_instance.enable_vertex_blending)
{
    continue;
}
```

This skips **everything** for skinned instances — geometry records, material records, instance records, index data, vertex data. But the path tracing shader needs all of these EXCEPT vertex data:

| Data | Needed for skinned? | Reason |
|------|--------------------|--------|
| Geometry record (`vertex_offset`, `index_offset`, `index_count`) | ✅ | Shader uses `geometry_data.vertex_offset + local_i0` to index into `g_skinned_vertices`, and `geometry_data.index_offset + primitive_index * 3u` for indices |
| Index data (`g_indices`) | ✅ | Local vertex offsets (0..N-1) are valid for both static and skinned |
| Material record | ✅ | Same material system for all instances |
| Instance record (`geometry_index`, `material_index`, `flags`) | ✅ | Flags bit 0 tells shader to use `g_skinned_vertices` |
| Vertex data (`g_vertices`) | ❌ | Skinned vertex data is in `g_skinned_vertices` |
| Texture views | ✅ | Same texture system |

With the current `continue`, skinned instances have no geometry/material/instance records, so the shader cannot find them at all.

**Required fix:** Replace `continue` with a targeted skip — only skip the vertex data push-back loop (the `for (uint32_t v = 0; ...)` block that creates `RenderPathTracingVertexGPUData` and pushes to `m_path_tracing_vertex_data`). Still create geometry records (with vertex_offset pointing into the skinned flat buffer), material records, instance records (with flags bit 0 set), and append index data.

**Important:** For skinned instances, `geometry_data.vertex_offset` must be the correct offset within the flat `g_skinned_vertices` buffer. Since GpuSkinningPass writes skinned vertices sequentially with per-instance offsets, and PathTracingPass independently manages the geometry records, these offsets must match. Currently there's no coordination mechanism — each pass collects instances independently and computes its own offsets. The offsets must be deterministic (based on mesh vertex counts in collection order) to match.

---

## 🟡 Medium: `geometry_data.vertex_offset` for skinned instances — no coordination between passes

The path tracing shader (Task 3 Step 3) reads skinned vertices as:

```hlsl
v0 = g_skinned_vertices[geometry_data.vertex_offset + local_i0];
```

`geometry_data.vertex_offset` is set by `updatePathTracingSceneBuffers()` in `PathTracingPass`. But `g_skinned_vertices` is populated by `GpuSkinningPass`. These two offsets must match — the vertex_offset stored in the geometry record must equal the offset at which GpuSkinningPass wrote that instance's vertex data in the flat buffer.

Since both passes independently call `collectPathTracingInstances()` and iterate in the same order (sorted by mesh/material), the offsets WILL match as long as both passes:
1. Collect instances in the same order
2. Use the same vertex count per mesh
3. Compute offsets using the same accumulation logic

The plan should explicitly note this requirement. If `collectPathTracingInstances()` returns instances in a different order between the two passes, the offsets will mismatch.

**Recommendation:** Add a comment in both `GpuSkinningPass::dispatch()` and `PathTracingPass::updatePathTracingSceneBuffers()` documenting that the offset computation must be kept in sync. Alternatively, have `updatePathTracingSceneBuffers()` compute the skinned vertex offsets based on the order it processes instances (which matches the order the compute shader will write them).

---

## 🟡 Medium: `g_skinned_vertices` binding slot conflicts with accumulation plan

The plan proposes binding `t13` for `g_skinned_vertices` (Task 3 Step 3, line 385). The [accumulation plan](docs/superpowers/plans/2026-06-17-path-tracing-perf-correctness.md) also proposes `t13` for `g_accumulation_prev`. If both features are implemented, they will conflict.

**Fix:** Use `t14` for `g_skinned_vertices` (or coordinate binding assignments across plans). The plan's comment "before g_accumulation_prev at t1035" appears to be a typo — `t1035` is not a valid HLSL register.

---

## 🟢 Minor: HLSL `[[vk::binding(N)]]` annotations not shown

Task 1 Step 1 only changes HLSL register assignments (b0→b5 etc.) but doesn't add `[[vk::binding(N)]]` annotations. These are optional for D3D12-only (which path tracing currently is, per the backend guard) but good practice for future Vulkan support.

---

## What's Fixed (vs v2)

| Fix | Status |
|-----|--------|
| (A) Constant buffer UAF | ✅ Step 5: persistent `m_skin_constants_buffer`, map per dispatch |
| (B) Descriptor binding conflicts | ✅ Steps 1-3: HLSL + layout + writes all updated to 5/6/7 |
| (C) Joint matrix stride | ✅ Step 4: `s_mesh_vertex_blending_max_joint_count` in both dispatch and upload |
| (D) Duplicate buffer creation | ✅ Not applicable in v3 architecture |
| Error propagation | ✅ Not needed in v3 — passes are independent |
| Instance cache | ✅ Removed — passes are independently collected |

---

## Issue Summary

| # | Severity | Location | Summary |
|---|----------|----------|---------|
| 1 | 🔴 | Task 2 Step 3 vs Task 3 Step 4 | Per-instance `skinned_vertex_buffer` (Task 2) contradicts flat `m_skinned_vertex_output_buffer` (Task 3). Compute shader u7 can't bind both simultaneously. |
| 2 | 🟡 | Task 3 Step 2 | `continue` skips geometry/material/instance record creation — shader can't find skinned instances |
| 3 | 🟡 | Task 3 Steps 2-3 | `geometry_data.vertex_offset` set by PathTracingPass must match GpuSkinningPass's write offset — no coordination mechanism |
| 4 | 🟡 | Task 3 Step 3 | `t13` conflicts with accumulation plan's `g_accumulation_prev` |
| 5 | 🟢 | Task 1 Step 1 | No `[[vk::binding(N)]]` annotations |

---

## Recommendation

Fix Issues 1 and 2 before implementation:

1. **Resolve the flat vs per-instance vertex buffer inconsistency** — remove `skinned_vertex_buffer` from `SkinnedPathTracingResources`; use flat `m_skinned_vertex_output_buffer` in `GpuSkinningPass` with per-instance offsets; expose via `RenderResource` getter for `PathTracingPass`
2. **Fix the `continue` in `updatePathTracingSceneBuffers()`** — create geometry/material/instance records + append indices for skinned instances; only skip vertex data push-back

The Task 1 fixes (binding conflicts, constant UAF, joint stride) are complete and ready to implement independently.
