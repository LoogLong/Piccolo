# D3D12 渲染后端当前进度

> 原始盘点时间：2026-06-07
> 原始盘点工作区：`D:\program\Piccolo\.worktrees\d3d12-render-backend`
> 原始盘点 HEAD：`56a6c1b refactor: remove shared vulkan naming leaks`
> 更新：2026-06-08 Task 9 在 `feat/d3d12-windows-replacement` 分支补充了 D3D12-only configure/build/smoke 和自动化生命周期验证；以本提交 HEAD 为准。
> 更新：2026-06-10 `feat/d3d12-render-backend-completion` 分支完成 D3D12/Vulkan parity 和性能收敛计划，代码 HEAD 为 `fb5d83c perf: reduce d3d12 descriptor churn`，后续文档提交记录最终合并状态。
> 说明：前半部分保留 2026-06-07 原始盘点语境，后文追加 2026-06-08 和 2026-06-10 验证证据。本文不包含测试源码变更，也未新增 CTest/engine test。

## 2026-06-10 完成摘要

本轮目标是让 Windows D3D12 后端可作为 Vulkan 渲染路径的可用替代：保持默认场景渲染效果一致、避免 1 FPS 回退、确保 D3D12/Auto/Vulkan 启动路径干净，并保留 Vulkan 作为显式后端。

完成的代码变更：

- `0165dac fix: preserve d3d12 submit ordering`：`D3D12RHI::queueSubmit()` 改为按每个 `RHISubmitInfo` 执行 wait -> command lists -> signal，匹配 Vulkan submit 边界语义。
- `083a8ab feat: clarify d3d12 compute queue semantics`：确认当前 D3D12 command buffer 仍使用 DIRECT command list，compute queue 显式映射到 graphics/direct queue，并记录日志；真实 async compute 留待 queue-typed command pool/list 和资源 ownership 模型补齐后再做。
- `e64b733 fix: align point shadow viewport state`：point light shadow pass 使用 shadow map viewport/scissor，而不是 swapchain viewport/scissor。
- `6bd0d6f fix: align d3d12 fxaa shader`：HLSL FXAA 与 GLSL FXAA 对齐，避免 D3D12 post-process 与 Vulkan 输出明显不同。
- `fb5d83c perf: reduce d3d12 descriptor churn`：为动态 descriptor table 增加 command-buffer 级缓存，按 descriptor set/version/set index/dynamic offsets 复用 transient table；descriptor write/copy 会递增 version，command buffer reset/begin/prepareBeforePass 会清 cache，避免跨帧 stale table。

2026-06-10 验收结论：

- Windows dual-backend Debug build 通过，D3D12 DXIL 编译工具 `dxc.exe` 被 CMake 找到。
- `RenderBackend=D3D12`、`RenderBackend=Auto` 和 `RenderBackend=Vulkan` 三条启动路径均通过 smoke；Auto 在 Windows dual-backend build 中初始化为 D3D12。
- D3D12 visible smoke 捕获到非黑 editor 输出，采样区非黑比例 `100.0000%`。
- 同默认场景截图显示 D3D12 与 Vulkan 的主相机画面、天空、场景几何、角色和 ImGui panels 均可见；采样平均 RGB L1 差异为 `1.49`，未观察到黑屏、上下翻转、明显缺失 pass 或 UI 覆盖。
- D3D12 最终 FPS 采样 `545.90`，Vulkan 最终 FPS 采样 `568.85`，D3D12 低约 `4.03%`，在计划允许的 5% 范围内，且未复现 1 FPS 回退。
- 最新 log scan 未发现 `D3D12 ERROR`、`DEVICE_REMOVED`、`DXGI_ERROR`、resource state、descriptor heap、root signature、command list lifetime、fallback 或 `[error]` 匹配。

仍保留的非阻塞 caveat：

