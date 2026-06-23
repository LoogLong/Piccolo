# Simplify Path Tracing Post-Processing Pipeline (v2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan.

**Goal:** Replace the tone mapping and color grading passes in the path tracing composite chain with a minimal passthrough composite shader. FXAA is skipped. The UI and combine_UI subpasses remain unchanged — identical to the rasterization pipeline.

**Key insight:** The existing `m_path_tracing_composite_render_pass` structure (8 subpasses, attachment chain) is already correct for the UI + combine_UI flow. Only the middle passes (tone_mapping, color_grading, fxaa) need to be replaced or skipped.

```
CURRENT drawPathTracing():
  cmdBeginRenderPass(m_path_tracing_composite_render_pass)
  cmdNextSubpass × 3                           // skip subpass 0-2 (unchanged)
  tone_mapping_pass.draw()                     // subpass 3: REPLACE with composite
  cmdNextSubpass
  color_grading_pass.draw()                    // subpass 4: REPLACE with composite
  cmdNextSubpass
  fxaa_pass.draw()                             // subpass 5: REMOVE, skip via cmdNextSubpass
  cmdNextSubpass
  clearUIAttachment + drawAxis + ui_pass.draw() // subpass 6: UNCHANGED
  cmdNextSubpass
  combine_ui_pass.draw()                        // subpass 7: UNCHANGED
  cmdEndRenderPass

TARGET drawPathTracing():
  cmdBeginRenderPass(m_path_tracing_composite_render_pass)
  cmdNextSubpass × 3
  compositePass.draw(backup_odd → backup_even)  // NEW: passthrough composite
  cmdNextSubpass
  compositePass.draw(backup_even → post_process_odd) // NEW: passthrough composite
  cmdNextSubpass                                // skip fxaa (no draw call)
  cmdNextSubpass
  clearUIAttachment + drawAxis + ui_pass.draw() // UNCHANGED
  cmdNextSubpass
  combine_ui_pass.draw()                        // UNCHANGED
  cmdEndRenderPass
```

**Attachment chain (unchanged):**
```
backup_odd (PT output)
    ↓ composite subpass 3 (reads backup_odd, writes backup_even)
backup_even
    ↓ composite subpass 4 (reads backup_even, writes post_process_odd)
post_process_odd (scene)
    ↓ subpass 5 skipped (fxaa removed, post_process_even stays stale)
post_process_even (stale)
    ↓ subpass 6 UI (clears via loadOp + draws UI → post_process_even)
post_process_even (UI)
    ↓ subpass 7 combine_ui (reads post_process_odd + post_process_even → swapchain)
swapchain
```

**Tech Stack:** C++17, D3D12, HLSL SM 6.0, Piccolo RHI

---

## File Map

| File | Change | Description |
|------|--------|-------------|
| `engine/shader/hlsl/path_tracing_composite.vert.hlsl` | **NEW** | Full-screen triangle vertex shader |
| `engine/shader/hlsl/path_tracing_composite.frag.hlsl` | **NEW** | Samples input texture, writes to output attachment |
| `engine/source/.../render/passes/main_camera_pass.h` | Modify | Add composite pipeline members; add `setupPathTracingCompositePass()`; simplify `drawPathTracing` signature |
| `engine/source/.../render/passes/main_camera_pass.cpp` | Modify | Setup composite pipeline + descriptor; rewrite `drawPathTracing()` |
| `engine/source/.../render/render_pipeline.cpp` | Modify | Update `drawPathTracing()` call site |
| `engine/source/.../render/render_shader_bytecode.h` | Modify | Register new shader macros |

---

## Tasks

### Task 1: Create passthrough composite shaders

**Files:**
- Create: `engine/shader/hlsl/path_tracing_composite.vert.hlsl`
- Create: `engine/shader/hlsl/path_tracing_composite.frag.hlsl`
- Modify: `engine/source/runtime/function/render/render_shader_bytecode.h`

- [ ] **Step 1: Create vertex shader**

