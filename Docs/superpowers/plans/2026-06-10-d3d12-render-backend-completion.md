# D3D12 Render Backend Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Windows D3D12 backend a Vulkan-compatible replacement for Piccolo rendering: same visual output within practical tolerance, no 1 FPS regressions, clean backend startup, and stable frame submission.

**Architecture:** Keep Vulkan as the behavioral reference and make D3D12 honor the existing RHI contract rather than adding pass-level D3D12-only behavior unless the API difference requires it. Split work by ownership boundaries so multiple sub agents can run in parallel: RHI synchronization/state, pass-level visual parity, shader/layout parity, and validation evidence. Shared files are staged in serial waves to avoid conflicting edits.

**Tech Stack:** C++17, Direct3D 12, DXGI, HLSL/DXIL via `dxc.exe`, existing Piccolo RHI abstraction, existing Vulkan backend, CMake, PowerShell smoke scripts.

---

## Constraints

- Do not add unit tests, CTest targets, or new engine test code unless the user explicitly asks for them later.
- Verification can use existing PowerShell smoke scripts and manual/recorded runtime evidence.
- Every code-changing sub agent must work in an isolated branch/worktree, commit its own scoped changes, and report exact files changed.
- Workers are not alone in the codebase. They must not revert edits made by other workers; they must rebase or merge mainline changes before final handoff.
- Keep Vulkan behavior as the oracle. D3D12-specific branches are acceptable only where D3D12 has a different API requirement, such as resource states, descriptor heaps, present flags, and queue objects.

## Current Baseline

- Latest known good commit: `5ad7e24 Fix D3D12 render backend frame pacing`.
- D3D12 visible smoke has passed with non-black output.
- D3D12 Debug FPS has been observed around `575 FPS`; Vulkan Debug around `562 FPS` on the sampled scene.
- The previous 1 FPS issue was fixed by moving D3D12 particle normal/depth copy into the main command buffer, correcting upload/default heap behavior, preserving DEFAULT heap for particle UAV buffers, adding scratch indirect dispatch arguments, enabling tearing present, and removing per-frame global waits from the main render path.
- Remaining gaps are visual parity, real compute queue behavior, multi-submit ordering semantics, descriptor churn reduction, and broader validation evidence.

## File Ownership Map

