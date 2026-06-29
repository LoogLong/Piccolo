# Fix All Path Tracing Pipeline Redundancy

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate all unnecessary GPU/CPU work from the path tracing pipeline identified in the Nsight investigation report.

**Tech Stack:** C++17, D3D12, Piccolo RHI

---

## Task 1: Remove Shadow Map Rendering (🔴 P0)

**Files:**
- Modify: `engine/source/runtime/function/render/render_pipeline.cpp`

- [ ] **Step 1: Delete shadow pass draw calls**

In `pathTracingRender()`, remove the two shadow pass lines. Path tracing uses ray-traced shadow rays in the closest-hit shader — shadow maps are never read.

```cpp
// REMOVE:
static_cast<DirectionalLightShadowPass*>(m_directional_light_pass.get())->draw();
static_cast<PointLightShadowPass*>(m_point_light_shadow_pass.get())->draw();
```

**Impact:** Saves (1 + N_point_lights) full-scene depth-only render passes per frame.

- [ ] **Step 2: Commit**

---

## Task 2: Create Dedicated Path Tracing Present Render Pass (🔴 P0)

**Files:**
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.h`
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp`

**Goal:** Replace `m_path_tracing_composite_render_pass` (9 attachments, 8 subpasses) with a minimal render pass (3 attachments, 2 subpasses) specifically designed for path tracing.

**New render pass structure:**

```
Attachment 0: backup_odd   R16G16B16A16_SFLOAT  loadOp=LOAD   → PT output, read by combine_ui
Attachment 1: backup_even  R16G16B16A16_SFLOAT  loadOp=DONT_CARE → UI output, written by UI subpass
Attachment 2: swapchain    RGBA8_SRGB            loadOp=CLEAR  → final output

Subpass 0 (UI):
  Color[0] = attachment 1 (backup_even)

Subpass 1 (Combine UI):
  Input[0] = attachment 0 (backup_odd)
  Input[1] = attachment 1 (backup_even)
  Color[0] = attachment 2 (swapchain)

Dependencies:
  external → subpass 0: RAY_TRACING_SHADER_WRITE → COLOR_ATTACHMENT_WRITE (PT→UI barrier)
  subpass 0 → subpass 1: COLOR_ATTACHMENT_WRITE → FRAGMENT_SHADER_READ
```

**Removed attachments vs current:**
| Attachment | Saved |
|------------|-------|
| gbuffer_a (RGBA8) | 1 clear + memory |
| gbuffer_b (RGBA8) | 1 clear + memory |
| gbuffer_c (RGBA8) | 1 clear + memory |
| post_process_odd (RGBA8_SRGB) | 1 clear + memory |
| post_process_even (RGBA8_SRGB) | 1 clear + memory |
| depth (D32_SFLOAT) | 1 clear + memory |

6 attachment clears + ~50MB VRAM saved per frame.

- [ ] **Step 1: Add new members to header**

```cpp
// Path tracing present render pass (minimal: PT output + UI + swapchain)
RHIRenderPass* m_path_tracing_present_render_pass {nullptr};
std::vector<RHIFramebuffer*> m_path_tracing_present_framebuffers;
```

- [ ] **Step 2: Implement `setupPathTracingPresentRenderPass()`**

Create the minimal 3-attachment, 2-subpass render pass and framebuffers. Called during PT pipeline initialization alongside `setupPathTracingCompositeRenderPass()` (which can now be removed).

- [ ] **Step 3: Update `drawPathTracing()` to use new render pass**

Replace `m_path_tracing_composite_render_pass` → `m_path_tracing_present_render_pass` and `m_path_tracing_composite_swapchain_framebuffers` → `m_path_tracing_present_framebuffers`. Reduce `cmdNextSubpass` from 6 to 1 (only UI → combine_ui). Reduce clear values from 9 to 3.

- [ ] **Step 4: Remove old composite render pass setup**

Delete `m_path_tracing_composite_render_pass` and related framebuffers. Clean up `setupPathTracingCompositeRenderPass()`.

- [ ] **Step 5: Commit**

---

## Task 3: Skip Full Scene Buffer Rebuild When Static Data Unchanged (🟡 P1)

**Files:**
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`
- Modify: `engine/source/runtime/function/render/render_resource.h`
- Modify: `engine/source/runtime/function/render/render_resource.cpp`

**Goal:** When `has_skinned` is true, TLAS always rebuilds, but only the instance buffer needs per-frame update. Static vertex/index/material/geometry data is unchanged.

- [ ] **Step 1: Track static data dirty separately**

In `PathTracingPass`, add `m_static_scene_buffers_dirty` flag. Set to true when:
- `scene.isPathTracingTLASDirty()` (new mesh added/removed)
- First frame (`m_last_collected_instance_count == 0`)

In `buildTopLevelAS()`:

```cpp
const bool static_data_dirty = scene.isPathTracingTLASDirty() ||
                                m_last_collected_instance_count == 0;

// TLAS rebuild check (unchanged)
const bool tlas_dirty = has_skinned || static_data_dirty || ...;

if (!tlas_dirty) return true;

