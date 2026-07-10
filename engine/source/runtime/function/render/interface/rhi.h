#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "rhi_allocation.h"
#include "rhi_frame_retire.h"
#include "rhi_ray_tracing.h"
#include "rhi_struct.h"

#include "runtime/core/math/math.h"

namespace Piccolo
{
    static constexpr uint8_t k_rhi_max_frames_in_flight = 3;

    class WindowSystem;

    enum class RHIBackendType
    {
        Auto,
        Vulkan,
        D3D12
    };

    struct RHIInitInfo
    {
        std::shared_ptr<WindowSystem> window_system;
        RHIBackendType                requested_backend {RHIBackendType::Auto};
        bool                          allow_fallback_to_vulkan {true};
    };
    
    class RHI
    {
    public:
        virtual ~RHI() = 0;

        virtual void initialize(RHIInitInfo initialize_info) = 0;
        virtual void prepareContext() = 0;

        virtual bool isPointLightShadowEnabled() = 0;
        // allocate and create
        virtual bool allocateCommandBuffers(const RHICommandBufferAllocateInfo* pAllocateInfo, RHICommandBuffer* &pCommandBuffers) = 0;
        virtual bool allocateDescriptorSets(const RHIDescriptorSetAllocateInfo* pAllocateInfo, RHIDescriptorSet* &pDescriptorSets) = 0;
        virtual void freeDescriptorSets(RHIDescriptorPool* pool, uint32_t count, RHIDescriptorSet** sets) = 0;
        virtual void createSwapchain() = 0;
        virtual void recreateSwapchain() = 0;
        virtual void createSwapchainImageViews() = 0;
        virtual void createFramebufferImageAndView() = 0;
        virtual RHISampler* getOrCreateDefaultSampler(RHIDefaultSamplerType type) = 0;
        virtual RHISampler* getOrCreateMipmapSampler(uint32_t width, uint32_t height) = 0;
        virtual RHIShader* createShaderModule(const std::vector<unsigned char>& shader_code) = 0;
        virtual void createBuffer(RHIDeviceSize size, RHIBufferUsageFlags usage, RHIMemoryPropertyFlags properties, RHIBuffer* &buffer, RHIDeviceMemory* &buffer_memory) = 0;
        virtual void createBufferAndInitialize(RHIBufferUsageFlags usage, RHIMemoryPropertyFlags properties, RHIBuffer*& buffer, RHIDeviceMemory*& buffer_memory, RHIDeviceSize size, void* data = nullptr, int datasize = 0) = 0;
        // Pass-driven declaration for buffers that are shared across GPU execution domains
        // (e.g. graphics -> compute). Backends can use this to apply portable state handoff
        // normalization when binding SRV/UAV/CBV resources, without relying on heuristics.
        virtual void registerBufferCrossQueueDomains(RHIBuffer* buffer, RHICrossQueueDomainFlags domains)
        {
            (void)buffer;
            (void)domains;
        }
        virtual bool createBufferWithAllocation(
            const RHIBufferCreateInfo* pBufferCreateInfo,
            RHIMemoryPropertyFlags memoryPropertyFlags,
            RHIBuffer* &pBuffer,
            RHIAllocation*& pAllocation) = 0;
        virtual bool createBufferWithAlignment(
            const RHIBufferCreateInfo* pBufferCreateInfo,
            RHIMemoryPropertyFlags memoryPropertyFlags,
            RHIDeviceSize minAlignment,
            RHIBuffer* &pBuffer,
            RHIAllocation*& pAllocation) = 0;
        virtual void copyBuffer(RHIBuffer* srcBuffer, RHIBuffer* dstBuffer, RHIDeviceSize srcOffset, RHIDeviceSize dstOffset, RHIDeviceSize size) = 0;
        virtual void createImage(uint32_t image_width, uint32_t image_height, RHIFormat format, RHIImageTiling image_tiling, RHIImageUsageFlags image_usage_flags, RHIMemoryPropertyFlags memory_property_flags,
            RHIImage* &image, RHIDeviceMemory* &memory, RHIImageCreateFlags image_create_flags, uint32_t array_layers, uint32_t miplevels, const RHIClearValue* pOptimizedClear = nullptr) = 0;
        virtual void createImageView(RHIImage* image, RHIFormat format, RHIImageAspectFlags image_aspect_flags, RHIImageViewType view_type, uint32_t layout_count, uint32_t miplevels,
            RHIImageView* &image_view) = 0;
        virtual void createGlobalImage(RHIImage* &image, RHIImageView* &image_view, RHIAllocation*& image_allocation, uint32_t texture_image_width, uint32_t texture_image_height, void* texture_image_pixels, RHIFormat texture_image_format, uint32_t miplevels = 0) = 0;
        virtual void createCubeMap(RHIImage* &image, RHIImageView* &image_view, RHIAllocation*& image_allocation, uint32_t texture_image_width, uint32_t texture_image_height, std::array<void*, 6> texture_image_pixels, RHIFormat texture_image_format, uint32_t miplevels) = 0;
        virtual void createCommandPool() = 0;
        virtual bool createCommandPool(const RHICommandPoolCreateInfo* pCreateInfo, RHICommandPool*& pCommandPool) = 0;
        virtual bool createDescriptorPool(const RHIDescriptorPoolCreateInfo* pCreateInfo, RHIDescriptorPool* &pDescriptorPool) = 0;
        virtual bool createDescriptorSetLayout(const RHIDescriptorSetLayoutCreateInfo* pCreateInfo, RHIDescriptorSetLayout* &pSetLayout) = 0;
        virtual bool createFence(const RHIFenceCreateInfo* pCreateInfo, RHIFence* &pFence) = 0;
        bool createFence(RHIFence*& pFence, RHIFenceCreateFlags flags = 0)
        {
            RHIFenceCreateInfo create_info = makeRHIFenceCreateInfo(flags);
            return createFence(&create_info, pFence);
        }
        virtual bool createFramebuffer(const RHIFramebufferCreateInfo* pCreateInfo, RHIFramebuffer* &pFramebuffer) = 0;
        virtual bool createGraphicsPipelines(RHIPipelineCache* pipelineCache, uint32_t createInfoCount, const RHIGraphicsPipelineCreateInfo* pCreateInfos, RHIPipeline* &pPipelines) = 0;
        virtual bool createComputePipelines(RHIPipelineCache* pipelineCache, uint32_t createInfoCount, const RHIComputePipelineCreateInfo* pCreateInfos, RHIPipeline* &pPipelines) = 0;
        virtual bool createPipelineLayout(const RHIPipelineLayoutCreateInfo* pCreateInfo, RHIPipelineLayout* &pPipelineLayout) = 0;
        virtual bool createRenderPass(const RHIRenderPassCreateInfo* pCreateInfo, RHIRenderPass* &pRenderPass) = 0;
        virtual bool createSampler(const RHISamplerCreateInfo* pCreateInfo, RHISampler* &pSampler) = 0;
        virtual bool createSemaphore(const RHISemaphoreCreateInfo* pCreateInfo, RHISemaphore* &pSemaphore) = 0;

