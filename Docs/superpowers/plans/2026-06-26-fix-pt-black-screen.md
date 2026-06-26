# Fix Path Tracing Black Screen — Attachment LoadOp Alias

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the black screen in path tracing mode caused by the D3D12 driver aliasing backup_even's memory with the swapchain attachment, due to `loadOp = DONT_CARE`.

**Root cause:** In commit `5cab95c` (Task 2), the path tracing composite render pass's `backup_even` attachment was changed from `loadOp = CLEAR` to `loadOp = DONT_CARE`. When `loadOp = DONT_CARE` and `initialLayout = UNDEFINED`, the D3D12 driver is allowed to alias (memory-share) this attachment with other attachments in the same render pass. The driver aliased backup_even with the swapchain attachment, causing both to resolve to the same D3D12 resource (Resource_48). This created a read-write hazard in subpass 7 where combine_ui reads `g_ui_color` (backup_even) and writes the output to swapchain — both the same physical resource.

**Fix:** Restore `backup_even.loadOp = CLEAR`. This forces the driver to allocate independent memory for backup_even. The CLEAR is free — `clearUIAttachment()` in subpass 6 immediately overwrites it via `cmdClearAttachments`.

**Tech Stack:** C++17, D3D12, Piccolo RHI

---

## Global Constraints

- Must not affect the rasterization pipeline
- Must not require new render passes, new shaders, or new pipelines
- Particles in PT are temporarily reverted to the previous "no particles" state (subpass 0 empty) since the forward_lighting subpass was configured without a depth attachment needed by the particle pipeline

---

## File Map

| File | Change | Description |
|------|--------|-------------|
| `engine/source/runtime/function/render/passes/main_camera_pass.cpp` | Modify | Restore `backup_even.loadOp = CLEAR`; revert forward_lighting subpass attachments; remove particle draw call |
| `engine/source/runtime/function/render/passes/main_camera_pass.h` | Modify | Remove `ParticlePass&` from `drawPathTracing` signature |
| `engine/source/runtime/function/render/render_pipeline.cpp` | Modify | Remove `particle_pass` from `drawPathTracing` call |

---

## Tasks

### Task 1: Restore `backup_even.loadOp = CLEAR`

**Files:**
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp:572-580`

- [ ] **Step 1: Change loadOp back to CLEAR**

```cpp
// Line 575 — BEFORE (commit 5cab95c):
backup_even.loadOp = RHI_ATTACHMENT_LOAD_OP_DONT_CARE;

// AFTER:
backup_even.loadOp = RHI_ATTACHMENT_LOAD_OP_CLEAR;
```

- [ ] **Step 2: Commit**

```bash
git add engine/source/runtime/function/render/passes/main_camera_pass.cpp
git commit -m "fix: restore backup_even loadOp to CLEAR to prevent D3D12 alias

When loadOp=DONT_CARE with initialLayout=UNDEFINED, the D3D12 driver
may alias the attachment memory with other attachments in the same
render pass. backup_even was aliased with the swapchain attachment,
causing Resource_48 to be shared as both g_ui_color input and render
target output in the combine_ui subpass. CLEAR loadOp forces the
driver to allocate independent memory.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 2: Revert forward_lighting subpass attachments + remove particle draw

**Files:**
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp:622-646`
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp:2316-2370`

**Why:** The forward_lighting subpass was configured in commit `fe5d198` with `backup_odd` as a color attachment and `depth` as a depth attachment. Without the particle draw call, these subpass 0 configurations are dead code and should be cleaned up. The particle draw call itself is removed because the particle pipeline requires a depth attachment and a specific subpass structure that the PT render pass doesn't fully match.

- [ ] **Step 1: Remove forward_lighting subpass configuration**

Remove lines added in commit `fe5d198` — the forward_lighting color and depth attachment configuration in `setupPathTracingCompositeRenderPass()`:

```cpp
// REMOVE these lines (added for particle support):
// RHIAttachmentReference forward_lighting_color {};
// forward_lighting_color.attachment = _main_camera_pass_backup_buffer_odd;
// ...
// subpasses[_main_camera_subpass_forward_lighting].colorAttachmentCount = 1;
// subpasses[_main_camera_subpass_forward_lighting].pColorAttachments = &forward_lighting_color;
// RHIAttachmentReference forward_lighting_depth {};
// forward_lighting_depth.attachment = _main_camera_pass_depth;
// ...
// subpasses[_main_camera_subpass_forward_lighting].pDepthStencilAttachment = &forward_lighting_depth;
```

- [ ] **Step 2: Remove particle draw call from drawPathTracing**

In `drawPathTracing()`, replace:

