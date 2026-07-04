# Vulkan Path Tracing 验证层与 BLAS 整改计划

| 字段 | 内容 |
|------|------|
| 文档版本 | v1.0 |
| 创建日期 | 2026-07-04 |
| 依据日志 | `D:\program\Piccolo\bin\logs\piccolo.log`（13:58:15 启动） |
| 状态 | **Phase 1 方案 B 修正已实施**（2026-07-04，待运行时验证） |
| 前置已完成 | 方案 A：Path Tracing binding 11 默认 2D 纹理（CUBE→2D 验证错误已消除） |

---

## 1. 背景与目标

### 1.1 当前运行状态（日志确认）

- Path Tracing 初始化成功，`effective_mode=PathTracing`，无 raster fallback
- 运行时 **无** `VUID-vkCmdPipelineBarrier-srcAccessMask-02815`（ParticlePass barrier 已修）
- 运行时 **无** `VUID-vkCmdTraceRaysKHR-None-02699`（binding 11 CUBE/2D 已修）
- Path Tracing 渲染已激活（`Path tracing rendering active`）

### 1.2 待解决问题

| 编号 | 类型 | 严重程度 | 简述 |
|------|------|----------|------|
| P1 | 功能性 | 高 | 大量 mesh BLAS 构建被跳过，场景几何无法进入 TLAS |
| P2 | 验证层 | 高 | Editor 退出时 GPU 资源在 in-flight 状态被销毁/更新 |
| P3 | 验证层 | 中 | Accumulation ping-pong 图像 layout 跟踪不一致 |
| P4 | 验证层 | 低（可选） | Path Tracing 单 descriptor set 跨帧 update 的潜在竞态 |

### 1.3 整改目标

1. 消除 `ensurePathTracingBLAS` 的 RT build-input 警告，使带骨骼数据的 mesh 在静态实例路径下可建 BLAS
2. Editor 正常关闭时，验证层 **零报错**（至少无 destroy-in-use / descriptor-in-use）
3. 不改变已有 Path Tracing 渲染管线架构，改动范围限定在本仓库

---

## 2. 问题 P1：BLAS 构建被跳过

### 2.1 现象

日志约 100 条（52–151 行）：

```text
[warning] Path tracing BLAS build skipped because mesh buffers lack RT build-input usage
```

退出 unload 阶段仍可能出现 1 条同类警告。

### 2.2 根因分析

#### 调用链

```text
PathTracingPass::buildTopLevelAS()
  → RenderResource::ensurePathTracingBLAS()   // 仅处理 enable_vertex_blending == false 的实例
      → 检查 mesh.path_tracing_vertex_blas_input_ready
      → 检查 mesh.path_tracing_index_blas_input_ready
```

#### 核心矛盾

| 维度 | 行为 |
|------|------|
| Mesh 上传（`getOrCreateMesh`） | 若 `mesh_data.m_skeleton_binding_buffer` 存在 → `updateMeshData(..., enable_vertex_blending=true)` |
| RT 标志（`supportsPathTracingMeshInputs`） | 仅当 `static_geometry == true` 时为 position buffer 添加 `ACCELERATION_STRUCTURE_BUILD_INPUT` |
| 实例分类（`RenderEntity`） | `m_enable_vertex_blending = transforms.size() > 1`（可与 mesh 资源形态不一致） |
| Path Tracing 静态路径 | `instance.enable_vertex_blending == false` 时走 `ensurePathTracingBLAS`，使用 bind-pose position buffer |

**结论**：mesh 资源按 skinned 路径创建（无 RT vertex 标志），但实例被当作静态几何时，BLAS 必然失败。

#### 关键代码位置

| 文件 | 行号（约） | 说明 |
|------|-----------|------|
| `render_resource.cpp` | 20–23 | `supportsPathTracingMeshInputs(rhi, static_geometry)` |
| `render_resource.cpp` | 1000–1026 | `getOrCreateMesh` 按 skeleton_binding 分支 |
| `render_resource.cpp` | 1450–1455 | skinned 分支：`path_tracing_vertex_blas_input_ready = false` |
| `render_resource.cpp` | 1758–1761 | index buffer：RT 标志正常 |
| `render_resource.cpp` | 457–461 | `ensurePathTracingBLAS` 失败点 |
| `path_tracing_pass.cpp` | 1016–1033 | 静态 vs skinned 实例分流 |
| `render_system.cpp` | 429–430 | entity 级 blending 判定 |

### 2.3 修复方案（采用方案 B）

#### 方案对比

