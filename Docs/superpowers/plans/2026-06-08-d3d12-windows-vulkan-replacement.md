# D3D12 Windows Vulkan Replacement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Windows editor/runtime use the D3D12 rendering backend as the primary backend without silently depending on Vulkan, while keeping Vulkan available for non-Windows and explicit Windows fallback/debug use.

**Architecture:** Keep the current RHI-centered render pipeline and finish the D3D12 backend behind that contract instead of rewriting render passes. The remaining work is to remove Windows default-path Vulkan dependencies, tighten D3D12 RHI semantics where compatibility wrappers still hide Vulkan-shaped assumptions, and build a validation matrix that proves main camera, post-process, UI, picking, debug draw, particles, resize, and resource update paths run on D3D12 without fallback.

**Tech Stack:** C++17, CMake, GLFW, Windows SDK D3D12/DXGI/DXC, Dear ImGui DX12/Vulkan backends, existing Vulkan backend, PowerShell smoke scripts.

---

## Current Code Findings

- `engine/source/runtime/function/render/render_system.cpp` already selects `VulkanRHI` or `_WIN32` `D3D12RHI` through `createRHIBackend()`, reads `RenderBackend`, maps `Auto` through `getPlatformDefaultRenderBackend()`, and can fall back to Vulkan when `RenderBackendAllowFallback=true`.
- `engine/source/runtime/function/render/render_backend.cpp` already parses `Auto`, `Vulkan`, `D3D12`, and `dx12`; Windows `Auto` already returns `D3D12`.
- `engine/configs/development/PiccoloEditor.ini` and `engine/configs/deployment/PiccoloEditor.ini` already set `RenderBackend=D3D12` and `RenderBackendAllowFallback=true`. For final Windows replacement, fallback should be explicit for developer/debug builds rather than the default shipped behavior.
- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp` implements most D3D12 basics: device, queue, swapchain, RTV/DSV, fences, command buffers, descriptor heaps, root signatures, shader modules, graphics/compute PSO, render-pass emulation, copy, readback, barriers, ImGui handles, and present.
- `D3D12RHI` still exposes `m_dummy_command_buffers`, `m_dummy_command_pool`, `m_dummy_descriptor_pool`, `m_dummy_graphics_queue`, `m_dummy_compute_queue`, `m_dummy_fences`, and `m_dummy_texture_copy_semaphore` in `d3d12_rhi.h`. Some are functional RHI wrapper objects, but their naming and lifetime make it hard to distinguish real D3D12 ownership from compatibility shims.
- `D3D12RHIBuffer` still stores `host_data` and `map_host_data`. This is useful for upload/readback mirroring, but current copy/map paths must be audited so D3D12 is not relying on CPU-side mirrors where real GPU synchronization is required.
- `engine/source/runtime/function/render/passes/ui_pass.cpp` directly includes `VulkanRHI`, calls `ImGui_ImplVulkan_*`, and casts Vulkan objects in the same file as the D3D12 UI branch. D3D12 UI exists, but UI remains a shared-layer Vulkan dependency point.
- `engine/source/runtime/CMakeLists.txt` always links `${vulkan_lib}` and always publishes `${vulkan_include}` and `vulkanmemoryallocator/include` to `PiccoloRuntime`, even when Windows defaults to D3D12.
- `engine/3rdparty/imgui.cmake` always compiles and links ImGui's Vulkan backend and Vulkan library; on Windows it additionally compiles DX12 backend. Windows D3D12-only packaging therefore still has a Vulkan link dependency.
- `engine/shader/CMakeLists.txt` always compiles GLSL/SPIR-V, while Windows additionally compiles HLSL/DXIL. This preserves Vulkan support, but a D3D12-primary Windows build still depends on Vulkan SDK tooling unless the shader build is made backend-aware.
- `engine/source/runtime/function/render/render_pipeline.cpp` drives frames through `waitForFences()`, `resetCommandPool()`, `prepareBeforePass()`, pass draws, `submitRendering()`, then particle depth/normal copy and simulation. D3D12 replacement must preserve this frame contract.
- Existing documentation records prior D3D12/Auto/Vulkan smoke passes, but current acceptance gaps remain: long-running D3D12 editor sessions, frequent resize/minimize, device-removed reporting, shader parity, particle compute/readback, non-Windows guard validation, and Windows no-Vulkan-dependency validation.

## File Structure

Modify these existing files:

- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h`: rename D3D12 compatibility wrapper members, document ownership, and expose only real D3D12-backed RHI objects to the rest of the engine.
- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`: tighten command/fence/semaphore naming, buffer copy/map/readback behavior, render-pass state transitions, resize handling, debug-layer logging, and device teardown.
- `engine/source/runtime/function/render/passes/ui_pass.h`: keep only backend-neutral UI pass state in the public header.
- `engine/source/runtime/function/render/passes/ui_pass.cpp`: split Vulkan and D3D12 ImGui code into compile-guarded helper blocks so Windows D3D12-only builds do not need Vulkan UI code.
- `engine/source/runtime/CMakeLists.txt`: add backend build options and stop linking Vulkan publicly in Windows D3D12-only builds.
- `engine/3rdparty/imgui.cmake`: compile/link Vulkan and DX12 ImGui backends only when their backend options are enabled.
- `engine/CMakeLists.txt`: introduce backend build options, make Vulkan SDK/glslang required only when Vulkan is enabled, and keep `dxc.exe` required when D3D12 is enabled on Windows.
- `engine/shader/CMakeLists.txt`: compile GLSL/SPIR-V only when Vulkan is enabled and HLSL/DXIL only when D3D12 is enabled.
- `engine/source/runtime/function/render/render_shader_bytecode.h`: keep shader selection backend-aware and fail loudly when a selected backend has no generated bytecode.
- `engine/source/runtime/function/render/render_system.cpp`: make fallback policy explicit in logs and prevent silent fallback in D3D12-primary validation mode.
- `engine/configs/development/PiccoloEditor.ini`: keep developer fallback if desired, but make the value intentional and documented.
- `engine/configs/deployment/PiccoloEditor.ini`: set Windows replacement behavior to D3D12 without silent fallback.
- `scripts/tests/render_backend/smoke_backend_boot.ps1`: add modes for D3D12-only and fallback-forbidden validation without adding new test source code.
- `.github/workflows/build_windows.yml`: add Windows D3D12-primary build/smoke and optional Vulkan build/smoke jobs.
- `README.md`: document Windows D3D12-primary workflow, optional Vulkan support, DXC requirement, and smoke commands.
- `ReleaseNotes.md`: record the final replacement behavior and validation matrix.

Do not create new unit-test or CTest source files for this plan. Verification uses builds, smoke scripts, log scans, and manual editor scenarios because project instruction says not to add test code unless explicitly requested.

## Task 1: Baseline Audit And Acceptance Lock

**Files:**
- Modify: `Docs/2026-06-07-d3d12-render-backend-current-progress.md`
- Modify: `ReleaseNotes.md`

- [ ] **Step 1: Capture current repository state**

Run:

```powershell
git status --short
git log --oneline -12
rg -n "dummy|host_data|map_host_data|Falling back to Vulkan backend|ImGui_ImplVulkan|VulkanRHI|VmaAllocation|Vk" engine/source/runtime/function/render -g "!interface/vulkan/**"
```

Expected:

- `git status --short` is clean before implementation begins.
- Recent commits include the existing D3D12 progress commits.
- The scan shows D3D12 wrapper names, D3D12 host mirrors, the explicit `render_system.cpp` fallback path, and `ui_pass.cpp` Vulkan UI branch.

- [ ] **Step 2: Update the progress document with exact acceptance criteria**

Append a section to `Docs/2026-06-07-d3d12-render-backend-current-progress.md`:

```markdown
## Windows D3D12 Replacement Acceptance Criteria