        // command and command write
        virtual bool waitForFencesPFN(uint32_t fenceCount, RHIFence* const* pFence, RHIBool32 waitAll, uint64_t timeout) = 0;
        virtual bool resetFencesPFN(uint32_t fenceCount, RHIFence* const* pFences) = 0;
        virtual bool resetCommandPoolPFN(RHICommandPool* commandPool, RHICommandPoolResetFlags flags) = 0;
        virtual bool beginCommandBufferPFN(RHICommandBuffer* commandBuffer, const RHICommandBufferBeginInfo* pBeginInfo) = 0;
        virtual bool endCommandBufferPFN(RHICommandBuffer* commandBuffer) = 0;
        virtual void cmdBeginRenderPassPFN(RHICommandBuffer* commandBuffer, const RHIRenderPassBeginInfo* pRenderPassBegin, RHISubpassContents contents) = 0;
        virtual void cmdNextSubpassPFN(RHICommandBuffer* commandBuffer, RHISubpassContents contents) = 0;
        virtual void cmdEndRenderPassPFN(RHICommandBuffer* commandBuffer) = 0;
        virtual void cmdBindPipelinePFN(RHICommandBuffer* commandBuffer, RHIPipelineBindPoint pipelineBindPoint, RHIPipeline* pipeline) = 0;
        virtual void cmdSetViewportPFN(RHICommandBuffer* commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const RHIViewport* pViewports) = 0;
        virtual void cmdSetScissorPFN(RHICommandBuffer* commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const RHIRect2D* pScissors) = 0;
        virtual void cmdBindVertexBuffersPFN(
            RHICommandBuffer* commandBuffer,
            uint32_t firstBinding,
            uint32_t bindingCount,
            RHIBuffer* const* pBuffers,
            const RHIDeviceSize* pOffsets) = 0;
        virtual void cmdBindIndexBufferPFN(RHICommandBuffer* commandBuffer, RHIBuffer* buffer, RHIDeviceSize offset, RHIIndexType indexType) = 0;
        virtual void cmdBindDescriptorSetsPFN(
            RHICommandBuffer* commandBuffer,
            RHIPipelineBindPoint pipelineBindPoint,
            RHIPipelineLayout* layout,
            uint32_t firstSet,
            uint32_t descriptorSetCount,
            const RHIDescriptorSet* const* pDescriptorSets,
            uint32_t dynamicOffsetCount,
            const uint32_t* pDynamicOffsets) = 0;
        virtual void cmdDrawIndexedPFN(RHICommandBuffer* commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) = 0;
        virtual void cmdClearAttachmentsPFN(RHICommandBuffer* commandBuffer, uint32_t attachmentCount, const RHIClearAttachment* pAttachments, uint32_t rectCount, const RHIClearRect* pRects) = 0;

