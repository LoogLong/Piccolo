# D3D12 Render Backend Progress Audit

日期：2026-06-08

## 来源

- 本地 Task 10 source scans
- 本地 Task 10 build/smoke validation
- `README.md`
- `ReleaseNotes.md`
- `Docs/2026-06-07-d3d12-render-backend-current-status.md`
- `scripts/tests/render_backend/smoke_backend_boot.ps1`

说明：本文件是 D3D12 replacement 分支的最终文档盘点。它记录已经验证的构建、启动和生命周期证据，也明确保留尚未完成的手动/视觉验证项。

## 当前仓库状态

- Worktree：`D:\program\Piccolo\.worktrees\d3d12-windows-replacement`
- 分支：`feat/d3d12-windows-replacement`
- Task 10 起点：`1a40936 docs: record d3d12 replacement validation`

## 已完成内容

### 后端选择与平台默认

- `RenderBackend` 支持 `Auto`、`Vulkan`、`D3D12`。
- Windows primary mode is D3D12。
- Windows D3D12-capable deployment config 使用 `RenderBackend=D3D12` 且 `RenderBackendAllowFallback=false`，不会静默回退 Vulkan。
- Windows Vulkan-only deployment config 使用 `RenderBackend=Vulkan` 且 `RenderBackendAllowFallback=false`，避免 `Auto` 在 Windows 上解析到 D3D12。
- `RenderBackendAllowFallback=true` 仍可用于开发/诊断场景，但只有构建包含 Vulkan 后端时才可回退。
- Linux/macOS deployment 使用 `RenderBackend=Auto` 并解析到 Vulkan。

### 构建与平台隔离

- Windows D3D12-only build option exists: `-DPICCOLO_ENABLE_VULKAN_BACKEND=OFF -DPICCOLO_ENABLE_D3D12_BACKEND=ON`。
- Windows dual-backend build remains available: `-DPICCOLO_ENABLE_VULKAN_BACKEND=ON -DPICCOLO_ENABLE_D3D12_BACKEND=ON`。
- D3D12-only Windows build no longer links PiccoloRuntime/imgui against Vulkan runtime or Vulkan ImGui backend.
- Optional Windows Vulkan build remains validated through dual-backend Debug Vulkan smoke.
- Non-Windows Vulkan remains supported; D3D12 code remains Windows-only.
- D3D12 builds require `dxc.exe`; Vulkan builds require Vulkan SDK/glslang.

### D3D12 backend wiring

- Public RHI/render-resource interfaces are backend-neutral; VMA remains Vulkan-backend-local.
- D3D12 RHI owns D3D12-specific wrappers for buffers, images, views, samplers, shaders, command buffers/pools, queues, fences, descriptor resources, render passes, framebuffers, and pipelines.
- D3D12 render path includes shader bytecode, descriptor/root signature handling, render pass/framebuffer emulation, graphics/compute pipeline creation, command recording/submission, particles, picking, debug draw, post-processing, and DX12 ImGui initialization.
- D3D12 lifecycle work covers teardown ordering, swapchain resize/recreate, per-frame context, sampler/swapchain lifetimes, resource barriers, image copy regions, present/error logging, and UI backend shutdown before RHI cleanup.

## Final Source Scan Results

```powershell
rg -n "m_dummy_|dummy" engine/source/runtime/function/render/interface/d3d12
```

Result: no matches.

```powershell
rg -n "VulkanRHI|VulkanUtil|VmaAllocation|Vk[A-Z_]|ImGui_ImplVulkan" engine/source/runtime/function/render -g "!interface/vulkan/**"
```

Result: the exact task glob produced backend-local `interface/vulkan` matches because the search root is above `interface`. A refined shared-layer scan with `-g "!**/interface/vulkan/**"` left only `render_system.cpp` backend selection and `passes/ui_pass.cpp` optional Vulkan UI branches, all guarded by `PICCOLO_ENABLE_VULKAN_BACKEND`.

```powershell
rg -n "vulkan-1|imgui_impl_vulkan|vk_mem_alloc" build_d3d12_only
```

Result: no matches.

## Final Validation Results

All final validation commands exited with code 0:

```powershell
git diff --check
cmake -S . -B build_d3d12_only -DPICCOLO_ENABLE_VULKAN_BACKEND=OFF -DPICCOLO_ENABLE_D3D12_BACKEND=ON
cmake --build build_d3d12_only --config Debug --target PiccoloEditor -- /verbosity:minimal
cmake --build build_d3d12_only --config Release --target PiccoloEditor -- /verbosity:minimal
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build_d3d12_only -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build_d3d12_only -Configuration Release -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
cmake -S . -B build_dual_backend -DPICCOLO_ENABLE_VULKAN_BACKEND=ON -DPICCOLO_ENABLE_D3D12_BACKEND=ON
cmake --build build_dual_backend --config Debug --target PiccoloEditor -- /verbosity:minimal
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build_dual_backend -Configuration Debug -TimeoutSeconds 20 -RenderBackend Vulkan -ExpectedBackend Vulkan
```

Notes:

- `build_dual_backend` was missing and was configured with both backend options enabled before the build.
- D3D12-only Debug/Release smoke found `Initialized RHI backend: D3D12` and `engine start`.
- Dual-backend Debug Vulkan smoke found `Initialized RHI backend: Vulkan` and `engine start`.
- Build output included existing MSVC `stdext::checked_array_iterator` deprecation warnings; these did not fail the build.

## Remaining Risks And Caveats

Remaining risks are normal engine validation work, not incomplete backend wiring:

- Do not claim full manual/visual D3D12 runtime validation is complete.
- D3D12-only configure/build passed.
- Debug/Release D3D12 smoke passed with fallback forbidden.
- Automated lifecycle liveness covered boot, world-load, resize, minimize-restore, and 60 seconds of liveness.
- D3D12 error/fallback log scan was clean.
- Visual render confirmation is not proven by automation.
- Automated screenshots were black.
- Known caveat: one Debug capture showed Visual C++ Debug Assertion: `debug_heap.cpp:908`, `is_block_type_valid(header->_block_use)`.
- Normal shutdown was not validated.
- Interactive camera, picking, UI, debug draw, particle, post-processing, and broader visual validation remain to be completed.
- Linux/macOS Vulkan paths remain supported but still need final native-platform builds outside this Windows validation environment.

## Suggested Follow-Up

1. Run native Linux/macOS editor builds to validate preserved Vulkan paths on those platforms.
2. Complete manual D3D12 visual validation for camera movement, picking IDs, UI panels, debug draw/axis rendering, particles, post-processing, level reload, and normal shutdown.
3. Investigate the Debug assertion observed during one screenshot capture before treating interactive Debug runs as clean.