- Windows `RenderBackend=D3D12` initializes D3D12 and does not log `Falling back to Vulkan backend`.
- Windows D3D12-primary validation can build and boot without linking PiccoloRuntime or imgui against Vulkan when Vulkan support is disabled by CMake option.
- Windows optional Vulkan validation can still build and boot when Vulkan support is enabled and `RenderBackend=Vulkan`.
- Non-Windows builds continue to enable Vulkan and exclude D3D12 `.cpp` files.
- The editor renders main camera, post-process, debug draw, particles, picking, ImGui, and swapchain resize on D3D12.
- D3D12 logs are clean of debug-layer, descriptor heap, root signature, resource state, command list lifetime, device removed, and fallback errors during boot plus the manual validation run.
- No new unit-test or CTest source files are required for this phase.
```

- [ ] **Step 3: Update release notes with the active goal**

Add this note near the existing D3D12 section in `ReleaseNotes.md`:

```markdown
- 当前后续目标是把 Windows D3D12 从“默认可回退 Vulkan”推进到“D3D12-primary 且可选 Vulkan”，并补齐 D3D12 长跑、resize、粒子、拾取、UI 与无 Vulkan 链接依赖验证。
```

- [ ] **Step 4: Run formatting check**

Run:

```powershell
git diff --check
```

Expected: no whitespace errors.

- [ ] **Step 5: Commit**

Run:

```powershell
git add Docs/2026-06-07-d3d12-render-backend-current-progress.md ReleaseNotes.md
git commit -m "docs: lock d3d12 replacement acceptance criteria"
```

## Task 2: Rename D3D12 Compatibility Wrappers Into Real Frame Resources

**Files:**
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`

- [ ] **Step 1: Rename misleading `m_dummy_*` members**

In `d3d12_rhi.h`, rename these members:

```cpp
std::array<RHICommandBuffer*, 3> m_frame_command_buffers {{nullptr, nullptr, nullptr}};
RHICommandBuffer*                m_current_command_buffer {nullptr};
RHICommandPool*                  m_default_command_pool {nullptr};
RHIDescriptorPool*               m_default_descriptor_pool {nullptr};
RHIQueue*                        m_graphics_queue {nullptr};
RHIQueue*                        m_compute_queue {nullptr};
std::array<RHIFence*, 3>         m_frame_fences {{nullptr, nullptr, nullptr}};
RHISemaphore*                    m_texture_copy_semaphore {nullptr};
```

Replace all old names in `d3d12_rhi.cpp`:

```text
m_dummy_command_buffers -> m_frame_command_buffers
m_dummy_command_pool -> m_default_command_pool
m_dummy_descriptor_pool -> m_default_descriptor_pool
m_dummy_graphics_queue -> m_graphics_queue
m_dummy_compute_queue -> m_compute_queue
m_dummy_fences -> m_frame_fences
m_dummy_texture_copy_semaphore -> m_texture_copy_semaphore
```

- [ ] **Step 2: Add ownership comments for the renamed members**

Add this short comment above the frame/default members in `d3d12_rhi.h`:

