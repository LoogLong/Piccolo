#pragma once

#include "runtime/function/render/interface/rhi.h"

#include <array>
#include <map>

#ifdef _WIN32
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;
#endif

struct GLFWwindow;

namespace Piccolo
{
    class D3D12RHI : public RHI
    {
    public:
        ~D3D12RHI() override;

        void initialize(RHIInitInfo init_info) override;
        void prepareContext() override;
        void clear() override;
        RHIBackendType getBackendType() const override;
        void setViewport(float x, float y, float width, float height, float min_depth = 0.0f, float max_depth = 1.0f) override;
        RHIViewport getViewport() const override;
#ifdef _WIN32
        GLFWwindow* getWindow() const;
        ID3D12Device* getD3D12Device() const;
        ID3D12CommandQueue* getD3D12GraphicsQueue() const;
        ID3D12GraphicsCommandList* getD3D12CommandList() const;
        ID3D12DescriptorHeap* getD3D12ImGuiSrvHeap() const;
        D3D12_CPU_DESCRIPTOR_HANDLE getD3D12ImGuiSrvCpuHandle() const;
        D3D12_GPU_DESCRIPTOR_HANDLE getD3D12ImGuiSrvGpuHandle() const;
        DXGI_FORMAT getD3D12SwapchainFormat() const;
#endif

