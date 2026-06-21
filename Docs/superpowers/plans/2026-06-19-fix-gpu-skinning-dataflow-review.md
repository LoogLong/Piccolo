# Review: Fix GPU Skinning Pass Data-Flow and Buffer Ownership (v2)

> Review of `docs/superpowers/plans/2026-06-19-fix-gpu-skinning-dataflow.md`
> Date: 2026-06-19
> Version reviewed: updated after first review round

## Verdict

The architectural fixes (caching, reordering, per-instance u0 binding) are correct. The plan's English preamble in Task 2 correctly declares four bug fixes. **However, the code in Task 2 Step 5 was not updated to match those declarations** — it still contains the original buggy code for three of the four fixes. The preamble says one thing; the code does another.

---

## English Declarations vs. Actual Code

Task 2 opens with four fix declarations (lines 86–94):

| Fix | Preamble claims | Code in Step 5 actually has |
|-----|----------------|---------------------------|
| **(A)** Constant buffer UAF | "persistent `m_skin_constants_buffer` allocated once during `setup()`" | Lines 339–345 + 402–403: `createBuffer(16)` → `destroyBuffer()` **every dispatch** using local variables `constants_buffer` / `constants_memory` — `m_skin_constants_buffer` is never used |
| **(B)** Binding conflicts | "`{0,1,2,3,4,5,6,7}`" | Lines 361/373/385: `dstBinding` still **0, 0, 1** (not 5, 6, 7) |
| **(C)** Joint matrix stride | "`s_mesh_vertex_blending_max_joint_count` (1024)" | Line 405: still `inst.joint_count` |
| **(D)** Duplicate buffer creation | "removed from PathTracingPass" | ✅ Step 7 correctly replaces with `continue` |

The English descriptions are correct about **what** to fix. The Step 5 code still contains the pre-fix version.

---

## Fix-by-Fix Analysis

### Fix (A): Constant buffer use-after-free → ❌ Code NOT updated

**What the preamble says:**

> The per-dispatch `createBuffer(16) → use → destroyBuffer()` pattern is replaced with a single persistent `m_skin_constants_buffer` allocated once during `setup()`. Each dispatch maps+writes+unmaps it. Never destroyed until pass teardown.

**What the code does (lines 339–353 + 402–403):**

```cpp
// Line 339-340: local variables, not m_skin_constants_buffer
RHIBuffer*       constants_buffer = nullptr;
RHIDeviceMemory* constants_memory = nullptr;
{
    // Line 342-345: CREATES a new buffer every dispatch
    m_rhi->createBuffer(16, ..., constants_buffer, constants_memory);
    // ... map, memcpy, unmap ...
}
// ... descriptor write + dispatch ...

// Line 402-403: DESTROYS immediately after cmdDispatch (GPU hasn't executed yet)
m_rhi->destroyBuffer(constants_buffer);
m_rhi->freeMemory(constants_memory);
```

The code creates a **local** `constants_buffer` (not the member `m_skin_constants_buffer`), allocates it fresh each dispatch, and destroys it immediately after recording the dispatch command. On the D3D12 GPU timeline, the buffer is destroyed before the GPU reads it — use-after-free, TDR, or validation error.

**Required code change:** Allocate `m_skin_constants_buffer` once in `setup()` (or lazily on first use). In `dispatchSkinCompute()`, map the persistent buffer, write constants, unmap — no create/destroy.

---

### Fix (B): Descriptor binding conflicts → ❌ Code partially updated

**What the preamble says:**

> Changed from `{0,1,2,3,4,0,0,1}` to `{0,1,2,3,4,5,6,7}`. The HLSL shader is updated to use explicit `[[vk::binding(N)]]` layout. The descriptor set layout uses the same unique bindings.

**What was actually changed:**

| Component | Fixed? | Detail |
|-----------|--------|--------|
| HLSL registers (Step 1) | ✅ | `b0→b5`, `u0→u6`, `u1→u7` |
| Layout bindings (Step 3, text only) | ✅ | Described as `bindings[5].binding = 5;` etc. |
| Descriptor writes (Step 5 code) | ❌ | Still old values |

**Lines 361, 373, 385 still have:**

```cpp
writes[5].dstBinding = 0;   // should be 5  — b5 in HLSL
writes[6].dstBinding = 0;   // should be 6  — u6 in HLSL
writes[7].dstBinding = 1;   // should be 7  — u7 in HLSL
```

With the new HLSL registers (`b5=5`, `u6=6`, `u7=7`), these `dstBinding` values must match. 0 and 1 would write to the wrong descriptor slots.

