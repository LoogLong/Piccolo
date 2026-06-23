# Simplify Path Tracing Post-Processing Pipeline

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan.

**Goal:** Replace the 8-subpass rasterization post-processing chain in `drawPathTracing()` with a streamlined 2-subpass present pass. Remove unnecessary tone mapping, color grading, and FXAA passes from the path tracing pipeline.

**Current state:** `drawPathTracing()` reuses the rasterization pipeline's `m_path_tracing_composite_render_pass` — an 8-subpass render pass designed for deferred rendering. For path tracing, subpasses 0-2 do nothing, subpasses 3-5 (tone mapping, color grading, FXAA) are redundant raster post-effects that add no value to a Monte Carlo path tracer.

```
CURRENT (8 subpasses, 3 empty + 3 redundant):
  Subpass 0: forward_lighting  → empty (no input, no draw call)
  Subpass 1:                   → empty (cmdNextSubpass skip)
  Subpass 2:                   → empty (cmdNextSubpass skip)
  Subpass 3: tone_mapping      → HDR→LDR (unnecessary: PT output is already LDR via accumulation)
  Subpass 4: color_grading     → LUT (unnecessary: can be done in PT shader if needed)
  Subpass 5: fxaa              → AA (unnecessary: PT has per-pixel multisampling)
  Subpass 6: ui
  Subpass 7: combine_ui

TARGET (2 subpasses):
  Subpass 0: composite         → read PT accumulation, write to swapchain
  Subpass 1: ui                → overlay UI
```

**Tech Stack:** C++17, D3D12, HLSL SM 6.0, Piccolo RHI

---

## File Map

| File | Change | Description |
|------|--------|-------------|
| `engine/shader/hlsl/path_tracing_composite.vert.hlsl` | **NEW** | Full-screen triangle vertex shader |
| `engine/shader/hlsl/path_tracing_composite.frag.hlsl` | **NEW** | Samples PT accumulation texture |
| `engine/source/.../render/passes/main_camera_pass.h` | Modify | Add present pass members; simplify `drawPathTracing` signature |
| `engine/source/.../render/passes/main_camera_pass.cpp` | Modify | Setup new present pass; rewrite `drawPathTracing()` |
| `engine/source/.../render/render_pipeline.cpp` | Modify | Update `drawPathTracing()` call site |
| `engine/source/.../render/render_shader_bytecode.h` | Modify | Register new shader macros |

---

## Design

### Render Pass Structure

The new present pass has 2 attachments:

| Index | Attachment | Role |
|-------|-----------|------|
| 0 | `m_scene_output_image` (backup_odd) | Input — path tracing accumulation output, loaded from previous pass |
| 1 | swapchain image | Color — final output, presented to screen |

Subpass dependencies:

```
Subpass 0 (composite):
  Input:  attachment 0 (PT output, SHADER_READ_ONLY_OPTIMAL)
  Color:  attachment 1 (swapchain, COLOR_ATTACHMENT_OPTIMAL)

  Dependency: external → subpass 0
    srcStage: RAY_TRACING_SHADER | srcAccess: SHADER_WRITE
    dstStage: FRAGMENT_SHADER   | dstAccess: SHADER_READ

Subpass 1 (ui):
  Input:  none (uses separate textures)
  Color:  attachment 1 (swapchain, COLOR_ATTACHMENT_OPTIMAL)

  Dependency: subpass 0 → subpass 1
    srcStage: COLOR_ATTACHMENT_OUTPUT | srcAccess: COLOR_ATTACHMENT_WRITE
    dstStage: COLOR_ATTACHMENT_OUTPUT | dstAccess: COLOR_ATTACHMENT_WRITE
```

### Shaders

**Vertex shader** — procedural full-screen triangle, no vertex buffer needed:

```hlsl
// 3 vertices cover entire NDC space: (-1,-1), (3,-1), (-1,3)
// Using SV_VertexID to generate positions:
//   id=0 → (-1,-1, 0,0)  (bottom-left texcoord)
//   id=1 → ( 3,-1, 2,0)  (bottom-right texcoord)
//   id=2 → (-1, 3, 0,2)  (top-left texcoord)

struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};
```

