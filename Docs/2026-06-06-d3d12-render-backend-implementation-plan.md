# D3D12 Render Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a real D3D12 rendering backend for Piccolo and make Windows use D3D12 by default while keeping Vulkan working on Windows, Linux, and macOS.

**Architecture:** Keep the existing RHI-facing render pipeline shape, but remove Vulkan-only types from public render interfaces before filling in D3D12. D3D12 will implement the same RHI contracts with backend-owned resource wrappers, descriptor heaps/root signatures, explicit resource barriers, swapchain frame resources, and an ImGui DX12 backend; Vulkan remains the fallback and non-Windows backend.

**Tech Stack:** C++17, CMake, GLFW, Vulkan SDK/glslangValidator, Windows SDK D3D12/DXGI, Dear ImGui Vulkan/DX12 backends, PowerShell smoke tests.

---

## Current Code Findings

- `engine/source/runtime/function/render/render_system.cpp` already parses `RenderBackend`, creates `VulkanRHI` or `D3D12RHI`, defaults `Auto` to `D3D12` on `_WIN32`, and falls back to Vulkan when allowed.
- `engine/configs/development/PiccoloEditor.ini` and `engine/configs/deployment/PiccoloEditor.ini` already set `RenderBackend=D3D12` and `RenderBackendAllowFallback=true`.
- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h` and `.cpp` exist, but most RHI methods allocate dummy objects or CPU-side `host_data`; real descriptor sets, GPU resources, shader modules, root signatures, render passes, framebuffers, pipeline states, vertex/index bindings, image copies, and compute dispatch binding are incomplete.
- Public render headers still pull Vulkan into non-Vulkan code: `interface/rhi.h`, `interface/rhi_struct.h`, `window_system.h`, `render_common.h`, `render_resource.h`, and `render_pass.h`.
- Render resources are Vulkan-named and Vulkan-shaped: `VulkanMesh`, `VulkanPBRMaterial`, `VmaAllocation`, `VulkanUtil`, and `static_cast<VulkanRHI*>` appear in `render_common.h`, `render_resource.h`, and `render_resource.cpp`.
- Most render passes create RHI structs but include Vulkan helper headers. `UIPass` directly calls `ImGui_ImplVulkan_*` and currently returns early for D3D12.
- The shader pipeline compiles `engine/shader/glsl/*` to SPIR-V byte arrays under `engine/shader/generated/cpp`. D3D12 needs DXIL byte arrays or a runtime compiler path.
- There is no enabled test target; `engine/CMakeLists.txt` comments out `source/test`. Existing verification is CMake generation/build plus editor boot logs.

## File Structure

Create these files:

- `engine/source/runtime/function/render/render_backend.h`: backend enum parsing, string conversion, and platform default declarations.
- `engine/source/runtime/function/render/render_backend.cpp`: backend parsing and platform default implementation.
- `engine/source/runtime/function/render/interface/rhi_allocation.h`: backend-neutral allocation wrapper base.
- `engine/source/runtime/function/render/interface/rhi_shader.h`: backend-neutral shader bytecode view and backend stage metadata.
- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi_resource.h`: D3D12 resource wrapper classes for every `RHI*` handle.
- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi_converter.h`: RHI-to-D3D12 enum conversion helpers.
- `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi_converter.cpp`: conversion implementations.
- `engine/source/runtime/function/render/interface/d3d12/d3d12_descriptor_heap.h`: CPU/GPU descriptor heap allocators.
- `engine/source/runtime/function/render/interface/d3d12/d3d12_descriptor_heap.cpp`: descriptor heap allocator implementation.
- `engine/source/runtime/function/render/interface/d3d12/d3d12_upload_context.h`: upload command queue, staging resource lifetime, and flush API.
- `engine/source/runtime/function/render/interface/d3d12/d3d12_upload_context.cpp`: upload implementation.
- `engine/source/runtime/function/render/render_gpu_resource.h`: backend-neutral mesh/material/global resource structs replacing Vulkan-named structs.
- `engine/shader/hlsl/*.hlsl`: D3D12 HLSL equivalents for the current GLSL shaders listed in Task 7.
- `engine/source/test/CMakeLists.txt`: lightweight test executable definitions.
- `engine/source/test/render_backend_config_test.cpp`: backend config tests.
- `engine/source/test/rhi_converter_test.cpp`: D3D12 converter tests compiled on Windows.
- `scripts/tests/render_backend/assert_log.ps1`: log assertion helper.
- `scripts/tests/render_backend/smoke_backend_boot.ps1`: backend boot smoke test.

Modify these files:

- `engine/CMakeLists.txt`: configure Vulkan and D3D12 toolchain variables, include tests, and keep non-Windows Vulkan behavior.
- `engine/source/runtime/CMakeLists.txt`: link D3D12/DXGI/DXGUID on Windows, expose D3D12 headers only on Windows, and keep Vulkan includes private to Vulkan users where possible.
- `engine/3rdparty/imgui.cmake`: compile both Vulkan and DX12 ImGui backends on Windows; compile Vulkan only on non-Windows.
- `engine/shader/CMakeLists.txt`: generate backend-specific shader headers.
- `cmake/ShaderCompile.cmake`: add DXIL generation on Windows.
- `cmake/GenerateShaderCPPFile.cmake`: keep current byte-array embedding and reuse it for DXIL.
- `engine/source/runtime/function/render/interface/rhi.h`: remove Vulkan/VMA includes from public API and replace allocation parameters.
- `engine/source/runtime/function/render/interface/rhi_struct.h`: remove raw `Vk*` structs from public RHI structs.
- `engine/source/runtime/function/render/window_system.h`: remove `GLFW_INCLUDE_VULKAN`.
- `engine/source/runtime/function/render/render_system.cpp`: use `render_backend.h`, keep factory creation, and centralize backend fallback logging.
- `engine/source/runtime/function/render/render_common.h`: rename Vulkan-only resource structs to backend-neutral names and use `RHIAllocation*`.
- `engine/source/runtime/function/render/render_resource.h`: use backend-neutral resource names and remove Vulkan/VMA includes.
- `engine/source/runtime/function/render/render_resource.cpp`: remove `VulkanUtil` calls and route uploads through RHI.
- `engine/source/runtime/function/render/render_mesh.h`: rename nested `VulkanMeshVertex*` structs to backend-neutral names.
- `engine/source/runtime/function/render/render_pass.h`: remove direct Vulkan include and forward declarations.
- `engine/source/runtime/function/render/passes/*.cpp` and `debugdraw/*.cpp`: remove Vulkan helper dependencies, use backend-neutral shader handles, and rely on RHI conversion.
- `engine/source/runtime/function/render/passes/ui_pass.cpp`: initialize and draw either Vulkan ImGui or DX12 ImGui based on `RHIBackendType`.
- `README.md` and `ReleaseNotes.md`: document the finished backend support and validation commands.

## Task 1: Backend Selection Extraction And Tests

**Files:**
- Create: `engine/source/runtime/function/render/render_backend.h`
- Create: `engine/source/runtime/function/render/render_backend.cpp`
- Create: `engine/source/test/CMakeLists.txt`
- Create: `engine/source/test/render_backend_config_test.cpp`
- Modify: `engine/source/runtime/function/render/render_system.cpp`
- Modify: `engine/CMakeLists.txt`

- [ ] **Step 1: Write backend config tests**

```cpp
// engine/source/test/render_backend_config_test.cpp
#include "runtime/function/render/render_backend.h"

#include <cassert>
#include <string>

using namespace Piccolo;

int main()
{
    assert(parseRenderBackend("Auto") == RHIBackendType::Auto);
    assert(parseRenderBackend("auto") == RHIBackendType::Auto);
    assert(parseRenderBackend("Vulkan") == RHIBackendType::Vulkan);
    assert(parseRenderBackend("vulkan") == RHIBackendType::Vulkan);
    assert(parseRenderBackend("D3D12") == RHIBackendType::D3D12);
    assert(parseRenderBackend("dx12") == RHIBackendType::D3D12);
    assert(parseRenderBackend("bad-value") == RHIBackendType::Auto);

    assert(std::string(renderBackendToString(RHIBackendType::Auto)) == "Auto");
    assert(std::string(renderBackendToString(RHIBackendType::Vulkan)) == "Vulkan");
    assert(std::string(renderBackendToString(RHIBackendType::D3D12)) == "D3D12");

#ifdef _WIN32
    assert(getPlatformDefaultRenderBackend() == RHIBackendType::D3D12);
#else
    assert(getPlatformDefaultRenderBackend() == RHIBackendType::Vulkan);
#endif

    return 0;
}
```

- [ ] **Step 2: Add the test target**

```cmake
# engine/source/test/CMakeLists.txt
set(TARGET_NAME PiccoloRenderBackendConfigTest)

add_executable(${TARGET_NAME}
  render_backend_config_test.cpp
  ../runtime/function/render/render_backend.cpp)

target_include_directories(${TARGET_NAME}
  PRIVATE
    ${ENGINE_ROOT_DIR}/source
    ${ENGINE_ROOT_DIR}/source/runtime)

set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Engine/Tests")

add_test(NAME PiccoloRenderBackendConfigTest COMMAND ${TARGET_NAME})
```

```cmake
# engine/CMakeLists.txt
enable_testing()
add_subdirectory(source/test)
```

- [ ] **Step 3: Run the failing test**

Run:

```powershell
cmake -S . -B build -DPICCOLO_BUILD_TESTS=ON
cmake --build build --config Debug --target PiccoloRenderBackendConfigTest
ctest --test-dir build -C Debug -R PiccoloRenderBackendConfigTest --output-on-failure
```

Expected: build fails because `render_backend.h` and `render_backend.cpp` do not exist.

- [ ] **Step 4: Implement backend helper files**

```cpp
// engine/source/runtime/function/render/render_backend.h
#pragma once

#include "runtime/function/render/interface/rhi.h"

#include <string>

namespace Piccolo
{
    RHIBackendType parseRenderBackend(const std::string& backend);
    const char*    renderBackendToString(RHIBackendType backend);
    RHIBackendType getPlatformDefaultRenderBackend();
}
```

```cpp
// engine/source/runtime/function/render/render_backend.cpp
#include "runtime/function/render/render_backend.h"

#include <algorithm>
#include <cctype>

namespace Piccolo
{
    namespace
    {
        std::string toLowerCopy(const std::string& value)
        {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return lower;
        }
    }

    RHIBackendType parseRenderBackend(const std::string& backend)
    {
        const std::string lower = toLowerCopy(backend);
        if (lower == "vulkan")
        {
            return RHIBackendType::Vulkan;
        }
        if (lower == "d3d12" || lower == "dx12")
        {
            return RHIBackendType::D3D12;
        }
        return RHIBackendType::Auto;
    }

    const char* renderBackendToString(RHIBackendType backend)
    {
        switch (backend)
        {
            case RHIBackendType::Vulkan:
                return "Vulkan";
            case RHIBackendType::D3D12:
                return "D3D12";
            case RHIBackendType::Auto:
            default:
                return "Auto";
        }
    }

    RHIBackendType getPlatformDefaultRenderBackend()
    {
#ifdef _WIN32
        return RHIBackendType::D3D12;
#else
        return RHIBackendType::Vulkan;
#endif
    }
}
```

- [ ] **Step 5: Use the helper from RenderSystem**

Replace the anonymous `parseBackendFromConfig`, `backendToString`, and `getPlatformDefaultBackend` functions in `engine/source/runtime/function/render/render_system.cpp` with calls to:

```cpp
#include "runtime/function/render/render_backend.h"

const RHIBackendType configured_backend = parseRenderBackend(config_manager->getRenderBackend());
RHIBackendType requested_backend = configured_backend;
if (requested_backend == RHIBackendType::Auto)
{
    requested_backend = getPlatformDefaultRenderBackend();
}

LOG_INFO(std::string("Initialized RHI backend: ") + renderBackendToString(backend));
```

- [ ] **Step 6: Run backend config tests**

Run:

```powershell
cmake --build build --config Debug --target PiccoloRenderBackendConfigTest
ctest --test-dir build -C Debug -R PiccoloRenderBackendConfigTest --output-on-failure
```

Expected: test passes.

- [ ] **Step 7: Commit**

```powershell
git add engine/source/runtime/function/render/render_backend.h engine/source/runtime/function/render/render_backend.cpp engine/source/test/CMakeLists.txt engine/source/test/render_backend_config_test.cpp engine/source/runtime/function/render/render_system.cpp engine/CMakeLists.txt
git commit -m "test: cover render backend selection"
```

## Task 2: Remove Vulkan Types From Public RHI Headers

**Files:**
- Create: `engine/source/runtime/function/render/interface/rhi_allocation.h`
- Modify: `engine/source/runtime/function/render/interface/rhi.h`
- Modify: `engine/source/runtime/function/render/interface/rhi_struct.h`
- Modify: `engine/source/runtime/function/render/window_system.h`
- Modify: `engine/source/runtime/function/render/render_common.h`
- Modify: `engine/source/runtime/function/render/render_resource.h`
- Modify: `engine/source/runtime/function/render/render_pass.h`
- Modify: `engine/source/runtime/function/render/interface/vulkan/vulkan_rhi.h`
- Modify: `engine/source/runtime/function/render/interface/vulkan/vulkan_rhi_resource.h`

- [ ] **Step 1: Add backend-neutral allocation type**

```cpp
// engine/source/runtime/function/render/interface/rhi_allocation.h
#pragma once

namespace Piccolo
{
    class RHIAllocation
    {
    public:
        virtual ~RHIAllocation() = default;
    };
}
```

- [ ] **Step 2: Change RHI allocation APIs**

In `engine/source/runtime/function/render/interface/rhi.h`, remove:

```cpp
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>
```

Add:

```cpp
#include "runtime/function/render/interface/rhi_allocation.h"

#include <array>
#include <memory>
#include <vector>
#include <functional>
```

Replace allocation signatures with backend-neutral versions:

```cpp
virtual bool createBufferWithAllocation(
    const RHIBufferCreateInfo* pBufferCreateInfo,
    RHIMemoryPropertyFlags memoryPropertyFlags,
    RHIBuffer*& pBuffer,
    RHIAllocation*& pAllocation) = 0;

virtual bool createBufferWithAlignment(
    const RHIBufferCreateInfo* pBufferCreateInfo,
    RHIMemoryPropertyFlags memoryPropertyFlags,
    RHIDeviceSize minAlignment,
    RHIBuffer*& pBuffer,
    RHIAllocation*& pAllocation) = 0;

virtual void createGlobalImage(
    RHIImage*& image,
    RHIImageView*& image_view,
    RHIAllocation*& image_allocation,
    uint32_t texture_image_width,
    uint32_t texture_image_height,
    void* texture_image_pixels,
    RHIFormat texture_image_format,
    uint32_t miplevels = 0) = 0;

virtual void createCubeMap(
    RHIImage*& image,
    RHIImageView*& image_view,
    RHIAllocation*& image_allocation,
    uint32_t texture_image_width,
    uint32_t texture_image_height,
    std::array<void*, 6> texture_image_pixels,
    RHIFormat texture_image_format,
    uint32_t miplevels) = 0;

virtual void freeAllocation(RHIAllocation*& allocation) = 0;
```

- [ ] **Step 3: Move Vulkan swapchain support details to Vulkan**

Remove this struct from `engine/source/runtime/function/render/interface/rhi_struct.h`:

```cpp
struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR        capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   presentModes;
};
```

Add the same struct in `engine/source/runtime/function/render/interface/vulkan/vulkan_rhi.h` near Vulkan private helpers.

- [ ] **Step 4: Remove GLFW Vulkan macro from the window public header**

In `engine/source/runtime/function/render/window_system.h`, replace:

```cpp
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
```

with:

```cpp
#include <GLFW/glfw3.h>
```

- [ ] **Step 5: Make Vulkan allocation wrapper**

```cpp
// engine/source/runtime/function/render/interface/vulkan/vulkan_rhi_resource.h
#include "runtime/function/render/interface/rhi_allocation.h"
#include <vk_mem_alloc.h>

class VulkanAllocation : public RHIAllocation
{
public:
    VmaAllocation allocation {nullptr};
};
```

- [ ] **Step 6: Update VulkanRHI method names and wrappers**

Rename `createBufferVMA` to `createBufferWithAllocation`, rename `createBufferWithAlignmentVMA` to `createBufferWithAlignment`, allocate `VulkanAllocation`, and store the `VmaAllocation` inside it:

```cpp
auto* allocation = new VulkanAllocation();
VkResult result = vmaCreateBuffer(
    m_assets_allocator,
    reinterpret_cast<const VkBufferCreateInfo*>(pBufferCreateInfo),
    &vma_allocation_create_info,
    &vk_buffer,
    &allocation->allocation,
    nullptr);
pAllocation = allocation;
```

- [ ] **Step 7: Run compile**

Run:

```powershell
cmake --build build --config Debug --target PiccoloRuntime
```

Expected: compile errors list every remaining `VmaAllocation` public usage. Fix each by replacing public allocation fields with `RHIAllocation*`.

- [ ] **Step 8: Commit**

```powershell
git add engine/source/runtime/function/render/interface engine/source/runtime/function/render/window_system.h engine/source/runtime/function/render/render_common.h engine/source/runtime/function/render/render_resource.h engine/source/runtime/function/render/render_pass.h
git commit -m "refactor: remove vulkan allocation types from rhi interfaces"
```

## Task 3: Introduce Backend-Neutral GPU Resource Structs

**Files:**
- Create: `engine/source/runtime/function/render/render_gpu_resource.h`
- Modify: `engine/source/runtime/function/render/render_common.h`
- Modify: `engine/source/runtime/function/render/render_resource.h`
- Modify: `engine/source/runtime/function/render/render_resource.cpp`
- Modify: `engine/source/runtime/function/render/render_mesh.h`
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp`
- Modify: `engine/source/runtime/function/render/passes/pick_pass.cpp`
- Modify: `engine/source/runtime/function/render/passes/point_light_pass.cpp`
- Modify: `engine/source/runtime/function/render/passes/directional_light_pass.cpp`

- [ ] **Step 1: Add backend-neutral resource structs**

```cpp
// engine/source/runtime/function/render/render_gpu_resource.h
#pragma once

#include "runtime/function/render/interface/rhi.h"
#include "runtime/function/render/interface/rhi_allocation.h"

namespace Piccolo
{
    struct RenderMeshGPUResource
    {
        bool enable_vertex_blending {false};
        uint32_t mesh_vertex_count {0};
        RHIBuffer* mesh_vertex_position_buffer {nullptr};
        RHIAllocation* mesh_vertex_position_buffer_allocation {nullptr};
        RHIBuffer* mesh_vertex_varying_enable_blending_buffer {nullptr};
        RHIAllocation* mesh_vertex_varying_enable_blending_buffer_allocation {nullptr};
        RHIBuffer* mesh_vertex_joint_binding_buffer {nullptr};
        RHIAllocation* mesh_vertex_joint_binding_buffer_allocation {nullptr};
        RHIDescriptorSet* mesh_vertex_blending_descriptor_set {nullptr};
        RHIBuffer* mesh_vertex_varying_buffer {nullptr};
        RHIAllocation* mesh_vertex_varying_buffer_allocation {nullptr};
        uint32_t mesh_index_count {0};
        RHIBuffer* mesh_index_buffer {nullptr};
        RHIAllocation* mesh_index_buffer_allocation {nullptr};
    };

    struct RenderPBRMaterialGPUResource
    {
        RHIImage* base_color_texture_image {nullptr};
        RHIImageView* base_color_image_view {nullptr};
        RHIAllocation* base_color_image_allocation {nullptr};
        RHIImage* metallic_roughness_texture_image {nullptr};
        RHIImageView* metallic_roughness_image_view {nullptr};
        RHIAllocation* metallic_roughness_image_allocation {nullptr};
        RHIImage* normal_texture_image {nullptr};
        RHIImageView* normal_image_view {nullptr};
        RHIAllocation* normal_image_allocation {nullptr};
        RHIImage* occlusion_texture_image {nullptr};
        RHIImageView* occlusion_image_view {nullptr};
        RHIAllocation* occlusion_image_allocation {nullptr};
        RHIImage* emissive_texture_image {nullptr};
        RHIImageView* emissive_image_view {nullptr};
        RHIAllocation* emissive_image_allocation {nullptr};
        RHIBuffer* material_uniform_buffer {nullptr};
        RHIAllocation* material_uniform_buffer_allocation {nullptr};
        RHIDescriptorSet* material_descriptor_set {nullptr};
    };
}
```

- [ ] **Step 2: Rename render structs**

In `render_common.h`, replace `VulkanMesh` with `RenderMeshGPUResource`, `VulkanPBRMaterial` with `RenderPBRMaterialGPUResource`, and rename `VulkanMeshInstance` to `RenderMeshInstance`. Keep buffer object memory layout identical.

- [ ] **Step 3: Rename mesh vertex structs**

In `render_mesh.h`, replace nested type names:

```cpp
VertexPosition
VertexVaryingEnableBlending
VertexVarying
VertexJointBinding
```

Update `getBindingDescriptions()` and `getAttributeDescriptions()` to use these names. The binding count, binding indices, formats, and offsets stay unchanged.

- [ ] **Step 4: Update RenderResource maps and getters**

In `render_resource.h`, replace:

```cpp
std::map<size_t, VulkanMesh> m_vulkan_meshes;
std::map<size_t, VulkanPBRMaterial> m_vulkan_pbr_materials;
```

with:

```cpp
std::map<size_t, RenderMeshGPUResource> m_meshes;
std::map<size_t, RenderPBRMaterialGPUResource> m_pbr_materials;
```

Rename methods:

```cpp
RenderMeshGPUResource& getEntityMesh(RenderEntity entity);
RenderPBRMaterialGPUResource& getEntityMaterial(RenderEntity entity);
RenderMeshGPUResource& getOrCreateMesh(std::shared_ptr<RHI> rhi, RenderEntity entity, RenderMeshData mesh_data);
RenderPBRMaterialGPUResource& getOrCreateMaterial(std::shared_ptr<RHI> rhi, RenderEntity entity, RenderMaterialData material_data);
```

- [ ] **Step 5: Replace VMA creation calls in RenderResource**

Change calls from:

```cpp
rhi->createBufferVMA(raw_rhi->m_assets_allocator, &bufferInfo, &allocInfo, buffer, &allocation, nullptr);
```

to:

```cpp
rhi->createBufferWithAllocation(&bufferInfo, RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer, allocation);
```

For aligned buffers:

```cpp
rhi->createBufferWithAlignment(
    &bufferInfo,
    RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    physical_device_properties.limits.minStorageBufferOffsetAlignment,
    buffer,
    allocation);
```

- [ ] **Step 6: Update pass references**

In the pass files listed for this task, change all `VulkanMesh` and `VulkanPBRMaterial` references to `RenderMeshGPUResource` and `RenderPBRMaterialGPUResource`. Do not change descriptor binding order or draw loop batching.

- [ ] **Step 7: Run compile**

Run:

```powershell
cmake --build build --config Debug --target PiccoloRuntime
```

Expected: no references to `VulkanMesh` or `VulkanPBRMaterial` outside Vulkan-specific files.

- [ ] **Step 8: Verify with search**

Run:

```powershell
rg -n "VulkanMesh|VulkanPBRMaterial|VmaAllocation" engine/source/runtime/function/render -g "!interface/vulkan/**"
```

Expected: no matches outside `interface/vulkan`.

- [ ] **Step 9: Commit**

```powershell
git add engine/source/runtime/function/render
git commit -m "refactor: use backend-neutral render resource structs"
```

## Task 4: D3D12 Resource Wrappers And Enum Conversion

**Files:**
- Create: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi_resource.h`
- Create: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi_converter.h`
- Create: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi_converter.cpp`
- Create: `engine/source/test/rhi_converter_test.cpp`
- Modify: `engine/source/test/CMakeLists.txt`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`

- [ ] **Step 1: Write converter tests**

```cpp
// engine/source/test/rhi_converter_test.cpp
#include "runtime/function/render/interface/d3d12/d3d12_rhi_converter.h"

#include <cassert>

using namespace Piccolo;

int main()
{
#ifdef _WIN32
    assert(toDXGIFormat(RHI_FORMAT_R8G8B8A8_UNORM) == DXGI_FORMAT_R8G8B8A8_UNORM);
    assert(toDXGIFormat(RHI_FORMAT_B8G8R8A8_UNORM) == DXGI_FORMAT_B8G8R8A8_UNORM);
    assert(toDXGIFormat(RHI_FORMAT_D32_SFLOAT) == DXGI_FORMAT_D32_FLOAT);
    assert(toD3D12PrimitiveTopologyType(RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) == D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    assert(toD3D12PrimitiveTopology(RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) == D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    assert(toD3D12IndexFormat(RHI_INDEX_TYPE_UINT16) == DXGI_FORMAT_R16_UINT);
    assert(toD3D12IndexFormat(RHI_INDEX_TYPE_UINT32) == DXGI_FORMAT_R32_UINT);
#endif
    return 0;
}
```

- [ ] **Step 2: Add converter test target**

```cmake
if(WIN32)
  add_executable(PiccoloRHIConverterTest
    rhi_converter_test.cpp
    ../runtime/function/render/interface/d3d12/d3d12_rhi_converter.cpp)
  target_include_directories(PiccoloRHIConverterTest PRIVATE
    ${ENGINE_ROOT_DIR}/source
    ${ENGINE_ROOT_DIR}/source/runtime)
  target_link_libraries(PiccoloRHIConverterTest PRIVATE dxgi.lib d3d12.lib)
  set_target_properties(PiccoloRHIConverterTest PROPERTIES FOLDER "Engine/Tests")
  add_test(NAME PiccoloRHIConverterTest COMMAND PiccoloRHIConverterTest)
endif()
```

- [ ] **Step 3: Implement D3D12 wrappers**

```cpp
// engine/source/runtime/function/render/interface/d3d12/d3d12_rhi_resource.h
#pragma once

#include "runtime/function/render/interface/rhi.h"
#include "runtime/function/render/interface/rhi_allocation.h"

#ifdef _WIN32
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl.h>
#endif

#include <vector>

namespace Piccolo
{
#ifdef _WIN32
    using Microsoft::WRL::ComPtr;

    class D3D12Buffer final : public RHIBuffer
    {
    public:
        ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_STATES state {D3D12_RESOURCE_STATE_COMMON};
        RHIDeviceSize size {0};
    };

    class D3D12Image final : public RHIImage
    {
    public:
        ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_STATES state {D3D12_RESOURCE_STATE_COMMON};
        RHIFormat format {RHI_FORMAT_UNDEFINED};
        uint32_t width {0};
        uint32_t height {0};
        uint32_t mip_levels {1};
        uint32_t array_layers {1};
    };

    class D3D12Allocation final : public RHIAllocation
    {
    public:
        ComPtr<ID3D12Resource> owning_resource;
    };

    class D3D12Shader final : public RHIShader
    {
    public:
        std::vector<unsigned char> bytecode;
        RHIShaderStageFlagBits stage {RHI_SHADER_STAGE_VERTEX_BIT};
    };
#endif
}
```

- [ ] **Step 4: Implement core converter functions**

```cpp
// engine/source/runtime/function/render/interface/d3d12/d3d12_rhi_converter.h
#pragma once

#include "runtime/function/render/interface/rhi.h"

#ifdef _WIN32
#include <d3d12.h>
#include <dxgi.h>

namespace Piccolo
{
    DXGI_FORMAT toDXGIFormat(RHIFormat format);
    D3D12_PRIMITIVE_TOPOLOGY_TYPE toD3D12PrimitiveTopologyType(RHIPrimitiveTopology topology);
    D3D_PRIMITIVE_TOPOLOGY toD3D12PrimitiveTopology(RHIPrimitiveTopology topology);
    DXGI_FORMAT toD3D12IndexFormat(RHIIndexType index_type);
    D3D12_CULL_MODE toD3D12CullMode(RHICullModeFlags cull_mode);
    D3D12_COMPARISON_FUNC toD3D12ComparisonFunc(RHICompareOp compare_op);
}
#endif
```

```cpp
// engine/source/runtime/function/render/interface/d3d12/d3d12_rhi_converter.cpp
#include "runtime/function/render/interface/d3d12/d3d12_rhi_converter.h"

#include <stdexcept>

#ifdef _WIN32
namespace Piccolo
{
    DXGI_FORMAT toDXGIFormat(RHIFormat format)
    {
        switch (format)
        {
            case RHI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case RHI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
            case RHI_FORMAT_R16G16B16A16_SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case RHI_FORMAT_R32G32B32_SFLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
            case RHI_FORMAT_R32G32_SFLOAT: return DXGI_FORMAT_R32G32_FLOAT;
            case RHI_FORMAT_R32_SFLOAT: return DXGI_FORMAT_R32_FLOAT;
            case RHI_FORMAT_R32_UINT: return DXGI_FORMAT_R32_UINT;
            case RHI_FORMAT_D32_SFLOAT: return DXGI_FORMAT_D32_FLOAT;
            default: return DXGI_FORMAT_UNKNOWN;
        }
    }

    D3D12_PRIMITIVE_TOPOLOGY_TYPE toD3D12PrimitiveTopologyType(RHIPrimitiveTopology topology)
    {
        switch (topology)
        {
            case RHI_PRIMITIVE_TOPOLOGY_POINT_LIST: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
            case RHI_PRIMITIVE_TOPOLOGY_LINE_LIST:
            case RHI_PRIMITIVE_TOPOLOGY_LINE_STRIP: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            case RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
            case RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            default: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
        }
    }

    D3D_PRIMITIVE_TOPOLOGY toD3D12PrimitiveTopology(RHIPrimitiveTopology topology)
    {
        switch (topology)
        {
            case RHI_PRIMITIVE_TOPOLOGY_POINT_LIST: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
            case RHI_PRIMITIVE_TOPOLOGY_LINE_LIST: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
            case RHI_PRIMITIVE_TOPOLOGY_LINE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
            case RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            case RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            default: return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        }
    }

    DXGI_FORMAT toD3D12IndexFormat(RHIIndexType index_type)
    {
        return index_type == RHI_INDEX_TYPE_UINT16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    }
}
#endif
```

- [ ] **Step 5: Run converter tests**

Run:

```powershell
cmake --build build --config Debug --target PiccoloRHIConverterTest
ctest --test-dir build -C Debug -R PiccoloRHIConverterTest --output-on-failure
```

Expected: converter test passes on Windows.

- [ ] **Step 6: Commit**

```powershell
git add engine/source/runtime/function/render/interface/d3d12 engine/source/test
git commit -m "test: cover d3d12 rhi conversions"
```

## Task 5: Real D3D12 Device, Swapchain, Frame Resources

**Files:**
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
- Modify: `engine/source/runtime/CMakeLists.txt`

- [ ] **Step 1: Define frame resources**

Add to `D3D12RHI`:

```cpp
struct FrameResource
{
    ComPtr<ID3D12CommandAllocator> command_allocator;
    ComPtr<ID3D12Resource> back_buffer;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv {};
    uint64_t fence_value {0};
};

std::array<FrameResource, k_max_frames_in_flight> m_frame_resources;
ComPtr<ID3D12GraphicsCommandList> m_command_list;
ComPtr<ID3D12CommandQueue> m_graphics_queue;
ComPtr<ID3D12Fence> m_fence;
HANDLE m_fence_event {nullptr};
uint64_t m_next_fence_value {1};
```

- [ ] **Step 2: Use per-frame command allocators**

In `prepareBeforePass`, wait for the current frame fence, reset only the current frame allocator, reset the command list with that allocator, and set `m_current_command_buffer`.

```cpp
FrameResource& frame = m_frame_resources[m_current_frame_index];
waitForFrame(frame);
frame.command_allocator->Reset();
m_command_list->Reset(frame.command_allocator.Get(), nullptr);
m_command_list_open = true;
```

- [ ] **Step 3: Implement resize-safe swapchain recreation**

In `recreateSwapchain`, wait for GPU, release old back buffers, call `IDXGISwapChain::ResizeBuffers`, recreate RTVs, recreate depth resources, refresh `m_swapchain_desc`, and invoke pass callback from `prepareBeforePass`/`submitRendering` when resize is detected.

- [ ] **Step 4: Link required Windows libraries**

In `engine/source/runtime/CMakeLists.txt`, use:

```cmake
if(WIN32)
  target_link_libraries(${TARGET_NAME} PUBLIC d3d12.lib dxgi.lib dxguid.lib)
endif()
```

- [ ] **Step 5: Build**

Run:

```powershell
cmake --build build --config Debug --target PiccoloRuntime
```

Expected: `PiccoloRuntime` links with no unresolved D3D12/DXGI symbols.

- [ ] **Step 6: Commit**

```powershell
git add engine/source/runtime/function/render/interface/d3d12 engine/source/runtime/CMakeLists.txt
git commit -m "feat: add d3d12 frame resources and swapchain lifecycle"
```

## Task 6: D3D12 Buffers, Textures, Uploads, And Barriers

**Files:**
- Create: `engine/source/runtime/function/render/interface/d3d12/d3d12_upload_context.h`
- Create: `engine/source/runtime/function/render/interface/d3d12/d3d12_upload_context.cpp`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi_resource.h`

- [ ] **Step 1: Add upload context**

```cpp
class D3D12UploadContext
{
public:
    void initialize(ID3D12Device* device, ID3D12CommandQueue* queue);
    void uploadBuffer(ID3D12Resource* dst, const void* data, uint64_t size);
    void uploadTexture2D(ID3D12Resource* dst, uint32_t width, uint32_t height, DXGI_FORMAT format, const void* pixels, uint32_t mip_levels, uint32_t array_layers);
    void flush();
    void clear();

private:
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<ID3D12CommandAllocator> m_allocator;
    ComPtr<ID3D12GraphicsCommandList> m_command_list;
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fence_event {nullptr};
    uint64_t m_fence_value {1};
    std::vector<ComPtr<ID3D12Resource>> m_staging_resources;
};
```

- [ ] **Step 2: Implement committed buffer creation**

Implement `D3D12RHI::createBufferWithAllocation`:

```cpp
D3D12_HEAP_PROPERTIES heap_props {};
heap_props.Type = (memoryPropertyFlags & RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    ? D3D12_HEAP_TYPE_UPLOAD
    : D3D12_HEAP_TYPE_DEFAULT;

D3D12_RESOURCE_DESC desc {};
desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
desc.Width = pBufferCreateInfo->size;
desc.Height = 1;
desc.DepthOrArraySize = 1;
desc.MipLevels = 1;
desc.Format = DXGI_FORMAT_UNKNOWN;
desc.SampleDesc.Count = 1;
desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

auto* buffer = new D3D12Buffer();
auto* allocation = new D3D12Allocation();
const D3D12_RESOURCE_STATES initial_state =
    heap_props.Type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST;
m_d3d12_device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc, initial_state, nullptr, IID_PPV_ARGS(&buffer->resource));
buffer->state = initial_state;
buffer->size = pBufferCreateInfo->size;
allocation->owning_resource = buffer->resource;
pBuffer = buffer;
pAllocation = allocation;
```

- [ ] **Step 3: Implement buffer initialization**

In `createBufferAndInitialize`, create an upload resource for host-visible buffers or use `D3D12UploadContext::uploadBuffer` for device-local buffers. Copy `datasize` bytes and transition device-local buffers to `D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER`, `D3D12_RESOURCE_STATE_INDEX_BUFFER`, or `D3D12_RESOURCE_STATE_GENERIC_READ` based on usage flags.

- [ ] **Step 4: Implement image creation**

`createImage` creates `D3D12Image` with:

```cpp
D3D12_RESOURCE_DESC desc {};
desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
desc.Width = image_width;
desc.Height = image_height;
desc.DepthOrArraySize = static_cast<UINT16>(array_layers);
desc.MipLevels = static_cast<UINT16>(std::max(1u, miplevels));
desc.Format = toDXGIFormat(format);
desc.SampleDesc.Count = 1;
desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
desc.Flags = (image_usage_flags & RHI_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
    ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
    : D3D12_RESOURCE_FLAG_NONE;
```

- [ ] **Step 5: Implement image upload helpers**

`createGlobalImage` and `createCubeMap` create `D3D12Image`, upload pixels through `D3D12UploadContext::uploadTexture2D`, create SRV descriptors through Task 8 descriptor heaps, and set final state to `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE`.

- [ ] **Step 6: Implement barrier translation**

`cmdPipelineBarrier` inspects `RHIImageMemoryBarrier` and updates `D3D12Image::state` with `ResourceBarrier`. Use these direct mappings:

```cpp
RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL -> D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL -> D3D12_RESOURCE_STATE_RENDER_TARGET
RHI_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL -> D3D12_RESOURCE_STATE_DEPTH_WRITE
RHI_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL -> D3D12_RESOURCE_STATE_COPY_DEST
RHI_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL -> D3D12_RESOURCE_STATE_COPY_SOURCE
RHI_IMAGE_LAYOUT_PRESENT_SRC_KHR -> D3D12_RESOURCE_STATE_PRESENT
```

- [ ] **Step 7: Build and run boot smoke**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor
```

Expected: editor still starts with D3D12 selected and no dummy CPU buffer crashes during global resource upload.

- [ ] **Step 8: Commit**

```powershell
git add engine/source/runtime/function/render/interface/d3d12
git commit -m "feat: implement d3d12 resource uploads"
```

## Task 7: Backend-Specific Shader Bytecode Generation

**Files:**
- Create: `engine/source/runtime/function/render/interface/rhi_shader.h`
- Create: `engine/shader/hlsl/axis.frag.hlsl`
- Create: `engine/shader/hlsl/axis.vert.hlsl`
- Create: `engine/shader/hlsl/color_grading.frag.hlsl`
- Create: `engine/shader/hlsl/combine_ui.frag.hlsl`
- Create: `engine/shader/hlsl/debugdraw.frag.hlsl`
- Create: `engine/shader/hlsl/debugdraw.vert.hlsl`
- Create: `engine/shader/hlsl/deferred_lighting.frag.hlsl`
- Create: `engine/shader/hlsl/deferred_lighting.vert.hlsl`
- Create: `engine/shader/hlsl/fxaa.frag.hlsl`
- Create: `engine/shader/hlsl/fxaa.vert.hlsl`
- Create: `engine/shader/hlsl/mesh.frag.hlsl`
- Create: `engine/shader/hlsl/mesh.vert.hlsl`
- Create: `engine/shader/hlsl/mesh_gbuffer.frag.hlsl`
- Create: `engine/shader/hlsl/mesh_directional_light_shadow.frag.hlsl`
- Create: `engine/shader/hlsl/mesh_directional_light_shadow.vert.hlsl`
- Create: `engine/shader/hlsl/mesh_inefficient_pick.frag.hlsl`
- Create: `engine/shader/hlsl/mesh_inefficient_pick.vert.hlsl`
- Create: `engine/shader/hlsl/mesh_point_light_shadow.frag.hlsl`
- Create: `engine/shader/hlsl/mesh_point_light_shadow.geom.hlsl`
- Create: `engine/shader/hlsl/mesh_point_light_shadow.vert.hlsl`
- Create: `engine/shader/hlsl/particle_emit.comp.hlsl`
- Create: `engine/shader/hlsl/particle_kickoff.comp.hlsl`
- Create: `engine/shader/hlsl/particle_simulate.comp.hlsl`
- Create: `engine/shader/hlsl/particlebillboard.frag.hlsl`
- Create: `engine/shader/hlsl/particlebillboard.vert.hlsl`
- Create: `engine/shader/hlsl/post_process.vert.hlsl`
- Create: `engine/shader/hlsl/skybox.frag.hlsl`
- Create: `engine/shader/hlsl/skybox.vert.hlsl`
- Create: `engine/shader/hlsl/tone_mapping.frag.hlsl`
- Modify: `engine/shader/CMakeLists.txt`
- Modify: `cmake/ShaderCompile.cmake`
- Modify: `engine/source/runtime/function/render/passes/*.cpp`
- Modify: `engine/source/runtime/function/render/debugdraw/debug_draw_pipeline.cpp`

- [ ] **Step 1: Add shader bytecode descriptor**

```cpp
// engine/source/runtime/function/render/interface/rhi_shader.h
#pragma once

#include "runtime/function/render/render_type.h"

#include <vector>

namespace Piccolo
{
    struct RHIShaderBytecode
    {
        const std::vector<unsigned char>* code {nullptr};
        RHIShaderStageFlagBits stage {RHI_SHADER_STAGE_VERTEX_BIT};
        const char* entry_point {"main"};
    };
}
```

Change `RHI::createShaderModule` to:

```cpp
virtual RHIShader* createShaderModule(const RHIShaderBytecode& shader_code) = 0;
```

- [ ] **Step 2: Generate DXIL headers on Windows**

Extend `cmake/ShaderCompile.cmake` with a `compile_hlsl_shader` function:

```cmake
function(compile_hlsl_shader SHADERS TARGET_NAME GENERATED_DIR DXC_BIN)
  set(ALL_GENERATED_DXIL_FILES "")
  set(ALL_GENERATED_CPP_FILES "")
  foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    string(REPLACE "." "_" HEADER_NAME ${SHADER_NAME})
    string(TOUPPER ${HEADER_NAME} GLOBAL_SHADER_VAR)

    if(SHADER_NAME MATCHES "\\.vert\\.hlsl$")
      set(PROFILE "vs_6_0")
    elseif(SHADER_NAME MATCHES "\\.frag\\.hlsl$")
      set(PROFILE "ps_6_0")
    elseif(SHADER_NAME MATCHES "\\.geom\\.hlsl$")
      set(PROFILE "gs_6_0")
    elseif(SHADER_NAME MATCHES "\\.comp\\.hlsl$")
      set(PROFILE "cs_6_0")
    endif()

    set(DXIL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_DIR}/dxil/${SHADER_NAME}.dxil")
    set(CPP_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_DIR}/cpp/d3d12_${HEADER_NAME}.h")

    add_custom_command(
      OUTPUT ${DXIL_FILE}
      COMMAND ${DXC_BIN} -E main -T ${PROFILE} -Fo ${DXIL_FILE} ${SHADER}
      DEPENDS ${SHADER}
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_custom_command(
      OUTPUT ${CPP_FILE}
      COMMAND ${CMAKE_COMMAND} -DPATH=${DXIL_FILE} -DHEADER="${CPP_FILE}" -DGLOBAL="D3D12_${GLOBAL_SHADER_VAR}" -P "${PICCOLO_ROOT_DIR}/cmake/GenerateShaderCPPFile.cmake"
      DEPENDS ${DXIL_FILE}
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    list(APPEND ALL_GENERATED_DXIL_FILES ${DXIL_FILE})
    list(APPEND ALL_GENERATED_CPP_FILES ${CPP_FILE})
  endforeach()

  add_custom_target(${TARGET_NAME}
    DEPENDS ${ALL_GENERATED_DXIL_FILES} ${ALL_GENERATED_CPP_FILES}
    SOURCES ${SHADERS})
endfunction()
```

- [ ] **Step 3: Configure DXC path**

In `engine/CMakeLists.txt`, add:

```cmake
if(WIN32)
  find_program(dxc_executable dxc.exe)
  if(NOT dxc_executable)
    message(FATAL_ERROR "dxc.exe is required to build the D3D12 backend shaders")
  endif()
endif()
```

In `engine/shader/CMakeLists.txt`, collect `hlsl/*.hlsl`, make `generated/dxil`, and call `compile_hlsl_shader` on Windows.

- [ ] **Step 4: Port shader resource bindings**

For each HLSL file, preserve the GLSL binding order used by the corresponding pass. Use this convention:

```hlsl
cbuffer PerFrame : register(b0, space0) { /* fields matching render_common.h */ };
StructuredBuffer<MeshInstance> MeshInstances : register(t0, space1);
Texture2D BaseColorTexture : register(t1, space2);
SamplerState LinearSampler : register(s0, space0);
```

The descriptor spaces map directly to RHI descriptor set indices; register numbers map to RHI binding numbers.

- [ ] **Step 5: Update shader module creation call sites**

Replace calls like:

```cpp
RHIShader* vert_shader_module = m_rhi->createShaderModule(MESH_VERT);
```

with:

```cpp
const RHIShaderBytecode mesh_vert_shader {
    m_rhi->getBackendType() == RHIBackendType::D3D12 ? &D3D12_MESH_VERT_HLSL : &MESH_VERT,
    RHI_SHADER_STAGE_VERTEX_BIT,
    "main"};