```cpp
// RHI-facing wrappers owned by D3D12RHI. They back the frame loop and keep the existing RHI contract
// without exposing ID3D12* objects outside the backend.
```

- [ ] **Step 3: Verify no misleading dummy names remain in D3D12**

Run:

```powershell
rg -n "m_dummy_|dummy" engine/source/runtime/function/render/interface/d3d12
```

Expected: no matches.

- [ ] **Step 4: Build**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /verbosity:minimal
```

Expected: build succeeds with only existing project warnings.

- [ ] **Step 5: Commit**

Run:

```powershell
git add engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp
git commit -m "refactor: name d3d12 frame rhi wrappers"
```

## Task 3: Tighten D3D12 Buffer Copy And Map Semantics

**Files:**
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
- Modify: `ReleaseNotes.md`

- [ ] **Step 1: Audit all D3D12 host mirror call sites**

Run:

```powershell
rg -n "host_data|map_host_data|copyBuffer\\(|cmdCopyBuffer\\(|invalidateMappedMemoryRanges|flushMappedMemoryRanges|mapMemory\\(" engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp engine/source/runtime/function/render/passes engine/source/runtime/function/render/render_resource.cpp engine/source/runtime/function/render/debugdraw
```

Expected: matches in `D3D12RHIBuffer`, `copyBuffer`, `cmdCopyBuffer`, `mapMemory`, `flushMappedMemoryRanges`, `invalidateMappedMemoryRanges`, particle pass, pick pass, render resource uploads, and debug draw uploads.

- [ ] **Step 2: Make CPU mirrors explicit cache state**

In `D3D12RHIBuffer`, keep `host_data` but add fields that distinguish valid CPU mirror data from mappable resource memory:

```cpp
std::vector<uint8_t>   host_data;
bool                   host_data_valid {false};
bool                   map_host_data {false};
```

Update D3D12 buffer creation:

- Upload heap buffers set `host_data_valid=true` after initialization or flush.
- Readback heap buffers set `host_data_valid=true` after `invalidateMappedMemoryRanges()`.
- Default heap buffers set `host_data_valid=false` after GPU writes or copies unless data is mirrored from a known valid source.

- [ ] **Step 3: Update `copyBuffer()`**

In `D3D12RHI::copyBuffer()`:

- Keep `CopyBufferRegion` as the authoritative path on Windows.
- Set `dst->host_data_valid=false` before issuing GPU copy into a default heap.
- Only copy CPU mirrors when both source and destination mirrors are valid and the copy range is inside both vectors.
- Log invalid ranges instead of returning silently when the buffers are D3D12 resources.

Use this mirror update rule:

```cpp
if (src->host_data_valid && dst->host_data_valid && !src->host_data.empty() && !dst->host_data.empty())
{
    // Copy the mirrored bytes for upload/readback-visible buffers.
}
else
{
    dst->host_data_valid = false;
}
```

- [ ] **Step 4: Update `cmdCopyBuffer()`**

In `D3D12RHI::cmdCopyBuffer()`:

- Keep the current default-region behavior when `pRegions == nullptr || regionCount == 0`.
- Transition default-heap source to `COPY_SOURCE` and destination to `COPY_DEST`.
- Set destination mirror validity using the same rule as `copyBuffer()`.
- Keep `"D3D12 cmdCopyBuffer skipped invalid copy region"` for invalid regions.

- [ ] **Step 5: Update map/flush/invalidate**

In `D3D12RHI::mapMemory()`, `unmapMemory()`, `flushMappedMemoryRanges()`, and `invalidateMappedMemoryRanges()`:

- Upload heap mapped writes mark `host_data_valid=true` after flush/unmap.
- Readback heap invalidation maps the D3D12 readback resource into `host_data`, then marks `host_data_valid=true`.
- Default heap host-data mapping returns false unless it is a known mirrored readback helper path.

- [ ] **Step 6: Verify D3D12 upload/readback users still build**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /verbosity:minimal
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12
```

Expected:

- Build succeeds.
- Smoke log contains `Initialized RHI backend: D3D12`.
- Smoke log does not contain `Falling back to Vulkan backend`.

- [ ] **Step 7: Commit**

Run:

```powershell
git add engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp ReleaseNotes.md
git commit -m "feat: tighten d3d12 buffer mirror semantics"
```

## Task 4: Harden D3D12 Render Pass, Descriptor, And Particle Paths

**Files:**
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
- Modify: `engine/source/runtime/function/render/passes/particle_pass.cpp`
- Modify: `ReleaseNotes.md`

- [ ] **Step 1: Add D3D12 diagnostic logging around high-risk no-op returns**

In `d3d12_rhi.cpp`, add `LOG_ERROR` or `LOG_WARN` before early returns in these functions when the function receives non-null user inputs but cannot record D3D12 work:

```cpp
cmdBeginRenderPassPFN
cmdNextSubpassPFN
cmdEndRenderPassPFN
cmdBindDescriptorSetsPFN
cmdDrawIndexedPFN
cmdCopyImageToBuffer
cmdCopyImageToImage
cmdDispatchIndirect
cmdPipelineBarrier
submitRendering
```

Do not log for intentionally empty calls such as zero region count after the default-region check.

- [ ] **Step 2: Verify render pass attachment state coverage**

Review these helpers in `d3d12_rhi.cpp` and make the transitions explicit for input attachments, color attachments, depth read/write, resolve/copy targets, and final presentation state:

