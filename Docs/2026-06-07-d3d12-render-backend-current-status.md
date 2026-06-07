# D3D12 渲染后端当前接入状态

## 当前分支 / 日期

- 日期：2026-06-07
- Worktree：`D:\program\Piccolo\.worktrees\d3d12-render-backend`
- 分支：`feat/d3d12-render-backend`
- 文档依据：实现计划、现有进度记录、`ReleaseNotes.md`、最近 12 条提交日志，以及 D3D12 RHI 关键实现抽查。

## 总体状态

D3D12 渲染后端已经从早期占位实现推进到可接入 Piccolo 主渲染流程的阶段。Windows 侧后端选择、D3D12 默认启用、Vulkan 回退、DXIL shader 生成、descriptor/root signature、render pass/framebuffer、pipeline、资源创建、copy/barrier、queue/fence/semaphore、ImGui DX12 分支、swapchain resize 等核心路径均已有实现或阶段性补齐。

当前整体进度接近实现计划中的 Task 11「D3D12 Command Recording And Submission」后段与 Task 13「Smoke Tests, CI, And Documentation」阶段。已有 Debug 构建与 D3D12 启动烟测覆盖，但还不能视为完整完成：submit/present 错误处理、复杂 copy/subresource 场景、长时间运行、频繁 resize、跨后端/跨平台回归验证仍需要继续推进。

## 已完成内容

### 后端选择与平台默认

- 新增 `RenderBackend` / `RenderBackendAllowFallback` 配置。
- 启动流程支持 `Auto`、`Vulkan`、`D3D12` 后端选择。
- Windows 下 `Auto` 默认选择 D3D12；非 Windows 平台继续以 Vulkan 为默认。
- D3D12 初始化失败时可按配置回退 Vulkan。
- Editor development/deployment 配置已经显式选择 `RenderBackend=D3D12`。
- README 已记录后端选择、DXIL shader bytecode 需求、WARP smoke validation 以及非 Windows 禁用 D3D12 路径等行为。

### RHI 与渲染资源解耦

- 公共 RHI 与渲染资源接口已从 Vulkan/VMA 类型中解耦，VMA 保留在 Vulkan 后端内部。
- 已引入后端中立的 GPU 资源结构，减少渲染层对 Vulkan 命名和 Vulkan helper 的依赖。
- `RenderSystem` viewport 读写等路径已通过 RHI 抽象统一。

### D3D12 资源、描述符与 pipeline

- D3D12 descriptor heap allocator 已存在，descriptor set/root signature 路径已接入。
- descriptor 更新已改为 CPU staging heap 写入后复制到 shader-visible heap。
- uniform buffer backing resource 已按 CBV 要求对齐，资源创建失败会显式上报。
- graphics/compute pipeline 创建、pipeline layout、shader bytecode 入口等主路径已有实现。
- D3D12 HLSL/DXIL shader 生成路径已接入构建系统；缺少 `dxc` 时会跳过 D3D12 shader 并通过运行时回退能力降低启动风险。

### Render pass、framebuffer 与 resource state

- D3D12 render pass/framebuffer emulation 已接入。
- subpass handling 已增强：保留 begin info/clear values，按 subpass 绑定 input/color/depth/resolve attachment layout。
- loadOp 只在附件首次使用时应用，subpass 边界会处理 resolve/copy。
- image resource barrier 已接入 per-subresource 状态跟踪，copy、upload、render pass 和显式 image barrier 可按 mip/layer 范围转换。

### Command、同步与生命周期

- D3D12 queue submit 已接入 fence-backed queue/fence/semaphore 语义，支持按具体 RHI fence 等待、重置和提交 signal。
- 默认 command pool 已接入 RHI 生命周期入口，默认帧命令缓冲改为通过 `allocateCommandBuffers` 创建。
- `prepareContext` 已同步 swapchain back buffer、当前 frame index 与命令缓冲，确保每帧资源更新阶段使用正确 ring buffer 槽位。
- sampler/swapchain 生命周期已补齐，默认 sampler、mipmap sampler、swapchain wrapped image/view 和 back buffer 引用均有销毁路径。
- teardown ordering 已整理，RenderSystem 清理顺序调整为先释放渲染资源与 pipeline，再关闭 RHI。
- ImGui Vulkan/DX12 backend 会在 RHI 清理前显式 shutdown。
- D3D12 command debug markers 已映射到 command-list `BeginEvent` / `EndEvent`，便于 GPU 捕获查看 pass marker。

### Swapchain resize 与 CI/烟测

- `prepareBeforePass` 已接入 framebuffer 尺寸变化检测，swapchain 重建成功后会触发 pipeline 的 framebuffer 依赖资源刷新。
- D3D12 设备创建在无可用硬件 adapter 时可回退到 WARP software adapter，便于 Windows CI 执行启动烟测。
- Windows CI 已构建 Debug/Release `PiccoloEditor`，Debug 构建会运行 D3D12 backend smoke。
- smoke 脚本验证日志中出现 `Initialized RHI backend: D3D12` 与 `engine start`。

## 进行中内容

### Task 11：D3D12 Command Recording And Submission

Task 11 的主体能力已经落地，包括 vertex/index/descriptor binding、copy 命令、queue submit、fence/semaphore signal/wait 以及基础 submit/present 流程。不过当前仍应视为进行中，原因如下：