**RHI Core**
- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h`
- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
- `engine/source/runtime/function/render/interface/d3d12/d3d12_descriptor_heap.h`
- `engine/source/runtime/function/render/interface/d3d12/d3d12_descriptor_heap.cpp`

**Render Pipeline And Passes**
- `engine/source/runtime/function/render/render_pipeline.cpp`
- `engine/source/runtime/function/render/render_pass.h`
- `engine/source/runtime/function/render/passes/main_camera_pass.h`
- `engine/source/runtime/function/render/passes/main_camera_pass.cpp`
- `engine/source/runtime/function/render/passes/directional_light_pass.h`
- `engine/source/runtime/function/render/passes/directional_light_pass.cpp`
- `engine/source/runtime/function/render/passes/point_light_pass.h`
- `engine/source/runtime/function/render/passes/point_light_pass.cpp`
- `engine/source/runtime/function/render/passes/particle_pass.h`
- `engine/source/runtime/function/render/passes/particle_pass.cpp`
- `engine/source/runtime/function/render/passes/tone_mapping_pass.h`
- `engine/source/runtime/function/render/passes/tone_mapping_pass.cpp`
- `engine/source/runtime/function/render/passes/color_grading_pass.h`
- `engine/source/runtime/function/render/passes/color_grading_pass.cpp`
- `engine/source/runtime/function/render/passes/fxaa_pass.h`
- `engine/source/runtime/function/render/passes/fxaa_pass.cpp`
- `engine/source/runtime/function/render/passes/combine_ui_pass.h`
- `engine/source/runtime/function/render/passes/combine_ui_pass.cpp`
- `engine/source/runtime/function/render/passes/ui_pass.h`
- `engine/source/runtime/function/render/passes/ui_pass.cpp`
- `engine/source/runtime/function/render/debugdraw/debug_draw_pipeline.h`
- `engine/source/runtime/function/render/debugdraw/debug_draw_pipeline.cpp`
- `engine/source/runtime/function/render/debugdraw/debug_draw_manager.cpp`

**Shader And Layout Parity**
- `engine/shader/glsl/**`
- `engine/shader/hlsl/**`
- `engine/shader/include/**`
- `engine/shader/CMakeLists.txt`
- `cmake/ShaderCompile.cmake`
- `engine/source/runtime/function/render/render_shader_bytecode.h`
- `engine/source/runtime/function/render/render_resource.cpp`
- `engine/source/runtime/function/render/render_camera.cpp`

**Existing Verification Scripts**
- `scripts/tests/render_backend/smoke_backend_boot.ps1`
- `scripts/tests/render_backend/smoke_d3d12_editor_visible.ps1`
- `scripts/tests/render_backend/assert_log.ps1`

---

## Parallel Execution Model

Use three waves.

**Wave 1 can run in parallel:**
- Task 1: RHI multi-submit ordering.
- Task 3: Main camera visual parity audit/fix.
- Task 4: Shadow visual parity audit/fix.
- Task 5: Particle visual and compute-call-site audit/fix.
- Task 6: Post chain visual parity audit/fix.
- Task 7: UI and debug draw parity audit/fix.
- Task 8: Shader/layout parity audit/fix.
- Task 9: Validation evidence runner using existing scripts only.

**Wave 2 starts after Task 1 and Task 5 finish:**
- Task 2: Real D3D12 compute queue support, or explicit documented same-queue finalization if real async compute creates worse hazards.
- Task 10: Descriptor churn reduction.

**Wave 3 starts after all code-changing tasks finish:**
- Task 11: Integration validation and performance gate.
- Task 12: Final documentation update and merge.

---

## Shared FPS Sampling Command

Use this inline PowerShell command whenever a task asks for FPS sampling. Run it twice, once with `$Backend = 'D3D12'` and once with `$Backend = 'Vulkan'`. It does not create test code; it temporarily edits the built editor config, samples the existing window title, restores the config, and writes a tail average.

```powershell
$Backend = 'D3D12'
$BuildDir = [System.IO.Path]::GetFullPath('build')
$Configuration = 'Debug'
$WarmupSeconds = 10
$SampleSeconds = 20
$EditorPath = Join-Path $BuildDir "engine\source\editor\$Configuration\PiccoloEditor.exe"
$EditorConfigPath = Join-Path ([System.IO.Path]::GetDirectoryName($EditorPath)) 'PiccoloEditor.ini'
$OriginalConfigBytes = [System.IO.File]::ReadAllBytes($EditorConfigPath)
$OriginalConfigText = [System.IO.File]::ReadAllText($EditorConfigPath)
if ($OriginalConfigText -match '(?m)^\s*RenderBackend\s*=') {
    $UpdatedConfigText = $OriginalConfigText -replace '(?m)^\s*RenderBackend\s*=.*$', "RenderBackend=$Backend"
} else {
    $UpdatedConfigText = $OriginalConfigText.TrimEnd([char[]]@("`r", "`n")) + [Environment]::NewLine + "RenderBackend=$Backend" + [Environment]::NewLine
}
$Samples = New-Object System.Collections.Generic.List[int]
$Process = $null
try {
    $Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($EditorConfigPath, $UpdatedConfigText, $Utf8NoBom)
    Get-Process PiccoloEditor -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    $Process = Start-Process -FilePath $EditorPath -WorkingDirectory (Get-Location) -PassThru -WindowStyle Normal
    Start-Sleep -Seconds $WarmupSeconds
    $EndTime = [DateTime]::UtcNow.AddSeconds($SampleSeconds)
    while ([DateTime]::UtcNow -lt $EndTime) {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "PiccoloEditor exited during FPS sampling for $Backend."
        }
        $Title = $Process.MainWindowTitle
        if ($Title -match 'Piccolo -\s*(\d+)\s*FPS') {
            $Samples.Add([int]$Matches[1])
        }
        Start-Sleep -Milliseconds 500
    }
} finally {
    [System.IO.File]::WriteAllBytes($EditorConfigPath, $OriginalConfigBytes)
    if ($Process -ne $null) {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            $Process.CloseMainWindow() | Out-Null
            Start-Sleep -Seconds 2
            $Process.Refresh()
            if (-not $Process.HasExited) {
                Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
            }
        }
    }
}
if ($Samples.Count -lt 5) {
    throw "Too few FPS samples for $Backend: $($Samples.Count)"
}
$TailCount = [Math]::Min(20, $Samples.Count)
$Tail = $Samples.ToArray() | Select-Object -Last $TailCount
$Average = ($Tail | Measure-Object -Average).Average
Write-Host ("{0} FPS samples={1} tail_count={2} tail_average={3:N2}" -f $Backend, $Samples.Count, $TailCount, $Average)
```

Expected: both backend runs collect at least five samples; D3D12 tail average is not more than 5% below Vulkan on the same scene, and D3D12 never collapses near `1 FPS`.

---

## Task 1: Fix D3D12 `queueSubmit` Multi-Submit Ordering

**Owner:** RHI synchronization worker.

**Files:**
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
- Do not modify: pass files, shader files, verification scripts.

**Context:**
- Current `D3D12RHI::queueSubmit()` starts near `d3d12_rhi.cpp:7310`.
- It currently flattens all submit command lists into one `ExecuteCommandLists()` call, then signals all semaphores afterward.
- Vulkan `vkQueueSubmit(submitCount, submits)` preserves submit boundaries. D3D12 must process waits, command lists, and signals per submit in order.

- [ ] **Step 1: Read the current submit implementation**

Run:

```powershell
$p='engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp'
Get-Content $p | Select-Object -Skip 7310 -First 180
```

Expected: the function collects `command_lists` across all submits before one `ExecuteCommandLists()` call.

- [ ] **Step 2: Replace flattened execution with per-submit execution**

Implement this behavior inside `D3D12RHI::queueSubmit()`:

```cpp
for (uint32_t submit_index = 0; submit_index < submitCount; ++submit_index)
{
    const RHISubmitInfo& submit = pSubmits[submit_index];

    for (uint32_t semaphore_index = 0; semaphore_index < submit.waitSemaphoreCount; ++semaphore_index)
    {
        if (submit.pWaitSemaphores == nullptr)
        {
            return false;
        }

        auto* semaphore = static_cast<D3D12RHISemaphore*>(submit.pWaitSemaphores[semaphore_index]);
        if (semaphore != nullptr && semaphore->fence != nullptr && semaphore->has_pending_signal)
        {
            if (FAILED(command_queue->Wait(semaphore->fence.Get(), semaphore->wait_value)))
            {
                return false;
            }
            semaphore->has_pending_signal = false;
        }
    }

    std::vector<ID3D12CommandList*> submit_command_lists;
    for (uint32_t command_buffer_index = 0; command_buffer_index < submit.commandBufferCount; ++command_buffer_index)
    {
        if (submit.pCommandBuffers == nullptr)
        {
            return false;
        }

        auto* d3d_command_buffer =
            static_cast<D3D12RHICommandBuffer*>(submit.pCommandBuffers[command_buffer_index]);
        if (d3d_command_buffer == nullptr || d3d_command_buffer->command_list == nullptr)
        {
            continue;
        }

        if (d3d_command_buffer->is_open)
        {
            if (FAILED(d3d_command_buffer->command_list->Close()))
            {
                d3d_command_buffer->is_open = false;
                return false;
            }
            d3d_command_buffer->is_open = false;
            d3d_command_buffer->has_recorded_commands = true;
        }

        if (d3d_command_buffer->has_recorded_commands)
        {
            submit_command_lists.push_back(d3d_command_buffer->command_list.Get());
        }
    }

    if (!submit_command_lists.empty())
    {
        command_queue->ExecuteCommandLists(static_cast<UINT>(submit_command_lists.size()),
                                           submit_command_lists.data());
    }

    for (uint32_t semaphore_index = 0; semaphore_index < submit.signalSemaphoreCount; ++semaphore_index)
    {
        if (submit.pSignalSemaphores == nullptr)
        {
            return false;
        }

        auto* semaphore =
            static_cast<D3D12RHISemaphore*>(const_cast<RHISemaphore*>(submit.pSignalSemaphores[semaphore_index]));
        if (semaphore == nullptr || semaphore->fence == nullptr)
        {
            return false;
        }

        const uint64_t signal_value = semaphore->next_signal_value + 1ULL;
        if (FAILED(command_queue->Signal(semaphore->fence.Get(), signal_value)))
        {
            return false;
        }
        semaphore->next_signal_value  = signal_value;
        semaphore->wait_value         = signal_value;
        semaphore->has_pending_signal = true;
    }
}
```

Keep the existing final `fence` signal after all submits.

- [ ] **Step 3: Build**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /clp:ErrorsOnly /v:minimal
```

Expected: exit code `0`.

- [ ] **Step 4: Run backend boot smoke**

Run:

```powershell
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
```

Expected: log contains `Initialized RHI backend: D3D12` and `engine start`, with no fallback.

- [ ] **Step 5: Commit**

Run:

```powershell
git add engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp
git commit -m "fix: preserve d3d12 submit ordering"
```

---

## Task 2: Add Real D3D12 Compute Queue Or Finalize Same-Queue Semantics

**Owner:** RHI queue worker. Start only after Task 1 and Task 5 finish.

**Files:**
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
- Do not modify: `particle_pass.cpp` unless Task 5 explicitly hands off a required call-site change.

**Decision Rule:**
- Prefer a real `D3D12_COMMAND_LIST_TYPE_COMPUTE` queue if the current particle compute command buffers can be recorded and submitted without breaking graphics resource-state ownership.
- If real async compute creates resource ownership hazards that cannot be solved in this task, keep compute mapped to direct queue but make that explicit in code comments and logs, then defer async compute to a separate feature.

- [ ] **Step 1: Inspect current queue creation and wrapper assignment**

Run:

```powershell
rg -n "createCommandQueue|getQueueFamilyIndices|getComputeQueue|m_compute_queue|m_d3d12_command_queue" engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.*
```

Expected: graphics and compute RHI wrappers currently share the same `ID3D12CommandQueue*`.

- [ ] **Step 2: Add queue identity to the wrapper**

In the internal `D3D12RHIQueue` wrapper near the top of `d3d12_rhi.cpp`, add a command list type field:

```cpp
D3D12_COMMAND_LIST_TYPE command_list_type {D3D12_COMMAND_LIST_TYPE_DIRECT};
```

Use it only for queue selection and diagnostics.

- [ ] **Step 3: Add compute queue members only if using real compute**

In `D3D12RHI` private members in `d3d12_rhi.h`, add:

```cpp
ComPtr<ID3D12CommandQueue> m_d3d12_compute_command_queue;
ComPtr<ID3D12Fence>        m_d3d12_compute_fence;
uint64_t                   m_d3d12_compute_fence_value {1};
HANDLE                     m_d3d12_compute_fence_event {nullptr};
```

Use these names exactly so future code can grep the compute queue path.

- [ ] **Step 4: Create the compute queue only when supported**

In queue creation code, create the direct queue as today. Then create a compute queue with:

```cpp
D3D12_COMMAND_QUEUE_DESC compute_queue_desc {};
compute_queue_desc.Type  = D3D12_COMMAND_LIST_TYPE_COMPUTE;
compute_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
```

If creation fails, log one warning and keep `m_compute_queue` mapped to the graphics queue. Do not throw, because D3D12-capable devices can still render correctly on the direct queue.

- [ ] **Step 5: Keep current frame rendering on graphics/direct queue**

Do not move main rendering command buffers to compute queue. `prepareBeforePass()` and `submitRendering()` must continue using the graphics queue.

- [ ] **Step 6: Route `queueWaitIdle()` through the queue wrapper**

For non-graphics queues, signal and wait on that queue's fence instead of calling the global graphics `waitForGpu()`.

- [ ] **Step 7: Build and smoke**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /clp:ErrorsOnly /v:minimal
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
.\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10 -MinNonBlackRatio 0.01
```

Expected: all commands exit `0`.

- [ ] **Step 8: Commit**

Run:

```powershell
git add engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp
git commit -m "feat: clarify d3d12 compute queue semantics"
```

---

## Task 3: Main Camera, GBuffer, Deferred, Forward, Skybox Parity

**Owner:** Main camera worker.

**Files:**
- Modify if needed: `engine/source/runtime/function/render/passes/main_camera_pass.h`
- Modify if needed: `engine/source/runtime/function/render/passes/main_camera_pass.cpp`
- Modify only if enum/subpass order is proven wrong: `engine/source/runtime/function/render/render_pass.h`
- Do not modify: `d3d12_rhi.cpp`, shader files, particle files.

**Focus:**
- `setupAttachments()` near `main_camera_pass.cpp:41`.
- `setupRenderPass()` near `main_camera_pass.cpp:124`.
- `setupPipelines()` near `main_camera_pass.cpp:792`.
- `drawMeshGbuffer()`, `drawDeferredLighting()`, `drawMeshLighting()`, `drawSkybox()`.

- [ ] **Step 1: Produce a Vulkan reference capture**

Run the editor with `RenderBackend=Vulkan` using the existing config override pattern from `smoke_backend_boot.ps1`. Capture one screenshot of the default level after warmup. Save evidence outside source control under `build\d3d12_parity\main_camera_vulkan.png`.

- [ ] **Step 2: Produce a D3D12 capture**

Run:

```powershell
.\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10 -MinNonBlackRatio 0.01
```

Copy the resulting capture to `build\d3d12_parity\main_camera_d3d12.png`.

- [ ] **Step 3: Compare pass setup**

Verify these values match Vulkan intent:

```text
GBuffer A: RHI_FORMAT_R8G8B8A8_UNORM
GBuffer B: RHI_FORMAT_R8G8B8A8_UNORM
GBuffer C: RHI_FORMAT_R8G8B8A8_SRGB
Post process: RHI_FORMAT_R16G16B16A16_SFLOAT
Depth: same format returned by RHI depth format selection
Fullscreen passes: front face/cull/depth/blend state matches Vulkan behavior
Mesh passes: cull and front face preserve the expected winding after backend projection adjustment
```

- [ ] **Step 4: Fix only proven mismatches**

If the D3D12 image is vertically flipped, culled, missing skybox, missing GBuffer lighting, or shows wrong color space while Vulkan is correct, patch only the matching setup block in `main_camera_pass.cpp`. Do not add new D3D12 special cases before confirming the existing RHI state translation cannot handle it.

- [ ] **Step 5: Build and smoke both backends**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /clp:ErrorsOnly /v:minimal
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -TimeoutSeconds 20 -RenderBackend Vulkan -ExpectedBackend Vulkan
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
.\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10 -MinNonBlackRatio 0.01
```

- [ ] **Step 6: Commit if code changed**

Run:

```powershell
git add engine/source/runtime/function/render/passes/main_camera_pass.h engine/source/runtime/function/render/passes/main_camera_pass.cpp engine/source/runtime/function/render/render_pass.h
git commit -m "fix: align d3d12 main camera rendering"
```

If no code changed, return an evidence report instead of an empty commit.

---

## Task 4: Shadow Pass Parity

**Owner:** Shadow worker.

**Files:**
- Modify if needed: `engine/source/runtime/function/render/passes/directional_light_pass.h`
- Modify if needed: `engine/source/runtime/function/render/passes/directional_light_pass.cpp`
- Modify if needed: `engine/source/runtime/function/render/passes/point_light_pass.h`
- Modify if needed: `engine/source/runtime/function/render/passes/point_light_pass.cpp`
- Do not modify: `d3d12_rhi.cpp`, main camera files, shader files.

- [ ] **Step 1: Inspect shadow render pass and pipeline setup**

Run:

```powershell
rg -n "setupRenderPass|setupPipelines|drawModel|depth|stencil|frontFace|cullMode|viewport|scissor" engine/source/runtime/function/render/passes/directional_light_pass.cpp engine/source/runtime/function/render/passes/point_light_pass.cpp
```

- [ ] **Step 2: Verify D3D12 output is not missing shadow contribution**

Compare Vulkan and D3D12 captures in a scene where directional light shadows are visible. Record whether D3D12 has missing shadows, inverted shadows, acne, peter-panning, or point-light shadow differences.

- [ ] **Step 3: Fix pass-level mismatches only**

Allowed fixes:

```text
Correct shadow viewport/scissor setup.
Correct depth compare/write state in the pass pipeline create info.
Correct cull/front-face state only if D3D12 winding differs from Vulkan reference.
Correct attachment load/store/final layout requests if the RHI receives the wrong intent.
```

Not allowed in this task:

```text
Do not change D3D12 global format mapping.
Do not change shader math.
Do not change particle or main camera pass.
```

- [ ] **Step 4: Build and run visible smoke**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /clp:ErrorsOnly /v:minimal
.\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10 -MinNonBlackRatio 0.01
```

- [ ] **Step 5: Commit if code changed**

Run:

```powershell
git add engine/source/runtime/function/render/passes/directional_light_pass.h engine/source/runtime/function/render/passes/directional_light_pass.cpp engine/source/runtime/function/render/passes/point_light_pass.h engine/source/runtime/function/render/passes/point_light_pass.cpp
git commit -m "fix: align d3d12 shadow passes"
```

---

## Task 5: Particle Copy, Simulation, And Billboard Parity

**Owner:** Particle worker.

**Files:**
- Modify: `engine/source/runtime/function/render/passes/particle_pass.h`
- Modify: `engine/source/runtime/function/render/passes/particle_pass.cpp`
- Read only: `engine/source/runtime/function/render/render_pipeline.cpp`
- Do not modify: `d3d12_rhi.cpp` unless Task 2 explicitly requests a call-site handoff.

**Context:**
- D3D12 currently calls `ParticlePass::copyNormalAndDepthImage()` before `submitRendering()`.
- Vulkan calls it after `submitRendering()`.
- D3D12 particle compute submits use `getComputeQueue()`, which currently maps to the direct queue until Task 2 changes or documents it.

- [ ] **Step 1: Inspect D3D12 particle branches**

Run:

```powershell
rg -n "RHIBackendType::D3D12|copyNormalAndDepthImage|simulate|cmdDispatch|cmdDispatchIndirect|queueSubmit|getComputeQueue|barrier|normal|depth" engine/source/runtime/function/render/passes/particle_pass.cpp
```

- [ ] **Step 2: Verify copy timing remains intentional**

Keep this ordering unless proven wrong:

```text
D3D12: main camera draw -> debug draw -> particle normal/depth copy -> submitRendering -> particle simulate
Vulkan: main camera draw -> debug draw -> submitRendering -> particle normal/depth copy -> particle simulate
```

The D3D12 copy must stay inline with the current command buffer to avoid the previous device removal and 1 FPS regression.

- [ ] **Step 3: Audit particle buffer heap assumptions**

Confirm:

```text
Particle UAV/RW buffers use DEFAULT heap.
CPU-updated GPU-read storage buffers use UPLOAD heap when the shader never writes them.
Indirect dispatch argument reads use the scratch argument buffer path.
No descriptor bind path uploads the 128 MB global ring buffer every frame.
```

- [ ] **Step 4: Fix particle-only mismatches**

Allowed fixes:

```text
Correct barriers around normal/depth copy.
Correct D3D12-only inline copy resource states.
Correct particle descriptor update ranges.
Correct particle indirect dispatch argument source/scratch usage.
Correct particle billboard pipeline state if Vulkan reference proves a mismatch.
```

Do not add a post-submit D3D12 copy.

- [ ] **Step 5: Build, smoke, and sample performance**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /clp:ErrorsOnly /v:minimal
.\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10 -MinNonBlackRatio 0.01
```

Then run the FPS sampling command from the "Shared FPS Sampling Command" section for D3D12 and Vulkan. Record D3D12 tail average, Vulkan tail average, and error-hit count in the agent report.

- [ ] **Step 6: Commit if code changed**

Run:

```powershell
git add engine/source/runtime/function/render/passes/particle_pass.h engine/source/runtime/function/render/passes/particle_pass.cpp
git commit -m "fix: align d3d12 particle rendering"
```

---

## Task 6: Post-Process Chain Parity

**Owner:** Post chain worker.

**Files:**
- Modify if needed: `engine/source/runtime/function/render/passes/tone_mapping_pass.h`
- Modify if needed: `engine/source/runtime/function/render/passes/tone_mapping_pass.cpp`
- Modify if needed: `engine/source/runtime/function/render/passes/color_grading_pass.h`
- Modify if needed: `engine/source/runtime/function/render/passes/color_grading_pass.cpp`
- Modify if needed: `engine/source/runtime/function/render/passes/fxaa_pass.h`
- Modify if needed: `engine/source/runtime/function/render/passes/fxaa_pass.cpp`
- Modify if needed: `engine/source/runtime/function/render/passes/combine_ui_pass.h`
- Modify if needed: `engine/source/runtime/function/render/passes/combine_ui_pass.cpp`

- [ ] **Step 1: Inspect pipeline state for fullscreen passes**

Run:

```powershell
rg -n "setupDescriptorSetLayout|setupPipelines|frontFace|cullMode|depthTestEnable|blendEnable|RHI_FORMAT|render_pass|subpass" engine/source/runtime/function/render/passes/tone_mapping_pass.cpp engine/source/runtime/function/render/passes/color_grading_pass.cpp engine/source/runtime/function/render/passes/fxaa_pass.cpp engine/source/runtime/function/render/passes/combine_ui_pass.cpp
```

- [ ] **Step 2: Compare Vulkan and D3D12 screenshots after post**

Use a scene with visible color grading, tone mapping, FXAA edges, and UI composition. Record whether differences are color-space, gamma, missing texture, wrong sampling, or full-screen triangle orientation.

- [ ] **Step 3: Fix only pass-level setup mismatches**

Allowed fixes:

```text
Correct descriptor binding ranges for input textures.
Correct sampler usage for the fullscreen pass.
Correct blend state for combine UI.
Correct depth state for post passes.
Correct fullscreen triangle cull/front-face state if D3D12 culls it.
```

- [ ] **Step 4: Build and smoke**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /clp:ErrorsOnly /v:minimal
.\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10 -MinNonBlackRatio 0.01
```

- [ ] **Step 5: Commit if code changed**

Run:

```powershell
git add engine/source/runtime/function/render/passes/tone_mapping_pass.* engine/source/runtime/function/render/passes/color_grading_pass.* engine/source/runtime/function/render/passes/fxaa_pass.* engine/source/runtime/function/render/passes/combine_ui_pass.*
git commit -m "fix: align d3d12 post process passes"
```

---

## Task 7: UI And Debug Draw Parity

**Owner:** Overlay worker.

**Files:**
- Modify if needed: `engine/source/runtime/function/render/passes/ui_pass.h`
- Modify if needed: `engine/source/runtime/function/render/passes/ui_pass.cpp`
- Modify if needed: `engine/source/runtime/function/render/debugdraw/debug_draw_pipeline.h`
- Modify if needed: `engine/source/runtime/function/render/debugdraw/debug_draw_pipeline.cpp`
- Modify if needed: `engine/source/runtime/function/render/debugdraw/debug_draw_manager.cpp`

- [ ] **Step 1: Inspect D3D12 UI backend path**

Run:

```powershell
rg -n "ImGui_ImplDX12|SetDescriptorHeaps|RenderDrawData|NewFrame|Shutdown|D3D12" engine/source/runtime/function/render/passes/ui_pass.cpp
```

- [ ] **Step 2: Inspect debug draw pipeline**

Run:

```powershell
rg -n "setup|pipeline|descriptor|draw|frontFace|cullMode|blend|depth" engine/source/runtime/function/render/debugdraw
```

- [ ] **Step 3: Compare overlay output**

Verify D3D12 shows:

```text
Editor ImGui panels.
Axis/debug draw primitives.
No descriptor heap corruption after UI draw.
No missing scene rendering after ImGui sets D3D12 descriptor heaps.
```

- [ ] **Step 4: Fix overlay-only mismatches**

Allowed fixes:

```text
Restore the expected descriptor heap after ImGui if later draws depend on it.
Correct ImGui SRV heap handle usage.
Correct debug draw dynamic buffer offsets.
Correct debug draw pipeline state or descriptor set setup.
```

- [ ] **Step 5: Build and visible smoke**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /clp:ErrorsOnly /v:minimal
.\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10 -MinNonBlackRatio 0.01
```

- [ ] **Step 6: Commit if code changed**

Run:

```powershell
git add engine/source/runtime/function/render/passes/ui_pass.* engine/source/runtime/function/render/debugdraw
git commit -m "fix: align d3d12 overlays"
```

---

## Task 8: Shader, Coordinate, And Layout Parity

**Owner:** Shader/layout worker.

**Files:**
- Modify if needed: `engine/shader/glsl/**`
- Modify if needed: `engine/shader/hlsl/**`
- Modify if needed: `engine/shader/include/**`
- Modify if needed: `engine/shader/CMakeLists.txt`
- Modify if needed: `cmake/ShaderCompile.cmake`
- Modify if needed: `engine/source/runtime/function/render/render_shader_bytecode.h`
- Modify if needed: `engine/source/runtime/function/render/render_resource.cpp`
- Modify if needed: `engine/source/runtime/function/render/render_camera.cpp`
- Do not modify: pass C++ files unless coordinating with Task 3-7 owners.

**Known Risk Points:**
- Vulkan projection Y flip exists in render resource/camera code; D3D12 should not duplicate it.
- HLSL uses `register(..., spaceN)` while GLSL uses `set/binding`.
- GLSL deferred/post paths use `subpassInput`; HLSL paths use `Texture2D`.

- [ ] **Step 1: Scan shader bindings**

Run:

```powershell
rg -n "layout\\(|set\\s*=|binding\\s*=|register\\(|space[0-9]|SubpassInput|subpassInput|Texture2D|RWStructuredBuffer|StructuredBuffer|cbuffer" engine/shader/glsl engine/shader/hlsl engine/shader/include
```

- [ ] **Step 2: Build a binding table**

Create an agent report table, not a committed source file, with these columns:

```text
Pass
GLSL file
HLSL file
Set/space
Binding/register
Resource type
Layout-sensitive struct
Mismatch found
Fix file
```

- [ ] **Step 3: Fix confirmed shader mismatches**

Allowed fixes:

```text
Correct HLSL register/space values to match RHI descriptor set layout.
Correct struct packing only when C++ side layout and GLSL/HLSL disagree.
Correct coordinate convention only where Vulkan-specific transform leaks into D3D12.
Correct DXIL shader CMake profile only if the generated bytecode is wrong for the shader stage.
```

- [ ] **Step 4: Reconfigure if shader generation changed**

Run:

```powershell
cmake -S . -B build -DPICCOLO_ENABLE_D3D12_BACKEND=ON -DPICCOLO_ENABLE_VULKAN_BACKEND=ON
cmake --build build --config Debug --target PiccoloEditor -- /clp:ErrorsOnly /v:minimal
```

Expected: DXIL and SPIR-V generation succeed.

- [ ] **Step 5: Smoke both backends**

Run:

```powershell
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -TimeoutSeconds 20 -RenderBackend Vulkan -ExpectedBackend Vulkan
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
```

- [ ] **Step 6: Commit if code changed**

Run:

```powershell
git add engine/shader engine/source/runtime/function/render/render_shader_bytecode.h engine/source/runtime/function/render/render_resource.cpp engine/source/runtime/function/render/render_camera.cpp cmake/ShaderCompile.cmake
git commit -m "fix: align d3d12 shader layouts"
```

---

## Task 9: Validation Evidence Without Adding Test Code

**Owner:** Validation worker.

**Files:**
- Read only: `scripts/tests/render_backend/smoke_backend_boot.ps1`
- Read only: `scripts/tests/render_backend/smoke_d3d12_editor_visible.ps1`
- Read only: `scripts/tests/render_backend/assert_log.ps1`
- Create no new test source files.
- Modify documentation only if recording command instructions: `Docs/2026-06-07-d3d12-render-backend-current-progress.md`

- [ ] **Step 1: Run D3D12 boot smoke**

Run:

```powershell
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
```

- [ ] **Step 2: Run Vulkan boot smoke**

Run:

```powershell
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -TimeoutSeconds 20 -RenderBackend Vulkan -ExpectedBackend Vulkan
```

- [ ] **Step 3: Run Auto boot smoke**

Run:

```powershell
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -TimeoutSeconds 20 -RenderBackend Auto -ExpectedBackend D3D12 -DisallowFallback
```

- [ ] **Step 4: Run D3D12 visible smoke**

Run:

```powershell
.\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10 -MinNonBlackRatio 0.01
```

- [ ] **Step 5: Scan logs for known D3D12 failures**

Run:

```powershell
rg -n "D3D12 ERROR|DEVICE_REMOVED|DXGI_ERROR|resource state|descriptor heap|root signature|command list|Falling back to Vulkan|RHI backend initialization failed|\\[error\\]" build
```

Expected: no matches in the latest smoke logs except unrelated historical logs already identified by path.

- [ ] **Step 6: Report evidence**

Return a Chinese report with:

```text
Build command and exit code.
D3D12 smoke log path.
Vulkan smoke log path.
Auto smoke log path.
D3D12 capture path.
Log scan result.
Any manual visual differences found.
```

Only commit if documentation was updated.

---

## Task 10: Descriptor Dynamic-Offset And Heap Churn Reduction

**Owner:** Descriptor worker. Start after Task 1, because it shares `d3d12_rhi.cpp`.

**Files:**
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
- Modify if needed: `engine/source/runtime/function/render/interface/d3d12/d3d12_descriptor_heap.h`
- Modify if needed: `engine/source/runtime/function/render/interface/d3d12/d3d12_descriptor_heap.cpp`

**Focus:**
- Descriptor/root signature path around `d3d12_rhi.cpp:4637`, `d3d12_rhi.cpp:5092`, `d3d12_rhi.cpp:5958`.
- Avoid per-draw/per-bind heap churn for descriptors that are stable for a frame.
- Do not reintroduce the previous repeated upload of the 128 MB global ring buffer.

- [ ] **Step 1: Locate descriptor bind hot paths**

Run:

```powershell
rg -n "CopyDescriptors|CopyDescriptorsSimple|SetGraphicsRootDescriptorTable|SetComputeRootDescriptorTable|dynamic|transient|descriptor_next|host_data|flushMappedMemoryRanges" engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp
```

- [ ] **Step 2: Add counters temporarily during investigation only**

Use local-only logging or debugger counters to measure descriptor copies per frame. Remove all temporary counters before commit. Do not commit trace macros or noisy logs.

- [ ] **Step 3: Implement caching only where ownership is clear**

Allowed changes:

```text
Cache descriptor table copies for unchanged descriptor set + dynamic offset combinations within a frame.
Reset transient descriptor allocation in resetCommandPool() as currently intended.
Keep CPU-visible upload buffers mapped; do not force DEFAULT heap uploads for CPU-written storage buffers.
Preserve UAV DEFAULT heap behavior for particle RW buffers.
```

- [ ] **Step 4: Verify no trace leftovers**

Run:

```powershell
rg -n "PICCOLO_TRACE|trace_mark|descriptor counter|temporary descriptor marker" engine/source/runtime/function/render/interface/d3d12
```

Expected: no temporary investigation symbols.

- [ ] **Step 5: Build, smoke, and compare FPS**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /clp:ErrorsOnly /v:minimal
.\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10 -MinNonBlackRatio 0.01
```

Then run the FPS sampling command from the "Shared FPS Sampling Command" section for D3D12 and Vulkan. D3D12 should remain comparable to or faster than Vulkan on the sampled scene.

- [ ] **Step 6: Commit**

Run:

```powershell
git add engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp engine/source/runtime/function/render/interface/d3d12/d3d12_descriptor_heap.h engine/source/runtime/function/render/interface/d3d12/d3d12_descriptor_heap.cpp
git commit -m "perf: reduce d3d12 descriptor churn"
```

---

## Task 11: Integration Validation And Performance Gate

**Owner:** Coordinator. Start after all code-changing tasks finish and are merged.

**Files:**
- Modify only documentation if recording results.

- [ ] **Step 1: Confirm clean merge state**

Run:

```powershell
git status --short
git log -8 --oneline
```

Expected: no unresolved conflicts, and all worker commits present.

- [ ] **Step 2: Configure dual backend**

Run:

```powershell
cmake -S . -B build -DPICCOLO_ENABLE_VULKAN_BACKEND=ON -DPICCOLO_ENABLE_D3D12_BACKEND=ON
```

Expected: configure succeeds and D3D12 DXIL bytecode is available.

- [ ] **Step 3: Build Debug editor**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor -- /clp:ErrorsOnly /v:minimal
```

Expected: exit code `0`.

- [ ] **Step 4: Run backend smoke matrix**

Run:

```powershell
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -TimeoutSeconds 20 -RenderBackend D3D12 -ExpectedBackend D3D12 -DisallowFallback
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -TimeoutSeconds 20 -RenderBackend Auto -ExpectedBackend D3D12 -DisallowFallback
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -BuildDir build -Configuration Debug -TimeoutSeconds 20 -RenderBackend Vulkan -ExpectedBackend Vulkan
```

Expected: all pass.

- [ ] **Step 5: Run D3D12 visible smoke**

Run:

```powershell
.\scripts\tests\render_backend\smoke_d3d12_editor_visible.ps1 -BuildDir build -Configuration Debug -WarmupSeconds 10 -MinNonBlackRatio 0.01
```

Expected: non-black sampled pixel ratio is above threshold.

- [ ] **Step 6: Manual visual parity pass**

Use the same level and camera state for both backends. Check:

```text
Main camera mesh output.
GBuffer/deferred lighting.
Directional and point shadows.
Skybox.
Tone mapping, color grading, and FXAA.
Particle simulation and billboard rendering.
ImGui panels.
Debug draw axis/primitives.
Picking does not crash and selected ID remains stable enough for editor use.
Resize/recreate swapchain remains stable.
```

- [ ] **Step 7: Performance gate**

Run the FPS sampling command from the "Shared FPS Sampling Command" section for D3D12 and Vulkan. Acceptance:

```text
D3D12 average FPS is not below Vulkan by more than 5% on the sampled scene.
D3D12 does not collapse to 1 FPS.
Error-hit count is 0 for D3D12 and Vulkan logs.
No D3D12 device removed, root signature, descriptor heap, resource state, or command list lifetime errors.
```

- [ ] **Step 8: Commit documentation evidence if updated**

Run:

```powershell
git add Docs
git commit -m "docs: record d3d12 completion validation"
```

Only run this commit if documentation changed.

---

## Task 12: Final Documentation And Merge

**Owner:** Coordinator.

**Files:**
- Modify: `Docs/2026-06-07-d3d12-render-backend-current-progress.md`
- Modify if useful: `ReleaseNotes.md`

- [ ] **Step 1: Update current progress document**

Record:

```text
Final merged commit.
Which worker tasks changed code.
Which tasks produced evidence only.
Build command result.
D3D12 boot smoke result.
Auto boot smoke result.
Vulkan boot smoke result.
D3D12 visible smoke result.
Manual visual parity summary.
FPS comparison.
Remaining non-blocking caveats.
```

- [ ] **Step 2: Update release notes only if user-facing behavior changed**

If D3D12 is now the expected Windows backend replacement, add one concise bullet to `ReleaseNotes.md`. Do not duplicate the full validation report there.

- [ ] **Step 3: Run final status check**

Run:

```powershell
git status --short
```

Expected: only intended documentation files are modified.

- [ ] **Step 4: Commit docs**

Run:

```powershell
git add Docs/2026-06-07-d3d12-render-backend-current-progress.md ReleaseNotes.md
git commit -m "docs: summarize d3d12 backend completion"
```

- [ ] **Step 5: Merge to mainline**

Use the repository's current mainline branch. Prefer a normal merge commit if worker branches contain meaningful separate commits; prefer fast-forward only if the branch history is already linear.

Run:

```powershell
git status --short
git branch --show-current
```

Then merge according to the branch state and report the final commit hash.

---

## Acceptance Criteria

- Windows `RenderBackend=D3D12` initializes D3D12 and does not fall back to Vulkan.
- Windows `RenderBackend=Auto` initializes D3D12.
- Windows `RenderBackend=Vulkan` still initializes Vulkan in a dual-backend build.
- D3D12 renders visible non-black editor output.
- D3D12 visual output matches Vulkan in main camera, shadows, particles, post-processing, UI, and debug draw within practical engine tolerance.
- D3D12 frame rate is comparable to Vulkan and does not regress to 1 FPS.
- D3D12 logs are clean of device removal, root signature, descriptor heap, resource state, command list lifetime, and fallback errors.
- No new unit tests, CTest targets, or engine test code were added.

## Sub Agent Dispatch Prompts

Use these prompts when launching workers.

**RHI Submit Worker**

```text
You are working in d:\program\Piccolo. Use the plan Docs/superpowers/plans/2026-06-10-d3d12-render-backend-completion.md Task 1 only. Own only engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp. Fix D3D12 queueSubmit so multiple RHISubmitInfo entries preserve wait -> execute -> signal ordering per submit. Do not modify pass or shader files. Build PiccoloEditor Debug, run D3D12 boot smoke, commit your scoped change, and return changed files plus command results.
```

**Compute Queue Worker**

```text
You are working in d:\program\Piccolo. Use Task 2 only, after Task 1 and Task 5 are merged. Own only engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h and .cpp. Add real D3D12 compute queue support if safe; otherwise make same-queue semantics explicit and stable. Do not modify particle_pass.cpp unless the coordinator hands off a required call-site change. Build, run D3D12 boot and visible smoke, commit, and report whether compute is real async or same-queue.
```

**Main Camera Worker**

```text
Use Task 3 only. Own main_camera_pass.h/.cpp and render_pass.h only if subpass order is proven wrong. Compare Vulkan and D3D12 main camera output, fix only proven pass-level mismatches, run build and backend smokes, commit if code changed, and return capture paths plus visual findings.
```

**Shadow Worker**

```text
Use Task 4 only. Own directional_light_pass.* and point_light_pass.*. Compare Vulkan and D3D12 shadows, fix pass-level shadow mismatches only, run build and D3D12 visible smoke, commit if code changed, and return visual findings.
```

**Particle Worker**

```text
Use Task 5 only. Own particle_pass.*. Preserve the D3D12 inline normal/depth copy timing that fixed the 1 FPS/device removal issue. Audit particle buffers, compute submits, barriers, and billboard parity. Build, run visible smoke, sample FPS using the shared PowerShell command in the plan, commit if code changed, and report D3D12/Vulkan FPS.
```

**Post Chain Worker**

```text
Use Task 6 only. Own tone_mapping_pass.*, color_grading_pass.*, fxaa_pass.*, and combine_ui_pass.*. Compare Vulkan and D3D12 post-process output, fix pass-level descriptor/sampler/blend/depth/fullscreen-state mismatches only, run build and visible smoke, commit if code changed, and report findings.
```

**Overlay Worker**

```text
Use Task 7 only. Own ui_pass.* and render/debugdraw files. Verify ImGui and debug draw parity on D3D12 versus Vulkan. Fix overlay-only descriptor heap or pipeline mismatches. Run build and visible smoke, commit if code changed, and report findings.
```

**Shader/Layout Worker**

```text
Use Task 8 only. Own engine/shader/**, render_shader_bytecode.h, render_resource.cpp, render_camera.cpp, engine/shader/CMakeLists.txt, and cmake/ShaderCompile.cmake. Build a binding/layout report, fix confirmed GLSL/HLSL/C++ layout mismatches, reconfigure if shader generation changes, build, smoke both backends, commit if code changed, and return the binding table.
```

**Validation Worker**

```text
Use Task 9 only. Do not add new test code or scripts. Use existing smoke scripts to gather D3D12, Vulkan, Auto, visible capture, and log-scan evidence. Commit only if you update documentation. Return command results, log paths, capture path, and any manual visual differences.
```

**Descriptor Worker**

```text
Use Task 10 only, after Task 1 is merged. Own d3d12_rhi.cpp and d3d12_descriptor_heap.*. Reduce descriptor dynamic-offset/heap churn without reintroducing repeated large buffer uploads. Remove all temporary traces before commit. Build, visible smoke, FPS compare, commit, and report before/after observations.
```