```cpp
subpassAttachmentState
finishD3D12Subpass
transitionImageView
transitionImageSubresource
bindFramebufferForSubpass
```

Required behavior:

- Input attachments transition to shader-readable state before descriptor use.
- Color attachments transition to render target before clear/draw.
- Read-only depth input transitions to `DEPTH_READ | PIXEL_SHADER_RESOURCE | NON_PIXEL_SHADER_RESOURCE`.
- Writable depth transitions to `DEPTH_WRITE`.
- Resolve attachments transition to resolve/copy destination and then to their next subpass/final state.
- Present back buffers transition from render target to present before `Present()`.

- [ ] **Step 3: Confirm descriptor heap binding survives UI rendering**

In `UIPass::draw()` and `D3D12RHI::cmdBindDescriptorSetsPFN()`:

- Ensure D3D12 ImGui descriptor heap binding does not permanently hide the main CBV/SRV/UAV and sampler heaps needed by subsequent commands.
- If a pass records commands after UI, bind the D3D12 RHI descriptor heaps again before drawing/dispatching.
- Keep the D3D12 ImGui heap scoped to the ImGui draw call.

- [ ] **Step 4: Validate particle copy and compute ordering**

Review these calls in `engine/source/runtime/function/render/render_pipeline.cpp` and `particle_pass.cpp`:

```cpp
ParticlePass::copyNormalAndDepthImage()
ParticlePass::simulate()
ParticlePass::updateEmitter()
ParticlePass::cmdDispatchIndirect usage
```

Required behavior:

- Depth/normal image copy happens after the main camera render pass has ended and before particle simulation reads those images.
- Counter/readback buffers are invalidated before CPU reads.
- Upload buffers are flushed before GPU copy/dispatch consumes them.
- Indirect dispatch buffers transition to indirect-argument state before `ExecuteIndirect`.

- [ ] **Step 5: Run D3D12 debug log scan**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /verbosity:minimal
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12
rg -n "D3D12 ERROR|DEVICE_REMOVED|DXGI_ERROR|resource state|descriptor heap|root signature|command list|Falling back to Vulkan|RHI backend initialization failed|\\[error\\]" build\test_d3d12_boot.log build\test_d3d12_boot.stderr.log build\test_d3d12_boot.stdout.log
```

Expected: `rg` exits with code `1` because no error patterns are found.

- [ ] **Step 6: Commit**

Run:

```powershell
git add engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp engine/source/runtime/function/render/passes/particle_pass.cpp ReleaseNotes.md
git commit -m "feat: harden d3d12 render pass and particle paths"
```

## Task 5: Make Windows D3D12 Fallback Policy Explicit

**Files:**
- Modify: `engine/source/runtime/resource/config_manager/config_manager.h`
- Modify: `engine/source/runtime/resource/config_manager/config_manager.cpp`
- Modify: `engine/source/runtime/function/render/render_system.cpp`
- Modify: `engine/configs/development/PiccoloEditor.ini`
- Modify: `engine/configs/deployment/PiccoloEditor.ini`
- Modify: `scripts/tests/render_backend/smoke_backend_boot.ps1`
- Modify: `README.md`
- Modify: `ReleaseNotes.md`

- [ ] **Step 1: Add a fallback-forbidden smoke mode**

Extend `scripts/tests/render_backend/smoke_backend_boot.ps1` with a switch:

```powershell
[switch]$DisallowFallback
```

After the existing fallback scan, enforce:

```powershell
if ($DisallowFallback -and $fallbackFound) {
    throw "D3D12-primary validation forbids fallback to Vulkan"
}
```

Keep the existing default behavior for Vulkan smoke and Auto smoke.

- [ ] **Step 2: Set deployment fallback policy**

In `engine/configs/deployment/PiccoloEditor.ini`, set:

```ini
RenderBackend=D3D12
RenderBackendAllowFallback=false
```

In `engine/configs/development/PiccoloEditor.ini`, keep one of these explicit choices:

```ini
RenderBackend=D3D12
RenderBackendAllowFallback=true
```

Development can keep fallback for diagnosis, while deployment proves D3D12-primary behavior.

- [ ] **Step 3: Improve fallback logging**

In `RenderSystem::initialize()`:

- Keep the current fallback path when `allow_fallback_to_vulkan` is true.
- Add an info log before primary backend initialization:

```cpp
LOG_INFO(std::string("Requested RHI backend: ") + renderBackendToString(requested_backend));
```

- Keep `Falling back to Vulkan backend from ...` exactly as the smoke script forbidden pattern.
- When fallback is disabled and D3D12 fails, throw `Failed to initialize RHI backend: D3D12` without attempting Vulkan.

- [ ] **Step 4: Update docs**

In `README.md`, document:

```markdown
For Windows D3D12-primary validation, set `RenderBackend=D3D12` and `RenderBackendAllowFallback=false`.
Use `RenderBackendAllowFallback=true` only when you intentionally want a Vulkan debug fallback.
```

In `ReleaseNotes.md`, add:

```markdown
- Windows deployment config now treats D3D12 as the primary backend and forbids silent Vulkan fallback; developer configs may opt into fallback for diagnosis.
```

- [ ] **Step 5: Run fallback policy smoke**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /verbosity:minimal
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -Configuration Debug -TimeoutSeconds 20 -RenderBackend Auto -ExpectedBackend D3D12 -DisallowFallback
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -Configuration Debug -TimeoutSeconds 20 -RenderBackend Vulkan -ExpectedBackend Vulkan
```

