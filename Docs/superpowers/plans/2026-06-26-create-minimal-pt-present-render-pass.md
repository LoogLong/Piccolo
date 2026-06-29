# Create Minimal Path Tracing Present Render Pass

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the 9-attachment path tracing composite render pass (reused from rasterization) with a dedicated 3-attachment render pass. Fixes the Resource_48 black-screen issue where backup_even and swapchain were accidentally sharing the same D3D12 resource.

**Architecture:** Keep the same 8-subpass structure (subpass 6=UI, 7=combine_ui) to maintain pipeline compatibility with CombineUIPass and UIPass. Subpasses 0-5 are empty pass-throughs. Only 3 attachments exist: backup_odd (PT output), backup_even (UI output), swapchain (final output). G-buffers, post-process buffers, and depth are eliminated from the path tracing render pass entirely.

**Root cause:** The current `m_path_tracing_composite_render_pass` has 9 attachments but path tracing only uses 3 (backup_odd, backup_even, swapchain). The 6 unused attachments (gbuffer ×3, post_process ×2, depth) share the same `m_framebuffer.attachments[...].view` across all framebuffers, while the swapchain attachment uses `getSwapchainInfo().imageViews[i]`. The D3D12 RTV heap descriptor assignment caused attachment[4] (backup_even) and attachment[8] (swapchain) to resolve to the same resource (Resource_48), creating a read-write hazard in the combine_ui subpass.

**Tech Stack:** C++17, D3D12, Piccolo RHI

## Global Constraints

- Must not break the rasterization pipeline (it continues using the 9-attachment render pass)
- Must maintain pipeline compatibility with CombineUIPass and UIPass (subpass indices 6 and 7 unchanged)
- Must not require new shaders or new pipeline objects
- Particles in path tracing are temporarily removed (subpass 0 has no depth attachment for the particle pipeline)

---

## File Map

| File | Change | Description |
|------|--------|-------------|
| `engine/source/runtime/function/render/passes/main_camera_pass.h` | Modify | Add members for new 3-attachment render pass + framebuffers; simplify `drawPathTracing` signature |
| `engine/source/runtime/function/render/passes/main_camera_pass.cpp` | Modify | Implement `setupPathTracingPresentRenderPass()`, rewrite `drawPathTracing()` to use new pass, remove particle draw call |
| `engine/source/runtime/function/render/render_pipeline.cpp` | Modify | Update `drawPathTracing()` call site (remove particle_pass parameter) |

---

## Design

### New Render Pass: `m_path_tracing_present_render_pass`

```
Attachment 0: backup_odd   R16G16B16A16_SFLOAT  loadOp=LOAD, initialLayout=SHADER_READ_ONLY_OPTIMAL
Attachment 1: backup_even  R16G16B16A16_SFLOAT  loadOp=DONT_CARE, initialLayout=UNDEFINED
Attachment 2: swapchain    RGBA8_UNORM           loadOp=CLEAR, initialLayout=UNDEFINED, finalLayout=PRESENT_SRC_KHR

Subpass 0-5: empty (cmdNextSubpass pass-through)
Subpass 6 (UI):
  Color[0] = attachment 1 (backup_even)
  Preserve = attachment 0 (backup_odd)

Subpass 7 (Combine UI):
  Input[0] = attachment 0 (backup_odd)    → g_scene_color (t0)
  Input[1] = attachment 1 (backup_even)   → g_ui_color (t1)
  Color[0] = attachment 2 (swapchain)

Dependencies:
  external → subpass 0: RAY_TRACING_SHADER_WRITE → COLOR_ATTACHMENT_WRITE
  subpass 6 → subpass 7: COLOR_ATTACHMENT_WRITE → FRAGMENT_SHADER_READ
```

### Framebuffers

Each swapchain image gets one framebuffer with 3 attachments:

```cpp
framebuffer[i]:
  [0] = m_framebuffer.attachments[_main_camera_pass_backup_buffer_odd].view
  [1] = m_framebuffer.attachments[_main_camera_pass_backup_buffer_even].view
  [2] = m_rhi->getSwapchainInfo().imageViews[i]
```

Key: attachment indices in the framebuffer are 0-2 (not 3, 4, 8 as before). This eliminates the gap that caused the RTV heap collision.

### drawPathTracing() Flow