**Required code change:** `writes[5].dstBinding = 5;`, `writes[6].dstBinding = 6;`, `writes[7].dstBinding = 7;`

---

### Fix (C): Joint matrix stride → ❌ Code NOT updated

**What the preamble says:**

> `dispatchSkinCompute()` uses `joint_matrix_offset += s_mesh_vertex_blending_max_joint_count` (1024) instead of `inst.joint_count`. `uploadJointMatrices()` is also fixed.

**What the code does (line 405):**

```cpp
joint_matrix_offset += inst.joint_count;
```

The raster pipeline stores joint matrices with `MAX_JOINTS` stride per instance ([main_camera_pass.cpp:2568](engine/source/runtime/function/render/passes/main_camera_pass.cpp#L2568)):
```cpp
.joint_matrices[s_mesh_vertex_blending_max_joint_count * i + j] =
```

And the HLSL accessor uses the same stride ([mesh.vert.hlsl:22](engine/shader/hlsl/mesh.vert.hlsl#L22)):
```hlsl
uint joint_base = M_MESH_VERTEX_BLENDING_MAX_JOINT_COUNT * instance_id;
```

`s_mesh_vertex_blending_max_joint_count` is **1024** ([render_common.h:20](engine/source/runtime/function/render/render_common.h#L20)). If instance 0 has 128 joints, `inst.joint_count = 128` places instance 1's matrices at index 128. The shader expects them at index 1024. Result: wrong joint matrices for every instance after the first.

**Required code change:**
```cpp
joint_matrix_offset += s_mesh_vertex_blending_max_joint_count;
```
Same fix needed in `uploadJointMatrices()` — pack matrices with MAX_JOINTS stride per instance.

---

### Fix (D): Duplicate per-instance buffer creation → ✅ Code correct

Step 7 correctly replaces the duplicate creation with an existence check:

```cpp
if (resources.skinned_position_buffer == nullptr || resources.vertex_count != vertex_count)
{
    continue; // GpuSkinningPass should have created this; skip if not ready
}
```

---

## Other Fixes: All Correct

| Item | Status | Detail |
|------|--------|--------|
| `takeCachedPathTracingInstances()` with `std::move` | ✅ | Step 1 — avoids copy; clears cache |
| Empty cache fallback in PathTracingPass | ✅ | Step 7 — falls back to `collectPathTracingInstances()` |
| Error propagation from GpuSkinningPass | ✅ | Step 8 — checks return value in `pathTracingRender()` |
| HLSL register fix (`b0→b5`, `u0→u6`, `u1→u7`) | ✅ | Step 1 — all registers now unique |
| Persistent constants buffer member declaration | ✅ | Step 2 — `m_skin_constants_buffer` declared in header |
| Correct frame ordering (buffer update → compute) | ✅ | Steps 4 → 6 in `dispatch()` |
| Per-instance u0 binding (no flat buffer) | ✅ | Step 5 — `resources.skinned_position_buffer` at offset 0 |

---

## Issue Summary

| # | Severity | Location | Summary |
|---|----------|----------|---------|
| 1 | 🔴 | Step 5 lines 339–345, 402–403 | Code creates/destroys `constants_buffer` per dispatch — `m_skin_constants_buffer` declared but unused; preamble says persistent |
| 2 | 🔴 | Step 5 lines 361, 373, 385 | `dstBinding` values still 0, 0, 1 — should be 5, 6, 7 to match the fixed HLSL registers and layout |
| 3 | 🔴 | Step 5 line 405 | `joint_matrix_offset += inst.joint_count` — should be `s_mesh_vertex_blending_max_joint_count`; preamble says it is |
| 4 | ✅ | Step 7 | Duplicate buffer creation removed |
| 5 | ✅ | Step 8 | Error propagation added |
| 6 | ✅ | Step 1 | HLSL registers fixed |
| 7 | ✅ | Step 1 | Move semantics for instance cache |

---

## Recommendation

Update **Task 2 Step 5** code to match the English fix declarations:

1. Replace `RHIBuffer* constants_buffer = nullptr;` + `createBuffer(16, ..., constants_buffer, ...)` + `destroyBuffer(constants_buffer)` with mapping of the persistent `m_skin_constants_buffer`
2. Replace `writes[5].dstBinding = 0;` → `5`, `writes[6].dstBinding = 0;` → `6`, `writes[7].dstBinding = 1;` → `7`
3. Replace `joint_matrix_offset += inst.joint_count;` → `joint_matrix_offset += s_mesh_vertex_blending_max_joint_count;`
4. Apply the same joint matrix stride fix to `uploadJointMatrices()` with an explicit code block

After these code-level corrections, the plan is approved for implementation.
