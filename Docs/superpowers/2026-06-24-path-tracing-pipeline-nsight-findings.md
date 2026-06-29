# Path Tracing Pipeline — Nsight Redundancy Report

> Nsight GPU 抓帧分析，2026-06-24

## 已修复

| # | 问题 | 修复 |
|---|------|------|
| 1 | tone_mapping / color_grading / fxaa 每帧运行 | `afe04e7` — 替换为 `cmdNextSubpass` 跳过 |

---

## 仍存在

### 1. Shadow Map 渲染

**代码:** `render_pipeline.cpp:341-342`

```cpp
static_cast<DirectionalLightShadowPass*>(m_directional_light_pass.get())->draw();
static_cast<PointLightShadowPass*>(m_point_light_shadow_pass.get())->draw();
```

两个 shadow pass 渲染深度图到 shadow map texture。Path tracing **完全不使用 shadow map**——它在 closest-hit shader 中通过 shadow ray (`TraceRay` with `RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH`) 直接计算遮挡。

| 指标 | 估算 |
|------|------|
| DirectionalLightShadowPass | 全场景深度渲染 1 次 |
| PointLightShadowPass | 每个点光源深度渲染 1 次 |
| 写入的纹理 | shadow map depth textures（PT 不读取） |
| GPU 时间 | 场景复杂度 × 光源数 |

**建议:** 从 `pathTracingRender()` 中删除这两行调用。

---

### 2. Render Pass 无用 Attachment

**代码:** `main_camera_pass.cpp:2324-2347` — `drawPathTracing()` 中的 render pass begin

`m_path_tracing_composite_render_pass` 定义了大量 attachment，大部分在 path tracing 中完全用不到：

| Attachment | 格式 | PT 中使用? | 说明 |
|------------|------|-----------|------|
| gbuffer_a | RGBA8_UNORM | ✗ | 光栅化 G-Buffer，PT 不需要 |
| gbuffer_b | RGBA8_UNORM | ✗ | 光栅化 G-Buffer，PT 不需要 |
| gbuffer_c | RGBA8_UNORM | ✗ | 光栅化 G-Buffer，PT 不需要 |
| backup_odd | R16G16B16A16_SFLOAT | ✓ | PT 输出 (m_scene_output_image) |
| backup_even | R16G16B16A16_SFLOAT | ✓ | UI subpass 写入 |
| post_process_odd | R8G8B8A8_SRGB | ✗ | 仅光栅化中间缓冲 |
| post_process_even | R8G8B8A8_SRGB | ✗ | 仅光栅化中间缓冲 |
| depth | D32_SFLOAT | ✗ | 深度缓冲，PT 不需要 |
| swapchain | RGBA8_SRGB | ✓ | 最终输出 |

每帧仍有 6 个 attachment 被 clear（3 GBuffer + 2 post_process + depth），完全是浪费：

```cpp
clear_values[_main_camera_pass_gbuffer_a].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
clear_values[_main_camera_pass_gbuffer_b].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
clear_values[_main_camera_pass_gbuffer_c].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
clear_values[_main_camera_pass_post_process_buffer_odd].color  = {{0.0f, 0.0f, 0.0f, 1.0f}};
clear_values[_main_camera_pass_post_process_buffer_even].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
clear_values[_main_camera_pass_depth].depthStencil = {1.0f, 0};
```

**建议:** 为 path tracing 创建专用的 render pass，只有 2 个 attachment（PT output + swapchain），去掉所有 G-Buffer 和中间缓冲。这会同时省掉 `createRenderPass` 中的 8 个 subpass 描述和 5 个 subpass dependency。

---

### 3. Scene Buffer 每帧全量重建

**代码:** `path_tracing_pass.cpp:827-834` — `buildTopLevelAS()`

```cpp
const bool tlas_dirty =
    has_skinned ||                    // ← 有 skinned mesh → 始终 true
    scene.isPathTracingTLASDirty() ||
    ...;
if (!tlas_dirty) return true;

updatePathTracingSceneBuffers(...);   // 全量重建！
```