```cpp
// Subpass 0 (forward_lighting): render particles on top of PT output (backup_odd).
// Particles blend with the path traced scene via alpha blending.
particle_pass.setRenderCommandBufferHandle(m_rhi->getCurrentCommandBuffer());
particle_pass.draw();

// Skip subpasses 1-5 (tone_mapping→color_grading→fxaa).
// Path tracing output (backup_odd) preserved — fxaa skipped so never overwritten.
for (int i = 0; i < 5; ++i)
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);
```

With:

```cpp
// Skip subpasses 0-5 (forward_lighting→tone_mapping→color_grading→fxaa).
// Path tracing output (backup_odd) is never overwritten since fxaa is skipped.
// clearUIAttachment() in subpass 6 clears backup_even to (0,0,0,0) so
// combine_ui returns scene_color for non-UI pixels.
for (int i = 0; i < 6; ++i)
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);
```

- [ ] **Step 3: Update drawPathTracing signature**

```cpp
// Line 2316 — BEFORE:
void MainCameraPass::drawPathTracing(ParticlePass&  particle_pass,
                                     UIPass&        ui_pass,
                                     CombineUIPass& combine_ui_pass,
                                     uint32_t       current_swapchain_image_index)

// AFTER:
void MainCameraPass::drawPathTracing(UIPass&        ui_pass,
                                     CombineUIPass& combine_ui_pass,
                                     uint32_t       current_swapchain_image_index)
```

- [ ] **Step 4: Commit**

```bash
git add engine/source/runtime/function/render/passes/main_camera_pass.cpp
git commit -m "revert: remove particle draw from PT, clean up forward_lighting subpass

Particles require a depth attachment not present in the PT composite
render pass. Removed until a proper solution (dedicated subpass or
depth data) is designed.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 3: Update call site in render_pipeline.cpp

**Files:**
- Modify: `engine/source/runtime/function/render/render_pipeline.cpp:347-356`

- [ ] **Step 1: Remove particle_pass from drawPathTracing call**

```cpp
// BEFORE:
ParticlePass&    particle_pass   = *(static_cast<ParticlePass*>(m_particle_pass.get()));
UIPass&           ui_pass         = *(static_cast<UIPass*>(m_ui_pass.get()));
CombineUIPass&    combine_ui_pass = *(static_cast<CombineUIPass*>(m_combine_ui_pass.get()));

const uint32_t current_swapchain_image_index = render_rhi->getCurrentSwapchainImageIndex();
static_cast<MainCameraPass*>(m_main_camera_pass.get())
    ->drawPathTracing(particle_pass,
                      ui_pass,
                      combine_ui_pass,
                      current_swapchain_image_index);

// AFTER:
UIPass&           ui_pass         = *(static_cast<UIPass*>(m_ui_pass.get()));
CombineUIPass&    combine_ui_pass = *(static_cast<CombineUIPass*>(m_combine_ui_pass.get()));

const uint32_t current_swapchain_image_index = render_rhi->getCurrentSwapchainImageIndex();
static_cast<MainCameraPass*>(m_main_camera_pass.get())
    ->drawPathTracing(ui_pass,
                      combine_ui_pass,
                      current_swapchain_image_index);
```

- [ ] **Step 2: Commit**

```bash
git add engine/source/runtime/function/render/render_pipeline.cpp
git commit -m "refactor: remove particle_pass from PT drawPathTracing call site"
```

---

### Task 4: Update drawPathTracing declaration in header

**Files:**
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.h`

- [ ] **Step 1: Update declaration**

```cpp
// BEFORE:
void drawPathTracing(ParticlePass&  particle_pass,
                     UIPass& ui_pass,
                     CombineUIPass& combine_ui_pass,
                     uint32_t       current_swapchain_image_index);

// AFTER:
void drawPathTracing(UIPass& ui_pass,
                     CombineUIPass& combine_ui_pass,
                     uint32_t       current_swapchain_image_index);
```

- [ ] **Step 2: Commit**

```bash
git add engine/source/runtime/function/render/passes/main_camera_pass.h
git commit -m "refactor: remove ParticlePass from drawPathTracing declaration"
```

---

### Task 5: Build verification

- [ ] **Step 1: Build**

```bash
cd d:/program/Piccolo/build
cmake --build . --config Debug --target PiccoloEditor 2>&1 | tail -5
```

Expected: `PiccoloEditor.exe` built successfully.

- [ ] **Step 2: Verify fix**

Run the engine in path tracing mode. Expected: path traced output visible (not black screen).

---

## Impact Summary

| Change | Before | After |
|--------|--------|-------|
| `backup_even.loadOp` | `DONT_CARE` | `CLEAR` |
| Particles in PT | `particle_pass.draw()` | Removed (skipped) |
| Subpass 0 config | Color+depth attachments | Empty (original) |
| `cmdNextSubpass` count | 5 (subpass 1-5) | 6 (subpass 0-5) |
