# Review: Fix Path Tracing Pipeline Bugs & Redundancy

> Review of `docs/superpowers/plans/2026-06-23-fix-path-tracing-bugs-and-redundancy.md`
> Date: 2026-06-23

## Verdict

**Approved.** Bug #3 (instance buffer / TLAS index mismatch) is a genuine critical bug — confirmed by tracing the code timeline. Bug #4 (dead `shader_instance_index`) is confirmed. All five tasks are correct and have no dependency conflicts. R2 is noted in the catalog but has no fix task — intentional scope limitation.

---

## Bug #3: Instance buffer / TLAS index mismatch — ✅ Confirmed

**Evidence chain:**

The path tracing shader reads instance data using DXR `InstanceIndex()` ([path_tracing.lib.hlsl:141-142](engine/shader/hlsl/path_tracing.lib.hlsl#L141-L142)):

```hlsl
const uint instance_index = min(InstanceIndex(), safe_instance_count - 1u);
const PathTracingInstanceData instance_data = g_instances[instance_index];
```

The TLAS build order ([path_tracing_pass.cpp:949-958](engine/source/runtime/function/render/passes/path_tracing_pass.cpp#L949-L958)) iterates `collected_instances` AFTER null-BLAS filtering at line 923. DXR assigns `InstanceIndex()` in TLAS build order — i.e., the **filtered** order.

But `g_instances` is populated by `updatePathTracingSceneBuffers()` at **line 837**, which receives the **unfiltered** `collected_instances` list. This runs BEFORE the filter at line 923.

**Concrete example:**
```
collected_instances = [StaticA, SkinnedB(BLAS=null), StaticC]

Line 837 → g_instances[0]=A_data, g_instances[1]=B_data, g_instances[2]=C_data
Line 923 → filter out SkinnedB → [StaticA, StaticC]
Line 949 → TLAS: InstanceIndex 0=StaticA, InstanceIndex 1=StaticC

Shader hit on TLAS instance 1 (StaticC):
  InstanceIndex() = 1
  g_instances[1] = B_data  ← WRONG! Should be C_data
```

**Impact:** Any instance with a null BLAS at filter time (skinned mesh BLAS not ready, static mesh BLAS build failure) causes ALL subsequent instances in the TLAS to read wrong geometry, material, and flags — producing garbled rendering or crashes.

**Fix verification:** Moving `updatePathTracingSceneBuffers()` after the filter ensures `g_instances[i]` corresponds to TLAS `InstanceIndex() == i`. ✅

---

## Bug #4: `shader_instance_index` dead code — ✅ Confirmed

Search across entire engine directory: `shader_instance_index` appears in exactly two places:
1. [render_resource.h:33](engine/source/runtime/function/render/render_resource.h#L33): struct field declaration
2. [path_tracing_pass.cpp:942](engine/source/runtime/function/render/passes/path_tracing_pass.cpp#L942): assignment in loop

Zero reads anywhere. Dead code. ✅

---

## R1: Reduce 7→4 passes — ✅ Correct

The merged passes are valid:

**Pass 1+2 merge (has_skinned into static BLAS loop):** `has_skinned` is tracked during the same loop that ensures static BLAS. Since `has_skinned` is used immediately after the loop (for `tlas_dirty`), and the loop runs before the tlas_dirty check, the timing is correct. ✅

**Pass 3+4 merge (per-instance BLAS + orphan cleanup):** Meshes are tracked during the BLAS loop into `skinned_meshes_in_frame`. Orphan cleanup then iterates only those tracked meshes instead of re-scanning `collected_instances`. No correctness change — same cleanup logic, fewer iterations. ✅

**Pass 6 (shader_instance_index):** Removed entirely by Task 2. ✅

---

## R7: Redundant null check — ✅ Safe to remove

First block at lines 580-596 populates `m_specular_texture_view` and `m_linear_sampler` when either is null. After this block, both are either populated (from IBL resource) or remain null (IBL resource not initialized — an engine startup failure). The second check at lines 598-601 catches the IBL-not-initialized edge case. Since IBL is initialized at engine startup and never changes, this edge case can only occur on the first frame — and by then IBL is always ready. Safe to remove. ✅

---

## R3: Partial buffer rebuild — ✅ Reasonable

The optimization adds a lightweight `updatePathTracingInstanceBuffer()` for the case where only transforms changed. The plan correctly notes this requires additional implementation and lists it last in priority. The approach (tracking `rebuild_scene_buffers` separately from TLAS dirty) is sound. ✅

---

## Omissions

| Item | Detail |
|------|--------|
| R2 (15-descriptor rewrite) | Acknowledged in problem catalog but has no fix task. Acceptable scope limitation. |
| R4-R6, R8 | Not listed in catalog. The plan covers only selected redundancy items. |

---

## Issue Summary

| # | Severity | Summary |
|---|----------|---------|
| — | — | **No issues found.** All bugs confirmed genuine. All fixes verified correct. |

---

## Recommendation

**Approve and implement in priority order.** Task 1 (Bug #3) should be implemented first — it's a critical correctness fix. Tasks 2 and 4 are safe one-line removals. Task 3 (pass merging) depends on Task 1+2 being done first for a clean diff. Task 5 (partial rebuild) is the most complex and lowest priority.
