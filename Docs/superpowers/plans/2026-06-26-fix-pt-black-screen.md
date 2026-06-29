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
| `engine/source/runtime/function/render/passes/main_camera_pass.cpp` | Modify | Restore `backup_even.loadOp = CLEAR` |

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

### Task 2: Build verification

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

| Before | After |
|--------|-------|
| `backup_even.loadOp = DONT_CARE` | `backup_even.loadOp = CLEAR` |
| D3D12 aliases backup_even with swapchain (Resource_48) | Independent memory for each attachment |
| Black screen | Path traced output visible |