**Fragment shader** — sample path tracing output, output to swapchain:

```hlsl
Texture2D g_path_tracing_output : register(t0);
SamplerState g_linear_sampler : register(s0);

float4 main(VSOutput input) : SV_Target
{
    return g_path_tracing_output.Sample(g_linear_sampler, input.texcoord);
}
```

No tone mapping, no FXAA, no color grading. The path tracing accumulation product is displayed directly.

### API Changes

**`MainCameraPass::drawPathTracing()`** — simplified signature:

```cpp
// BEFORE:
void drawPathTracing(ColorGradingPass&, FXAAPass&, ToneMappingPass&,
                     UIPass&, CombineUIPass&, uint32_t);

// AFTER:
void drawPathTracing(UIPass& ui_pass, CombineUIPass& combine_ui_pass,
                     uint32_t current_swapchain_image_index);
```

**`RenderPipeline::pathTracingRender()`** — call site simplified:

```cpp
// BEFORE:
static_cast<MainCameraPass*>(m_main_camera_pass.get())
    ->drawPathTracing(color_grading_pass, fxaa_pass, tone_mapping_pass,
                      ui_pass, combine_ui_pass, current_swapchain_image_index);

// AFTER:
static_cast<MainCameraPass*>(m_main_camera_pass.get())
    ->drawPathTracing(ui_pass, combine_ui_pass, current_swapchain_image_index);
```

---

## Tasks

### Task 1: Create composite shaders

- [ ] **Step 1: Create `path_tracing_composite.vert.hlsl`**

Full-screen triangle vertex shader. Uses `SV_VertexID` to generate positions without a vertex buffer.

```hlsl
struct VSInput
{
    uint   vertex_id   : SV_VertexID;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    // Full-screen triangle: 3 vertices cover NDC [-1,1]x[-1,1]
    // vertex_id=0: (-1,-1) texcoord(0,0)
    // vertex_id=1: ( 3,-1) texcoord(2,0)
    // vertex_id=2: (-1, 3) texcoord(0,2)
    float x = (input.vertex_id == 1) ? 3.0 : -1.0;
    float y = (input.vertex_id == 2) ? 3.0 : -1.0;
    float u = (input.vertex_id == 1) ? 2.0 : 0.0;
    float v = (input.vertex_id == 2) ? 2.0 : 0.0;

    VSOutput output;
    output.position = float4(x, y, 0.0, 1.0);
    output.texcoord = float2(u, v);
    return output;
}
```

- [ ] **Step 2: Create `path_tracing_composite.frag.hlsl`**

```hlsl
Texture2D g_path_tracing_output : register(t0);
SamplerState g_linear_sampler : register(s0);

struct VSOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

float4 main(VSOutput input) : SV_Target
{
    return g_path_tracing_output.Sample(g_linear_sampler, input.texcoord);
}
```

- [ ] **Step 3: Register bytecode macros**

In `render_shader_bytecode.h`, add entries for the new shaders (D3D12 and Vulkan).

- [ ] **Step 4: Commit**

---

### Task 2: Setup new present render pass in MainCameraPass

- [ ] **Step 1: Add member variables to `main_camera_pass.h`**

```cpp
// New: simplified path tracing present pass (composite + UI only)
RHIRenderPass*         m_path_tracing_present_render_pass {nullptr};
std::vector<RHIFramebuffer*> m_path_tracing_present_framebuffers;

// Composite pipeline
RHIDescriptorSetLayout* m_pt_composite_descriptor_set_layout {nullptr};
RHIPipelineLayout*      m_pt_composite_pipeline_layout {nullptr};
RHIPipeline*            m_pt_composite_pipeline {nullptr};
RHIDescriptorSet*       m_pt_composite_descriptor_set {nullptr};
RHISampler*             m_pt_composite_sampler {nullptr};
```

- [ ] **Step 2: Implement setup function in `main_camera_pass.cpp`**

Create `setupPathTracingPresentPass()`:
1. Create descriptor set layout (1 binding: combined image sampler for PT output)
2. Create pipeline layout
3. Allocate descriptor set
4. Create graphics pipeline (full-screen triangle, no depth, no cull, no blend)
5. Create sampler
6. Create render pass with 2 attachments (PT output + swapchain), 2 subpasses
7. Create framebuffers for each swapchain image