Expected: all three smoke runs pass, and the D3D12/Auto runs do not fallback.

- [ ] **Step 6: Commit**

Run:

```powershell
git add engine/source/runtime/resource/config_manager engine/source/runtime/function/render/render_system.cpp engine/configs scripts/tests/render_backend/smoke_backend_boot.ps1 README.md ReleaseNotes.md
git commit -m "feat: make d3d12 fallback policy explicit"
```

## Task 6: Split UI Backend Dependencies

**Files:**
- Modify: `engine/source/runtime/function/render/passes/ui_pass.cpp`
- Modify: `engine/source/runtime/function/render/passes/ui_pass.h`
- Modify: `engine/3rdparty/imgui.cmake`
- Modify: `engine/source/runtime/CMakeLists.txt`

- [ ] **Step 1: Guard Vulkan UI code behind a compile definition**

Introduce compile definitions from CMake:

```cmake
PICCOLO_ENABLE_VULKAN_BACKEND
PICCOLO_ENABLE_D3D12_BACKEND
```

In `ui_pass.cpp`, wrap Vulkan includes and Vulkan-only code:

```cpp
#if PICCOLO_ENABLE_VULKAN_BACKEND
#include "runtime/function/render/interface/vulkan/vulkan_rhi.h"
#include <backends/imgui_impl_vulkan.h>
#endif
```

Wrap D3D12 includes:

```cpp
#if PICCOLO_ENABLE_D3D12_BACKEND && defined(_WIN32)
#include "runtime/function/render/interface/d3d12/d3d12_rhi.h"
#include <backends/imgui_impl_dx12.h>
#endif
```

- [ ] **Step 2: Keep unsupported branches explicit**

For a disabled backend branch, log and skip:

```cpp
LOG_WARN("Vulkan UI backend is not compiled in this build");
```

or:

```cpp
LOG_WARN("D3D12 UI backend is not compiled in this build");
```

- [ ] **Step 3: Scope ImGui font upload to Vulkan builds**

Wrap `UIPass::uploadFonts()` Vulkan command-buffer implementation in `#if PICCOLO_ENABLE_VULKAN_BACKEND`.

For non-Vulkan builds, keep:

```cpp
void UIPass::uploadFonts()
{
}
```

because the DX12 backend owns font upload through `ImGui_ImplDX12_Init()`.

- [ ] **Step 4: Make ImGui CMake backend-aware**

In `engine/3rdparty/imgui.cmake`:

- Always compile core ImGui and `imgui_impl_glfw`.
- Compile `imgui_impl_vulkan` and link `${vulkan_lib}` only when `PICCOLO_ENABLE_VULKAN_BACKEND` is true.
- Compile `imgui_impl_dx12` and link `d3d12 dxgi dxguid` only when `WIN32` and `PICCOLO_ENABLE_D3D12_BACKEND` are true.

Use CMake options created in Task 7.

- [ ] **Step 5: Verify UI dependency scan**

Run:

```powershell
rg -n "ImGui_ImplVulkan|VulkanRHI|VulkanQueue|VulkanRenderPass|VulkanCommandBuffer" engine/source/runtime/function/render/passes/ui_pass.cpp
```

Expected: matches only inside `#if PICCOLO_ENABLE_VULKAN_BACKEND` guarded blocks.

- [ ] **Step 6: Build D3D12 default**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /verbosity:minimal
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
```

Expected: build and smoke pass.

- [ ] **Step 7: Commit**

Run:

```powershell
git add engine/source/runtime/function/render/passes/ui_pass.cpp engine/source/runtime/function/render/passes/ui_pass.h engine/3rdparty/imgui.cmake engine/source/runtime/CMakeLists.txt
git commit -m "refactor: split imgui render backend dependencies"
```

## Task 7: Add Backend Build Options And Remove Windows Vulkan Link Requirement

**Files:**
- Modify: `engine/CMakeLists.txt`
- Modify: `engine/source/runtime/CMakeLists.txt`
- Modify: `engine/shader/CMakeLists.txt`
- Modify: `cmake/ShaderCompile.cmake`
- Modify: `engine/source/runtime/function/render/render_system.cpp`
- Modify: `README.md`
- Modify: `ReleaseNotes.md`

- [ ] **Step 1: Add CMake backend options**

In `engine/CMakeLists.txt`, add:

```cmake
option(PICCOLO_ENABLE_VULKAN_BACKEND "Build the Vulkan render backend" ON)
option(PICCOLO_ENABLE_D3D12_BACKEND "Build the D3D12 render backend on Windows" ON)

if(NOT WIN32)
  set(PICCOLO_ENABLE_D3D12_BACKEND OFF CACHE BOOL "" FORCE)
endif()

if(WIN32 AND NOT PICCOLO_ENABLE_VULKAN_BACKEND AND NOT PICCOLO_ENABLE_D3D12_BACKEND)
  message(FATAL_ERROR "At least one render backend must be enabled")
endif()

if(NOT WIN32 AND NOT PICCOLO_ENABLE_VULKAN_BACKEND)
  message(FATAL_ERROR "Non-Windows builds require the Vulkan backend")
endif()
```

Add compile definitions:

```cmake
add_compile_definitions(
  PICCOLO_ENABLE_VULKAN_BACKEND=$<BOOL:${PICCOLO_ENABLE_VULKAN_BACKEND}>
  PICCOLO_ENABLE_D3D12_BACKEND=$<BOOL:${PICCOLO_ENABLE_D3D12_BACKEND}>)
