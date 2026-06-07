# D3D12 Render Backend Progress Audit

日期：2026-06-07

来源：
- Sub agent Huygens 的只读盘点结果
- 本地 `git status --short --branch`
- 本地 `git log -15 --oneline`
- `Docs/2026-06-06-d3d12-render-backend-implementation-plan.md`
- `Docs/2026-06-07-d3d12-render-backend-current-status.md`
- `README.md`、`ReleaseNotes.md`、`scripts/tests/render_backend/smoke_backend_boot.ps1`

说明：本文件是当前进度整理，不代表最终验收完成。本次整理未重新运行构建、烟测或长时间运行验证。

## 当前仓库状态

- Worktree：`D:\program\Piccolo\.worktrees\d3d12-render-backend`
- 分支：`feat/d3d12-render-backend`
- 当前 HEAD：`76fdf29 feat: tighten d3d12 backend validation`
- 盘点前工作区状态：干净

最近关键提交：

- `76fdf29 feat: tighten d3d12 backend validation`
- `5cb8ee2 docs: mark d3d12 image copy phase complete`
- `b173485 feat: support d3d12 image copy regions`
- `697d725 docs: refresh d3d12 backend current status`
- `8c02a18 feat: handle d3d12 present results`
- `3d4f112 feat: handle d3d12 swapchain resize callbacks`
- `ae34bdc feat: finalize d3d12 teardown ordering`
- `9165b43 feat: create d3d12 default command pool`
- `1f01321 feat: sync d3d12 frame context`
- `0d4f8cd feat: manage d3d12 sampler and swapchain lifetimes`
- `a27cb0d feat: add d3d12 command debug markers`
- `d9e74cd feat: shutdown imgui backends before rhi cleanup`
- `d743536 feat: track d3d12 image subresource states`

## 已完成内容

### 后端选择与平台默认

- 已新增 `render_backend.h/.cpp`，集中处理 `Auto`、`Vulkan`、`D3D12`、`dx12` 配置解析。
- Windows 平台 `Auto` 默认选择 D3D12，非 Windows 默认选择 Vulkan。
- `RenderSystem` 已通过后端工厂创建 Vulkan 或 D3D12 RHI。
- D3D12 工厂实例化在 `_WIN32` 下启用，非 Windows 路径返回 `nullptr`。
- `RenderBackendAllowFallback` 已接入：D3D12 初始化失败且允许回退时，会尝试 Vulkan。
- 随附 development/deployment Editor 配置已设置 `RenderBackend=D3D12` 和 `RenderBackendAllowFallback=true`。

### 构建与平台隔离

- Windows runtime 链接 `d3d12.lib`、`dxgi.lib`、`dxguid.lib`。
- 非 Windows 构建会从 runtime 源文件列表中过滤 `interface/d3d12/*.cpp`。
- `render_system.cpp` 中 D3D12 include 和实例化已受 `_WIN32` guard 保护。
- CMake 会查找 `dxc.exe`。找到时启用 D3D12 shader bytecode 标记；未找到时会生成空 DXIL header，并通过运行时逻辑避免误用缺失的 D3D12 shader。

### 公共接口去 Vulkan 化

- 公共 RHI 与渲染资源接口已从 Vulkan/VMA allocation 类型中解耦。
- VMA allocation 保留在 Vulkan 后端内部。
- GPU mesh/material/global resource 命名和数据结构已改为 backend-neutral 语义。
- Window public header 已移除 `GLFW_INCLUDE_VULKAN` 暴露。

### D3D12 RHI 主体

- `engine/source/runtime/function/render/interface/d3d12` 已包含 D3D12 RHI 与 descriptor heap allocator。
- D3D12 后端拥有自己的 RHI wrapper：buffer、image、image view、sampler、shader、device memory、command buffer/pool、queue、fence、semaphore、descriptor pool/set/layout、pipeline layout、render pass、framebuffer、pipeline。
- D3D12 设备创建支持硬件 adapter；无可用硬件 adapter 时可回退 WARP software adapter，便于 Windows CI 启动烟测。
- D3D12 descriptor set/root signature 路径已接入，descriptor 更新通过 CPU staging heap 写入后复制到 shader-visible heap。
- Uniform buffer backing resource 已按 CBV 要求对齐。
- Shader module、graphics pipeline、compute pipeline 创建路径已接入。
- Render pass/framebuffer emulation 已接入，包含 subpass attachment 绑定、clear value、input/color/depth/resolve attachment layout 处理。
- Command recording 已接入 vertex/index buffer binding、descriptor binding、draw、dispatch、copy、barrier、submit/present 等路径。
- Queue/fence/semaphore 语义已接入 D3D12 fence-backed 实现。
- Image resource barrier 已接入 per-subresource 状态跟踪。
- Image-to-image copy 已支持 region、mip、array layer 与区域边界处理。
- Present/command list close 失败日志已补充 HRESULT 与 device removed reason。
- Swapchain resize/recreate 路径已接入 framebuffer 依赖资源刷新。
- 默认 command pool、frame context、sampler/swapchain 生命周期和 device teardown 顺序已补强。
- Push/pop render event 已映射到 D3D12 command-list event marker。

### 渲染管线与 UI