- [ ] **Step 3: Add setup call in initialization**

Call `setupPathTracingPresentPass()` from the MainCameraPass setup flow (after the path tracing init info is available).

- [ ] **Step 4: Commit**

---

### Task 3: Rewrite `drawPathTracing()` with simplified pipeline

- [ ] **Step 1: Update `drawPathTracing()` signature and body**

```cpp
void MainCameraPass::drawPathTracing(UIPass& ui_pass,
                                     CombineUIPass& combine_ui_pass,
                                     uint32_t current_swapchain_image_index)
{
    RHICommandBuffer* cmd = m_rhi->getCurrentCommandBuffer();

    // Update descriptor: bind path tracing output texture
    {
        RHIDescriptorImageInfo image_info {};
        image_info.imageView   = m_scene_output_image_view;  // PT output
        image_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_info.sampler     = m_pt_composite_sampler;

        RHIWriteDescriptorSet write {};
        write.sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = m_pt_composite_descriptor_set;
        write.dstBinding      = 0;
        write.descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &image_info;
        m_rhi->updateDescriptorSets(1, &write, 0, nullptr);
    }

    // Begin render pass
    RHIRenderPassBeginInfo begin_info {};
    begin_info.renderPass  = m_path_tracing_present_render_pass;
    begin_info.framebuffer = m_path_tracing_present_framebuffers[current_swapchain_image_index];
    begin_info.renderArea  = {{0, 0}, m_rhi->getSwapchainInfo().extent};

    RHIClearValue clear_values[2] {};
    clear_values[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};  // PT output: clear (won't be used, loaded from prior pass)
    clear_values[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}};  // swapchain: clear to black
    begin_info.clearValueCount = 2;
    begin_info.pClearValues    = clear_values;

    m_rhi->cmdBeginRenderPassPFN(cmd, &begin_info, RHI_SUBPASS_CONTENTS_INLINE);

    // Subpass 0: Composite — full-screen triangle reading PT output
    m_rhi->cmdBindPipelinePFN(cmd, RHI_PIPELINE_BIND_POINT_GRAPHICS, m_pt_composite_pipeline);
    m_rhi->cmdBindDescriptorSetsPFN(cmd, RHI_PIPELINE_BIND_POINT_GRAPHICS,
                                     m_pt_composite_pipeline_layout, 0, 1,
                                     &m_pt_composite_descriptor_set, 0, nullptr);
    m_rhi->cmdDraw(cmd, 3, 1, 0, 0);  // 3 vertices = full-screen triangle

    // Subpass 1: UI
    m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);
    clearUIAttachment();
    drawAxis();
    ui_pass.draw();

    // Subpass 2: Combine UI — if still needed, or merge into subpass 1
    m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);
    combine_ui_pass.draw();

    m_rhi->cmdEndRenderPassPFN(cmd);
}
```

- [ ] **Step 2: Update call site in `render_pipeline.cpp`**

Remove `color_grading_pass`, `fxaa_pass`, `tone_mapping_pass` from the call.

- [ ] **Step 3: Commit**

---

### Task 4: Build verification

- [ ] **Step 1: Build**

```bash
cmake --build . --config Debug --target PiccoloEditor
```

- [ ] **Step 2: Fix any compilation errors**

---

## Impact Summary

| Metric | Before | After |
|--------|--------|-------|
| Subpasses | 8 | 3 (composite + ui + combine_ui) |
| Draw calls in post-process | 5 (tone_map + color_grading + fxaa + ui + combine) | 3 (composite + ui + combine) |
| Intermediate RTs | 6 (gbuffer A/B/C, backup_odd/even, post_process_odd/even) | 1 (PT output only) |
| GPU bandwidth | ~6 full-screen reads + ~5 writes per frame | ~1 read + 2 writes per frame |
| Tone mapping | Yes (unnecessary) | No |
| Color grading | Yes (unnecessary) | No |
| FXAA | Yes (unnecessary for PT) | No |
| Temporal accumulation | Yes (in PT shader) | Yes (unchanged) |
