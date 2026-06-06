#include "runtime/function/render/interface/d3d12/d3d12_rhi.h"

#include "runtime/function/render/window_system.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace Piccolo
{
    namespace
    {
#ifdef _WIN32
        struct D3D12RHIBuffer final : RHIBuffer
        {
            ComPtr<ID3D12Resource> resource;
            std::vector<uint8_t>   host_data;
        };

        struct D3D12RHIDeviceMemory final : RHIDeviceMemory
        {
            D3D12RHIBuffer* owner_buffer {nullptr};
            void*           mapped_ptr {nullptr};
        };
#else
        struct D3D12RHIBuffer final : RHIBuffer
        {
            std::vector<uint8_t> host_data;
        };

        struct D3D12RHIDeviceMemory final : RHIDeviceMemory
        {
            D3D12RHIBuffer* owner_buffer {nullptr};
            void*           mapped_ptr {nullptr};
        };
#endif

        struct D3D12RHICommandPool final : RHICommandPool
        {
        };

        struct D3D12RHIQueue final : RHIQueue
        {
        };

        struct D3D12RHIFence final : RHIFence
        {
        };

        struct D3D12RHISemaphore final : RHISemaphore
        {
        };
    } // namespace

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
        createFence();

        m_dummy_command_pool    = new D3D12RHICommandPool();
        m_dummy_descriptor_pool = new RHIDescriptorPool();
        m_dummy_graphics_queue  = new D3D12RHIQueue();
        m_dummy_compute_queue   = new D3D12RHIQueue();

        for (auto& command_buffer : m_dummy_command_buffers)
        {
            command_buffer = new RHICommandBuffer();
        }
        for (auto& fence : m_dummy_fences)
        {
            fence = new D3D12RHIFence();
        }

        m_swapchain_viewport.x        = 0.0f;
        m_swapchain_viewport.y        = 0.0f;
        m_swapchain_viewport.width    = static_cast<float>(m_window_width);
        m_swapchain_viewport.height   = static_cast<float>(m_window_height);
        m_swapchain_viewport.minDepth = 0.0f;
        m_swapchain_viewport.maxDepth = 1.0f;

        m_swapchain_scissor.offset = {0, 0};
        m_swapchain_scissor.extent = {m_window_width, m_window_height};

        m_swapchain_desc.extent       = {m_window_width, m_window_height};
        m_swapchain_desc.image_format = RHI_FORMAT_R8G8B8A8_UNORM;
        m_swapchain_desc.viewport     = &m_swapchain_viewport;
        m_swapchain_desc.scissor      = &m_swapchain_scissor;
        m_owned_swapchain_image_views.resize(m_swapchain_buffer_count);
        m_swapchain_desc.imageViews.clear();
        m_swapchain_desc.imageViews.reserve(m_swapchain_buffer_count);
        for (auto& image_view : m_owned_swapchain_image_views)
        {
            image_view = new RHIImageView();
            m_swapchain_desc.imageViews.push_back(image_view);
        }

        m_depth_desc.depth_image        = new RHIImage();
        m_depth_desc.depth_image_view   = new RHIImageView();
        m_depth_desc.depth_image_format = RHI_FORMAT_D32_SFLOAT;

        m_dummy_texture_copy_semaphore = new D3D12RHISemaphore();

        m_current_command_buffer = m_dummy_command_buffers[0];
        m_current_frame_index    = 0;
#endif
    }

    void D3D12RHI::prepareContext()
    {
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
        m_d3d12_rtv_heap.Reset();
        m_d3d12_swapchain.Reset();
        m_d3d12_command_list.Reset();
        m_d3d12_command_allocator.Reset();
        m_d3d12_command_queue.Reset();
        m_d3d12_device.Reset();
        m_dxgi_factory.Reset();
#endif

        delete m_dummy_command_pool;
        m_dummy_command_pool = nullptr;
        delete m_dummy_descriptor_pool;
        m_dummy_descriptor_pool = nullptr;
        delete m_dummy_graphics_queue;
        m_dummy_graphics_queue = nullptr;
        delete m_dummy_compute_queue;
        m_dummy_compute_queue = nullptr;

        for (auto& command_buffer : m_dummy_command_buffers)
        {
            delete command_buffer;
            command_buffer = nullptr;
        }
        for (auto& fence : m_dummy_fences)
        {
            delete fence;
            fence = nullptr;
        }

        for (auto*& image_view : m_owned_swapchain_image_views)
        {
            delete image_view;
            image_view = nullptr;
        }
        m_owned_swapchain_image_views.clear();

        delete m_depth_desc.depth_image;
        m_depth_desc.depth_image = nullptr;
        delete m_depth_desc.depth_image_view;
        m_depth_desc.depth_image_view = nullptr;

        delete m_dummy_texture_copy_semaphore;
        m_dummy_texture_copy_semaphore = nullptr;

        m_swapchain_desc.imageViews.clear();
        m_swapchain_desc.viewport = nullptr;
        m_swapchain_desc.scissor  = nullptr;

        m_current_command_buffer = nullptr;
        m_window = nullptr;
    }

    RHIBackendType D3D12RHI::getBackendType() const
    {
        return RHIBackendType::D3D12;
    }

bool D3D12RHI::isPointLightShadowEnabled()
{
    return true;
}

bool D3D12RHI::allocateCommandBuffers(const RHICommandBufferAllocateInfo* pAllocateInfo, RHICommandBuffer* &pCommandBuffers)
{
    (void)pAllocateInfo;
    if (pCommandBuffers == nullptr)
    {
        pCommandBuffers = new RHICommandBuffer();
    }
    return true;
}