- `submitRendering` 已调用 `Present(1, 0)`，但 present 返回值处理仍偏弱，后续需要补充 DXGI error/device removed/swapchain out-of-date 等分支处理。
- queue submit 已有多处 HRESULT 日志，但还需要统一 submit、signal、present、device removed 的错误传播和恢复策略。
- `cmdCopyImageToImage` 当前按现有接口/调用场景处理 mip0/layer0，尚未覆盖更通用的多 mip、多 array layer、多区域复制语义。
- copy 与 barrier 路径虽已有 per-subresource 状态跟踪，但复杂组合仍需要 D3D12 debug layer 与实际场景继续验证。

### Task 13：Smoke Tests、CI 与文档

- CI 和 smoke validation 已经接入。
- README 与 ReleaseNotes 已记录 D3D12 后端迁移状态。
- 当前文档补齐的是“当前接入状态”视角，后续仍需要随着 Task 11 收尾和完整验证持续更新。

## 未完成内容

- submit/present 错误处理仍需增强，包括 `Present` HRESULT 检查、DXGI swapchain 状态、device removed reason、可恢复/不可恢复路径区分。
- `cmdCopyImageToImage` 尚未通用化到完整 mip/layer/region 复制。
- 仍需用 D3D12 debug layer / PIX 检查 descriptor heap、root signature、resource state、command list 生命周期与 swapchain resize 后资源重建。
- 仍需确认 Windows 强制 Vulkan 路径无回归。
- 仍需确认 Linux/macOS Vulkan 路径不受 D3D12 接入影响，且非 Windows 平台不会编译 D3D12 源文件。
- 仍需验证首帧之后的长时间运行、频繁窗口 resize、资源热更新、场景切换和退出清理边界。
- 当前验证以 Debug 构建、启动烟测和 Windows CI 为主，尚缺更完整的 CTest/单元测试覆盖。
- shader parity、复杂 subpass/resource state 组合、粒子 compute 与图形调试层长期稳定性仍需持续验证。

## 风险 / 注意事项

- D3D12 与 Vulkan 的资源状态模型差异较大，subpass emulation 和 per-subresource barrier 是后续最容易出现隐藏问题的区域。
- shader parity 仍是长期风险：GLSL/SPIR-V 与 HLSL/DXIL 需要保持绑定布局、结构体布局和语义一致。
- 当前 smoke test 只能证明启动和基础日志路径，不等同于完整渲染正确性验证。
- WARP fallback 有利于 CI 启动验证，但不能替代真实硬件 GPU 上的性能、同步和 debug layer 验证。
- resize callback 已接入，但频繁 resize、最小化/恢复、swapchain buffer 重新创建后的 framebuffer 依赖刷新仍应重点验证。
- D3D12 lifecycle 已经过多轮整理，后续改动需要继续保持 RenderSystem、UI backend、RHI device、swapchain、sampler、command pool、fence/semaphore 的销毁顺序一致。
- 仓库处于多人协作状态，后续修改应基于当前 worktree 状态增量推进，不要 revert 或覆盖他人改动。

## 建议下一阶段

1. 收尾 Task 11：补强 `submitRendering` / `queueSubmit` / `Present` 的错误处理和日志，明确 device removed、swapchain resize/out-of-date、signal/wait 失败时的恢复策略。
2. 泛化 `cmdCopyImageToImage`：支持多 mip、多 layer、更多 region 场景，并同步完善 subresource state 更新。
3. 运行 D3D12 debug layer 与 PIX 捕获：重点检查 descriptor heap、root signature、resource barrier、command list close/reset、swapchain resize 后资源状态。
4. 扩展验证矩阵：Windows D3D12、Windows Vulkan、Linux Vulkan、macOS Vulkan。
5. 做编辑器实际场景验证：首帧、持续运行、窗口 resize、UI 绘制、主相机 pass、post-processing、debug draw、particles、picking、退出清理。
6. 在不扩大实现范围的前提下补充关键轻量测试：backend config、RHI converter、descriptor/resource barrier 辅助逻辑。
7. 持续更新 `ReleaseNotes.md` 和本文档，确保“已完成”和“仍需验证”边界清晰。

## 参考信息摘要

最近 12 条提交显示当前增量集中在 D3D12 细节收口：

```text
c3c2b38 docs: add d3d12 backend progress report
3d4f112 feat: handle d3d12 swapchain resize callbacks
ae34bdc feat: finalize d3d12 teardown ordering
9165b43 feat: create d3d12 default command pool
1f01321 feat: sync d3d12 frame context
0d4f8cd feat: manage d3d12 sampler and swapchain lifetimes
a27cb0d feat: add d3d12 command debug markers
d9e74cd feat: shutdown imgui backends before rhi cleanup
d743536 feat: track d3d12 image subresource states
7c78cb1 feat: improve d3d12 render pass subpass handling
1204773 feat: implement d3d12 queue fence synchronization
d1b5427 refactor: use backend-neutral render resource structs
```

这些提交与现有进度记录共同表明：D3D12 后端主路径已基本接入，当前重点已经从“大块功能缺失”转向 command submit/present、copy 泛化、资源状态正确性和验证矩阵完善。
