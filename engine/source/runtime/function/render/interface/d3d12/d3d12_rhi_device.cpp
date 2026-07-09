#include "runtime/function/render/interface/d3d12/d3d12_rhi.h"
#include "runtime/function/render/interface/d3d12/d3d12_rhi_internal.h"
#include "runtime/function/render/interface/d3d12/d3d12_rhi_resource.h"

#include "runtime/core/base/macro.h"
#include "runtime/function/render/window_system.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
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

void D3D12RHI::createSwapchain()
{
    m_swapchain_desc.extent       = {m_window_width, m_window_height};
    m_swapchain_desc.image_format = RHI_FORMAT_R8G8B8A8_UNORM;
    m_swapchain_desc.viewport     = &m_viewport;
    m_swapchain_desc.scissor      = &m_scissor;
    m_swapchain_viewport.x        = 0.0f;
    m_swapchain_viewport.y        = 0.0f;
    m_swapchain_viewport.width    = static_cast<float>(m_window_width);
    m_swapchain_viewport.height   = static_cast<float>(m_window_height);
    m_swapchain_viewport.minDepth = 0.0f;
    m_swapchain_viewport.maxDepth = 1.0f;
    m_swapchain_scissor.offset    = {0, 0};
    m_swapchain_scissor.extent    = {m_window_width, m_window_height};
    m_viewport                    = m_swapchain_viewport;
    m_scissor                     = m_swapchain_scissor;
#ifdef _WIN32
    if (m_d3d12_swapchain == nullptr)
    {
        if (m_window == nullptr ||
            m_dxgi_factory == nullptr ||
            m_d3d12_device == nullptr ||
            m_d3d12_command_queue == nullptr)
        {
            LOG_ERROR("D3D12 createSwapchain requires an initialized window, DXGI factory, device, and command queue");
            return;
        }

        HWND hwnd = glfwGetWin32Window(m_window);
        if (hwnd == nullptr)
        {
            LOG_ERROR("D3D12 createSwapchain failed to get HWND from GLFW window");
            return;
        }

        createSwapchain(hwnd);

        if (m_d3d12_rtv_heap == nullptr || m_d3d12_dsv_heap == nullptr)
        {
            createRenderTargetViews();
        }
        else
        {
            D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = m_d3d12_rtv_heap->GetCPUDescriptorHandleForHeapStart();
            for (uint32_t i = 0; i < m_swapchain_buffer_count; ++i)
            {
                if (FAILED(m_d3d12_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_d3d12_render_targets[i]))))
                {
                    LOG_ERROR("D3D12 createSwapchain failed to get swapchain back buffer");
                    return;
                }
                m_d3d12_device->CreateRenderTargetView(m_d3d12_render_targets[i].Get(), nullptr, rtv_handle);
                rtv_handle.ptr += static_cast<SIZE_T>(m_d3d12_rtv_descriptor_size);
            }
            m_d3d12_rtv_descriptor_next = (std::max)(m_d3d12_rtv_descriptor_next, m_swapchain_buffer_count);
        }

        m_current_swapchain_image_index = m_d3d12_swapchain->GetCurrentBackBufferIndex();
    }
#endif
    return;
}
void D3D12RHI::recreateSwapchain()
{
#ifdef _WIN32
    waitForGpu();

    int framebuffer_width = static_cast<int>(m_window_width);
    int framebuffer_height = static_cast<int>(m_window_height);
    if (m_window != nullptr)
    {
        glfwGetFramebufferSize(m_window, &framebuffer_width, &framebuffer_height);
    }
    if (framebuffer_width <= 0 || framebuffer_height <= 0)
    {
        return;
    }

    m_window_width  = static_cast<uint32_t>(framebuffer_width);
    m_window_height = static_cast<uint32_t>(framebuffer_height);

    for (auto*& image_view : m_owned_swapchain_image_views)
    {
        delete static_cast<D3D12RHIImageView*>(image_view);
        image_view = nullptr;
    }
    m_owned_swapchain_image_views.clear();
    for (auto*& image : m_owned_swapchain_images)
    {
        delete static_cast<D3D12RHIImage*>(image);
        image = nullptr;
    }
    m_owned_swapchain_images.clear();
    m_swapchain_desc.imageViews.clear();

    for (auto& render_target : m_d3d12_render_targets)
    {
        render_target.Reset();
    }

    if (m_d3d12_swapchain != nullptr)
    {
        const HRESULT resize_result = m_d3d12_swapchain->ResizeBuffers(m_swapchain_buffer_count,
                                                                       m_window_width,
                                                                       m_window_height,
                                                                       DXGI_FORMAT_R8G8B8A8_UNORM,
                                                                       m_allow_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
        if (FAILED(resize_result))
        {
            return;
        }
    }

    if (m_d3d12_rtv_heap != nullptr && m_d3d12_device != nullptr && m_d3d12_swapchain != nullptr)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = m_d3d12_rtv_heap->GetCPUDescriptorHandleForHeapStart();
        for (uint32_t i = 0; i < m_swapchain_buffer_count; ++i)
        {
            if (FAILED(m_d3d12_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_d3d12_render_targets[i]))))
            {
                return;
            }
            m_d3d12_device->CreateRenderTargetView(m_d3d12_render_targets[i].Get(), nullptr, rtv_handle);
            rtv_handle.ptr += static_cast<SIZE_T>(m_d3d12_rtv_descriptor_size);
        }
        m_d3d12_rtv_descriptor_next = (std::max)(m_d3d12_rtv_descriptor_next, m_swapchain_buffer_count);
        m_current_swapchain_image_index = m_d3d12_swapchain->GetCurrentBackBufferIndex();
    }
