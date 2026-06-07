# D3D12 Render Backend 进度记录

## 当前分支 / worktree

- 分支：`feat/d3d12-render-backend`
- Worktree：`D:\program\Piccolo\.worktrees\d3d12-render-backend`

## 总体目标

- 为 Piccolo 增加可实际运行的 D3D12 渲染后端。
- Windows 默认使用 D3D12，并保留 Vulkan 强制选择与失败回退能力。
- Linux/macOS 继续使用 Vulkan，避免非 Windows 平台编译 D3D12 代码。

## 已完成内容

- 已新增并接入 `RenderBackend` / `RenderBackendAllowFallback` 配置。
- 启动流程支持 `Auto` / `Vulkan` / `D3D12` 后端选择，Windows `Auto` 默认请求 D3D12。
- D3D12 初始化失败时可按配置自动回退 Vulkan。
- 公共 RHI 与渲染资源接口已从 Vulkan/VMA 类型中解耦。
- D3D12 主渲染路径已覆盖 shader bytecode、descriptor/root signature、render pass/framebuffer、graphics/compute pipeline、命令录制、粒子、拾取、debug draw 与 DX12 ImGui 分支。
- D3D12 queue/fence/semaphore、render pass subpass、per-subresource image barrier、sampler/swapchain 生命周期、默认 command pool、frame context 同步、teardown ordering 与 resize callback 已继续补齐。
- Windows CI 已构建 Debug/Release PiccoloEditor，Debug 构建运行 D3D12 启动烟测。

## 已验证内容

- Debug 构建可运行 PiccoloEditor D3D12 启动烟测。
- 烟测覆盖日志中的 `Initialized RHI backend: D3D12` 与 `engine start`。
- Windows CI 中已加入 D3D12 backend smoke 步骤。
- 设备创建在无硬件 adapter 时可回退 WARP，便于 Windows CI 启动验证。

## 最近本地提交列表

```text
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
b4607ec test: validate d3d12 backend boot
```

## 未完成 / 风险项

- 当前验证仍以 Debug 构建、启动烟测和 Windows CI 为主，尚未补充更完整的 CTest/单元测试覆盖。
- D3D12 shader parity、复杂 subpass/resource state 组合、粒子 compute 与图形调试层长期稳定性仍需持续验证。
- 仍需确认 Windows Vulkan 强制路径、Linux/macOS Vulkan 路径在最终合入前无回归。
- D3D12 首帧之后的长时间运行、窗口频繁 resize、资源热更新和设备清理边界仍有风险。

## 下一阶段建议

- 扩展验证矩阵：Windows D3D12、Windows Vulkan、Linux/macOS Vulkan。
- 增加关键 RHI converter、backend config、descriptor/resource barrier 的轻量测试覆盖。
- 使用 D3D12 debug layer / PIX 捕获检查 descriptor heap、root signature、resource state 与 command list 生命周期。
- 对 editor 常用场景进行长时间运行和频繁 resize 验证。
