# Review: Eliminate Per-Frame Scene Buffer Rebuild in Path Tracing

> Review of `docs/superpowers/plans/2026-06-18-path-tracing-eliminate-per-frame-buffer-rebuild.md`
> Date: 2026-06-18

## Verdict

**Approved as-is.** This is a clean, low-risk optimization with no correctness bugs. The plan correctly identifies the performance bottleneck and the fix is a pure code reordering — moving the `tlas_dirty` check before `updatePathTracingSceneBuffers()`.

---

## Analysis

### The bottleneck is real

[`updatePathTracingSceneBuffers()`](engine/source/runtime/function/render/render_resource.cpp#L316) is called every frame unconditionally and does significant CPU work:

1. **Clears 5 vectors** (vertex, index, material, geometry, instance) — line 319-323
2. **Iterates all meshes** — for each unique mesh, copies every vertex (position, normal, tangent, texcoord) and every index into global arrays — lines 347-364
3. **Builds per-instance records** (material, geometry, instance) — lines 373-419
4. **Maps and memcpys 5 GPU buffers** — lines 479-494+

For a static scene, this produces **identical data every frame** — pure waste. The fix is to gate this behind the existing `tlas_dirty` condition.

### The code motion is correct

**Before (current):**
```
1. Assign shader_instance_index to each instance
2. updatePathTracingSceneBuffers()  ← expensive, always runs
3. m_descriptor_set_dirty = true
4. Check tlas_dirty
5. If not dirty → return (skipping TLAS rebuild only)
6. If dirty → rebuild TLAS
```

**After (plan):**
```
1. Assign shader_instance_index to each instance
2. Check tlas_dirty
3. If not dirty → return (skipping BOTH buffer update AND TLAS rebuild)
4. If dirty → updatePathTracingSceneBuffers() + m_descriptor_set_dirty = true
5. Rebuild TLAS
```

### Correctness by case

| Scenario | `tlas_dirty` | Behavior | Correct? |
|----------|-------------|----------|----------|
| First frame (`m_top_level_as == nullptr`) | true | Buffers updated, TLAS built | ✅ |
| Static scene, frame 2+ | false | Early return — buffers unchanged, data still valid from frame 1 | ✅ |
| Camera moves only | false (camera doesn't dirty TLAS) | Early return — geometry unchanged | ✅ |
| Object added/removed | true | Buffers rebuilt, TLAS rebuilt | ✅ |
| Object transform | true (scene marks TLAS dirty) | Buffers rebuilt, TLAS rebuilt | ✅ |
| Mesh BLAS rebuilt | Depends on `isPathTracingTLASDirty()` | If TLAS dirty marked → same as dynamic case | ✅ |
| Swapchain resize | false (extent doesn't dirty TLAS) | Early return — geometry unchanged | ✅ |

The shader instance indices loop (lines 825-828) still runs before the early return, which is harmless wasted work — setting fields on a local vector that gets destroyed on return.

---

## 🟢 Minor Observations

### 1. Shader instance index assignment runs before early return

```cpp
// These lines run even when we early-return (tlas not dirty)
for (uint32_t instance_index = 0; instance_index < collected_instances.size(); ++instance_index)
{
    collected_instances[instance_index].shader_instance_index = instance_index;
}
```

The `collected_instances` vector is a local variable destroyed on return, so these assignments are wasted when `!tlas_dirty`. The cost is negligible (~N integer writes), but for maximum efficiency the loop could also move inside the guard. **Not a correctness issue** — purely cosmetic.

### 2. Redundant `m_descriptor_set_dirty = true` inside TLAS rebuild block

After the fix, `m_descriptor_set_dirty` is set to `true` twice when TLAS is dirty — once after the buffer update (line 96 in plan) and again after TLAS rebuild (original line 887). Setting a bool to `true` twice is idempotent. **Harmless.**

### 3. `m_descriptor_set_dirty` is not used as a conditional guard

Looking at `dispatch()`, `updateDescriptorSet()` is called unconditionally every frame (line 120). The `m_descriptor_set_dirty` flag is set in various places but never checked as a guard — it appears to be informational or used externally. This means moving the flag assignment inside the `tlas_dirty` guard has no behavioral effect on the descriptor set update path. **Safe.**

### 4. Build command uses `--config Debug` for verification

The build command in Step 2 specifies `--config Debug`. The existing commit messages in this repo suggest `Release` builds are the norm. Debug builds may have different DXC shader compilation behavior (debug symbols, no optimization). Consider using `--config Release` for a production-representative verification, or at least noting that both configurations should compile.

### 5. Verification checklist requires runtime, not just compilation

The verification checklist (items 1-4) requires running the engine and observing frame rates. The plan correctly notes this as post-commit validation — the code change itself is verified by compilation. **Good separation of concerns.**

---

## Issue Summary

| # | Severity | Summary |
|---|----------|---------|
| 1 | 🟢 Minor | Shader index assignment runs before early return — N wasted integer writes |
| 2 | 🟢 Minor | Redundant `m_descriptor_set_dirty = true` set twice in same code path |
| 3 | 🟢 Minor | `m_descriptor_set_dirty` is dead code (set but never checked as guard) — unrelated to plan |
| 4 | 🟢 Minor | Build step uses Debug config; Release may catch different issues |
| 5 | 🟢 Minor | Runtime verification needed for definitive confirmation — noted in plan |

---

## Recommendation

**Approve and implement as written.** No changes required to the plan. The optimization is correct, the scope is minimal (one function, pure code reordering), and the risk of regression is near zero. The minor observations above are cosmetic and do not affect correctness.
