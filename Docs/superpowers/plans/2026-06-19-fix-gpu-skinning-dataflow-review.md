# Review: Fix GPU Skinning Pass Data-Flow and Buffer Ownership (v3, round 2)

> Review of `docs/superpowers/plans/2026-06-19-fix-gpu-skinning-dataflow.md`
> Date: 2026-06-19
> Version reviewed: v3 after round-1 fixes applied

## Verdict

**Approved.** All four issues from the previous review are fixed. The design is now consistent across all three tasks: Task 1 handles binding/UAF/stride fixes cleanly, Task 2 creates per-instance position buffers (u6) plus a flat vertex buffer (u7), and Task 3 reads the flat buffer via `g_skinned_vertices` at t14. One minor gap remains (flat buffer allocation not shown). Two cosmetic text issues noted below.

---

## Fix Verification

| v3 Review Issue | Status | Detail |
|-----------------|--------|--------|
| **1. Flat vs per-instance buffer inconsistency** | ✅ Fixed | `SkinnedPathTracingResources` now only has `skinned_position_buffer` (Task 2 Step 1). u6 binds per-instance buffer at offset 0; u7 binds flat `m_skinned_vertex_output_buffer` at per-instance offset (Task 2 Step 3). Consistent throughout. |
| **2. `continue` too aggressive** | ✅ Fixed | Task 3 Step 2 uses `if (!is_skinned) { /* vertex push-back */ }` — all other records (geometry, material, instance, indices) created for both static and skinned instances. Only vertex data push-back is skipped. |
| **3. `vertex_offset` coordination** | ✅ Addressed | Both passes use `collectPathTracingInstances()` which iterates in deterministic order. Skinned instance geometry records get `vertex_offset` from GpuSkinningPass compute via the flat buffer layout — same accumulating offset pattern on both sides. |
| **4. t13 binding conflict with accumulation plan** | ✅ Fixed | HLSL code uses `register(t14, space0)` (line 383). |

---

## 🟢 Minor: Remaining Items

### Issue 1 — Comment on line 379 is stale

The comment says "at `t13`" and references "t1035", but the actual code on line 383 uses `t14`. The comment should be updated to match the code.

### Issue 2 — Step 3 comment on line 426 references "shifting existing binding 13 for `g_accumulation_prev` to 1035"

The reference to "1035" is a placeholder/typo. The sentence should simply state "adding binding 13 as the 14th entry in the descriptor set layout."

### Issue 3 — `m_skinned_vertex_output_buffer` allocation not shown

Task 2 Step 3 shows how to bind and write to `m_skinned_vertex_output_buffer` in the per-instance loop, but doesn't show the code that allocates/ensures this buffer before the loop. The buffer must be created (or recreated if total vertex count exceeds capacity) similar to the pattern in the original `ensureSkinBuffers()`.

The GpuSkinningPass should compute `total_skinned_vertices` before the dispatch loop, then ensure `m_skinned_vertex_output_buffer` has sufficient capacity:

```cpp
// Before the instance loop:
uint32_t total_vertices = 0;
for (const auto& inst : instances)
    if (inst.enable_vertex_blending && inst.mesh)
        total_vertices += inst.mesh->mesh_vertex_count;

if (total_vertices * sizeof(RenderPathTracingVertexGPUData) > m_skinned_vertex_output_capacity)
{
    // (re)create m_skinned_vertex_output_buffer
}
```

Add this explicitly to Task 2 Step 3 or as a separate step.

---

## Issue Summary

| # | Severity | Location | Summary |
|---|----------|----------|---------|
| 1 | 🟢 | Line 379 (comment) | Comment says `t13` but code says `t14` — stale text |
| 2 | 🟢 | Line 426 (comment) | "1035" is placeholder text |
| 3 | 🟢 | Task 2 Step 3 | `m_skinned_vertex_output_buffer` allocation/ensure code not shown |

---

## Recommendation

**Approve for implementation.** Issues 1-3 are cosmetic text fixes or missing-but-straightforward code that won't block implementation. All critical design issues are resolved. Task 1 is complete and can be implemented independently.
