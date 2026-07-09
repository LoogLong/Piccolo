# RHI Resource Lifecycle

This document describes ownership, creation/destruction pairing, and debug naming contracts for Piccolo RHI resources (Vulkan / D3D12).

## RHIObject

All RHI wrapper types (`RHIBuffer`, `RHIImage`, `RHIPipeline`, …) inherit `RHIObject`, which provides:

- **Virtual destructor** — `delete` through a base pointer (`RHIImage*`, etc.) runs the derived destructor and releases backend handles (`ComPtr`, `Vk*`).
- **Debug name storage** — `setDebugName` / `getDebugName` for logging and leak tracking.

Backend implementations are `final` subclasses of the interface types (e.g. `D3D12RHIBuffer final : RHIBuffer`).

## Owned Destroy Contract

Every `create*` / `allocate*` that returns an owned wrapper must be paired with the matching `destroy*` / `free*`:

1. **Stop using the resource on the GPU** — finish or wait for in-flight work (`waitForGpuIdle`, fence, frame boundary).
2. **Call the typed `destroy*`** — releases the GPU object and deletes the wrapper.
3. **Clear the caller's pointer** — use reference-output `destroy*` APIs where available (`RHIBuffer*&`, `RHIPipeline*&`, …) or assign `nullptr` manually after destroy.

**Never** `delete` an RHI wrapper directly from application code. Always go through `RHI::destroy*`.

### Anti-patterns

- Calling `object->setDebugName(...)` without `setDebugObjectName` — wrapper name is set but the GPU object stays unnamed in debug tools.
- `delete image` through `RHIImage*` without virtual destructor chain (fixed by `RHIObject`).
- Destroying resources still referenced by pending command lists.
- Calling `freeMemory` / `freeAllocation` while buffers or images still reference that memory.

## Buffer / Memory / Allocation (independent lifetimes)

These are **orthogonal** resources:

```
Buffer A ──┐
Buffer B ──┼──► RHIDeviceMemory / RHIAllocation
Buffer C ──┘
```

| Action | Owns |
|--------|------|
| `createBuffer` | `RHIBuffer` wrapper + GPU buffer object |
| `freeMemory` / `freeAllocation` | Device memory block only |
| `destroyBuffer` | Buffer wrapper + GPU buffer only (not memory) |
| `destroyBufferWithAllocation` | Convenience pair for single-owner buffers |

**Rule:** Destroy all buffers using a memory block before freeing the memory. Multiple buffers may share one allocation (suballocation, ring buffers, etc.).

## Image / ImageView / Allocation

Same independence as buffers:

| Action | Owns |
|--------|------|
| `createImage` + `createImageView` | Image and view wrappers separately |
| `freeMemory` / `freeAllocation` | Memory only |
| `destroyImage` | Image wrapper only |
| `destroyImageView` | Image view wrapper only |
| `destroyImageWithAllocation` | **Asset convenience API** — symmetric to `createGlobalImage` / `createCubeMap` |

Use `destroyImageWithAllocation` only for textures created via `createGlobalImage` or `createCubeMap`. Do not use it for render-target images that use separate `createImage` + `freeMemory`.

## Caller Naming Contract

**Whoever calls `create*` / `allocate*` must immediately call `setDebugObjectName` on success** (same block, next statement).

```cpp
m_rhi->createImage(..., image, mem, ...);
m_rhi->setDebugObjectName(image, "MainCamera.GBuffer.Color0");

m_rhi->createImageView(image, ..., view);
m_rhi->setDebugObjectName(view, "MainCamera.GBuffer.Color0.View");
```

RHI backends **do not** auto-name resources in `create*`. No create-time hooks or fallback names.

### Supported `setDebugObjectName` overloads

`RHIImage`, `RHIImageView`, `RHIBuffer`, `RHIPipeline`, `RHIDescriptorSet`, `RHICommandBuffer`, `RHIAccelerationStructure`.

For types without a dedicated overload (`RHIRenderPass`, `RHIFramebuffer`, …), set the wrapper name only:

```cpp
static_cast<RHIObject*>(render_pass)->setDebugName("MainCamera.RenderPass");
```

`setDebugObjectName` writes both the wrapper (`RHIObject::setDebugName`) and the GPU debug label (Vulkan `VK_EXT_debug_utils`, D3D12 `SetName`).

### Naming style

Use hierarchical dot-separated names: `Pass.Resource.Role` (e.g. `Particle.DstDepth`, `Global.IBL.Specular.View`).

## Shutdown order (Editor / Runtime)

When tearing down rendering:

```
waitForGpuIdle()
  → release high-level consumers (e.g. DebugDrawManager)
  → RenderSystem::clear() / RHI::clear()
  → destroy device
```

GPU work must finish before destroying pipelines, images, or buffers they reference.

## Frame-slot retirement (per-frame / resizeable resources)

Aligns with Render Graph **physical release** timing: destroy only after the in-flight frame slot’s GPU work completes.

```cpp
// When replacing a resource still referenced by in-flight GPU work:
const uint8_t slot = m_rhi->getCurrentFrameIndex();
m_rhi->retireBuffer(slot, old_buffer, old_memory);
// pointers are cleared by retire*

// For images with views:
m_rhi->retireImage(slot, old_image, old_image_view, old_memory);

// For acceleration structures (path tracing BLAS/TLAS):
m_rhi->retireAccelerationStructure(slot, old_as);

// Flush happens automatically inside waitForFences() after the fence signals.
// Shutdown:
m_rhi->flushAllRetiredResources();
```

**Rules:**

- `slot` must be `getCurrentFrameIndex()` at the time the resource was last submitted to the GPU.
- Do **not** call `flushAllRetiredResources` or destroy retired resources from `submitRendering` or per-pass `dispatch_index` counters.
- Owned long-lived resources still use `destroy*` after `waitForGpuIdle()` at shutdown.

`RHI::clear()` on both backends calls `flushAllRetiredResources()` before tearing down device resources.

## Descriptor sets

```cpp
m_rhi->allocateDescriptorSets(&alloc_info, descriptor_set);
m_rhi->setDebugObjectName(descriptor_set, "Pass.Set0");

// When a set is no longer needed (pool must outlive the free call):
m_rhi->freeDescriptorSets(pool, 1, &descriptor_set);
```

Vulkan returns sets to the pool via `vkFreeDescriptorSets`. D3D12 deletes the wrapper; heap slots are not reclaimed (linear allocator). Limited pools with `enforce_limits` decrement accounting counters on free.

## Reference-output destroy APIs

`destroy*` methods take `T*&` and set the pointer to `nullptr` after destruction:

```cpp
virtual void destroyImage(RHIImage*& image) = 0;
virtual void destroyBuffer(RHIBuffer*& buffer) = 0;
virtual void destroyPipeline(RHIPipeline*& pipeline) = 0;
// ... destroyImageView, destroyFramebuffer, destroyFence, destroySemaphore,
//     destroyCommandPool, destroySampler, destroyInstance, destroyShaderModule
```

Legacy callers passing lvalue members require no signature change at the call site.
