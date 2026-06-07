# D3D12 渲染后端当前进度

> 盘点时间：2026-06-07  
> 工作区：`D:\program\Piccolo\.worktrees\d3d12-render-backend`  
> 当前 HEAD：`56a6c1b refactor: remove shared vulkan naming leaks`  
> 说明：本文是接手说明，不代表最终验收完成。本文只整理当前状态，不包含源码实现变更，也未新增测试。

## 当前进度

项目仍沿 `Docs\2026-06-06-d3d12-render-backend-implementation-plan.md` 推进，目标是 Windows 默认使用 D3D12，同时保留 Vulkan 后端。相较现有审计文档 `Docs\2026-06-07-d3d12-render-backend-progress-audit.md` 记录的 `76fdf29` 状态，当前主线已推进到 `56a6c1b`，最近提交继续收敛了公共渲染层里的 Vulkan 命名泄漏。

当前工作树存在一处未提交源码修改：

- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`

该修改集中在 D3D12 RHI 的 public `createSwapchain()`、`copyBuffer()` 和 `cmdCopyBuffer()`。从 diff 看，它在补齐真实 GPU copy 路径、参数校验、错误日志、host-side 镜像边界检查，以及 public swapchain 创建路径。本文未改动这些代码。

## 已完成内容

- 后端选择已经抽出为 `render_backend.h/.cpp`，Windows 默认 D3D12，非 Windows 默认 Vulkan；配置中 `RenderBackend=D3D12` 和 fallback 语义已有落地记录。
- 公共 RHI/资源接口已大幅去 Vulkan 化，引入了 `RHIAllocation`、后端中立 GPU resource 命名、后端中立 shader bytecode 入口等结构。
- Vulkan 后端仍保留，并继续作为非 Windows 默认和 Windows fallback/强制 Vulkan 路径存在。
- D3D12 基础设施已大量接入，包括 D3D12 资源包装、descriptor heap、descriptor set/root signature、render pass/framebuffer emulation、pipeline、command recording/submission、present/recreate 路径、subresource state 跟踪和 D3D12 debug 信息输出。
- D3D12 ImGui 分支已经存在，`ui_pass.cpp` 中可见 `ImGui_ImplDX12_Init/NewFrame/RenderDrawData/Shutdown` 路径；Vulkan UI 分支仍保留。
- 构建与验证脚本已纳入项目：Windows smoke 脚本、日志断言、CI 相关配置、`ReleaseNotes.md` 和已有进度审计文档均记录过 D3D12/Auto/Vulkan 启动验证。
- 最近提交 `56a6c1b` 的方向是清理共享层 Vulkan 命名泄漏，说明主线仍在执行计划中的公共接口收敛工作。

## 未完成内容

- 不能把 D3D12 后端视为最终完成；更准确的状态是主渲染路径已大规模接入，仍处于补齐、收敛和验证阶段。
- `D3D12RHI` 中仍存在 `m_dummy_*` RHI 包装对象，用于适配现有 RHI 形状。它们不一定都是渲染假实现，但后续应逐项确认哪些只是兼容包装，哪些仍遮蔽真实后端能力。
- D3D12 buffer copy 正在被并行修改中，当前未提交 diff 已把 `copyBuffer/cmdCopyBuffer` 从 CPU 镜像 fallback 推向 GPU `CopyBufferRegion`。该改动尚未提交，接手者应先评审并验证它。
- `host_data` 镜像仍在 D3D12 buffer/readback/map 路径中使用。它可能仍是必要的 CPU 可见镜像，但不能再把它误认为 D3D12 copy 的唯一实现。
- Shader parity 仍需持续确认，尤其是 HLSL 与 GLSL/SPIR-V 的 binding、结构布局、矩阵/坐标约定和 compute 路径一致性。
- Vulkan 专有引用扫描仍能在 Vulkan 目录和 UI 的 Vulkan 分支看到大量 `Vk`/`Vulkan*`，这是预期；但共享层和非 Vulkan 分支仍需保持扫描约束，避免再次泄漏。
- 非 Windows 构建隔离、长时间运行、频繁 resize/swapchain recreate、复杂 mip/layer copy、readback、资源热更新和 D3D12 debug layer 长跑仍需补验证。

## 当前风险与待验证项

- **未提交实现风险**：`d3d12_rhi.cpp` 当前 dirty，且涉及 copy/swapchain 关键路径。它看起来不与主线方向冲突，但尚未经过本次文档整理验证。
- **copy 状态转换风险**：`copyBuffer()` 和 `cmdCopyBuffer()` 需要确保 default/upload/readback heap 的状态转换、恢复状态、host 镜像更新和 invalid region 行为都与上层调用期望一致。
- **swapchain public API 风险**：public `createSwapchain()` 补全后要确认不会与 `initialize()` 中的 HWND swapchain 创建、`recreateSwapchain()`、RTV heap 复用和 frame index 更新发生重复创建或生命周期冲突。
- **render pass emulation 风险**：D3D12 对 Vulkan subpass 的模拟仍是核心风险区，重点关注 attachment transition、final state、resolve/copy、depth/stencil 与 shader-readable offscreen 资源。
- **descriptor/root signature 风险**：descriptor table 顺序、set/binding 映射、sampler heap、CBV/SRV/UAV range 分配和 graphics/compute 绑定需要继续用实际 pass 覆盖。
- **跨平台风险**：D3D12 源文件、DX12 ImGui backend、DXIL 生成和 Windows SDK 依赖必须继续被 Windows guard 隔离，Linux/macOS 应只走 Vulkan。
- **验证证据风险**：现有审计文档记录过上一轮 Debug/Release build、D3D12/Auto/Vulkan smoke boot 通过，但本次文档整理没有重新运行构建或烟测。

## 下一阶段建议

1. 先处理当前 dirty 的 `d3d12_rhi.cpp`：代码评审 public `createSwapchain()`、`copyBuffer()`、`cmdCopyBuffer()`，确认没有与已提交生命周期逻辑冲突后再运行验证。
2. 在 Windows 上重新跑最小验证：Debug `PiccoloEditor` build、D3D12 smoke boot、Auto smoke boot、Vulkan smoke boot，并检查日志中没有 D3D12 fallback 或 debug layer 错误。
3. 针对 copy 路径补做实际资源场景验证：staging 到 default、default 到 default、invalid region、upload/readback 边界、mesh/material/debugdraw/particle 中调用 `copyBuffer/cmdCopyBuffer` 的路径。
4. 跑一次 Vulkan 强制路径，确认最近 D3D12 copy/swapchain 变更没有污染 Vulkan。
5. 在可用环境中补 Linux/macOS 或至少非 Windows configure/build 检查，确认 D3D12 相关源码和 ImGui DX12 backend 不进入非 Windows 编译路径。
6. 继续执行计划尾部验收扫描：关注共享渲染层中的 `VulkanRHI`、`VulkanUtil`、`VmaAllocation`、`Vk` 是否只出现在允许的 Vulkan 专用区域或明确的 Vulkan 分支。

## 依据的关键事实

- `git status --short` 显示仅 `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp` 被修改。
- `git log --oneline -8` 显示当前 HEAD 为 `56a6c1b refactor: remove shared vulkan naming leaks`，其前一批提交包括 D3D12 validation、image copy、present、progress audit 等。
- 现有审计文档记录上一轮 HEAD 为 `76fdf29 feat: tighten d3d12 backend validation`，并说明上一轮已有 Debug/Release build、D3D12 smoke、Auto smoke、Vulkan smoke 的验证记录。
- 当前 diff 显示 `d3d12_rhi.cpp` 的未提交修改补了 public `createSwapchain()`，并加强 `copyBuffer()` / `cmdCopyBuffer()` 的 GPU resource 校验、`CopyBufferRegion` 路径和 host_data 边界检查。
- 代码扫描显示 `ui_pass.cpp` 已有 D3D12 ImGui 调用，同时保留 Vulkan ImGui 分支；D3D12 RHI 仍存在 `m_dummy_*` wrapper 和 `host_data` 镜像。
