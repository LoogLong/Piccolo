#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "rhi_allocation.h"
#include "rhi_ray_tracing.h"
#include "rhi_struct.h"

namespace Piccolo
{
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
        virtual void createSwapchain() = 0;
        virtual void recreateSwapchain() = 0;
        virtual void createSwapchainImageViews() = 0;
        virtual void createFramebufferImageAndView() = 0;
        virtual RHISampler* getOrCreateDefaultSampler(RHIDefaultSamplerType type) = 0;
        virtual RHISampler* getOrCreateMipmapSampler(uint32_t width, uint32_t height) = 0;
        virtual RHIShader* createShaderModule(const std::vector<unsigned char>& shader_code) = 0;
        virtual void createBuffer(RHIDeviceSize size, RHIBufferUsageFlags usage, RHIMemoryPropertyFlags properties, RHIBuffer* &buffer, RHIDeviceMemory* &buffer_memory) = 0;
        virtual void createBufferAndInitialize(RHIBufferUsageFlags usage, RHIMemoryPropertyFlags properties, RHIBuffer*& buffer, RHIDeviceMemory*& buffer_memory, RHIDeviceSize size, void* data = nullptr, int datasize = 0) = 0;
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
            RHIImage* &image, RHIDeviceMemory* &memory, RHIImageCreateFlags image_create_flags, uint32_t array_layers, uint32_t miplevels) = 0;
        virtual void createImageView(RHIImage* image, RHIFormat format, RHIImageAspectFlags image_aspect_flags, RHIImageViewType view_type, uint32_t layout_count, uint32_t miplevels,
            RHIImageView* &image_view) = 0;
        virtual void createGlobalImage(RHIImage* &image, RHIImageView* &image_view, RHIAllocation*& image_allocation, uint32_t texture_image_width, uint32_t texture_image_height, void* texture_image_pixels, RHIFormat texture_image_format, uint32_t miplevels = 0) = 0;
        virtual void createCubeMap(RHIImage* &image, RHIImageView* &image_view, RHIAllocation*& image_allocation, uint32_t texture_image_width, uint32_t texture_image_height, std::array<void*, 6> texture_image_pixels, RHIFormat texture_image_format, uint32_t miplevels) = 0;
        virtual void createCommandPool() = 0;
        virtual bool createCommandPool(const RHICommandPoolCreateInfo* pCreateInfo, RHICommandPool*& pCommandPool) = 0;
        virtual bool createDescriptorPool(const RHIDescriptorPoolCreateInfo* pCreateInfo, RHIDescriptorPool* &pDescriptorPool) = 0;
        virtual bool createDescriptorSetLayout(const RHIDescriptorSetLayoutCreateInfo* pCreateInfo, RHIDescriptorSetLayout* &pSetLayout) = 0;
        virtual bool createFence(const RHIFenceCreateInfo* pCreateInfo, RHIFence* &pFence) = 0;
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

        // Backend behaviour capability queries. The render layer uses these instead of hard-coding
        // getBackendType()==D3D12 so that pass/pipeline code stays backend-neutral. Defaults describe
        // the Vulkan execution model; backends override where their submission model differs.

        // Whether the normal/depth copy for the particle pass must be recorded inline in the current
        // command buffer before submitRendering (true), or can run afterwards on a dedicated copy
        // command buffer using the previous frame's resources (false, Vulkan).
        virtual bool requiresDepthNormalCopyBeforeSubmit() const { return false; }
        // Whether particle GPU compute uses dedicated per-frame compute command buffers and fences
        // (true), rather than being recorded into the shared graphics command buffer (false, Vulkan).
        virtual bool usesDedicatedComputeSubmission() const { return false; }
        // Whether the backend uses the Vulkan clip-space convention (Y-down NDC, [0,1] depth) for
        // projection matrices. D3D12 uses [0,1] depth with Y-up, so it returns false (the default).
        virtual bool usesVulkanClipSpace() const { return false; }

        virtual void resetCommandPool() = 0;
        virtual void waitForFences() = 0;

        // query
        virtual void getPhysicalDeviceProperties(RHIPhysicalDeviceProperties* pProperties) = 0;
        virtual RHICommandBuffer* getCurrentCommandBuffer() const = 0;
        virtual RHICommandBuffer* const* getCommandBufferList() const = 0;
        virtual RHICommandPool* getCommandPoor() const = 0;
        virtual RHIDescriptorPool* getDescriptorPoor() const = 0;
        virtual RHIFence* const* getFenceList() const = 0;
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

        // command write
        virtual RHICommandBuffer* beginSingleTimeCommands() = 0;
        virtual void            endSingleTimeCommands(RHICommandBuffer* command_buffer) = 0;
        virtual bool prepareBeforePass(std::function<void()> passUpdateAfterRecreateSwapchain) = 0;
        virtual void submitRendering(std::function<void()> passUpdateAfterRecreateSwapchain) = 0;
        virtual void pushEvent(RHICommandBuffer* commond_buffer, const char* name, const float* color) = 0;
        virtual void popEvent(RHICommandBuffer* commond_buffer) = 0;

        // destory
        virtual void clear() = 0;
        virtual void clearSwapchain() = 0;
        virtual void destroyDefaultSampler(RHIDefaultSamplerType type) = 0;
        virtual void destroyMipmappedSampler() = 0;
        virtual void destroyShaderModule(RHIShader* shader) = 0;
        virtual void destroySemaphore(RHISemaphore* semaphore) = 0;
        virtual void destroySampler(RHISampler* sampler) = 0;
        virtual void destroyInstance(RHIInstance* instance) = 0;
        virtual void destroyImageView(RHIImageView* imageView) = 0;
        virtual void destroyImage(RHIImage* image) = 0;
        virtual void destroyFramebuffer(RHIFramebuffer* framebuffer) = 0;
        virtual void destroyFence(RHIFence* fence) = 0;
        virtual void destroyDevice() = 0;
        virtual void destroyCommandPool(RHICommandPool* commandPool) = 0;
        virtual void destroyBuffer(RHIBuffer* &buffer) = 0;
        virtual void freeCommandBuffers(RHICommandPool* commandPool, uint32_t commandBufferCount, RHICommandBuffer* pCommandBuffers) = 0;

        // memory
        virtual void freeAllocation(RHIAllocation*& allocation) = 0;
        virtual void freeMemory(RHIDeviceMemory* &memory) = 0;
        virtual bool mapMemory(RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size, RHIMemoryMapFlags flags, void** ppData) = 0;
        virtual void unmapMemory(RHIDeviceMemory* memory) = 0;
        virtual void invalidateMappedMemoryRanges(void* pNext, RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size) = 0;
        virtual void flushMappedMemoryRanges(void* pNext, RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size) = 0;

        //semaphores
        virtual RHISemaphore* &getTextureCopySemaphore(uint32_t index) = 0;

    private:
    };

    inline RHI::~RHI() = default;
} // namespace Piccolo