```
cmdBeginRenderPass(m_path_tracing_present_render_pass)   // 3 attachments, 3 clear values
cmdNextSubpass × 6                                          // skip subpass 0-5
clearUIAttachment()                                        // subpass 6: clear backup_even
drawAxis()
ui_pass.draw()
cmdNextSubpass                                              // → subpass 7
combine_ui_pass.draw()
cmdEndRenderPass
```

Particles are temporarily removed — without a depth attachment in subpass 0, the particle pipeline would fail.

---

## Tasks

### Task 1: Add new render pass and framebuffer members

**Files:**
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.h`

- [ ] **Step 1: Add member declarations**

After the existing `m_path_tracing_composite_render_pass` and `m_path_tracing_composite_swapchain_framebuffers` declarations, add:

```cpp
// Path tracing present render pass (3 attachments: backup_odd, backup_even, swapchain)
RHIRenderPass*              m_path_tracing_present_render_pass {nullptr};
std::vector<RHIFramebuffer*> m_path_tracing_present_framebuffers;
```

- [ ] **Step 2: Update drawPathTracing declaration**

```cpp
// BEFORE:
void drawPathTracing(ParticlePass&  particle_pass,
                     UIPass& ui_pass,
                     CombineUIPass& combine_ui_pass,
                     uint32_t       current_swapchain_image_index);

// AFTER (remove ParticlePass&):
void drawPathTracing(UIPass& ui_pass,
                     CombineUIPass& combine_ui_pass,
                     uint32_t       current_swapchain_image_index);
```

- [ ] **Step 3: Commit**

```bash
git add engine/source/runtime/function/render/passes/main_camera_pass.h
git commit -m "feat: add members for minimal 3-attachment PT present render pass"
```

---

### Task 2: Implement `setupPathTracingPresentRenderPass()`

**Files:**
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp`

**Interfaces:**
- Produces: `m_path_tracing_present_render_pass` (created render pass), `m_path_tracing_present_framebuffers` (created framebuffers)

- [ ] **Step 1: Implement render pass creation**

Add a new function `setupPathTracingPresentRenderPass()` after the existing `setupPathTracingCompositeRenderPass()`. The function creates:

1. **3 attachments** (backup_odd, backup_even, swapchain)
2. **8 subpasses** (0-5 empty, 6=UI, 7=combine_ui)
3. **2 subpass dependencies** (external→subpass 0, subpass 6→subpass 7)
4. **N framebuffers** (3 attachments each, swapchain varied)