| 方案 | 描述 | 评估 |
|------|------|------|
| A | 仅给 skinned mesh 的 position buffer 加 RT build-input | 最小改动，解决静态实例 BLAS |
| **B（推荐）** | A + 明确静态实例用 bind-pose BLAS、skinned 实例仍走 GpuSkinning | 语义清晰，与现有双路径一致 |
| C | 为 RT 单独分配 position/index buffer | 内存翻倍，改动面大 |

#### 方案 B 详细设计

**原则**：skinned mesh 的 **position buffer**（bind-pose 顶点）与 index buffer 均可作为 BLAS 输入；变形后的几何仍由 `GpuSkinningPass` + `buildPathTracingBLASFromSkinned` 负责。

**代码改动**：

1. **`render_resource.cpp` — `updateVertexBuffer(enable_vertex_blending=true)` 分支**
   - `withPathTracingBuildInputUsage(..., static_geometry=true)` 用于 **position buffer**（不再传 `!enable_vertex_blending`）
   - 设置 `now_mesh.path_tracing_vertex_blas_input_ready = rhi->supportsRayTracing()`
   - varying / joint binding buffer **不加** RT 标志（非 BLAS 输入）

2. **（可选增强）`ensurePathTracingBLAS`**
   - 失败日志区分 vertex/index 哪一侧未 ready，便于后续排查

3. **不改**
   - skinned 实例仍 skip `ensurePathTracingBLAS`，走 GpuSkinning 路径
   - `getOrCreateMesh` 的 skeleton_binding 分支逻辑

**预期结果**：

- 带 skeleton_binding 但被当作静态实例的 mesh 可成功建 BLAS
- 纯 skinned 动画实例行为不变
- 日志中 `lack RT build-input usage` 警告消失或仅剩真正不支持的 mesh

### 2.4 验证标准（P1）

- [ ] 启动 Editor，加载 `1-1.level.json`，Path Tracing 模式下运行 ≥10 秒
- [ ] `piccolo.log` 中 **无** `BLAS build skipped because mesh buffers lack RT build-input usage`
- [ ] 场景中静态物体（建筑、道具等）在 Path Tracing 画面中可见
- [ ] 有骨骼动画的角色仍正常显示（GpuSkinning + per-instance BLAS）

---

## 3. 问题 P2：退出时 GPU 同步不足

### 3.1 现象

退出阶段（日志 155–203 行）验证层报错，主要包括：

| VUID / 类型 | 数量级 | 含义 |
|-------------|--------|------|
| `vkUpdateDescriptorSets` + descriptor in use | 15 条 | Path Tracing 全量写 descriptor，GPU 仍在使用 |
| `VkImageMemoryBarrier-oldLayout-01197` | 1 条 | accumulation image layout 不匹配 |
| `QueueForwardProgress` (semaphore) | 1 条 | 信号量未 wait 再次 signal |
| `vkDestroyFramebuffer/Pipeline/Buffer/Image/AS ... in use` | 多条 | teardown 时 GPU 未完成 |

### 3.2 根因分析

#### Shutdown 顺序（`global_context.cpp`）

```text
shutdownSystems()
  1. m_debugdraw_manager.reset()
  2. m_render_system->clear()
       → shutdownUIRenderBackend()
       → waitForFences()          // ⚠ 仅 wait 1 个 fence
       → render_pipeline->clear() // 所有 pass teardown
       → render_resource->clear()
       → rhi->clear()             // vkDeviceWaitIdle（太晚，destroy 已发生）
  3. m_world_manager->clear()
  ...
```

#### 关键缺陷

| 缺陷 | 位置 | 说明 |
|------|------|------|
| 单 fence 等待 | `VulkanRHI::waitForFences()` | 只 wait `m_is_frame_in_flight_fences[m_current_frame_index]` |
| 对比：正确做法 | `VulkanRHI::recreateSwapchain()` | wait **全部** `k_max_frames_in_flight` 个 fence |
| Particle 独立 fence | `particle_pass.cpp` | compute / copy 提交，`RenderSystem::clear` 未等待 |
| 延迟销毁 AS | `PathTracingPass::destroyTopLevelAS()` | push 到 pending，teardown 时可能仍 in-flight |
| Descriptor 全量更新 | `path_tracing_pass.cpp` | `invalidateStaticDescriptors()` 后 15 binding 重写，与 in-flight CB 冲突 |

#### Descriptor 15 路写入说明