```

- [ ] **Step 2: Make Vulkan SDK setup conditional**

In `engine/CMakeLists.txt`:

- Set `vulkan_include`, `vulkan_lib`, `glslangValidator_executable`, and Vulkan layer compile definitions only inside `if(PICCOLO_ENABLE_VULKAN_BACKEND)`.
- Keep `dxc_executable` lookup inside `if(WIN32 AND PICCOLO_ENABLE_D3D12_BACKEND)`.
- Require `dxc.exe` when D3D12 is enabled.
- Require `glslangValidator` when Vulkan is enabled.

- [ ] **Step 3: Make runtime source/link conditional**

In `engine/source/runtime/CMakeLists.txt`:

- Exclude `interface/d3d12/*.cpp` when `NOT PICCOLO_ENABLE_D3D12_BACKEND`.
- Exclude `interface/vulkan/*.cpp` when `NOT PICCOLO_ENABLE_VULKAN_BACKEND`.
- Link `${vulkan_lib}` only when `PICCOLO_ENABLE_VULKAN_BACKEND`.
- Add `${vulkan_include}` and VMA include dirs only when `PICCOLO_ENABLE_VULKAN_BACKEND`.
- Link `d3d12.lib dxgi.lib dxguid.lib` only when `PICCOLO_ENABLE_D3D12_BACKEND`.

- [ ] **Step 4: Make shader compilation conditional**

In `engine/shader/CMakeLists.txt`:

- Call `compile_shader()` only when `PICCOLO_ENABLE_VULKAN_BACKEND`.
- Call `compile_hlsl_shader()` only when `WIN32 AND PICCOLO_ENABLE_D3D12_BACKEND`.
- Ensure `PiccoloShaderCompile` exists even when only one backend is enabled.

In `render_shader_bytecode.h`, keep Vulkan shader macros available only when Vulkan headers are generated and D3D12 shader macros available only when DXIL headers are generated. If a selected backend has no bytecode, D3D12 should fail at initialization instead of falling through to Vulkan unless fallback is explicitly enabled.

- [ ] **Step 5: Guard backend factory includes**

In `render_system.cpp`, wrap includes and factory cases:

```cpp
#if PICCOLO_ENABLE_VULKAN_BACKEND
#include "runtime/function/render/interface/vulkan/vulkan_rhi.h"
#endif

#if PICCOLO_ENABLE_D3D12_BACKEND && defined(_WIN32)
#include "runtime/function/render/interface/d3d12/d3d12_rhi.h"
#endif
```

In `createRHIBackend()`:

- Return `std::make_shared<VulkanRHI>()` only when Vulkan is enabled.
- Return `std::make_shared<D3D12RHI>()` only when Windows D3D12 is enabled.
- Return `nullptr` otherwise so existing `"is not implemented in this build"` logging remains useful.

- [ ] **Step 6: Configure a D3D12-only Windows build**

Run:

```powershell
cmake -S . -B build_d3d12_only -DPICCOLO_ENABLE_VULKAN_BACKEND=OFF -DPICCOLO_ENABLE_D3D12_BACKEND=ON
cmake --build build_d3d12_only --config Debug --target PiccoloEditor -- /verbosity:minimal
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build_d3d12_only -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
```

If the smoke script does not yet support `-BuildDir`, add that parameter in this task and default it to `build`.

Expected:

- Configure does not require Vulkan SDK libraries for runtime/imgui linking.
- Build succeeds.
- D3D12 smoke passes without fallback.

- [ ] **Step 7: Verify no Vulkan link dependency in D3D12-only build**

Run:

```powershell
rg -n "vulkan-1|imgui_impl_vulkan|VulkanRHI|VulkanUtil|vk_mem_alloc" build_d3d12_only
```

Expected: no matches in generated build files for PiccoloRuntime/PiccoloEditor/imgui targets. Matches inside source cache paths or unrelated text logs must be reviewed and listed in the commit message if unavoidable.

- [ ] **Step 8: Commit**

Run:

```powershell
git add engine/CMakeLists.txt engine/source/runtime/CMakeLists.txt engine/shader/CMakeLists.txt cmake/ShaderCompile.cmake engine/source/runtime/function/render/render_system.cpp engine/source/runtime/function/render/render_shader_bytecode.h scripts/tests/render_backend/smoke_backend_boot.ps1 README.md ReleaseNotes.md
git commit -m "feat: add backend build options"
```

## Task 8: Preserve Optional Windows Vulkan And Non-Windows Vulkan

**Files:**
- Modify: `engine/CMakeLists.txt`
- Modify: `engine/source/runtime/CMakeLists.txt`
- Modify: `engine/3rdparty/imgui.cmake`
- Modify: `.github/workflows/build_windows.yml`
- Modify: `README.md`
- Modify: `ReleaseNotes.md`

- [ ] **Step 1: Configure a Windows dual-backend build**

Run:

```powershell
cmake -S . -B build_dual_backend -DPICCOLO_ENABLE_VULKAN_BACKEND=ON -DPICCOLO_ENABLE_D3D12_BACKEND=ON
cmake --build build_dual_backend --config Debug --target PiccoloEditor -- /verbosity:minimal
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build_dual_backend -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build_dual_backend -Configuration Debug -TimeoutSeconds 20 -RenderBackend Vulkan -ExpectedBackend Vulkan
```

Expected: both D3D12 and explicit Vulkan smoke paths pass.

- [ ] **Step 2: Verify non-Windows configure rules**

On Linux/macOS, run:

```bash
cmake -S . -B build -DPICCOLO_ENABLE_VULKAN_BACKEND=ON -DPICCOLO_ENABLE_D3D12_BACKEND=OFF
cmake --build build --target PiccoloEditor --parallel
```

Expected:

- D3D12 `.cpp` files are not compiled.
- Vulkan remains the active backend.
- No Windows SDK/D3D12/DXGI libraries are required.

- [ ] **Step 3: Add CI matrix entries**

In `.github/workflows/build_windows.yml`, add a backend mode matrix:

```yaml
strategy:
  matrix:
    configuration: [Debug, Release]
    backend_mode: [d3d12_only, dual_backend]
```

Configure with:

```yaml
run: >
  cmake -S . -B build
  ${{ matrix.backend_mode == 'd3d12_only' && '-DPICCOLO_ENABLE_VULKAN_BACKEND=OFF -DPICCOLO_ENABLE_D3D12_BACKEND=ON' || '-DPICCOLO_ENABLE_VULKAN_BACKEND=ON -DPICCOLO_ENABLE_D3D12_BACKEND=ON' }}
```

If GitHub expressions are awkward inside a folded command, split into two configure steps guarded by `if: matrix.backend_mode == '...'`.

- [ ] **Step 4: Add CI smoke commands**

Keep D3D12 smoke for Debug in both Windows modes:

```yaml
run: ./scripts/tests/render_backend/smoke_backend_boot.ps1 -Configuration Debug -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
```

Add Vulkan smoke only for the `dual_backend` Debug mode:

```yaml
run: ./scripts/tests/render_backend/smoke_backend_boot.ps1 -Configuration Debug -RenderBackend Vulkan -ExpectedBackend Vulkan
```

- [ ] **Step 5: Update docs**

In `README.md`, document the two Windows build modes:

```markdown
cmake -S . -B build_d3d12_only -DPICCOLO_ENABLE_VULKAN_BACKEND=OFF -DPICCOLO_ENABLE_D3D12_BACKEND=ON
cmake -S . -B build_dual_backend -DPICCOLO_ENABLE_VULKAN_BACKEND=ON -DPICCOLO_ENABLE_D3D12_BACKEND=ON
```

- [ ] **Step 6: Commit**

Run:

```powershell
git add engine/CMakeLists.txt engine/source/runtime/CMakeLists.txt engine/3rdparty/imgui.cmake .github/workflows/build_windows.yml README.md ReleaseNotes.md
git commit -m "ci: validate d3d12-only and dual backend builds"
```

## Task 9: Manual D3D12 Runtime Validation

**Files:**
- Modify: `Docs/2026-06-07-d3d12-render-backend-current-progress.md`
- Modify: `ReleaseNotes.md`

- [ ] **Step 1: Build Debug and Release D3D12-only editors**

Run:

```powershell
cmake --build build_d3d12_only --config Debug --target PiccoloEditor -- /verbosity:minimal
cmake --build build_d3d12_only --config Release --target PiccoloEditor -- /verbosity:minimal
```

Expected: both builds succeed.

- [ ] **Step 2: Run smoke validation**

Run:

```powershell
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build_d3d12_only -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build_d3d12_only -Configuration Release -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
```

Expected: both smoke runs pass.

- [ ] **Step 3: Run manual editor scenarios**

Launch the Debug editor from `build_d3d12_only` and exercise:

```text
1. Boot to default world and wait 60 seconds.
2. Resize the editor window repeatedly, including very small sizes.
3. Minimize and restore the editor.
4. Move the camera through the default scene.
5. Select/pick mesh objects in the viewport.
6. Toggle or display editor UI panels.
7. Trigger axis/debug draw rendering.
8. Let particle simulation run for 60 seconds after a scene load.
9. Reload the default level if the editor exposes that workflow.
10. Close the editor normally.
```

Expected:

- No crash or hang.
- Main camera and post-process output remain visible.
- ImGui continues rendering after resize/minimize/restore.
- Picking returns stable object IDs.
- Debug draw and particles render or update without D3D12 errors.

- [ ] **Step 4: Scan logs after manual run**

Run:

```powershell
rg -n "D3D12 ERROR|DEVICE_REMOVED|DXGI_ERROR|resource state|descriptor heap|root signature|command list|Falling back to Vulkan|RHI backend initialization failed|\\[error\\]" build_d3d12_only
```

Expected: no D3D12/fallback/error matches from the manual run logs. Document any unrelated existing warning separately.

- [ ] **Step 5: Record validation evidence**

Append to `Docs/2026-06-07-d3d12-render-backend-current-progress.md`:

```markdown
## Windows D3D12 Replacement Validation

- Debug D3D12-only build: passed.
- Release D3D12-only build: passed.
- Debug D3D12-only smoke: passed, no Vulkan fallback.
- Release D3D12-only smoke: passed, no Vulkan fallback.
- Manual editor run covered boot, 60-second idle, resize, minimize/restore, camera movement, picking, UI, debug draw, particles, and normal shutdown.
- Log scan found no D3D12 debug-layer, device removed, descriptor heap, root signature, resource state, command list, initialization, or fallback errors.
```

- [ ] **Step 6: Commit**

Run:

```powershell
git add Docs/2026-06-07-d3d12-render-backend-current-progress.md ReleaseNotes.md
git commit -m "docs: record d3d12 replacement validation"
```

## Task 10: Final Cleanup And Public Documentation

**Files:**
- Modify: `README.md`
- Modify: `ReleaseNotes.md`
- Modify: `Docs/2026-06-07-d3d12-render-backend-current-status.md`
- Modify: `Docs/2026-06-07-d3d12-render-backend-progress-audit.md`

- [ ] **Step 1: Run final source scans**

Run:

```powershell
rg -n "m_dummy_|dummy" engine/source/runtime/function/render/interface/d3d12
rg -n "VulkanRHI|VulkanUtil|VmaAllocation|Vk[A-Z_]|ImGui_ImplVulkan" engine/source/runtime/function/render -g "!interface/vulkan/**"
rg -n "vulkan-1|imgui_impl_vulkan|vk_mem_alloc" build_d3d12_only
```

Expected:

- No D3D12 dummy wrapper names.
- Shared render-layer Vulkan matches are limited to backend selection, optional Vulkan UI branches guarded by `PICCOLO_ENABLE_VULKAN_BACKEND`, backend enum names, and comments.
- D3D12-only build files do not link `vulkan-1` or `imgui_impl_vulkan`.

- [ ] **Step 2: Run final validation commands**

Run:

```powershell
git diff --check
cmake --build build_d3d12_only --config Debug --target PiccoloEditor -- /verbosity:minimal
cmake --build build_d3d12_only --config Release --target PiccoloEditor -- /verbosity:minimal
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build_d3d12_only -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build_d3d12_only -Configuration Release -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
cmake --build build_dual_backend --config Debug --target PiccoloEditor -- /verbosity:minimal
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build_dual_backend -Configuration Debug -TimeoutSeconds 20 -RenderBackend Vulkan -ExpectedBackend Vulkan
```

Expected: every command succeeds.

- [ ] **Step 3: Consolidate README**

In `README.md`, ensure the render backend section states:

```markdown
- Windows primary mode is D3D12.
- Windows D3D12-only builds can be configured with `PICCOLO_ENABLE_VULKAN_BACKEND=OFF`.
- Vulkan remains available for Windows debug/fallback builds when `PICCOLO_ENABLE_VULKAN_BACKEND=ON`.
- Linux/macOS continue to use Vulkan.
- D3D12 builds require `dxc.exe`.
- Vulkan builds require Vulkan SDK/glslang.
```

- [ ] **Step 4: Consolidate progress docs**

Update `Docs/2026-06-07-d3d12-render-backend-current-status.md` and `Docs/2026-06-07-d3d12-render-backend-progress-audit.md` to reflect:

- D3D12-only Windows build option exists.
- Windows deployment config does not silently fall back to Vulkan.
- Optional Windows Vulkan build remains validated.
- Non-Windows Vulkan remains supported.
- Remaining risks are normal engine validation, not incomplete backend wiring.

- [ ] **Step 5: Commit**

Run:

```powershell
git add README.md ReleaseNotes.md Docs/2026-06-07-d3d12-render-backend-current-status.md Docs/2026-06-07-d3d12-render-backend-progress-audit.md
git commit -m "docs: finalize d3d12 replacement guidance"
```

## Acceptance Criteria

- Windows D3D12-only build configures with `-DPICCOLO_ENABLE_VULKAN_BACKEND=OFF -DPICCOLO_ENABLE_D3D12_BACKEND=ON`.
- Windows D3D12-only `PiccoloEditor` builds in Debug and Release.
- Windows D3D12-only smoke passes with `RenderBackend=D3D12`, `ExpectedBackend=D3D12`, and fallback forbidden.
- Windows dual-backend build still supports explicit `RenderBackend=Vulkan`.
- Non-Windows builds keep Vulkan and do not compile D3D12 `.cpp` files.
- `PiccoloRuntime` and `imgui` do not link Vulkan in the D3D12-only Windows build.
- D3D12 RHI no longer uses misleading `dummy` names for real frame resources.
- D3D12 buffer host mirrors are explicitly valid/invalid and do not replace GPU copy/readback synchronization.
- D3D12 render pass/subpass/input attachment/resolve/final state transitions are clean under debug-layer log scans.
- Editor manual validation covers boot, idle, resize, minimize/restore, camera, picking, UI, debug draw, particles, and shutdown.
- Documentation explains D3D12-primary, optional Vulkan, DXC, Vulkan SDK, and smoke commands.

## Risk Notes

- D3D12 render-pass emulation remains the highest technical risk because current passes are Vulkan-shaped and use subpasses/input attachments.
- Shader parity remains high risk because HLSL and GLSL must keep binding order, structure layout, matrix conventions, and compute behavior aligned.
- Particle compute is the broadest runtime path: it uses copy, readback, dispatch, indirect dispatch, descriptors, and per-frame resource updates.
- D3D12-only Windows builds remove Vulkan link pressure but make shader build configuration stricter; missing `dxc.exe` must fail clearly.
- Optional Windows Vulkan should remain available until D3D12 has longer production soak time.

## Self Review

- Spec coverage: The plan covers current-state audit, D3D12 wrapper cleanup, copy/map semantics, render pass and particle hardening, fallback policy, UI dependency split, CMake backend options, optional Vulkan preservation, validation, and docs.
- Open-marker scan: The plan contains no unfinished marker task; every task lists exact files, commands, expected outcomes, and commit points.
- Type consistency: The renamed D3D12 members are introduced in Task 2 and used consistently in later scan criteria. Backend option names are introduced in Task 7 and used consistently by UI, runtime, shader, and CI tasks.