```cpp
void MainCameraPass::setupPathTracingPresentRenderPass()
{
    const uint32_t attachment_count = 3;
    RHIAttachmentDescription attachments[3] = {};

    // Attachment 0: backup_odd — PT output, loaded from path tracing pass
    attachments[0].format         = RHI_FORMAT_R16G16B16A16_SFLOAT;
    attachments[0].samples        = RHI_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp         = RHI_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].storeOp        = RHI_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp  = RHI_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = RHI_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout  = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[0].finalLayout    = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Attachment 1: backup_even — UI output, cleared by clearUIAttachment in subpass 6
    attachments[1].format         = RHI_FORMAT_R16G16B16A16_SFLOAT;
    attachments[1].samples        = RHI_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp         = RHI_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].storeOp        = RHI_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp  = RHI_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = RHI_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout  = RHI_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout    = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Attachment 2: swapchain — final output
    attachments[2].format         = m_rhi->getSwapchainInfo().image_format;
    attachments[2].samples        = RHI_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp         = RHI_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp        = RHI_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp  = RHI_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = RHI_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout  = RHI_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout    = RHI_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // 8 subpasses, configured for indices 6 (UI) and 7 (combine_ui)
    const uint32_t subpass_count = 8;
    RHISubpassDescription subpasses[8] = {};
    for (uint32_t i = 0; i < subpass_count; ++i)
        subpasses[i].pipelineBindPoint = RHI_PIPELINE_BIND_POINT_GRAPHICS;

    // Subpass 6 (UI): write to backup_even, preserve backup_odd
    RHIAttachmentReference ui_color {};
    ui_color.attachment = 1;  // backup_even
    ui_color.layout     = RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    subpasses[_main_camera_subpass_ui].colorAttachmentCount = 1;
    subpasses[_main_camera_subpass_ui].pColorAttachments    = &ui_color;
    uint32_t ui_preserve = 0;  // backup_odd
    subpasses[_main_camera_subpass_ui].preserveAttachmentCount = 1;
    subpasses[_main_camera_subpass_ui].pPreserveAttachments    = &ui_preserve;

    // Subpass 7 (combine_ui): read backup_odd + backup_even, write swapchain
    RHIAttachmentReference combine_inputs[2] {};
    combine_inputs[0].attachment = 0;  // backup_odd → g_scene_color
    combine_inputs[0].layout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    combine_inputs[1].attachment = 1;  // backup_even → g_ui_color
    combine_inputs[1].layout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    RHIAttachmentReference combine_color {};
    combine_color.attachment = 2;  // swapchain
    combine_color.layout     = RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    subpasses[_main_camera_subpass_combine_ui].inputAttachmentCount = 2;
    subpasses[_main_camera_subpass_combine_ui].pInputAttachments    = combine_inputs;
    subpasses[_main_camera_subpass_combine_ui].colorAttachmentCount = 1;
    subpasses[_main_camera_subpass_combine_ui].pColorAttachments    = &combine_color;

    // Dependencies
    RHISubpassDependency dependencies[2] {};
    dependencies[0].srcSubpass    = RHI_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass    = 0;
    dependencies[0].srcStageMask  = RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    dependencies[0].dstStageMask  = RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = RHI_ACCESS_SHADER_WRITE_BIT;
    dependencies[0].dstAccessMask = RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass    = _main_camera_subpass_ui;
    dependencies[1].dstSubpass    = _main_camera_subpass_combine_ui;
    dependencies[1].srcStageMask  = RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask  = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = RHI_ACCESS_SHADER_READ_BIT;

    RHIRenderPassCreateInfo create_info {};
    create_info.sType           = RHI_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = attachment_count;
    create_info.pAttachments    = attachments;
    create_info.subpassCount    = subpass_count;
    create_info.pSubpasses      = subpasses;
    create_info.dependencyCount = 2;
    create_info.pDependencies   = dependencies;

    if (m_rhi->createRenderPass(&create_info, m_path_tracing_present_render_pass) != RHI_SUCCESS)
    {
        throw std::runtime_error("failed to create path tracing present render pass");
    }
}
```

- [ ] **Step 2: Implement framebuffer creation**

Add framebuffer creation at the end of the same function:

```cpp
    // Create framebuffers — one per swapchain image
    m_path_tracing_present_framebuffers.resize(m_rhi->getSwapchainInfo().imageViews.size());
    for (size_t i = 0; i < m_rhi->getSwapchainInfo().imageViews.size(); i++)
    {
        RHIImageView* fb_attachments[3] = {
            m_framebuffer.attachments[_main_camera_pass_backup_buffer_odd].view,
            m_framebuffer.attachments[_main_camera_pass_backup_buffer_even].view,
            m_rhi->getSwapchainInfo().imageViews[i]
        };

        RHIFramebufferCreateInfo fb_info {};
        fb_info.sType           = RHI_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass      = m_path_tracing_present_render_pass;
        fb_info.attachmentCount = 3;
        fb_info.pAttachments    = fb_attachments;
        fb_info.width           = m_rhi->getSwapchainInfo().extent.width;
        fb_info.height          = m_rhi->getSwapchainInfo().extent.height;
        fb_info.layers          = 1;

        if (RHI_SUCCESS != m_rhi->createFramebuffer(&fb_info, m_path_tracing_present_framebuffers[i]))
        {
            throw std::runtime_error("create path tracing present framebuffer");
        }
    }
```

- [ ] **Step 3: Call setup from initialization**

In `setupPathTracingCompositeSwapchainFramebuffers()` or the main init flow, add a call:

```cpp
setupPathTracingPresentRenderPass();
```

Also call `setupPathTracingPresentRenderPass()` at the end of `updateAfterFramebufferRecreate()` after the existing framebuffer recreation calls.

- [ ] **Step 4: Commit**

```bash
git add engine/source/runtime/function/render/passes/main_camera_pass.cpp
git commit -m "feat: create minimal 3-attachment PT present render pass"
```

---

### Task 3: Rewrite `drawPathTracing()` to use new render pass