Path Tracing descriptor 共 15 个 write（binding 0–14）。当 `m_static_descriptors_written == false` 时一次性全部更新。退出前若 TLAS dirty 触发 `invalidateStaticDescriptors()`，最后一次 dispatch 会全量 update，与尚未完成的 `vkCmdTraceRaysKHR` 冲突。

### 3.3 修复方案（分三步）

---

#### 步骤 2.1：Shutdown 前完整 GPU drain（**必做，核心**）

**目标**：在销毁任何 pass / GPU 资源之前，保证所有 queue 上已提交工作完成。

**改动 A — `VulkanRHI` 新增接口**

```cpp
// vulkan_rhi.h / rhi.h
void waitAllFramesInFlight();  // wait k_max_frames_in_flight 全部 fence
void waitDeviceIdle();         // 封装 vkDeviceWaitIdle（可选，shutdown 专用）
```

实现参考 `recreateSwapchain()` 4496–4497 行：

```cpp
_vkWaitForFences(m_device, k_max_frames_in_flight, m_is_frame_in_flight_fences, VK_TRUE, UINT64_MAX);
```

**改动 B — `RenderSystem::clear()`**

```text
shutdownUIRenderBackend()
waitAllFramesInFlight()           // 替代单次 waitForFences()
ParticlePass::waitPendingCompute() // 见步骤 2.2
waitDeviceIdle()                   // 兜底，确保 destroy 前 GPU 完全空闲
render_scene->clear()
render_pipeline->clear()
...
```

**改动 C — 各 Pass `teardown()` 顺序**

在 `RenderPipeline::clear()` 调用各 pass `teardown()` **之前**，GPU 必须已 idle（由 RenderSystem 保证）。Pass teardown 内 **不再** 提交新 command。

**涉及文件**：

| 文件 | 改动 |
|------|------|
| `interface/rhi.h` | 声明 `waitAllFramesInFlight()` |
| `interface/vulkan/vulkan_rhi.h/.cpp` | 实现 |
| `interface/d3d12/d3d12_rhi.h/.cpp` | D3D12 等价实现（wait 所有 frame fence） |
| `render_system.cpp` | `clear()` 调用链调整 |

---

#### 步骤 2.2：ParticlePass compute fence 同步（**必做**）

**目标**：消除 ParticlePass 独立 compute queue / fence 导致的 in-flight 泄漏。

**改动**：

1. `ParticlePass` 新增 `waitAllPendingGpuWork()` 或于现有 `teardown()` 开头调用：
   - wait 所有 `m_compute_fences[frame]`
   - wait copy command 相关 fence（若存在）
2. `RenderSystem::clear()` 在 `render_pipeline->clear()` 之前，通过 pipeline 或直接调用 particle pass 的 wait 方法

**涉及文件**：

| 文件 | 改动 |
|------|------|
| `passes/particle_pass.h` | 声明 `waitAllPendingGpuWork()` |
| `passes/particle_pass.cpp` | 实现；`teardown()` 开头调用 |
| `render_pipeline.cpp` 或 `render_system.cpp` | shutdown 时触发 wait |

---

#### 步骤 2.3：PathTracingPass teardown 加固（**必做**）

**改动**：

1. `teardown()` 开头：`flushPendingDestroys()` — 在 **GPU idle 之后** 立即销毁 pending AS（不再留到下一帧）
2. `destroyAccumulationImage()` 前无需额外 barrier（device idle 已保证）
3. `m_descriptor_set = nullptr` 之前不再调用 `updateDescriptorSet()`

**涉及文件**：

| 文件 | 改动 |
|------|------|
| `passes/path_tracing_pass.cpp` | `teardown()` 顺序调整 |

---

### 3.4 验证标准（P2）

- [ ] 启动 Editor → Path Tracing 模式 → 运行数秒 → 正常关闭
- [ ] `piccolo.log` 退出阶段 **无** `validation layer: Validation Error`
- [ ] 特别检查：无 `vkDestroyDevice-device-00378`、无 `in use by a command buffer`
- [ ] 快速关闭（启动后 1 秒内关）也应无验证层错误

---

## 4. 问题 P3：Accumulation 图像 Layout 跟踪

### 4.1 现象

```text
VUID-VkImageMemoryBarrier-oldLayout-01197
VkImage ... cannot transition from VK_IMAGE_LAYOUT_GENERAL
when the previous known layout is VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
```

涉及 Path Tracing accumulation ping-pong 缓冲（日志中 handle `0x73b89b0000000057`）。

### 4.2 根因分析

`PathTracingPass::dispatch()` 使用双缓冲 accumulation：