        virtual bool beginCommandBuffer(RHICommandBuffer* commandBuffer, const RHICommandBufferBeginInfo* pBeginInfo) = 0;
        virtual void cmdCopyImageToBuffer(RHICommandBuffer* commandBuffer, RHIImage* srcImage, RHIImageLayout srcImageLayout, RHIBuffer* dstBuffer, uint32_t regionCount, const RHIBufferImageCopy* pRegions) = 0;
        virtual void cmdCopyImageToImage(RHICommandBuffer* commandBuffer, RHIImage* srcImage, RHIImage* dstImage, uint32_t regionCount, const RHIImageBlit* pRegions) = 0;
        virtual void cmdCopyImageToImage(RHICommandBuffer* commandBuffer, RHIImage* srcImage, RHIImageAspectFlagBits srcFlag, RHIImage* dstImage, RHIImageAspectFlagBits dstFlag, uint32_t width, uint32_t height)
        {
            RHIImageBlit region {};
            region.srcSubresource = {static_cast<RHIImageAspectFlags>(srcFlag), 0, 0, 1};
            region.srcOffsets[0]  = {0, 0, 0};
            region.srcOffsets[1]  = {static_cast<int32_t>(width), static_cast<int32_t>(height), 1};
            region.dstSubresource = {static_cast<RHIImageAspectFlags>(dstFlag), 0, 0, 1};
            region.dstOffsets[0]  = {0, 0, 0};
            region.dstOffsets[1]  = {static_cast<int32_t>(width), static_cast<int32_t>(height), 1};
            cmdCopyImageToImage(commandBuffer, srcImage, dstImage, 1, &region);
        }
        virtual void cmdCopyBuffer(RHICommandBuffer* commandBuffer, RHIBuffer* srcBuffer, RHIBuffer* dstBuffer, uint32_t regionCount, RHIBufferCopy* pRegions) = 0;
        virtual void cmdDraw(RHICommandBuffer* commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) = 0;
        virtual void cmdDispatch(RHICommandBuffer* commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;
        virtual void cmdDispatchIndirect(RHICommandBuffer* commandBuffer, RHIBuffer* buffer, RHIDeviceSize offset) = 0;
        virtual RHIRayTracingCapabilities getRayTracingCapabilities() const = 0;
        // Capability query used by the render layer to gate ray-traced features (e.g. path tracing,
        // GPU-skinned BLAS builds) without hard-coding backend-type checks. Backends that cannot run
        // ray tracing report Unsupported; use supportsPathTracing() when shader bytecode is also required.
        bool supportsRayTracing() const
        {
            return getRayTracingCapabilities().support_level == RHIRayTracingSupportLevel::Supported;
        }
        virtual bool createAccelerationStructure(const RHIAccelerationStructureBuildDesc* build_desc,
                                                 RHIAccelerationStructure*& acceleration_structure) = 0;
        virtual bool buildAccelerationStructure(RHICommandBuffer* command_buffer,
                                                const RHIAccelerationStructureBuildDesc* build_desc,
                                                RHIAccelerationStructure* acceleration_structure) = 0;
        virtual bool createRayTracingPipeline(const RHIRayTracingPipelineCreateInfo* create_info,
                                              RHIPipeline*& pipeline) = 0;
        virtual bool createShaderBindingTable(const RHIShaderBindingTableCreateInfo* create_info,
                                              RHIShaderBindingTable*& shader_binding_table) = 0;
        virtual void cmdTraceRays(RHICommandBuffer* command_buffer,
                                  const RHIRayTracingDispatchDesc* dispatch_desc) = 0;
        virtual void destroyAccelerationStructure(RHIAccelerationStructure*& acceleration_structure) = 0;
        virtual void destroyShaderBindingTable(RHIShaderBindingTable*& shader_binding_table) = 0;
        // Release a ray tracing pipeline created by createRayTracingPipeline. Default no-op for backends
        // without ray tracing; the owning pass calls this to avoid leaking the GPU pipeline object.
        virtual void destroyRayTracingPipeline(RHIPipeline*& pipeline) { pipeline = nullptr; }
        virtual void cmdPipelineBarrier(RHICommandBuffer* commandBuffer, RHIPipelineStageFlags srcStageMask, RHIPipelineStageFlags dstStageMask, RHIDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const RHIMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const RHIBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const RHIImageMemoryBarrier* pImageMemoryBarriers) = 0;
        virtual bool endCommandBuffer(RHICommandBuffer* commandBuffer) = 0;
        virtual void updateDescriptorSets(uint32_t descriptorWriteCount, const RHIWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const RHICopyDescriptorSet* pDescriptorCopies) = 0;
        virtual bool queueSubmit(RHIQueue* queue, uint32_t submitCount, const RHISubmitInfo* pSubmits, RHIFence* fence) = 0;
        virtual bool queueWaitIdle(RHIQueue* queue) = 0;
        virtual RHIBackendType getBackendType() const = 0;

        virtual ClipSpaceConvention getClipSpaceConvention() const
        {
            return ClipSpaceConvention::YUpNDC;
        }

        virtual void resetCommandPool() = 0;
        virtual bool waitForFences() = 0;
        virtual void waitAllFramesInFlight() = 0;
        virtual void waitDeviceIdle() = 0;

        // query
        virtual void getPhysicalDeviceProperties(RHIPhysicalDeviceProperties* pProperties) = 0;
        virtual RHICommandBuffer* getCurrentCommandBuffer() const = 0;
        virtual RHICommandBuffer* const* getCommandBufferList() const = 0;
        virtual RHICommandPool* getCommandPoor() const = 0;
        virtual RHIDescriptorPool* getDescriptorPoor() const = 0;
        virtual RHIFence* const* getFenceList() const = 0;
        virtual RHIFence* const* getCopyFenceList() const = 0;
        virtual RHISemaphore*& getCopyReadySemaphore(uint32_t index) = 0;
        virtual RHISemaphore*& getCopyDoneSemaphore(uint32_t index) = 0;
        virtual void setCommandBufferComputeQueue(RHICommandBuffer* command_buffer, bool use_compute_queue) {}
        virtual QueueFamilyIndices getQueueFamilyIndices() const = 0;
        virtual RHIQueue* getGraphicsQueue() const = 0;
        virtual RHIQueue* getComputeQueue() const = 0;
        virtual RHISwapChainDesc getSwapchainInfo() = 0;
        virtual RHIDepthImageDesc getDepthImageInfo() const = 0;
        virtual void setViewport(float x, float y, float width, float height, float min_depth = 0.0f, float max_depth = 1.0f) = 0;
        virtual RHIViewport getViewport() const = 0;
        virtual uint8_t getMaxFramesInFlight() const = 0;
        virtual uint8_t getCurrentFrameIndex() const = 0;
        virtual uint32_t getCurrentSwapchainImageIndex() const = 0;
        virtual void setCurrentFrameIndex(uint8_t index) = 0;

        uint8_t getLastSubmittedFrameIndex() const
        {
            const uint8_t frames_in_flight = getMaxFramesInFlight();
            return static_cast<uint8_t>((getCurrentFrameIndex() + frames_in_flight - 1U) % frames_in_flight);
        }

        // command write
        virtual RHICommandBuffer* beginSingleTimeCommands() = 0;
        virtual void            endSingleTimeCommands(RHICommandBuffer* command_buffer) = 0;
        virtual bool prepareBeforePass(std::function<void()> passUpdateAfterRecreateSwapchain) = 0;
        virtual void submitRendering(std::function<void()> passUpdateAfterRecreateSwapchain) = 0;
        virtual void pushEvent(RHICommandBuffer* commond_buffer, const char* name, const float* color) = 0;
        virtual void popEvent(RHICommandBuffer* commond_buffer) = 0;

        // Debug-only: copies name into the backend wrapper and registers it with the validation layer.
        virtual void setDebugObjectName(RHIImage* image, const char* name) {}
        virtual void setDebugObjectName(RHIImageView* image_view, const char* name) {}
        virtual void setDebugObjectName(RHIDescriptorSet* descriptor_set, const char* name) {}
        virtual void setDebugObjectName(RHICommandBuffer* command_buffer, const char* name) {}
        virtual void setDebugObjectName(RHIPipeline* pipeline, const char* name) {}
        virtual void setDebugObjectName(RHIBuffer* buffer, const char* name) {}
        virtual void setDebugObjectName(RHIAccelerationStructure* acceleration_structure, const char* name) {}

        // Retire per-frame / resizeable resources. Destroy happens in waitForFences after GPU completes.
        void retireBuffer(uint8_t slot, RHIBuffer*& buffer, RHIDeviceMemory*& memory)
        {
            m_frame_retire_queue.retireBuffer(slot, buffer, memory);
        }

        void retireImage(uint8_t slot,
                         RHIImage*& image,
                         RHIImageView*& image_view,
                         RHIDeviceMemory*& memory)
        {
            m_frame_retire_queue.retireImage(slot, image, image_view, memory);
        }

        void retireAccelerationStructure(uint8_t slot, RHIAccelerationStructure*& acceleration_structure)
        {
            m_frame_retire_queue.retireAccelerationStructure(slot, acceleration_structure);
        }

        void flushAllRetiredResources() { m_frame_retire_queue.flushAllRetiredResources(this); }

        // destory
        virtual void clear() = 0;
        virtual void clearSwapchain() = 0;
        virtual void destroyDefaultSampler(RHIDefaultSamplerType type) = 0;
        virtual void destroyMipmappedSampler() = 0;
        virtual void destroyShaderModule(RHIShader*& shader) = 0;
        virtual void destroyPipeline(RHIPipeline*& pipeline) { pipeline = nullptr; }
        virtual void destroyPipelineLayout(RHIPipelineLayout*& pipeline_layout) { pipeline_layout = nullptr; }
        virtual void destroyRenderPass(RHIRenderPass*& render_pass) { render_pass = nullptr; }
        virtual void destroyDescriptorSetLayout(RHIDescriptorSetLayout*& descriptor_set_layout)
        {
            descriptor_set_layout = nullptr;
        }
        virtual void destroySemaphore(RHISemaphore*& semaphore) = 0;
        virtual void destroySampler(RHISampler*& sampler) = 0;
        virtual void destroyInstance(RHIInstance*& instance) = 0;
        virtual void destroyImageView(RHIImageView*& image_view) = 0;
        virtual void destroyImage(RHIImage*& image) = 0;
        virtual void destroyFramebuffer(RHIFramebuffer*& framebuffer) = 0;
        virtual void destroyFence(RHIFence*& fence) = 0;
        virtual void destroyDevice() = 0;
        virtual void destroyCommandPool(RHICommandPool*& command_pool) = 0;
        virtual void destroyBuffer(RHIBuffer* &buffer) = 0;
        virtual void destroyBufferWithAllocation(RHIBuffer*& buffer, RHIAllocation*& allocation) = 0;
        virtual void destroyImageWithAllocation(RHIImage*& image, RHIImageView*& image_view, RHIAllocation*& allocation) = 0;
        virtual void freeCommandBuffers(RHICommandPool* commandPool, uint32_t commandBufferCount, RHICommandBuffer* pCommandBuffers) = 0;

        // memory
        virtual void freeAllocation(RHIAllocation*& allocation) = 0;
        virtual void freeMemory(RHIDeviceMemory* &memory) = 0;
        virtual bool mapMemory(RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size, RHIMemoryMapFlags flags, void** ppData) = 0;
        virtual void unmapMemory(RHIDeviceMemory* memory) = 0;
        virtual void invalidateMappedMemoryRanges(void* pNext, RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size) = 0;
        virtual void flushMappedMemoryRanges(void* pNext, RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size) = 0;

        bool isDeviceLost() const { return m_device_lost; }

    protected:
        void markDeviceLost() { m_device_lost = true; }

        void onFrameSlotReady(uint8_t slot_index) { m_frame_retire_queue.flushRetiredResources(this, slot_index); }

        bool m_device_lost {false};
        RHIFrameRetireQueue m_frame_retire_queue;
    };