RHIShader* vert_shader_module = m_rhi->createShaderModule(mesh_vert_shader);
```

- [ ] **Step 6: Build shader targets**

Run:

```powershell
cmake --build build --config Debug --target PiccoloShaderCompile
```

Expected: `engine/shader/generated/cpp/d3d12_*.h` and `engine/shader/generated/dxil/*.dxil` are generated on Windows.

- [ ] **Step 7: Commit**

```powershell
git add cmake/ShaderCompile.cmake engine/CMakeLists.txt engine/shader engine/source/runtime/function/render
git commit -m "feat: generate d3d12 shader bytecode"
```

## Task 8: D3D12 Descriptor Heaps, Descriptor Sets, Root Signatures

**Files:**
- Create: `engine/source/runtime/function/render/interface/d3d12/d3d12_descriptor_heap.h`
- Create: `engine/source/runtime/function/render/interface/d3d12/d3d12_descriptor_heap.cpp`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi_resource.h`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`

- [ ] **Step 1: Add descriptor heap allocator**

```cpp
class D3D12DescriptorHeapAllocator
{
public:
    void initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count, bool shader_visible);
    uint32_t allocate();
    D3D12_CPU_DESCRIPTOR_HANDLE cpu(uint32_t index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu(uint32_t index) const;
    ID3D12DescriptorHeap* heap() const { return m_heap.Get(); }

private:
    ComPtr<ID3D12DescriptorHeap> m_heap;
    uint32_t m_descriptor_size {0};
    uint32_t m_capacity {0};
    uint32_t m_next {0};
    bool m_shader_visible {false};
};
```

- [ ] **Step 2: Represent descriptor layouts and sets**

```cpp
class D3D12DescriptorSetLayout final : public RHIDescriptorSetLayout
{
public:
    std::vector<RHIDescriptorSetLayoutBinding> bindings;
};

class D3D12DescriptorSet final : public RHIDescriptorSet
{
public:
    D3D12DescriptorSetLayout* layout {nullptr};
    uint32_t srv_uav_cbv_base {0};
    uint32_t sampler_base {0};
};

class D3D12PipelineLayout final : public RHIPipelineLayout
{
public:
    ComPtr<ID3D12RootSignature> root_signature;
    std::vector<D3D12DescriptorSetLayout*> set_layouts;
};
```

- [ ] **Step 3: Implement descriptor set layout and allocation**

`createDescriptorSetLayout` stores binding metadata. `allocateDescriptorSets` reserves ranges from CBV/SRV/UAV and sampler heaps based on descriptor types and `descriptorCount`.

- [ ] **Step 4: Implement descriptor writes**

`updateDescriptorSets` maps:

```cpp
RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER -> CreateConstantBufferView
RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER -> CreateShaderResourceView for StructuredBuffer or CreateUnorderedAccessView for UAV usage
RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER -> CreateShaderResourceView plus CreateSampler
RHI_DESCRIPTOR_TYPE_INPUT_ATTACHMENT -> CreateShaderResourceView
```

Use `RHIWriteDescriptorSet::dstSet`, `dstBinding`, and `dstArrayElement` to find the destination descriptor index.

- [ ] **Step 5: Build root signatures from pipeline layout**

`createPipelineLayout` creates one descriptor table per RHI set plus static samplers for immutable sampler bindings. Root parameter order equals set index order, so `cmdBindDescriptorSetsPFN` can call:

```cpp
m_command_list->SetGraphicsRootDescriptorTable(root_parameter_index, descriptor_set->gpu_handle);
m_command_list->SetComputeRootDescriptorTable(root_parameter_index, descriptor_set->gpu_handle);
```

- [ ] **Step 6: Build**

Run:

```powershell
cmake --build build --config Debug --target PiccoloRuntime
```

Expected: D3D12 descriptor methods compile and no dummy `new RHIDescriptorSet()` remains in `d3d12_rhi.cpp`.

- [ ] **Step 7: Commit**

```powershell
git add engine/source/runtime/function/render/interface/d3d12
git commit -m "feat: add d3d12 descriptor sets and root signatures"
```

## Task 9: D3D12 Render Pass And Framebuffer Emulation

**Files:**
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi_resource.h`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
- Modify: `engine/source/runtime/function/render/passes/main_camera_pass.cpp`
- Modify: `engine/source/runtime/function/render/passes/*_pass.cpp`

- [ ] **Step 1: Store render pass metadata**

```cpp
class D3D12RenderPass final : public RHIRenderPass
{
public:
    std::vector<RHIAttachmentDescription> attachments;
    std::vector<RHISubpassDescription> subpasses;
    std::vector<RHISubpassDependency> dependencies;
};

class D3D12Framebuffer final : public RHIFramebuffer
{
public:
    D3D12RenderPass* render_pass {nullptr};
    std::vector<RHIImageView*> attachments;
    uint32_t width {0};
    uint32_t height {0};
};
```

- [ ] **Step 2: Implement render pass and framebuffer creation**

`createRenderPass` copies RHI attachment and subpass data into `D3D12RenderPass`. `createFramebuffer` stores attachment views and dimensions in `D3D12Framebuffer`.

- [ ] **Step 3: Implement begin render pass**

`cmdBeginRenderPassPFN` resolves current subpass attachment references, sets RTV/DSV handles, transitions attachments to `RENDER_TARGET` or `DEPTH_WRITE`, clears attachments with load op `RHI_ATTACHMENT_LOAD_OP_CLEAR`, and stores current framebuffer/subpass index.

- [ ] **Step 4: Implement next subpass**

`cmdNextSubpassPFN` transitions attachments whose next usage changes, calls `OMSetRenderTargets` for the next subpass, and increments the active subpass index.

- [ ] **Step 5: Implement end render pass**

`cmdEndRenderPassPFN` transitions attachments to their final D3D12 states. Swapchain image final layout maps to `PRESENT`, shader-readable offscreen images map to `PIXEL_SHADER_RESOURCE`.

- [ ] **Step 6: Build and verify main camera pass creation**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor
```

Expected: `MainCameraPass::initialize` creates D3D12 render passes and framebuffers without returning dummy objects.

- [ ] **Step 7: Commit**

```powershell
git add engine/source/runtime/function/render/interface/d3d12 engine/source/runtime/function/render/passes
git commit -m "feat: emulate render passes on d3d12"
```

## Task 10: D3D12 Graphics And Compute Pipelines

**Files:**
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi_resource.h`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi_converter.h`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi_converter.cpp`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
- Modify: `engine/source/runtime/function/render/passes/particle_pass.cpp`

- [ ] **Step 1: Add pipeline wrappers**

```cpp
class D3D12Pipeline final : public RHIPipeline
{
public:
    ComPtr<ID3D12PipelineState> pipeline_state;
    D3D_PRIMITIVE_TOPOLOGY primitive_topology {D3D_PRIMITIVE_TOPOLOGY_UNDEFINED};
};
```

- [ ] **Step 2: Implement shader module creation**

`D3D12RHI::createShaderModule` copies DXIL bytecode and stage metadata:

```cpp
auto* shader = new D3D12Shader();
shader->bytecode = *shader_code.code;
shader->stage = shader_code.stage;
return shader;
```

- [ ] **Step 3: Implement graphics PSO creation**

Build `D3D12_GRAPHICS_PIPELINE_STATE_DESC` from `RHIGraphicsPipelineCreateInfo`:

```cpp
desc.pRootSignature = static_cast<D3D12PipelineLayout*>(pCreateInfos->layout)->root_signature.Get();
desc.VS = {vertex_shader->bytecode.data(), vertex_shader->bytecode.size()};
desc.PS = {pixel_shader->bytecode.data(), pixel_shader->bytecode.size()};
desc.InputLayout = {input_elements.data(), static_cast<UINT>(input_elements.size())};
desc.PrimitiveTopologyType = toD3D12PrimitiveTopologyType(pCreateInfos->pInputAssemblyState->topology);
desc.NumRenderTargets = color_attachment_count;
desc.DSVFormat = depth_format;
desc.SampleDesc.Count = 1;
```

- [ ] **Step 4: Implement compute PSO creation**

Build `D3D12_COMPUTE_PIPELINE_STATE_DESC` using `RHIComputePipelineCreateInfo::pStages`, `layout`, and `D3D12Shader::bytecode`.

- [ ] **Step 5: Bind pipelines and topology**

`cmdBindPipelinePFN` calls `SetPipelineState`, `SetGraphicsRootSignature` or `SetComputeRootSignature`, and `IASetPrimitiveTopology`.

- [ ] **Step 6: Remove raw Vulkan pipeline cache from ParticlePass**

Replace direct `VkPipelineCache`, `VkPipelineCacheCreateInfo`, and `vkCreatePipelineCache` in `engine/source/runtime/function/render/passes/particle_pass.cpp` with `RHI_NULL_HANDLE` or an RHI pipeline cache wrapper. Keep specialization constants as normal CPU constants embedded in the HLSL compile output for D3D12.

- [ ] **Step 7: Build**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor
```

Expected: all render pass pipeline creation methods return real `D3D12Pipeline` objects.

- [ ] **Step 8: Commit**

```powershell
git add engine/source/runtime/function/render/interface/d3d12 engine/source/runtime/function/render/passes/particle_pass.cpp
git commit -m "feat: create d3d12 graphics and compute pipelines"
```

## Task 11: D3D12 Command Recording And Submission

**Files:**
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.cpp`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi_resource.h`

- [ ] **Step 1: Implement vertex buffer binding**

`cmdBindVertexBuffersPFN` converts each `D3D12Buffer` into `D3D12_VERTEX_BUFFER_VIEW` using buffer GPU address, size, and binding stride cached in the active pipeline input layout.

- [ ] **Step 2: Implement index buffer binding**

`cmdBindIndexBufferPFN` creates:

```cpp
D3D12_INDEX_BUFFER_VIEW view {};
view.BufferLocation = d3d_buffer->resource->GetGPUVirtualAddress() + offset;
view.SizeInBytes = static_cast<UINT>(d3d_buffer->size - offset);
view.Format = toD3D12IndexFormat(indexType);
m_command_list->IASetIndexBuffer(&view);
```

- [ ] **Step 3: Implement descriptor binding**

Before draw/dispatch, set descriptor heaps:

```cpp
ID3D12DescriptorHeap* heaps[] = {m_srv_uav_cbv_heap.heap(), m_sampler_heap.heap()};
m_command_list->SetDescriptorHeaps(2, heaps);
```

`cmdBindDescriptorSetsPFN` binds each `D3D12DescriptorSet` to its root parameter.

- [ ] **Step 4: Implement copy commands**

`cmdCopyBuffer`, `cmdCopyImageToBuffer`, and `cmdCopyImageToImage` call `CopyBufferRegion`, `CopyTextureRegion`, and required state transitions. Update `D3D12Image::state` and `D3D12Buffer::state` after barriers.

- [ ] **Step 5: Implement submit and present**

`submitRendering` closes the current command list, executes it, calls `Present(1, 0)`, signals the frame fence, stores fence value in `FrameResource`, and advances `m_current_frame_index` from `IDXGISwapChain3::GetCurrentBackBufferIndex()`.

- [ ] **Step 6: Run editor for a fixed boot window**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor
Start-Process -FilePath .\build\engine\source\editor\Debug\PiccoloEditor.exe -Wait
```

Expected: editor boots with D3D12 and reaches first rendered frame without D3D12 debug layer errors.

- [ ] **Step 7: Commit**

```powershell
git add engine/source/runtime/function/render/interface/d3d12
git commit -m "feat: record and submit d3d12 render commands"
```

## Task 12: D3D12 ImGui Backend

**Files:**
- Modify: `engine/3rdparty/imgui.cmake`
- Modify: `engine/source/runtime/function/render/passes/ui_pass.cpp`
- Modify: `engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h`

- [ ] **Step 1: Compile DX12 ImGui backend on Windows**

```cmake
if(WIN32)
  list(APPEND imgui_impl
    "${imgui_SOURCE_DIR_}/backends/imgui_impl_dx12.cpp"
    "${imgui_SOURCE_DIR_}/backends/imgui_impl_dx12.h")
endif()
```

Link `imgui` with `d3d12.lib dxgi.lib` on Windows.

- [ ] **Step 2: Expose D3D12 ImGui handles**

Add getters to `D3D12RHI`:

```cpp
ID3D12Device* getD3D12Device() const;
ID3D12CommandQueue* getD3D12GraphicsQueue() const;
ID3D12DescriptorHeap* getD3D12ImGuiSrvHeap() const;
DXGI_FORMAT getD3D12SwapchainFormat() const;
```

- [ ] **Step 3: Initialize ImGui DX12**

In `UIPass::initializeUIRenderBackend`, branch on D3D12:

```cpp
ImGui_ImplGlfw_InitForOther(std::static_pointer_cast<D3D12RHI>(m_rhi)->getWindow(), true);
ImGui_ImplDX12_Init(
    d3d12_rhi->getD3D12Device(),
    d3d12_rhi->getMaxFramesInFlight(),
    d3d12_rhi->getD3D12SwapchainFormat(),
    d3d12_rhi->getD3D12ImGuiSrvHeap(),
    d3d12_rhi->getD3D12ImGuiSrvHeap()->GetCPUDescriptorHandleForHeapStart(),
    d3d12_rhi->getD3D12ImGuiSrvHeap()->GetGPUDescriptorHandleForHeapStart());
```

- [ ] **Step 4: Draw ImGui DX12**

In `UIPass::draw`, branch on D3D12:

```cpp
ImGui_ImplDX12_NewFrame();
ImGui_ImplGlfw_NewFrame();
ImGui::NewFrame();
m_window_ui->preRender();
ImGui::Render();
ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), d3d12_rhi->getD3D12CommandList());
```

- [ ] **Step 5: Build and boot**

Run:

```powershell
cmake --build build --config Debug --target PiccoloEditor
.\build\engine\source\editor\Debug\PiccoloEditor.exe
```

Expected: editor UI renders on D3D12; the previous warning about Vulkan-only UI no longer appears.

- [ ] **Step 6: Commit**

```powershell
git add engine/3rdparty/imgui.cmake engine/source/runtime/function/render/passes/ui_pass.cpp engine/source/runtime/function/render/interface/d3d12/d3d12_rhi.h
git commit -m "feat: render imgui with d3d12"
```

## Task 13: Smoke Tests, CI, And Documentation

**Files:**
- Create: `scripts/tests/render_backend/assert_log.ps1`
- Create: `scripts/tests/render_backend/smoke_backend_boot.ps1`
- Modify: `.github/workflows/build_windows.yml`
- Modify: `README.md`
- Modify: `ReleaseNotes.md`

- [ ] **Step 1: Add log assertion helper**

```powershell
# scripts/tests/render_backend/assert_log.ps1
param(
    [Parameter(Mandatory=$true)][string]$LogPath,
    [Parameter(Mandatory=$true)][string]$Pattern
)

$content = Get-Content -Raw -ErrorAction Stop $LogPath
if ($content -notmatch [regex]::Escape($Pattern)) {
    throw "Expected pattern not found: $Pattern"
}
```

- [ ] **Step 2: Add boot smoke script**

```powershell
# scripts/tests/render_backend/smoke_backend_boot.ps1
param(
    [string]$Configuration = "Debug",
    [int]$TimeoutSeconds = 20
)

$exe = ".\build\engine\source\editor\$Configuration\PiccoloEditor.exe"
$log = ".\build\test_d3d12_boot.log"

if (Test-Path $log) {
    Remove-Item -LiteralPath $log
}

$process = Start-Process -FilePath $exe -PassThru -RedirectStandardOutput $log -RedirectStandardError $log
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    $process.Kill()
    $process.WaitForExit()
}

.\scripts\tests\render_backend\assert_log.ps1 -LogPath $log -Pattern "Initialized RHI backend: D3D12"
.\scripts\tests\render_backend\assert_log.ps1 -LogPath $log -Pattern "engine start"
```

- [ ] **Step 3: Add Windows CI smoke step**

In `.github/workflows/build_windows.yml`, after build:

```yaml
      - name: D3D12 backend smoke
        if: matrix.configuration == 'debug'
        shell: pwsh
        run: ./scripts/tests/render_backend/smoke_backend_boot.ps1 -Configuration Debug
```

- [ ] **Step 4: Document backend behavior**

In `README.md`, state:

```markdown
### Render Backend Selection

`RenderBackend=Auto` selects D3D12 on Windows and Vulkan on Linux/macOS. Set `RenderBackend=Vulkan` to force Vulkan on Windows. Set `RenderBackendAllowFallback=true` to let a failed D3D12 startup retry Vulkan.
```

- [ ] **Step 5: Run full validation**

Run:

```powershell
cmake -S . -B build -DPICCOLO_BUILD_TESTS=ON
cmake --build build --config Debug --target PiccoloEditor
ctest --test-dir build -C Debug --output-on-failure
.\scripts\tests\render_backend\smoke_backend_boot.ps1 -Configuration Debug
```

Expected: tests pass, boot log contains `Initialized RHI backend: D3D12`, and no Vulkan fallback message appears in the D3D12 smoke log.

- [ ] **Step 6: Commit**

```powershell
git add scripts/tests/render_backend .github/workflows/build_windows.yml README.md ReleaseNotes.md
git commit -m "test: validate d3d12 backend boot"
```

## Acceptance Criteria

- Windows with `RenderBackend=Auto` or `RenderBackend=D3D12` initializes D3D12 without falling back to Vulkan.
- Windows with `RenderBackend=Vulkan` still initializes Vulkan.
- Linux and macOS still initialize Vulkan and do not compile D3D12 source files outside guarded Windows sections.
- The editor boots, renders the main camera pass, post-processing, debug draw, particles, picking, and ImGui on D3D12.
- D3D12 debug layer does not report resource state, descriptor heap, root signature, or command list lifetime errors during boot and first rendered frame.
- `ctest` runs backend config and converter tests.
- `scripts/tests/render_backend/smoke_backend_boot.ps1` verifies D3D12 startup by log.
- `rg -n "VulkanRHI|VulkanUtil|VmaAllocation|Vk" engine/source/runtime/function/render -g "!interface/vulkan/**"` returns only approved Vulkan-specific references or no matches.

## Risk Notes

- The largest risk is shader parity because the current source is GLSL/SPIR-V-only. Keeping HLSL files parallel to GLSL is the direct route with the least build-system risk.
- The second largest risk is emulating Vulkan subpasses on D3D12. Treat each Piccolo subpass as an explicit render-target rebind plus resource transitions.
- Particle compute currently contains raw Vulkan code. D3D12 parity requires moving its pipeline cache, specialization constants, and image copy paths fully through RHI.
- Existing dirty worktree changes include early D3D12 backend and config work. During execution, read each target file before editing and preserve user changes.

## Self Review

- Spec coverage: D3D12 backend creation, Windows default selection, Vulkan fallback, shader generation, resources, descriptors, render passes, UI, particles, tests, CI, and docs are covered by Tasks 1-13.
- Open marker scan: this plan has no open marker tokens for unspecified work.
- Type consistency: `RHIAllocation`, `RenderMeshGPUResource`, `RenderPBRMaterialGPUResource`, `D3D12Shader`, `D3D12Pipeline`, and `D3D12DescriptorSet` names are introduced before later tasks use them.