**Files:**
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp`

- [ ] **Step 1: Replace the function body**

The new `drawPathTracing()` uses `m_path_tracing_present_render_pass` (3 attachments, 3 clear values) instead of `m_path_tracing_composite_render_pass` (9 attachments, 9 clear values). Particles are removed (no depth attachment in subpass 0).

```cpp
void MainCameraPass::drawPathTracing(UIPass&        ui_pass,
                                     CombineUIPass& combine_ui_pass,
                                     uint32_t       current_swapchain_image_index)
{
    RHICommandBuffer* cmd = m_rhi->getCurrentCommandBuffer();

    RHIRenderPassBeginInfo begin_info {};
    begin_info.sType       = RHI_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin_info.renderPass  = m_path_tracing_present_render_pass;
    begin_info.framebuffer = m_path_tracing_present_framebuffers[current_swapchain_image_index];
    begin_info.renderArea  = {{0, 0}, m_rhi->getSwapchainInfo().extent};

    RHIClearValue clear_values[3] {};
    clear_values[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};  // backup_odd (not cleared — loadOp=LOAD, value ignored)
    clear_values[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}};  // backup_even (not cleared — loadOp=DONT_CARE)
    clear_values[2].color = {{0.0f, 0.0f, 0.0f, 1.0f}};  // swapchain (cleared to black)
    begin_info.clearValueCount = 3;
    begin_info.pClearValues    = clear_values;

    m_rhi->cmdBeginRenderPassPFN(cmd, &begin_info, RHI_SUBPASS_CONTENTS_INLINE);

    // Skip subpasses 0-5 to reach subpass 6 (UI)
    for (int i = 0; i < 6; ++i)
        m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);

    // Subpass 6: UI
    clearUIAttachment();
    drawAxis();
    ui_pass.draw();

    // Subpass 7: Combine UI
    m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);
    combine_ui_pass.draw();

    m_rhi->cmdEndRenderPassPFN(cmd);
}
```

- [ ] **Step 2: Remove `setupPathTracingCompositeSwapchainFramebuffers()` references in `updateAfterFramebufferRecreate()`**

Replace destruction/recreation of `m_path_tracing_composite_swapchain_framebuffers` with destruction/recreation of `m_path_tracing_present_framebuffers` and `m_path_tracing_present_render_pass`.

- [ ] **Step 3: Commit**

```bash
git add engine/source/runtime/function/render/passes/main_camera_pass.cpp
git commit -m "refactor: use minimal 3-attachment render pass for PT present"
```

---

### Task 4: Update call site in render_pipeline.cpp

**Files:**
- Modify: `engine/source/runtime/function/render/render_pipeline.cpp`

- [ ] **Step 1: Remove particle_pass from drawPathTracing call**

```cpp
// BEFORE:
ParticlePass&    particle_pass   = *(static_cast<ParticlePass*>(m_particle_pass.get()));
UIPass&           ui_pass         = *(static_cast<UIPass*>(m_ui_pass.get()));
CombineUIPass&    combine_ui_pass = *(static_cast<CombineUIPass*>(m_combine_ui_pass.get()));
static_cast<MainCameraPass*>(m_main_camera_pass.get())
    ->drawPathTracing(particle_pass, ui_pass, combine_ui_pass, current_swapchain_image_index);

// AFTER:
UIPass&           ui_pass         = *(static_cast<UIPass*>(m_ui_pass.get()));
CombineUIPass&    combine_ui_pass = *(static_cast<CombineUIPass*>(m_combine_ui_pass.get()));
static_cast<MainCameraPass*>(m_main_camera_pass.get())
    ->drawPathTracing(ui_pass, combine_ui_pass, current_swapchain_image_index);
```

- [ ] **Step 2: Commit**

```bash
git add engine/source/runtime/function/render/render_pipeline.cpp
git commit -m "refactor: remove particle pass from PT present call"
```

---

### Task 5: Build verification

- [ ] **Step 1: Build**

```bash
cd d:/program/Piccolo/build
cmake --build . --config Debug --target PiccoloEditor
```

- [ ] **Step 2: Fix any compilation errors**

---

## Impact Summary

| Metric | Before | After |
|--------|--------|-------|
| Render pass attachments | 9 | 3 |
| Attachment clears | 9 (6 wasted) | 1 (swapchain only) |
| VRAM | ~50MB for unused attachments | ~16MB for backup_odd + backup_even |
| subpass count | 8 | 8 (same, pipeline compat) |
| cmdNextSubpass calls | 5 (after particle removed) | 6 |
| New pipelines | 0 | 0 |
| New shaders | 0 | 0 |
