#include "runtime/function/render/interface/d3d12/d3d12_rhi.h"
#include "runtime/function/render/interface/d3d12/d3d12_rhi_internal.h"
#include "runtime/function/render/interface/d3d12/d3d12_rhi_resource.h"

#include "runtime/core/base/macro.h"
#include "runtime/function/render/window_system.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cwchar>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <d3dcompiler.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#ifdef D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT
#define PICCOLO_D3D12_HAS_DXR 1
#else
#define PICCOLO_D3D12_HAS_DXR 0
#endif
#endif


namespace Piccolo
{
using namespace d3d12_detail;

    D3D12RHI::~D3D12RHI()
    {
        clear();
    }
    void D3D12RHI::initialize(RHIInitInfo init_info)
    {
#ifndef _WIN32
        (void)init_info;
        throw std::runtime_error("D3D12 backend is only supported on Windows");
#else
        if (!init_info.window_system)
        {
            throw std::runtime_error("Window system is null during D3D12 initialization");
        }

        m_window = init_info.window_system->getWindow();
        if (!m_window)
        {
            throw std::runtime_error("GLFW window is null during D3D12 initialization");
        }

        const std::array<int, 2> window_size = init_info.window_system->getWindowSize();
        m_window_width  = static_cast<uint32_t>(window_size[0]);
        m_window_height = static_cast<uint32_t>(window_size[1]);

        if (m_window_width == 0 || m_window_height == 0)
        {
            throw std::runtime_error("Invalid window size during D3D12 initialization");
        }

        HWND hwnd = glfwGetWin32Window(m_window);
        if (!hwnd)
        {
            throw std::runtime_error("Failed to get HWND from GLFW window");
        }

        createDevice();
        createCommandQueue();
        createCommandObjects();
        createSwapchain(hwnd);
        createRenderTargetViews();
        if (!createDescriptorHeap(m_d3d12_device.Get(),
                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                  kCbvSrvUavHeapDescriptorCount,
                                  true,
                                  m_d3d12_cbv_srv_uav_heap,
                                  m_d3d12_cbv_srv_uav_descriptor_size,
                                  m_d3d12_cbv_srv_uav_descriptor_capacity,
                                  m_d3d12_cbv_srv_uav_descriptor_next))
        {
            throw std::runtime_error("Failed to create D3D12 CBV/SRV/UAV descriptor heap");
        }
        if (!createCpuDescriptorHeap(m_d3d12_device.Get(),
                                     D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                     m_d3d12_cbv_srv_uav_descriptor_capacity,
                                     m_d3d12_cbv_srv_uav_cpu_heap))
        {
            throw std::runtime_error("Failed to create D3D12 CPU CBV/SRV/UAV descriptor heap");
        }
        m_d3d12_cbv_srv_uav_descriptor_next = 1;
        m_d3d12_transient_cbv_srv_uav_descriptor_next = m_d3d12_cbv_srv_uav_descriptor_next;
        if (!createDescriptorHeap(m_d3d12_device.Get(),
                                  D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                  kSamplerHeapDescriptorCount,
                                  true,
                                  m_d3d12_sampler_heap,
                                  m_d3d12_sampler_descriptor_size,
                                  m_d3d12_sampler_descriptor_capacity,
                                  m_d3d12_sampler_descriptor_next))
        {
            throw std::runtime_error("Failed to create D3D12 sampler descriptor heap");
        }
        if (!createCpuDescriptorHeap(m_d3d12_device.Get(),
                                     D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                     m_d3d12_sampler_descriptor_capacity,
                                     m_d3d12_sampler_cpu_heap))
        {
            throw std::runtime_error("Failed to create D3D12 CPU sampler descriptor heap");
        }
        createFence();

        createCommandPool();
        if (m_default_command_pool == nullptr)
        {
            throw std::runtime_error("Failed to create D3D12 default command pool");
        }
        m_default_descriptor_pool = new D3D12RHIDescriptorPool();
        m_graphics_queue  = new D3D12RHIQueue();
        m_compute_queue   = new D3D12RHIQueue();
#ifdef _WIN32
        auto* d3d_graphics_queue = static_cast<D3D12RHIQueue*>(m_graphics_queue);
        auto* d3d_compute_queue  = static_cast<D3D12RHIQueue*>(m_compute_queue);
        d3d_graphics_queue->command_queue     = m_d3d12_command_queue.Get();
        d3d_graphics_queue->command_list_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        d3d_compute_queue->command_queue     = m_d3d12_compute_command_queue.Get();
        d3d_compute_queue->command_list_type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        LOG_INFO("D3D12 compute queue uses dedicated COMPUTE command queue");
#endif

        for (auto& command_buffer : m_frame_command_buffers)
        {
            RHICommandBufferAllocateInfo allocate_info {};
            allocate_info.commandPool = m_default_command_pool;
            allocate_info.commandBufferCount = 1;
            if (!allocateCommandBuffers(&allocate_info, command_buffer))
            {
                throw std::runtime_error("Failed to create D3D12 frame command buffer");
            }
        }

        RHIFenceCreateInfo signaled_fence_info {};
        signaled_fence_info.flags = RHI_FENCE_CREATE_SIGNALED_BIT;
        for (auto& fence : m_frame_fences)
        {
            if (!createFence(&signaled_fence_info, fence))
            {
                throw std::runtime_error("Failed to create D3D12 frame fence");
            }
        }

        RHIFenceCreateInfo copy_fence_info {};
        copy_fence_info.flags = 0;
        for (auto& fence : m_copy_fences)
        {
            if (!createFence(&copy_fence_info, fence))
            {
                throw std::runtime_error("Failed to create D3D12 particle copy fence");
            }
        }

        createParticleCopySync();

        m_swapchain_viewport.x        = 0.0f;
        m_swapchain_viewport.y        = 0.0f;
        m_swapchain_viewport.width    = static_cast<float>(m_window_width);
        m_swapchain_viewport.height   = static_cast<float>(m_window_height);
        m_swapchain_viewport.minDepth = 0.0f;
        m_swapchain_viewport.maxDepth = 1.0f;

        m_swapchain_scissor.offset = {0, 0};
        m_swapchain_scissor.extent = {m_window_width, m_window_height};
        m_viewport                 = m_swapchain_viewport;
        m_scissor                  = m_swapchain_scissor;

        m_swapchain_desc.extent       = {m_window_width, m_window_height};
        m_swapchain_desc.image_format = RHI_FORMAT_R8G8B8A8_UNORM;
        m_swapchain_desc.viewport     = &m_viewport;
        m_swapchain_desc.scissor      = &m_scissor;
        createSwapchainImageViews();
        createFramebufferImageAndView();

        m_current_command_buffer          = m_frame_command_buffers[0];
        m_current_frame_index             = 0;
        m_current_swapchain_image_index   = 0;
#endif
    }
    void D3D12RHI::prepareContext()
    {
#ifdef _WIN32
        if (m_d3d12_swapchain != nullptr)
        {
            m_current_swapchain_image_index = m_d3d12_swapchain->GetCurrentBackBufferIndex();
        }

        m_current_command_buffer = m_frame_command_buffers[m_current_frame_index % m_frame_command_buffers.size()];
        if (m_current_command_buffer != nullptr && m_d3d12_device != nullptr)
        {
            (void)ensureCommandBufferObjects(m_current_command_buffer);
        }
#endif
        return;
    }
    void D3D12RHI::setViewport(float x, float y, float width, float height, float min_depth, float max_depth)
    {
        m_viewport = {x, y, width, height, min_depth, max_depth};
    }
    RHIViewport D3D12RHI::getViewport() const
    {
        return m_viewport;
    }
    void D3D12RHI::clear()
    {
#ifdef _WIN32
        waitForGpu();

        destroyDefaultSampler(Default_Sampler_Linear);
        destroyDefaultSampler(Default_Sampler_Nearest);
        destroyMipmappedSampler();
#endif

        delete m_default_command_pool;
        m_default_command_pool = nullptr;
        delete m_default_descriptor_pool;
        m_default_descriptor_pool = nullptr;
        delete static_cast<D3D12RHIQueue*>(m_graphics_queue);
        m_graphics_queue = nullptr;
        delete static_cast<D3D12RHIQueue*>(m_compute_queue);
        m_compute_queue = nullptr;

        for (auto& command_buffer : m_frame_command_buffers)
        {
            delete static_cast<D3D12RHICommandBuffer*>(command_buffer);
            command_buffer = nullptr;
        }
        for (auto& fence : m_frame_fences)
        {
            delete static_cast<D3D12RHIFence*>(fence);
            fence = nullptr;
        }
        for (auto& fence : m_copy_fences)
        {
            delete static_cast<D3D12RHIFence*>(fence);
            fence = nullptr;
        }
        for (auto& semaphore : m_copy_ready_semaphores)
        {
            destroySemaphore(semaphore);
            semaphore = nullptr;
        }
        for (auto& semaphore : m_copy_done_semaphores)
        {
            destroySemaphore(semaphore);
            semaphore = nullptr;
        }

        for (auto*& image_view : m_owned_swapchain_image_views)
        {
            delete image_view;
            image_view = nullptr;
        }
        m_owned_swapchain_image_views.clear();
        for (auto*& image : m_owned_swapchain_images)
        {
            delete image;
            image = nullptr;
        }
        m_owned_swapchain_images.clear();

        delete m_depth_desc.depth_image;
        m_depth_desc.depth_image = nullptr;
        delete m_depth_desc.depth_image_view;
        m_depth_desc.depth_image_view = nullptr;

        m_swapchain_desc.imageViews.clear();
        m_swapchain_desc.viewport = nullptr;
        m_swapchain_desc.scissor  = nullptr;

        destroyDevice();

        m_current_command_buffer = nullptr;
        m_bound_graphics_pipeline = nullptr;
        m_active_render_pass = nullptr;
        m_active_framebuffer = nullptr;
        m_active_subpass_index = 0;
        m_window = nullptr;
    }
    RHIBackendType D3D12RHI::getBackendType() const
    {
        return RHIBackendType::D3D12;
    }
} // namespace Piccolo