当有 skinned mesh 时 `tlas_dirty` 始终为 true → `updatePathTracingSceneBuffers()` 每帧运行。它重建**所有** buffer：

| Buffer | 数据 | 每帧变? | 重建成本 |
|--------|------|---------|----------|
| g_vertices | 静态 mesh 顶点 | ✗ | CPU push + GPU upload |
| g_indices | 静态 mesh 索引（+ skinned 副本） | ✗ (静态部分) | CPU push + GPU upload |
| g_materials | 材质数据 | ✗ | CPU push + GPU upload |
| g_geometries | geometry 记录 | ✗ (除 skinned vertex_offset) | CPU push + GPU upload |
| g_instances | instance 记录 | ✗ (除 transform) | CPU push + GPU upload |

只有 instance transforms 和 skinned vertex data 每帧变化。其余全部可以跳过。

**建议:** 新增 `updatePathTracingInstanceBuffer()` 轻量更新——只重建 instance buffer，其余只在 scene dirty 时重建。

---

### 4. Path Tracing Descriptor Set 全量重写

**代码:** `path_tracing_pass.cpp:670-776` — `updateDescriptorSet()`

每帧写入全部 15 个 descriptor。其中 ~7 个从未变化：

| 不变化的 binding | 内容 |
|------------------|------|
| u1 (scene_output) | 固定的 PT 输出 image view |
| t9 (irradiance) | IBL cubemap |
| t10 (specular) | IBL cubemap |
| t11 (texture_array) | 材质纹理数组 |
| s12 (sampler) | Linear sampler |

每次调用 `rhi->updateDescriptorSets(15, ...)` 比实际需要的多 5-6 个写入。

**建议:** 拆分 descriptor set——static bindings 只在初始化时写一次，frame bindings 每帧更新。

---

### 5. Per-instance BLAS 每帧无条件 destroy + rebuild

**代码:** `path_tracing_pass.cpp:869-877`

```cpp
if (pt_resources.blas != nullptr)
{
    m_rhi->destroyAccelerationStructure(pt_resources.blas);  // GPU 同步点
    pt_resources.blas = nullptr;
}
pt_resources.blas = buildPathTracingBLASFromSkinned(...);   // 重建
```

对于 skinned mesh，因为顶点位置每帧变化，BLAS 确实需要重建。但 `destroyAccelerationStructure` 立即释放 D3D12 资源——这会触发 GPU 同步。可以改为引用计数延迟释放或使用 `allow_update` 模式。

对于**非动画**的 skinned mesh（关节矩阵为单位矩阵时），顶点位置与上一帧相同，BLAS 不需要重建。

**建议:** 比较当前帧和上一帧的 joint matrices，如果相同则跳过 BLAS 重建。

---

### 6. Particle Pass

**代码:** `render_pipeline.cpp:362`

```cpp
static_cast<ParticlePass*>(m_particle_pass.get())->simulate();
```

粒子模拟每帧运行。如果 path tracing 场景中没有粒子，这是纯 CPU 浪费。如果 GPU 粒子在 PT 中不可见，simulate 也无意义。

---

### 7. Debug Draw (Axis)

**代码:** `main_camera_pass.cpp:2367` — `drawAxis()`

每帧画坐标轴。调试用途，Release 应关闭。

---

## 优先级

| 优先级 | 项目 | 影响 |
|--------|------|------|
| 🔴 P0 | #1 Shadow Map 渲染 | GPU 瓶颈：全场景深度 pass × (1 + N_lights) |
| 🔴 P0 | #2 无用 Attachment | GPU 带宽：6 个 RT clear + 内存占用 |
| 🟡 P1 | #3 Scene Buffer 全量重建 | CPU → GPU 带宽：不必要的 buffer upload |
| 🟡 P1 | #5 Per-instance BLAS | GPU 时间：BLAS destroy 触发同步 |
| 🟢 P2 | #4 Descriptor 全量重写 | CPU 开销：多余的 descriptor copy |
| 🟢 P2 | #6 Particle Pass | CPU 开销 |
| 🟢 P3 | #7 Debug Draw | 极少量 GPU 开销 |