bool D3D12RHI::allocateDescriptorSets(const RHIDescriptorSetAllocateInfo* pAllocateInfo, RHIDescriptorSet* &pDescriptorSets)
{
    (void)pAllocateInfo;
    if (pDescriptorSets == nullptr)
    {
        pDescriptorSets = new RHIDescriptorSet();
    }
    return true;
}

void D3D12RHI::createSwapchain()
{
    m_swapchain_desc.extent       = {m_window_width, m_window_height};
    m_swapchain_desc.image_format = RHI_FORMAT_R8G8B8A8_UNORM;
    m_swapchain_desc.viewport     = &m_swapchain_viewport;
    m_swapchain_desc.scissor      = &m_swapchain_scissor;
    return;
}

void D3D12RHI::recreateSwapchain()
{
    createSwapchain();
    createSwapchainImageViews();
    createFramebufferImageAndView();
    return;
}

void D3D12RHI::createSwapchainImageViews()
{
    for (auto*& image_view : m_owned_swapchain_image_views)
    {
        delete image_view;
        image_view = nullptr;
    }

    m_owned_swapchain_image_views.resize(m_swapchain_buffer_count);
    m_swapchain_desc.imageViews.clear();
    m_swapchain_desc.imageViews.reserve(m_swapchain_buffer_count);
    for (auto& image_view : m_owned_swapchain_image_views)
    {
        image_view = new RHIImageView();
        m_swapchain_desc.imageViews.push_back(image_view);
    }
    return;
}

void D3D12RHI::createFramebufferImageAndView()
{
    if (m_depth_desc.depth_image == nullptr)
    {
        m_depth_desc.depth_image = new RHIImage();
    }
    if (m_depth_desc.depth_image_view == nullptr)
    {
        m_depth_desc.depth_image_view = new RHIImageView();
    }
    m_depth_desc.depth_image_format = RHI_FORMAT_D32_SFLOAT;
    return;
}

RHISampler* D3D12RHI::getOrCreateDefaultSampler(RHIDefaultSamplerType type)
{
    (void)type;
    return new RHISampler();
}

RHISampler* D3D12RHI::getOrCreateMipmapSampler(uint32_t width, uint32_t height)
{
    (void)width;
    (void)height;
    return new RHISampler();
}

RHIShader* D3D12RHI::createShaderModule(const std::vector<unsigned char>& shader_code)
{
    (void)shader_code;
    return new RHIShader();
}

void D3D12RHI::createBuffer(RHIDeviceSize size, RHIBufferUsageFlags usage, RHIMemoryPropertyFlags properties, RHIBuffer* &buffer, RHIDeviceMemory* &buffer_memory)
{
    (void)usage;
    (void)properties;
    auto* d3d_buffer = new D3D12RHIBuffer();
    d3d_buffer->host_data.resize(static_cast<size_t>(size));
    buffer = d3d_buffer;

    auto* d3d_memory = new D3D12RHIDeviceMemory();
    d3d_memory->owner_buffer = d3d_buffer;
    buffer_memory = d3d_memory;
    return;
}

void D3D12RHI::createBufferAndInitialize(RHIBufferUsageFlags usage, RHIMemoryPropertyFlags properties, RHIBuffer*& buffer, RHIDeviceMemory*& buffer_memory, RHIDeviceSize size, void* data, int datasize)
{
    createBuffer(size, usage, properties, buffer, buffer_memory);
    if (data != nullptr && datasize > 0)
    {
        auto* d3d_buffer = static_cast<D3D12RHIBuffer*>(buffer);
        const size_t copy_size = std::min<size_t>(d3d_buffer->host_data.size(), static_cast<size_t>(datasize));
        std::memcpy(d3d_buffer->host_data.data(), data, copy_size);
    }
    return;
}