```hlsl
struct VSOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(uint vertex_id : SV_VertexID)
{
    // Full-screen triangle without vertex buffer
    // id=0: (-1, -1) tex(0, 0)
    // id=1: ( 3, -1) tex(2, 0)
    // id=2: (-1,  3) tex(0, 2)
    VSOutput output;
    output.position = float4(
        (vertex_id == 1) ? 3.0 : -1.0,
        (vertex_id == 2) ? 3.0 : -1.0,
        0.0, 1.0);
    output.texcoord = float2(
        (vertex_id == 1) ? 2.0 : 0.0,
        (vertex_id == 2) ? 2.0 : 0.0);
    return output;
}
```

- [ ] **Step 2: Create fragment shader**

Passthrough — samples input texture and outputs color. Same shader works for both subpass 3 (backup_odd→backup_even) and subpass 4 (backup_even→post_process_odd) because the input attachment is set via descriptor per subpass.

```hlsl
// Declared as input attachment by the render pass;
// descriptor is updated before each subpass to point to the correct attachment.
Texture2D g_input_texture : register(t0);
SamplerState g_sampler : register(s0);

struct VSOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

float4 main(VSOutput input) : SV_Target
{
    return g_input_texture.Sample(g_sampler, input.texcoord);
}
```

- [ ] **Step 3: Register bytecode macros in `render_shader_bytecode.h`**

Add `PICCOLO_VULKAN_PATH_TRACING_COMPOSITE_VERT`, `PICCOLO_VULKAN_PATH_TRACING_COMPOSITE_FRAG` (Vulkan — empty fallback) and `PICCOLO_D3D12_PATH_TRACING_COMPOSITE_VERT`, `PICCOLO_D3D12_PATH_TRACING_COMPOSITE_FRAG` (D3D12).

- [ ] **Step 4: Commit**

---

### Task 2: Setup composite pipeline in MainCameraPass

**Files:**
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.h`
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp`

- [ ] **Step 1: Add members to header**

```cpp
// Path tracing passthrough composite (replaces tone_mapping + color_grading)
RHIDescriptorSetLayout* m_pt_composite_descriptor_set_layout {nullptr};
RHIPipelineLayout*      m_pt_composite_pipeline_layout {nullptr};
RHIPipeline*            m_pt_composite_pipeline {nullptr};
RHIDescriptorSet*       m_pt_composite_descriptor_set {nullptr};
RHISampler*             m_pt_composite_sampler {nullptr};
```

- [ ] **Step 2: Implement `setupPathTracingCompositePass()`**

Called once during path tracing pipeline setup. Creates:

```
Descriptor set layout:
  binding 0: COMBINED_IMAGE_SAMPLER, FRAGMENT_BIT

Pipeline layout:
  1 descriptor set layout

Descriptor set:
  Allocated from descriptor pool (updated per subpass)

Graphics pipeline:
  Vertex: path_tracing_composite.vert
  Fragment: path_tracing_composite.frag
  No depth test/write, no cull, no blend
  Color attachment format: R8G8B8A8_SRGB (same as backup/post_process buffers)

Sampler:
  Linear clamp, point mipmap
```

- [ ] **Step 3: Call `setupPathTracingCompositePass()` from init path**

- [ ] **Step 4: Commit**

---

### Task 3: Rewrite `drawPathTracing()` and simplify call site