if (static_data_dirty)
{
    // Full rebuild: all buffers changed
    updatePathTracingSceneBuffers(m_rhi, filtered_instances);
}
else
{
    // Lightweight: only instance transforms changed
    updatePathTracingInstanceBuffer(m_rhi, filtered_instances);
}
```

- [ ] **Step 2: Implement `updatePathTracingInstanceBuffer()`**

Lightweight function that only rebuilds `m_path_tracing_instance_data` + uploads `g_instances`. Does NOT touch vertex, index, material, or geometry buffers.

- [ ] **Step 3: Commit**

---

## Task 4: Split Descriptor Set into Static + Dynamic (🟢 P2)

**Files:**
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.h`

**Goal:** Never-changing bindings written once at init, frame-changing bindings updated per-frame.

- [ ] **Step 1: Identify static vs dynamic bindings**

| Binding | Type | Frequency |
|---------|------|-----------|
| u1 (scene_output) | Static | Never |
| t9 (irradiance) | Static | Never |
| t10 (specular) | Static | Never |
| t11 (texture_array) | Static | Rarely (material load) |
| s12 (sampler) | Static | Never |
| t0 (TLAS) | Dynamic | Per TLAS rebuild |
| b2 (frame_data) | Dynamic | Every frame |
| u3 (accumulation) | Dynamic | Every frame (ping-pong) |
| t4 (vertices) | Dynamic | Per scene buffer update |
| t5 (indices) | Dynamic | Per scene buffer update |
| t6 (materials) | Dynamic | Per scene buffer update |
| t7 (geometries) | Dynamic | Per scene buffer update |
| t8 (instances) | Dynamic | Per scene buffer update |
| t1036 (skinned_vert) | Dynamic | Per frame (if skinned) |
| t1035 (accum_prev) | Dynamic | Every frame (ping-pong) |

- [ ] **Step 2: Write static descriptors once at init**

After `setupDescriptorSet()`, write bindings 1, 9, 10, 11, 12 once and never again.

- [ ] **Step 3: Only update dynamic bindings in `updateDescriptorSet()`**

Skip writes for already-initialized static bindings.

- [ ] **Step 4: Commit**

---

## Task 5: Per-instance BLAS Optimization (🟡 P1)

**Files:**
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp`

**Goal:** Use `allow_update` + `perform_update` instead of destroy + rebuild for skinned BLAS. The vertex count and index count don't change — only positions change.

- [ ] **Step 1: Modify BLAS build to support update**

In the per-instance BLAS loop:

```cpp
auto& pt_resources = mesh->path_tracing_skinned_resources[inst_id];

if (pt_resources.blas == nullptr)
{
    // First time: create with allow_update = true
    pt_resources.blas = buildPathTracingBLASFromSkinned(..., allow_update = true);
}
else
{
    // Subsequent frames: update in place
    updatePathTracingBLASFromSkinned(pt_resources.blas, ...);
}
```

This requires adding `buildPathTracingBLASFromSkinned` parameters for `allow_update`/`perform_update`, and adding an `updatePathTracingBLASFromSkinned` variant that passes `perform_update = true` + the existing BLAS as `source`.

- [ ] **Step 2: Commit**

---

## Task 6: Skip Particle Simulate in Path Tracing Mode (🟢 P2)

**Files:**
- Modify: `engine/source/runtime/function/render/render_pipeline.cpp`

- [ ] **Step 1: Add conditional check**

In `pathTracingRender()`, skip particle simulation. Particles are not rendered in the path tracing pipeline.

```cpp
// REMOVE:
static_cast<ParticlePass*>(m_particle_pass.get())->simulate();
```

If particles need to be kept (e.g., for a hybrid PT+raster mode), wrap in a flag check.

- [ ] **Step 2: Commit**

---

## Task 7: Disable Debug Axis in Path Tracing Mode (🟢 P3)

**Files:**
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp`

- [ ] **Step 1: Skip `drawAxis()` in `drawPathTracing()`**

Remove or comment out `drawAxis()` call — it's debug-only geometry.

- [ ] **Step 2: Commit**

---

## Task 8: Build Verification

- [ ] **Step 1: Build**

```bash
cd d:/program/Piccolo/build
cmake --build . --config Debug --target PiccoloEditor 2>&1 | tail -5
```

- [ ] **Step 2: Fix compilation errors if any**

---

## Execution Order

| Priority | Task | Dependencies |
|----------|------|--------------|
| 1 | Task 1 (Shadow maps) | None |
| 2 | Task 2 (Present render pass) | After Task 1 |
| 3 | Task 6 (Particle) | None |
| 4 | Task 7 (Debug axis) | None |
| 5 | Task 3 (Scene buffer rebuild) | After Task 2 (uses filtered list) |
| 6 | Task 5 (BLAS update) | None |
| 7 | Task 4 (Descriptor split) | None |
| 8 | Task 8 (Build) | After all |

---

## Impact Summary

| Task | Before | After | Savings |
|------|--------|-------|---------|
| #1 Shadow maps | 1+N_lights depth passes | 0 | Full scene depth render × (1+N_lights) |
| #2 Present RP | 9 attach, 8 subpass, 6 clears | 3 attach, 2 subpass, 0 unnecessary clears | 6 RT clears + memory |
| #3 Scene buffers | 5 buffer uploads/frame | 1 upload (instance only) | 4 buffer uploads + CPU memcpy |
| #4 Descriptors | 15 writes/frame | ~8 writes/frame | ~7 descriptor writes |
| #5 BLAS | destroy+rebuild/frame | update in place | GPU sync point removed |
| #6 Particle | simulate/frame | skipped | CPU sim time |
| #7 Axis | draw/frame | skipped | Minimal GPU |