- D3D12 主渲染路径已接入 shader bytecode、descriptor/root signature、render pass/framebuffer、graphics/compute pipeline、命令录制、粒子、拾取、debug draw 与 post-processing 相关路径。
- `UIPass` 已按 backend 分支初始化 Vulkan ImGui 或 DX12 ImGui。
- D3D12 ImGui 初始化使用实际 swapchain format，避免 DX12 renderer backend 的 render target format 与 back buffer 不一致。
- `RenderSystem` 清理 RHI 前会关闭 ImGui backend，避免 UI renderer/platform 资源晚于图形设备释放。

### 文档、脚本与 CI

- `README.md` 已记录 Render Backend 配置、Windows D3D12 默认、Windows 强制 Vulkan、DXIL 要求、WARP fallback、非 Windows D3D12 禁用和 smoke 命令。
- `ReleaseNotes.md` 已记录 D3D12 后端选择、回退、验证命令和当前状态。
- `scripts/tests/render_backend/smoke_backend_boot.ps1` 支持 `-RenderBackend Auto|Vulkan|D3D12` 与 `-ExpectedBackend Vulkan|D3D12`。
- Smoke 脚本会临时覆盖构建产物旁的 `PiccoloEditor.ini`，结束后恢复原字节。
- Smoke 脚本会断言启动日志包含期望后端与 `engine start`，并禁止出现 `Falling back to Vulkan backend`。
- Windows CI Debug 配置已加入 D3D12 backend smoke 步骤。

## 已验证内容

以下为已有文档和提交记录中可确认的验证结果，不是本次盘点重新运行的结果。

- `git diff --check` 已在上一轮验证中通过。
- Debug `PiccoloEditor` build 已在上一轮验证中通过。
- Release `PiccoloEditor` build 已在上一轮验证中通过。
- D3D12 smoke boot 已在上一轮验证中通过。
- Auto smoke boot 已在上一轮验证中通过，并确认 Windows `Auto` 初始化 D3D12。
- Vulkan smoke boot 已在上一轮验证中通过，并确认 Windows 强制 Vulkan 路径仍可启动。
- D3D12/Auto/Vulkan smoke 日志已扫描，未发现 D3D12 debug/error/fallback 相关匹配项。
- 构建产物中的 Debug/Release `PiccoloEditor.ini` 已确认恢复为 `RenderBackend=D3D12`。

上一轮记录的验证命令：

```powershell
git diff --check
cmake --build build --config Debug --target PiccoloEditor -- /verbosity:minimal
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -Configuration Debug -TimeoutSeconds 20 -RenderBackend Auto -ExpectedBackend D3D12
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -Configuration Debug -TimeoutSeconds 20 -RenderBackend Vulkan -ExpectedBackend Vulkan
cmake --build build --config Release --target PiccoloEditor -- /verbosity:minimal
```

## 未完成或仍需确认

- Linux/macOS Vulkan 路径尚未在当前 Windows 工作环境中实测，仍需在对应平台确认 D3D12 源文件未参与编译且默认后端为 Vulkan。
- D3D12 首帧之后的长时间运行稳定性未完整确认。
- 频繁 resize、swapchain recreate、资源热更新、复杂 subresource barrier 组合仍需继续验证。
- D3D12 debug layer / PIX 级别的 descriptor heap、root signature、resource state、command list lifetime 清洁度仍需更完整证据。
- Shader parity 仍是风险区，需要继续关注 HLSL 与 GLSL/SPIR-V 输出一致性。
- 粒子 compute、image copy、readback 和 present 错误恢复路径已经接入，但仍需要更多场景覆盖。
- Acceptance Criteria 中的 `ctest` backend config/converter test 目标未作为当前用户要求推进；当前约束为“先不必加测试代码，以完整接入优先”。
- `rg "VulkanRHI|VulkanUtil|VmaAllocation|Vk" engine/source/runtime/function/render -g "!interface/vulkan/**"` 仍会命中 `render_system.cpp` 和 `passes/ui_pass.cpp` 中的 Vulkan 分支引用；这些看起来属于后端选择和 UI 分支所需引用，但仍应在最终验收时列入 approved Vulkan-specific references。

## 风险与注意事项

- 当前不应声称 D3D12 后端整体最终完成；更准确的状态是：D3D12 后端主路径已大规模接入，Windows 默认和三种启动 smoke 路径已有上一轮验证证据，后续仍需补齐跨平台和长时间运行验证。
- Shader bytecode 生成依赖 Windows 上可用的 `dxc.exe`；缺失时运行时会避免使用不可用的 D3D12 shader，并按配置回退。
- D3D12 render pass 对 Vulkan subpass 的 emulation 是高风险区域，后续修改要重点关注 attachment layout、resolve/copy 和 final state。
- Per-subresource state tracking 已接入，但复杂 mip/layer copy、upload、render-pass 切换仍需要更多实际资源组合验证。
- 目前遵循用户指示未新增测试代码；验证主要依赖构建、启动 smoke、日志断言和人工审查。

## 建议下一步

1. 继续做 acceptance gap audit，优先确认是否还有 Vulkan-only public path 或 D3D12 stub/dummy 行为影响完整渲染。
2. 对 Linux/macOS 做一次非 Windows 构建验证，确认 D3D12 源文件不会进入编译路径。
3. 在 Windows 上补一轮带 debug layer 的较长时间 editor 运行，覆盖 resize、场景加载、粒子、拾取、debug draw 和 UI 交互。
4. 将仍可接受的 Vulkan 分支引用整理为最终验收清单，避免 `rg` 扫描结果被误判为 public API 泄漏。