    inline RHI::~RHI() = default;

    inline void initializeRHIImageClearBinding(RHIImage& image,
                                              RHIImageUsageFlags usage,
                                              const RHIClearValue* pOptimizedClear = nullptr)
    {
        image.clear_binding   = RHI_CLEAR_BINDING_NONE;
        image.optimized_clear = {};

        if ((usage & RHI_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
        {
            image.clear_binding = RHI_CLEAR_BINDING_DEPTH_STENCIL;
            if (pOptimizedClear != nullptr)
            {
                image.optimized_clear = *pOptimizedClear;
            }
            else
            {
                image.optimized_clear.depthStencil = {1.0f, 0};
            }
        }
        else if ((usage & RHI_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0)
        {
            image.clear_binding = RHI_CLEAR_BINDING_COLOR;
            if (pOptimizedClear != nullptr)
            {
                image.optimized_clear = *pOptimizedClear;
            }
            else
            {
                image.optimized_clear.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
            }
        }
    }

    inline RHIClearValue getImageClearValue(const RHIImage* image)
    {
        RHIClearValue clear_value {};
        if (image != nullptr && image->clear_binding != RHI_CLEAR_BINDING_NONE)
        {
            clear_value = image->optimized_clear;
        }
        return clear_value;
    }

    inline void populateFramebufferClearValues(uint32_t attachment_count,
                                               RHIImageView* const* attachment_views,
                                               std::vector<RHIClearValue>& out_clear_values)
    {
        out_clear_values.resize(attachment_count);
        for (uint32_t attachment_index = 0; attachment_index < attachment_count; ++attachment_index)
        {
            const RHIImageView* attachment_view =
                attachment_views != nullptr ? attachment_views[attachment_index] : nullptr;
            out_clear_values[attachment_index] =
                getImageClearValue(attachment_view != nullptr ? attachment_view->image : nullptr);
        }
    }

} // namespace Piccolo