bool D3D12RHI::createBufferVMA(VmaAllocator allocator, const RHIBufferCreateInfo* pBufferCreateInfo, const VmaAllocationCreateInfo* pAllocationCreateInfo, RHIBuffer* &pBuffer, VmaAllocation* pAllocation, VmaAllocationInfo* pAllocationInfo)
{
    (void)allocator;
    (void)pAllocationCreateInfo;
    (void)pAllocation;
    (void)pAllocationInfo;
    const RHIBufferCreateInfo default_buffer_info {RHI_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    if (pBufferCreateInfo == nullptr)
    {
        pBufferCreateInfo = &default_buffer_info;
    }

    auto* d3d_buffer = new D3D12RHIBuffer();
    d3d_buffer->host_data.resize(static_cast<size_t>(pBufferCreateInfo->size));
    pBuffer = d3d_buffer;
    return true;
}

bool D3D12RHI::createBufferWithAlignmentVMA( VmaAllocator allocator, const RHIBufferCreateInfo* pBufferCreateInfo, const VmaAllocationCreateInfo* pAllocationCreateInfo, RHIDeviceSize minAlignment, RHIBuffer* &pBuffer, VmaAllocation* pAllocation, VmaAllocationInfo* pAllocationInfo)
{
    (void)minAlignment;
    return createBufferVMA(allocator, pBufferCreateInfo, pAllocationCreateInfo, pBuffer, pAllocation, pAllocationInfo);
}

void D3D12RHI::copyBuffer(RHIBuffer* srcBuffer, RHIBuffer* dstBuffer, RHIDeviceSize srcOffset, RHIDeviceSize dstOffset, RHIDeviceSize size)
{
    if (srcBuffer == nullptr || dstBuffer == nullptr)
    {
        return;
    }

    auto* src = static_cast<D3D12RHIBuffer*>(srcBuffer);
    auto* dst = static_cast<D3D12RHIBuffer*>(dstBuffer);
    const size_t src_offset = static_cast<size_t>(srcOffset);
    const size_t dst_offset = static_cast<size_t>(dstOffset);
    const size_t copy_size  = static_cast<size_t>(size);
    if (src_offset + copy_size > src->host_data.size() || dst_offset + copy_size > dst->host_data.size())
    {
        return;
    }

    std::memcpy(dst->host_data.data() + dst_offset, src->host_data.data() + src_offset, copy_size);
    return;
}

void D3D12RHI::createImage(uint32_t image_width, uint32_t image_height, RHIFormat format, RHIImageTiling image_tiling, RHIImageUsageFlags image_usage_flags, RHIMemoryPropertyFlags memory_property_flags, RHIImage* &image, RHIDeviceMemory* &memory, RHIImageCreateFlags image_create_flags, uint32_t array_layers, uint32_t miplevels)
{
    (void)image_width;
    (void)image_height;
    (void)format;
    (void)image_tiling;
    (void)image_usage_flags;
    (void)memory_property_flags;
    (void)image_create_flags;
    (void)array_layers;
    (void)miplevels;
    image  = new RHIImage();
    memory = new D3D12RHIDeviceMemory();
    return;
}

void D3D12RHI::createImageView(RHIImage* image, RHIFormat format, RHIImageAspectFlags image_aspect_flags, RHIImageViewType view_type, uint32_t layout_count, uint32_t miplevels, RHIImageView* &image_view)
{
    (void)image;
    (void)format;
    (void)image_aspect_flags;
    (void)view_type;
    (void)layout_count;
    (void)miplevels;
    image_view = new RHIImageView();
    return;
}

void D3D12RHI::createGlobalImage(RHIImage* &image, RHIImageView* &image_view, VmaAllocation& image_allocation, uint32_t texture_image_width, uint32_t texture_image_height, void* texture_image_pixels, RHIFormat texture_image_format, uint32_t miplevels)
{
    (void)image_allocation;
    RHIDeviceMemory* memory = nullptr;
    createImage(texture_image_width,
                texture_image_height,
                texture_image_format,
                RHI_IMAGE_TILING_OPTIMAL,
                RHI_IMAGE_USAGE_SAMPLED_BIT,
                RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                image,
                memory,
                0,
                1,
                miplevels);
    createImageView(image,
                    texture_image_format,
                    RHI_IMAGE_ASPECT_COLOR_BIT,
                    RHI_IMAGE_VIEW_TYPE_2D,
                    1,
                    miplevels,
                    image_view);
    delete memory;
    (void)texture_image_pixels;
    return;
}

void D3D12RHI::createCubeMap(RHIImage* &image, RHIImageView* &image_view, VmaAllocation& image_allocation, uint32_t texture_image_width, uint32_t texture_image_height, std::array<void*, 6> texture_image_pixels, RHIFormat texture_image_format, uint32_t miplevels)
{
    (void)texture_image_pixels;
    (void)image_allocation;
    RHIDeviceMemory* memory = nullptr;
    createImage(texture_image_width,
                texture_image_height,
                texture_image_format,
                RHI_IMAGE_TILING_OPTIMAL,
                RHI_IMAGE_USAGE_SAMPLED_BIT,
                RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                image,
                memory,
                0,
                6,
                miplevels);
    createImageView(image,
                    texture_image_format,
                    RHI_IMAGE_ASPECT_COLOR_BIT,
                    RHI_IMAGE_VIEW_TYPE_CUBE,
                    6,
                    miplevels,
                    image_view);
    delete memory;
    return;
}

void D3D12RHI::createCommandPool()
{
    return;
}

bool D3D12RHI::createCommandPool(const RHICommandPoolCreateInfo* pCreateInfo, RHICommandPool*& pCommandPool)
{
    (void)pCreateInfo;
    if (pCommandPool == nullptr)
    {
        pCommandPool = new D3D12RHICommandPool();
    }
    return true;
}

bool D3D12RHI::createDescriptorPool(const RHIDescriptorPoolCreateInfo* pCreateInfo, RHIDescriptorPool* &pDescriptorPool)
{
    (void)pCreateInfo;
    if (pDescriptorPool == nullptr)
    {
        pDescriptorPool = new RHIDescriptorPool();
    }
    return true;
}

bool D3D12RHI::createDescriptorSetLayout(const RHIDescriptorSetLayoutCreateInfo* pCreateInfo, RHIDescriptorSetLayout* &pSetLayout)
{
    (void)pCreateInfo;
    if (pSetLayout == nullptr)
    {
        pSetLayout = new RHIDescriptorSetLayout();
    }
    return true;
}

bool D3D12RHI::createFence(const RHIFenceCreateInfo* pCreateInfo, RHIFence* &pFence)
{
    (void)pCreateInfo;
    if (pFence == nullptr)
    {
        pFence = new D3D12RHIFence();
    }
    return true;
}

bool D3D12RHI::createFramebuffer(const RHIFramebufferCreateInfo* pCreateInfo, RHIFramebuffer* &pFramebuffer)
{
    (void)pCreateInfo;
    if (pFramebuffer == nullptr)
    {
        pFramebuffer = new RHIFramebuffer();
    }
    return true;
}

bool D3D12RHI::createGraphicsPipelines(RHIPipelineCache* pipelineCache, uint32_t createInfoCount, const RHIGraphicsPipelineCreateInfo* pCreateInfos, RHIPipeline* &pPipelines)
{
    (void)pipelineCache;
    (void)createInfoCount;
    (void)pCreateInfos;
    if (pPipelines == nullptr)
    {
        pPipelines = new RHIPipeline();
    }
    return true;
}

bool D3D12RHI::createComputePipelines(RHIPipelineCache* pipelineCache, uint32_t createInfoCount, const RHIComputePipelineCreateInfo* pCreateInfos, RHIPipeline* &pPipelines)
{
    (void)pipelineCache;
    (void)createInfoCount;
    (void)pCreateInfos;
    if (pPipelines == nullptr)
    {
        pPipelines = new RHIPipeline();
    }
    return true;
}

bool D3D12RHI::createPipelineLayout(const RHIPipelineLayoutCreateInfo* pCreateInfo, RHIPipelineLayout* &pPipelineLayout)
{
    (void)pCreateInfo;
    if (pPipelineLayout == nullptr)
    {
        pPipelineLayout = new RHIPipelineLayout();
    }
    return true;
}

bool D3D12RHI::createRenderPass(const RHIRenderPassCreateInfo* pCreateInfo, RHIRenderPass* &pRenderPass)
{
    (void)pCreateInfo;
    if (pRenderPass == nullptr)
    {
        pRenderPass = new RHIRenderPass();
    }
    return true;
}

bool D3D12RHI::createSampler(const RHISamplerCreateInfo* pCreateInfo, RHISampler* &pSampler)
{
    (void)pCreateInfo;
    if (pSampler == nullptr)
    {
        pSampler = new RHISampler();
    }
    return true;
}

bool D3D12RHI::createSemaphore(const RHISemaphoreCreateInfo* pCreateInfo, RHISemaphore* &pSemaphore)
{
    (void)pCreateInfo;
    if (pSemaphore == nullptr)
    {
        pSemaphore = new D3D12RHISemaphore();
    }
    return true;
}

bool D3D12RHI::waitForFencesPFN(uint32_t fenceCount, RHIFence* const* pFence, RHIBool32 waitAll, uint64_t timeout)
{
    (void)fenceCount;
    (void)pFence;
    (void)waitAll;
    (void)timeout;
#ifdef _WIN32
    waitForGpu();
#endif
    return true;
}

bool D3D12RHI::resetFencesPFN(uint32_t fenceCount, RHIFence* const* pFences)
{
    (void)fenceCount;
    (void)pFences;
    return true;
}

bool D3D12RHI::resetCommandPoolPFN(RHICommandPool* commandPool, RHICommandPoolResetFlags flags)
{
    (void)commandPool;
    (void)flags;
    resetCommandPool();
    return true;
}

bool D3D12RHI::beginCommandBufferPFN(RHICommandBuffer* commandBuffer, const RHICommandBufferBeginInfo* pBeginInfo)
{
    (void)commandBuffer;
    (void)pBeginInfo;
#ifdef _WIN32
    if (m_d3d12_command_allocator == nullptr || m_d3d12_command_list == nullptr)
    {
        return false;
    }

    if (FAILED(m_d3d12_command_allocator->Reset()))
    {
        return false;
    }

    if (FAILED(m_d3d12_command_list->Reset(m_d3d12_command_allocator.Get(), nullptr)))
    {
        return false;
    }

    m_in_render_pass = false;
    m_command_list_open = true;
    return true;
#else
    return true;
#endif
}

bool D3D12RHI::endCommandBufferPFN(RHICommandBuffer* commandBuffer)
{
    (void)commandBuffer;
#ifdef _WIN32
    if (m_d3d12_command_list == nullptr)
    {
        return false;
    }

    if (FAILED(m_d3d12_command_list->Close()))
    {
        return false;
    }

    m_command_list_open = false;
    return true;
#else
    return true;
#endif
}

void D3D12RHI::cmdBeginRenderPassPFN(RHICommandBuffer* commandBuffer, const RHIRenderPassBeginInfo* pRenderPassBegin, RHISubpassContents contents)
{
    (void)commandBuffer;
    (void)contents;
#ifdef _WIN32
    if (m_d3d12_command_list == nullptr || m_d3d12_rtv_heap == nullptr || m_swapchain_buffer_count == 0)
    {
        return;
    }

    const uint32_t back_buffer_index = m_current_frame_index % m_swapchain_buffer_count;
    if (back_buffer_index >= m_d3d12_render_targets.size())
    {
        return;
    }

    ID3D12Resource* render_target = m_d3d12_render_targets[back_buffer_index].Get();
    if (render_target == nullptr)
    {
        return;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = m_d3d12_rtv_heap->GetCPUDescriptorHandleForHeapStart();
    rtv_handle.ptr += static_cast<SIZE_T>(back_buffer_index) * static_cast<SIZE_T>(m_d3d12_rtv_descriptor_size);

    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = render_target;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore  = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter   = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_d3d12_command_list->ResourceBarrier(1, &barrier);

    D3D12_VIEWPORT d3d_viewport {};
    d3d_viewport.TopLeftX = m_swapchain_viewport.x;
    d3d_viewport.TopLeftY = m_swapchain_viewport.y;
    d3d_viewport.Width    = m_swapchain_viewport.width;
    d3d_viewport.Height   = m_swapchain_viewport.height;
    d3d_viewport.MinDepth = m_swapchain_viewport.minDepth;
    d3d_viewport.MaxDepth = m_swapchain_viewport.maxDepth;

    D3D12_RECT d3d_scissor {};
    d3d_scissor.left   = static_cast<LONG>(m_swapchain_scissor.offset.x);
    d3d_scissor.top    = static_cast<LONG>(m_swapchain_scissor.offset.y);
    d3d_scissor.right  = static_cast<LONG>(m_swapchain_scissor.offset.x + m_swapchain_scissor.extent.width);
    d3d_scissor.bottom = static_cast<LONG>(m_swapchain_scissor.offset.y + m_swapchain_scissor.extent.height);

    m_d3d12_command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
    m_d3d12_command_list->RSSetViewports(1, &d3d_viewport);
    m_d3d12_command_list->RSSetScissorRects(1, &d3d_scissor);

    if (pRenderPassBegin != nullptr && pRenderPassBegin->clearValueCount > 0 && pRenderPassBegin->pClearValues != nullptr)
    {
        const auto& clear_color = pRenderPassBegin->pClearValues[0].color;
        const FLOAT color[4] = {clear_color.float32[0], clear_color.float32[1], clear_color.float32[2], clear_color.float32[3]};
        m_d3d12_command_list->ClearRenderTargetView(rtv_handle, color, 0, nullptr);
    }

    m_in_render_pass = true;
#endif
}

void D3D12RHI::cmdNextSubpassPFN(RHICommandBuffer* commandBuffer, RHISubpassContents contents)
{
    (void)commandBuffer;
    (void)contents;
}

void D3D12RHI::cmdEndRenderPassPFN(RHICommandBuffer* commandBuffer)
{
    (void)commandBuffer;
#ifdef _WIN32
    if (m_d3d12_command_list == nullptr || m_swapchain_buffer_count == 0)
    {
        return;
    }

    const uint32_t back_buffer_index = m_current_frame_index % m_swapchain_buffer_count;
    if (back_buffer_index < m_d3d12_render_targets.size())
    {
        ID3D12Resource* render_target = m_d3d12_render_targets[back_buffer_index].Get();
        if (render_target != nullptr)
        {
            D3D12_RESOURCE_BARRIER barrier {};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource   = render_target;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore  = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter   = D3D12_RESOURCE_STATE_PRESENT;
            m_d3d12_command_list->ResourceBarrier(1, &barrier);
        }
    }

    m_in_render_pass = false;
#endif
}

void D3D12RHI::cmdBindPipelinePFN(RHICommandBuffer* commandBuffer, RHIPipelineBindPoint pipelineBindPoint, RHIPipeline* pipeline)
{
    return;
}

void D3D12RHI::cmdSetViewportPFN(RHICommandBuffer* commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const RHIViewport* pViewports)
{
    (void)commandBuffer;
#ifdef _WIN32
    if (m_d3d12_command_list == nullptr || pViewports == nullptr || viewportCount == 0)
    {
        return;
    }

    std::vector<D3D12_VIEWPORT> d3d_viewports;
    d3d_viewports.reserve(viewportCount);
    for (uint32_t i = 0; i < viewportCount; ++i)
    {
        const auto& viewport = pViewports[i];
        D3D12_VIEWPORT d3d_viewport {};
        d3d_viewport.TopLeftX = viewport.x;
        d3d_viewport.TopLeftY = viewport.y;
        d3d_viewport.Width    = viewport.width;
        d3d_viewport.Height   = viewport.height;
        d3d_viewport.MinDepth = viewport.minDepth;
        d3d_viewport.MaxDepth = viewport.maxDepth;
        d3d_viewports.push_back(d3d_viewport);
    }

    if (firstViewport < d3d_viewports.size())
    {
        m_d3d12_command_list->RSSetViewports(viewportCount - firstViewport, d3d_viewports.data() + firstViewport);
    }
#endif
}

void D3D12RHI::cmdSetScissorPFN(RHICommandBuffer* commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const RHIRect2D* pScissors)
{
    (void)commandBuffer;
#ifdef _WIN32
    if (m_d3d12_command_list == nullptr || pScissors == nullptr || scissorCount == 0)
    {
        return;
    }

    std::vector<D3D12_RECT> d3d_scissors;
    d3d_scissors.reserve(scissorCount);
    for (uint32_t i = 0; i < scissorCount; ++i)
    {
        const auto& scissor = pScissors[i];
        D3D12_RECT d3d_scissor {};
        d3d_scissor.left   = scissor.offset.x;
        d3d_scissor.top    = scissor.offset.y;
        d3d_scissor.right  = scissor.offset.x + static_cast<LONG>(scissor.extent.width);
        d3d_scissor.bottom = scissor.offset.y + static_cast<LONG>(scissor.extent.height);
        d3d_scissors.push_back(d3d_scissor);
    }

    if (firstScissor < d3d_scissors.size())
    {
        m_d3d12_command_list->RSSetScissorRects(scissorCount - firstScissor, d3d_scissors.data() + firstScissor);
    }
#endif
}

void D3D12RHI::cmdBindVertexBuffersPFN( RHICommandBuffer* commandBuffer, uint32_t firstBinding, uint32_t bindingCount, RHIBuffer* const* pBuffers, const RHIDeviceSize* pOffsets)
{
    return;
}

void D3D12RHI::cmdBindIndexBufferPFN(RHICommandBuffer* commandBuffer, RHIBuffer* buffer, RHIDeviceSize offset, RHIIndexType indexType)
{
    return;
}

void D3D12RHI::cmdBindDescriptorSetsPFN( RHICommandBuffer* commandBuffer, RHIPipelineBindPoint pipelineBindPoint, RHIPipelineLayout* layout, uint32_t firstSet, uint32_t descriptorSetCount, const RHIDescriptorSet* const* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets)
{
    return;
}

void D3D12RHI::cmdDrawIndexedPFN(RHICommandBuffer* commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
    (void)commandBuffer;
#ifdef _WIN32
    if (m_d3d12_command_list == nullptr)
    {
        return;
    }

    m_d3d12_command_list->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
#endif
}

void D3D12RHI::cmdClearAttachmentsPFN(RHICommandBuffer* commandBuffer, uint32_t attachmentCount, const RHIClearAttachment* pAttachments, uint32_t rectCount, const RHIClearRect* pRects)
{
    return;
}

bool D3D12RHI::beginCommandBuffer(RHICommandBuffer* commandBuffer, const RHICommandBufferBeginInfo* pBeginInfo)
{
    return beginCommandBufferPFN(commandBuffer, pBeginInfo);
}

void D3D12RHI::cmdCopyImageToBuffer(RHICommandBuffer* commandBuffer, RHIImage* srcImage, RHIImageLayout srcImageLayout, RHIBuffer* dstBuffer, uint32_t regionCount, const RHIBufferImageCopy* pRegions)
{
    (void)commandBuffer;
    (void)srcImage;
    (void)srcImageLayout;
    (void)dstBuffer;
    (void)regionCount;
    (void)pRegions;
    return;
}

void D3D12RHI::cmdCopyImageToImage(RHICommandBuffer* commandBuffer, RHIImage* srcImage, RHIImageAspectFlagBits srcFlag, RHIImage* dstImage, RHIImageAspectFlagBits dstFlag, uint32_t width, uint32_t height)
{
    (void)commandBuffer;
    (void)srcImage;
    (void)srcFlag;
    (void)dstImage;
    (void)dstFlag;
    (void)width;
    (void)height;
    return;
}

void D3D12RHI::cmdCopyBuffer(RHICommandBuffer* commandBuffer, RHIBuffer* srcBuffer, RHIBuffer* dstBuffer, uint32_t regionCount, RHIBufferCopy* pRegions)
{
    (void)commandBuffer;
    if (srcBuffer == nullptr || dstBuffer == nullptr)
    {
        return;
    }

    if (pRegions == nullptr || regionCount == 0)
    {
        auto* src = static_cast<D3D12RHIBuffer*>(srcBuffer);
        const RHIDeviceSize fallback_size = static_cast<RHIDeviceSize>(src->host_data.size());
        copyBuffer(srcBuffer, dstBuffer, 0, 0, fallback_size);
        return;
    }

    for (uint32_t i = 0; i < regionCount; ++i)
    {
        copyBuffer(srcBuffer, dstBuffer, pRegions[i].srcOffset, pRegions[i].dstOffset, pRegions[i].size);
    }
    return;
}

void D3D12RHI::cmdDraw(RHICommandBuffer* commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
    (void)commandBuffer;
#ifdef _WIN32
    if (m_d3d12_command_list != nullptr)
    {
        m_d3d12_command_list->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
    }
#endif
    return;
}

void D3D12RHI::cmdDispatch(RHICommandBuffer* commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    (void)commandBuffer;
#ifdef _WIN32
    if (m_d3d12_command_list != nullptr)
    {
        m_d3d12_command_list->Dispatch(groupCountX, groupCountY, groupCountZ);
    }
#endif
    return;
}

void D3D12RHI::cmdDispatchIndirect(RHICommandBuffer* commandBuffer, RHIBuffer* buffer, RHIDeviceSize offset)
{
    (void)commandBuffer;
    (void)buffer;
    (void)offset;
    return;
}

void D3D12RHI::cmdPipelineBarrier(RHICommandBuffer* commandBuffer, RHIPipelineStageFlags srcStageMask, RHIPipelineStageFlags dstStageMask, RHIDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const RHIMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const RHIBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const RHIImageMemoryBarrier* pImageMemoryBarriers)
{
    (void)commandBuffer;
    (void)srcStageMask;
    (void)dstStageMask;
    (void)dependencyFlags;
    (void)memoryBarrierCount;
    (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount;
    (void)pBufferMemoryBarriers;
    (void)imageMemoryBarrierCount;
    (void)pImageMemoryBarriers;
    return;
}

bool D3D12RHI::endCommandBuffer(RHICommandBuffer* commandBuffer)
{
    return endCommandBufferPFN(commandBuffer);
}

void D3D12RHI::updateDescriptorSets(uint32_t descriptorWriteCount, const RHIWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const RHICopyDescriptorSet* pDescriptorCopies)
{
    return;
}

bool D3D12RHI::queueSubmit(RHIQueue* queue, uint32_t submitCount, const RHISubmitInfo* pSubmits, RHIFence* fence)
{
    (void)queue;
    (void)fence;
#ifdef _WIN32
    if (m_d3d12_command_queue == nullptr || m_d3d12_command_list == nullptr)
    {
        return false;
    }

    bool has_work = false;
    if (pSubmits != nullptr)
    {
        for (uint32_t i = 0; i < submitCount; ++i)
        {
            if (pSubmits[i].commandBufferCount > 0 && pSubmits[i].pCommandBuffers != nullptr)
            {
                has_work = true;
                break;
            }
        }
    }

    if (!has_work)
    {
        return true;
    }

    if (m_command_list_open)
    {
        if (FAILED(m_d3d12_command_list->Close()))
        {
            return false;
        }
        m_command_list_open = false;
    }

    ID3D12CommandList* command_lists[] = {m_d3d12_command_list.Get()};
    m_d3d12_command_queue->ExecuteCommandLists(1, command_lists);
    return true;
#else
    (void)submitCount;
    (void)pSubmits;
    return true;
#endif
}

bool D3D12RHI::queueWaitIdle(RHIQueue* queue)
{
    (void)queue;
    waitForGpu();
    return true;
}

void D3D12RHI::resetCommandPool()
{
#ifdef _WIN32
    if (!m_d3d12_command_allocator || !m_d3d12_command_list)
    {
        return;
    }

    (void)m_d3d12_command_allocator->Reset();
    (void)m_d3d12_command_list->Reset(m_d3d12_command_allocator.Get(), nullptr);
    m_command_list_open = true;
#endif
    return;
}

void D3D12RHI::waitForFences()
{
#ifdef _WIN32
    waitForGpu();
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

RHICommandBuffer* D3D12RHI::getCurrentCommandBuffer() const
{
    return m_current_command_buffer;
}

RHICommandBuffer* const* D3D12RHI::getCommandBufferList() const
{
    return m_dummy_command_buffers.data();
}

RHICommandPool* D3D12RHI::getCommandPoor() const
{
    return m_dummy_command_pool;
}

RHIDescriptorPool* D3D12RHI::getDescriptorPoor() const
{
    return m_dummy_descriptor_pool;
}

RHIFence* const* D3D12RHI::getFenceList() const
{
    return m_dummy_fences.data();
}

QueueFamilyIndices D3D12RHI::getQueueFamilyIndices() const
{
    QueueFamilyIndices indices;
    indices.graphics_family = 0;
    indices.present_family  = 0;
    indices.m_compute_family = 0;
    return indices;
}

RHIQueue* D3D12RHI::getGraphicsQueue() const
{
    return m_dummy_graphics_queue;
}

RHIQueue* D3D12RHI::getComputeQueue() const
{
    return m_dummy_compute_queue;
}

RHISwapChainDesc D3D12RHI::getSwapchainInfo()
{
    return m_swapchain_desc;
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

void D3D12RHI::setCurrentFrameIndex(uint8_t index)
{
    m_current_frame_index = index;
}

RHICommandBuffer* D3D12RHI::beginSingleTimeCommands()
{
    auto* command_buffer = new RHICommandBuffer();
    return command_buffer;
}

void D3D12RHI::endSingleTimeCommands(RHICommandBuffer* command_buffer)
{
    delete command_buffer;
    return;
}

bool D3D12RHI::prepareBeforePass(std::function<void()> passUpdateAfterRecreateSwapchain)
{
    (void)passUpdateAfterRecreateSwapchain;
#ifdef _WIN32
    if (!m_d3d12_swapchain)
    {
        return true;
    }

    m_current_frame_index = static_cast<uint8_t>(m_d3d12_swapchain->GetCurrentBackBufferIndex());
    m_current_command_buffer = m_dummy_command_buffers[m_current_frame_index % m_dummy_command_buffers.size()];

    if (m_d3d12_command_allocator && m_d3d12_command_list)
    {
        (void)m_d3d12_command_allocator->Reset();
        (void)m_d3d12_command_list->Reset(m_d3d12_command_allocator.Get(), nullptr);
        m_command_list_open = true;
    }
#endif
    return false;
}

void D3D12RHI::submitRendering(std::function<void()> passUpdateAfterRecreateSwapchain)
{
    (void)passUpdateAfterRecreateSwapchain;
#ifdef _WIN32
    if (!m_d3d12_swapchain || !m_d3d12_command_queue || !m_d3d12_command_list)
    {
        return;
    }

    if (m_command_list_open && FAILED(m_d3d12_command_list->Close()))
    {
        return;
    }
    m_command_list_open = false;

    ID3D12CommandList* command_lists[] = {m_d3d12_command_list.Get()};
    m_d3d12_command_queue->ExecuteCommandLists(1, command_lists);
    (void)m_d3d12_swapchain->Present(1, 0);
    waitForGpu();

    m_current_frame_index = static_cast<uint8_t>(m_d3d12_swapchain->GetCurrentBackBufferIndex());
    m_current_command_buffer = m_dummy_command_buffers[m_current_frame_index % m_dummy_command_buffers.size()];
#endif
    return;
}

void D3D12RHI::pushEvent(RHICommandBuffer* commond_buffer, const char* name, const float* color)
{
    return;
}

void D3D12RHI::popEvent(RHICommandBuffer* commond_buffer)
{
    return;
}

void D3D12RHI::clearSwapchain()
{
    return;
}

void D3D12RHI::destroyDefaultSampler(RHIDefaultSamplerType type)
{
    (void)type;
    return;
}

void D3D12RHI::destroyMipmappedSampler()
{
    return;
}

void D3D12RHI::destroyShaderModule(RHIShader* shader)
{
    delete shader;
    return;
}

void D3D12RHI::destroySemaphore(RHISemaphore* semaphore)
{
    delete semaphore;
    return;
}

void D3D12RHI::destroySampler(RHISampler* sampler)
{
    delete sampler;
    return;
}

void D3D12RHI::destroyInstance(RHIInstance* instance)
{
    delete instance;
    return;
}

void D3D12RHI::destroyImageView(RHIImageView* imageView)
{
    if (imageView == nullptr)
    {
        return;
    }

    if (imageView == m_depth_desc.depth_image_view)
    {
        delete m_depth_desc.depth_image_view;
        m_depth_desc.depth_image_view = nullptr;
        return;
    }

    for (auto*& swapchain_image_view : m_owned_swapchain_image_views)
    {
        if (swapchain_image_view == imageView)
        {
            delete swapchain_image_view;
            swapchain_image_view = nullptr;
            return;
        }
    }

    delete imageView;
    return;
}

void D3D12RHI::destroyImage(RHIImage* image)
{
    if (image == nullptr)
    {
        return;
    }

    if (image == m_depth_desc.depth_image)
    {
        delete m_depth_desc.depth_image;
        m_depth_desc.depth_image = nullptr;
        return;
    }

    delete image;
    return;
}

void D3D12RHI::destroyFramebuffer(RHIFramebuffer* framebuffer)
{
    delete framebuffer;
    return;
}

void D3D12RHI::destroyFence(RHIFence* fence)
{
    if (fence == nullptr)
    {
        return;
    }

    for (auto*& dummy_fence : m_dummy_fences)
    {
        if (dummy_fence == fence)
        {
            delete dummy_fence;
            dummy_fence = nullptr;
            return;
        }
    }

    delete fence;
    return;
}

void D3D12RHI::destroyDevice()
{
    return;
}

void D3D12RHI::destroyCommandPool(RHICommandPool* commandPool)
{
    if (commandPool == nullptr)
    {
        return;
    }

    if (commandPool == m_dummy_command_pool)
    {
        delete m_dummy_command_pool;
        m_dummy_command_pool = nullptr;
        return;
    }

    delete commandPool;
    return;
}

void D3D12RHI::destroyBuffer(RHIBuffer* &buffer)
{
    delete static_cast<D3D12RHIBuffer*>(buffer);
    buffer = nullptr;
    return;
}

void D3D12RHI::freeCommandBuffers(RHICommandPool* commandPool, uint32_t commandBufferCount, RHICommandBuffer* pCommandBuffers)
{
    (void)commandPool;
    (void)commandBufferCount;
    delete pCommandBuffers;
    return;
}

void D3D12RHI::freeMemory(RHIDeviceMemory* &memory)
{
    delete static_cast<D3D12RHIDeviceMemory*>(memory);
    memory = nullptr;
    return;
}

bool D3D12RHI::mapMemory(RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size, RHIMemoryMapFlags flags, void** ppData)
{
    (void)flags;
    if (memory == nullptr || ppData == nullptr)
    {
        return false;
    }

    auto* d3d_memory = static_cast<D3D12RHIDeviceMemory*>(memory);
    if (d3d_memory->owner_buffer == nullptr)
    {
        return false;
    }

    auto& host_data = d3d_memory->owner_buffer->host_data;
    const size_t begin = static_cast<size_t>(offset);
    if (begin > host_data.size())
    {
        return false;
    }

    const size_t requested = (size == RHI_WHOLE_SIZE) ? (host_data.size() - begin) : static_cast<size_t>(size);
    if (requested > host_data.size() - begin)
    {
        return false;
    }

    d3d_memory->mapped_ptr = host_data.data() + begin;
    *ppData                = d3d_memory->mapped_ptr;
    return true;
}

void D3D12RHI::unmapMemory(RHIDeviceMemory* memory)
{
    if (memory)
    {
        static_cast<D3D12RHIDeviceMemory*>(memory)->mapped_ptr = nullptr;
    }
    return;
}

void D3D12RHI::invalidateMappedMemoryRanges(void* pNext, RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size)
{
    return;
}

void D3D12RHI::flushMappedMemoryRanges(void* pNext, RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size)
{
    return;
}

RHISemaphore*& D3D12RHI::getTextureCopySemaphore(uint32_t index)
{
    (void)index;
    if (m_dummy_texture_copy_semaphore == nullptr)
    {
        m_dummy_texture_copy_semaphore = new D3D12RHISemaphore();
    }
    return m_dummy_texture_copy_semaphore;
}



#ifdef _WIN32
    void D3D12RHI::createDevice()
    {
        UINT dxgi_factory_flags = 0;
#ifdef _DEBUG
        ComPtr<ID3D12Debug> debug_controller;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller))))
        {
            debug_controller->EnableDebugLayer();
            dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
        }
#endif

        if (FAILED(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&m_dxgi_factory))))
        {
            throw std::runtime_error("Failed to create DXGI factory");
        }

        ComPtr<IDXGIAdapter1> hardware_adapter;
        for (UINT adapter_index = 0; DXGI_ERROR_NOT_FOUND != m_dxgi_factory->EnumAdapters1(adapter_index, &hardware_adapter); ++adapter_index)
        {
            DXGI_ADAPTER_DESC1 desc {};
            hardware_adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                continue;
            }

            if (SUCCEEDED(D3D12CreateDevice(hardware_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_d3d12_device))))
            {
                break;
            }
        }

        if (!m_d3d12_device)
        {
            throw std::runtime_error("Failed to create D3D12 device");
        }
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
        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc {};
        rtv_heap_desc.NumDescriptors = m_swapchain_buffer_count;
        rtv_heap_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        if (FAILED(m_d3d12_device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&m_d3d12_rtv_heap))))
        {
            throw std::runtime_error("Failed to create D3D12 RTV descriptor heap");
        }

        m_d3d12_rtv_descriptor_size = m_d3d12_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

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
    }

    void D3D12RHI::createFence()
    {
        if (FAILED(m_d3d12_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_d3d12_fence))))
        {
            throw std::runtime_error("Failed to create D3D12 fence");
        }

        m_d3d12_fence_value = 1;
        m_d3d12_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!m_d3d12_fence_event)
        {
            throw std::runtime_error("Failed to create D3D12 fence event");
        }
    }

    void D3D12RHI::waitForGpu()
    {
        if (!m_d3d12_command_queue || !m_d3d12_fence || !m_d3d12_fence_event)
        {
            return;
        }

        const uint64_t fence_value = m_d3d12_fence_value;
        if (FAILED(m_d3d12_command_queue->Signal(m_d3d12_fence.Get(), fence_value)))
        {
            return;
        }

        ++m_d3d12_fence_value;

        if (m_d3d12_fence->GetCompletedValue() < fence_value)
        {
            if (FAILED(m_d3d12_fence->SetEventOnCompletion(fence_value, m_d3d12_fence_event)))
            {
                return;
            }
            WaitForSingleObject(m_d3d12_fence_event, INFINITE);
        }
    }
#endif
} // namespace Piccolo