- 当前 compute queue 是显式 same-direct-queue 语义，不是真 async compute。
- 本轮视觉 parity 证据来自自动截图、visible smoke、代码审查和默认场景快速目视；尚未覆盖长时间人工 QA、复杂相机交互、picking ID 稳定性、频繁 resize 长跑、正常 shutdown 和 Linux/macOS native Vulkan build。
- 2026-06-08 记录过的黑屏截图和 Debug assertion 属于历史自动化截图问题；2026-06-10 visible smoke 与同场景截图已证明当前 D3D12 默认场景不再是黑屏。

## 当前进度

原始盘点时，项目仍沿 `Docs\2026-06-06-d3d12-render-backend-implementation-plan.md` 推进，目标是 Windows 默认使用 D3D12，同时保留 Vulkan 后端。相较现有审计文档 `Docs\2026-06-07-d3d12-render-backend-progress-audit.md` 记录的 `76fdf29` 状态，原始盘点主线已推进到 `56a6c1b`，当时最近提交继续收敛了公共渲染层里的 Vulkan 命名泄漏。

原始盘点工作树存在一处未提交源码修改：

- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`

该原始盘点修改集中在 D3D12 RHI 的 public `createSwapchain()`、`copyBuffer()` 和 `cmdCopyBuffer()`。从当时 diff 看，它在补齐真实 GPU copy 路径、参数校验、错误日志、host-side 镜像边界检查，以及 public swapchain 创建路径。本文未改动这些代码。

## 已完成内容

- 后端选择已经抽出为 `render_backend.h/.cpp`，Windows 默认 D3D12，非 Windows 默认 Vulkan；配置中 `RenderBackend=D3D12` 和 fallback 语义已有落地记录。
- 公共 RHI/资源接口已大幅去 Vulkan 化，引入了 `RHIAllocation`、后端中立 GPU resource 命名、后端中立 shader bytecode 入口等结构。
- Vulkan 后端仍保留，并继续作为非 Windows 默认和 Windows fallback/强制 Vulkan 路径存在。
- D3D12 基础设施已大量接入，包括 D3D12 资源包装、descriptor heap、descriptor set/root signature、render pass/framebuffer emulation、pipeline、command recording/submission、present/recreate 路径、subresource state 跟踪和 D3D12 debug 信息输出。
- D3D12 ImGui 分支已经存在，`ui_pass.cpp` 中可见 `ImGui_ImplDX12_Init/NewFrame/RenderDrawData/Shutdown` 路径；Vulkan UI 分支仍保留。
- 构建与验证脚本已纳入项目：Windows smoke 脚本、日志断言、CI 相关配置、`ReleaseNotes.md` 和已有进度审计文档均记录过 D3D12/Auto/Vulkan 启动验证。
- 原始盘点时最近提交 `56a6c1b` 的方向是清理共享层 Vulkan 命名泄漏，说明当时主线仍在执行计划中的公共接口收敛工作。

## 未完成内容

- D3D12 后端已达到 Windows dual-backend 默认场景可替代 Vulkan 的阶段；后续工作转为长跑 QA、真 async compute 和更广覆盖的交互/跨平台验证。
- `D3D12RHI` 中仍存在 `m_dummy_*` RHI 包装对象，用于适配现有 RHI 形状。它们不一定都是渲染假实现，但后续应逐项确认哪些只是兼容包装，哪些仍遮蔽真实后端能力。
- 原始盘点时 D3D12 buffer copy 正在被并行修改中，当时未提交 diff 已把 `copyBuffer/cmdCopyBuffer` 从 CPU 镜像 fallback 推向 GPU `CopyBufferRegion`。该历史状态不代表 Task 9 验证时的工作树状态。
- `host_data` 镜像仍在 D3D12 buffer/readback/map 路径中使用。它可能仍是必要的 CPU 可见镜像，但不能再把它误认为 D3D12 copy 的唯一实现。
- Shader parity 仍需持续确认，尤其是 HLSL 与 GLSL/SPIR-V 的 binding、结构布局、矩阵/坐标约定和 compute 路径一致性。
- Vulkan 专有引用扫描仍能在 Vulkan 目录和 UI 的 Vulkan 分支看到大量 `Vk`/`Vulkan*`，这是预期；但共享层和非 Vulkan 分支仍需保持扫描约束，避免再次泄漏。
- 非 Windows 构建隔离、长时间运行、频繁 resize/swapchain recreate、复杂 mip/layer copy、readback、资源热更新和 D3D12 debug layer 长跑仍需补验证。

## 当前风险与待验证项

- **原始盘点未提交实现风险**：2026-06-07 原始盘点时 `d3d12_rhi.cpp` 为 dirty，且涉及 copy/swapchain 关键路径。该条保留为历史交接背景，不代表 Task 9 验证时的工作树状态。
- **copy 状态转换风险**：`copyBuffer()` 和 `cmdCopyBuffer()` 需要确保 default/upload/readback heap 的状态转换、恢复状态、host 镜像更新和 invalid region 行为都与上层调用期望一致。
- **swapchain public API 风险**：public `createSwapchain()` 补全后要确认不会与 `initialize()` 中的 HWND swapchain 创建、`recreateSwapchain()`、RTV heap 复用和 frame index 更新发生重复创建或生命周期冲突。
- **render pass emulation 风险**：D3D12 对 Vulkan subpass 的模拟仍是核心风险区，重点关注 attachment transition、final state、resolve/copy、depth/stencil 与 shader-readable offscreen 资源。
- **descriptor/root signature 风险**：descriptor table 顺序、set/binding 映射、sampler heap、CBV/SRV/UAV range 分配和 graphics/compute 绑定需要继续用实际 pass 覆盖。
- **跨平台风险**：D3D12 源文件、DX12 ImGui backend、DXIL 生成和 Windows SDK 依赖必须继续被 Windows guard 隔离，Linux/macOS 应只走 Vulkan。
- **验证证据风险**：2026-06-10 已补充 dual-backend configure/build、D3D12/Auto/Vulkan smoke、D3D12 visible capture、同场景截图、FPS pair 和 log scan；交互长跑、picking 稳定性、正常 shutdown 和非 Windows native build 仍需后续 QA。
- **2026-06-08 Task 9 视觉/关闭验证风险**：D3D12-only Debug/Release build、smoke 和自动化生命周期 liveness 已通过，但当时自动化截图为黑屏，一次 Debug capture 显示 Visual C++ Debug Assertion（`debug_heap.cpp:908`，`is_block_type_valid(header->_block_use)`）。该黑屏状态已被 2026-06-10 D3D12 visible smoke 和默认场景截图取代；正常 shutdown 仍未作为本轮 gate 验收。

## 下一阶段建议

1. 原始盘点建议先处理当时 dirty 的 `d3d12_rhi.cpp`：代码评审 public `createSwapchain()`、`copyBuffer()`、`cmdCopyBuffer()`，确认没有与已提交生命周期逻辑冲突后再运行验证。
2. 在 Windows 上重新跑最小验证：Debug `PiccoloEditor` build、D3D12 smoke boot、Auto smoke boot、Vulkan smoke boot，并检查日志中没有 D3D12 fallback 或 debug layer 错误。
3. 针对 copy 路径补做实际资源场景验证：staging 到 default、default 到 default、invalid region、upload/readback 边界、mesh/material/debugdraw/particle 中调用 `copyBuffer/cmdCopyBuffer` 的路径。
4. 跑一次 Vulkan 强制路径，确认最近 D3D12 copy/swapchain 变更没有污染 Vulkan。
5. 在可用环境中补 Linux/macOS 或至少非 Windows configure/build 检查，确认 D3D12 相关源码和 ImGui DX12 backend 不进入非 Windows 编译路径。
6. 继续执行计划尾部验收扫描：关注共享渲染层中的 `VulkanRHI`、`VulkanUtil`、`VmaAllocation`、`Vk` 是否只出现在允许的 Vulkan 专用区域或明确的 Vulkan 分支。

## 依据的关键事实

- 原始盘点时 `git status --short` 显示仅 `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp` 被修改。
- 原始盘点时 `git log --oneline -8` 显示 HEAD 为 `56a6c1b refactor: remove shared vulkan naming leaks`，其前一批提交包括 D3D12 validation、image copy、present、progress audit 等。
- 现有审计文档记录上一轮 HEAD 为 `76fdf29 feat: tighten d3d12 backend validation`，并说明上一轮已有 Debug/Release build、D3D12 smoke、Auto smoke、Vulkan smoke 的验证记录。
- 原始盘点 diff 显示 `d3d12_rhi.cpp` 的未提交修改补了 public `createSwapchain()`，并加强 `copyBuffer()` / `cmdCopyBuffer()` 的 GPU resource 校验、`CopyBufferRegion` 路径和 host_data 边界检查。
- 代码扫描显示 `ui_pass.cpp` 已有 D3D12 ImGui 调用，同时保留 Vulkan ImGui 分支；D3D12 RHI 仍存在 `m_dummy_*` wrapper 和 `host_data` 镜像。

## Windows D3D12 Replacement Acceptance Criteria

- Windows `RenderBackend=D3D12` initializes D3D12 and does not log `Falling back to Vulkan backend`.
- Windows D3D12-primary validation can build and boot without linking PiccoloRuntime or imgui against Vulkan when Vulkan support is disabled by CMake option.
- Windows optional Vulkan validation can still build and boot when Vulkan support is enabled and `RenderBackend=Vulkan`.
- Non-Windows builds continue to enable Vulkan and exclude D3D12 `.cpp` files.
- The editor renders main camera, post-process, debug draw, particles, picking, ImGui, and swapchain resize on D3D12.
- D3D12 logs are clean of debug-layer, descriptor heap, root signature, resource state, command list lifetime, device removed, and fallback errors during boot plus the manual validation run.
- No new unit-test or CTest source files are required for this phase.

## Windows D3D12 Replacement Validation

### 2026-06-10 dual-backend completion evidence

- Branch/code HEAD before documentation update: `fb5d83c perf: reduce d3d12 descriptor churn`.
- Merged to mainline with merge commit `52c53b2 merge: complete d3d12 render backend`; a follow-up documentation update records the mainline verification below.
- Dual-backend configure passed: `cmake -S . -B build -DPICCOLO_ENABLE_VULKAN_BACKEND=ON -DPICCOLO_ENABLE_D3D12_BACKEND=ON`; CMake found `dxc.exe` at `C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/dxc.exe`.
- Debug editor build passed: `cmake --build build --config Debug --target PiccoloEditor -- /clp:ErrorsOnly /v:minimal`.
- D3D12 boot smoke passed with fallback forbidden: `.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback`; log: `build\test_d3d12_boot.log`.
- Auto boot smoke passed with fallback forbidden and initialized D3D12: `.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -TimeoutSeconds 20 -RenderBackend Auto -ExpectedBackend D3D12 -DisallowFallback`; log: `build\test_auto_d3d12_boot.log`.
- Vulkan boot smoke passed in the same dual-backend build: `.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -TimeoutSeconds 20 -RenderBackend Vulkan -ExpectedBackend Vulkan`; log: `build\test_vulkan_boot.log`.
- D3D12 visible smoke passed: `.\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10 -MinNonBlackRatio 0.01`; capture: `build\test_d3d12_editor_visible.png`; sampled non-black ratio: `100.0000%`.
- Same-scene parity captures were recorded outside source control: `build\d3d12_parity\default_D3D12.png` and `build\d3d12_parity\default_Vulkan.png`; both captures were `1280x720`. The visible-smoke crop remained `100.0000%` non-black, while the 4-pixel-step full-capture comparison sampled 57,600 pixels per image with `93.2465%` non-black pixels, average RGB L1 delta `1.49`, and max RGB L1 delta `132`.
- Log scan command `rg -n "D3D12 ERROR|DEVICE_REMOVED|DXGI_ERROR|resource state|descriptor heap|root signature|command list|Falling back to Vulkan|RHI backend initialization failed|\[error\]" build` found no matches in latest build/smoke logs.
- Temporary descriptor trace scan `rg -n "PICCOLO_TRACE|trace_mark|descriptor counter|temporary descriptor marker" engine/source/runtime/function/render/interface/d3d12` found no matches.
- Final FPS gate passed on the default scene: D3D12 collected 40 samples with tail average `545.90 FPS`; Vulkan collected 40 samples with tail average `568.85 FPS`; D3D12 was about `4.03%` below Vulkan and did not regress near `1 FPS`.
- After merging to `main`, the same mainline checkout was freshly configured and built, then ran D3D12 boot, Auto->D3D12 boot, Vulkan boot, and D3D12 visible smoke successfully. The mainline visible smoke capture was `build\test_d3d12_editor_visible.png` with sampled non-black ratio `100.0000%`.
- Mainline latest-smoke log scan over `build\test_d3d12_boot.log`, `build\test_auto_d3d12_boot.log`, `build\test_vulkan_boot.log`, `build\test_d3d12_editor_visible.log`, `build\test_d3d12_editor_visible.stdout.log`, and `build\test_d3d12_editor_visible.stderr.log` found no matches. A whole-`build` scan still matched historical `build\isolate_skip_simulate.txt` from 2026-06-09, which records the old skip-simulate 1 FPS/device-loss reproduction and is not part of the latest smoke run.

### 2026-06-08 D3D12-only evidence

- D3D12-only configure command run: `cmake -S . -B build_d3d12_only -DPICCOLO_ENABLE_VULKAN_BACKEND=OFF -DPICCOLO_ENABLE_D3D12_BACKEND=ON`.
- Debug D3D12-only build passed: `cmake --build build_d3d12_only --config Debug --target PiccoloEditor -- /verbosity:minimal`.
- Release D3D12-only build passed: `cmake --build build_d3d12_only --config Release --target PiccoloEditor -- /verbosity:minimal`.
- Debug D3D12-only smoke passed with fallback forbidden: `.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build_d3d12_only -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback`.
- Release D3D12-only smoke passed with fallback forbidden: `.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build_d3d12_only -Configuration Release -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback`.
- Automated Debug editor lifecycle run launched `build_d3d12_only\engine\source\editor\Debug\PiccoloEditor.exe` with `RenderBackend=D3D12`, observed a main window handle, loaded `asset/world/hello.world.json` and `asset/level/1-1.level.json`, resized the window through 1280x720, 640x480, 320x240, 160x120, and 1024x768, minimized/restored the window, and kept the process alive for 60 seconds after restore. Evidence log: `build_d3d12_only\manual_d3d12_debug_lifecycle.log`.
- Automated lifecycle close was limited: `CloseMainWindow()` returned false, `WM_CLOSE` did not exit within 15 seconds, and cleanup stopped the still-running editor process. Normal shutdown was not validated.
- Automated visual capture attempts did not produce reliable render evidence: `build_d3d12_only\visual_d3d12_debug_capture.png` and `build_d3d12_only\visual_d3d12_debug_screen_capture.png` captured a black client area, and the screen capture also showed a Visual C++ Debug Assertion dialog from `PiccoloEditor.exe` (`debug_heap.cpp`, line 908, `is_block_type_valid(header->_block_use)`). No matching engine log error was emitted.
- Interactive and visual scenarios not run or not proven in this automation pass: camera movement, mesh picking/stable picked IDs, editor UI panel toggles, axis/debug draw trigger, default-level reload workflow, normal shutdown, and visual confirmation that main camera, post-process, ImGui, debug draw, or particles rendered correctly. The 60-second particle interval was only a process/log-liveness check after level load, not visual particle validation.
- Log scan command `rg -n "D3D12 ERROR|DEVICE_REMOVED|DXGI_ERROR|resource state|descriptor heap|root signature|command list|Falling back to Vulkan|RHI backend initialization failed|\[error\]" build_d3d12_only` found no matches across the smoke and automated lifecycle logs.