```text
帧开始：
  accumulation       : tracked_layout → GENERAL（写入）
  accumulation_prev  : tracked_layout → SHADER_READ_ONLY（读取）

帧结束：
  accumulation 保持 GENERAL
  swap(accumulation, accumulation_prev) + swap(layout 变量)
```

**可能不一致点**：

1. ping-pong swap 后，`m_accumulation_*_image_layout` 与 GPU 实际 layout 在边界帧不一致
2. `ensureAccumulationImage()` / `destroyAccumulationImage()` 重建时未重置跟踪状态
3. 最后一帧 dispatch 后 swap，shutdown 前无 transition，teardown 直接 destroy

### 4.3 修复方案

**改动 1 — 统一 layout 跟踪**

在 `path_tracing_pass.cpp` 的 `dispatch()` 末尾 swap 块后，添加注释并核对：

| 物理图像 | 帧结束后应有 layout | 跟踪变量 |
|----------|---------------------|----------|
| 当前 `m_accumulation_image`（下帧写入目标） | `SHADER_READ_ONLY`（若上帧作 prev 读）或 `GENERAL` | 与 barrier oldLayout 严格一致 |
| 当前 `m_accumulation_prev_image`（下帧读 prev） | `GENERAL`（上帧写入） | 同上 |

建议在 swap 后 **显式赋值** layout 跟踪变量，而非仅依赖 swap：

```cpp
// 伪代码：swap 后明确记录
RHIImageLayout new_write_layout = RHI_IMAGE_LAYOUT_GENERAL;
RHIImageLayout new_read_layout  = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
// 根据 swap 结果写入 m_accumulation_image_layout / m_accumulation_prev_image_layout
```

**改动 2 — teardown 安全**

P2 的 `waitDeviceIdle()` 已覆盖 destroy 安全性；可选在 `destroyAccumulationImage()` 中将 layout 跟踪置 `UNDEFINED`。

**涉及文件**：

| 文件 | 改动 |
|------|------|
| `passes/path_tracing_pass.cpp` | `dispatch()` swap 后 layout 跟踪；必要时首帧 barrier 从 `UNDEFINED` 起步 |

### 4.4 验证标准（P3）

- [ ] Path Tracing 连续运行 ≥60 秒（含相机移动触发 accumulation reset）
- [ ] 运行期间及退出时 **无** `oldLayout-01197`
- [ ] 多帧累积收敛正常（画面无闪烁/全黑）

---

## 5. 问题 P4：Descriptor 双缓冲（可选）

### 5.1 背景

Path Tracing 当前使用 **单个** `m_descriptor_set`，每帧 `updateDescriptorSet()` 更新 dynamic binding；static binding 在 `invalidateStaticDescriptors()` 后全量 15 路重写。

在 P2 修复（shutdown 前 device idle）后，**退出时** descriptor in-use 应消失。若运行时仍偶发 in-use，再实施本项。

### 5.2 方案（暂缓，按需）

1. 按 `getMaxFramesInFlight()` 分配 descriptor set 数组
2. 每帧 bind/update 对应 frame index 的 set
3. `invalidateStaticDescriptors()` 标记所有 frame 的 static 部分需重写

**涉及文件**：`path_tracing_pass.h/.cpp`，可能涉及 descriptor pool 容量调整。

### 5.3 触发条件

- P2 完成后，正常运行（非 shutdown）仍出现 `vkUpdateDescriptorSets ... in use`

---

## 6. 实施计划

### 6.1 阶段划分

| 阶段 | 内容 | 预估改动文件数 | 依赖 |
|------|------|----------------|------|
| **Phase 1** | P2 步骤 2.1–2.3（GPU shutdown 同步） | 5–7 | 无 |
| **Phase 2** | P1 方案 B（mesh position RT 标志） | 1–2 | 无，可与 Phase 1 并行 |
| **Phase 3** | P3 accumulation layout | 1 | Phase 1 完成后验证 |
| **Phase 4** | P4 descriptor 双缓冲（可选） | 2 | 仅当 Phase 1–3 后仍有问题 |

### 6.2 推荐实施顺序

```text
1. Phase 1  → 验证退出无 validation error
2. Phase 2  → 验证 BLAS 警告消失、场景几何完整
3. Phase 3  → 验证 layout 错误消失
4. Phase 4  → 按触发条件决定
```

### 6.3 具体任务清单

#### Phase 1 任务