#endif
    createSwapchain();
    createSwapchainImageViews();
    createFramebufferImageAndView();
    return;
}
void D3D12RHI::createSwapchainImageViews()
{
    for (auto*& image_view : m_owned_swapchain_image_views)
    {
        delete static_cast<D3D12RHIImageView*>(image_view);
        image_view = nullptr;
    }
    for (auto*& image : m_owned_swapchain_images)
    {
        delete static_cast<D3D12RHIImage*>(image);
        image = nullptr;
    }
    m_owned_swapchain_images.clear();

#ifdef _WIN32
    m_owned_swapchain_image_views.resize(m_swapchain_buffer_count);
    m_owned_swapchain_images.resize(m_swapchain_buffer_count);
    m_swapchain_desc.imageViews.clear();
    m_swapchain_desc.imageViews.reserve(m_swapchain_buffer_count);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle {};
    if (m_d3d12_rtv_heap)
    {
        rtv_handle = m_d3d12_rtv_heap->GetCPUDescriptorHandleForHeapStart();
    }
    for (uint32_t image_index = 0; image_index < m_swapchain_buffer_count; ++image_index)
    {
        auto* image                    = new D3D12RHIImage();
        image->resource                = m_d3d12_render_targets[image_index];
        image->width                   = m_window_width;
        image->height                  = m_window_height;
        image->array_layers            = 1;
        image->mip_levels              = 1;
        image->format                  = RHI_FORMAT_R8G8B8A8_UNORM;
        image->dxgi_format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        image->usage                   = RHI_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        initializeImageSubresourceStates(*image, D3D12_RESOURCE_STATE_PRESENT);
        image->source_bytes_per_pixel  = 4;
        image->resource_bytes_per_pixel = 4;

        auto* image_view                  = new D3D12RHIImageView();
        image_view->image                 = image;
        image_view->format                = RHI_FORMAT_R8G8B8A8_UNORM;
        image_view->dxgi_format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        image_view->aspect_flags          = RHI_IMAGE_ASPECT_COLOR_BIT;
        image_view->view_type             = RHI_IMAGE_VIEW_TYPE_2D;
        image_view->descriptor_heap_type  = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        image_view->cpu_descriptor        = rtv_handle;
        image_view->has_rtv               = true;
        image_view->rtv_desc.Format       = DXGI_FORMAT_R8G8B8A8_UNORM;
        image_view->rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        m_owned_swapchain_images[image_index] = image;
        m_owned_swapchain_image_views[image_index] = image_view;
        {
            char image_debug_name[64];
            char view_debug_name[64];
            snprintf(image_debug_name, sizeof(image_debug_name), "RHI.Swapchain.Image[%u]", image_index);
            snprintf(view_debug_name, sizeof(view_debug_name), "RHI.Swapchain.View[%u]", image_index);
            setDebugObjectName(image, image_debug_name);
            setDebugObjectName(image_view, view_debug_name);
        }
        m_swapchain_desc.imageViews.push_back(image_view);
        rtv_handle.ptr += static_cast<SIZE_T>(m_d3d12_rtv_descriptor_size);
    }
#else
    m_owned_swapchain_image_views.resize(3);
    m_swapchain_desc.imageViews.clear();
    m_swapchain_desc.imageViews.reserve(m_owned_swapchain_image_views.size());
    for (auto& image_view : m_owned_swapchain_image_views)
    {
        image_view = new D3D12RHIImageView();
        m_swapchain_desc.imageViews.push_back(image_view);
    }
#endif
    return;
}
void D3D12RHI::getPhysicalDeviceProperties(RHIPhysicalDeviceProperties* pProperties)
{
    if (pProperties)
    {
        std::memset(pProperties, 0, sizeof(RHIPhysicalDeviceProperties));
        pProperties->limits.maxSamplerAnisotropy              = 1.0f;
        pProperties->limits.minUniformBufferOffsetAlignment   = 256;
        pProperties->limits.minStorageBufferOffsetAlignment   = 256;
        pProperties->limits.maxStorageBufferRange             = 128 * 1024 * 1024;
        pProperties->limits.nonCoherentAtomSize               = 256;
    }
    return;
}
RHISwapChainDesc D3D12RHI::getSwapchainInfo()
{
    RHISwapChainDesc desc = m_swapchain_desc;
    desc.viewport         = &m_viewport;
    desc.scissor          = &m_scissor;
    return desc;
}
RHIDepthImageDesc D3D12RHI::getDepthImageInfo() const
{
    return m_depth_desc;
}
uint8_t D3D12RHI::getMaxFramesInFlight() const
{
    return static_cast<uint8_t>(m_swapchain_buffer_count);
}
uint8_t D3D12RHI::getCurrentFrameIndex() const
{
    return m_current_frame_index;
}
uint32_t D3D12RHI::getCurrentSwapchainImageIndex() const
{
    return m_current_swapchain_image_index;
}
void D3D12RHI::setCurrentFrameIndex(uint8_t index)
{
    m_current_frame_index = index % getMaxFramesInFlight();
}
void D3D12RHI::clearSwapchain()
{
#ifdef _WIN32
    waitForGpu();
    for (auto& render_target : m_d3d12_render_targets)
    {
        render_target.Reset();
    }
    m_d3d12_swapchain.Reset();
#endif
    for (auto*& image_view : m_owned_swapchain_image_views)
    {
        delete static_cast<D3D12RHIImageView*>(image_view);
        image_view = nullptr;
    }
    m_owned_swapchain_image_views.clear();
    for (auto*& image : m_owned_swapchain_images)
    {
        delete static_cast<D3D12RHIImage*>(image);
        image = nullptr;
    }
    m_owned_swapchain_images.clear();
    m_swapchain_desc.imageViews.clear();
    m_current_frame_index = 0;
    m_current_swapchain_image_index = 0;
    return;
}
void D3D12RHI::destroyDevice()
{
#ifdef _WIN32
    waitForGpu();

    if (m_d3d12_fence_event)
    {
        CloseHandle(m_d3d12_fence_event);
        m_d3d12_fence_event = nullptr;
    }

    m_d3d12_fence.Reset();
    for (auto& render_target : m_d3d12_render_targets)
    {
        render_target.Reset();
    }
    m_d3d12_dispatch_command_signature.Reset();
    m_d3d12_sampler_heap.Reset();
    m_d3d12_cbv_srv_uav_heap.Reset();
    m_d3d12_sampler_cpu_heap.Reset();
    m_d3d12_cbv_srv_uav_cpu_heap.Reset();
    m_d3d12_dsv_heap.Reset();
    m_d3d12_rtv_heap.Reset();
    m_d3d12_swapchain.Reset();
    m_d3d12_command_list.Reset();
    m_d3d12_command_allocator.Reset();
    m_d3d12_command_queue.Reset();
    m_d3d12_device.Reset();
    m_dxgi_factory.Reset();
    m_pending_texture_readbacks.clear();
    m_pending_upload_buffers.clear();
    m_d3d12_fence_value = 0;
    m_d3d12_rtv_descriptor_size = 0;
    m_d3d12_dsv_descriptor_size = 0;
    m_d3d12_cbv_srv_uav_descriptor_size = 0;
    m_d3d12_sampler_descriptor_size = 0;
    m_d3d12_rtv_descriptor_capacity = 0;
    m_d3d12_dsv_descriptor_capacity = 0;
    m_d3d12_cbv_srv_uav_descriptor_capacity = 0;
    m_d3d12_sampler_descriptor_capacity = 0;
    m_d3d12_rtv_descriptor_next = 0;
    m_d3d12_dsv_descriptor_next = 0;
    m_d3d12_cbv_srv_uav_descriptor_next = 0;
    m_d3d12_transient_cbv_srv_uav_descriptor_next = 0;
    m_d3d12_sampler_descriptor_next = 0;
#endif
    return;
}
    void D3D12RHI::createDevice()
    {
        UINT dxgi_factory_flags = 0;
#ifdef _DEBUG
        char  debug_layer_env[16] {};
        DWORD debug_layer_env_length =
            GetEnvironmentVariableA("PICCOLO_D3D12_DEBUG_LAYER", debug_layer_env, static_cast<DWORD>(sizeof(debug_layer_env)));
        const bool enable_debug_layer = debug_layer_env_length > 0 && debug_layer_env_length < sizeof(debug_layer_env) &&
                                        (debug_layer_env[0] == '1' || debug_layer_env[0] == 't' ||
                                         debug_layer_env[0] == 'T' || debug_layer_env[0] == 'y' ||
                                         debug_layer_env[0] == 'Y');

        ComPtr<ID3D12Debug> debug_controller;
        if (enable_debug_layer && SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller))))
        {
            debug_controller->EnableDebugLayer();
            dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
            LOG_INFO("D3D12 debug layer enabled by PICCOLO_D3D12_DEBUG_LAYER");
        }
