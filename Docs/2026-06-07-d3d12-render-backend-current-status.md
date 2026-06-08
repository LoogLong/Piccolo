# D3D12 Render Backend Current Status

Date: 2026-06-08

## Repository Status

- Root repository: `D:\program\Piccolo`
- Active worktree: `D:\program\Piccolo\.worktrees\d3d12-windows-replacement`
- Branch: `feat/d3d12-windows-replacement`
- Task 10 validation started from: `1a40936 docs: record d3d12 replacement validation`
- Main plan: `Docs\2026-06-06-d3d12-render-backend-implementation-plan.md`

## Final Backend State

- Windows primary mode is D3D12.
- Windows PiccoloEditor deployment config selects `RenderBackend=D3D12` and `RenderBackendAllowFallback=false` when D3D12 is enabled, so D3D12-capable deployment does not silently fall back to Vulkan.
- Windows Vulkan-only builds package an explicit `RenderBackend=Vulkan` deployment config because `Auto` resolves to D3D12 on Windows.
- Windows D3D12-only builds are supported with `PICCOLO_ENABLE_VULKAN_BACKEND=OFF` and `PICCOLO_ENABLE_D3D12_BACKEND=ON`; this removes Vulkan runtime/imgui link dependencies from that build graph.
- Optional Windows Vulkan remains available for debug/fallback validation when `PICCOLO_ENABLE_VULKAN_BACKEND=ON`.
- Linux/macOS continue to use Vulkan; D3D12 remains Windows-only and is forced out of non-Windows builds.
- D3D12 builds require `dxc.exe` for DXIL shader bytecode.
- Vulkan builds require Vulkan SDK/glslang for SPIR-V shader bytecode.

## Confirmed Capabilities

- Backend selection supports `Auto`, `Vulkan`, and `D3D12`.
- Windows `Auto` selects D3D12; non-Windows `Auto` selects Vulkan.
- Explicit fallback is controlled by `RenderBackendAllowFallback`; fallback is not used in Windows deployment configs.
- D3D12 RHI wrapper naming has been cleaned up; final source scan found no D3D12 `dummy` wrapper names.
- Public/shared render-layer Vulkan references are limited to backend selection and optional Vulkan UI branches guarded by `PICCOLO_ENABLE_VULKAN_BACKEND`; backend-local Vulkan implementation remains under `interface/vulkan`.
- D3D12-only build scan found no `vulkan-1`, `imgui_impl_vulkan`, or `vk_mem_alloc` references in `build_d3d12_only`.
- Exact final scan commands and the Vulkan glob false-positive/refined glob explanation are recorded in `Docs/2026-06-07-d3d12-render-backend-progress-audit.md` under `Final Source Scan Results`.

## Latest Validation Evidence

Task 10 final validation ran these commands successfully:

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

`build_dual_backend` did not exist before this validation run, so it was configured with both backend options enabled before building.

Additional Task 9 validation evidence remains:

- D3D12-only configure/build passed.
- Debug and Release D3D12 smoke passed with fallback forbidden.
- Automated lifecycle liveness covered boot, world load, resize, minimize/restore, and 60 seconds of post-restore liveness.
- D3D12 error/fallback log scan was clean.

## Remaining Risks

These are normal engine validation risks, not evidence of incomplete backend wiring:

- Full manual/visual D3D12 runtime validation is not complete.
- Visual render confirmation is not proven by automation.
- Automated screenshots were black.
- Known caveat: one Debug capture showed a Visual C++ Debug Assertion: `debug_heap.cpp:908`, `is_block_type_valid(header->_block_use)`.
- Normal shutdown was not validated.
- Interactive camera, picking, UI, debug draw, particle, post-processing, and broader visual validation remain to be completed.
- Linux/macOS Vulkan support is preserved architecturally, but final platform builds still need to run on those platforms.

## Operating Constraints

- Do not add new test source code unless explicitly requested.
- Preserve unrelated worktree changes.
- Keep D3D12 as Windows primary while keeping optional Windows Vulkan and non-Windows Vulkan supported.
