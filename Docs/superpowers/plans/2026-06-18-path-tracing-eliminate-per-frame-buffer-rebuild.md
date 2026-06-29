# Eliminate Per-Frame Scene Buffer Rebuild in Path Tracing

> **For agentic workers:** Use `superpowers:subagent-driven-development` to implement this plan.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate unnecessary per-frame CPU-side scene buffer rebuilds that cause extremely low frame rates, especially when skinned meshes (with large vertex counts) are present in the scene.

**Architecture:** The root cause is in `PathTracingPass::buildTopLevelAS()` which unconditionally calls `updatePathTracingSceneBuffers()` BEFORE checking whether `tlas_dirty` requires a rebuild. This means every frame — regardless of whether the scene changed — all 5 GPU scene buffers (vertex, index, material, geometry, instance) are cleared, rebuilt by iterating over every vertex of every mesh, and uploaded via map/memcpy/unmap. The overhead scales with total vertex count, making skinned character meshes (typically 10K-100K vertices) catastrophic for performance. The fix is a pure code reorder: check `tlas_dirty` first, then update buffers only when actually needed.

**Tech Stack:** C++17, D3D12, Piccolo RHI

## Global Constraints

- Modify only `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`
- Must not change shader data layout or descriptor bindings
- Static scenes must produce identical visual output before and after
- Scenes where TLAS dirty is true must behave identically to before

---

## Root Cause Analysis

### The Per-Frame Call Chain

```
RenderPipeline::pathTracingRender()          [render_pipeline.cpp:310]
  → PathTracingPass::dispatch()              [path_tracing_pass.cpp:72]
    → buildTopLevelAS(*render_scene)          [path_tracing_pass.cpp:764]
      → collectPathTracingInstances()          [render_resource.cpp:271]
        → rebuildPathTracingInstances()         [render_scene.cpp:144]
      → updatePathTracingSceneBuffers()  ← ← ← BUG: called every frame! [line 831]
      → isPathTracingTLASDirty()?             [line 838]
        → if not dirty: return true ← ← ← only NOW do we skip work
```

### What `updatePathTracingSceneBuffers` Does Every Frame

In [render_resource.cpp:316-520](engine/source/runtime/function/render/render_resource.cpp#L316):

1. **Clears 5 CPU vectors**: `m_path_tracing_vertex_data`, `m_path_tracing_index_data`, `m_path_tracing_material_data`, `m_path_tracing_geometry_data`, `m_path_tracing_instance_data`
2. **Iterates all unique meshes** (deduped by pointer per frame, not cached cross-frame):
   - Copies ALL vertex positions, normals, tangents, texcoords into vectors
   - Copies ALL indices
3. **Iterates all instances**: builds material/texture mapping
4. **Maps and updates 5 GPU buffers**: vertex, index, material, geometry, instance — each via `mapMemory` → `memcpy` → `unmapMemory`

### Why Skinned Meshes Make It Catastrophic

| Property | Static mesh (e.g. cube) | Skinned mesh (e.g. character) |
|----------|------------------------|-------------------------------|
| Vertex count | ~24 | ~10,000–50,000 |
| Index count | ~36 | ~30,000–150,000 |
| Bytes copied/frame | ~2 KB | ~2–10 MB |
| GPU buffer map/unmap | 5 operations | 5 operations (same count, larger size) |

For a static scene: thousands of unnecessary memcpy operations per frame.  
For a skinned character: **millions** of unnecessary memcpy operations per frame.

### Why The TLAS Dirty Check Currently Comes Too Late

The original code at [path_tracing_pass.cpp:830-845](engine/source/runtime/function/render/passes/path_tracing_pass.cpp#L830-L845):

```cpp
// Step A: Update scene buffers ← EXPENSIVE, runs every frame
if (!updatePathTracingSceneBuffers(...)) { return false; }
m_descriptor_set_dirty = true;

// Step B: Check if TLAS actually needs rebuilding ← too late!
const bool tlas_dirty = scene.isPathTracingTLASDirty() || ...;
if (!tlas_dirty) { return true; }  // ← we already did all the work!

// Step C: Rebuild TLAS ← only reached when dirty
...
```

For a static scene after the first frame:
- `isPathTracingTLASDirty()` returns `false` (scene constructed on frame 1, not modified)
- But `updatePathTracingSceneBuffers()` already ran — all vertices already copied

---

## File Map

| File | Responsibility | Modified? |
|------|---------------|-----------|
| `engine/source/runtime/function/render/passes/path_tracing_pass.cpp` | `buildTopLevelAS()` lines 825-845 — reorder dirty check before buffer update | ✅ Yes |

---

### Task 1: Move scene buffer update inside TLAS dirty guard

**Files:**
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp:825-845`

**Interfaces:**
- Consumes: `RenderResource::updatePathTracingSceneBuffers(rhi, collected_instances)` — the expensive function
- Consumes: `scene.isPathTracingTLASDirty()`, `m_top_level_as`, `m_tlas_instance_count` — TLAS dirty conditions
- Produces: Early return when TLAS unchanged, without touching scene buffers

- [ ] **Step 1: Reorder code — move dirty check before buffer update**

In `buildTopLevelAS()`, swap the order of the `tlas_dirty` check and the `updatePathTracingSceneBuffers` call.

**Locate lines 825-845.** The current code:

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

**Replace with:**

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

**Important:** The `m_descriptor_set_dirty = true` at the old line 887 (inside the TLAS rebuild block below) is preserved — it sets dirty again during TLAS rebuild, which is redundant with the above but harmless.

- [ ] **Step 2: Build and verify compilation**

```bash
cd d:/program/Piccolo/build
cmake --build . --config Debug --target PiccoloRuntime 2>&1 | tail -5
```

**Expected:** Build succeeds with `PiccoloRuntime.lib` output. No errors, no new warnings.

- [ ] **Step 3: Commit**

```bash
git add engine/source/runtime/function/render/passes/path_tracing_pass.cpp
git commit -m "perf: skip per-frame scene buffer rebuild when TLAS is unchanged

Move updatePathTracingSceneBuffers() inside the tlas_dirty guard in
buildTopLevelAS(). Previously, all 5 GPU scene buffers (vertex, index,
material, geometry, instance) were cleared and rebuilt every frame
regardless of whether the scene had changed.

This was especially catastrophic for skinned meshes with large vertex
counts (10K-100K vertices) where millions of unnecessary memcpy
operations occurred every frame.

For static scenes, this eliminates per-frame:
- Iterating over all meshes and copying every vertex/index
- Mapping and unmapping 5 GPU buffers for memcpy
- 14 descriptor set writes triggered by m_descriptor_set_dirty

TLAS-dirty cases (first frame, scene change, instance count change)
are unchanged."
```

---

### Verification Checklist (runtime, post-commit)

1. **Static scene without skinned mesh:** Frame rate measurably higher
2. **Scene with skinned mesh:** Frame rate dramatically higher (was the worst case)
3. **No visual regression:** Output matches pre-fix for identical scene/camera
4. **Scene change still triggers rebuild:** Reload level → accumulation resets → TLAS rebuilds → correct output
