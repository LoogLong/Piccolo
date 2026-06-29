# Simplify Path Tracing Post-Processing Pipeline (v3 — Final)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan.

**Goal:** Remove unnecessary tone mapping, color grading, and FXAA passes from the path tracing pipeline. Requires zero new shaders — leverages the existing `combine_ui.frag.hlsl` behavior.

**Key insight:** The `combine_ui` fragment shader already implements a simple passthrough: if `g_ui_color` (backup_even) is nearly black → returns `g_scene_color` (backup_odd) unchanged. Since PT output lives in `backup_odd` and is never overwritten when the intermediate passes are skipped, the PT output reaches the swapchain directly.

```
combine_ui.frag.hlsl:
  if (ui_color is nearly black) → return scene_color;  // PT output passes through
  else                          → return gamma(ui_color); // UI overlaid
```

**Current attachment chain (path tracing composite render pass):**

```
Subpass 0: forward_lighting  → empty (no draw)
Subpass 1:                   → empty (cmdNextSubpass)
Subpass 2:                   → empty (cmdNextSubpass)
Subpass 3: tone_mapping      → reads backup_odd → writes backup_even     ← DELETE
Subpass 4: color_grading     → reads backup_even → writes post_process_odd ← DELETE
Subpass 5: fxaa              → reads post_process_odd → writes backup_odd ← DELETE (overwrites PT!)
Subpass 6: UI                → writes backup_even                         ← KEEP
Subpass 7: combine_ui        → reads backup_odd + backup_even → swapchain ← KEEP
```

**Target chain:**

```
Subpass 0-5: skip all (cmdNextSubpass × 6, no draw calls)
Subpass 6: UI                → unchanged
Subpass 7: combine_ui        → unchanged (reads backup_odd=PT output, backup_even=UI)
```

**Tech Stack:** C++17, D3D12, Piccolo RHI

---

## How It Works

1. `backup_odd` holds the PT output (written by `PathTracingPass::dispatch()`)
2. Subpasses 0-5 are skipped — `backup_odd` is never overwritten (FXAA's color attachment target is `backup_odd`, but with no draw call it stays untouched)
3. Subpass 6 (UI): writes UI elements to `backup_even`
4. Subpass 7 (combine_ui):
   - `g_scene_color` (t0) = `backup_odd` = raw PT output
   - `g_ui_color` (t1) = `backup_even` = UI (black where no UI)
   - Pixels without UI → shader returns `scene_color` (PT output)
   - Pixels with UI → shader returns `gamma_ui` (UI overlay)

No changes to clear values or the combine_ui shader are needed. `clearUIAttachment()` (called at the start of subpass 6) already clears `backup_even` to `(0,0,0,0)` including alpha via `cmdClearAttachments`. The render pass loadOp's clear value is overwritten before combine_ui reads the attachment. Non-UI pixels naturally have r=g=a=0, passing the nearly-black check and returning `scene_color`.

---

## File Map

| File | Change | Description |
|------|--------|-------------|
| `engine/source/.../render/passes/main_camera_pass.h` | Modify | Simplify `drawPathTracing` signature |
| `engine/source/.../render/passes/main_camera_pass.cpp` | Modify | Skip tone_mapping/color_grading/fxaa draws; fix backup_even clear alpha |
| `engine/source/.../render/render_pipeline.cpp` | Modify | Update call site |

No new shaders. No new render passes. No new pipelines.

---

## Tasks

### Task 1: Rewrite `drawPathTracing()` — skip 3 redundant passes

**Files:**
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp`

- [ ] **Step 1: Replace tone_map/color_grading/fxaa draws with cmdNextSubpass skips**

In `drawPathTracing()`, replace:

```cpp
// OLD (3 individual cmdNextSubpass + 3 draw calls):
m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);  // subpass 1
m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);  // subpass 2
m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);  // subpass 3
tone_mapping_pass.draw();
m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);  // subpass 4
color_grading_pass.draw();
m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);  // subpass 5
if (m_enable_fxaa) fxaa_pass.draw();
m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);  // subpass 6
clearUIAttachment();
drawAxis();
ui_pass.draw();
```

With:

```cpp
// NEW (6 cmdNextSubpass skips, zero intermediate draws):
for (int i = 0; i < 6; ++i)
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);
// Now in subpass 6 (UI) — unchanged from rasterization
clearUIAttachment();
drawAxis();
ui_pass.draw();
```

No other changes needed:
- `backup_even` loadOp=CLEAR is irrelevant — `clearUIAttachment()` overwrites it to (0,0,0,0) in subpass 6
- `backup_odd` stays unchanged (PT output) — fxaa is skipped so it's never overwritten
- combine_ui reads backup_odd (PT output) + backup_even (UI, zero where no UI) → returns PT output for non-UI pixels

- [ ] **Step 2: Commit**

---

### Task 2: Simplify `drawPathTracing()` signature

**Files:**
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.h`
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp`

- [ ] **Step 1: Update declaration**

Remove `ColorGradingPass&`, `FXAAPass&`, `ToneMappingPass&` from parameter list:

```cpp
// BEFORE:
void drawPathTracing(ColorGradingPass& color_grading_pass,
                     FXAAPass&         fxaa_pass,
                     ToneMappingPass&  tone_mapping_pass,
                     UIPass&           ui_pass,
                     CombineUIPass&    combine_ui_pass,
                     uint32_t          current_swapchain_image_index);

// AFTER:
void drawPathTracing(UIPass&           ui_pass,
                     CombineUIPass&    combine_ui_pass,
                     uint32_t          current_swapchain_image_index);
```

- [ ] **Step 2: Update definition to match**

- [ ] **Step 3: Commit**

---

### Task 3: Update call site in `render_pipeline.cpp`

**Files:**
- Modify: `engine/source/runtime/function/render/render_pipeline.cpp`

- [ ] **Step 1: Update call**

```cpp
// BEFORE:
static_cast<MainCameraPass*>(m_main_camera_pass.get())
    ->drawPathTracing(color_grading_pass,
                      fxaa_pass,
                      tone_mapping_pass,
                      ui_pass,
                      combine_ui_pass,
                      current_swapchain_image_index);

// AFTER:
static_cast<MainCameraPass*>(m_main_camera_pass.get())
    ->drawPathTracing(ui_pass,
                      combine_ui_pass,
                      current_swapchain_image_index);
```

- [ ] **Step 2: Remove unused local variables**

Check if `color_grading_pass`, `fxaa_pass`, `tone_mapping_pass` local variables become unused and remove them.

- [ ] **Step 3: Commit**

---

### Task 4: Build verification

- [ ] **Step 1: Build**

```bash
cd d:/program/Piccolo/build
cmake --build . --config Debug --target PiccoloEditor 2>&1 | tail -5
```

- [ ] **Step 2: Fix any compilation errors**

---

## Impact Summary

| Metric | Before | After |
|--------|--------|-------|
| tone_mapping_pass.draw() | Yes | **Removed** |
| color_grading_pass.draw() | Yes | **Removed** |
| fxaa_pass.draw() | Yes | **Removed** |
| ui_pass.draw() | Yes | Yes (unchanged) |
| combine_ui_pass.draw() | Yes | Yes (unchanged) |
| New shaders | — | **Zero** |
| New pipelines | — | **Zero** |
| New render passes | — | **Zero** |
| GPU full-screen passes saved | — | 3 (tone map + color grade + FXAA) |
| PT output path | PT → tone_map → color_grade → FXAA → combine → swapchain | PT → combine → swapchain |