#endif

        if (FAILED(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&m_dxgi_factory))))
        {
            throw std::runtime_error("Failed to create DXGI factory");
        }

        auto try_create_device = [this](IDXGIAdapter1* adapter, const char* source) -> bool {
            if (adapter == nullptr)
            {
                return false;
            }

            DXGI_ADAPTER_DESC1 desc {};
            if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            {
                return false;
            }

            ComPtr<ID3D12Device> device;
            if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
            {
                return false;
            }

            m_d3d12_device = device;
            LOG_INFO("D3D12 selected {} adapter: {} (dedicated_video_memory={} MB)",
                     source != nullptr ? source : "hardware",
                     dxgiAdapterDescriptionToUtf8(desc.Description),
                     static_cast<uint64_t>(desc.DedicatedVideoMemory / (1024 * 1024)));
            return true;
        };

        ComPtr<IDXGIFactory6> factory6;
        if (SUCCEEDED(m_dxgi_factory.As(&factory6)) && factory6 != nullptr)
        {
            for (UINT adapter_index = 0; !m_d3d12_device; ++adapter_index)
            {
                ComPtr<IDXGIAdapter1> high_performance_adapter;
                const HRESULT enum_result =
                    factory6->EnumAdapterByGpuPreference(adapter_index,
                                                         DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                         IID_PPV_ARGS(&high_performance_adapter));
                if (enum_result == DXGI_ERROR_NOT_FOUND)
                {
                    break;
                }
                if (FAILED(enum_result))
                {
                    continue;
                }

                try_create_device(high_performance_adapter.Get(), "high-performance");
            }
        }

        if (!m_d3d12_device)
        {
            ComPtr<IDXGIAdapter1> best_adapter;
            SIZE_T                best_dedicated_video_memory = 0;
            for (UINT adapter_index = 0;; ++adapter_index)
            {
                ComPtr<IDXGIAdapter1> hardware_adapter;
                const HRESULT         enum_result = m_dxgi_factory->EnumAdapters1(adapter_index, &hardware_adapter);
                if (enum_result == DXGI_ERROR_NOT_FOUND)
                {
                    break;
                }
                if (FAILED(enum_result) || hardware_adapter == nullptr)
                {
                    continue;
                }

                DXGI_ADAPTER_DESC1 desc {};
                if (FAILED(hardware_adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
                {
                    continue;
                }

                if (SUCCEEDED(D3D12CreateDevice(hardware_adapter.Get(),
                                                D3D_FEATURE_LEVEL_11_0,
                                                __uuidof(ID3D12Device),
                                                nullptr)) &&
                    (best_adapter == nullptr || desc.DedicatedVideoMemory > best_dedicated_video_memory))
                {
                    best_adapter                 = hardware_adapter;
                    best_dedicated_video_memory  = desc.DedicatedVideoMemory;
                }
            }

            if (best_adapter != nullptr)
            {
                try_create_device(best_adapter.Get(), "ranked hardware");
            }
        }

        if (!m_d3d12_device)
        {
            ComPtr<IDXGIAdapter> warp_adapter;
            if (SUCCEEDED(m_dxgi_factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter))) &&
                SUCCEEDED(D3D12CreateDevice(warp_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_d3d12_device))))
            {
                LOG_WARN("D3D12 hardware adapter unavailable; using WARP software adapter");
            }
        }

        if (!m_d3d12_device)
        {
            throw std::runtime_error("Failed to create D3D12 device");
        }

        BOOL allow_tearing = FALSE;
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(m_dxgi_factory.As(&factory5)) &&
            SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                    &allow_tearing,
                                                    sizeof(allow_tearing))))
        {
            m_allow_tearing = allow_tearing == TRUE;
        }
        else
        {
            m_allow_tearing = false;
        }
        LOG_INFO("D3D12 tearing present {}", m_allow_tearing ? "enabled" : "unavailable");
    }
    void D3D12RHI::createCommandQueue()
    {
        D3D12_COMMAND_QUEUE_DESC queue_desc {};
        queue_desc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        if (FAILED(m_d3d12_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&m_d3d12_command_queue))))
        {
            throw std::runtime_error("Failed to create D3D12 command queue");
        }

        D3D12_COMMAND_QUEUE_DESC compute_queue_desc {};
        compute_queue_desc.Type  = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        compute_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        if (FAILED(m_d3d12_device->CreateCommandQueue(&compute_queue_desc, IID_PPV_ARGS(&m_d3d12_compute_command_queue))))
        {
            throw std::runtime_error("Failed to create D3D12 compute command queue");
        }
    }
    void D3D12RHI::createCommandObjects()
    {
        if (FAILED(m_d3d12_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                          IID_PPV_ARGS(&m_d3d12_command_allocator))))
        {
            throw std::runtime_error("Failed to create D3D12 command allocator");
        }

        if (FAILED(m_d3d12_device->CreateCommandList(0,
                                                     D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                     m_d3d12_command_allocator.Get(),
                                                     nullptr,
                                                     IID_PPV_ARGS(&m_d3d12_command_list))))
        {
            throw std::runtime_error("Failed to create D3D12 command list");
        }

        if (FAILED(m_d3d12_command_list->Close()))
        {
            throw std::runtime_error("Failed to close initial D3D12 command list");
        }
    }
    void D3D12RHI::createSwapchain(HWND hWnd)
    {
        DXGI_SWAP_CHAIN_DESC1 swapchain_desc {};
        swapchain_desc.BufferCount = m_swapchain_buffer_count;
        swapchain_desc.Width       = m_window_width;
        swapchain_desc.Height      = m_window_height;
        swapchain_desc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchain_desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapchain_desc.SampleDesc.Count = 1;
        swapchain_desc.Flags       = m_allow_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        ComPtr<IDXGISwapChain1> swapchain;
        if (FAILED(m_dxgi_factory->CreateSwapChainForHwnd(m_d3d12_command_queue.Get(), hWnd, &swapchain_desc, nullptr, nullptr, &swapchain)))
        {
            throw std::runtime_error("Failed to create D3D12 swapchain");
        }

        if (FAILED(swapchain.As(&m_d3d12_swapchain)))
        {
            throw std::runtime_error("Failed to cast swapchain to IDXGISwapChain3");
        }
    }
    void D3D12RHI::createRenderTargetViews()
    {
        if (!createDescriptorHeap(m_d3d12_device.Get(),
                                  D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                  kRtvHeapDescriptorCount,
                                  false,
                                  m_d3d12_rtv_heap,
                                  m_d3d12_rtv_descriptor_size,
                                  m_d3d12_rtv_descriptor_capacity,
                                  m_d3d12_rtv_descriptor_next))
        {
            throw std::runtime_error("Failed to create D3D12 RTV descriptor heap");
        }
        if (!createDescriptorHeap(m_d3d12_device.Get(),
                                  D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                                  kDsvHeapDescriptorCount,
                                  false,
                                  m_d3d12_dsv_heap,
                                  m_d3d12_dsv_descriptor_size,
                                  m_d3d12_dsv_descriptor_capacity,
                                  m_d3d12_dsv_descriptor_next))
        {
            throw std::runtime_error("Failed to create D3D12 DSV descriptor heap");
        }

        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = m_d3d12_rtv_heap->GetCPUDescriptorHandleForHeapStart();
        for (uint32_t i = 0; i < m_swapchain_buffer_count; ++i)
        {
            if (FAILED(m_d3d12_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_d3d12_render_targets[i]))))
            {
                throw std::runtime_error("Failed to get D3D12 swapchain back buffer");
            }

            m_d3d12_device->CreateRenderTargetView(m_d3d12_render_targets[i].Get(), nullptr, rtv_handle);
            rtv_handle.ptr += static_cast<SIZE_T>(m_d3d12_rtv_descriptor_size);
        }
        m_d3d12_rtv_descriptor_next = m_swapchain_buffer_count;
    }
    GLFWwindow* D3D12RHI::getWindow() const
    {
        return m_window;
    }
    ID3D12Device* D3D12RHI::getD3D12Device() const
    {
        return m_d3d12_device.Get();
    }
    ID3D12CommandQueue* D3D12RHI::getD3D12GraphicsQueue() const
    {
        return m_d3d12_command_queue.Get();
    }
    ID3D12GraphicsCommandList* D3D12RHI::getD3D12CommandList() const
    {
        auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(m_current_command_buffer);
        if (d3d_command_buffer != nullptr)
        {
            markCommandBufferExternalStateDirty(*d3d_command_buffer);
        }
        if (auto* current_command_list = d3d12CommandListFor(m_current_command_buffer))
        {
            return current_command_list;
        }
        return m_d3d12_command_list.Get();
    }
    ID3D12DescriptorHeap* D3D12RHI::getD3D12ImGuiSrvHeap() const
    {
        return m_d3d12_cbv_srv_uav_heap.Get();
    }
    D3D12_CPU_DESCRIPTOR_HANDLE D3D12RHI::getD3D12ImGuiSrvCpuHandle() const
    {
        return cpuDescriptor(m_d3d12_cbv_srv_uav_heap.Get(), m_d3d12_cbv_srv_uav_descriptor_size, 0);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE D3D12RHI::getD3D12ImGuiSrvGpuHandle() const
    {
        return gpuDescriptor(m_d3d12_cbv_srv_uav_heap.Get(), m_d3d12_cbv_srv_uav_descriptor_size, 0);
    }
    DXGI_FORMAT D3D12RHI::getD3D12SwapchainFormat() const
    {
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    DXGI_FORMAT D3D12RHI::getD3D12UiRenderTargetFormat() const
    {
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    }
    bool D3D12RHI::ensureCommandBufferObjects(RHICommandBuffer* commandBuffer)
    {
        auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
        if (d3d_command_buffer == nullptr || m_d3d12_device == nullptr)
        {
            return false;
        }

        if (d3d_command_buffer->command_allocator == nullptr)
        {
            const HRESULT allocator_result =
                m_d3d12_device->CreateCommandAllocator(d3d_command_buffer->command_list_type,
                                                       IID_PPV_ARGS(&d3d_command_buffer->command_allocator));
            if (FAILED(allocator_result))
            {
                logD3D12InfoQueueMessages(m_d3d12_device.Get(), "command buffer allocator creation failure");
                LOG_ERROR("D3D12 command allocator creation failed (HRESULT=0x{:08X})",
                          static_cast<unsigned int>(allocator_result));
                return false;
            }
        }

        if (d3d_command_buffer->command_list == nullptr)
        {
            const HRESULT list_result =
                m_d3d12_device->CreateCommandList(0,
                                                  d3d_command_buffer->command_list_type,
                                                  d3d_command_buffer->command_allocator.Get(),
                                                  nullptr,
                                                  IID_PPV_ARGS(&d3d_command_buffer->command_list));
            if (FAILED(list_result))
            {
                logD3D12InfoQueueMessages(m_d3d12_device.Get(), "command buffer list creation failure");
                LOG_ERROR("D3D12 command list creation failed (HRESULT=0x{:08X})",
                          static_cast<unsigned int>(list_result));
                return false;
            }
            if (FAILED(d3d_command_buffer->command_list->Close()))
            {
                logD3D12InfoQueueMessages(m_d3d12_device.Get(), "initial command buffer list close failure");
                return false;
            }
            d3d_command_buffer->is_open = false;
        }

        return true;
    }
    ID3D12GraphicsCommandList* D3D12RHI::d3d12CommandListFor(RHICommandBuffer* commandBuffer) const
    {
        auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
        if (d3d_command_buffer == nullptr || d3d_command_buffer->command_list == nullptr)
        {
            return nullptr;
        }
        return d3d_command_buffer->command_list.Get();
    }
    bool D3D12RHI::executeImmediateCommands(const std::function<void(ID3D12GraphicsCommandList*)>& record_commands)
    {
        if (!record_commands ||
            m_d3d12_command_queue == nullptr ||
            m_d3d12_command_allocator == nullptr ||
            m_d3d12_command_list == nullptr)
        {
            return false;
        }

        if (m_command_list_open)
        {
            return false;
        }

        waitForGpu();
        if (FAILED(m_d3d12_command_allocator->Reset()))
        {
            return false;
        }
        if (FAILED(m_d3d12_command_list->Reset(m_d3d12_command_allocator.Get(), nullptr)))
        {
            return false;
        }

        record_commands(m_d3d12_command_list.Get());

        if (FAILED(m_d3d12_command_list->Close()))
        {
            m_command_list_open = false;
            return false;
        }
        m_command_list_open = false;

        ID3D12CommandList* command_lists[] = {m_d3d12_command_list.Get()};
        m_d3d12_command_queue->ExecuteCommandLists(1, command_lists);
        waitForGpu();
        return true;
    }
    bool D3D12RHI::uploadTexture2D(RHIImage* image,
                                   const void* texture_pixels,
                                   uint32_t layer_count,
                                   uint32_t source_mip_levels)
    {
        auto* d3d_image = static_cast<D3D12RHIImage*>(image);
        if (d3d_image == nullptr ||
            d3d_image->resource == nullptr ||
            texture_pixels == nullptr ||
            layer_count == 0 ||
            d3d_image->width == 0 ||
            d3d_image->height == 0 ||
            d3d_image->source_bytes_per_pixel == 0 ||
            d3d_image->resource_bytes_per_pixel == 0 ||
            m_d3d12_device == nullptr)
        {
            return false;
        }

        layer_count = (std::min)(layer_count, d3d_image->array_layers);
        const uint32_t mip_count = (std::max)(1U, d3d_image->mip_levels);
        source_mip_levels = (std::max)(1U, (std::min)(source_mip_levels, mip_count));
        const uint32_t subresource_count = layer_count * mip_count;

        D3D12_RESOURCE_DESC texture_desc = d3d_image->resource->GetDesc();
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(subresource_count);
        std::vector<UINT>                               row_counts(subresource_count);
        std::vector<UINT64>                             row_sizes(subresource_count);
        UINT64 upload_buffer_size = 0;
        m_d3d12_device->GetCopyableFootprints(&texture_desc,
                                              0,
                                              subresource_count,
                                              0,
                                              footprints.data(),
                                              row_counts.data(),
                                              row_sizes.data(),
                                              &upload_buffer_size);

        if (upload_buffer_size == 0)
        {
            return false;
        }

        D3D12_HEAP_PROPERTIES upload_heap_properties {};
        upload_heap_properties.Type                 = D3D12_HEAP_TYPE_UPLOAD;
        upload_heap_properties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        upload_heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        upload_heap_properties.CreationNodeMask     = 1;
        upload_heap_properties.VisibleNodeMask      = 1;

        D3D12_RESOURCE_DESC upload_desc {};
        upload_desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
        upload_desc.Alignment          = 0;
        upload_desc.Width              = upload_buffer_size;
        upload_desc.Height             = 1;
        upload_desc.DepthOrArraySize   = 1;
        upload_desc.MipLevels          = 1;
        upload_desc.Format             = DXGI_FORMAT_UNKNOWN;
        upload_desc.SampleDesc.Count   = 1;
        upload_desc.SampleDesc.Quality = 0;
        upload_desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        upload_desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

        ComPtr<ID3D12Resource> upload_buffer;
        if (FAILED(m_d3d12_device->CreateCommittedResource(&upload_heap_properties,
                                                           D3D12_HEAP_FLAG_NONE,
                                                           &upload_desc,
                                                           D3D12_RESOURCE_STATE_GENERIC_READ,
                                                           nullptr,
                                                           IID_PPV_ARGS(&upload_buffer))))
        {
            return false;
        }

        uint8_t* mapped_data = nullptr;
        D3D12_RANGE read_range {0, 0};
        if (FAILED(upload_buffer->Map(0, &read_range, reinterpret_cast<void**>(&mapped_data))) || mapped_data == nullptr)
        {
            return false;
        }

        const auto* source_pixels = static_cast<const uint8_t*>(texture_pixels);
        std::vector<size_t> source_mip_offsets(source_mip_levels, 0);
        size_t source_layer_size = 0;
        for (uint32_t mip = 0; mip < source_mip_levels; ++mip)
        {
            source_mip_offsets[mip] = source_layer_size;
            source_layer_size += textureMipByteSize(mipDimension(d3d_image->width, mip),
                                                    mipDimension(d3d_image->height, mip),
                                                    d3d_image->source_bytes_per_pixel);
        }

        struct UploadMipData
        {
            const uint8_t* pixels {nullptr};
            uint32_t width {0};
            uint32_t height {0};
            std::vector<uint8_t> generated_pixels;
        };
        std::vector<UploadMipData> layer_mips(static_cast<size_t>(layer_count) * mip_count);

        for (uint32_t layer = 0; layer < layer_count; ++layer)
        {
            for (uint32_t mip = 0; mip < mip_count; ++mip)
            {
                UploadMipData& mip_data = layer_mips[static_cast<size_t>(layer) * mip_count + mip];
                mip_data.width  = mipDimension(d3d_image->width, mip);
                mip_data.height = mipDimension(d3d_image->height, mip);
                if (mip < source_mip_levels)
                {
                    mip_data.pixels = source_pixels +
                                      static_cast<size_t>(layer) * source_layer_size +
                                      source_mip_offsets[mip];
                }
                else
                {
                    const UploadMipData& previous_mip =
                        layer_mips[static_cast<size_t>(layer) * mip_count + mip - 1];
                    mip_data.generated_pixels =
                        generateTextureMipLevel(previous_mip.pixels,
                                                previous_mip.width,
                                                previous_mip.height,
                                                mip_data.width,
                                                mip_data.height,
                                                d3d_image->source_bytes_per_pixel,
                                                d3d_image->format);
                    mip_data.pixels = mip_data.generated_pixels.data();
                }
            }
        }

        for (uint32_t layer = 0; layer < layer_count; ++layer)
        {
            for (uint32_t mip = 0; mip < mip_count; ++mip)
            {
                const uint32_t subresource = d3d12SubresourceIndex(*d3d_image, mip, layer);
                if (subresource >= footprints.size())
                {
                    continue;
                }

                const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint = footprints[subresource];
                const UploadMipData& mip_data = layer_mips[static_cast<size_t>(layer) * mip_count + mip];
                const size_t source_row_size =
                    static_cast<size_t>(mip_data.width) * d3d_image->source_bytes_per_pixel;
                for (UINT row = 0; row < row_counts[subresource]; ++row)
                {
                    uint8_t* dst_row =
                        mapped_data +
                        footprint.Offset +
                        static_cast<size_t>(row) * footprint.Footprint.RowPitch;
                    const uint8_t* src_row =
                        mip_data.pixels +
                        static_cast<size_t>(row) * source_row_size;
                    std::memset(dst_row, 0, footprint.Footprint.RowPitch);
                    copyTextureRowToD3D12Upload(dst_row,
                                                src_row,
                                                mip_data.width,
                                                source_row_size,
                                                static_cast<size_t>(row_sizes[subresource]),
                                                d3d_image->source_bytes_per_pixel,
                                                d3d_image->resource_bytes_per_pixel);
                }
            }
        }
        upload_buffer->Unmap(0, nullptr);

        const D3D12_RESOURCE_STATES final_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        return executeImmediateCommands(
            [&](ID3D12GraphicsCommandList* command_list)
            {
                for (uint32_t layer = 0; layer < layer_count; ++layer)
                {
                    for (uint32_t mip = 0; mip < mip_count; ++mip)
                    {
                        const uint32_t subresource = d3d12SubresourceIndex(*d3d_image, mip, layer);
                        if (subresource >= footprints.size())
                        {
                            continue;
                        }
                        transitionImageSubresource(command_list,
                                                   *d3d_image,
                                                   subresource,
                                                   D3D12_RESOURCE_STATE_COPY_DEST);

                        D3D12_TEXTURE_COPY_LOCATION dst_location {};
                        dst_location.pResource        = d3d_image->resource.Get();
                        dst_location.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                        dst_location.SubresourceIndex = subresource;

                        D3D12_TEXTURE_COPY_LOCATION src_location {};
                        src_location.pResource       = upload_buffer.Get();
                        src_location.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                        src_location.PlacedFootprint = footprints[subresource];

                        command_list->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, nullptr);

                        transitionImageSubresource(command_list,
                                                   *d3d_image,
                                                   subresource,
                                                   final_state);
                    }
                }
            });
    }
    void D3D12RHI::bindFramebufferForSubpass(RHICommandBuffer* command_buffer,
                                             ID3D12GraphicsCommandList* command_list,
                                             const RHIRenderPassBeginInfo* pRenderPassBegin,
                                             uint32_t subpass_index,
                                             bool apply_load_ops)
    {
        auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(command_buffer);
        auto* render_pass = static_cast<D3D12RHIRenderPass*>(pRenderPassBegin != nullptr ?
                                                                 pRenderPassBegin->renderPass :
                                                                 (d3d_command_buffer != nullptr ?
                                                                      d3d_command_buffer->active_render_pass :
                                                                      m_active_render_pass));
        auto* framebuffer = static_cast<D3D12RHIFramebuffer*>(pRenderPassBegin != nullptr ?
                                                                  pRenderPassBegin->framebuffer :
                                                                  (d3d_command_buffer != nullptr ?
                                                                       d3d_command_buffer->active_framebuffer :
                                                                       m_active_framebuffer));
        if (command_list == nullptr ||
            render_pass == nullptr ||
            framebuffer == nullptr ||
            subpass_index >= render_pass->subpasses.size())
        {
            return;
        }

        const D3D12RHIRenderPass::SubpassInfo& subpass = render_pass->subpasses[subpass_index];
        for (uint32_t input_index = 0; input_index < subpass.input_attachment_indices.size(); ++input_index)
        {
            const uint32_t attachment_index = subpass.input_attachment_indices[input_index];
            if (!isValidAttachmentIndex(attachment_index) ||
                attachment_index >= framebuffer->attachments.size() ||
                attachment_index >= render_pass->attachments.size())
            {
                continue;
            }

            auto* view = framebuffer->attachments[attachment_index];
            if (view == nullptr || view->image == nullptr || view->image->resource == nullptr)
            {
                continue;
            }

            transitionImageView(command_list, view, inputAttachmentState(view));
        }

        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv_handles;
        rtv_handles.reserve(subpass.color_attachment_indices.size());

        for (uint32_t color_slot = 0; color_slot < subpass.color_attachment_indices.size(); ++color_slot)
        {
            const uint32_t attachment_index = subpass.color_attachment_indices[color_slot];
            if (!isValidAttachmentIndex(attachment_index) ||
                attachment_index >= framebuffer->attachments.size() ||
                attachment_index >= render_pass->attachments.size())
            {
                continue;
            }

            auto* view = framebuffer->attachments[attachment_index];
            if (view == nullptr || !view->has_rtv || view->cpu_descriptor.ptr == 0)
            {
                continue;
            }

            if (view->image != nullptr && view->image->resource != nullptr)
            {
                transitionImageView(command_list, view, D3D12_RESOURCE_STATE_RENDER_TARGET);
            }
            else
            {
                const uint32_t back_buffer_index =
                    m_current_swapchain_image_index % m_swapchain_buffer_count;
                if (back_buffer_index < m_d3d12_render_targets.size() &&
                    m_d3d12_render_targets[back_buffer_index] != nullptr)
                {
                    D3D12_RESOURCE_BARRIER barrier {};
                    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    barrier.Transition.pResource   = m_d3d12_render_targets[back_buffer_index].Get();
                    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    command_list->ResourceBarrier(1, &barrier);
                }
            }

            rtv_handles.push_back(view->cpu_descriptor);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle {};
        const bool has_depth_attachment =
            isValidAttachmentIndex(subpass.depth_attachment_index) &&
            subpass.depth_attachment_index < framebuffer->attachments.size() &&
            subpass.depth_attachment_index < render_pass->attachments.size();
        if (has_depth_attachment)
        {
            auto* depth_view = framebuffer->attachments[subpass.depth_attachment_index];
            if (depth_view != nullptr && depth_view->has_dsv && depth_view->cpu_descriptor.ptr != 0)
            {
                const bool depth_is_input =
                    std::find(subpass.input_attachment_indices.begin(),
                              subpass.input_attachment_indices.end(),
                              subpass.depth_attachment_index) != subpass.input_attachment_indices.end();
                const bool depth_read_only = depth_is_input ||
                                             isDepthReadOnlyLayout(subpass.depth_attachment_layout);
                dsv_handle =
                    depth_read_only && depth_view->read_only_dsv_cpu_descriptor.ptr != 0 ?
                        depth_view->read_only_dsv_cpu_descriptor :
                        depth_view->cpu_descriptor;
                if (depth_view->image != nullptr && depth_view->image->resource != nullptr)
                {
                    const D3D12_RESOURCE_STATES depth_state =
                        depthAttachmentState(depth_view, subpass.depth_attachment_layout, depth_read_only);
                    transitionImageView(command_list, depth_view, depth_state);
                }
            }
        }

        command_list->OMSetRenderTargets(static_cast<UINT>(rtv_handles.size()),
                                         rtv_handles.empty() ? nullptr : rtv_handles.data(),
                                         FALSE,
                                         dsv_handle.ptr != 0 ? &dsv_handle : nullptr);

        RHIRect2D render_area = pRenderPassBegin != nullptr ? pRenderPassBegin->renderArea : RHIRect2D {};
        if (render_area.extent.width == 0 || render_area.extent.height == 0)
        {
            render_area.offset = {0, 0};
            render_area.extent = {framebuffer->width, framebuffer->height};
        }

        D3D12_VIEWPORT d3d_viewport {};
        d3d_viewport.TopLeftX = static_cast<float>(render_area.offset.x);
        d3d_viewport.TopLeftY = static_cast<float>(render_area.offset.y);
        d3d_viewport.Width    = static_cast<float>(render_area.extent.width);
        d3d_viewport.Height   = static_cast<float>(render_area.extent.height);
        d3d_viewport.MinDepth = 0.0f;
        d3d_viewport.MaxDepth = 1.0f;

        D3D12_RECT d3d_scissor {};
        d3d_scissor.left   = render_area.offset.x;
        d3d_scissor.top    = render_area.offset.y;
        d3d_scissor.right  = render_area.offset.x + static_cast<LONG>(render_area.extent.width);
        d3d_scissor.bottom = render_area.offset.y + static_cast<LONG>(render_area.extent.height);

        command_list->RSSetViewports(1, &d3d_viewport);
        command_list->RSSetScissorRects(1, &d3d_scissor);

        if (!apply_load_ops ||
            pRenderPassBegin == nullptr ||
            d3d_command_buffer == nullptr)
        {
            return;
        }

        for (uint32_t color_slot = 0; color_slot < subpass.color_attachment_indices.size(); ++color_slot)
        {
            const uint32_t attachment_index = subpass.color_attachment_indices[color_slot];
            if (!isValidAttachmentIndex(attachment_index) ||
                attachment_index >= render_pass->attachments.size() ||
                attachment_index >= framebuffer->attachments.size() ||
                (attachment_index < d3d_command_buffer->attachment_load_ops_applied.size() &&
                 d3d_command_buffer->attachment_load_ops_applied[attachment_index]))
            {
                continue;
            }

            if (attachment_index < d3d_command_buffer->attachment_load_ops_applied.size())
            {
                d3d_command_buffer->attachment_load_ops_applied[attachment_index] = true;
            }

            if (attachment_index >= pRenderPassBegin->clearValueCount ||
                pRenderPassBegin->pClearValues == nullptr ||
                render_pass->attachments[attachment_index].loadOp != RHI_ATTACHMENT_LOAD_OP_CLEAR)
            {
                continue;
            }

            auto* view = framebuffer->attachments[attachment_index];
            if (view == nullptr || !view->has_rtv || view->cpu_descriptor.ptr == 0)
            {
                continue;
            }

            const auto& clear_color = pRenderPassBegin->pClearValues[attachment_index].color;
            const FLOAT color[4] = {clear_color.float32[0],
                                    clear_color.float32[1],
                                    clear_color.float32[2],
                                    clear_color.float32[3]};
            command_list->ClearRenderTargetView(view->cpu_descriptor, color, 0, nullptr);
        }

        const bool depth_load_op_already_applied =
            has_depth_attachment &&
            subpass.depth_attachment_index < d3d_command_buffer->attachment_load_ops_applied.size() &&
            d3d_command_buffer->attachment_load_ops_applied[subpass.depth_attachment_index];
        if (has_depth_attachment &&
            dsv_handle.ptr != 0 &&
            !depth_load_op_already_applied)
        {
            if (subpass.depth_attachment_index < d3d_command_buffer->attachment_load_ops_applied.size())
            {
                d3d_command_buffer->attachment_load_ops_applied[subpass.depth_attachment_index] = true;
            }
            if (subpass.depth_attachment_index >= pRenderPassBegin->clearValueCount ||
                pRenderPassBegin->pClearValues == nullptr ||
                (render_pass->attachments[subpass.depth_attachment_index].loadOp != RHI_ATTACHMENT_LOAD_OP_CLEAR &&
                 render_pass->attachments[subpass.depth_attachment_index].stencilLoadOp != RHI_ATTACHMENT_LOAD_OP_CLEAR))
            {
                return;
            }
            const auto& depth_attachment = render_pass->attachments[subpass.depth_attachment_index];
            const auto& depth_stencil = pRenderPassBegin->pClearValues[subpass.depth_attachment_index].depthStencil;
            D3D12_CLEAR_FLAGS clear_flags = static_cast<D3D12_CLEAR_FLAGS>(0);
            if (depth_attachment.loadOp == RHI_ATTACHMENT_LOAD_OP_CLEAR)
            {
                clear_flags = static_cast<D3D12_CLEAR_FLAGS>(clear_flags | D3D12_CLEAR_FLAG_DEPTH);
            }
            if (depth_attachment.stencilLoadOp == RHI_ATTACHMENT_LOAD_OP_CLEAR &&
                formatHasStencil(depth_attachment.format))
            {
                clear_flags = static_cast<D3D12_CLEAR_FLAGS>(clear_flags | D3D12_CLEAR_FLAG_STENCIL);
            }
            if (clear_flags == 0)
            {
                return;
            }
            command_list->ClearDepthStencilView(dsv_handle,
                                                clear_flags,
                                                depth_stencil.depth,
                                                static_cast<UINT8>(depth_stencil.stencil),
                                                0,
                                                nullptr);
        }
    }
    void D3D12RHI::resolvePendingTextureReadbacks()
    {
        for (auto& pending_readback : m_pending_texture_readbacks)
        {
            auto* dst = static_cast<D3D12RHIBuffer*>(pending_readback.destination_buffer);
            if (dst == nullptr || pending_readback.readback_buffer == nullptr)
            {
                continue;
            }

            const RHIDeviceSize required_size =
                pending_readback.destination_offset +
                static_cast<RHIDeviceSize>(pending_readback.destination_row_pitch) * pending_readback.row_count;
            if (dst->host_data.size() < required_size)
            {
                dst->host_data.resize(static_cast<size_t>(required_size));
            }

            uint8_t* mapped_data = nullptr;
            D3D12_RANGE read_range {
                static_cast<SIZE_T>(pending_readback.footprint.Offset),
                static_cast<SIZE_T>(pending_readback.footprint.Offset +
                                    static_cast<UINT64>(pending_readback.footprint.Footprint.RowPitch) *
                                        pending_readback.row_count)};
            if (FAILED(pending_readback.readback_buffer->Map(0, &read_range, reinterpret_cast<void**>(&mapped_data))) ||
                mapped_data == nullptr)
            {
                continue;
            }

            for (uint32_t row = 0; row < pending_readback.row_count; ++row)
            {
                const uint8_t* src_row = mapped_data + pending_readback.footprint.Offset +
                                         static_cast<size_t>(row) * pending_readback.footprint.Footprint.RowPitch;
                uint8_t* dst_row = dst->host_data.data() +
                                   static_cast<size_t>(pending_readback.destination_offset) +
                                   static_cast<size_t>(row) * pending_readback.destination_row_pitch;
                std::memcpy(dst_row,
                            src_row,
                            (std::min)(pending_readback.row_size, pending_readback.destination_row_pitch));
            }

            D3D12_RANGE written_range {0, 0};
            pending_readback.readback_buffer->Unmap(0, &written_range);
            dst->map_host_data = true;
            dst->host_data_valid = true;
            dst->host_data_uploadable = false;
        }

        m_pending_texture_readbacks.clear();
    }
    bool D3D12RHI::ensureDispatchCommandSignature()
    {
        if (m_d3d12_dispatch_command_signature != nullptr)
        {
            return true;
        }
        if (m_d3d12_device == nullptr)
        {
            return false;
        }

        D3D12_INDIRECT_ARGUMENT_DESC argument_desc {};
        argument_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

        D3D12_COMMAND_SIGNATURE_DESC signature_desc {};
        signature_desc.ByteStride       = sizeof(D3D12_DISPATCH_ARGUMENTS);
        signature_desc.NumArgumentDescs = 1;
        signature_desc.pArgumentDescs   = &argument_desc;
        signature_desc.NodeMask         = 0;

        return SUCCEEDED(m_d3d12_device->CreateCommandSignature(&signature_desc,
                                                                nullptr,
                                                                IID_PPV_ARGS(&m_d3d12_dispatch_command_signature)));
    }
    void D3D12RHI::waitForGpu()
    {
        if (!m_d3d12_command_queue || !m_d3d12_fence || !m_d3d12_fence_event)
        {
            return;
        }

        const uint64_t fence_value = m_d3d12_fence_value;
        const HRESULT signal_result = m_d3d12_command_queue->Signal(m_d3d12_fence.Get(), fence_value);
        if (FAILED(signal_result))
        {
            const HRESULT removed_reason = m_d3d12_device != nullptr ? m_d3d12_device->GetDeviceRemovedReason() : S_OK;
            logD3D12InfoQueueMessages(m_d3d12_device.Get(), "waitForGpu signal failure");
            LOG_ERROR("D3D12 queue Signal failed (HRESULT=0x{:08X}, removed_reason=0x{:08X})",
                      static_cast<unsigned int>(signal_result),
                      static_cast<unsigned int>(removed_reason));
            return;
        }

        ++m_d3d12_fence_value;

        if (m_d3d12_fence->GetCompletedValue() < fence_value)
        {
            const HRESULT event_result = m_d3d12_fence->SetEventOnCompletion(fence_value, m_d3d12_fence_event);
            if (FAILED(event_result))
            {
                const HRESULT removed_reason = m_d3d12_device != nullptr ? m_d3d12_device->GetDeviceRemovedReason() : S_OK;
                logD3D12InfoQueueMessages(m_d3d12_device.Get(), "waitForGpu event failure");
                LOG_ERROR("D3D12 fence SetEventOnCompletion failed (HRESULT=0x{:08X}, removed_reason=0x{:08X})",
                          static_cast<unsigned int>(event_result),
                          static_cast<unsigned int>(removed_reason));
                return;
            }
            WaitForSingleObject(m_d3d12_fence_event, INFINITE);
        }

        resolvePendingTextureReadbacks();
        m_pending_upload_buffers.clear();
    }
} // namespace Piccolo
