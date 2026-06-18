# Eliminate Per-Frame Scene Buffer Rebuild in Path Tracing

> **For agentic workers:** Use `superpowers:subagent-driven-development` to implement this plan.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate unnecessary CPU-side scene buffer rebuilds that occur every frame in `buildTopLevelAS()`, which is the root cause of extremely low path tracing frame rates.

**Architecture:** The `buildTopLevelAS()` function in `PathTracingPass` unconditionally calls `updatePathTracingSceneBuffers()` before checking whether the TLAS actually needs rebuilding. For static scenes, the TLAS is NOT dirty after the first frame, yet all 5 GPU scene buffers (vertex, index, material, geometry, instance) are cleared and rebuilt every frame — iterating over every vertex of every mesh and mapping/unmapping GPU memory. The fix moves the buffer update inside the `tlas_dirty` guard so it only executes when the acceleration structure actually changes.

**Tech Stack:** C++17, D3D12, Piccolo RHI

## Global Constraints

- Modify only `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`
- Must not change shader data layout or descriptor bindings
- Static scenes must produce identical visual output before and after
- Dynamic scenes (object add/remove/transform) must still update correctly

---

## File Map

| File | Responsibility | Modified? |
|------|---------------|-----------|
| `engine/source/runtime/function/render/passes/path_tracing_pass.cpp` | `buildTopLevelAS()` — reorder buffer update and TLAS dirty check | ✅ Yes |
| `engine/source/runtime/function/render/render_resource.cpp` | `updatePathTracingSceneBuffers()` — the expensive function being guarded (read-only for verification) | ❌ No |

---

### Task 1: Move scene buffer update inside TLAS dirty guard

**Files:**
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp:825-845`

**Interfaces:**
- Consumes: `RenderResource::updatePathTracingSceneBuffers()`, `RenderScene::isPathTracingTLASDirty()`, `m_top_level_as`, `m_tlas_instance_count`
- Produces: `buildTopLevelAS()` returns true without calling `updatePathTracingSceneBuffers()` when TLAS is not dirty

**Behavior change:**
- Before: `updatePathTracingSceneBuffers()` called every frame unconditionally
- After: `updatePathTracingSceneBuffers()` called only when TLAS is dirty (scene changed OR first frame OR instance count changed)

- [ ] **Step 1: Reorder code — move dirty check before buffer update**

Open `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`.

**Locate lines 825-888** (the shader instance index assignment through the end of TLAS rebuild).

**Current code (lines 825-845):**
```cpp
        for (uint32_t instance_index = 0; instance_index < collected_instances.size(); ++instance_index)
        {
            collected_instances[instance_index].shader_instance_index = instance_index;
        }

        // Update path tracing scene buffers
        if (!m_render_resource_impl->updatePathTracingSceneBuffers(m_rhi, collected_instances))
        {
            return false;
        }
        m_descriptor_set_dirty = true;

        const bool tlas_dirty =
            scene.isPathTracingTLASDirty() ||
            m_top_level_as == nullptr ||
            m_tlas_instance_count != collected_instances.size();
        m_last_tlas_rebuilt = tlas_dirty;
        if (!tlas_dirty)
        {
            return true;
        }
```

**Replace with (lines 825-855):**
```cpp
        for (uint32_t instance_index = 0; instance_index < collected_instances.size(); ++instance_index)
        {
            collected_instances[instance_index].shader_instance_index = instance_index;
        }

        const bool tlas_dirty =
            scene.isPathTracingTLASDirty() ||
            m_top_level_as == nullptr ||
            m_tlas_instance_count != collected_instances.size();
        m_last_tlas_rebuilt = tlas_dirty;
        if (!tlas_dirty)
        {
            return true;
        }

        // Only update scene buffers when TLAS actually needs rebuilding
        if (!m_render_resource_impl->updatePathTracingSceneBuffers(m_rhi, collected_instances))
        {
            return false;
        }
        m_descriptor_set_dirty = true;
```

Note: the `m_descriptor_set_dirty = true` on the old line 887 (inside the TLAS rebuild block, which runs AFTER the above code) is preserved — it sets dirty again inside the rebuild path, which is redundant but harmless.

- [ ] **Step 2: Build and verify compilation**

```bash
cd d:/program/Piccolo/build
cmake --build . --config Debug --target PiccoloRuntime 2>&1 | tail -5
```

**Expected:** Build succeeds with `PiccoloRuntime.lib` output. No errors.

- [ ] **Step 3: Commit**

```bash
git add engine/source/runtime/function/render/passes/path_tracing_pass.cpp
git commit -m "perf: skip per-frame scene buffer rebuild when TLAS is unchanged

Move updatePathTracingSceneBuffers() inside the tlas_dirty guard in
buildTopLevelAS(). Previously, all 5 GPU scene buffers (vertex, index,
material, geometry, instance) were cleared and rebuilt every frame
regardless of whether the scene had changed.

For static scenes, this eliminates per-frame:
- Iterating over all meshes and copying every vertex/index
- Mapping and unmapping 5 GPU buffers for memcpy
- 14 descriptor set writes triggered by m_descriptor_set_dirty

Dynamic scenes (TLAS dirty) behavior is unchanged."
```

---

### Verification Checklist (post-commit, runtime)

These require running the engine — not part of the code change, but confirm after:

1. **Static scene:** Frame rate significantly higher than before
2. **Dynamic scene:** Objects added/removed still render correctly
3. **No visual regression:** Output matches pre-fix output for identical scene/camera
4. **TLAS still rebuilds on scene change:** Trigger a scene change (e.g., reload level) and confirm TLAS rebuild + accumulation reset
