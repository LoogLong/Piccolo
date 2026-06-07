# D3D12 Render Backend Current Status

Date: 2026-06-07

## 1. Repository Status

- Root repository: `D:\program\Piccolo`
- Active worktree: `D:\program\Piccolo\.worktrees\d3d12-render-backend`
- Branch: `feat/d3d12-render-backend`
- HEAD at time of writing: `8c02a18 feat: handle d3d12 present results`
- Main plan: `Docs\2026-06-06-d3d12-render-backend-implementation-plan.md`
- Related progress docs:
  - `Docs\2026-06-07-d3d12-render-backend-progress.md`
  - `Docs\2026-06-07-d3d12-render-backend-current-status.md`

Current code worktree changes before this status-document update were limited to the in-progress image-copy phase:

```text
ReleaseNotes.md
engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp
engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h
engine/source/runtime/function/render/interface/rhi.h
engine/source/runtime/function/render/interface/vulkan/vulkan_rhi.cpp
engine/source/runtime/function/render/interface/vulkan/vulkan_rhi.h
engine/source/runtime/function/render/passes/particle_pass.cpp
```

This status document is documentation-only. It does not represent a completed implementation phase commit.

## 2. Completed Stages And Capabilities

The branch already contains multiple committed D3D12 backend phases. Recent commits include:

```text
8c02a18 feat: handle d3d12 present results
6781912 docs: summarize d3d12 backend status
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
b4607ec test: validate d3d12 backend boot
```

Confirmed completed or substantially completed areas, mapped to the implementation plan:

- Task 1 - Backend selection:
  - `RenderBackend` / `RenderBackendAllowFallback` are wired into startup.
  - `Auto`, `Vulkan`, and `D3D12` backend selection is supported.
  - Windows platform default selects D3D12.
  - Vulkan fallback remains available when configured.

- Task 2 - Public interface cleanup:
  - Public RHI/render-resource headers have been decoupled from direct Vulkan/VMA ownership.
  - Backend-neutral allocation/resource types are present.

- Task 3 - Backend-neutral render resources:
  - Vulkan-named render resource structures were renamed or replaced with backend-neutral equivalents.
  - Render resource upload paths have been routed through RHI-facing abstractions.

- Task 4 - D3D12 wrappers and conversion:
  - D3D12 resource wrapper classes and RHI-to-D3D12 conversion helpers exist.
  - D3D12 buffer, image, image view, sampler, render pass, framebuffer, descriptor, shader, and pipeline objects are represented as backend-owned RHI resources.

- Task 5 - Device, swapchain, frame resources:
  - Real D3D12 device, command queue, swapchain, RTV/DSV resources, fences, and per-frame context have been implemented.
  - Swapchain resize callbacks and teardown ordering have been improved.
  - Present result handling now logs failures/device-removed reasons and guards swapchain recreation by valid extent.

- Task 6 - Buffers, textures, uploads, and barriers:
  - D3D12 GPU resources, upload/readback paths, sampler lifetimes, and image subresource state tracking have been progressively filled in.
  - Per-subresource image barriers are tracked instead of relying only on whole-resource assumptions.

- Task 7 - Shader bytecode:
  - D3D12 shader bytecode generation and selection are wired alongside existing Vulkan shader paths.
  - Runtime shader selection can choose D3D12 DXIL or Vulkan bytecode through backend-aware helpers.

- Task 8 - Descriptors and root signatures:
  - D3D12 descriptor heap allocator exists.
  - Descriptor layouts/sets and root signatures are implemented enough for the D3D12 render path.
  - Descriptor allocation/resource creation has been stabilized in follow-up commits.

- Task 9 - Render pass and framebuffer emulation:
  - D3D12 render pass/framebuffer metadata is represented.
  - Subpass handling and attachment state transitions have been improved.
  - Main camera, picking, debug draw, particles, and post-processing paths have been worked through enough for boot smoke coverage.

- Task 10 - Graphics and compute pipelines:
  - D3D12 graphics and compute PSO creation is implemented.
  - Pipeline binding, topology handling, and raw Vulkan pipeline-cache usage in particle code have been addressed.

- Task 11 - Command recording and submission:
  - Per-command-buffer recording, vertex/index binding, descriptor binding, queue/fence synchronization, command debug markers, submit, and present handling have been implemented.
  - Task 11 Step 4 image copy generalization is currently in progress; see the next section.

- Task 12 - ImGui:
  - DX12 ImGui backend path has been wired.
  - UI backend shutdown ordering was adjusted so ImGui backends shut down before RHI cleanup.

- Task 13 - Smoke, CI, and docs:
  - D3D12 boot smoke script and Windows CI smoke coverage exist.
  - Documentation/progress notes exist and continue to be updated.
  - Full final validation matrix is still pending.

## 3. Current In-Progress Work

Current phase: Task 11 Step 4 - generalize image-to-image copy to region/subresource.

Uncommitted implementation changes currently include:

- `engine/source/runtime/function/render/interface/rhi.h`
  - Adds a new pure virtual `cmdCopyImageToImage` overload that accepts `regionCount` and `RHIImageBlit*`.
  - Keeps the old width/height/depth overload as a compatibility wrapper that builds one mip0/layer0 region.

- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h`
  - Overrides the new region-based image-copy overload.
  - Uses `using RHI::cmdCopyImageToImage` so the old overload remains visible.

- `engine/source/runtime/function/render/interface/vulkan/vulkan_rhi.h`
  - Overrides the new region-based image-copy overload.
  - Uses `using RHI::cmdCopyImageToImage` for overload visibility.

- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
  - Replaces the old hardcoded mip0/layer0 `CopyTextureRegion` path with a region loop.
  - Adds per-layer copy handling and subresource state transitions.

- `engine/source/runtime/function/render/interface/vulkan/vulkan_rhi.cpp`
  - Converts `RHIImageBlit` regions into `VkImageCopy` entries.

- `engine/source/runtime/function/render/passes/particle_pass.cpp`
  - Updates depth/normal image copies to pass explicit mip0/layer0 whole-swapchain `RHIImageBlit` regions.

- `ReleaseNotes.md`
  - Notes the D3D12 image-to-image copy region/subresource work.

Open reviewer issue from Darwin:

- In `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`, D3D12 region bounds are still clamped against base `src->width`, `src->height`, `dst->width`, and `dst->height`.
- For non-zero mip copies, bounds must be validated against the selected mip dimensions, e.g. `max(1, base >> mipLevel)`.
- The fix should compute source and destination dimensions from `region.srcSubresource.mipLevel` and `region.dstSubresource.mipLevel`, then use those dimensions for source-box clamp, destination offset validation, and copy extent clamp.

Current phase status:

- `git diff --check` was reported as passed before the reviewer finding.
- Debug `PiccoloEditor` build was reported as passing after explicit aspect-flag casts fixed MSVC narrowing.
- The D3D12 smoke command and log scan still need to be rerun after the mip-dimension clamp fix.
- This image-copy phase has not been committed yet.

## 4. Remaining Work

Immediate next items:

1. Fix Task 11 Step 4 D3D12 mip-dimension bounds validation.
2. Rerun `git diff --check`.
3. Rebuild Debug `PiccoloEditor`.
4. Rerun the D3D12 boot smoke script.
5. Rerun D3D12 log scan for debug-layer/device-loss/fallback/error patterns.
6. Get reviewer sign-off for spec compliance and code quality.
7. Commit the completed image-copy phase locally.

Plan-level follow-up items still needing final confirmation:

- Task 11:
  - Confirm all copy paths (`cmdCopyBuffer`, `cmdCopyImageToBuffer`, `cmdCopyImageToImage`) behave correctly for subresources, mips, layers, and resource-state transitions.
  - Continue checking submit/present behavior under resize, device-loss, and invalid extent cases.

- Task 13:
  - Finalize documentation for backend selection and validation commands.
  - Confirm Windows CI smoke remains stable after the latest D3D12 command/copy changes.
  - Confirm whether README/ReleaseNotes need final consolidation before merge.

- Acceptance criteria still requiring broader validation:
  - Windows `RenderBackend=Auto` and `RenderBackend=D3D12` initialize D3D12 without Vulkan fallback.
  - Windows `RenderBackend=Vulkan` still initializes Vulkan.
  - Linux/macOS continue using Vulkan and do not compile D3D12 code outside guarded Windows sections.
  - Editor render coverage includes main camera, post-processing, debug draw, particles, picking, and ImGui on D3D12.
  - D3D12 debug layer remains clean for resource state, descriptor heap, root signature, and command list lifetime issues during boot and first rendered frame.
  - Vulkan-specific public-interface references remain limited to approved Vulkan-only areas.

## 5. Verification And Commit Status

Recently reported verification:

```powershell
git diff --check
cmake --build build --config Debug --target PiccoloEditor -- /verbosity:minimal
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -Configuration Debug -TimeoutSeconds 20
```

Reported status:

- `git diff --check`: passed before the latest open reviewer item.
- Debug `PiccoloEditor` build: passed after explicit aspect-flag casts.
- D3D12 smoke/log scan: needs rerun after the mip-dimension clamp fix.
- Current image-copy phase: uncommitted.

Recommended verification before the next phase commit:

```powershell
git diff --check
cmake --build build --config Debug --target PiccoloEditor -- /verbosity:minimal
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -Configuration Debug -TimeoutSeconds 20
rg -n "D3D12 ERROR|DEVICE_REMOVED|DXGI_ERROR|resource state|descriptor heap|root signature|command list|Falling back to Vulkan|RHI backend initialization failed|\[error\]" build\test_d3d12_boot.log build\test_d3d12_boot.stderr.log build\test_d3d12_boot.stdout.log
```

Expected log-scan result: no matches; `rg` exit code `1` is expected when no error patterns are found.

Suggested commit message after the image-copy phase is fixed and verified:

```text
feat: support d3d12 image copy regions
```

## 6. Operating Constraints

- Windows should default to D3D12 rendering.
- Do not add new test code unless explicitly requested.
- Prioritize complete D3D12 integration.
- Commit locally after each completed implementation phase.
- Use subagents as much as practical for implementation/review work.
- Clean up subagents before final response.
- Preserve unrelated worktree changes; do not revert user or other-agent edits.

## 7. Unconfirmed Assumptions

- The current image-copy implementation state is based on the visible git diff summary and reviewer notification, not a fresh full line-by-line review of every changed code block.
- `Carver` code-quality review may still have been running when this status was prepared; any later findings should supersede this document.
- The reported build/smoke status reflects the immediately preceding implementation session and should be rerun after the D3D12 mip clamp fix.