- [ ] T1.1 `RHI` / `VulkanRHI` 添加 `waitAllFramesInFlight()`
- [ ] T1.2 `D3D12RHI` 添加等价实现
- [ ] T1.3 `RenderSystem::clear()` 改用 `waitAllFramesInFlight()` + `vkDeviceWaitIdle`
- [ ] T1.4 `ParticlePass` 添加并在 teardown 前调用 compute fence wait
- [ ] T1.5 `PathTracingPass::teardown()` 调整 pending AS 销毁时机
- [ ] T1.6 编译 `PiccoloRuntime` + `PiccoloEditor`
- [ ] T1.7 按 §3.4 验证

#### Phase 2 任务

- [ ] T2.1 `updateVertexBuffer(true)` 分支为 position buffer 添加 RT build-input
- [ ] T2.2 设置 `path_tracing_vertex_blas_input_ready = rhi->supportsRayTracing()`
- [ ] T2.3 （可选）`ensurePathTracingBLAS` 细化失败日志
- [ ] T2.4 编译验证
- [ ] T2.5 按 §2.4 验证

#### Phase 3 任务

- [ ] T3.1 审计 `dispatch()` ping-pong swap 与 layout 变量
- [ ] T3.2 修正 swap 后 layout 跟踪逻辑
- [ ] T3.3 首帧 / reset 时 layout 从 `UNDEFINED` 过渡
- [ ] T3.4 按 §4.4 验证

### 6.4 不在本次范围

- 重新引入 `VulkanGpuObjectTracker` 调试工具
- Path Tracing shader 功能扩展
- 新增自动化测试（除非后续明确要求）
- 修改 `engine/configs` 或用户 INI

---

## 7. 风险与回滚

| 风险 | 影响 | 缓解 |
|------|------|------|
| `vkDeviceWaitIdle` 拖慢关闭 | 关闭 Editor 多等待 ~100ms | 仅 shutdown 路径使用，不影响运行时 |
| skinned mesh position buffer 加 RT flag | 略增 buffer usage 限制 | 仅 position buffer，非全 mesh 复制 |
| D3D12 后端 wait 行为不一致 | 双后端 shutdown 表现不同 | D3D12 同步实现一并修改 |

**回滚**：各 Phase 独立，可按文件 git revert 单阶段改动。

---

## 8. 验收总表

| # | 验收项 | 通过标准 |
|---|--------|----------|
| 1 | 运行时 BLAS | 无 RT build-input 警告 |
| 2 | 场景完整性 | Path Tracing 下静态物体可见 |
| 3 | 退出验证层 | 关闭 Editor 后 log 无 Validation Error |
| 4 | 快速关闭 | 启动 1 秒内关闭仍无 validation error |
| 5 | 长时运行 | 60 秒 + 相机移动无 layout / semaphore 错误 |
| 6 | 回归 | Raster 模式、Particle、DebugDraw 正常 |
| 7 | 已知已修项 | 无 02699、无 02815、无 vkDestroyDevice 对象泄漏 |

---

## 9. 附录

### 9.1 相关源文件索引

| 模块 | 路径 |
|------|------|
| Path Tracing Pass | `engine/source/runtime/function/render/passes/path_tracing_pass.cpp/.h` |
| Render Resource | `engine/source/runtime/function/render/render_resource.cpp/.h` |
| Render System | `engine/source/runtime/function/render/render_system.cpp` |
| Render Pipeline | `engine/source/runtime/function/render/render_pipeline.cpp` |
| Particle Pass | `engine/source/runtime/function/render/passes/particle_pass.cpp/.h` |
| Vulkan RHI | `engine/source/runtime/function/render/interface/vulkan/vulkan_rhi.cpp/.h` |
| Global Shutdown | `engine/source/runtime/function/global/global_context.cpp` |
| Path Tracing Shader | `engine/shader/hlsl/path_tracing.lib.hlsl` |

### 9.2 日志关键片段索引（2026-07-04 13:58）

| 行号 | 内容 |
|------|------|
| 1–14 | Path Tracing 就绪，无 fallback |
| 52–151 | BLAS build skipped 警告 |
| 153 | Level unload |
| 155–169 | Descriptor update in use（15 writes） |
| 170 | Image layout 01197 |
| 171 | Semaphore forward progress |
| 173–203 | Destroy in use（framebuffer/pipeline/buffer/image/AS） |

### 9.3 审核签字栏

| 角色 | 决定 | 日期 |
|------|------|------|
| 负责人 | ☐ 批准 Phase 1 ☐ 批准 Phase 2 ☐ 批准 Phase 3 ☐ 暂缓 Phase 4 | |
| 实施人 | | |

---

*本文档存放于 `Docs/plans/`。*
