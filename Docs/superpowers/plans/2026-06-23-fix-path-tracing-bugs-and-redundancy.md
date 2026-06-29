# Fix Path Tracing Pipeline Bugs & Redundancy

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the instance buffer / TLAS index mismatch bug (Bug #3), remove dead code (Bug #4), and eliminate redundant iterations/updates in the path tracing pipeline.

**Status:** Path tracing runs (confirmed — no `PATH_TRACING_FAIL` log), but output appears rasterized. Root cause is Bug #3 — `g_instances` buffer order does not match DXR `InstanceIndex()` after null-BLAS filtering.

**Tech Stack:** C++17, D3D12, HLSL SM 6.6, Piccolo RHI

---

## Problem Catalog

### Bug #3: Instance buffer / TLAS index mismatch (🔴 Critical)

**Location:** `path_tracing_pass.cpp:837` vs `path_tracing_pass.cpp:923-958`

**Root cause:** `updatePathTracingSceneBuffers()` at line 837 receives the UNFILTERED `collected_instances` list (before null-BLAS filtering at line 923). It creates `g_instances` entries for ALL instances. Later, `remove_if` removes instances with null BLAS, and the TLAS is built from the remaining instances. DXR `InstanceIndex()` returns the TLAS position, but `g_instances` was indexed by the unfiltered order.

```
Timeline:
  collected_instances = [StaticA, SkinnedB, StaticC]

L837: updatePathTracingSceneBuffers(collected_instances)
  → g_instances[0] = StaticA
  → g_instances[1] = SkinnedB  (BLAS not yet built!)
  → g_instances[2] = StaticC

L844-894: Per-instance BLAS loop
  → 假设 SkinnedB BLAS 构建失败 → bottom_level_as 仍为 null

L923-929: Filter null BLAS
  → collected_instances = [StaticA, StaticC]

L947-958: Build TLAS
  → DXR instance 0 → StaticA → reads g_instances[0] = StaticA ✓
  → DXR instance 1 → StaticC → reads g_instances[1] = SkinnedB ✗ MISMATCH!
```

**Impact:** Even if all BLAS builds succeed (no instances filtered), the structural issue remains. If any BLAS build fails, all subsequent instances in the TLAS read wrong geometry/material/flags from `g_instances`, producing garbled rendering.

### Bug #4: `shader_instance_index` dead code (🟡 Minor)

**Location:** `render_resource.h:33`, `path_tracing_pass.cpp:940-943`

`shader_instance_index` is assigned in a loop but never read anywhere in the entire codebase. It is dead code consuming a struct field and one pass over `collected_instances`.

---

## Redundancy Issues

### R1: 7 passes over `collected_instances` in `buildTopLevelAS()`

| Pass | Line | What | Can merge? |
|------|------|------|------------|
| 1 | 798 | `std::any_of` — check `has_skinned` | → merge into pass 2 |
| 2 | 803 | Static BLAS ensure | — |
| 3 | 845 | Per-instance BLAS build | → merge with pass 4 |
| 4 | 900 | Orphan BLAS cleanup | → merge with pass 3 |
| 5 | 923 | Filter null BLAS | — (needs two-phase: build then filter) |
| 6 | 940 | `shader_instance_index` assignment | ✗ **REMOVE** (dead code) |
| 7 | 949 | TLAS instance desc creation | — (needs filtered list) |

### R2: Full 15-descriptor rewrite every frame

`updateDescriptorSet()` rewrites all 15 bindings every frame. ~7 bindings are static (never change after initialization). Only ~6-7 need per-frame updates.

### R3: Scene buffer rebuild every frame with skinned instances

When `has_skinned` → `tlas_dirty` always true → `updatePathTracingSceneBuffers()` called every frame. But static vertex/index/material/geometry data never changes. Only instance transforms and skinned vertex data change.

### R7: Redundant null check in `updateDescriptorSet()`

Lines 580 and 598 both check `m_specular_texture_view == nullptr || m_linear_sampler == nullptr`. After the first block populates them (or fails), the second check at L598 can never be true.

---

## Fix Plan

### Task 1: Fix Bug #3 — Instance buffer / TLAS index mismatch

**Files:**
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`

- [ ] **Step 1: Move `updatePathTracingSceneBuffers()` after filtering**

In `buildTopLevelAS()`, restructure the order to:

```
1. collectPathTracingInstances()
2. Compute has_skinned (merged with static BLAS loop — see Task 3)
3. Static BLAS ensure loop                                                    (unchanged)
4. Compute tlas_dirty                                                        (unchanged, but use pre-filter count)
5. if (!tlas_dirty) return true                                              (unchanged)
6. Per-instance BLAS build loop                                              (unchanged)
7. Filter: remove null BLAS instances                                        (unchanged)
8. [MOVED] updatePathTracingSceneBuffers(FILTERED collected_instances)       ← KEY FIX
9. Build TLAS from filtered instances                                        (unchanged)
```

The critical change: `updatePathTracingSceneBuffers()` now receives the FILTERED list (same instances that will go into the TLAS). This ensures `g_instances[i]` corresponds to DXR `InstanceIndex() == i`.

**Before (buggy):**
```cpp
// L837: called with UNFILTERED instances
if (!m_render_resource_impl->updatePathTracingSceneBuffers(m_rhi, collected_instances))
    return false;
m_descriptor_set_dirty = true;

// L845-894: per-instance BLAS loop
// L923-929: filter null BLAS
// L947-958: build TLAS
```

**After (fixed):**
```cpp
// L845-894: per-instance BLAS loop (no change)

// L923-929: filter null BLAS (no change)

// FIXED: now called with FILTERED instances, AFTER filtering
if (!m_render_resource_impl->updatePathTracingSceneBuffers(m_rhi, collected_instances))
    return false;
m_descriptor_set_dirty = true;

// L947-958: build TLAS (no change) — g_instances now matches TLAS order
```

- [ ] **Step 2: Commit**

```
git commit -m "fix: Bug B3 — call updatePathTracingSceneBuffers with filtered instances"
```

---

### Task 2: Fix Bug #4 — Remove `shader_instance_index` dead code

**Files:**
- Modify: `engine/source/runtime/function/render/render_resource.h`
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`

- [ ] **Step 1: Remove field from struct**

In `render_resource.h`, remove `shader_instance_index` from `RenderPathTracingCollectedInstance`:

```cpp
struct RenderPathTracingCollectedInstance
{
    // ... other fields ...
    // REMOVE: uint32_t shader_instance_index {0};
};
```

- [ ] **Step 2: Remove assignment loop**

In `path_tracing_pass.cpp`, remove lines 940-943:

```cpp
// REMOVE this whole loop:
// for (uint32_t instance_index = 0; instance_index < collected_instances.size(); ++instance_index)
// {
//     collected_instances[instance_index].shader_instance_index = instance_index;
// }
```

- [ ] **Step 3: Commit**

```
git commit -m "fix: Bug B4 — remove dead shader_instance_index code"
```

---

### Task 3: Reduce 7→4 passes over `collected_instances` (R1)

**Files:**
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`

- [ ] **Step 1: Merge `has_skinned` check into static BLAS loop**

Replace `std::any_of` (pass 1) by tracking `has_skinned` during the static BLAS loop (pass 2):

```cpp
// BEFORE (2 passes):
const bool has_skinned = std::any_of(collected_instances.begin(), collected_instances.end(),
    [](const auto& i) { return i.enable_vertex_blending; });

std::unordered_set<RenderMeshGPUResource*> processed_meshes;
for (auto& instance : collected_instances)
{
    if (instance.enable_vertex_blending || instance.mesh == nullptr) continue;
    // ... static BLAS ensure ...
}

// AFTER (1 pass):
bool has_skinned = false;
std::unordered_set<RenderMeshGPUResource*> processed_meshes;
for (auto& instance : collected_instances)
{
    if (instance.enable_vertex_blending)
    {
        has_skinned = true;
        continue;
    }
    if (instance.mesh == nullptr) continue;
    // ... static BLAS ensure ...
}
```

- [ ] **Step 2: Merge per-instance BLAS loop with orphan cleanup**

Track active instances during the per-instance BLAS loop, then clean up orphans in a single pass over the map (not over `collected_instances`):

```cpp
std::unordered_set<uint32_t> active_skinned_instance_ids;
for (auto& instance : collected_instances)
{
    if (!instance.enable_vertex_blending || instance.mesh == nullptr) continue;
    
    // ... build per-instance BLAS (same as before) ...
    
    active_skinned_instance_ids.insert(inst_id);
}

// Orphan cleanup: iterate the MAP, not collected_instances
std::unordered_set<RenderMeshGPUResource*> cleaned_skinned_meshes;
for (auto& instance : collected_instances)
{
    // ... (this still iterates collected_instances to get unique meshes)
}
```

Note: The orphan cleanup loop currently iterates `collected_instances` to find unique meshes, then iterates the map. This can be optimized by directly iterating the map and checking against `active_skinned_instance_ids`. But the mesh iteration is needed to find which meshes to clean up.

A cleaner approach: collect unique skinned meshes during the per-instance BLAS loop, then clean up orphans after:

```cpp
std::unordered_set<uint32_t> active_skinned_instance_ids;
std::unordered_set<RenderMeshGPUResource*> skinned_meshes_in_frame;

for (auto& instance : collected_instances)
{
    if (!instance.enable_vertex_blending || instance.mesh == nullptr) continue;
    
    skinned_meshes_in_frame.insert(instance.mesh);  // track mesh
    active_skinned_instance_ids.insert(inst_id);     // track instance
    
    // ... build per-instance BLAS ...
}

// Orphan cleanup: only iterate skinned meshes present in this frame
for (auto* mesh : skinned_meshes_in_frame)
{
    auto& map = mesh->path_tracing_skinned_resources;
    for (auto it = map.begin(); it != map.end(); )
    {
        if (active_skinned_instance_ids.count(it->first) == 0)
        {
            if (it->second.blas != nullptr)
                m_rhi->destroyAccelerationStructure(it->second.blas);
            it = map.erase(it);
        }
        else { ++it; }
    }
}
```

This eliminates pass 4 (the `collected_instances` iteration for orphan cleanup) by tracking meshes during pass 3.

**Result:** 7 passes → 4 passes:
1. Static BLAS + has_skinned (merged)
2. Per-instance BLAS + track meshes (merged)
3. Filter null BLAS
4. TLAS instance desc creation

- [ ] **Step 3: Commit**

```
git commit -m "refactor: reduce collected_instances passes from 7 to 4 (R1)"
```

---

### Task 4: Fix redundant null check (R7)

**Files:**
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`

- [ ] **Step 1: Remove redundant check**

In `updateDescriptorSet()`, remove lines 598-601 (the second null check). The first block at lines 580-596 already ensures the views/sampler are populated or returns false.

```cpp
// REMOVE:
// if (m_specular_texture_view == nullptr || m_linear_sampler == nullptr)
// {
//     return false;
// }
```

- [ ] **Step 2: Commit**

```
git commit -m "fix: remove redundant null check in updateDescriptorSet (R7)"
```

---

### Task 5: Avoid full scene buffer rebuild when static data unchanged (R3)

**Files:**
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`
- Modify: `engine/source/runtime/function/render/render_resource.cpp`

- [ ] **Step 1: Add static-data dirty tracking**

In `buildTopLevelAS()`, track whether the static scene data actually changed. The static data only changes when:
- A new static mesh is added/removed
- A mesh's geometry is modified

Since `has_skinned` forces TLAS rebuild, but the STATIC vertex/index/geometry/material buffers haven't changed, we can skip rebuilding those buffers. Only the instance buffer needs updating (for new transforms).

However, the current `updatePathTracingSceneBuffers()` rebuilds everything from scratch. Separating static vs dynamic rebuild is a larger refactoring.

A minimal fix: track `m_static_scene_buffer_dirty` separately from TLAS dirty:

```cpp
// In buildTopLevelAS():
const bool static_data_changed = scene.isPathTracingTLASDirty() ||  // scene-level change
                                  m_last_collected_instance_count == 0; // first frame

const bool rebuild_scene_buffers = has_skinned || static_data_changed;

if (!tlas_dirty) return true;

if (rebuild_scene_buffers)
{
    // Full rebuild: all buffers
    updatePathTracingSceneBuffers(m_rhi, filtered_instances);
}
else
{
    // Partial rebuild: only instance buffer (transforms changed)
    updatePathTracingInstanceBuffer(m_rhi, filtered_instances);
}
```

Note: This requires adding `updatePathTracingInstanceBuffer()` as a lightweight alternative that only rebuilds the instance buffer.

- [ ] **Step 2: Commit**

```
git commit -m "perf: skip full scene buffer rebuild when only instance transforms changed (R3)"
```

---

### Task 6: Build verification

- [ ] **Step 1: Build**

```bash
cd d:/program/Piccolo/build
cmake --build . --config Debug --target PiccoloRuntime 2>&1 | tail -5
```

Expected: Build succeeds.

- [ ] **Step 2: Commit any remaining fixes**

---

## Execution Order

| Priority | Task | Bug/Redundancy | Dependencies |
|----------|------|----------------|--------------|
| 1 | Task 1 | Bug #3 (index mismatch) 🔴 | None |
| 2 | Task 2 | Bug #4 (dead code) 🟡 | None |
| 3 | Task 4 | R7 (redundant check) 🟡 | None |
| 4 | Task 3 | R1 (7→4 passes) 🟡 | After Task 1+2 (cleaner diff) |
| 5 | Task 5 | R3 (partial rebuild) 🟡 | After Task 1 (uses filtered list) |
| 6 | Task 6 | Build verification | After all tasks |

---

## Appendix A: Before/After — `buildTopLevelAS()` structure

### Before (buggy + 7 passes)

```
buildTopLevelAS():
  collectPathTracingInstances()              // collect
  std::any_of(all)                           // pass 1: has_skinned
  for (all): static BLAS                     // pass 2
  check tlas_dirty
  updatePathTracingSceneBuffers(all)         // ← BUG: unfiltered!
  for (all): per-instance BLAS               // pass 3
  for (all): orphan cleanup                  // pass 4
  remove_if(all): filter null BLAS           // pass 5
  for (all): assign shader_instance_index    // pass 6: DEAD CODE
  for (all): TLAS desc                       // pass 7
  build TLAS
```

### After (fixed + 4 passes)

```
buildTopLevelAS():
  collectPathTracingInstances()              // collect
  for (all): static BLAS + has_skinned       // pass 1 (merged)
  check tlas_dirty
  for (all): per-instance BLAS + track meshes // pass 2 (merged)
  remove_if(all): filter null BLAS            // pass 3
  updatePathTracingSceneBuffers(filtered)     // ← FIX: filtered list
  for (filtered): TLAS desc                  // pass 4
  build TLAS
```