    bool isPointLightShadowEnabled() override;
    bool allocateCommandBuffers(const RHICommandBufferAllocateInfo* pAllocateInfo, RHICommandBuffer* &pCommandBuffers) override;
    bool allocateDescriptorSets(const RHIDescriptorSetAllocateInfo* pAllocateInfo, RHIDescriptorSet* &pDescriptorSets) override;
    void createSwapchain() override;
    void recreateSwapchain() override;
    void createSwapchainImageViews() override;
    void createFramebufferImageAndView() override;
    RHISampler* getOrCreateDefaultSampler(RHIDefaultSamplerType type) override;
    RHISampler* getOrCreateMipmapSampler(uint32_t width, uint32_t height) override;
    RHIShader* createShaderModule(const std::vector<unsigned char>& shader_code) override;
    void createBuffer(RHIDeviceSize size, RHIBufferUsageFlags usage, RHIMemoryPropertyFlags properties, RHIBuffer* &buffer, RHIDeviceMemory* &buffer_memory) override;
    void createBufferAndInitialize(RHIBufferUsageFlags usage, RHIMemoryPropertyFlags properties, RHIBuffer*& buffer, RHIDeviceMemory*& buffer_memory, RHIDeviceSize size, void* data = nullptr, int datasize = 0) override;
    bool createBufferWithAllocation(const RHIBufferCreateInfo* pBufferCreateInfo, RHIMemoryPropertyFlags memoryPropertyFlags, RHIBuffer* &pBuffer, RHIAllocation*& pAllocation) override;
    bool createBufferWithAlignment(const RHIBufferCreateInfo* pBufferCreateInfo, RHIMemoryPropertyFlags memoryPropertyFlags, RHIDeviceSize minAlignment, RHIBuffer* &pBuffer, RHIAllocation*& pAllocation) override;
    void copyBuffer(RHIBuffer* srcBuffer, RHIBuffer* dstBuffer, RHIDeviceSize srcOffset, RHIDeviceSize dstOffset, RHIDeviceSize size) override;
    void createImage(uint32_t image_width, uint32_t image_height, RHIFormat format, RHIImageTiling image_tiling, RHIImageUsageFlags image_usage_flags, RHIMemoryPropertyFlags memory_property_flags, RHIImage* &image, RHIDeviceMemory* &memory, RHIImageCreateFlags image_create_flags, uint32_t array_layers, uint32_t miplevels) override;
    void createImageView(RHIImage* image, RHIFormat format, RHIImageAspectFlags image_aspect_flags, RHIImageViewType view_type, uint32_t layout_count, uint32_t miplevels, RHIImageView* &image_view) override;
    void createGlobalImage(RHIImage* &image, RHIImageView* &image_view, RHIAllocation*& image_allocation, uint32_t texture_image_width, uint32_t texture_image_height, void* texture_image_pixels, RHIFormat texture_image_format, uint32_t miplevels = 0) override;
    void createCubeMap(RHIImage* &image, RHIImageView* &image_view, RHIAllocation*& image_allocation, uint32_t texture_image_width, uint32_t texture_image_height, std::array<void*, 6> texture_image_pixels, RHIFormat texture_image_format, uint32_t miplevels) override;
    void createCommandPool() override;
    bool createCommandPool(const RHICommandPoolCreateInfo* pCreateInfo, RHICommandPool*& pCommandPool) override;
    bool createDescriptorPool(const RHIDescriptorPoolCreateInfo* pCreateInfo, RHIDescriptorPool* &pDescriptorPool) override;
    bool createDescriptorSetLayout(const RHIDescriptorSetLayoutCreateInfo* pCreateInfo, RHIDescriptorSetLayout* &pSetLayout) override;
    bool createFence(const RHIFenceCreateInfo* pCreateInfo, RHIFence* &pFence) override;
    bool createFramebuffer(const RHIFramebufferCreateInfo* pCreateInfo, RHIFramebuffer* &pFramebuffer) override;
    bool createGraphicsPipelines(RHIPipelineCache* pipelineCache, uint32_t createInfoCount, const RHIGraphicsPipelineCreateInfo* pCreateInfos, RHIPipeline* &pPipelines) override;
    bool createComputePipelines(RHIPipelineCache* pipelineCache, uint32_t createInfoCount, const RHIComputePipelineCreateInfo* pCreateInfos, RHIPipeline* &pPipelines) override;
    bool createPipelineLayout(const RHIPipelineLayoutCreateInfo* pCreateInfo, RHIPipelineLayout* &pPipelineLayout) override;
    bool createRenderPass(const RHIRenderPassCreateInfo* pCreateInfo, RHIRenderPass* &pRenderPass) override;
    bool createSampler(const RHISamplerCreateInfo* pCreateInfo, RHISampler* &pSampler) override;
    bool createSemaphore(const RHISemaphoreCreateInfo* pCreateInfo, RHISemaphore* &pSemaphore) override;
    bool waitForFencesPFN(uint32_t fenceCount, RHIFence* const* pFence, RHIBool32 waitAll, uint64_t timeout) override;
    bool resetFencesPFN(uint32_t fenceCount, RHIFence* const* pFences) override;
    bool resetCommandPoolPFN(RHICommandPool* commandPool, RHICommandPoolResetFlags flags) override;
    bool beginCommandBufferPFN(RHICommandBuffer* commandBuffer, const RHICommandBufferBeginInfo* pBeginInfo) override;
    bool endCommandBufferPFN(RHICommandBuffer* commandBuffer) override;
    void cmdBeginRenderPassPFN(RHICommandBuffer* commandBuffer, const RHIRenderPassBeginInfo* pRenderPassBegin, RHISubpassContents contents) override;
    void cmdNextSubpassPFN(RHICommandBuffer* commandBuffer, RHISubpassContents contents) override;
    void cmdEndRenderPassPFN(RHICommandBuffer* commandBuffer) override;
    void cmdBindPipelinePFN(RHICommandBuffer* commandBuffer, RHIPipelineBindPoint pipelineBindPoint, RHIPipeline* pipeline) override;
    void cmdSetViewportPFN(RHICommandBuffer* commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const RHIViewport* pViewports) override;
    void cmdSetScissorPFN(RHICommandBuffer* commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const RHIRect2D* pScissors) override;
    void cmdBindVertexBuffersPFN( RHICommandBuffer* commandBuffer, uint32_t firstBinding, uint32_t bindingCount, RHIBuffer* const* pBuffers, const RHIDeviceSize* pOffsets) override;
    void cmdBindIndexBufferPFN(RHICommandBuffer* commandBuffer, RHIBuffer* buffer, RHIDeviceSize offset, RHIIndexType indexType) override;
    void cmdBindDescriptorSetsPFN( RHICommandBuffer* commandBuffer, RHIPipelineBindPoint pipelineBindPoint, RHIPipelineLayout* layout, uint32_t firstSet, uint32_t descriptorSetCount, const RHIDescriptorSet* const* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) override;
    void cmdDrawIndexedPFN(RHICommandBuffer* commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) override;
    void cmdClearAttachmentsPFN(RHICommandBuffer* commandBuffer, uint32_t attachmentCount, const RHIClearAttachment* pAttachments, uint32_t rectCount, const RHIClearRect* pRects) override;
    bool beginCommandBuffer(RHICommandBuffer* commandBuffer, const RHICommandBufferBeginInfo* pBeginInfo) override;
    void cmdCopyImageToBuffer(RHICommandBuffer* commandBuffer, RHIImage* srcImage, RHIImageLayout srcImageLayout, RHIBuffer* dstBuffer, uint32_t regionCount, const RHIBufferImageCopy* pRegions) override;
    using RHI::cmdCopyImageToImage;
    void cmdCopyImageToImage(RHICommandBuffer* commandBuffer, RHIImage* srcImage, RHIImage* dstImage, uint32_t regionCount, const RHIImageBlit* pRegions) override;
    void cmdCopyBuffer(RHICommandBuffer* commandBuffer, RHIBuffer* srcBuffer, RHIBuffer* dstBuffer, uint32_t regionCount, RHIBufferCopy* pRegions) override;
    void cmdDraw(RHICommandBuffer* commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
    void cmdDispatch(RHICommandBuffer* commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;
    void cmdDispatchIndirect(RHICommandBuffer* commandBuffer, RHIBuffer* buffer, RHIDeviceSize offset) override;
    RHIRayTracingCapabilities getRayTracingCapabilities() const override;
    bool createAccelerationStructure(const RHIAccelerationStructureBuildDesc* build_desc,
                                     RHIAccelerationStructure*& acceleration_structure) override;
    bool buildAccelerationStructure(RHICommandBuffer* command_buffer,
                                    const RHIAccelerationStructureBuildDesc* build_desc,
                                    RHIAccelerationStructure* acceleration_structure) override;
    bool createRayTracingPipeline(const RHIRayTracingPipelineCreateInfo* create_info,
                                  RHIPipeline*& pipeline) override;
    bool createShaderBindingTable(const RHIShaderBindingTableCreateInfo* create_info,
                                  RHIShaderBindingTable*& shader_binding_table) override;
    void cmdTraceRays(RHICommandBuffer* command_buffer, const RHIRayTracingDispatchDesc* dispatch_desc) override;
    void destroyAccelerationStructure(RHIAccelerationStructure*& acceleration_structure) override;
    void destroyShaderBindingTable(RHIShaderBindingTable*& shader_binding_table) override;
    void cmdPipelineBarrier(RHICommandBuffer* commandBuffer, RHIPipelineStageFlags srcStageMask, RHIPipelineStageFlags dstStageMask, RHIDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const RHIMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const RHIBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const RHIImageMemoryBarrier* pImageMemoryBarriers) override;
    bool endCommandBuffer(RHICommandBuffer* commandBuffer) override;
    void updateDescriptorSets(uint32_t descriptorWriteCount, const RHIWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const RHICopyDescriptorSet* pDescriptorCopies) override;
    bool queueSubmit(RHIQueue* queue, uint32_t submitCount, const RHISubmitInfo* pSubmits, RHIFence* fence) override;
    bool queueWaitIdle(RHIQueue* queue) override;
    void resetCommandPool() override;
    void waitForFences() override;
    void getPhysicalDeviceProperties(RHIPhysicalDeviceProperties* pProperties) override;
    RHICommandBuffer* getCurrentCommandBuffer() const override;
    RHICommandBuffer* const* getCommandBufferList() const override;
    RHICommandPool* getCommandPoor() const override;
    RHIDescriptorPool* getDescriptorPoor() const override;
    RHIFence* const* getFenceList() const override;
    QueueFamilyIndices getQueueFamilyIndices() const override;
    RHIQueue* getGraphicsQueue() const override;
    RHIQueue* getComputeQueue() const override;
    RHISwapChainDesc getSwapchainInfo() override;
    RHIDepthImageDesc getDepthImageInfo() const override;
    uint8_t getMaxFramesInFlight() const override;
    uint8_t getCurrentFrameIndex() const override;
    uint32_t getCurrentSwapchainImageIndex() const override;
    void setCurrentFrameIndex(uint8_t index) override;
    RHICommandBuffer* beginSingleTimeCommands() override;
    void endSingleTimeCommands(RHICommandBuffer* command_buffer) override;
    bool prepareBeforePass(std::function<void()> passUpdateAfterRecreateSwapchain) override;
    void submitRendering(std::function<void()> passUpdateAfterRecreateSwapchain) override;
    void pushEvent(RHICommandBuffer* commond_buffer, const char* name, const float* color) override;
    void popEvent(RHICommandBuffer* commond_buffer) override;
    void clearSwapchain() override;
    void destroyDefaultSampler(RHIDefaultSamplerType type) override;
    void destroyMipmappedSampler() override;
    void destroyShaderModule(RHIShader* shader) override;
    void destroySemaphore(RHISemaphore* semaphore) override;
    void destroySampler(RHISampler* sampler) override;
    void destroyInstance(RHIInstance* instance) override;
    void destroyImageView(RHIImageView* imageView) override;
    void destroyImage(RHIImage* image) override;
    void destroyFramebuffer(RHIFramebuffer* framebuffer) override;
    void destroyFence(RHIFence* fence) override;
    void destroyDevice() override;
    void destroyCommandPool(RHICommandPool* commandPool) override;
    void destroyBuffer(RHIBuffer* &buffer) override;
    void freeCommandBuffers(RHICommandPool* commandPool, uint32_t commandBufferCount, RHICommandBuffer* pCommandBuffers) override;
    void freeAllocation(RHIAllocation*& allocation) override;
    void freeMemory(RHIDeviceMemory* &memory) override;
    bool mapMemory(RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size, RHIMemoryMapFlags flags, void** ppData) override;
    void unmapMemory(RHIDeviceMemory* memory) override;
    void invalidateMappedMemoryRanges(void* pNext, RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size) override;
    void flushMappedMemoryRanges(void* pNext, RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size) override;
    RHISemaphore*& getTextureCopySemaphore(uint32_t index) override;


    private:
#ifdef _WIN32
        static const uint32_t m_swapchain_buffer_count {3};

        void createDevice();
        void createCommandQueue();
        void createCommandObjects();
        void createSwapchain(HWND hWnd);
        void createRenderTargetViews();
        void createFence();
        bool ensureCommandBufferObjects(RHICommandBuffer* commandBuffer);
        ID3D12GraphicsCommandList* d3d12CommandListFor(RHICommandBuffer* commandBuffer) const;
        bool executeImmediateCommands(const std::function<void(ID3D12GraphicsCommandList*)>& record_commands);
        bool uploadTexture2D(RHIImage* image, const void* texture_pixels, uint32_t layer_count, uint32_t source_mip_levels);
        void bindFramebufferForSubpass(RHICommandBuffer* command_buffer, ID3D12GraphicsCommandList* command_list, const RHIRenderPassBeginInfo* pRenderPassBegin, uint32_t subpass_index, bool apply_load_ops);
        void resolvePendingTextureReadbacks();
        bool ensureDispatchCommandSignature();
        void waitForGpu();

        struct D3D12PendingTextureReadback
        {
            RHIBuffer* destination_buffer {nullptr};
            RHIDeviceSize destination_offset {0};
            uint32_t destination_row_pitch {0};
            uint32_t row_count {0};
            uint32_t row_size {0};
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
            ComPtr<ID3D12Resource> readback_buffer;
        };

        ComPtr<IDXGIFactory4> m_dxgi_factory;
        ComPtr<ID3D12Device> m_d3d12_device;
        ComPtr<ID3D12CommandQueue> m_d3d12_command_queue;
        ComPtr<ID3D12CommandAllocator> m_d3d12_command_allocator;
        ComPtr<ID3D12GraphicsCommandList> m_d3d12_command_list;
        ComPtr<IDXGISwapChain3> m_d3d12_swapchain;
        ComPtr<ID3D12DescriptorHeap> m_d3d12_rtv_heap;
        ComPtr<ID3D12DescriptorHeap> m_d3d12_dsv_heap;
        ComPtr<ID3D12DescriptorHeap> m_d3d12_cbv_srv_uav_heap;
        ComPtr<ID3D12DescriptorHeap> m_d3d12_sampler_heap;
        ComPtr<ID3D12DescriptorHeap> m_d3d12_cbv_srv_uav_cpu_heap;
        ComPtr<ID3D12DescriptorHeap> m_d3d12_sampler_cpu_heap;
        ComPtr<ID3D12CommandSignature> m_d3d12_dispatch_command_signature;
        std::array<ComPtr<ID3D12Resource>, m_swapchain_buffer_count> m_d3d12_render_targets;
        ComPtr<ID3D12Fence> m_d3d12_fence;

        HANDLE m_d3d12_fence_event {nullptr};
        uint64_t m_d3d12_fence_value {0};
        uint32_t m_d3d12_rtv_descriptor_size {0};
        uint32_t m_d3d12_dsv_descriptor_size {0};
        uint32_t m_d3d12_cbv_srv_uav_descriptor_size {0};
        uint32_t m_d3d12_sampler_descriptor_size {0};
        uint32_t m_d3d12_rtv_descriptor_capacity {0};
        uint32_t m_d3d12_dsv_descriptor_capacity {0};
        uint32_t m_d3d12_cbv_srv_uav_descriptor_capacity {0};
        uint32_t m_d3d12_sampler_descriptor_capacity {0};
        uint32_t m_d3d12_rtv_descriptor_next {0};
        uint32_t m_d3d12_dsv_descriptor_next {0};
        uint32_t m_d3d12_cbv_srv_uav_descriptor_next {0};
        uint32_t m_d3d12_transient_cbv_srv_uav_descriptor_next {0};
        uint32_t m_d3d12_sampler_descriptor_next {0};

        bool m_allow_tearing {false};

        uint32_t m_window_width {0};
        uint32_t m_window_height {0};
        std::vector<D3D12PendingTextureReadback> m_pending_texture_readbacks;
        std::vector<ComPtr<ID3D12Resource>> m_pending_upload_buffers;
#endif

        GLFWwindow* m_window {nullptr};
        RHIViewport m_viewport {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        RHIRect2D   m_scissor {};
        uint8_t m_current_frame_index {0};
        uint32_t m_current_swapchain_image_index {0};
        // RHI-facing wrappers owned by D3D12RHI. They back the frame loop and keep the existing RHI contract
        // without exposing ID3D12* objects outside the backend.
        std::array<RHICommandBuffer*, 3> m_frame_command_buffers {{nullptr, nullptr, nullptr}};
        RHICommandBuffer*                m_current_command_buffer {nullptr};
        RHICommandPool*                  m_default_command_pool {nullptr};
        RHIDescriptorPool*               m_default_descriptor_pool {nullptr};
        RHIQueue*                        m_graphics_queue {nullptr};
        RHIQueue*                        m_compute_queue {nullptr};
        std::array<RHIFence*, 3>         m_frame_fences {{nullptr, nullptr, nullptr}};
        RHISwapChainDesc  m_swapchain_desc {};
        RHIViewport       m_swapchain_viewport {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        RHIRect2D         m_swapchain_scissor {};
        std::vector<RHIImage*> m_owned_swapchain_images;
        std::vector<RHIImageView*> m_owned_swapchain_image_views;
        RHIDepthImageDesc m_depth_desc {};
        RHISampler*       m_linear_sampler {nullptr};
        RHISampler*       m_nearest_sampler {nullptr};
        std::map<uint32_t, RHISampler*> m_mipmap_sampler_map;
        bool              m_in_render_pass {false};
        bool              m_command_list_open {false};
        RHIPipeline*      m_bound_graphics_pipeline {nullptr};
        RHIRenderPass*    m_active_render_pass {nullptr};
        RHIFramebuffer*   m_active_framebuffer {nullptr};
        uint32_t          m_active_subpass_index {0};
        RHISemaphore*     m_texture_copy_semaphore {nullptr};
    };
} // namespace Piccolo