**Files:**
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp`
- Modify: `engine/source/runtime/function/render/render_pipeline.cpp`

- [ ] **Step 1: Update descriptor before each composite subpass**

Before subpass 3: bind `backup_odd` (PT output) image view as t0
Before subpass 4: bind `backup_even` image view as t0

The composite shader reads from t0, writes to the subpass color attachment.

- [ ] **Step 2: Rewrite `drawPathTracing()` body**

```cpp
void MainCameraPass::drawPathTracing(UIPass& ui_pass,
                                     CombineUIPass& combine_ui_pass,
                                     uint32_t current_swapchain_image_index)
{
    RHICommandBuffer* cmd = m_rhi->getCurrentCommandBuffer();

    // Begin render pass (same as before)
    // ... setup begin_info ...
    m_rhi->cmdBeginRenderPassPFN(cmd, &begin_info, RHI_SUBPASS_CONTENTS_INLINE);

    // Skip subpasses 0-2
    m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);
    m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);
    m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);

    // --- Subpass 3: Composite backup_odd (PT output) → backup_even ---
    {
        RHIDescriptorImageInfo img_info {};
        img_info.imageView   = m_scene_output_image_view;  // PT output = backup_odd
        img_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        img_info.sampler     = m_pt_composite_sampler;
        RHIWriteDescriptorSet write {};
        write.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_pt_composite_descriptor_set;
        write.dstBinding = 0;
        write.descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &img_info;
        m_rhi->updateDescriptorSets(1, &write, 0, nullptr);
    }
    m_rhi->cmdBindPipelinePFN(cmd, RHI_PIPELINE_BIND_POINT_GRAPHICS, m_pt_composite_pipeline);
    m_rhi->cmdBindDescriptorSetsPFN(cmd, RHI_PIPELINE_BIND_POINT_GRAPHICS,
                                     m_pt_composite_pipeline_layout, 0, 1,
                                     &m_pt_composite_descriptor_set, 0, nullptr);
    m_rhi->cmdDraw(cmd, 3, 1, 0, 0);  // full-screen triangle

    // --- Subpass 4: Composite backup_even → post_process_odd ---
    m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);
    {
        RHIDescriptorImageInfo img_info {};
        img_info.imageView   = m_backup_even_image_view;  // backup_even
        img_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        img_info.sampler     = m_pt_composite_sampler;
        RHIWriteDescriptorSet write {};
        write.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_pt_composite_descriptor_set;
        write.dstBinding = 0;
        write.descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &img_info;
        m_rhi->updateDescriptorSets(1, &write, 0, nullptr);
    }
    m_rhi->cmdBindPipelinePFN(cmd, RHI_PIPELINE_BIND_POINT_GRAPHICS, m_pt_composite_pipeline);
    m_rhi->cmdBindDescriptorSetsPFN(cmd, RHI_PIPELINE_BIND_POINT_GRAPHICS,
                                     m_pt_composite_pipeline_layout, 0, 1,
                                     &m_pt_composite_descriptor_set, 0, nullptr);
    m_rhi->cmdDraw(cmd, 3, 1, 0, 0);

    // --- Subpass 5: Skip FXAA (post_process_odd preserved, post_process_even stale) ---
    m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);
    // No draw — attachment is preserved (or cleared by UI loadOp in next subpass)

    // --- Subpass 6: UI (unchanged from rasterization pipeline) ---
    m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);
    clearUIAttachment();
    drawAxis();
    ui_pass.draw();

    // --- Subpass 7: Combine UI (unchanged from rasterization pipeline) ---
    m_rhi->cmdNextSubpassPFN(cmd, RHI_SUBPASS_CONTENTS_INLINE);
    combine_ui_pass.draw();

    m_rhi->cmdEndRenderPassPFN(cmd);
}
```

- [ ] **Step 3: Simplify `drawPathTracing()` signature**

Remove `ColorGradingPass&`, `FXAAPass&`, `ToneMappingPass&` parameters. Keep only `UIPass&`, `CombineUIPass&`, `uint32_t`.

- [ ] **Step 4: Update call site in `render_pipeline.cpp`**

Remove the three removed parameters from the call. Remove local variables `color_grading_pass`, `fxaa_pass`, `tone_mapping_pass` if they become unused in `pathTracingRender()`.

- [ ] **Step 5: Commit**

---

### Task 4: Build verification

- [ ] **Step 1: Build**

```bash
cmake --build . --config Debug --target PiccoloEditor
```

- [ ] **Step 2: Fix compilation errors if any**

---

## Impact Summary

| Metric | Before | After |
|--------|--------|-------|
| Post-process shaders | 3 (tone_map, color_grading, fxaa) | 1 (composite) |
| tone_mapping_pass.draw() | Yes | Replaced by composite.draw() |
| color_grading_pass.draw() | Yes | Replaced by composite.draw() |
| fxaa_pass.draw() | Yes | Removed (skip subpass) |
| ui_pass.draw() | Unchanged | Unchanged |
| combine_ui_pass.draw() | Unchanged | Unchanged |
| Render pass structure | Unchanged | Unchanged (same attachments, same subpasses) |
| GPU bandwidth (full-screen R/W per frame) | ~5 reads + ~4 writes | ~3 reads + ~3 writes |
