#include "runtime/function/render/interface/vulkan/vulkan_rhi.h"
#include "runtime/function/render/interface/vulkan/vulkan_util.h"
#include "runtime/function/render/interface/vulkan/vulkan_rhi_resource.h"

#include "runtime/function/render/window_system.h"
#include "runtime/core/base/macro.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

// https://gcc.gnu.org/onlinedocs/cpp/Stringizing.html
#define PICCOLO_XSTR(s) PICCOLO_STR(s)
#define PICCOLO_STR(s) #s

#if defined(__GNUC__)
// https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html
#if defined(__linux__)
#include <stdlib.h>
#elif defined(__MACH__)
// https://developer.apple.com/library/archive/documentation/Porting/Conceptual/PortingUnix/compiling/compiling.html
#include <stdlib.h>
#else
#error Unknown Platform
#endif
#elif defined(_MSC_VER)
// https://docs.microsoft.com/en-us/cpp/preprocessor/predefined-macros
#include <sdkddkver.h>
#define WIN32_LEAN_AND_MEAN 1
#define NOGDICAPMASKS 1
#define NOVIRTUALKEYCODES 1
#define NOWINMESSAGES 1
#define NOWINSTYLES 1
#define NOSYSMETRICS 1
#define NOMENUS 1
#define NOICONS 1
#define NOKEYSTATES 1
#define NOSYSCOMMANDS 1
#define NORASTEROPS 1
#define NOSHOWWINDOW 1
#define NOATOM 1
#define NOCLIPBOARD 1
#define NOCOLOR 1
#define NOCTLMGR 1
#define NODRAWTEXT 1
#define NOGDI 1
#define NOKERNEL 1
#define NOUSER 1
#define NONLS 1
#define NOMB 1
#define NOMEMMGR 1
#define NOMETAFILE 1
#define NOMINMAX 1
#define NOMSG 1
#define NOOPENFILE 1
#define NOSCROLL 1
#define NOSERVICE 1
#define NOSOUND 1
#define NOTEXTMETRIC 1
#define NOWH 1
#define NOWINOFFSETS 1
#define NOCOMM 1
#define NOKANJI 1
#define NOHELP 1
#define NOPROFILER 1
#define NODEFERWINDOWPOS 1
#define NOMCX 1
#include <Windows.h>
#else
#error Unknown Compiler
#endif

#include <cstring>
#include <cctype>
#include <cstdio>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace Piccolo
{
    namespace
    {
        // Vulkan-backed implementation of the opaque RHI ray tracing handles.
        struct VulkanRHIAccelerationStructure : public RHIAccelerationStructure, VulkanDebugNameStorage
        {
            VkAccelerationStructureKHR   handle {VK_NULL_HANDLE};
            VkBuffer                     as_buffer {VK_NULL_HANDLE};
            VkDeviceMemory               as_memory {VK_NULL_HANDLE};
            VkBuffer                     scratch_buffer {VK_NULL_HANDLE};
            VkDeviceMemory               scratch_memory {VK_NULL_HANDLE};
            VkBuffer                     instance_buffer {VK_NULL_HANDLE}; // TLAS only
            VkDeviceMemory               instance_memory {VK_NULL_HANDLE};
            void*                        instance_mapped {nullptr};
            VkDeviceAddress              device_address {0};
            RHIAccelerationStructureType type {RHIAccelerationStructureType::BottomLevel};

            void applyChildDebugNames(
                const std::function<void(uint64_t object_handle, VkObjectType object_type, const char* name)>& apply,
                const char* base_name) const
            {
                if (base_name == nullptr || base_name[0] == '\0')
                {
                    return;
                }

                auto apply_child = [&](const char* suffix, uint64_t object_handle, VkObjectType object_type) {
                    if (object_handle == 0)
                    {
                        return;
                    }
                    const std::string child_name = std::string(base_name) + suffix;
                    apply(object_handle, object_type, child_name.c_str());
                };

                apply_child(".as_buffer", reinterpret_cast<uint64_t>(as_buffer), VK_OBJECT_TYPE_BUFFER);
                apply_child(".scratch_buffer", reinterpret_cast<uint64_t>(scratch_buffer), VK_OBJECT_TYPE_BUFFER);
                apply_child(".instance_buffer", reinterpret_cast<uint64_t>(instance_buffer), VK_OBJECT_TYPE_BUFFER);
            }
        };

        struct VulkanRHIShaderBindingTable : public RHIShaderBindingTable
        {
            VkBuffer                        buffer {VK_NULL_HANDLE};
            VkDeviceMemory                  memory {VK_NULL_HANDLE};
            VkStridedDeviceAddressRegionKHR raygen_region {};
            VkStridedDeviceAddressRegionKHR miss_region {};
            VkStridedDeviceAddressRegionKHR hit_region {};
            VkStridedDeviceAddressRegionKHR callable_region {};
        };

        VkTransformMatrixKHR toVkTransformMatrix(const float* row_major_3x4)
        {
            VkTransformMatrixKHR m {};
            if (row_major_3x4 != nullptr)
            {
                std::memcpy(&m, row_major_3x4, sizeof(float) * 12);
            }
            else
            {
                m.matrix[0][0] = 1.0f;
                m.matrix[1][1] = 1.0f;
                m.matrix[2][2] = 1.0f;
            }
            return m;
        }

        VkDeviceSize alignUpSize(VkDeviceSize value, VkDeviceSize alignment)
        {
            return alignment == 0 ? value : (value + alignment - 1) & ~(alignment - 1);
        }
    } // namespace

    VulkanRHI::~VulkanRHI()
    {
        clear();
    }

    void VulkanRHI::initialize(RHIInitInfo init_info)
    {
        m_window = init_info.window_system->getWindow();

        std::array<int, 2> window_size = init_info.window_system->getWindowSize();

        m_viewport = {0.0f, 0.0f, (float)window_size[0], (float)window_size[1], 0.0f, 1.0f};
        m_scissor  = {{0, 0}, {(uint32_t)window_size[0], (uint32_t)window_size[1]}};

#ifndef NDEBUG
        m_enable_validation_Layers = true;
        m_enable_debug_utils_label = true;
#else
        m_enable_validation_Layers  = false;
        m_enable_debug_utils_label  = false;
#endif

#if defined(__GNUC__) && defined(__MACH__)
        m_enable_point_light_shadow = false;
#else
        m_enable_point_light_shadow = true;
#endif

#if defined(__GNUC__)
        // https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html
#if defined(__linux__)
        char const* vk_layer_path = PICCOLO_XSTR(PICCOLO_VK_LAYER_PATH);
        setenv("VK_LAYER_PATH", vk_layer_path, 1);
#elif defined(__MACH__)
        // https://developer.apple.com/library/archive/documentation/Porting/Conceptual/PortingUnix/compiling/compiling.html
        char const* vk_layer_path    = PICCOLO_XSTR(PICCOLO_VK_LAYER_PATH);
        char const* vk_icd_filenames = PICCOLO_XSTR(PICCOLO_VK_ICD_FILENAMES);
        setenv("VK_LAYER_PATH", vk_layer_path, 1);
        setenv("VK_ICD_FILENAMES", vk_icd_filenames, 1);
#else
#error Unknown Platform
#endif
#elif defined(_MSC_VER)
        // https://docs.microsoft.com/en-us/cpp/preprocessor/predefined-macros
        char const* vk_layer_path = PICCOLO_XSTR(PICCOLO_VK_LAYER_PATH);
        SetEnvironmentVariableA("VK_LAYER_PATH", vk_layer_path);
        SetEnvironmentVariableA("DISABLE_LAYER_AMD_SWITCHABLE_GRAPHICS_1", "1");
#else
#error Unknown Compiler
#endif

        createInstance();

        initializeDebugMessenger();

        createWindowSurface();

        initializePhysicalDevice();

        createLogicalDevice();

        createCommandPool();

        createCommandBuffers();

        createDescriptorPool();

        createSyncPrimitives();

        createSwapchain();

        createSwapchainImageViews();

        createFramebufferImageAndView();

        createAssetAllocator();
    }

    void VulkanRHI::prepareContext()
    {
        m_vk_current_command_buffer = m_vk_command_buffers[m_current_frame_index];
        ((VulkanCommandBuffer*)m_current_command_buffer)->setResource(m_vk_current_command_buffer);
    }

    void VulkanRHI::clear()
    {
        if (m_device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_device);

            destroyDefaultSampler(Default_Sampler_Linear);
            destroyDefaultSampler(Default_Sampler_Nearest);
            destroyMipmappedSampler();

            for (auto framebuffer : m_swapchain_framebuffers)
            {
                vkDestroyFramebuffer(m_device, framebuffer, nullptr);
            }
            m_swapchain_framebuffers.clear();

            if (m_swapchain != VK_NULL_HANDLE)
            {
                for (auto* image_view : m_swapchain_imageviews)
                {
                    if (image_view != nullptr)
                    {
                        vkDestroyImageView(m_device, ((VulkanImageView*)image_view)->getResource(), nullptr);
                        delete image_view;
                    }
                }
                m_swapchain_imageviews.clear();
                vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
                m_swapchain = VK_NULL_HANDLE;
            }

            if (m_depth_image_view != nullptr && ((VulkanImageView*)m_depth_image_view)->getResource() != VK_NULL_HANDLE)
            {
                destroyImageView(m_depth_image_view);
            }
            if (m_depth_image != nullptr && ((VulkanImage*)m_depth_image)->getResource() != VK_NULL_HANDLE)
            {
                vkDestroyImage(m_device, ((VulkanImage*)m_depth_image)->getResource(), nullptr);
                ((VulkanImage*)m_depth_image)->setResource(VK_NULL_HANDLE);
            }
            if (m_depth_image_memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(m_device, m_depth_image_memory, nullptr);
                m_depth_image_memory = VK_NULL_HANDLE;
            }

            for (uint32_t i = 0; i < k_max_frames_in_flight; ++i)
            {
                if (m_image_available_for_render_semaphores[i] != VK_NULL_HANDLE)
                {
                    vkDestroySemaphore(m_device, m_image_available_for_render_semaphores[i], nullptr);
                    m_image_available_for_render_semaphores[i] = VK_NULL_HANDLE;
                }
                if (m_image_finished_for_presentation_semaphores[i] != VK_NULL_HANDLE)
                {
                    vkDestroySemaphore(m_device, m_image_finished_for_presentation_semaphores[i], nullptr);
                    m_image_finished_for_presentation_semaphores[i] = VK_NULL_HANDLE;
                }
                if (m_copy_ready_semaphores[i] != nullptr)
                {
                    vkDestroySemaphore(m_device,
                                       ((VulkanSemaphore*)m_copy_ready_semaphores[i])->getResource(),
                                       nullptr);
                    delete m_copy_ready_semaphores[i];
                    m_copy_ready_semaphores[i] = nullptr;
                }
                if (m_copy_done_semaphores[i] != nullptr)
                {
                    vkDestroySemaphore(m_device,
                                       ((VulkanSemaphore*)m_copy_done_semaphores[i])->getResource(),
                                       nullptr);
                    delete m_copy_done_semaphores[i];
                    m_copy_done_semaphores[i] = nullptr;
                }
                if (m_is_frame_in_flight_fences[i] != VK_NULL_HANDLE)
                {
                    vkDestroyFence(m_device, m_is_frame_in_flight_fences[i], nullptr);
                    m_is_frame_in_flight_fences[i] = VK_NULL_HANDLE;
                }
                if (m_copy_fences[i] != VK_NULL_HANDLE)
                {
                    vkDestroyFence(m_device, m_copy_fences[i], nullptr);
                    m_copy_fences[i] = VK_NULL_HANDLE;
                }
                delete m_rhi_is_frame_in_flight_fences[i];
                m_rhi_is_frame_in_flight_fences[i] = nullptr;
                delete m_rhi_copy_fences[i];
                m_rhi_copy_fences[i] = nullptr;
                delete m_command_buffers[i];
                m_command_buffers[i] = nullptr;
                if (m_command_pools[i] != VK_NULL_HANDLE)
                {
                    vkDestroyCommandPool(m_device, m_command_pools[i], nullptr);
                    m_command_pools[i] = VK_NULL_HANDLE;
                }
            }

            delete m_current_command_buffer;
            m_current_command_buffer = nullptr;

            if (m_rhi_command_pool != nullptr)
            {
                destroyCommandPool(m_rhi_command_pool);
                delete m_rhi_command_pool;
                m_rhi_command_pool = nullptr;
            }

            if (m_vk_descriptor_pool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(m_device, m_vk_descriptor_pool, nullptr);
                m_vk_descriptor_pool = VK_NULL_HANDLE;
            }
            delete m_descriptor_pool;
            m_descriptor_pool = nullptr;

            delete static_cast<VulkanQueue*>(m_graphics_queue);
            m_graphics_queue = nullptr;
            delete static_cast<VulkanQueue*>(m_compute_queue);
            m_compute_queue = nullptr;

            if (m_assets_allocator != VK_NULL_HANDLE)
            {
                vmaDestroyAllocator(m_assets_allocator);
                m_assets_allocator = VK_NULL_HANDLE;
            }

            destroyDevice();
            m_device = VK_NULL_HANDLE;
        }

        if (m_surface != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
            m_surface = VK_NULL_HANDLE;
        }

        if (m_enable_validation_Layers && m_debug_messenger != nullptr && m_instance != VK_NULL_HANDLE)
        {
            destroyDebugUtilsMessengerEXT(m_instance, m_debug_messenger, nullptr);
            m_debug_messenger = nullptr;
        }

        if (m_instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }

        m_physical_device = VK_NULL_HANDLE;
        m_present_queue   = VK_NULL_HANDLE;
        m_ray_tracing_enabled = false;
        m_ray_tracing_capabilities.support_level = RHIRayTracingSupportLevel::Unsupported;
    }

    bool VulkanRHI::waitForFences()
    {
        VkFence fences[2] = {m_is_frame_in_flight_fences[m_current_frame_index],
                           m_copy_fences[m_current_frame_index]};
        VkResult res_wait_for_fences = _vkWaitForFences(m_device, 2, fences, VK_TRUE, UINT64_MAX);
        if (VK_SUCCESS != res_wait_for_fences)
        {
            if (res_wait_for_fences == VK_ERROR_DEVICE_LOST)
            {
                markDeviceLost();
            }
            LOG_ERROR("waitForFences failed (VkResult={}, frame_index={})",
                      static_cast<int32_t>(res_wait_for_fences),
                      static_cast<uint32_t>(m_current_frame_index));
            return false;
        }
        return true;
    }

    void VulkanRHI::waitAllFramesInFlight()
    {
        if (m_device == VK_NULL_HANDLE)
        {
            return;
        }

        VkResult res_wait_for_fences =
            _vkWaitForFences(m_device, k_max_frames_in_flight, m_is_frame_in_flight_fences, VK_TRUE, UINT64_MAX);
        if (VK_SUCCESS != res_wait_for_fences)
        {
            LOG_ERROR("waitAllFramesInFlight failed for frame fences");
        }

        res_wait_for_fences =
            _vkWaitForFences(m_device, k_max_frames_in_flight, m_copy_fences, VK_TRUE, UINT64_MAX);
        if (VK_SUCCESS != res_wait_for_fences)
        {
            LOG_ERROR("waitAllFramesInFlight failed for copy fences");
        }
    }

    void VulkanRHI::waitDeviceIdle()
    {
        if (m_device == VK_NULL_HANDLE)
        {
            return;
        }

        VkResult result = vkDeviceWaitIdle(m_device);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("vkDeviceWaitIdle failed");
        }
    }

    bool VulkanRHI::waitForFences(uint32_t fenceCount, const RHIFence* const* pFences, RHIBool32 waitAll, uint64_t timeout)
    {
        //fence
        int fence_size = fenceCount;
        std::vector<VkFence> vk_fence_list(fence_size);
        for (int i = 0; i < fence_size; ++i)
        {
            const auto& rhi_fence_element = pFences[i];
            auto& vk_fence_element = vk_fence_list[i];

            vk_fence_element = ((VulkanFence*)rhi_fence_element)->getResource();
        };

        VkResult result = vkWaitForFences(m_device, fenceCount, vk_fence_list.data(), waitAll, timeout);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("waitForFences failed");
            return false;
        }
    }

    void VulkanRHI::getPhysicalDeviceProperties(RHIPhysicalDeviceProperties* pProperties)
    {
        VkPhysicalDeviceProperties vk_physical_device_properties;
        vkGetPhysicalDeviceProperties(m_physical_device, &vk_physical_device_properties);

        pProperties->apiVersion = vk_physical_device_properties.apiVersion;
        pProperties->driverVersion = vk_physical_device_properties.driverVersion;
        pProperties->vendorID = vk_physical_device_properties.vendorID;
        pProperties->deviceID = vk_physical_device_properties.deviceID;
        pProperties->deviceType = (RHIPhysicalDeviceType)vk_physical_device_properties.deviceType;
        for (uint32_t i = 0; i < RHI_MAX_PHYSICAL_DEVICE_NAME_SIZE; i++)
        {
            pProperties->deviceName[i] = vk_physical_device_properties.deviceName[i];
        }
        for (uint32_t i = 0; i < RHI_UUID_SIZE; i++)
        {
            pProperties->pipelineCacheUUID[i] = vk_physical_device_properties.pipelineCacheUUID[i];
        }
        pProperties->sparseProperties.residencyStandard2DBlockShape = (VkBool32)vk_physical_device_properties.sparseProperties.residencyStandard2DBlockShape;
        pProperties->sparseProperties.residencyStandard2DMultisampleBlockShape = (VkBool32)vk_physical_device_properties.sparseProperties.residencyStandard2DMultisampleBlockShape;
        pProperties->sparseProperties.residencyStandard3DBlockShape = (VkBool32)vk_physical_device_properties.sparseProperties.residencyStandard3DBlockShape;
        pProperties->sparseProperties.residencyAlignedMipSize = (VkBool32)vk_physical_device_properties.sparseProperties.residencyAlignedMipSize;
        pProperties->sparseProperties.residencyNonResidentStrict = (VkBool32)vk_physical_device_properties.sparseProperties.residencyNonResidentStrict;

        pProperties->limits.maxImageDimension1D = vk_physical_device_properties.limits.maxImageDimension1D;
        pProperties->limits.maxImageDimension2D = vk_physical_device_properties.limits.maxImageDimension2D;
        pProperties->limits.maxImageDimension3D = vk_physical_device_properties.limits.maxImageDimension3D;
        pProperties->limits.maxImageDimensionCube = vk_physical_device_properties.limits.maxImageDimensionCube;
        pProperties->limits.maxImageArrayLayers = vk_physical_device_properties.limits.maxImageArrayLayers;
        pProperties->limits.maxTexelBufferElements = vk_physical_device_properties.limits.maxTexelBufferElements;
        pProperties->limits.maxUniformBufferRange = vk_physical_device_properties.limits.maxUniformBufferRange;
        pProperties->limits.maxStorageBufferRange = vk_physical_device_properties.limits.maxStorageBufferRange;
        pProperties->limits.maxPushConstantsSize = vk_physical_device_properties.limits.maxPushConstantsSize;
        pProperties->limits.maxMemoryAllocationCount = vk_physical_device_properties.limits.maxMemoryAllocationCount;
        pProperties->limits.maxSamplerAllocationCount = vk_physical_device_properties.limits.maxSamplerAllocationCount;
        pProperties->limits.bufferImageGranularity = (VkDeviceSize)vk_physical_device_properties.limits.bufferImageGranularity;
        pProperties->limits.sparseAddressSpaceSize = (VkDeviceSize)vk_physical_device_properties.limits.sparseAddressSpaceSize;
        pProperties->limits.maxBoundDescriptorSets = vk_physical_device_properties.limits.maxBoundDescriptorSets;
        pProperties->limits.maxPerStageDescriptorSamplers = vk_physical_device_properties.limits.maxPerStageDescriptorSamplers;
        pProperties->limits.maxPerStageDescriptorUniformBuffers = vk_physical_device_properties.limits.maxPerStageDescriptorUniformBuffers;
        pProperties->limits.maxPerStageDescriptorStorageBuffers = vk_physical_device_properties.limits.maxPerStageDescriptorStorageBuffers;
        pProperties->limits.maxPerStageDescriptorSampledImages = vk_physical_device_properties.limits.maxPerStageDescriptorSampledImages;
        pProperties->limits.maxPerStageDescriptorStorageImages = vk_physical_device_properties.limits.maxPerStageDescriptorStorageImages;
        pProperties->limits.maxPerStageDescriptorInputAttachments = vk_physical_device_properties.limits.maxPerStageDescriptorInputAttachments;
        pProperties->limits.maxPerStageResources = vk_physical_device_properties.limits.maxPerStageResources;
        pProperties->limits.maxDescriptorSetSamplers = vk_physical_device_properties.limits.maxDescriptorSetSamplers;
        pProperties->limits.maxDescriptorSetUniformBuffers = vk_physical_device_properties.limits.maxDescriptorSetUniformBuffers;
        pProperties->limits.maxDescriptorSetUniformBuffersDynamic = vk_physical_device_properties.limits.maxDescriptorSetUniformBuffersDynamic;
        pProperties->limits.maxDescriptorSetStorageBuffers = vk_physical_device_properties.limits.maxDescriptorSetStorageBuffers;
        pProperties->limits.maxDescriptorSetStorageBuffersDynamic = vk_physical_device_properties.limits.maxDescriptorSetStorageBuffersDynamic;
        pProperties->limits.maxDescriptorSetSampledImages = vk_physical_device_properties.limits.maxDescriptorSetSampledImages;
        pProperties->limits.maxDescriptorSetStorageImages = vk_physical_device_properties.limits.maxDescriptorSetStorageImages;
        pProperties->limits.maxDescriptorSetInputAttachments = vk_physical_device_properties.limits.maxDescriptorSetInputAttachments;
        pProperties->limits.maxVertexInputAttributes = vk_physical_device_properties.limits.maxVertexInputAttributes;
        pProperties->limits.maxVertexInputBindings = vk_physical_device_properties.limits.maxVertexInputBindings;
        pProperties->limits.maxVertexInputAttributeOffset = vk_physical_device_properties.limits.maxVertexInputAttributeOffset;
        pProperties->limits.maxVertexInputBindingStride = vk_physical_device_properties.limits.maxVertexInputBindingStride;
        pProperties->limits.maxVertexOutputComponents = vk_physical_device_properties.limits.maxVertexOutputComponents;
        pProperties->limits.maxTessellationGenerationLevel = vk_physical_device_properties.limits.maxTessellationGenerationLevel;
        pProperties->limits.maxTessellationPatchSize = vk_physical_device_properties.limits.maxTessellationPatchSize;
        pProperties->limits.maxTessellationControlPerVertexInputComponents = vk_physical_device_properties.limits.maxTessellationControlPerVertexInputComponents;
        pProperties->limits.maxTessellationControlPerVertexOutputComponents = vk_physical_device_properties.limits.maxTessellationControlPerVertexOutputComponents;
        pProperties->limits.maxTessellationControlPerPatchOutputComponents = vk_physical_device_properties.limits.maxTessellationControlPerPatchOutputComponents;
        pProperties->limits.maxTessellationControlTotalOutputComponents = vk_physical_device_properties.limits.maxTessellationControlTotalOutputComponents;
        pProperties->limits.maxTessellationEvaluationInputComponents = vk_physical_device_properties.limits.maxTessellationEvaluationInputComponents;
        pProperties->limits.maxTessellationEvaluationOutputComponents = vk_physical_device_properties.limits.maxTessellationEvaluationOutputComponents;
        pProperties->limits.maxGeometryShaderInvocations = vk_physical_device_properties.limits.maxGeometryShaderInvocations;
        pProperties->limits.maxGeometryInputComponents = vk_physical_device_properties.limits.maxGeometryInputComponents;
        pProperties->limits.maxGeometryOutputComponents = vk_physical_device_properties.limits.maxGeometryOutputComponents;
        pProperties->limits.maxGeometryOutputVertices = vk_physical_device_properties.limits.maxGeometryOutputVertices;
        pProperties->limits.maxGeometryTotalOutputComponents = vk_physical_device_properties.limits.maxGeometryTotalOutputComponents;
        pProperties->limits.maxFragmentInputComponents = vk_physical_device_properties.limits.maxFragmentInputComponents;
        pProperties->limits.maxFragmentOutputAttachments = vk_physical_device_properties.limits.maxFragmentOutputAttachments;
        pProperties->limits.maxFragmentDualSrcAttachments = vk_physical_device_properties.limits.maxFragmentDualSrcAttachments;
        pProperties->limits.maxFragmentCombinedOutputResources = vk_physical_device_properties.limits.maxFragmentCombinedOutputResources;
        pProperties->limits.maxComputeSharedMemorySize = vk_physical_device_properties.limits.maxComputeSharedMemorySize;
        for (uint32_t i = 0; i < 3; i++)
        {
            pProperties->limits.maxComputeWorkGroupCount[i] = vk_physical_device_properties.limits.maxComputeWorkGroupCount[i];
        }
        pProperties->limits.maxComputeWorkGroupInvocations = vk_physical_device_properties.limits.maxComputeWorkGroupInvocations;
        for (uint32_t i = 0; i < 3; i++)
        {
            pProperties->limits.maxComputeWorkGroupSize[i] = vk_physical_device_properties.limits.maxComputeWorkGroupSize[i];
        }
        pProperties->limits.subPixelPrecisionBits = vk_physical_device_properties.limits.subPixelPrecisionBits;
        pProperties->limits.subTexelPrecisionBits = vk_physical_device_properties.limits.subTexelPrecisionBits;
        pProperties->limits.mipmapPrecisionBits = vk_physical_device_properties.limits.mipmapPrecisionBits;
        pProperties->limits.maxDrawIndexedIndexValue = vk_physical_device_properties.limits.maxDrawIndexedIndexValue;
        pProperties->limits.maxDrawIndirectCount = vk_physical_device_properties.limits.maxDrawIndirectCount;
        pProperties->limits.maxSamplerLodBias = vk_physical_device_properties.limits.maxSamplerLodBias;
        pProperties->limits.maxSamplerAnisotropy = vk_physical_device_properties.limits.maxSamplerAnisotropy;
        pProperties->limits.maxViewports = vk_physical_device_properties.limits.maxViewports;
        for (uint32_t i = 0; i < 2; i++)
        {
            pProperties->limits.maxViewportDimensions[i] = vk_physical_device_properties.limits.maxViewportDimensions[i];
        }
        for (uint32_t i = 0; i < 2; i++)
        {
            pProperties->limits.viewportBoundsRange[i] = vk_physical_device_properties.limits.viewportBoundsRange[i];
        }
        pProperties->limits.viewportSubPixelBits = vk_physical_device_properties.limits.viewportSubPixelBits;
        pProperties->limits.minMemoryMapAlignment = vk_physical_device_properties.limits.minMemoryMapAlignment;
        pProperties->limits.minTexelBufferOffsetAlignment = (VkDeviceSize)vk_physical_device_properties.limits.minTexelBufferOffsetAlignment;
        pProperties->limits.minUniformBufferOffsetAlignment = (VkDeviceSize)vk_physical_device_properties.limits.minUniformBufferOffsetAlignment;
        pProperties->limits.minStorageBufferOffsetAlignment = (VkDeviceSize)vk_physical_device_properties.limits.minStorageBufferOffsetAlignment;
        pProperties->limits.minTexelOffset = vk_physical_device_properties.limits.minTexelOffset;
        pProperties->limits.maxTexelOffset = vk_physical_device_properties.limits.maxTexelOffset;
        pProperties->limits.minTexelGatherOffset = vk_physical_device_properties.limits.minTexelGatherOffset;
        pProperties->limits.maxTexelGatherOffset = vk_physical_device_properties.limits.maxTexelGatherOffset;
        pProperties->limits.minInterpolationOffset = vk_physical_device_properties.limits.minInterpolationOffset;
        pProperties->limits.maxInterpolationOffset = vk_physical_device_properties.limits.maxInterpolationOffset;
        pProperties->limits.subPixelInterpolationOffsetBits = vk_physical_device_properties.limits.subPixelInterpolationOffsetBits;
        pProperties->limits.maxFramebufferWidth = vk_physical_device_properties.limits.maxFramebufferWidth;
        pProperties->limits.maxFramebufferHeight = vk_physical_device_properties.limits.maxFramebufferHeight;
        pProperties->limits.maxFramebufferLayers = vk_physical_device_properties.limits.maxFramebufferLayers;
        pProperties->limits.framebufferColorSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.framebufferColorSampleCounts;
        pProperties->limits.framebufferDepthSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.framebufferDepthSampleCounts;
        pProperties->limits.framebufferStencilSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.framebufferStencilSampleCounts;
        pProperties->limits.framebufferNoAttachmentsSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.framebufferNoAttachmentsSampleCounts;
        pProperties->limits.maxColorAttachments = vk_physical_device_properties.limits.maxColorAttachments;
        pProperties->limits.sampledImageColorSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.sampledImageColorSampleCounts;
        pProperties->limits.sampledImageIntegerSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.sampledImageIntegerSampleCounts;
        pProperties->limits.sampledImageDepthSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.sampledImageDepthSampleCounts;
        pProperties->limits.sampledImageStencilSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.sampledImageStencilSampleCounts;
        pProperties->limits.storageImageSampleCounts = (VkSampleCountFlags)vk_physical_device_properties.limits.storageImageSampleCounts;
        pProperties->limits.maxSampleMaskWords = vk_physical_device_properties.limits.maxSampleMaskWords;
        pProperties->limits.timestampComputeAndGraphics = (VkBool32)vk_physical_device_properties.limits.timestampComputeAndGraphics;
        pProperties->limits.timestampPeriod = vk_physical_device_properties.limits.timestampPeriod;
        pProperties->limits.maxClipDistances = vk_physical_device_properties.limits.maxClipDistances;
        pProperties->limits.maxCullDistances = vk_physical_device_properties.limits.maxCullDistances;
        pProperties->limits.maxCombinedClipAndCullDistances = vk_physical_device_properties.limits.maxCombinedClipAndCullDistances;
        pProperties->limits.discreteQueuePriorities = vk_physical_device_properties.limits.discreteQueuePriorities;
        for (uint32_t i = 0; i < 2; i++)
        {
            pProperties->limits.pointSizeRange[i] = vk_physical_device_properties.limits.pointSizeRange[i];
        }
        for (uint32_t i = 0; i < 2; i++)
        {
            pProperties->limits.lineWidthRange[i] = vk_physical_device_properties.limits.lineWidthRange[i];
        }
        pProperties->limits.pointSizeGranularity = vk_physical_device_properties.limits.pointSizeGranularity;
        pProperties->limits.lineWidthGranularity = vk_physical_device_properties.limits.lineWidthGranularity;
        pProperties->limits.strictLines = (VkBool32)vk_physical_device_properties.limits.strictLines;
        pProperties->limits.standardSampleLocations = (VkBool32)vk_physical_device_properties.limits.standardSampleLocations;
        pProperties->limits.optimalBufferCopyOffsetAlignment = (VkDeviceSize)vk_physical_device_properties.limits.optimalBufferCopyOffsetAlignment;
        pProperties->limits.optimalBufferCopyRowPitchAlignment = (VkDeviceSize)vk_physical_device_properties.limits.optimalBufferCopyRowPitchAlignment;
        pProperties->limits.nonCoherentAtomSize = (VkDeviceSize)vk_physical_device_properties.limits.nonCoherentAtomSize;

    }

    void VulkanRHI::resetCommandPool()
    {
        VkResult res_reset_command_pool = _vkResetCommandPool(m_device, m_command_pools[m_current_frame_index], 0);
        if (VK_SUCCESS != res_reset_command_pool)
        {
            LOG_ERROR("failed to synchronize");
        }
    }

    bool VulkanRHI::prepareBeforePass(std::function<void()> passUpdateAfterRecreateSwapchain)
    {
        VkResult acquire_image_result =
            vkAcquireNextImageKHR(m_device,
                                  m_swapchain,
                                  UINT64_MAX,
                                  m_image_available_for_render_semaphores[m_current_frame_index],
                                  VK_NULL_HANDLE,
                                  &m_current_swapchain_image_index);

        if (VK_ERROR_OUT_OF_DATE_KHR == acquire_image_result)
        {
            recreateSwapchain();
            passUpdateAfterRecreateSwapchain();
            return RHI_SUCCESS;
        }
        else if (VK_SUBOPTIMAL_KHR == acquire_image_result)
        {
            recreateSwapchain();
            passUpdateAfterRecreateSwapchain();

            // NULL submit to wait semaphore
            VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT};
            VkSubmitInfo         submit_info   = {};
            submit_info.sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.waitSemaphoreCount     = 1;
            submit_info.pWaitSemaphores        = &m_image_available_for_render_semaphores[m_current_frame_index];
            submit_info.pWaitDstStageMask      = wait_stages;
            submit_info.commandBufferCount     = 0;
            submit_info.pCommandBuffers        = NULL;
            submit_info.signalSemaphoreCount   = 0;
            submit_info.pSignalSemaphores      = NULL;

            VkResult res_reset_fences = _vkResetFences(m_device, 1, &m_is_frame_in_flight_fences[m_current_frame_index]);
            if (VK_SUCCESS != res_reset_fences)
            {
                LOG_ERROR("_vkResetFences failed!");
                return false;
            }

            VkResult res_queue_submit =
                vkQueueSubmit(((VulkanQueue*)m_graphics_queue)->getResource(), 1, &submit_info, m_is_frame_in_flight_fences[m_current_frame_index]);
            if (VK_SUCCESS != res_queue_submit)
            {
                LOG_ERROR("vkQueueSubmit failed!");
                return false;
            }
            m_current_frame_index = (m_current_frame_index + 1) % k_max_frames_in_flight;
            return RHI_SUCCESS;
        }
        else
        {
            if (VK_SUCCESS != acquire_image_result)
            {
                LOG_ERROR("vkAcquireNextImageKHR failed!");
                return false;
            }
        }

        // begin command buffer
        VkCommandBufferBeginInfo command_buffer_begin_info {};
        command_buffer_begin_info.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_buffer_begin_info.flags            = 0;
        command_buffer_begin_info.pInheritanceInfo = nullptr;

        VkResult res_begin_command_buffer =
            _vkBeginCommandBuffer(m_vk_command_buffers[m_current_frame_index], &command_buffer_begin_info);

        if (VK_SUCCESS != res_begin_command_buffer)
        {
            LOG_ERROR("_vkBeginCommandBuffer failed!");
            return false;
        }
        return false;
    }

    void VulkanRHI::submitRendering(std::function<void()> passUpdateAfterRecreateSwapchain)
    {
        // end command buffer
        VkResult res_end_command_buffer = _vkEndCommandBuffer(m_vk_command_buffers[m_current_frame_index]);
        if (VK_SUCCESS != res_end_command_buffer)
        {
            LOG_ERROR("_vkEndCommandBuffer failed!");
            return;
        }

        VkSemaphore semaphores[2] = {((VulkanSemaphore*)m_copy_ready_semaphores[m_current_frame_index])->getResource(),
                                     m_image_finished_for_presentation_semaphores[m_current_frame_index]};

        VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo         submit_info   = {};
        submit_info.sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount     = 1;
        submit_info.pWaitSemaphores        = &m_image_available_for_render_semaphores[m_current_frame_index];
        submit_info.pWaitDstStageMask      = wait_stages;
        submit_info.commandBufferCount     = 1;
        submit_info.pCommandBuffers        = &m_vk_command_buffers[m_current_frame_index];
        submit_info.signalSemaphoreCount   = 2;
        submit_info.pSignalSemaphores      = semaphores;

        VkResult res_reset_fences = _vkResetFences(m_device, 1, &m_is_frame_in_flight_fences[m_current_frame_index]);

        if (VK_SUCCESS != res_reset_fences)
        {
            LOG_ERROR("_vkResetFences failed!");
            return;
        }
        VkResult res_queue_submit =
            vkQueueSubmit(((VulkanQueue*)m_graphics_queue)->getResource(), 1, &submit_info, m_is_frame_in_flight_fences[m_current_frame_index]);
        
        if (VK_SUCCESS != res_queue_submit)
        {
            LOG_ERROR("vkQueueSubmit failed!");
            return;
        }

        // present swapchain
        VkPresentInfoKHR present_info   = {};
        present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores    = &m_image_finished_for_presentation_semaphores[m_current_frame_index];
        present_info.swapchainCount     = 1;
        present_info.pSwapchains        = &m_swapchain;
        present_info.pImageIndices      = &m_current_swapchain_image_index;

        VkResult present_result = vkQueuePresentKHR(m_present_queue, &present_info);
        if (VK_ERROR_OUT_OF_DATE_KHR == present_result || VK_SUBOPTIMAL_KHR == present_result)
        {
            recreateSwapchain();
            passUpdateAfterRecreateSwapchain();
        }
        else
        {
            if (VK_SUCCESS != present_result)
            {
                LOG_ERROR("vkQueuePresentKHR failed!");
                return;
            }
        }

        m_current_frame_index = (m_current_frame_index + 1) % k_max_frames_in_flight;
    }

    RHICommandBuffer* VulkanRHI::beginSingleTimeCommands()
    {
        VkCommandBufferAllocateInfo allocInfo {};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool        = ((VulkanCommandPool*)m_rhi_command_pool)->getResource();
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer command_buffer;
        vkAllocateCommandBuffers(m_device, &allocInfo, &command_buffer);

        VkCommandBufferBeginInfo beginInfo {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        _vkBeginCommandBuffer(command_buffer, &beginInfo);

        RHICommandBuffer* rhi_command_buffer = new VulkanCommandBuffer();
        ((VulkanCommandBuffer*)rhi_command_buffer)->setResource(command_buffer);
        return rhi_command_buffer;
    }

    void VulkanRHI::endSingleTimeCommands(RHICommandBuffer* command_buffer)
    {
        VkCommandBuffer vk_command_buffer = ((VulkanCommandBuffer*)command_buffer)->getResource();
        _vkEndCommandBuffer(vk_command_buffer);

        VkSubmitInfo submitInfo {};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &vk_command_buffer;

        vkQueueSubmit(((VulkanQueue*)m_graphics_queue)->getResource(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(((VulkanQueue*)m_graphics_queue)->getResource());

        vkFreeCommandBuffers(m_device, ((VulkanCommandPool*)m_rhi_command_pool)->getResource(), 1, &vk_command_buffer);
        delete(command_buffer);
    }

    // validation layers
    bool VulkanRHI::checkValidationLayerSupport()
    {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char* layerName : m_validation_layers)
        {
            bool layerFound = false;

            for (const auto& layerProperties : availableLayers)
            {
                if (strcmp(layerName, layerProperties.layerName) == 0)
                {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound)
            {
                return false;
            }
        }

        return RHI_SUCCESS;
    }

    std::vector<const char*> VulkanRHI::getRequiredExtensions()
    {
        uint32_t     glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        if (m_enable_validation_Layers || m_enable_debug_utils_label)
        {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

#if defined(__MACH__)
        extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif

        return extensions;
    }

    // debug callback
    namespace
    {
        const char* vkObjectTypeName(VkObjectType object_type)
        {
            switch (object_type)
            {
                case VK_OBJECT_TYPE_INSTANCE: return "VK_OBJECT_TYPE_INSTANCE";
                case VK_OBJECT_TYPE_PHYSICAL_DEVICE: return "VK_OBJECT_TYPE_PHYSICAL_DEVICE";
                case VK_OBJECT_TYPE_DEVICE: return "VK_OBJECT_TYPE_DEVICE";
                case VK_OBJECT_TYPE_QUEUE: return "VK_OBJECT_TYPE_QUEUE";
                case VK_OBJECT_TYPE_SEMAPHORE: return "VK_OBJECT_TYPE_SEMAPHORE";
                case VK_OBJECT_TYPE_COMMAND_BUFFER: return "VK_OBJECT_TYPE_COMMAND_BUFFER";
                case VK_OBJECT_TYPE_FENCE: return "VK_OBJECT_TYPE_FENCE";
                case VK_OBJECT_TYPE_DEVICE_MEMORY: return "VK_OBJECT_TYPE_DEVICE_MEMORY";
                case VK_OBJECT_TYPE_BUFFER: return "VK_OBJECT_TYPE_BUFFER";
                case VK_OBJECT_TYPE_IMAGE: return "VK_OBJECT_TYPE_IMAGE";
                case VK_OBJECT_TYPE_EVENT: return "VK_OBJECT_TYPE_EVENT";
                case VK_OBJECT_TYPE_QUERY_POOL: return "VK_OBJECT_TYPE_QUERY_POOL";
                case VK_OBJECT_TYPE_BUFFER_VIEW: return "VK_OBJECT_TYPE_BUFFER_VIEW";
                case VK_OBJECT_TYPE_IMAGE_VIEW: return "VK_OBJECT_TYPE_IMAGE_VIEW";
                case VK_OBJECT_TYPE_SHADER_MODULE: return "VK_OBJECT_TYPE_SHADER_MODULE";
                case VK_OBJECT_TYPE_PIPELINE_CACHE: return "VK_OBJECT_TYPE_PIPELINE_CACHE";
                case VK_OBJECT_TYPE_PIPELINE_LAYOUT: return "VK_OBJECT_TYPE_PIPELINE_LAYOUT";
                case VK_OBJECT_TYPE_RENDER_PASS: return "VK_OBJECT_TYPE_RENDER_PASS";
                case VK_OBJECT_TYPE_PIPELINE: return "VK_OBJECT_TYPE_PIPELINE";
                case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT: return "VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT";
                case VK_OBJECT_TYPE_SAMPLER: return "VK_OBJECT_TYPE_SAMPLER";
                case VK_OBJECT_TYPE_DESCRIPTOR_POOL: return "VK_OBJECT_TYPE_DESCRIPTOR_POOL";
                case VK_OBJECT_TYPE_DESCRIPTOR_SET: return "VK_OBJECT_TYPE_DESCRIPTOR_SET";
                case VK_OBJECT_TYPE_FRAMEBUFFER: return "VK_OBJECT_TYPE_FRAMEBUFFER";
                case VK_OBJECT_TYPE_COMMAND_POOL: return "VK_OBJECT_TYPE_COMMAND_POOL";
                case VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR: return "VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR";
                default: return "VK_OBJECT_TYPE_UNKNOWN";
            }
        }

        std::string formatValidationMessage(const VkDebugUtilsMessengerCallbackDataEXT* callback_data)
        {
            std::ostringstream stream;
            stream << "[Vulkan Validation]";

            if (callback_data->pMessageIdName != nullptr && callback_data->pMessageIdName[0] != '\0')
            {
                stream << " " << callback_data->pMessageIdName;
            }

            const char* message = callback_data->pMessage != nullptr ? callback_data->pMessage : "(null)";
            stream << "\n  message: " << message;

            if (callback_data->cmdBufLabelCount > 0)
            {
                stream << "\n  cmd_buf_labels:";
                for (uint32_t i = 0; i < callback_data->cmdBufLabelCount; ++i)
                {
                    const char* label_name = callback_data->pCmdBufLabels[i].pLabelName;
                    stream << "\n    - " << (label_name != nullptr ? label_name : "(null)");
                }
            }

            if (callback_data->queueLabelCount > 0)
            {
                stream << "\n  queue_labels:";
                for (uint32_t i = 0; i < callback_data->queueLabelCount; ++i)
                {
                    const char* label_name = callback_data->pQueueLabels[i].pLabelName;
                    stream << "\n    - " << (label_name != nullptr ? label_name : "(null)");
                }
            }

            auto lookupEmbeddedName = [message](uint64_t object_handle) -> const char* {
                if (message == nullptr || object_handle == 0)
                {
                    return nullptr;
                }

                char handle_pattern[32];
                std::snprintf(handle_pattern, sizeof(handle_pattern), "0x%llx", static_cast<unsigned long long>(object_handle));
                const char* handle_pos = std::strstr(message, handle_pattern);
                if (handle_pos == nullptr)
                {
                    return nullptr;
                }

                const char* name_start = std::strchr(handle_pos, '[');
                if (name_start == nullptr)
                {
                    return nullptr;
                }
                ++name_start;
                const char* name_end = std::strchr(name_start, ']');
                if (name_end == nullptr || name_end == name_start)
                {
                    return nullptr;
                }

                static thread_local std::string embedded_name;
                embedded_name.assign(name_start, static_cast<size_t>(name_end - name_start));
                return embedded_name.c_str();
            };

            if (callback_data->objectCount > 0)
            {
                stream << "\n  objects:";
                for (uint32_t i = 0; i < callback_data->objectCount; ++i)
                {
                    const VkDebugUtilsObjectNameInfoEXT& object = callback_data->pObjects[i];
                    stream << "\n    - type=" << vkObjectTypeName(object.objectType)
                           << " handle=0x" << std::hex << object.objectHandle << std::dec;
                    const char* object_name = object.pObjectName;
                    if (object_name == nullptr || object_name[0] == '\0')
                    {
                        object_name = lookupEmbeddedName(object.objectHandle);
                    }
                    if (object_name != nullptr && object_name[0] != '\0')
                    {
                        stream << " name=" << object_name;
                    }
                }
            }

            if (message != nullptr)
            {
                bool printed_embedded = false;
                const char* scan = message;
                while (*scan != '\0')
                {
                    if (scan[0] == '0' && scan[1] == 'x')
                    {
                        const char* hex_start = scan + 2;
                        const char* cursor    = hex_start;
                        while (*cursor != '\0' && std::isxdigit(static_cast<unsigned char>(*cursor)))
                        {
                            ++cursor;
                        }
                        if (cursor != hex_start && *cursor == '[')
                        {
                            const char* name_start = cursor + 1;
                            const char* name_end   = std::strchr(name_start, ']');
                            if (name_end != nullptr && name_end != name_start)
                            {
                                if (!printed_embedded)
                                {
                                    stream << "\n  message_embedded_names:";
                                    printed_embedded = true;
                                }
                                stream << "\n    - handle=0x";
                                stream.write(hex_start, static_cast<std::streamsize>(cursor - hex_start));
                                stream << " name=";
                                stream.write(name_start, static_cast<std::streamsize>(name_end - name_start));
                            }
                            scan = name_end != nullptr ? name_end + 1 : cursor;
                            continue;
                        }
                    }
                    ++scan;
                }
            }

            return stream.str();
        }
    } // namespace

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT,
                                                        VkDebugUtilsMessageTypeFlagsEXT,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                        void*)
    {
        if (pCallbackData == nullptr)
        {
            return VK_FALSE;
        }

        const std::string formatted_message = formatValidationMessage(pCallbackData);
        std::cerr << formatted_message << std::endl;
        LOG_ERROR("{}", formatted_message);
        return VK_FALSE;
    }

    void VulkanRHI::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
    {
        createInfo       = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
    }

    void VulkanRHI::createInstance()
    {
        // validation layer will be enabled in debug mode
        if (m_enable_validation_Layers && !checkValidationLayerSupport())
        {
            LOG_ERROR("validation layers requested, but not available!");
        }

        // Vulkan 1.2 is required for buffer device address and descriptor indexing (core), which the
        // ray tracing path depends on. Ray tracing itself stays optional and is gated per-device.
        m_vulkan_api_version = VK_API_VERSION_1_2;

        // app info
        VkApplicationInfo appInfo {};
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "piccolo_renderer";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName        = "Piccolo";
        appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion         = m_vulkan_api_version;

        // create info
        VkInstanceCreateInfo instance_create_info {};
        instance_create_info.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_create_info.pApplicationInfo = &appInfo; // the appInfo is stored here

        auto extensions                              = getRequiredExtensions();
        instance_create_info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
        instance_create_info.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo {};
        if (m_enable_validation_Layers)
        {
            instance_create_info.enabledLayerCount   = static_cast<uint32_t>(m_validation_layers.size());
            instance_create_info.ppEnabledLayerNames = m_validation_layers.data();

            populateDebugMessengerCreateInfo(debugCreateInfo);
            instance_create_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
        }
        else
        {
            instance_create_info.enabledLayerCount = 0;
            instance_create_info.pNext             = nullptr;
        }

        // create m_vulkan_context._instance
        if (vkCreateInstance(&instance_create_info, nullptr, &m_instance) != VK_SUCCESS)
        {
            LOG_ERROR("vk create instance");
        }
    }

    void VulkanRHI::initializeDebugMessenger()
    {
        if (m_enable_validation_Layers)
        {
            VkDebugUtilsMessengerCreateInfoEXT createInfo;
            populateDebugMessengerCreateInfo(createInfo);
            if (VK_SUCCESS != createDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debug_messenger))
            {
                LOG_ERROR("failed to set up debug messenger!");
            }
        }

        if (m_enable_debug_utils_label)
        {
            _vkCmdBeginDebugUtilsLabelEXT =
                (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(m_instance, "vkCmdBeginDebugUtilsLabelEXT");
            _vkCmdEndDebugUtilsLabelEXT =
                (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(m_instance, "vkCmdEndDebugUtilsLabelEXT");
        }
    }

    void VulkanRHI::createWindowSurface()
    {
        if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS)
        {
            LOG_ERROR("glfwCreateWindowSurface failed!");
        }
    }

    void VulkanRHI::initializePhysicalDevice()
    {
        uint32_t physical_device_count;
        vkEnumeratePhysicalDevices(m_instance, &physical_device_count, nullptr);
        if (physical_device_count == 0)
        {
            LOG_ERROR("enumerate physical devices failed!");
        }
        else
        {
            // find one device that matches our requirement
            // or find which is the best
            std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
            vkEnumeratePhysicalDevices(m_instance, &physical_device_count, physical_devices.data());

            std::vector<std::pair<int, VkPhysicalDevice>> ranked_physical_devices;
            for (const auto& device : physical_devices)
            {
                VkPhysicalDeviceProperties physical_device_properties;
                vkGetPhysicalDeviceProperties(device, &physical_device_properties);
                int score = 0;

                if (physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                {
                    score += 1000;
                }
                else if (physical_device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
                {
                    score += 100;
                }

                ranked_physical_devices.push_back({score, device});
            }

            std::sort(ranked_physical_devices.begin(),
                      ranked_physical_devices.end(),
                      [](const std::pair<int, VkPhysicalDevice>& p1, const std::pair<int, VkPhysicalDevice>& p2) {
                          return p1 > p2;
                      });

            for (const auto& device : ranked_physical_devices)
            {
                if (isDeviceSuitable(device.second))
                {
                    m_physical_device = device.second;
                    break;
                }
            }

            if (m_physical_device == VK_NULL_HANDLE)
            {
                LOG_ERROR("failed to find suitable physical device");
            }
        }
    }

    // logical device (m_vulkan_context._device : graphic queue, present queue,
    // feature:samplerAnisotropy)
    void VulkanRHI::createLogicalDevice()
    {
        m_queue_indices = findQueueFamilies(m_physical_device);

        std::vector<VkDeviceQueueCreateInfo> queue_create_infos; // all queues that need to be created
        std::set<uint32_t>                   queue_families = {m_queue_indices.graphics_family.value(),
                                             m_queue_indices.present_family.value(),
                                             m_queue_indices.m_compute_family.value()};

        float queue_priority = 1.0f;
        for (uint32_t queue_family : queue_families) // for every queue family
        {
            // queue create info
            VkDeviceQueueCreateInfo queue_create_info {};
            queue_create_info.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_info.queueFamilyIndex = queue_family;
            queue_create_info.queueCount       = 1;
            queue_create_info.pQueuePriorities = &queue_priority;
            queue_create_infos.push_back(queue_create_info);
        }

        // physical device features
        VkPhysicalDeviceFeatures physical_device_features = {};

        physical_device_features.samplerAnisotropy = VK_TRUE;

        // support inefficient readback storage buffer
        physical_device_features.fragmentStoresAndAtomics = VK_TRUE;

        // support independent blending
        physical_device_features.independentBlend = VK_TRUE;

        // support geometry shader
        if (m_enable_point_light_shadow)
        {
            physical_device_features.geometryShader = VK_TRUE;
        }

        // Ray tracing is optional: only enable the extensions/features when the device supports them.
        // Otherwise the device is created exactly as before and the engine runs the raster path.
        m_ray_tracing_enabled = checkRayTracingSupport(m_physical_device);

        std::vector<char const*> enabled_device_extensions = m_device_extensions;

        // Feature chain used only when ray tracing is enabled (keeps the raster-only path unchanged).
        VkPhysicalDeviceFeatures2                        features2 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        VkPhysicalDeviceVulkan12Features                 vulkan12_features {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR    ray_tracing_pipeline_features {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
        VkPhysicalDeviceRayQueryFeaturesKHR              ray_query_features {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};

        if (m_ray_tracing_enabled)
        {
            enabled_device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            enabled_device_extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
            enabled_device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            enabled_device_extensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
            enabled_device_extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);

            vulkan12_features.bufferDeviceAddress                       = VK_TRUE;
            vulkan12_features.descriptorIndexing                        = VK_TRUE;
            vulkan12_features.runtimeDescriptorArray                    = VK_TRUE;
            vulkan12_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
            vulkan12_features.descriptorBindingPartiallyBound           = VK_TRUE;
            vulkan12_features.descriptorBindingVariableDescriptorCount  = VK_TRUE;

            acceleration_structure_features.accelerationStructure = VK_TRUE;
            ray_tracing_pipeline_features.rayTracingPipeline      = VK_TRUE;
            ray_query_features.rayQuery                           = VK_TRUE;

            features2.features               = physical_device_features;
            features2.pNext                  = &vulkan12_features;
            vulkan12_features.pNext          = &acceleration_structure_features;
            acceleration_structure_features.pNext = &ray_tracing_pipeline_features;
            ray_tracing_pipeline_features.pNext   = &ray_query_features;
        }

        // device create info
        VkDeviceCreateInfo device_create_info {};
        device_create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create_info.pQueueCreateInfos       = queue_create_infos.data();
        device_create_info.queueCreateInfoCount    = static_cast<uint32_t>(queue_create_infos.size());
        device_create_info.enabledExtensionCount   = static_cast<uint32_t>(enabled_device_extensions.size());
        device_create_info.ppEnabledExtensionNames = enabled_device_extensions.data();
        device_create_info.enabledLayerCount       = 0;
        if (m_ray_tracing_enabled)
        {
            // When using VkPhysicalDeviceFeatures2 in pNext, pEnabledFeatures must be null.
            device_create_info.pNext            = &features2;
            device_create_info.pEnabledFeatures = nullptr;
        }
        else
        {
            device_create_info.pEnabledFeatures = &physical_device_features;
        }

        if (vkCreateDevice(m_physical_device, &device_create_info, nullptr, &m_device) != VK_SUCCESS)
        {
            throw std::runtime_error("VulkanRHI: vkCreateDevice failed");
        }

        // initialize queues of this device
        VkQueue vk_graphics_queue;
        vkGetDeviceQueue(m_device, m_queue_indices.graphics_family.value(), 0, &vk_graphics_queue);
        m_graphics_queue = new VulkanQueue();
        ((VulkanQueue*)m_graphics_queue)->setResource(vk_graphics_queue);

        vkGetDeviceQueue(m_device, m_queue_indices.present_family.value(), 0, &m_present_queue);

        VkQueue vk_compute_queue;
        vkGetDeviceQueue(m_device, m_queue_indices.m_compute_family.value(), 0, &vk_compute_queue);
        m_compute_queue = new VulkanQueue();
        ((VulkanQueue*)m_compute_queue)->setResource(vk_compute_queue);

        // more efficient pointer
        _vkResetCommandPool      = (PFN_vkResetCommandPool)vkGetDeviceProcAddr(m_device, "vkResetCommandPool");
        _vkBeginCommandBuffer    = (PFN_vkBeginCommandBuffer)vkGetDeviceProcAddr(m_device, "vkBeginCommandBuffer");
        _vkEndCommandBuffer      = (PFN_vkEndCommandBuffer)vkGetDeviceProcAddr(m_device, "vkEndCommandBuffer");
        _vkCmdBeginRenderPass    = (PFN_vkCmdBeginRenderPass)vkGetDeviceProcAddr(m_device, "vkCmdBeginRenderPass");
        _vkCmdNextSubpass        = (PFN_vkCmdNextSubpass)vkGetDeviceProcAddr(m_device, "vkCmdNextSubpass");
        _vkCmdEndRenderPass      = (PFN_vkCmdEndRenderPass)vkGetDeviceProcAddr(m_device, "vkCmdEndRenderPass");
        _vkCmdBindPipeline       = (PFN_vkCmdBindPipeline)vkGetDeviceProcAddr(m_device, "vkCmdBindPipeline");
        _vkCmdSetViewport        = (PFN_vkCmdSetViewport)vkGetDeviceProcAddr(m_device, "vkCmdSetViewport");
        _vkCmdSetScissor         = (PFN_vkCmdSetScissor)vkGetDeviceProcAddr(m_device, "vkCmdSetScissor");
        _vkWaitForFences         = (PFN_vkWaitForFences)vkGetDeviceProcAddr(m_device, "vkWaitForFences");
        _vkResetFences           = (PFN_vkResetFences)vkGetDeviceProcAddr(m_device, "vkResetFences");
        _vkCmdDrawIndexed        = (PFN_vkCmdDrawIndexed)vkGetDeviceProcAddr(m_device, "vkCmdDrawIndexed");
        _vkCmdBindVertexBuffers  = (PFN_vkCmdBindVertexBuffers)vkGetDeviceProcAddr(m_device, "vkCmdBindVertexBuffers");
        _vkCmdBindIndexBuffer    = (PFN_vkCmdBindIndexBuffer)vkGetDeviceProcAddr(m_device, "vkCmdBindIndexBuffer");
        _vkCmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets)vkGetDeviceProcAddr(m_device, "vkCmdBindDescriptorSets");
        _vkCmdClearAttachments   = (PFN_vkCmdClearAttachments)vkGetDeviceProcAddr(m_device, "vkCmdClearAttachments");

        loadDebugUtilsDeviceFunctions();

        if (m_ray_tracing_enabled)
        {
            loadRayTracingFunctions();
            queryRayTracingProperties();
        }

        m_depth_image_format = (RHIFormat)findDepthFormat();
    }

    bool VulkanRHI::checkRayTracingSupport(VkPhysicalDevice physical_device) const
    {
        uint32_t extension_count = 0;
        vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> available(extension_count);
        vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extension_count, available.data());

        const char* required[] = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                                  VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
                                  VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
                                  VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME};
        for (const char* req : required)
        {
            bool found = false;
            for (const VkExtensionProperties& ext : available)
            {
                if (std::strcmp(ext.extensionName, req) == 0)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                return false;
            }
        }

        VkPhysicalDeviceFeatures2                        features2 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        VkPhysicalDeviceVulkan12Features                 vulkan12_features {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline_features {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
        features2.pNext                         = &vulkan12_features;
        vulkan12_features.pNext                 = &acceleration_structure_features;
        acceleration_structure_features.pNext   = &ray_tracing_pipeline_features;
        vkGetPhysicalDeviceFeatures2(physical_device, &features2);

        return vulkan12_features.bufferDeviceAddress == VK_TRUE &&
               acceleration_structure_features.accelerationStructure == VK_TRUE &&
               ray_tracing_pipeline_features.rayTracingPipeline == VK_TRUE;
    }

    void VulkanRHI::loadDebugUtilsDeviceFunctions()
    {
        if (!m_enable_debug_utils_label || m_device == VK_NULL_HANDLE)
        {
            return;
        }

        _vkSetDebugUtilsObjectNameEXT =
            (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(m_device, "vkSetDebugUtilsObjectNameEXT");
    }

    void VulkanRHI::applyDebugUtilsObjectName(uint64_t object_handle, VkObjectType object_type, const char* name)
    {
        if (!m_enable_debug_utils_label || object_handle == 0 || name == nullptr || name[0] == '\0' ||
            _vkSetDebugUtilsObjectNameEXT == nullptr || m_device == VK_NULL_HANDLE)
        {
            return;
        }

        VkDebugUtilsObjectNameInfoEXT name_info {};
        name_info.sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        name_info.objectType   = object_type;
        name_info.objectHandle = object_handle;
        name_info.pObjectName  = name;
        _vkSetDebugUtilsObjectNameEXT(m_device, &name_info);
    }

    void VulkanRHI::setDebugObjectName(RHIImage* image, const char* name)
    {
        if (image == nullptr || name == nullptr)
        {
            return;
        }

        auto* vk_image = static_cast<VulkanImage*>(image);
        vk_image->setDebugName(name);
        applyDebugUtilsObjectName(reinterpret_cast<uint64_t>(vk_image->getResource()),
                                  VK_OBJECT_TYPE_IMAGE,
                                  vk_image->debugNameCStr());
    }

    void VulkanRHI::setDebugObjectName(RHIImageView* image_view, const char* name)
    {
        if (image_view == nullptr || name == nullptr)
        {
            return;
        }

        auto* vk_image_view = static_cast<VulkanImageView*>(image_view);
        vk_image_view->setDebugName(name);
        applyDebugUtilsObjectName(reinterpret_cast<uint64_t>(vk_image_view->getResource()),
                                  VK_OBJECT_TYPE_IMAGE_VIEW,
                                  vk_image_view->debugNameCStr());
    }

    void VulkanRHI::setDebugObjectName(RHIDescriptorSet* descriptor_set, const char* name)
    {
        if (descriptor_set == nullptr || name == nullptr)
        {
            return;
        }

        auto* vk_descriptor_set = static_cast<VulkanDescriptorSet*>(descriptor_set);
        vk_descriptor_set->setDebugName(name);
        applyDebugUtilsObjectName(reinterpret_cast<uint64_t>(vk_descriptor_set->getResource()),
                                  VK_OBJECT_TYPE_DESCRIPTOR_SET,
                                  vk_descriptor_set->debugNameCStr());
    }

    void VulkanRHI::setDebugObjectName(RHICommandBuffer* command_buffer, const char* name)
    {
        if (command_buffer == nullptr || name == nullptr)
        {
            return;
        }

        auto* vk_command_buffer = static_cast<VulkanCommandBuffer*>(command_buffer);
        vk_command_buffer->setDebugName(name);
        applyDebugUtilsObjectName(reinterpret_cast<uint64_t>(vk_command_buffer->getResource()),
                                  VK_OBJECT_TYPE_COMMAND_BUFFER,
                                  vk_command_buffer->debugNameCStr());
    }

    void VulkanRHI::setDebugObjectName(RHIPipeline* pipeline, const char* name)
    {
        if (pipeline == nullptr || name == nullptr)
        {
            return;
        }

        auto* vk_pipeline = static_cast<VulkanPipeline*>(pipeline);
        vk_pipeline->setDebugName(name);
        applyDebugUtilsObjectName(reinterpret_cast<uint64_t>(vk_pipeline->getResource()),
                                  VK_OBJECT_TYPE_PIPELINE,
                                  vk_pipeline->debugNameCStr());
    }

    void VulkanRHI::setDebugObjectName(RHIBuffer* buffer, const char* name)
    {
        if (buffer == nullptr || name == nullptr)
        {
            return;
        }

        auto* vk_buffer = static_cast<VulkanBuffer*>(buffer);
        vk_buffer->setDebugName(name);
        applyDebugUtilsObjectName(reinterpret_cast<uint64_t>(vk_buffer->getResource()),
                                  VK_OBJECT_TYPE_BUFFER,
                                  vk_buffer->debugNameCStr());
    }

    void VulkanRHI::setDebugObjectName(RHIAccelerationStructure* acceleration_structure, const char* name)
    {
        if (acceleration_structure == nullptr || name == nullptr)
        {
            return;
        }

        auto* vk_as = static_cast<VulkanRHIAccelerationStructure*>(acceleration_structure);
        vk_as->setDebugName(name);
        applyDebugUtilsObjectName(reinterpret_cast<uint64_t>(vk_as->handle),
                                  VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR,
                                  vk_as->debugNameCStr());
        vk_as->applyChildDebugNames(
            [this](uint64_t object_handle, VkObjectType object_type, const char* child_name) {
                applyDebugUtilsObjectName(object_handle, object_type, child_name);
            },
            name);
    }

    void VulkanRHI::loadRayTracingFunctions()
    {
        _vkGetBufferDeviceAddress =
            (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(m_device, "vkGetBufferDeviceAddressKHR");
        _vkCreateAccelerationStructure =
            (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(m_device, "vkCreateAccelerationStructureKHR");
        _vkDestroyAccelerationStructure =
            (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(m_device, "vkDestroyAccelerationStructureKHR");
        _vkGetAccelerationStructureBuildSizes = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(
            m_device, "vkGetAccelerationStructureBuildSizesKHR");
        _vkCmdBuildAccelerationStructures = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(
            m_device, "vkCmdBuildAccelerationStructuresKHR");
        _vkGetAccelerationStructureDeviceAddress = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(
            m_device, "vkGetAccelerationStructureDeviceAddressKHR");
        _vkCreateRayTracingPipelines =
            (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(m_device, "vkCreateRayTracingPipelinesKHR");
        _vkGetRayTracingShaderGroupHandles = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(
            m_device, "vkGetRayTracingShaderGroupHandlesKHR");
        _vkCmdTraceRays = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(m_device, "vkCmdTraceRaysKHR");

        // If any required entry point is missing, disable ray tracing to stay safe.
        if (_vkGetBufferDeviceAddress == nullptr || _vkCreateAccelerationStructure == nullptr ||
            _vkDestroyAccelerationStructure == nullptr || _vkGetAccelerationStructureBuildSizes == nullptr ||
            _vkCmdBuildAccelerationStructures == nullptr || _vkGetAccelerationStructureDeviceAddress == nullptr ||
            _vkCreateRayTracingPipelines == nullptr || _vkGetRayTracingShaderGroupHandles == nullptr ||
            _vkCmdTraceRays == nullptr)
        {
            LOG_WARN("Ray tracing extension entry points missing; disabling Vulkan ray tracing.");
            m_ray_tracing_enabled = false;
            m_ray_tracing_capabilities.support_level = RHIRayTracingSupportLevel::Unsupported;
        }
    }

    void VulkanRHI::queryRayTracingProperties()
    {
        if (!m_ray_tracing_enabled)
        {
            m_ray_tracing_capabilities.support_level = RHIRayTracingSupportLevel::Unsupported;
            return;
        }
        m_ray_tracing_pipeline_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        m_acceleration_structure_properties.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
        m_ray_tracing_pipeline_properties.pNext = &m_acceleration_structure_properties;

        VkPhysicalDeviceProperties2 properties2 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties2.pNext = &m_ray_tracing_pipeline_properties;
        vkGetPhysicalDeviceProperties2(m_physical_device, &properties2);

        m_ray_tracing_capabilities.support_level                  = RHIRayTracingSupportLevel::Supported;
        m_ray_tracing_capabilities.max_recursion_depth            = m_ray_tracing_pipeline_properties.maxRayRecursionDepth;
        m_ray_tracing_capabilities.shader_group_handle_size       = m_ray_tracing_pipeline_properties.shaderGroupHandleSize;
        m_ray_tracing_capabilities.shader_group_handle_alignment  = m_ray_tracing_pipeline_properties.shaderGroupHandleAlignment;
        m_ray_tracing_capabilities.shader_binding_table_alignment = m_ray_tracing_pipeline_properties.shaderGroupBaseAlignment;
        m_ray_tracing_capabilities.supports_inline_ray_tracing    = false;
    }

    VkDeviceAddress VulkanRHI::getBufferDeviceAddress(VkBuffer buffer) const
    {
        if (buffer == VK_NULL_HANDLE || _vkGetBufferDeviceAddress == nullptr)
        {
            return 0;
        }
        VkBufferDeviceAddressInfo info {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        info.buffer = buffer;
        return _vkGetBufferDeviceAddress(m_device, &info);
    }

    void VulkanRHI::createCommandPool()
    {
        // default graphics command pool
        {
            m_rhi_command_pool = new VulkanCommandPool();
            VkCommandPool vk_command_pool;
            VkCommandPoolCreateInfo command_pool_create_info {};
            command_pool_create_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            command_pool_create_info.pNext            = NULL;
            command_pool_create_info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            command_pool_create_info.queueFamilyIndex = m_queue_indices.graphics_family.value();

            if (vkCreateCommandPool(m_device, &command_pool_create_info, nullptr, &vk_command_pool) != VK_SUCCESS)
            {
                LOG_ERROR("vk create command pool");
            }

            ((VulkanCommandPool*)m_rhi_command_pool)->setResource(vk_command_pool);
        }

        // other command pools
        {
            VkCommandPoolCreateInfo command_pool_create_info;
            command_pool_create_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            command_pool_create_info.pNext            = NULL;
            command_pool_create_info.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            command_pool_create_info.queueFamilyIndex = m_queue_indices.graphics_family.value();

            for (uint32_t i = 0; i < k_max_frames_in_flight; ++i)
            {
                if (vkCreateCommandPool(m_device, &command_pool_create_info, NULL, &m_command_pools[i]) != VK_SUCCESS)
                {
                    LOG_ERROR("vk create command pool");
                }
            }
        }
    }

    bool VulkanRHI::createCommandPool(const RHICommandPoolCreateInfo* pCreateInfo, RHICommandPool* &pCommandPool)
    {
        VkCommandPoolCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkCommandPoolCreateFlags)pCreateInfo->flags;
        create_info.queueFamilyIndex = pCreateInfo->queueFamilyIndex;

        pCommandPool = new VulkanCommandPool();
        VkCommandPool vk_commandPool;
        VkResult result = vkCreateCommandPool(m_device, &create_info, nullptr, &vk_commandPool);
        ((VulkanCommandPool*)pCommandPool)->setResource(vk_commandPool);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateCommandPool is failed!");
            return false;
        }
    }

    bool VulkanRHI::createDescriptorPool(const RHIDescriptorPoolCreateInfo* pCreateInfo, RHIDescriptorPool* & pDescriptorPool)
    {
        int size = pCreateInfo->poolSizeCount;
        std::vector<VkDescriptorPoolSize> descriptor_pool_size(size);
        for (int i = 0; i < size; ++i)
        {
            const auto& rhi_desc = pCreateInfo->pPoolSizes[i];
            auto& vk_desc = descriptor_pool_size[i];

            vk_desc.type = (VkDescriptorType)rhi_desc.type;
            vk_desc.descriptorCount = rhi_desc.descriptorCount;
        };

        VkDescriptorPoolCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkDescriptorPoolCreateFlags)pCreateInfo->flags;
        create_info.maxSets = pCreateInfo->maxSets;
        create_info.poolSizeCount = pCreateInfo->poolSizeCount;
        create_info.pPoolSizes = descriptor_pool_size.data();

        pDescriptorPool = new VulkanDescriptorPool();
        VkDescriptorPool vk_descriptorPool;
        VkResult result = vkCreateDescriptorPool(m_device, &create_info, nullptr, &vk_descriptorPool);
        ((VulkanDescriptorPool*)pDescriptorPool)->setResource(vk_descriptorPool);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateDescriptorPool is failed!");
            return false;
        }
    }

    bool VulkanRHI::createDescriptorSetLayout(const RHIDescriptorSetLayoutCreateInfo* pCreateInfo, RHIDescriptorSetLayout* &pSetLayout)
    {
        //descriptor_set_layout_binding
        int descriptor_set_layout_binding_size = pCreateInfo->bindingCount;
        std::vector<VkDescriptorSetLayoutBinding> vk_descriptor_set_layout_binding_list(descriptor_set_layout_binding_size);

        int sampler_count = 0;
        for (int i = 0; i < descriptor_set_layout_binding_size; ++i)
        {
            const auto& rhi_descriptor_set_layout_binding_element = pCreateInfo->pBindings[i];
            if (rhi_descriptor_set_layout_binding_element.pImmutableSamplers != nullptr)
            {
                sampler_count += rhi_descriptor_set_layout_binding_element.descriptorCount;
            }
        }
        std::vector<VkSampler> sampler_list(sampler_count);
        int sampler_current = 0;

        for (int i = 0; i < descriptor_set_layout_binding_size; ++i)
        {
            const auto& rhi_descriptor_set_layout_binding_element = pCreateInfo->pBindings[i];
            auto& vk_descriptor_set_layout_binding_element = vk_descriptor_set_layout_binding_list[i];

            //sampler
            vk_descriptor_set_layout_binding_element.pImmutableSamplers = nullptr;
            if (rhi_descriptor_set_layout_binding_element.pImmutableSamplers)
            {
                vk_descriptor_set_layout_binding_element.pImmutableSamplers = &sampler_list[sampler_current];
                for (int i = 0; i < rhi_descriptor_set_layout_binding_element.descriptorCount; ++i)
                {
                    const auto& rhi_sampler_element = rhi_descriptor_set_layout_binding_element.pImmutableSamplers[i];
                    auto& vk_sampler_element = sampler_list[sampler_current];

                    vk_sampler_element = ((VulkanSampler*)rhi_sampler_element)->getResource();

                    sampler_current++;
                };
            }
            vk_descriptor_set_layout_binding_element.binding = rhi_descriptor_set_layout_binding_element.binding;
            vk_descriptor_set_layout_binding_element.descriptorType = (VkDescriptorType)rhi_descriptor_set_layout_binding_element.descriptorType;
            vk_descriptor_set_layout_binding_element.descriptorCount = rhi_descriptor_set_layout_binding_element.descriptorCount;
            vk_descriptor_set_layout_binding_element.stageFlags = rhi_descriptor_set_layout_binding_element.stageFlags;
        };
        
        if (sampler_count != sampler_current)
        {
            LOG_ERROR("sampler_count != sampller_current");
            return false;
        }

        VkDescriptorSetLayoutCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkDescriptorSetLayoutCreateFlags)pCreateInfo->flags;
        create_info.bindingCount = pCreateInfo->bindingCount;
        create_info.pBindings = vk_descriptor_set_layout_binding_list.data();

        pSetLayout = new VulkanDescriptorSetLayout();
        VkDescriptorSetLayout vk_descriptorSetLayout;
        VkResult result = vkCreateDescriptorSetLayout(m_device, &create_info, nullptr, &vk_descriptorSetLayout);
        ((VulkanDescriptorSetLayout*)pSetLayout)->setResource(vk_descriptorSetLayout);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateDescriptorSetLayout failed!");
            return false;
        }
    }

    bool VulkanRHI::createFence(const RHIFenceCreateInfo* pCreateInfo, RHIFence* &pFence)
    {
        RHIFenceCreateInfo normalized = *pCreateInfo;
        if (normalized.sType == 0)
        {
            normalized.sType = RHI_STRUCTURE_TYPE_FENCE_CREATE_INFO;
#if !defined(NDEBUG)
            LOG_WARN("RHIFenceCreateInfo.sType was unset; defaulting to FENCE_CREATE_INFO");
#endif
        }

        VkFenceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        create_info.pNext = (const void*)normalized.pNext;
        create_info.flags = (VkFenceCreateFlags)normalized.flags;

        pFence = new VulkanFence();
        VkFence vk_fence;
        VkResult result = vkCreateFence(m_device, &create_info, nullptr, &vk_fence);
        ((VulkanFence*)pFence)->setResource(vk_fence);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateFence failed!");
            return false;
        }
    }

    bool VulkanRHI::createFramebuffer(const RHIFramebufferCreateInfo* pCreateInfo, RHIFramebuffer* &pFramebuffer)
    {
        //image_view
        int image_view_size = pCreateInfo->attachmentCount;
        std::vector<VkImageView> vk_image_view_list(image_view_size);
        for (int i = 0; i < image_view_size; ++i)
        {
            const auto& rhi_image_view_element = pCreateInfo->pAttachments[i];
            auto& vk_image_view_element = vk_image_view_list[i];

            vk_image_view_element = ((VulkanImageView*)rhi_image_view_element)->getResource();
        };

        VkFramebufferCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkFramebufferCreateFlags)pCreateInfo->flags;
        create_info.renderPass = ((VulkanRenderPass*)pCreateInfo->renderPass)->getResource();
        create_info.attachmentCount = pCreateInfo->attachmentCount;
        create_info.pAttachments = vk_image_view_list.data();
        create_info.width = pCreateInfo->width;
        create_info.height = pCreateInfo->height;
        create_info.layers = pCreateInfo->layers;

        pFramebuffer = new VulkanFramebuffer();
        VkFramebuffer vk_framebuffer;
        VkResult result = vkCreateFramebuffer(m_device, &create_info, nullptr, &vk_framebuffer);
        ((VulkanFramebuffer*)pFramebuffer)->setResource(vk_framebuffer);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateFramebuffer failed!");
            return false;
        }
    }

    bool VulkanRHI::createGraphicsPipelines(RHIPipelineCache* pipelineCache, uint32_t createInfoCount, const RHIGraphicsPipelineCreateInfo* pCreateInfo, RHIPipeline* &pPipelines)
    {
        //pipeline_shader_stage_create_info
        int pipeline_shader_stage_create_info_size = pCreateInfo->stageCount;
        std::vector<VkPipelineShaderStageCreateInfo> vk_pipeline_shader_stage_create_info_list(pipeline_shader_stage_create_info_size);

        int specialization_map_entry_size_total = 0;
        int specialization_info_total = 0;
        for (int i = 0; i < pipeline_shader_stage_create_info_size; ++i)
        {
            const auto& rhi_pipeline_shader_stage_create_info_element = pCreateInfo->pStages[i];
            if (rhi_pipeline_shader_stage_create_info_element.pSpecializationInfo != nullptr)
            {
                specialization_info_total++;
                specialization_map_entry_size_total+= rhi_pipeline_shader_stage_create_info_element.pSpecializationInfo->mapEntryCount;
            }
        }
        std::vector<VkSpecializationInfo> vk_specialization_info_list(specialization_info_total);
        std::vector<VkSpecializationMapEntry> vk_specialization_map_entry_list(specialization_map_entry_size_total);
        int specialization_map_entry_current = 0;
        int specialization_info_current = 0;

        for (int i = 0; i < pipeline_shader_stage_create_info_size; ++i)
        {
            const auto& rhi_pipeline_shader_stage_create_info_element = pCreateInfo->pStages[i];
            auto& vk_pipeline_shader_stage_create_info_element = vk_pipeline_shader_stage_create_info_list[i];

            if (rhi_pipeline_shader_stage_create_info_element.pSpecializationInfo != nullptr)
            {
                vk_pipeline_shader_stage_create_info_element.pSpecializationInfo = &vk_specialization_info_list[specialization_info_current];

                VkSpecializationInfo vk_specialization_info{};
                vk_specialization_info.mapEntryCount = rhi_pipeline_shader_stage_create_info_element.pSpecializationInfo->mapEntryCount;
                vk_specialization_info.pMapEntries = &vk_specialization_map_entry_list[specialization_map_entry_current];
                vk_specialization_info.dataSize = rhi_pipeline_shader_stage_create_info_element.pSpecializationInfo->dataSize;
                vk_specialization_info.pData = (const void*)rhi_pipeline_shader_stage_create_info_element.pSpecializationInfo->pData;

                //specialization_map_entry
                for (int i = 0; i < rhi_pipeline_shader_stage_create_info_element.pSpecializationInfo->mapEntryCount; ++i)
                {
                    const auto& rhi_specialization_map_entry_element = rhi_pipeline_shader_stage_create_info_element.pSpecializationInfo->pMapEntries[i];
                    auto& vk_specialization_map_entry_element = vk_specialization_map_entry_list[specialization_map_entry_current];

                    vk_specialization_map_entry_element.constantID = rhi_specialization_map_entry_element->constantID;
                    vk_specialization_map_entry_element.offset = rhi_specialization_map_entry_element->offset;
                    vk_specialization_map_entry_element.size = rhi_specialization_map_entry_element->size;

                    specialization_map_entry_current++;
                };

                specialization_info_current++;
            }
            else
            {
                vk_pipeline_shader_stage_create_info_element.pSpecializationInfo = nullptr;
            }
            vk_pipeline_shader_stage_create_info_element.sType = (VkStructureType)rhi_pipeline_shader_stage_create_info_element.sType;
            vk_pipeline_shader_stage_create_info_element.pNext = (const void*)rhi_pipeline_shader_stage_create_info_element.pNext;
            vk_pipeline_shader_stage_create_info_element.flags = (VkPipelineShaderStageCreateFlags)rhi_pipeline_shader_stage_create_info_element.flags;
            vk_pipeline_shader_stage_create_info_element.stage = (VkShaderStageFlagBits)rhi_pipeline_shader_stage_create_info_element.stage;
            vk_pipeline_shader_stage_create_info_element.module = ((VulkanShader*)rhi_pipeline_shader_stage_create_info_element.module)->getResource();
            vk_pipeline_shader_stage_create_info_element.pName = rhi_pipeline_shader_stage_create_info_element.pName;
        };

        if (!((specialization_map_entry_size_total == specialization_map_entry_current)
            && (specialization_info_total == specialization_info_current)))
        {
            LOG_ERROR("(specialization_map_entry_size_total == specialization_map_entry_current)&& (specialization_info_total == specialization_info_current)");
            return false;
        }

        //vertex_input_binding_description
        int vertex_input_binding_description_size = pCreateInfo->pVertexInputState->vertexBindingDescriptionCount;
        std::vector<VkVertexInputBindingDescription> vk_vertex_input_binding_description_list(vertex_input_binding_description_size);
        for (int i = 0; i < vertex_input_binding_description_size; ++i)
        {
            const auto& rhi_vertex_input_binding_description_element = pCreateInfo->pVertexInputState->pVertexBindingDescriptions[i];
            auto& vk_vertex_input_binding_description_element = vk_vertex_input_binding_description_list[i];

            vk_vertex_input_binding_description_element.binding = rhi_vertex_input_binding_description_element.binding;
            vk_vertex_input_binding_description_element.stride = rhi_vertex_input_binding_description_element.stride;
            vk_vertex_input_binding_description_element.inputRate = (VkVertexInputRate)rhi_vertex_input_binding_description_element.inputRate;
        };

        //vertex_input_attribute_description
        int vertex_input_attribute_description_size = pCreateInfo->pVertexInputState->vertexAttributeDescriptionCount;
        std::vector<VkVertexInputAttributeDescription> vk_vertex_input_attribute_description_list(vertex_input_attribute_description_size);
        for (int i = 0; i < vertex_input_attribute_description_size; ++i)
        {
            const auto& rhi_vertex_input_attribute_description_element = pCreateInfo->pVertexInputState->pVertexAttributeDescriptions[i];
            auto& vk_vertex_input_attribute_description_element = vk_vertex_input_attribute_description_list[i];

            vk_vertex_input_attribute_description_element.location = rhi_vertex_input_attribute_description_element.location;
            vk_vertex_input_attribute_description_element.binding = rhi_vertex_input_attribute_description_element.binding;
            vk_vertex_input_attribute_description_element.format = (VkFormat)rhi_vertex_input_attribute_description_element.format;
            vk_vertex_input_attribute_description_element.offset = rhi_vertex_input_attribute_description_element.offset;
        };

        VkPipelineVertexInputStateCreateInfo vk_pipeline_vertex_input_state_create_info{};
        vk_pipeline_vertex_input_state_create_info.sType = (VkStructureType)pCreateInfo->pVertexInputState->sType;
        vk_pipeline_vertex_input_state_create_info.pNext = (const void*)pCreateInfo->pVertexInputState->pNext;
        vk_pipeline_vertex_input_state_create_info.flags = (VkPipelineVertexInputStateCreateFlags)pCreateInfo->pVertexInputState->flags;
        vk_pipeline_vertex_input_state_create_info.vertexBindingDescriptionCount = pCreateInfo->pVertexInputState->vertexBindingDescriptionCount;
        vk_pipeline_vertex_input_state_create_info.pVertexBindingDescriptions = vk_vertex_input_binding_description_list.data();
        vk_pipeline_vertex_input_state_create_info.vertexAttributeDescriptionCount = pCreateInfo->pVertexInputState->vertexAttributeDescriptionCount;
        vk_pipeline_vertex_input_state_create_info.pVertexAttributeDescriptions = vk_vertex_input_attribute_description_list.data();

        VkPipelineInputAssemblyStateCreateInfo vk_pipeline_input_assembly_state_create_info{};
        vk_pipeline_input_assembly_state_create_info.sType = (VkStructureType)pCreateInfo->pInputAssemblyState->sType;
        vk_pipeline_input_assembly_state_create_info.pNext = (const void*)pCreateInfo->pInputAssemblyState->pNext;
        vk_pipeline_input_assembly_state_create_info.flags = (VkPipelineInputAssemblyStateCreateFlags)pCreateInfo->pInputAssemblyState->flags;
        vk_pipeline_input_assembly_state_create_info.topology = (VkPrimitiveTopology)pCreateInfo->pInputAssemblyState->topology;
        vk_pipeline_input_assembly_state_create_info.primitiveRestartEnable = (VkBool32)pCreateInfo->pInputAssemblyState->primitiveRestartEnable;

        const VkPipelineTessellationStateCreateInfo* vk_pipeline_tessellation_state_create_info_ptr = nullptr;
        VkPipelineTessellationStateCreateInfo vk_pipeline_tessellation_state_create_info{};
        if (pCreateInfo->pTessellationState != nullptr)
        {
            vk_pipeline_tessellation_state_create_info.sType = (VkStructureType)pCreateInfo->pTessellationState->sType;
            vk_pipeline_tessellation_state_create_info.pNext = (const void*)pCreateInfo->pTessellationState->pNext;
            vk_pipeline_tessellation_state_create_info.flags = (VkPipelineTessellationStateCreateFlags)pCreateInfo->pTessellationState->flags;
            vk_pipeline_tessellation_state_create_info.patchControlPoints = pCreateInfo->pTessellationState->patchControlPoints;

            vk_pipeline_tessellation_state_create_info_ptr = &vk_pipeline_tessellation_state_create_info;
        }

        //viewport
        int viewport_size = pCreateInfo->pViewportState->viewportCount;
        std::vector<VkViewport> vk_viewport_list(viewport_size);
        for (int i = 0; i < viewport_size; ++i)
        {
            const auto& rhi_viewport_element = pCreateInfo->pViewportState->pViewports[i];
            auto& vk_viewport_element = vk_viewport_list[i];

            vk_viewport_element.x = rhi_viewport_element.x;
            vk_viewport_element.y = rhi_viewport_element.y;
            vk_viewport_element.width = rhi_viewport_element.width;
            vk_viewport_element.height = rhi_viewport_element.height;
            vk_viewport_element.minDepth = rhi_viewport_element.minDepth;
            vk_viewport_element.maxDepth = rhi_viewport_element.maxDepth;
        };

        //rect_2d
        int rect_2d_size = pCreateInfo->pViewportState->scissorCount;
        std::vector<VkRect2D> vk_rect_2d_list(rect_2d_size);
        for (int i = 0; i < rect_2d_size; ++i)
        {
            const auto& rhi_rect_2d_element = pCreateInfo->pViewportState->pScissors[i];
            auto& vk_rect_2d_element = vk_rect_2d_list[i];

            VkOffset2D offset2d{};
            offset2d.x = rhi_rect_2d_element.offset.x;
            offset2d.y = rhi_rect_2d_element.offset.y;

            VkExtent2D extend2d{};
            extend2d.width = rhi_rect_2d_element.extent.width;
            extend2d.height = rhi_rect_2d_element.extent.height;

            vk_rect_2d_element.offset = offset2d;
            vk_rect_2d_element.extent = extend2d;
        };

        VkPipelineViewportStateCreateInfo vk_pipeline_viewport_state_create_info{};
        vk_pipeline_viewport_state_create_info.sType = (VkStructureType)pCreateInfo->pViewportState->sType;
        vk_pipeline_viewport_state_create_info.pNext = (const void*)pCreateInfo->pViewportState->pNext;
        vk_pipeline_viewport_state_create_info.flags = (VkPipelineViewportStateCreateFlags)pCreateInfo->pViewportState->flags;
        vk_pipeline_viewport_state_create_info.viewportCount = pCreateInfo->pViewportState->viewportCount;
        vk_pipeline_viewport_state_create_info.pViewports = vk_viewport_list.data();
        vk_pipeline_viewport_state_create_info.scissorCount = pCreateInfo->pViewportState->scissorCount;
        vk_pipeline_viewport_state_create_info.pScissors = vk_rect_2d_list.data();

        VkPipelineRasterizationStateCreateInfo vk_pipeline_rasterization_state_create_info{};
        vk_pipeline_rasterization_state_create_info.sType = (VkStructureType)pCreateInfo->pRasterizationState->sType;
        vk_pipeline_rasterization_state_create_info.pNext = (const void*)pCreateInfo->pRasterizationState->pNext;
        vk_pipeline_rasterization_state_create_info.flags = (VkPipelineRasterizationStateCreateFlags)pCreateInfo->pRasterizationState->flags;
        vk_pipeline_rasterization_state_create_info.depthClampEnable = (VkBool32)pCreateInfo->pRasterizationState->depthClampEnable;
        vk_pipeline_rasterization_state_create_info.rasterizerDiscardEnable = (VkBool32)pCreateInfo->pRasterizationState->rasterizerDiscardEnable;
        vk_pipeline_rasterization_state_create_info.polygonMode = (VkPolygonMode)pCreateInfo->pRasterizationState->polygonMode;
        vk_pipeline_rasterization_state_create_info.cullMode = (VkCullModeFlags)pCreateInfo->pRasterizationState->cullMode;
        vk_pipeline_rasterization_state_create_info.frontFace = (VkFrontFace)pCreateInfo->pRasterizationState->frontFace;
        vk_pipeline_rasterization_state_create_info.depthBiasEnable = (VkBool32)pCreateInfo->pRasterizationState->depthBiasEnable;
        vk_pipeline_rasterization_state_create_info.depthBiasConstantFactor = pCreateInfo->pRasterizationState->depthBiasConstantFactor;
        vk_pipeline_rasterization_state_create_info.depthBiasClamp = pCreateInfo->pRasterizationState->depthBiasClamp;
        vk_pipeline_rasterization_state_create_info.depthBiasSlopeFactor = pCreateInfo->pRasterizationState->depthBiasSlopeFactor;
        vk_pipeline_rasterization_state_create_info.lineWidth = pCreateInfo->pRasterizationState->lineWidth;

        VkPipelineMultisampleStateCreateInfo vk_pipeline_multisample_state_create_info{};
        vk_pipeline_multisample_state_create_info.sType = (VkStructureType)pCreateInfo->pMultisampleState->sType;
        vk_pipeline_multisample_state_create_info.pNext = (const void*)pCreateInfo->pMultisampleState->pNext;
        vk_pipeline_multisample_state_create_info.flags = (VkPipelineMultisampleStateCreateFlags)pCreateInfo->pMultisampleState->flags;
        vk_pipeline_multisample_state_create_info.rasterizationSamples = (VkSampleCountFlagBits)pCreateInfo->pMultisampleState->rasterizationSamples;
        vk_pipeline_multisample_state_create_info.sampleShadingEnable = (VkBool32)pCreateInfo->pMultisampleState->sampleShadingEnable;
        vk_pipeline_multisample_state_create_info.minSampleShading = pCreateInfo->pMultisampleState->minSampleShading;
        vk_pipeline_multisample_state_create_info.pSampleMask = (const RHISampleMask*)pCreateInfo->pMultisampleState->pSampleMask;
        vk_pipeline_multisample_state_create_info.alphaToCoverageEnable = (VkBool32)pCreateInfo->pMultisampleState->alphaToCoverageEnable;
        vk_pipeline_multisample_state_create_info.alphaToOneEnable = (VkBool32)pCreateInfo->pMultisampleState->alphaToOneEnable;

        VkStencilOpState stencil_op_state_front{};
        stencil_op_state_front.failOp = (VkStencilOp)pCreateInfo->pDepthStencilState->front.failOp;
        stencil_op_state_front.passOp = (VkStencilOp)pCreateInfo->pDepthStencilState->front.passOp;
        stencil_op_state_front.depthFailOp = (VkStencilOp)pCreateInfo->pDepthStencilState->front.depthFailOp;
        stencil_op_state_front.compareOp = (VkCompareOp)pCreateInfo->pDepthStencilState->front.compareOp;
        stencil_op_state_front.compareMask = pCreateInfo->pDepthStencilState->front.compareMask;
        stencil_op_state_front.writeMask = pCreateInfo->pDepthStencilState->front.writeMask;
        stencil_op_state_front.reference = pCreateInfo->pDepthStencilState->front.reference;

        VkStencilOpState stencil_op_state_back{};
        stencil_op_state_back.failOp = (VkStencilOp)pCreateInfo->pDepthStencilState->back.failOp;
        stencil_op_state_back.passOp = (VkStencilOp)pCreateInfo->pDepthStencilState->back.passOp;
        stencil_op_state_back.depthFailOp = (VkStencilOp)pCreateInfo->pDepthStencilState->back.depthFailOp;
        stencil_op_state_back.compareOp = (VkCompareOp)pCreateInfo->pDepthStencilState->back.compareOp;
        stencil_op_state_back.compareMask = pCreateInfo->pDepthStencilState->back.compareMask;
        stencil_op_state_back.writeMask = pCreateInfo->pDepthStencilState->back.writeMask;
        stencil_op_state_back.reference = pCreateInfo->pDepthStencilState->back.reference;


        VkPipelineDepthStencilStateCreateInfo vk_pipeline_depth_stencil_state_create_info{};
        vk_pipeline_depth_stencil_state_create_info.sType = (VkStructureType)pCreateInfo->pDepthStencilState->sType;
        vk_pipeline_depth_stencil_state_create_info.pNext = (const void*)pCreateInfo->pDepthStencilState->pNext;
        vk_pipeline_depth_stencil_state_create_info.flags = (VkPipelineDepthStencilStateCreateFlags)pCreateInfo->pDepthStencilState->flags;
        vk_pipeline_depth_stencil_state_create_info.depthTestEnable = (VkBool32)pCreateInfo->pDepthStencilState->depthTestEnable;
        vk_pipeline_depth_stencil_state_create_info.depthWriteEnable = (VkBool32)pCreateInfo->pDepthStencilState->depthWriteEnable;
        vk_pipeline_depth_stencil_state_create_info.depthCompareOp = (VkCompareOp)pCreateInfo->pDepthStencilState->depthCompareOp;
        vk_pipeline_depth_stencil_state_create_info.depthBoundsTestEnable = (VkBool32)pCreateInfo->pDepthStencilState->depthBoundsTestEnable;
        vk_pipeline_depth_stencil_state_create_info.stencilTestEnable = (VkBool32)pCreateInfo->pDepthStencilState->stencilTestEnable;
        vk_pipeline_depth_stencil_state_create_info.front = stencil_op_state_front;
        vk_pipeline_depth_stencil_state_create_info.back = stencil_op_state_back;
        vk_pipeline_depth_stencil_state_create_info.minDepthBounds = pCreateInfo->pDepthStencilState->minDepthBounds;
        vk_pipeline_depth_stencil_state_create_info.maxDepthBounds = pCreateInfo->pDepthStencilState->maxDepthBounds;

        //pipeline_color_blend_attachment_state
        int pipeline_color_blend_attachment_state_size = pCreateInfo->pColorBlendState->attachmentCount;
        std::vector<VkPipelineColorBlendAttachmentState> vk_pipeline_color_blend_attachment_state_list(pipeline_color_blend_attachment_state_size);
        for (int i = 0; i < pipeline_color_blend_attachment_state_size; ++i)
        {
            const auto& rhi_pipeline_color_blend_attachment_state_element = pCreateInfo->pColorBlendState->pAttachments[i];
            auto& vk_pipeline_color_blend_attachment_state_element = vk_pipeline_color_blend_attachment_state_list[i];

            vk_pipeline_color_blend_attachment_state_element.blendEnable = (VkBool32)rhi_pipeline_color_blend_attachment_state_element.blendEnable;
            vk_pipeline_color_blend_attachment_state_element.srcColorBlendFactor = (VkBlendFactor)rhi_pipeline_color_blend_attachment_state_element.srcColorBlendFactor;
            vk_pipeline_color_blend_attachment_state_element.dstColorBlendFactor = (VkBlendFactor)rhi_pipeline_color_blend_attachment_state_element.dstColorBlendFactor;
            vk_pipeline_color_blend_attachment_state_element.colorBlendOp = (VkBlendOp)rhi_pipeline_color_blend_attachment_state_element.colorBlendOp;
            vk_pipeline_color_blend_attachment_state_element.srcAlphaBlendFactor = (VkBlendFactor)rhi_pipeline_color_blend_attachment_state_element.srcAlphaBlendFactor;
            vk_pipeline_color_blend_attachment_state_element.dstAlphaBlendFactor = (VkBlendFactor)rhi_pipeline_color_blend_attachment_state_element.dstAlphaBlendFactor;
            vk_pipeline_color_blend_attachment_state_element.alphaBlendOp = (VkBlendOp)rhi_pipeline_color_blend_attachment_state_element.alphaBlendOp;
            vk_pipeline_color_blend_attachment_state_element.colorWriteMask = (VkColorComponentFlags)rhi_pipeline_color_blend_attachment_state_element.colorWriteMask;
        };

        VkPipelineColorBlendStateCreateInfo vk_pipeline_color_blend_state_create_info{};
        vk_pipeline_color_blend_state_create_info.sType = (VkStructureType)pCreateInfo->pColorBlendState->sType;
        vk_pipeline_color_blend_state_create_info.pNext = pCreateInfo->pColorBlendState->pNext;
        vk_pipeline_color_blend_state_create_info.flags = pCreateInfo->pColorBlendState->flags;
        vk_pipeline_color_blend_state_create_info.logicOpEnable = pCreateInfo->pColorBlendState->logicOpEnable;
        vk_pipeline_color_blend_state_create_info.logicOp = (VkLogicOp)pCreateInfo->pColorBlendState->logicOp;
        vk_pipeline_color_blend_state_create_info.attachmentCount = pCreateInfo->pColorBlendState->attachmentCount;
        vk_pipeline_color_blend_state_create_info.pAttachments = vk_pipeline_color_blend_attachment_state_list.data();
        for (int i = 0; i < 4; ++i)
        {
            vk_pipeline_color_blend_state_create_info.blendConstants[i] = pCreateInfo->pColorBlendState->blendConstants[i];
        };

        //dynamic_state
        int dynamic_state_size = pCreateInfo->pDynamicState->dynamicStateCount;
        std::vector<VkDynamicState> vk_dynamic_state_list(dynamic_state_size);
        for (int i = 0; i < dynamic_state_size; ++i)
        {
            const auto& rhi_dynamic_state_element = pCreateInfo->pDynamicState->pDynamicStates[i];
            auto& vk_dynamic_state_element = vk_dynamic_state_list[i];

            vk_dynamic_state_element = (VkDynamicState)rhi_dynamic_state_element;
        };

        VkPipelineDynamicStateCreateInfo vk_pipeline_dynamic_state_create_info{};
        vk_pipeline_dynamic_state_create_info.sType = (VkStructureType)pCreateInfo->pDynamicState->sType;
        vk_pipeline_dynamic_state_create_info.pNext = pCreateInfo->pDynamicState->pNext;
        vk_pipeline_dynamic_state_create_info.flags = (VkPipelineDynamicStateCreateFlags)pCreateInfo->pDynamicState->flags;
        vk_pipeline_dynamic_state_create_info.dynamicStateCount = pCreateInfo->pDynamicState->dynamicStateCount;
        vk_pipeline_dynamic_state_create_info.pDynamicStates = vk_dynamic_state_list.data();

        VkGraphicsPipelineCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkPipelineCreateFlags)pCreateInfo->flags;
        create_info.stageCount = pCreateInfo->stageCount;
        create_info.pStages = vk_pipeline_shader_stage_create_info_list.data();
        create_info.pVertexInputState = &vk_pipeline_vertex_input_state_create_info;
        create_info.pInputAssemblyState = &vk_pipeline_input_assembly_state_create_info;
        create_info.pTessellationState = vk_pipeline_tessellation_state_create_info_ptr;
        create_info.pViewportState = &vk_pipeline_viewport_state_create_info;
        create_info.pRasterizationState = &vk_pipeline_rasterization_state_create_info;
        create_info.pMultisampleState = &vk_pipeline_multisample_state_create_info;
        create_info.pDepthStencilState = &vk_pipeline_depth_stencil_state_create_info;
        create_info.pColorBlendState = &vk_pipeline_color_blend_state_create_info;
        create_info.pDynamicState = &vk_pipeline_dynamic_state_create_info;
        create_info.layout = ((VulkanPipelineLayout*)pCreateInfo->layout)->getResource();
        create_info.renderPass = ((VulkanRenderPass*)pCreateInfo->renderPass)->getResource();
        create_info.subpass = pCreateInfo->subpass;
        if (pCreateInfo->basePipelineHandle != nullptr)
        {
            create_info.basePipelineHandle = ((VulkanPipeline*)pCreateInfo->basePipelineHandle)->getResource();
        }
        else
        {
            create_info.basePipelineHandle = VK_NULL_HANDLE;
        }
        create_info.basePipelineIndex = pCreateInfo->basePipelineIndex;

        pPipelines = new VulkanPipeline();
        VkPipeline vk_pipelines;
        VkPipelineCache vk_pipeline_cache = VK_NULL_HANDLE;
        if (pipelineCache != nullptr)
        {
            vk_pipeline_cache = ((VulkanPipelineCache*)pipelineCache)->getResource();
        }
        VkResult result = vkCreateGraphicsPipelines(m_device, vk_pipeline_cache, createInfoCount, &create_info, nullptr, &vk_pipelines);
        ((VulkanPipeline*)pPipelines)->setResource(vk_pipelines);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateGraphicsPipelines failed!");
            return false;
        }
    }

    bool VulkanRHI::createComputePipelines(RHIPipelineCache* pipelineCache, uint32_t createInfoCount, const RHIComputePipelineCreateInfo* pCreateInfos, RHIPipeline*& pPipelines)
    {
        VkPipelineShaderStageCreateInfo shader_stage_create_info{};
        if (pCreateInfos->pStages->pSpecializationInfo != nullptr)
        {
            //will be complete soon if needed.
            shader_stage_create_info.pSpecializationInfo = nullptr;
        }
        else
        {
            shader_stage_create_info.pSpecializationInfo = nullptr;
        }
        shader_stage_create_info.sType = (VkStructureType)pCreateInfos->pStages->sType;
        shader_stage_create_info.pNext = (const void*)pCreateInfos->pStages->pNext;
        shader_stage_create_info.flags = (VkPipelineShaderStageCreateFlags)pCreateInfos->pStages->flags;
        shader_stage_create_info.stage = (VkShaderStageFlagBits)pCreateInfos->pStages->stage;
        shader_stage_create_info.module = ((VulkanShader*)pCreateInfos->pStages->module)->getResource();
        shader_stage_create_info.pName = pCreateInfos->pStages->pName;

        VkComputePipelineCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfos->sType;
        create_info.pNext = (const void*)pCreateInfos->pNext;
        create_info.flags = (VkPipelineCreateFlags)pCreateInfos->flags;
        create_info.stage = shader_stage_create_info;
        create_info.layout = ((VulkanPipelineLayout*)pCreateInfos->layout)->getResource();;
        if (pCreateInfos->basePipelineHandle != nullptr)
        {
            create_info.basePipelineHandle = ((VulkanPipeline*)pCreateInfos->basePipelineHandle)->getResource();
        }
        else
        {
            create_info.basePipelineHandle = VK_NULL_HANDLE;
        }
        create_info.basePipelineIndex = pCreateInfos->basePipelineIndex;

        pPipelines = new VulkanPipeline();
        VkPipeline vk_pipelines;
        VkPipelineCache vk_pipeline_cache = VK_NULL_HANDLE;
        if (pipelineCache != nullptr)
        {
            vk_pipeline_cache = ((VulkanPipelineCache*)pipelineCache)->getResource();
        }
        VkResult result = vkCreateComputePipelines(m_device, vk_pipeline_cache, createInfoCount, &create_info, nullptr, &vk_pipelines);
        ((VulkanPipeline*)pPipelines)->setResource(vk_pipelines);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateComputePipelines failed!");
            return false;
        }
    }

    bool VulkanRHI::createPipelineLayout(const RHIPipelineLayoutCreateInfo* pCreateInfo, RHIPipelineLayout* &pPipelineLayout)
    {
        //descriptor_set_layout
        int descriptor_set_layout_size = pCreateInfo->setLayoutCount;
        std::vector<VkDescriptorSetLayout> vk_descriptor_set_layout_list(descriptor_set_layout_size);
        for (int i = 0; i < descriptor_set_layout_size; ++i)
        {
            const auto& rhi_descriptor_set_layout_element = pCreateInfo->pSetLayouts[i];
            auto& vk_descriptor_set_layout_element = vk_descriptor_set_layout_list[i];

            vk_descriptor_set_layout_element = ((VulkanDescriptorSetLayout*)rhi_descriptor_set_layout_element)->getResource();
        };

        VkPipelineLayoutCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkPipelineLayoutCreateFlags)pCreateInfo->flags;
        create_info.setLayoutCount = pCreateInfo->setLayoutCount;
        create_info.pSetLayouts = vk_descriptor_set_layout_list.data();

        pPipelineLayout = new VulkanPipelineLayout();
        VkPipelineLayout vk_pipeline_layout;
        VkResult result = vkCreatePipelineLayout(m_device, &create_info, nullptr, &vk_pipeline_layout);
        ((VulkanPipelineLayout*)pPipelineLayout)->setResource(vk_pipeline_layout);
        
        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreatePipelineLayout failed!");
            return false;
        }
    }

    bool VulkanRHI::createRenderPass(const RHIRenderPassCreateInfo* pCreateInfo, RHIRenderPass* &pRenderPass)
    {
        // attachment convert
        std::vector<VkAttachmentDescription> vk_attachments(pCreateInfo->attachmentCount);
        for (int i = 0; i < pCreateInfo->attachmentCount; ++i)
        {
            const auto& rhi_desc = pCreateInfo->pAttachments[i];
            auto& vk_desc = vk_attachments[i];

            vk_desc.flags = (VkAttachmentDescriptionFlags)(rhi_desc).flags;
            vk_desc.format = (VkFormat)(rhi_desc).format;
            vk_desc.samples = (VkSampleCountFlagBits)(rhi_desc).samples;
            vk_desc.loadOp = (VkAttachmentLoadOp)(rhi_desc).loadOp;
            vk_desc.storeOp = (VkAttachmentStoreOp)(rhi_desc).storeOp;
            vk_desc.stencilLoadOp = (VkAttachmentLoadOp)(rhi_desc).stencilLoadOp;
            vk_desc.stencilStoreOp = (VkAttachmentStoreOp)(rhi_desc).stencilStoreOp;
            vk_desc.initialLayout = (VkImageLayout)(rhi_desc).initialLayout;
            vk_desc.finalLayout = (VkImageLayout)(rhi_desc).finalLayout;
        };

        // subpass convert
        int totalAttachmentRefenrence = 0;
        for (int i = 0; i < pCreateInfo->subpassCount; i++)
        {
            const auto& rhi_desc = pCreateInfo->pSubpasses[i];
            totalAttachmentRefenrence += rhi_desc.inputAttachmentCount; // pInputAttachments
            totalAttachmentRefenrence += rhi_desc.colorAttachmentCount; // pColorAttachments
            if (rhi_desc.pDepthStencilAttachment != nullptr)
            {
                totalAttachmentRefenrence += 1;
            }
            if (rhi_desc.pResolveAttachments != nullptr)
            {
                totalAttachmentRefenrence += rhi_desc.colorAttachmentCount; // pResolveAttachments
            }
        }
        std::vector<VkSubpassDescription> vk_subpass_description(pCreateInfo->subpassCount);
        std::vector<VkAttachmentReference> vk_attachment_reference(totalAttachmentRefenrence);
        int currentAttachmentRefence = 0;
        for (int i = 0; i < pCreateInfo->subpassCount; ++i)
        {
            const auto& rhi_desc = pCreateInfo->pSubpasses[i];
            auto& vk_desc = vk_subpass_description[i];

            vk_desc.flags = (VkSubpassDescriptionFlags)(rhi_desc).flags;
            vk_desc.pipelineBindPoint = (VkPipelineBindPoint)(rhi_desc).pipelineBindPoint;
            vk_desc.preserveAttachmentCount = (rhi_desc).preserveAttachmentCount;
            vk_desc.pPreserveAttachments = (const uint32_t*)(rhi_desc).pPreserveAttachments;

            vk_desc.inputAttachmentCount = (rhi_desc).inputAttachmentCount;
            vk_desc.pInputAttachments = &vk_attachment_reference[currentAttachmentRefence];
            for (int i = 0; i < (rhi_desc).inputAttachmentCount; i++)
            {
                const auto& rhi_attachment_refence_input = (rhi_desc).pInputAttachments[i];
                auto& vk_attachment_refence_input = vk_attachment_reference[currentAttachmentRefence];

                vk_attachment_refence_input.attachment = rhi_attachment_refence_input.attachment;
                vk_attachment_refence_input.layout = (VkImageLayout)(rhi_attachment_refence_input.layout);

                currentAttachmentRefence += 1;
            };

            vk_desc.colorAttachmentCount = (rhi_desc).colorAttachmentCount;
            vk_desc.pColorAttachments = &vk_attachment_reference[currentAttachmentRefence];
            for (int i = 0; i < (rhi_desc).colorAttachmentCount; ++i)
            {
                const auto& rhi_attachment_refence_color = (rhi_desc).pColorAttachments[i];
                auto& vk_attachment_refence_color = vk_attachment_reference[currentAttachmentRefence];

                vk_attachment_refence_color.attachment = rhi_attachment_refence_color.attachment;
                vk_attachment_refence_color.layout = (VkImageLayout)(rhi_attachment_refence_color.layout);

                currentAttachmentRefence += 1;
            };

            if (rhi_desc.pResolveAttachments != nullptr)
            {
                vk_desc.pResolveAttachments = &vk_attachment_reference[currentAttachmentRefence];
                for (int i = 0; i < (rhi_desc).colorAttachmentCount; ++i)
                {
                    const auto& rhi_attachment_refence_resolve = (rhi_desc).pResolveAttachments[i];
                    auto& vk_attachment_refence_resolve = vk_attachment_reference[currentAttachmentRefence];

                    vk_attachment_refence_resolve.attachment = rhi_attachment_refence_resolve.attachment;
                    vk_attachment_refence_resolve.layout = (VkImageLayout)(rhi_attachment_refence_resolve.layout);

                    currentAttachmentRefence += 1;
                };
            }

            if (rhi_desc.pDepthStencilAttachment != nullptr)
            {
                vk_desc.pDepthStencilAttachment = &vk_attachment_reference[currentAttachmentRefence];
                const auto& rhi_attachment_refence_depth = *(rhi_desc).pDepthStencilAttachment;
                auto& vk_attachment_refence_depth = vk_attachment_reference[currentAttachmentRefence];

                vk_attachment_refence_depth.attachment = rhi_attachment_refence_depth.attachment;
                vk_attachment_refence_depth.layout = (VkImageLayout)(rhi_attachment_refence_depth.layout);

                currentAttachmentRefence += 1;
            };
        };
        if (currentAttachmentRefence != totalAttachmentRefenrence)
        {
            LOG_ERROR("currentAttachmentRefence != totalAttachmentRefenrence");
            return false;
        }

        std::vector<VkSubpassDependency> vk_subpass_depandecy(pCreateInfo->dependencyCount);
        for (int i = 0; i < pCreateInfo->dependencyCount; ++i)
        {
            const auto& rhi_desc = pCreateInfo->pDependencies[i];
            auto& vk_desc = vk_subpass_depandecy[i];

            vk_desc.srcSubpass = rhi_desc.srcSubpass;
            vk_desc.dstSubpass = rhi_desc.dstSubpass;
            vk_desc.srcStageMask = (VkPipelineStageFlags)(rhi_desc).srcStageMask;
            vk_desc.dstStageMask = (VkPipelineStageFlags)(rhi_desc).dstStageMask;
            vk_desc.srcAccessMask = (VkAccessFlags)(rhi_desc).srcAccessMask;
            vk_desc.dstAccessMask = (VkAccessFlags)(rhi_desc).dstAccessMask;
            vk_desc.dependencyFlags = (VkDependencyFlags)(rhi_desc).dependencyFlags;
        };

        VkRenderPassCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkRenderPassCreateFlags)pCreateInfo->flags;
        create_info.attachmentCount = pCreateInfo->attachmentCount;
        create_info.pAttachments = vk_attachments.data();
        create_info.subpassCount = pCreateInfo->subpassCount;
        create_info.pSubpasses = vk_subpass_description.data();
        create_info.dependencyCount = pCreateInfo->dependencyCount;
        create_info.pDependencies = vk_subpass_depandecy.data();

        pRenderPass = new VulkanRenderPass();
        VkRenderPass vk_render_pass;
        VkResult result = vkCreateRenderPass(m_device, &create_info, nullptr, &vk_render_pass);
        ((VulkanRenderPass*)pRenderPass)->setResource(vk_render_pass);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateRenderPass failed!");
            return false;
        }
    }

    bool VulkanRHI::createSampler(const RHISamplerCreateInfo* pCreateInfo, RHISampler* &pSampler)
    {
        VkSamplerCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = (const void*)pCreateInfo->pNext;
        create_info.flags = (VkSamplerCreateFlags)pCreateInfo->flags;
        create_info.magFilter = (VkFilter)pCreateInfo->magFilter;
        create_info.minFilter = (VkFilter)pCreateInfo->minFilter;
        create_info.mipmapMode = (VkSamplerMipmapMode)pCreateInfo->mipmapMode;
        create_info.addressModeU = (VkSamplerAddressMode)pCreateInfo->addressModeU;
        create_info.addressModeV = (VkSamplerAddressMode)pCreateInfo->addressModeV;
        create_info.addressModeW = (VkSamplerAddressMode)pCreateInfo->addressModeW;
        create_info.mipLodBias = pCreateInfo->mipLodBias;
        create_info.anisotropyEnable = (VkBool32)pCreateInfo->anisotropyEnable;
        create_info.maxAnisotropy = pCreateInfo->maxAnisotropy;
        create_info.compareEnable = (VkBool32)pCreateInfo->compareEnable;
        create_info.compareOp = (VkCompareOp)pCreateInfo->compareOp;
        create_info.minLod = pCreateInfo->minLod;
        create_info.maxLod = pCreateInfo->maxLod;
        create_info.borderColor = (VkBorderColor)pCreateInfo->borderColor;
        create_info.unnormalizedCoordinates = (VkBool32)pCreateInfo->unnormalizedCoordinates;

        pSampler = new VulkanSampler();
        VkSampler vk_sampler;
        VkResult result = vkCreateSampler(m_device, &create_info, nullptr, &vk_sampler);
        ((VulkanSampler*)pSampler)->setResource(vk_sampler);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateSampler failed!");
            return false;
        }
    }

    bool VulkanRHI::createSemaphore(const RHISemaphoreCreateInfo* pCreateInfo, RHISemaphore* &pSemaphore)
    {
        VkSemaphoreCreateInfo create_info{};
        create_info.sType = (VkStructureType)pCreateInfo->sType;
        create_info.pNext = pCreateInfo->pNext;
        create_info.flags = (VkSemaphoreCreateFlags)pCreateInfo->flags;

        pSemaphore = new VulkanSemaphore();
        VkSemaphore vk_semaphore;
        VkResult result = vkCreateSemaphore(m_device, &create_info, nullptr, &vk_semaphore);
        ((VulkanSemaphore*)pSemaphore)->setResource(vk_semaphore);

        if (result == VK_SUCCESS)
        {
            return RHI_SUCCESS;
        }
        else
        {
            LOG_ERROR("vkCreateSemaphore failed!");
            return false;
        }
    }

    bool VulkanRHI::waitForFencesPFN(uint32_t fenceCount, RHIFence* const* pFences, RHIBool32 waitAll, uint64_t timeout)
    {
        //fence
        int fence_size = fenceCount;
        std::vector<VkFence> vk_fence_list(fence_size);
        for (int i = 0; i < fence_size; ++i)
        {
            const auto& rhi_fence_element = pFences[i];
            auto& vk_fence_element = vk_fence_list[i];

            vk_fence_element = ((VulkanFence*)rhi_fence_element)->getResource();
        };

        VkResult result = _vkWaitForFences(m_device, fenceCount, vk_fence_list.data(), waitAll, timeout);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            if (result == VK_ERROR_DEVICE_LOST)
            {
                markDeviceLost();
            }
            LOG_ERROR("waitForFencesPFN failed (VkResult={}, fenceCount={})",
                      static_cast<int32_t>(result),
                      fenceCount);
            return false;
        }
    }

    bool VulkanRHI::resetFencesPFN(uint32_t fenceCount, RHIFence* const* pFences)
    {
        //fence
        int fence_size = fenceCount;
        std::vector<VkFence> vk_fence_list(fence_size);
        for (int i = 0; i < fence_size; ++i)
        {
            const auto& rhi_fence_element = pFences[i];
            auto& vk_fence_element = vk_fence_list[i];

            vk_fence_element = ((VulkanFence*)rhi_fence_element)->getResource();
        };

        VkResult result = _vkResetFences(m_device, fenceCount, vk_fence_list.data());

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("_vkResetFences failed!");
            return false;
        }
    }

    bool VulkanRHI::resetCommandPoolPFN(RHICommandPool* commandPool, RHICommandPoolResetFlags flags)
    {
        VkResult result = _vkResetCommandPool(m_device, ((VulkanCommandPool*)commandPool)->getResource(), (VkCommandPoolResetFlags)flags);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("_vkResetCommandPool failed!");
            return false;
        }
    }

    bool VulkanRHI::beginCommandBufferPFN(RHICommandBuffer* commandBuffer, const RHICommandBufferBeginInfo* pBeginInfo)
    {
        VkCommandBufferInheritanceInfo* command_buffer_inheritance_info_ptr = nullptr;
        VkCommandBufferInheritanceInfo command_buffer_inheritance_info{};
        if (pBeginInfo->pInheritanceInfo != nullptr)
        {
            command_buffer_inheritance_info.sType = (VkStructureType)pBeginInfo->pInheritanceInfo->sType;
            command_buffer_inheritance_info.pNext = (const void*)pBeginInfo->pInheritanceInfo->pNext;
            command_buffer_inheritance_info.renderPass = ((VulkanRenderPass*)pBeginInfo->pInheritanceInfo->renderPass)->getResource();
            command_buffer_inheritance_info.subpass = pBeginInfo->pInheritanceInfo->subpass;
            command_buffer_inheritance_info.framebuffer = ((VulkanFramebuffer*)pBeginInfo->pInheritanceInfo->framebuffer)->getResource();
            command_buffer_inheritance_info.occlusionQueryEnable = (VkBool32)pBeginInfo->pInheritanceInfo->occlusionQueryEnable;
            command_buffer_inheritance_info.queryFlags = (VkQueryControlFlags)pBeginInfo->pInheritanceInfo->queryFlags;
            command_buffer_inheritance_info.pipelineStatistics = (VkQueryPipelineStatisticFlags)pBeginInfo->pInheritanceInfo->pipelineStatistics;

            command_buffer_inheritance_info_ptr = &command_buffer_inheritance_info;
        }

        VkCommandBufferBeginInfo command_buffer_begin_info{};
        command_buffer_begin_info.sType = (VkStructureType)pBeginInfo->sType;
        command_buffer_begin_info.pNext = (const void*)pBeginInfo->pNext;
        command_buffer_begin_info.flags = (VkCommandBufferUsageFlags)pBeginInfo->flags;
        command_buffer_begin_info.pInheritanceInfo = command_buffer_inheritance_info_ptr;
        VkResult result = _vkBeginCommandBuffer(((VulkanCommandBuffer*)commandBuffer)->getResource(), &command_buffer_begin_info);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("_vkBeginCommandBuffer failed!");
            return false;
        }
    }

    bool VulkanRHI::endCommandBufferPFN(RHICommandBuffer* commandBuffer)
    {
        VkResult result = _vkEndCommandBuffer(((VulkanCommandBuffer*)commandBuffer)->getResource());

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("_vkEndCommandBuffer failed!");
            return false;
        }
    }

    void VulkanRHI::cmdBeginRenderPassPFN(RHICommandBuffer* commandBuffer, const RHIRenderPassBeginInfo* pRenderPassBegin, RHISubpassContents contents)
    {
        VkOffset2D offset_2d{};
        offset_2d.x = pRenderPassBegin->renderArea.offset.x;
        offset_2d.y = pRenderPassBegin->renderArea.offset.y;

        VkExtent2D extent_2d{};
        extent_2d.width = pRenderPassBegin->renderArea.extent.width;
        extent_2d.height = pRenderPassBegin->renderArea.extent.height;

        VkRect2D rect_2d{};
        rect_2d.offset = offset_2d;
        rect_2d.extent = extent_2d;

        //clear_values
        int clear_value_size = pRenderPassBegin->clearValueCount;
        std::vector<VkClearValue> vk_clear_value_list(clear_value_size);
        for (int i = 0; i < clear_value_size; ++i)
        {
            const auto& rhi_clear_value_element = pRenderPassBegin->pClearValues[i];
            auto& vk_clear_value_element = vk_clear_value_list[i];

            VkClearColorValue vk_clear_color_value;
            vk_clear_color_value.float32[0] = rhi_clear_value_element.color.float32[0];
            vk_clear_color_value.float32[1] = rhi_clear_value_element.color.float32[1];
            vk_clear_color_value.float32[2] = rhi_clear_value_element.color.float32[2];
            vk_clear_color_value.float32[3] = rhi_clear_value_element.color.float32[3];
            vk_clear_color_value.int32[0] = rhi_clear_value_element.color.int32[0];
            vk_clear_color_value.int32[1] = rhi_clear_value_element.color.int32[1];
            vk_clear_color_value.int32[2] = rhi_clear_value_element.color.int32[2];
            vk_clear_color_value.int32[3] = rhi_clear_value_element.color.int32[3];
            vk_clear_color_value.uint32[0] = rhi_clear_value_element.color.uint32[0];
            vk_clear_color_value.uint32[1] = rhi_clear_value_element.color.uint32[1];
            vk_clear_color_value.uint32[2] = rhi_clear_value_element.color.uint32[2];
            vk_clear_color_value.uint32[3] = rhi_clear_value_element.color.uint32[3];

            VkClearDepthStencilValue vk_clear_depth_stencil_value;
            vk_clear_depth_stencil_value.depth = rhi_clear_value_element.depthStencil.depth;
            vk_clear_depth_stencil_value.stencil = rhi_clear_value_element.depthStencil.stencil;

            vk_clear_value_element.color = vk_clear_color_value;
            vk_clear_value_element.depthStencil = vk_clear_depth_stencil_value;

        };

        VkRenderPassBeginInfo vk_render_pass_begin_info{};
        vk_render_pass_begin_info.sType = (VkStructureType)pRenderPassBegin->sType;
        vk_render_pass_begin_info.pNext = pRenderPassBegin->pNext;
        vk_render_pass_begin_info.renderPass = ((VulkanRenderPass*)pRenderPassBegin->renderPass)->getResource();
        vk_render_pass_begin_info.framebuffer = ((VulkanFramebuffer*)pRenderPassBegin->framebuffer)->getResource();
        vk_render_pass_begin_info.renderArea = rect_2d;
        vk_render_pass_begin_info.clearValueCount = pRenderPassBegin->clearValueCount;
        vk_render_pass_begin_info.pClearValues = vk_clear_value_list.data();

        return _vkCmdBeginRenderPass(((VulkanCommandBuffer*)commandBuffer)->getResource(), &vk_render_pass_begin_info, (VkSubpassContents)contents);
    }

    void VulkanRHI::cmdNextSubpassPFN(RHICommandBuffer* commandBuffer, RHISubpassContents contents)
    {
        return _vkCmdNextSubpass(((VulkanCommandBuffer*)commandBuffer)->getResource(), ((VkSubpassContents)contents));
    }

    void VulkanRHI::cmdEndRenderPassPFN(RHICommandBuffer* commandBuffer)
    {
        return _vkCmdEndRenderPass(((VulkanCommandBuffer*)commandBuffer)->getResource());
    }

    void VulkanRHI::cmdBindPipelinePFN(RHICommandBuffer* commandBuffer, RHIPipelineBindPoint pipelineBindPoint, RHIPipeline* pipeline)
    {
        return _vkCmdBindPipeline(((VulkanCommandBuffer*)commandBuffer)->getResource(), (VkPipelineBindPoint)pipelineBindPoint, ((VulkanPipeline*)pipeline)->getResource());
    }

    void VulkanRHI::cmdSetViewportPFN(RHICommandBuffer* commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const RHIViewport* pViewports)
    {
        //viewport
        int viewport_size = viewportCount;
        std::vector<VkViewport> vk_viewport_list(viewport_size);
        for (int i = 0; i < viewport_size; ++i)
        {
            const auto& rhi_viewport_element = pViewports[i];
            auto& vk_viewport_element = vk_viewport_list[i];

            vk_viewport_element.x = rhi_viewport_element.x;
            vk_viewport_element.y = rhi_viewport_element.y;
            vk_viewport_element.width = rhi_viewport_element.width;
            vk_viewport_element.height = rhi_viewport_element.height;
            vk_viewport_element.minDepth = rhi_viewport_element.minDepth;
            vk_viewport_element.maxDepth = rhi_viewport_element.maxDepth;
        };

        return _vkCmdSetViewport(((VulkanCommandBuffer*)commandBuffer)->getResource(), firstViewport, viewportCount, vk_viewport_list.data());
    }

    void VulkanRHI::cmdSetScissorPFN(RHICommandBuffer* commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const RHIRect2D* pScissors)
    {
        //rect_2d
        int rect_2d_size = scissorCount;
        std::vector<VkRect2D> vk_rect_2d_list(rect_2d_size);
        for (int i = 0; i < rect_2d_size; ++i)
        {
            const auto& rhi_rect_2d_element = pScissors[i];
            auto& vk_rect_2d_element = vk_rect_2d_list[i];

            VkOffset2D offset_2d{};
            offset_2d.x = rhi_rect_2d_element.offset.x;
            offset_2d.y = rhi_rect_2d_element.offset.y;

            VkExtent2D extent_2d{};
            extent_2d.width = rhi_rect_2d_element.extent.width;
            extent_2d.height = rhi_rect_2d_element.extent.height;

            vk_rect_2d_element.offset = (VkOffset2D)offset_2d;
            vk_rect_2d_element.extent = (VkExtent2D)extent_2d;

        };

        return _vkCmdSetScissor(((VulkanCommandBuffer*)commandBuffer)->getResource(), firstScissor, scissorCount, vk_rect_2d_list.data());
    }

    void VulkanRHI::cmdBindVertexBuffersPFN(
        RHICommandBuffer* commandBuffer,
        uint32_t firstBinding,
        uint32_t bindingCount,
        RHIBuffer* const* pBuffers,
        const RHIDeviceSize* pOffsets)
    {
        //buffer
        int buffer_size = bindingCount;
        std::vector<VkBuffer> vk_buffer_list(buffer_size);
        for (int i = 0; i < buffer_size; ++i)
        {
            const auto& rhi_buffer_element = pBuffers[i];
            auto& vk_buffer_element = vk_buffer_list[i];

            vk_buffer_element = ((VulkanBuffer*)rhi_buffer_element)->getResource();
        };

        //offset
        int offset_size = bindingCount;
        std::vector<VkDeviceSize> vk_device_size_list(offset_size);
        for (int i = 0; i < offset_size; ++i)
        {
            const auto& rhi_offset_element = pOffsets[i];
            auto& vk_offset_element = vk_device_size_list[i];

            vk_offset_element = rhi_offset_element;
        };

        return _vkCmdBindVertexBuffers(((VulkanCommandBuffer*)commandBuffer)->getResource(), firstBinding, bindingCount, vk_buffer_list.data(), vk_device_size_list.data());
    }

    void VulkanRHI::cmdBindIndexBufferPFN(RHICommandBuffer* commandBuffer, RHIBuffer* buffer, RHIDeviceSize offset, RHIIndexType indexType)
    {
        return _vkCmdBindIndexBuffer(((VulkanCommandBuffer*)commandBuffer)->getResource(), ((VulkanBuffer*)buffer)->getResource(), (VkDeviceSize)offset, (VkIndexType)indexType);
    }

    void VulkanRHI::cmdBindDescriptorSetsPFN(
        RHICommandBuffer* commandBuffer,
        RHIPipelineBindPoint pipelineBindPoint,
        RHIPipelineLayout* layout,
        uint32_t firstSet,
        uint32_t descriptorSetCount,
        const RHIDescriptorSet* const* pDescriptorSets,
        uint32_t dynamicOffsetCount,
        const uint32_t* pDynamicOffsets)
    {
        //descriptor_set
        int descriptor_set_size = descriptorSetCount;
        std::vector<VkDescriptorSet> vk_descriptor_set_list(descriptor_set_size);
        for (int i = 0; i < descriptor_set_size; ++i)
        {
            const auto& rhi_descriptor_set_element = pDescriptorSets[i];
            auto& vk_descriptor_set_element = vk_descriptor_set_list[i];

            vk_descriptor_set_element = ((VulkanDescriptorSet*)rhi_descriptor_set_element)->getResource();
        };

        //offset
        int offset_size = dynamicOffsetCount;
        std::vector<uint32_t> vk_offset_list(offset_size);
        for (int i = 0; i < offset_size; ++i)
        {
            const auto& rhi_offset_element = pDynamicOffsets[i];
            auto& vk_offset_element = vk_offset_list[i];

            vk_offset_element = rhi_offset_element;
        };

        return _vkCmdBindDescriptorSets(
            ((VulkanCommandBuffer*)commandBuffer)->getResource(),
            (VkPipelineBindPoint)pipelineBindPoint,
            ((VulkanPipelineLayout*)layout)->getResource(),
            firstSet, descriptorSetCount,
            vk_descriptor_set_list.data(),
            dynamicOffsetCount,
            vk_offset_list.data());
    }

    void VulkanRHI::cmdDrawIndexedPFN(RHICommandBuffer* commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        return _vkCmdDrawIndexed(((VulkanCommandBuffer*)commandBuffer)->getResource(), indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void VulkanRHI::cmdClearAttachmentsPFN(
        RHICommandBuffer* commandBuffer,
        uint32_t attachmentCount,
        const RHIClearAttachment* pAttachments,
        uint32_t rectCount,
        const RHIClearRect* pRects)
    {
        //clear_attachment
        int clear_attachment_size = attachmentCount;
        std::vector<VkClearAttachment> vk_clear_attachment_list(clear_attachment_size);
        for (int i = 0; i < clear_attachment_size; ++i)
        {
            const auto& rhi_clear_attachment_element = pAttachments[i];
            auto& vk_clear_attachment_element = vk_clear_attachment_list[i];

            VkClearColorValue vk_clear_color_value;
            vk_clear_color_value.float32[0] = rhi_clear_attachment_element.clearValue.color.float32[0];
            vk_clear_color_value.float32[1] = rhi_clear_attachment_element.clearValue.color.float32[1];
            vk_clear_color_value.float32[2] = rhi_clear_attachment_element.clearValue.color.float32[2];
            vk_clear_color_value.float32[3] = rhi_clear_attachment_element.clearValue.color.float32[3];
            vk_clear_color_value.int32[0] = rhi_clear_attachment_element.clearValue.color.int32[0];
            vk_clear_color_value.int32[1] = rhi_clear_attachment_element.clearValue.color.int32[1];
            vk_clear_color_value.int32[2] = rhi_clear_attachment_element.clearValue.color.int32[2];
            vk_clear_color_value.int32[3] = rhi_clear_attachment_element.clearValue.color.int32[3];
            vk_clear_color_value.uint32[0] = rhi_clear_attachment_element.clearValue.color.uint32[0];
            vk_clear_color_value.uint32[1] = rhi_clear_attachment_element.clearValue.color.uint32[1];
            vk_clear_color_value.uint32[2] = rhi_clear_attachment_element.clearValue.color.uint32[2];
            vk_clear_color_value.uint32[3] = rhi_clear_attachment_element.clearValue.color.uint32[3];

            VkClearDepthStencilValue vk_clear_depth_stencil_value;
            vk_clear_depth_stencil_value.depth = rhi_clear_attachment_element.clearValue.depthStencil.depth;
            vk_clear_depth_stencil_value.stencil = rhi_clear_attachment_element.clearValue.depthStencil.stencil;

            vk_clear_attachment_element.clearValue.color = vk_clear_color_value;
            vk_clear_attachment_element.clearValue.depthStencil = vk_clear_depth_stencil_value;
            vk_clear_attachment_element.aspectMask = rhi_clear_attachment_element.aspectMask;
            vk_clear_attachment_element.colorAttachment = rhi_clear_attachment_element.colorAttachment;
        };

        //clear_rect
        int clear_rect_size = rectCount;
        std::vector<VkClearRect> vk_clear_rect_list(clear_rect_size);
        for (int i = 0; i < clear_rect_size; ++i)
        {
            const auto& rhi_clear_rect_element = pRects[i];
            auto& vk_clear_rect_element = vk_clear_rect_list[i];

            VkOffset2D offset_2d{};
            offset_2d.x = rhi_clear_rect_element.rect.offset.x;
            offset_2d.y = rhi_clear_rect_element.rect.offset.y;

            VkExtent2D extent_2d{};
            extent_2d.width = rhi_clear_rect_element.rect.extent.width;
            extent_2d.height = rhi_clear_rect_element.rect.extent.height;

            vk_clear_rect_element.rect.offset = (VkOffset2D)offset_2d;
            vk_clear_rect_element.rect.extent = (VkExtent2D)extent_2d;
            vk_clear_rect_element.baseArrayLayer = rhi_clear_rect_element.baseArrayLayer;
            vk_clear_rect_element.layerCount = rhi_clear_rect_element.layerCount;
        };

        return _vkCmdClearAttachments(
            ((VulkanCommandBuffer*)commandBuffer)->getResource(),
            attachmentCount,
            vk_clear_attachment_list.data(),
            rectCount,
            vk_clear_rect_list.data());
    }

    bool VulkanRHI::beginCommandBuffer(RHICommandBuffer* commandBuffer, const RHICommandBufferBeginInfo* pBeginInfo)
    {
        VkCommandBufferInheritanceInfo command_buffer_inheritance_info{};
        const VkCommandBufferInheritanceInfo* command_buffer_inheritance_info_ptr = nullptr;
        if (pBeginInfo->pInheritanceInfo != nullptr)
        {
            command_buffer_inheritance_info.sType = (VkStructureType)(pBeginInfo->pInheritanceInfo->sType);
            command_buffer_inheritance_info.pNext = (const void*)pBeginInfo->pInheritanceInfo->pNext;
            command_buffer_inheritance_info.renderPass = ((VulkanRenderPass*)pBeginInfo->pInheritanceInfo->renderPass)->getResource();
            command_buffer_inheritance_info.subpass = pBeginInfo->pInheritanceInfo->subpass;
            command_buffer_inheritance_info.framebuffer = ((VulkanFramebuffer*)(pBeginInfo->pInheritanceInfo->framebuffer))->getResource();
            command_buffer_inheritance_info.occlusionQueryEnable = (VkBool32)pBeginInfo->pInheritanceInfo->occlusionQueryEnable;
            command_buffer_inheritance_info.queryFlags = (VkQueryControlFlags)pBeginInfo->pInheritanceInfo->queryFlags;
            command_buffer_inheritance_info.pipelineStatistics = (VkQueryPipelineStatisticFlags)pBeginInfo->pInheritanceInfo->pipelineStatistics;

            command_buffer_inheritance_info_ptr = &command_buffer_inheritance_info;
        }

        VkCommandBufferBeginInfo command_buffer_begin_info{};
        command_buffer_begin_info.sType = (VkStructureType)pBeginInfo->sType;
        command_buffer_begin_info.pNext = (const void*)pBeginInfo->pNext;
        command_buffer_begin_info.flags = (VkCommandBufferUsageFlags)pBeginInfo->flags;
        command_buffer_begin_info.pInheritanceInfo = command_buffer_inheritance_info_ptr;

        VkResult result = vkBeginCommandBuffer(((VulkanCommandBuffer*)commandBuffer)->getResource(), &command_buffer_begin_info);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("vkBeginCommandBuffer failed!");
            return false;
        }
    }

    bool VulkanRHI::endCommandBuffer(RHICommandBuffer* commandBuffer)
    {
        VkResult result = vkEndCommandBuffer(((VulkanCommandBuffer*)commandBuffer)->getResource());

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("vkEndCommandBuffer failed!");
            return false;
        }
    }

    void VulkanRHI::updateDescriptorSets(
        uint32_t descriptorWriteCount,
        const RHIWriteDescriptorSet* pDescriptorWrites,
        uint32_t descriptorCopyCount,
        const RHICopyDescriptorSet* pDescriptorCopies)
    {
        //write_descriptor_set
        int write_descriptor_set_size = descriptorWriteCount;
        std::vector<VkWriteDescriptorSet> vk_write_descriptor_set_list(write_descriptor_set_size);
        int image_info_count = 0;
        int buffer_info_count = 0;
        int accel_info_count = 0;
        int accel_handle_count = 0;
        for (int i = 0; i < write_descriptor_set_size; ++i)
        {
            const auto& rhi_write_descriptor_set_element = pDescriptorWrites[i];
            if (rhi_write_descriptor_set_element.pImageInfo != nullptr)
            {
                image_info_count += static_cast<int>(rhi_write_descriptor_set_element.descriptorCount);
            }
            if (rhi_write_descriptor_set_element.pBufferInfo != nullptr)
            {
                buffer_info_count += static_cast<int>(rhi_write_descriptor_set_element.descriptorCount);
            }
            if (rhi_write_descriptor_set_element.pAccelerationStructureInfo != nullptr)
            {
                accel_info_count++;
                accel_handle_count +=
                    rhi_write_descriptor_set_element.pAccelerationStructureInfo->accelerationStructureCount;
            }
        }
        std::vector<VkDescriptorImageInfo> vk_descriptor_image_info_list(image_info_count);
        std::vector<VkDescriptorBufferInfo> vk_descriptor_buffer_info_list(buffer_info_count);
        // Acceleration-structure descriptor writes are chained into VkWriteDescriptorSet::pNext; these
        // vectors are pre-sized so their addresses stay stable while the write list is populated.
        std::vector<VkWriteDescriptorSetAccelerationStructureKHR> vk_accel_write_list(accel_info_count);
        std::vector<VkAccelerationStructureKHR>                   vk_accel_handle_list(accel_handle_count);
        int image_info_current = 0;
        int buffer_info_current = 0;
        int accel_info_current = 0;
        int accel_handle_current = 0;

        for (int i = 0; i < write_descriptor_set_size; ++i)
        {
            const auto& rhi_write_descriptor_set_element = pDescriptorWrites[i];
            auto& vk_write_descriptor_set_element = vk_write_descriptor_set_list[i];

            const VkDescriptorImageInfo* vk_descriptor_image_info_ptr = nullptr;
            if (rhi_write_descriptor_set_element.pImageInfo != nullptr)
            {
                vk_descriptor_image_info_ptr = &vk_descriptor_image_info_list[image_info_current];
                for (uint32_t descriptor_index = 0;
                     descriptor_index < rhi_write_descriptor_set_element.descriptorCount;
                     ++descriptor_index)
                {
                    auto& vk_descriptor_image_info = vk_descriptor_image_info_list[image_info_current++];
                    const RHIDescriptorImageInfo& rhi_image_info =
                        rhi_write_descriptor_set_element.pImageInfo[descriptor_index];
                    if (rhi_image_info.sampler == nullptr)
                    {
                        vk_descriptor_image_info.sampler = nullptr;
                    }
                    else
                    {
                        vk_descriptor_image_info.sampler =
                            ((VulkanSampler*)rhi_image_info.sampler)->getResource();
                    }
                    if (rhi_image_info.imageView == nullptr)
                    {
                        vk_descriptor_image_info.imageView = VK_NULL_HANDLE;
                    }
                    else
                    {
                        VkImageView image_view =
                            ((VulkanImageView*)rhi_image_info.imageView)->getResource();
                        vk_descriptor_image_info.imageView = image_view != VK_NULL_HANDLE ? image_view :
                                                                                              VK_NULL_HANDLE;
                    }
                    vk_descriptor_image_info.imageLayout = (VkImageLayout)rhi_image_info.imageLayout;
                }
            }

            const VkDescriptorBufferInfo* vk_descriptor_buffer_info_ptr = nullptr;
            if (rhi_write_descriptor_set_element.pBufferInfo != nullptr)
            {
                vk_descriptor_buffer_info_ptr = &vk_descriptor_buffer_info_list[buffer_info_current];
                for (uint32_t descriptor_index = 0;
                     descriptor_index < rhi_write_descriptor_set_element.descriptorCount;
                     ++descriptor_index)
                {
                    auto& vk_descriptor_buffer_info = vk_descriptor_buffer_info_list[buffer_info_current++];
                    const RHIDescriptorBufferInfo& rhi_buffer_info =
                        rhi_write_descriptor_set_element.pBufferInfo[descriptor_index];
                    if (rhi_buffer_info.buffer == nullptr)
                    {
                        vk_descriptor_buffer_info.buffer = VK_NULL_HANDLE;
                    }
                    else
                    {
                        vk_descriptor_buffer_info.buffer =
                            ((VulkanBuffer*)rhi_buffer_info.buffer)->getResource();
                    }
                    vk_descriptor_buffer_info.offset = (VkDeviceSize)rhi_buffer_info.offset;
                    vk_descriptor_buffer_info.range  = (VkDeviceSize)rhi_buffer_info.range;
                }
            }

            vk_write_descriptor_set_element.sType = (VkStructureType)rhi_write_descriptor_set_element.sType;
            vk_write_descriptor_set_element.pNext = (const void*)rhi_write_descriptor_set_element.pNext;
            vk_write_descriptor_set_element.dstSet = ((VulkanDescriptorSet*)rhi_write_descriptor_set_element.dstSet)->getResource();
            vk_write_descriptor_set_element.dstBinding = rhi_write_descriptor_set_element.dstBinding;
            vk_write_descriptor_set_element.dstArrayElement = rhi_write_descriptor_set_element.dstArrayElement;
            vk_write_descriptor_set_element.descriptorCount = rhi_write_descriptor_set_element.descriptorCount;
            vk_write_descriptor_set_element.descriptorType = (VkDescriptorType)rhi_write_descriptor_set_element.descriptorType;
            vk_write_descriptor_set_element.pImageInfo = vk_descriptor_image_info_ptr;
            vk_write_descriptor_set_element.pBufferInfo = vk_descriptor_buffer_info_ptr;
            //vk_write_descriptor_set_element.pTexelBufferView = &((VulkanBufferView*)rhi_write_descriptor_set_element.pTexelBufferView)->getResource();

            // Acceleration structure write: translate RHI handles to Vulkan handles and chain the
            // VkWriteDescriptorSetAccelerationStructureKHR into pNext (overriding the copied pNext).
            if (rhi_write_descriptor_set_element.pAccelerationStructureInfo != nullptr)
            {
                const RHIWriteDescriptorSetAccelerationStructure* as_info =
                    rhi_write_descriptor_set_element.pAccelerationStructureInfo;
                VkWriteDescriptorSetAccelerationStructureKHR& vk_as_write =
                    vk_accel_write_list[accel_info_current++];
                vk_as_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                vk_as_write.pNext = nullptr;
                vk_as_write.accelerationStructureCount = as_info->accelerationStructureCount;

                const int handle_base = accel_handle_current;
                for (uint32_t k = 0; k < as_info->accelerationStructureCount; ++k)
                {
                    RHIAccelerationStructure* rhi_as = as_info->pAccelerationStructures[k];
                    vk_accel_handle_list[accel_handle_current++] =
                        rhi_as != nullptr ? static_cast<VulkanRHIAccelerationStructure*>(rhi_as)->handle
                                          : VK_NULL_HANDLE;
                }
                vk_as_write.pAccelerationStructures = vk_accel_handle_list.data() + handle_base;
                vk_write_descriptor_set_element.pNext = &vk_as_write;
            }
        };

        if (image_info_current != image_info_count
            || buffer_info_current != buffer_info_count)
        {
            LOG_ERROR("image_info_current != image_info_count || buffer_info_current != buffer_info_count");
            return;
        }

        //copy_descriptor_set
        int copy_descriptor_set_size = descriptorCopyCount;
        std::vector<VkCopyDescriptorSet> vk_copy_descriptor_set_list(copy_descriptor_set_size);
        for (int i = 0; i < copy_descriptor_set_size; ++i)
        {
            const auto& rhi_copy_descriptor_set_element = pDescriptorCopies[i];
            auto& vk_copy_descriptor_set_element = vk_copy_descriptor_set_list[i];

            vk_copy_descriptor_set_element.sType = (VkStructureType)rhi_copy_descriptor_set_element.sType;
            vk_copy_descriptor_set_element.pNext = (const void*)rhi_copy_descriptor_set_element.pNext;
            vk_copy_descriptor_set_element.srcSet = ((VulkanDescriptorSet*)rhi_copy_descriptor_set_element.srcSet)->getResource();
            vk_copy_descriptor_set_element.srcBinding = rhi_copy_descriptor_set_element.srcBinding;
            vk_copy_descriptor_set_element.srcArrayElement = rhi_copy_descriptor_set_element.srcArrayElement;
            vk_copy_descriptor_set_element.dstSet = ((VulkanDescriptorSet*)rhi_copy_descriptor_set_element.dstSet)->getResource();
            vk_copy_descriptor_set_element.dstBinding = rhi_copy_descriptor_set_element.dstBinding;
            vk_copy_descriptor_set_element.dstArrayElement = rhi_copy_descriptor_set_element.dstArrayElement;
            vk_copy_descriptor_set_element.descriptorCount = rhi_copy_descriptor_set_element.descriptorCount;
        };

        vkUpdateDescriptorSets(m_device, descriptorWriteCount, vk_write_descriptor_set_list.data(), descriptorCopyCount, vk_copy_descriptor_set_list.data());
    }

    bool VulkanRHI::queueSubmit(RHIQueue* queue, uint32_t submitCount, const RHISubmitInfo* pSubmits, RHIFence* fence)
    {
        //submit_info
        int command_buffer_size_total = 0;
        int semaphore_size_total = 0;
        int signal_semaphore_size_total = 0;
        int pipeline_stage_flags_size_total = 0;

        int submit_info_size = submitCount;
        for (int i = 0; i < submit_info_size; ++i)
        {
            const auto& rhi_submit_info_element = pSubmits[i];
            command_buffer_size_total += rhi_submit_info_element.commandBufferCount;
            semaphore_size_total += rhi_submit_info_element.waitSemaphoreCount;
            signal_semaphore_size_total += rhi_submit_info_element.signalSemaphoreCount;
            pipeline_stage_flags_size_total += rhi_submit_info_element.waitSemaphoreCount;
        }
        std::vector<VkCommandBuffer> vk_command_buffer_list_external(command_buffer_size_total);
        std::vector<VkSemaphore> vk_semaphore_list_external(semaphore_size_total);
        std::vector<VkSemaphore> vk_signal_semaphore_list_external(signal_semaphore_size_total);
        std::vector<VkPipelineStageFlags> vk_pipeline_stage_flags_list_external(pipeline_stage_flags_size_total);

        int command_buffer_size_current = 0;
        int semaphore_size_current = 0;
        int signal_semaphore_size_current = 0;
        int pipeline_stage_flags_size_current = 0;


        std::vector<VkSubmitInfo> vk_submit_info_list(submit_info_size);
        for (int i = 0; i < submit_info_size; ++i)
        {
            const auto& rhi_submit_info_element = pSubmits[i];
            auto& vk_submit_info_element = vk_submit_info_list[i];

            vk_submit_info_element.sType = (VkStructureType)rhi_submit_info_element.sType;
            vk_submit_info_element.pNext = (const void*)rhi_submit_info_element.pNext;

            //command_buffer
            if (rhi_submit_info_element.commandBufferCount > 0)
            {
                vk_submit_info_element.commandBufferCount = rhi_submit_info_element.commandBufferCount;
                vk_submit_info_element.pCommandBuffers = &vk_command_buffer_list_external[command_buffer_size_current];
                int command_buffer_size = rhi_submit_info_element.commandBufferCount;
                for (int i = 0; i < command_buffer_size; ++i)
                {
                    const auto& rhi_command_buffer_element = rhi_submit_info_element.pCommandBuffers[i];
                    auto& vk_command_buffer_element = vk_command_buffer_list_external[command_buffer_size_current];

                    vk_command_buffer_element = ((VulkanCommandBuffer*)rhi_command_buffer_element)->getResource();

                    command_buffer_size_current++;
                };
            }

            //semaphore
            if (rhi_submit_info_element.waitSemaphoreCount > 0)
            {
                vk_submit_info_element.waitSemaphoreCount = rhi_submit_info_element.waitSemaphoreCount;
                vk_submit_info_element.pWaitSemaphores = &vk_semaphore_list_external[semaphore_size_current];
                int semaphore_size = rhi_submit_info_element.waitSemaphoreCount;
                for (int i = 0; i < semaphore_size; ++i)
                {
                    const auto& rhi_semaphore_element = rhi_submit_info_element.pWaitSemaphores[i];
                    auto& vk_semaphore_element = vk_semaphore_list_external[semaphore_size_current];

                    vk_semaphore_element = ((VulkanSemaphore*)rhi_semaphore_element)->getResource();

                    semaphore_size_current++;
                };
            }

            //signal_semaphore
            if (rhi_submit_info_element.signalSemaphoreCount > 0)
            {
                vk_submit_info_element.signalSemaphoreCount = rhi_submit_info_element.signalSemaphoreCount;
                vk_submit_info_element.pSignalSemaphores = &vk_signal_semaphore_list_external[signal_semaphore_size_current];
                int signal_semaphore_size = rhi_submit_info_element.signalSemaphoreCount;
                for (int i = 0; i < signal_semaphore_size; ++i)
                {
                    const auto& rhi_signal_semaphore_element = rhi_submit_info_element.pSignalSemaphores[i];
                    auto& vk_signal_semaphore_element = vk_signal_semaphore_list_external[signal_semaphore_size_current];

                    vk_signal_semaphore_element = ((VulkanSemaphore*)rhi_signal_semaphore_element)->getResource();

                    signal_semaphore_size_current++;
                };
            }

            //pipeline_stage_flags
            if (rhi_submit_info_element.waitSemaphoreCount > 0)
            {
                vk_submit_info_element.pWaitDstStageMask = &vk_pipeline_stage_flags_list_external[pipeline_stage_flags_size_current];
                int pipeline_stage_flags_size = rhi_submit_info_element.waitSemaphoreCount;
                for (int i = 0; i < pipeline_stage_flags_size; ++i)
                {
                    const auto& rhi_pipeline_stage_flags_element = rhi_submit_info_element.pWaitDstStageMask[i];
                    auto& vk_pipeline_stage_flags_element = vk_pipeline_stage_flags_list_external[pipeline_stage_flags_size_current];

                    vk_pipeline_stage_flags_element = (VkPipelineStageFlags)rhi_pipeline_stage_flags_element;

                    pipeline_stage_flags_size_current++;
                };
            }
        };
        

        if ((command_buffer_size_total != command_buffer_size_current)
            || (semaphore_size_total != semaphore_size_current)
            || (signal_semaphore_size_total != signal_semaphore_size_current)
            || (pipeline_stage_flags_size_total != pipeline_stage_flags_size_current))
        {
            LOG_ERROR("submit info is not right!");
            return false;
        }

        VkFence vk_fence = VK_NULL_HANDLE;
        if (fence != nullptr)
        {
            vk_fence = ((VulkanFence*)fence)->getResource();
        }

        VkResult result = vkQueueSubmit(((VulkanQueue*)queue)->getResource(), submitCount, vk_submit_info_list.data(), vk_fence);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            if (result == VK_ERROR_DEVICE_LOST)
            {
                markDeviceLost();
            }
            LOG_ERROR("vkQueueSubmit failed (VkResult={}, frame_index={})",
                      static_cast<int32_t>(result),
                      static_cast<uint32_t>(m_current_frame_index));
            return false;
        }
    }

    bool VulkanRHI::queueWaitIdle(RHIQueue* queue)
    {
        VkResult result = vkQueueWaitIdle(((VulkanQueue*)queue)->getResource());

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("vkQueueWaitIdle failed!");
            return false;
        }
    }

    RHIBackendType VulkanRHI::getBackendType() const
    {
        return RHIBackendType::Vulkan;
    }

    void VulkanRHI::cmdPipelineBarrier(RHICommandBuffer* commandBuffer,
        RHIPipelineStageFlags srcStageMask,
        RHIPipelineStageFlags dstStageMask,
        RHIDependencyFlags dependencyFlags,
        uint32_t memoryBarrierCount,
        const RHIMemoryBarrier* pMemoryBarriers,
        uint32_t bufferMemoryBarrierCount,
        const RHIBufferMemoryBarrier* pBufferMemoryBarriers,
        uint32_t imageMemoryBarrierCount,
        const RHIImageMemoryBarrier* pImageMemoryBarriers)
    {

        //memory_barrier
        int memory_barrier_size = memoryBarrierCount;
        std::vector<VkMemoryBarrier> vk_memory_barrier_list(memory_barrier_size);
        for (int i = 0; i < memory_barrier_size; ++i)
        {
            const auto& rhi_memory_barrier_element = pMemoryBarriers[i];
            auto& vk_memory_barrier_element = vk_memory_barrier_list[i];


            vk_memory_barrier_element.sType = (VkStructureType)rhi_memory_barrier_element.sType;
            vk_memory_barrier_element.pNext = (const void*)rhi_memory_barrier_element.pNext;
            vk_memory_barrier_element.srcAccessMask = (VkAccessFlags)rhi_memory_barrier_element.srcAccessMask;
            vk_memory_barrier_element.dstAccessMask = (VkAccessFlags)rhi_memory_barrier_element.dstAccessMask;
        };

        //buffer_memory_barrier
        int buffer_memory_barrier_size = bufferMemoryBarrierCount;
        std::vector<VkBufferMemoryBarrier> vk_buffer_memory_barrier_list(buffer_memory_barrier_size);
        for (int i = 0; i < buffer_memory_barrier_size; ++i)
        {
            const auto& rhi_buffer_memory_barrier_element = pBufferMemoryBarriers[i];
            auto& vk_buffer_memory_barrier_element = vk_buffer_memory_barrier_list[i];

            vk_buffer_memory_barrier_element.sType = (VkStructureType)rhi_buffer_memory_barrier_element.sType;
            vk_buffer_memory_barrier_element.pNext = (const void*)rhi_buffer_memory_barrier_element.pNext;
            vk_buffer_memory_barrier_element.srcAccessMask = (VkAccessFlags)rhi_buffer_memory_barrier_element.srcAccessMask;
            vk_buffer_memory_barrier_element.dstAccessMask = (VkAccessFlags)rhi_buffer_memory_barrier_element.dstAccessMask;
            vk_buffer_memory_barrier_element.srcQueueFamilyIndex = rhi_buffer_memory_barrier_element.srcQueueFamilyIndex;
            vk_buffer_memory_barrier_element.dstQueueFamilyIndex = rhi_buffer_memory_barrier_element.dstQueueFamilyIndex;
            vk_buffer_memory_barrier_element.buffer = ((VulkanBuffer*)rhi_buffer_memory_barrier_element.buffer)->getResource();
            vk_buffer_memory_barrier_element.offset = (VkDeviceSize)rhi_buffer_memory_barrier_element.offset;
            vk_buffer_memory_barrier_element.size = (VkDeviceSize)rhi_buffer_memory_barrier_element.size;
        };

        //image_memory_barrier
        int image_memory_barrier_size = imageMemoryBarrierCount;
        std::vector<VkImageMemoryBarrier> vk_image_memory_barrier_list(image_memory_barrier_size);
        for (int i = 0; i < image_memory_barrier_size; ++i)
        {
            const auto& rhi_image_memory_barrier_element = pImageMemoryBarriers[i];
            auto& vk_image_memory_barrier_element = vk_image_memory_barrier_list[i];

            VkImageSubresourceRange image_subresource_range{};
            image_subresource_range.aspectMask = (VkImageAspectFlags)rhi_image_memory_barrier_element.subresourceRange.aspectMask;
            image_subresource_range.baseMipLevel = rhi_image_memory_barrier_element.subresourceRange.baseMipLevel;
            image_subresource_range.levelCount = rhi_image_memory_barrier_element.subresourceRange.levelCount;
            image_subresource_range.baseArrayLayer = rhi_image_memory_barrier_element.subresourceRange.baseArrayLayer;
            image_subresource_range.layerCount = rhi_image_memory_barrier_element.subresourceRange.layerCount;

            vk_image_memory_barrier_element.sType = (VkStructureType)rhi_image_memory_barrier_element.sType;
            vk_image_memory_barrier_element.pNext = (const void*)rhi_image_memory_barrier_element.pNext;
            vk_image_memory_barrier_element.srcAccessMask = (VkAccessFlags)rhi_image_memory_barrier_element.srcAccessMask;
            vk_image_memory_barrier_element.dstAccessMask = (VkAccessFlags)rhi_image_memory_barrier_element.dstAccessMask;
            vk_image_memory_barrier_element.oldLayout = (VkImageLayout)rhi_image_memory_barrier_element.oldLayout;
            vk_image_memory_barrier_element.newLayout = (VkImageLayout)rhi_image_memory_barrier_element.newLayout;
            vk_image_memory_barrier_element.srcQueueFamilyIndex = rhi_image_memory_barrier_element.srcQueueFamilyIndex;
            vk_image_memory_barrier_element.dstQueueFamilyIndex = rhi_image_memory_barrier_element.dstQueueFamilyIndex;
            vk_image_memory_barrier_element.image = ((VulkanImage*)rhi_image_memory_barrier_element.image)->getResource();
            vk_image_memory_barrier_element.subresourceRange = image_subresource_range;
        };

        vkCmdPipelineBarrier(
            ((VulkanCommandBuffer*)commandBuffer)->getResource(),
            (RHIPipelineStageFlags)srcStageMask,
            (RHIPipelineStageFlags)dstStageMask,
            (RHIDependencyFlags)dependencyFlags,
            memoryBarrierCount,
            vk_memory_barrier_list.data(),
            bufferMemoryBarrierCount,
            vk_buffer_memory_barrier_list.data(),
            imageMemoryBarrierCount,
            vk_image_memory_barrier_list.data());
    }

    void VulkanRHI::cmdDraw(RHICommandBuffer* commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        vkCmdDraw(((VulkanCommandBuffer*)commandBuffer)->getResource(), vertexCount, instanceCount, firstVertex, firstInstance);
    }
    
    void VulkanRHI::cmdDispatch(RHICommandBuffer* commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        vkCmdDispatch(((VulkanCommandBuffer*)commandBuffer)->getResource(), groupCountX, groupCountY, groupCountZ);
    }

    void VulkanRHI::cmdDispatchIndirect(RHICommandBuffer* commandBuffer, RHIBuffer* buffer, RHIDeviceSize offset)
    {
        vkCmdDispatchIndirect(((VulkanCommandBuffer*)commandBuffer)->getResource(), ((VulkanBuffer*)buffer)->getResource(), offset);
    }

    RHIRayTracingCapabilities VulkanRHI::getRayTracingCapabilities() const
    {
        return m_ray_tracing_capabilities;
    }

    bool VulkanRHI::createAccelerationStructure(const RHIAccelerationStructureBuildDesc* build_desc,
                                                RHIAccelerationStructure*& acceleration_structure)
    {
        acceleration_structure = nullptr;
        if (!m_ray_tracing_enabled || build_desc == nullptr)
        {
            return false;
        }

        const bool is_tlas = build_desc->type == RHIAccelerationStructureType::TopLevel;

        // Build the geometry description (device addresses are needed only for the final build; sizing
        // uses primitive counts, so instance-data address may stay 0 here).
        std::vector<VkAccelerationStructureGeometryKHR> geometries;
        std::vector<uint32_t>                           max_primitive_counts;
        if (is_tlas)
        {
            VkAccelerationStructureGeometryKHR geo {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
            geo.geometryType                          = VK_GEOMETRY_TYPE_INSTANCES_KHR;
            geo.flags                                 = VK_GEOMETRY_OPAQUE_BIT_KHR;
            geo.geometry.instances.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
            geo.geometry.instances.arrayOfPointers    = VK_FALSE;
            geometries.push_back(geo);
            max_primitive_counts.push_back(build_desc->instance_count);
        }
        else
        {
            for (uint32_t g = 0; g < build_desc->geometry_count; ++g)
            {
                const RHIAccelerationStructureGeometryDesc& src = build_desc->geometries[g];
                VkAccelerationStructureGeometryKHR          geo {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
                geo.geometryType       = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                geo.flags              = src.opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
                geo.geometry.triangles.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                geo.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                geo.geometry.triangles.vertexStride = src.vertex_stride;
                geo.geometry.triangles.maxVertex    = src.vertex_count > 0 ? src.vertex_count - 1 : 0;
                geo.geometry.triangles.indexType    = static_cast<VkIndexType>(src.index_type);
                geometries.push_back(geo);
                max_primitive_counts.push_back(src.index_count / 3);
            }
        }

        VkAccelerationStructureBuildGeometryInfoKHR build_info {
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        build_info.type          = is_tlas ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
                                           : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build_info.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        if (build_desc->allow_update)
        {
            build_info.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        }
        build_info.mode           = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build_info.geometryCount  = static_cast<uint32_t>(geometries.size());
        build_info.pGeometries    = geometries.data();

        VkAccelerationStructureBuildSizesInfoKHR size_info {
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        _vkGetAccelerationStructureBuildSizes(m_device,
                                              VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                              &build_info,
                                              max_primitive_counts.data(),
                                              &size_info);

        auto* impl = new VulkanRHIAccelerationStructure();
        impl->type = build_desc->type;

        // Acceleration structure backing storage.
        VulkanUtil::createBuffer(m_physical_device,
                                 m_device,
                                 size_info.accelerationStructureSize,
                                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 impl->as_buffer,
                                 impl->as_memory);

        VkAccelerationStructureCreateInfoKHR as_create_info {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        as_create_info.buffer = impl->as_buffer;
        as_create_info.size   = size_info.accelerationStructureSize;
        as_create_info.type   = build_info.type;
        if (_vkCreateAccelerationStructure(m_device, &as_create_info, nullptr, &impl->handle) != VK_SUCCESS)
        {
            LOG_ERROR("vkCreateAccelerationStructureKHR failed");
            RHIAccelerationStructure* to_destroy = impl;
            destroyAccelerationStructure(to_destroy);
            return false;
        }

        // Scratch buffer (sized for the larger of build/update, aligned per device requirement).
        const VkDeviceSize scratch_alignment =
            m_acceleration_structure_properties.minAccelerationStructureScratchOffsetAlignment;
        const VkDeviceSize scratch_size =
            alignUpSize((std::max)(size_info.buildScratchSize, size_info.updateScratchSize), scratch_alignment);
        VulkanUtil::createBuffer(m_physical_device,
                                 m_device,
                                 scratch_size,
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 impl->scratch_buffer,
                                 impl->scratch_memory);

        VkAccelerationStructureDeviceAddressInfoKHR address_info {
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        address_info.accelerationStructure = impl->handle;
        impl->device_address               = _vkGetAccelerationStructureDeviceAddress(m_device, &address_info);

        // TLAS: host-visible instance buffer written each build.
        if (is_tlas)
        {
            const VkDeviceSize instance_buffer_size =
                sizeof(VkAccelerationStructureInstanceKHR) * (std::max)(1u, build_desc->instance_count);
            VulkanUtil::createBuffer(m_physical_device,
                                     m_device,
                                     instance_buffer_size,
                                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                     impl->instance_buffer,
                                     impl->instance_memory);
            vkMapMemory(m_device, impl->instance_memory, 0, instance_buffer_size, 0, &impl->instance_mapped);
        }

        acceleration_structure = impl;
        return true;
    }

    bool VulkanRHI::buildAccelerationStructure(RHICommandBuffer* command_buffer,
                                               const RHIAccelerationStructureBuildDesc* build_desc,
                                               RHIAccelerationStructure* acceleration_structure)
    {
        if (!m_ray_tracing_enabled || command_buffer == nullptr || build_desc == nullptr ||
            acceleration_structure == nullptr)
        {
            return false;
        }

        auto*        impl       = static_cast<VulkanRHIAccelerationStructure*>(acceleration_structure);
        VkCommandBuffer cmd      = static_cast<VulkanCommandBuffer*>(command_buffer)->getResource();
        const bool   is_tlas    = build_desc->type == RHIAccelerationStructureType::TopLevel;

        std::vector<VkAccelerationStructureGeometryKHR>       geometries;
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;

        if (is_tlas)
        {
            // Populate the instance buffer for this build.
            auto* dst = static_cast<VkAccelerationStructureInstanceKHR*>(impl->instance_mapped);
            for (uint32_t i = 0; i < build_desc->instance_count; ++i)
            {
                const RHIAccelerationStructureInstanceDesc& src = build_desc->instances[i];
                VkAccelerationStructureInstanceKHR          inst {};
                inst.transform                              = toVkTransformMatrix(src.row_major_3x4_transform);
                inst.instanceCustomIndex                    = src.instance_id & 0xFFFFFF;
                inst.mask                                   = src.instance_mask;
                inst.instanceShaderBindingTableRecordOffset = src.hit_group_index & 0xFFFFFF;
                inst.flags = src.force_opaque ? VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR : 0;
                inst.accelerationStructureReference =
                    src.bottom_level_as
                        ? static_cast<VulkanRHIAccelerationStructure*>(src.bottom_level_as)->device_address
                        : 0;
                if (dst != nullptr)
                {
                    dst[i] = inst;
                }
            }

            VkAccelerationStructureGeometryKHR geo {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
            geo.geometryType                       = VK_GEOMETRY_TYPE_INSTANCES_KHR;
            geo.flags                              = VK_GEOMETRY_OPAQUE_BIT_KHR;
            geo.geometry.instances.sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
            geo.geometry.instances.arrayOfPointers = VK_FALSE;
            geo.geometry.instances.data.deviceAddress = getBufferDeviceAddress(impl->instance_buffer);
            geometries.push_back(geo);

            VkAccelerationStructureBuildRangeInfoKHR range {};
            range.primitiveCount = build_desc->instance_count;
            ranges.push_back(range);
        }
        else
        {
            for (uint32_t g = 0; g < build_desc->geometry_count; ++g)
            {
                const RHIAccelerationStructureGeometryDesc& src = build_desc->geometries[g];
                VkAccelerationStructureGeometryKHR          geo {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
                geo.geometryType       = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                geo.flags              = src.opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
                geo.geometry.triangles.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                geo.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                geo.geometry.triangles.vertexData.deviceAddress =
                    getBufferDeviceAddress(static_cast<VulkanBuffer*>(src.vertex_position_buffer)->getResource()) +
                    src.vertex_position_offset;
                geo.geometry.triangles.vertexStride = src.vertex_stride;
                geo.geometry.triangles.maxVertex    = src.vertex_count > 0 ? src.vertex_count - 1 : 0;
                geo.geometry.triangles.indexType    = static_cast<VkIndexType>(src.index_type);
                geo.geometry.triangles.indexData.deviceAddress =
                    src.index_buffer
                        ? getBufferDeviceAddress(static_cast<VulkanBuffer*>(src.index_buffer)->getResource()) +
                              src.index_offset
                        : 0;
                geometries.push_back(geo);

                VkAccelerationStructureBuildRangeInfoKHR range {};
                range.primitiveCount = src.index_count / 3;
                ranges.push_back(range);
            }
        }

        const bool do_update = build_desc->perform_update && build_desc->allow_update;

        VkAccelerationStructureBuildGeometryInfoKHR build_info {
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        build_info.type  = is_tlas ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
                                   : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        if (build_desc->allow_update)
        {
            build_info.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        }
        build_info.mode                     = do_update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                                                        : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build_info.srcAccelerationStructure = do_update ? impl->handle : VK_NULL_HANDLE;
        build_info.dstAccelerationStructure = impl->handle;
        build_info.geometryCount            = static_cast<uint32_t>(geometries.size());
        build_info.pGeometries              = geometries.data();
        build_info.scratchData.deviceAddress = getBufferDeviceAddress(impl->scratch_buffer);

        const VkAccelerationStructureBuildRangeInfoKHR* range_ptr = ranges.data();
        _vkCmdBuildAccelerationStructures(cmd, 1, &build_info, &range_ptr);

        // Ensure the build completes before the structure is read (by a subsequent TLAS build or a trace).
        VkMemoryBarrier barrier {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                                 VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                             0,
                             1,
                             &barrier,
                             0,
                             nullptr,
                             0,
                             nullptr);
        return true;
    }

    bool VulkanRHI::createRayTracingPipeline(const RHIRayTracingPipelineCreateInfo* create_info,
                                             RHIPipeline*& pipeline)
    {
        pipeline = nullptr;
        if (!m_ray_tracing_enabled || create_info == nullptr || create_info->shader_library.bytecode == nullptr ||
            create_info->layout == nullptr)
        {
            return false;
        }

        // The shader library is a single SPIR-V module exposing multiple ray tracing entry points
        // (raygen/miss/closesthit), mirroring the DXIL library used by the D3D12 backend.
        VkShaderModuleCreateInfo module_info {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module_info.codeSize = create_info->shader_library.bytecode_size;
        module_info.pCode    = reinterpret_cast<const uint32_t*>(create_info->shader_library.bytecode);
        VkShaderModule shader_module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(m_device, &module_info, nullptr, &shader_module) != VK_SUCCESS)
        {
            LOG_ERROR("failed to create ray tracing shader module");
            return false;
        }
        auto narrow = [](const wchar_t* w) {
            std::string s;
            if (w != nullptr)
            {
                for (; *w != 0; ++w)
                {
                    s.push_back(static_cast<char>(*w));
                }
            }
            return s;
        };
        const std::string raygen_name = narrow(create_info->shader_library.raygen_export);
        const std::string miss_name   = narrow(create_info->shader_library.miss_export);
        const std::string chit_name   = narrow(create_info->shader_library.closest_hit_export);

        VkPipelineShaderStageCreateInfo stages[3] {};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        stages[0].module = shader_module;
        stages[0].pName  = raygen_name.c_str();
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
        stages[1].module = shader_module;
        stages[1].pName  = miss_name.c_str();
        stages[2].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[2].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        stages[2].module = shader_module;
        stages[2].pName  = chit_name.c_str();

        VkRayTracingShaderGroupCreateInfoKHR groups[3] {};
        for (auto& group : groups)
        {
            group.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            group.generalShader      = VK_SHADER_UNUSED_KHR;
            group.closestHitShader   = VK_SHADER_UNUSED_KHR;
            group.anyHitShader       = VK_SHADER_UNUSED_KHR;
            group.intersectionShader = VK_SHADER_UNUSED_KHR;
        }
        groups[0].type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[0].generalShader = 0; // raygen
        groups[1].type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[1].generalShader = 1; // miss
        groups[2].type            = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        groups[2].closestHitShader = 2; // closest hit

        VkRayTracingPipelineCreateInfoKHR pipeline_info {VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
        pipeline_info.stageCount = 3;
        pipeline_info.pStages    = stages;
        pipeline_info.groupCount = 3;
        pipeline_info.pGroups    = groups;
        pipeline_info.maxPipelineRayRecursionDepth =
            (std::min)(create_info->max_recursion_depth, m_ray_tracing_pipeline_properties.maxRayRecursionDepth);
        pipeline_info.layout = static_cast<VulkanPipelineLayout*>(create_info->layout)->getResource();

        VkPipeline vk_pipeline = VK_NULL_HANDLE;
        const         VkResult result  = _vkCreateRayTracingPipelines(
            m_device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &vk_pipeline);
        vkDestroyShaderModule(m_device, shader_module, nullptr);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("vkCreateRayTracingPipelinesKHR failed");
            return false;
        }

        auto* impl = new VulkanPipeline();
        impl->setResource(vk_pipeline);
        pipeline = impl;
        return true;
    }

    bool VulkanRHI::createShaderBindingTable(const RHIShaderBindingTableCreateInfo* create_info,
                                             RHIShaderBindingTable*& shader_binding_table)
    {
        shader_binding_table = nullptr;
        if (!m_ray_tracing_enabled || create_info == nullptr || create_info->ray_tracing_pipeline == nullptr)
        {
            return false;
        }
        VkPipeline vk_pipeline = static_cast<VulkanPipeline*>(create_info->ray_tracing_pipeline)->getResource();

        const uint32_t handle_size       = m_ray_tracing_pipeline_properties.shaderGroupHandleSize;
        const uint32_t handle_alignment  = m_ray_tracing_pipeline_properties.shaderGroupHandleAlignment;
        const uint32_t base_alignment    = m_ray_tracing_pipeline_properties.shaderGroupBaseAlignment;
        const uint32_t handle_aligned    = static_cast<uint32_t>(alignUpSize(handle_size, handle_alignment));
        const uint32_t group_count       = 3;

        std::vector<uint8_t> handles(static_cast<size_t>(handle_size) * group_count);
        if (_vkGetRayTracingShaderGroupHandles(
                m_device, vk_pipeline, 0, group_count, handles.size(), handles.data()) != VK_SUCCESS)
        {
            LOG_ERROR("vkGetRayTracingShaderGroupHandlesKHR failed");
            return false;
        }

        // One record per region (raygen/miss/hit), each region base-aligned; raygen size must equal stride.
        const VkDeviceSize raygen_stride = alignUpSize(handle_aligned, base_alignment);
        const VkDeviceSize raygen_size   = raygen_stride;
        const VkDeviceSize miss_offset   = raygen_size;
        const VkDeviceSize miss_stride   = handle_aligned;
        const VkDeviceSize miss_size     = alignUpSize(miss_stride, base_alignment);
        const VkDeviceSize hit_offset    = miss_offset + miss_size;
        const VkDeviceSize hit_stride    = handle_aligned;
        const VkDeviceSize hit_size      = alignUpSize(hit_stride, base_alignment);
        const VkDeviceSize total_size    = hit_offset + hit_size;

        auto* impl = new VulkanRHIShaderBindingTable();
        VulkanUtil::createBuffer(m_physical_device,
                                 m_device,
                                 total_size,
                                 VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 impl->buffer,
                                 impl->memory);

        void* mapped = nullptr;
        vkMapMemory(m_device, impl->memory, 0, total_size, 0, &mapped);
        auto* dst = static_cast<uint8_t*>(mapped);
        std::memcpy(dst, handles.data() + 0 * handle_size, handle_size);                        // raygen
        std::memcpy(dst + miss_offset, handles.data() + 1 * handle_size, handle_size);          // miss
        std::memcpy(dst + hit_offset, handles.data() + 2 * handle_size, handle_size);           // hit
        vkUnmapMemory(m_device, impl->memory);

        const VkDeviceAddress base_address = getBufferDeviceAddress(impl->buffer);
        impl->raygen_region.deviceAddress  = base_address;
        impl->raygen_region.stride         = raygen_stride;
        impl->raygen_region.size           = raygen_size;
        impl->miss_region.deviceAddress    = base_address + miss_offset;
        impl->miss_region.stride           = miss_stride;
        impl->miss_region.size             = miss_size;
        impl->hit_region.deviceAddress     = base_address + hit_offset;
        impl->hit_region.stride            = hit_stride;
        impl->hit_region.size              = hit_size;

        shader_binding_table = impl;
        return true;
    }

    void VulkanRHI::cmdTraceRays(RHICommandBuffer* command_buffer, const RHIRayTracingDispatchDesc* dispatch_desc)
    {
        if (!m_ray_tracing_enabled || command_buffer == nullptr || dispatch_desc == nullptr ||
            dispatch_desc->shader_binding_table == nullptr)
        {
            return;
        }
        VkCommandBuffer cmd = static_cast<VulkanCommandBuffer*>(command_buffer)->getResource();
        auto*           sbt = static_cast<VulkanRHIShaderBindingTable*>(dispatch_desc->shader_binding_table);
        _vkCmdTraceRays(cmd,
                        &sbt->raygen_region,
                        &sbt->miss_region,
                        &sbt->hit_region,
                        &sbt->callable_region,
                        dispatch_desc->width,
                        dispatch_desc->height,
                        dispatch_desc->depth);
    }

    void VulkanRHI::destroyAccelerationStructure(RHIAccelerationStructure*& acceleration_structure)
    {
        if (acceleration_structure != nullptr && m_device != nullptr)
        {
            auto* impl = static_cast<VulkanRHIAccelerationStructure*>(acceleration_structure);
            if (impl->instance_mapped != nullptr && impl->instance_memory != VK_NULL_HANDLE)
            {
                vkUnmapMemory(m_device, impl->instance_memory);
            }
            if (impl->handle != VK_NULL_HANDLE && _vkDestroyAccelerationStructure != nullptr)
            {
                _vkDestroyAccelerationStructure(m_device, impl->handle, nullptr);
            }
            auto free_buffer = [this](VkBuffer& b, VkDeviceMemory& m) {
                if (b != VK_NULL_HANDLE)
                {
                    vkDestroyBuffer(m_device, b, nullptr);
                    b = VK_NULL_HANDLE;
                }
                if (m != VK_NULL_HANDLE)
                {
                    vkFreeMemory(m_device, m, nullptr);
                    m = VK_NULL_HANDLE;
                }
            };
            free_buffer(impl->as_buffer, impl->as_memory);
            free_buffer(impl->scratch_buffer, impl->scratch_memory);
            free_buffer(impl->instance_buffer, impl->instance_memory);
            delete impl;
        }
        acceleration_structure = nullptr;
    }

    void VulkanRHI::destroyShaderBindingTable(RHIShaderBindingTable*& shader_binding_table)
    {
        if (shader_binding_table != nullptr && m_device != nullptr)
        {
            auto* impl = static_cast<VulkanRHIShaderBindingTable*>(shader_binding_table);
            if (impl->buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(m_device, impl->buffer, nullptr);
            }
            if (impl->memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(m_device, impl->memory, nullptr);
            }
            delete impl;
        }
        shader_binding_table = nullptr;
    }

    void VulkanRHI::destroyRayTracingPipeline(RHIPipeline*& pipeline)
    {
        if (pipeline != nullptr && m_device != nullptr)
        {
            VkPipeline vk_pipeline = static_cast<VulkanPipeline*>(pipeline)->getResource();
            if (vk_pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(m_device, vk_pipeline, nullptr);
            }
            delete pipeline;
        }
        pipeline = nullptr;
    }

    void VulkanRHI::cmdCopyImageToBuffer(
        RHICommandBuffer* commandBuffer,
        RHIImage* srcImage,
        RHIImageLayout srcImageLayout,
        RHIBuffer* dstBuffer,
        uint32_t regionCount,
        const RHIBufferImageCopy* pRegions)
    {
        //buffer_image_copy
        int buffer_image_copy_size = regionCount;
        std::vector<VkBufferImageCopy> vk_buffer_image_copy_list(buffer_image_copy_size);
        for (int i = 0; i < buffer_image_copy_size; ++i)
        {
            const auto& rhi_buffer_image_copy_element = pRegions[i];
            auto& vk_buffer_image_copy_element = vk_buffer_image_copy_list[i];

            VkImageSubresourceLayers image_subresource_layers{};
            image_subresource_layers.aspectMask = (VkImageAspectFlags)rhi_buffer_image_copy_element.imageSubresource.aspectMask;
            image_subresource_layers.mipLevel = rhi_buffer_image_copy_element.imageSubresource.mipLevel;
            image_subresource_layers.baseArrayLayer = rhi_buffer_image_copy_element.imageSubresource.baseArrayLayer;
            image_subresource_layers.layerCount = rhi_buffer_image_copy_element.imageSubresource.layerCount;

            VkOffset3D offset_3d{};
            offset_3d.x = rhi_buffer_image_copy_element.imageOffset.x;
            offset_3d.y = rhi_buffer_image_copy_element.imageOffset.y;
            offset_3d.z = rhi_buffer_image_copy_element.imageOffset.z;

            VkExtent3D extent_3d{};
            extent_3d.width = rhi_buffer_image_copy_element.imageExtent.width;
            extent_3d.height = rhi_buffer_image_copy_element.imageExtent.height;
            extent_3d.depth = rhi_buffer_image_copy_element.imageExtent.depth;

            VkBufferImageCopy buffer_image_copy{};
            buffer_image_copy.bufferOffset = (VkDeviceSize)rhi_buffer_image_copy_element.bufferOffset;
            buffer_image_copy.bufferRowLength = rhi_buffer_image_copy_element.bufferRowLength;
            buffer_image_copy.bufferImageHeight = rhi_buffer_image_copy_element.bufferImageHeight;
            buffer_image_copy.imageSubresource = image_subresource_layers;
            buffer_image_copy.imageOffset = offset_3d;
            buffer_image_copy.imageExtent = extent_3d;

            vk_buffer_image_copy_element.bufferOffset = (VkDeviceSize)rhi_buffer_image_copy_element.bufferOffset;
            vk_buffer_image_copy_element.bufferRowLength = rhi_buffer_image_copy_element.bufferRowLength;
            vk_buffer_image_copy_element.bufferImageHeight = rhi_buffer_image_copy_element.bufferImageHeight;
            vk_buffer_image_copy_element.imageSubresource = image_subresource_layers;
            vk_buffer_image_copy_element.imageOffset = offset_3d;
            vk_buffer_image_copy_element.imageExtent = extent_3d;
        };

        vkCmdCopyImageToBuffer(
            ((VulkanCommandBuffer*)commandBuffer)->getResource(),
            ((VulkanImage*)srcImage)->getResource(),
            (VkImageLayout)srcImageLayout,
            ((VulkanBuffer*)dstBuffer)->getResource(),
            regionCount,
            vk_buffer_image_copy_list.data());
    }

    void VulkanRHI::cmdCopyImageToImage(RHICommandBuffer* commandBuffer, RHIImage* srcImage, RHIImage* dstImage, uint32_t regionCount, const RHIImageBlit* pRegions)
    {
        if (commandBuffer == nullptr ||
            srcImage == nullptr ||
            dstImage == nullptr ||
            pRegions == nullptr ||
            regionCount == 0)
        {
            return;
        }

        std::vector<VkImageCopy> image_copy_regions;
        image_copy_regions.reserve(regionCount);
        for (uint32_t region_index = 0; region_index < regionCount; ++region_index)
        {
            const RHIImageBlit& region = pRegions[region_index];
            const int32_t width        = region.srcOffsets[1].x - region.srcOffsets[0].x;
            const int32_t height       = region.srcOffsets[1].y - region.srcOffsets[0].y;
            const int32_t depth        = region.srcOffsets[1].z - region.srcOffsets[0].z;
            if (width <= 0 || height <= 0)
            {
                continue;
            }

            VkImageCopy image_copy {};
            image_copy.srcSubresource = {static_cast<VkImageAspectFlags>(region.srcSubresource.aspectMask),
                                         region.srcSubresource.mipLevel,
                                         region.srcSubresource.baseArrayLayer,
                                         (std::max)(1U, region.srcSubresource.layerCount)};
            image_copy.srcOffset = {region.srcOffsets[0].x,
                                    region.srcOffsets[0].y,
                                    region.srcOffsets[0].z};
            image_copy.dstSubresource = {static_cast<VkImageAspectFlags>(region.dstSubresource.aspectMask),
                                         region.dstSubresource.mipLevel,
                                         region.dstSubresource.baseArrayLayer,
                                         (std::max)(1U, region.dstSubresource.layerCount)};
            image_copy.dstOffset = {region.dstOffsets[0].x,
                                    region.dstOffsets[0].y,
                                    region.dstOffsets[0].z};
            image_copy.extent = {static_cast<uint32_t>(width),
                                 static_cast<uint32_t>(height),
                                 static_cast<uint32_t>((std::max)(1, depth))};
            image_copy_regions.push_back(image_copy);
        }

        if (image_copy_regions.empty())
        {
            return;
        }

        vkCmdCopyImage(((VulkanCommandBuffer*)commandBuffer)->getResource(),
            ((VulkanImage*)srcImage)->getResource(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            ((VulkanImage*)dstImage)->getResource(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(image_copy_regions.size()),
            image_copy_regions.data());
    }

    void VulkanRHI::cmdCopyBuffer(RHICommandBuffer* commandBuffer, RHIBuffer* srcBuffer, RHIBuffer* dstBuffer, uint32_t regionCount, RHIBufferCopy* pRegions)
    {
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = pRegions->srcOffset;
        copyRegion.dstOffset = pRegions->dstOffset;
        copyRegion.size = pRegions->size;

        vkCmdCopyBuffer(((VulkanCommandBuffer*)commandBuffer)->getResource(),
            ((VulkanBuffer*)srcBuffer)->getResource(),
            ((VulkanBuffer*)dstBuffer)->getResource(),
            regionCount,
            &copyRegion);
    }

    void VulkanRHI::createCommandBuffers()
    {
        VkCommandBufferAllocateInfo command_buffer_allocate_info {};
        command_buffer_allocate_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_buffer_allocate_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_buffer_allocate_info.commandBufferCount = 1U;

        for (uint32_t i = 0; i < k_max_frames_in_flight; ++i)
        {
            command_buffer_allocate_info.commandPool = m_command_pools[i];
            VkCommandBuffer vk_command_buffer;
            if (vkAllocateCommandBuffers(m_device, &command_buffer_allocate_info, &vk_command_buffer) != VK_SUCCESS)
            {
                LOG_ERROR("vk allocate command buffers");
            }
            m_vk_command_buffers[i] = vk_command_buffer;
            m_command_buffers[i] = new VulkanCommandBuffer();
            ((VulkanCommandBuffer*)m_command_buffers[i])->setResource(vk_command_buffer);

            char command_buffer_name[64];
            std::snprintf(command_buffer_name, sizeof(command_buffer_name), "graphics.frame[%u]", i);
            setDebugObjectName(m_command_buffers[i], command_buffer_name);
        }
    }

    void VulkanRHI::createDescriptorPool()
    {
        // Since DescriptorSet should be treated as asset in Vulkan, DescriptorPool
        // should be big enough, and thus we can sub-allocate DescriptorSet from
        // DescriptorPool merely as we sub-allocate Buffer/Image from DeviceMemory.

        std::vector<VkDescriptorPoolSize> pool_sizes(7);
        pool_sizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        pool_sizes[0].descriptorCount = 3 + 2 + 2 + 2 + 1 + 1 + 3 + 3;
        pool_sizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[1].descriptorCount = 1 + 1 + 1 * m_max_vertex_blending_mesh_count;
        pool_sizes[2].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[2].descriptorCount = 1 * m_max_material_count;
        pool_sizes[3].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[3].descriptorCount = 3 + 5 * m_max_material_count + 1 + 1; // ImGui_ImplVulkan_CreateDeviceObjects
        pool_sizes[4].type            = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        pool_sizes[4].descriptorCount = 4 + 1 + 1 + 2;
        pool_sizes[5].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        pool_sizes[5].descriptorCount = 3;
        pool_sizes[6].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        pool_sizes[6].descriptorCount = 1;

        uint32_t max_sets =
            1 + 1 + 1 + m_max_material_count + m_max_vertex_blending_mesh_count + 1 + 1; // +skybox + axis descriptor set

        {
            const uint32_t frames            = k_max_frames_in_flight;
            const uint32_t max_blending_meshes = m_max_vertex_blending_mesh_count;
            const uint32_t max_instances     = max_blending_meshes;
            const uint32_t gpu_skinning_sets = max_blending_meshes + frames + max_instances * frames;

            max_sets += gpu_skinning_sets;
            pool_sizes[1].descriptorCount +=
                max_blending_meshes * 4 + frames * 2 + max_instances * frames * 1;
            pool_sizes[2].descriptorCount += frames * 1;
        }

        // The path tracing descriptor set (allocated from this pool) needs an acceleration structure,
        // storage images, a large sampled-image array, samplers and storage buffers. Reserve capacity
        // for it when ray tracing is available so the set can be allocated.
        if (m_ray_tracing_enabled)
        {
            pool_sizes.push_back({VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 4});
            pool_sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4});
            pool_sizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2});
            pool_sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16});
            pool_sizes.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4});
            pool_sizes.push_back({VK_DESCRIPTOR_TYPE_SAMPLER, 4});
            // binding 11 is a 1024-element sampled-image array.
            pool_sizes.push_back({VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024 + 8});
            max_sets += 1;
        }

        VkDescriptorPoolCreateInfo pool_info {};
        pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes    = pool_sizes.data();
        pool_info.maxSets       = max_sets;
        pool_info.flags = 0U;

        if (vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_vk_descriptor_pool) != VK_SUCCESS)
        {
            LOG_ERROR("create descriptor pool");
        }

        m_descriptor_pool = new VulkanDescriptorPool();
        ((VulkanDescriptorPool*)m_descriptor_pool)->setResource(m_vk_descriptor_pool);
    }

    // semaphore : signal an image is ready for rendering // ready for presentation
    // (m_vulkan_context._swapchain_images --> semaphores, fences)
    void VulkanRHI::createSyncPrimitives()
    {
        VkSemaphoreCreateInfo semaphore_create_info {};
        semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence_create_info {};
        fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT; // the fence is initialized as signaled

        VkFenceCreateInfo copy_fence_create_info {};
        copy_fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        copy_fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < k_max_frames_in_flight; i++)
        {
            m_copy_ready_semaphores[i] = new VulkanSemaphore();
            m_copy_done_semaphores[i]  = new VulkanSemaphore();
            if (vkCreateSemaphore(
                    m_device, &semaphore_create_info, nullptr, &m_image_available_for_render_semaphores[i]) !=
                    VK_SUCCESS ||
                vkCreateSemaphore(
                    m_device, &semaphore_create_info, nullptr, &m_image_finished_for_presentation_semaphores[i]) !=
                    VK_SUCCESS ||
                vkCreateSemaphore(m_device,
                                  &semaphore_create_info,
                                  nullptr,
                                  &(((VulkanSemaphore*)m_copy_ready_semaphores[i])->getResource())) != VK_SUCCESS ||
                vkCreateSemaphore(m_device,
                                  &semaphore_create_info,
                                  nullptr,
                                  &(((VulkanSemaphore*)m_copy_done_semaphores[i])->getResource())) != VK_SUCCESS ||
                vkCreateFence(m_device, &fence_create_info, nullptr, &m_is_frame_in_flight_fences[i]) != VK_SUCCESS)
            {
                LOG_ERROR("vk create semaphore & fence");
            }

            if (vkCreateFence(m_device, &copy_fence_create_info, nullptr, &m_copy_fences[i]) != VK_SUCCESS)
            {
                LOG_ERROR("vk create particle copy fence");
            }

            m_rhi_is_frame_in_flight_fences[i] = new VulkanFence();
            ((VulkanFence*)m_rhi_is_frame_in_flight_fences[i])->setResource(m_is_frame_in_flight_fences[i]);
            m_rhi_copy_fences[i] = new VulkanFence();
            ((VulkanFence*)m_rhi_copy_fences[i])->setResource(m_copy_fences[i]);
        }
    }

    void VulkanRHI::createFramebufferImageAndView()
    {
        VulkanUtil::createImage(m_physical_device,
                                m_device,
                                m_swapchain_extent.width,
                                m_swapchain_extent.height,
                                (VkFormat)m_depth_image_format,
                                VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                ((VulkanImage*)m_depth_image)->getResource(),
                                m_depth_image_memory,
                                0,
                                1,
                                1);

        ((VulkanImageView*)m_depth_image_view)->setResource(
            VulkanUtil::createImageView(m_device, ((VulkanImage*)m_depth_image)->getResource(), (VkFormat)m_depth_image_format, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_VIEW_TYPE_2D, 1, 1));
    }

    RHISampler* VulkanRHI::getOrCreateDefaultSampler(RHIDefaultSamplerType type)
    {
        switch (type)
        {
        case Piccolo::Default_Sampler_Linear:
            if (m_linear_sampler == nullptr)
            {
                m_linear_sampler = new VulkanSampler();
                ((VulkanSampler*)m_linear_sampler)->setResource(VulkanUtil::getOrCreateLinearSampler(m_physical_device, m_device));
            }
            return m_linear_sampler;
            break;

        case Piccolo::Default_Sampler_Nearest:
            if (m_nearest_sampler == nullptr)
            {
                m_nearest_sampler = new VulkanSampler();
                ((VulkanSampler*)m_nearest_sampler)->setResource(VulkanUtil::getOrCreateNearestSampler(m_physical_device, m_device));
            }
            return m_nearest_sampler;
            break;

        default:
            return nullptr;
            break;
        }
    }

    RHISampler* VulkanRHI::getOrCreateMipmapSampler(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            LOG_ERROR("width == 0 || height == 0");
            return nullptr;
        }
        RHISampler* sampler;
        uint32_t  mip_levels = floor(log2(std::max(width, height))) + 1;
        auto      find_sampler = m_mipmap_sampler_map.find(mip_levels);
        if (find_sampler != m_mipmap_sampler_map.end())
        {
            return find_sampler->second;
        }
        else
        {
            sampler = new VulkanSampler();

            VkSampler vk_sampler = VulkanUtil::getOrCreateMipmapSampler(m_physical_device, m_device, width, height);

            ((VulkanSampler*)sampler)->setResource(vk_sampler);

            m_mipmap_sampler_map.insert(std::make_pair(mip_levels, sampler));

            return sampler;
        }
    }

    RHIShader* VulkanRHI::createShaderModule(const std::vector<unsigned char>& shader_code)
    {
        RHIShader* shahder = new VulkanShader();

        VkShaderModule vk_shader =  VulkanUtil::createShaderModule(m_device, shader_code);

        ((VulkanShader*)shahder)->setResource(vk_shader);

        return shahder;
    }

    void VulkanRHI::createBuffer(RHIDeviceSize size, RHIBufferUsageFlags usage, RHIMemoryPropertyFlags properties, RHIBuffer* & buffer, RHIDeviceMemory* & buffer_memory)
    {
        VkBuffer vk_buffer;
        VkDeviceMemory vk_device_memory;
        
        VulkanUtil::createBuffer(m_physical_device, m_device, size, usage, properties, vk_buffer, vk_device_memory);

        buffer = new VulkanBuffer();
        buffer_memory = new VulkanDeviceMemory();
        ((VulkanBuffer*)buffer)->setResource(vk_buffer);
        ((VulkanDeviceMemory*)buffer_memory)->setResource(vk_device_memory);
    }

    void VulkanRHI::createBufferAndInitialize(RHIBufferUsageFlags usage, RHIMemoryPropertyFlags properties, RHIBuffer*& buffer, RHIDeviceMemory*& buffer_memory, RHIDeviceSize size, void* data, int datasize)
    {
        VkBuffer vk_buffer;
        VkDeviceMemory vk_device_memory;

        VulkanUtil::createBufferAndInitialize(m_device, m_physical_device, usage, properties, &vk_buffer, &vk_device_memory, size, data, datasize);

        buffer = new VulkanBuffer();
        buffer_memory = new VulkanDeviceMemory();
        ((VulkanBuffer*)buffer)->setResource(vk_buffer);
        ((VulkanDeviceMemory*)buffer_memory)->setResource(vk_device_memory);
    }

    bool VulkanRHI::createBufferWithAllocation(const RHIBufferCreateInfo* pBufferCreateInfo, RHIMemoryPropertyFlags memoryPropertyFlags, RHIBuffer* & pBuffer, RHIAllocation*& pAllocation)
    {
        VkBuffer vk_buffer;
        VkBufferCreateInfo buffer_create_info{};
        buffer_create_info.sType = (VkStructureType)pBufferCreateInfo->sType;
        buffer_create_info.pNext = (const void*)pBufferCreateInfo->pNext;
        buffer_create_info.flags = (VkBufferCreateFlags)pBufferCreateInfo->flags;
        buffer_create_info.size = (VkDeviceSize)pBufferCreateInfo->size;
        buffer_create_info.usage = (VkBufferUsageFlags)pBufferCreateInfo->usage;
        buffer_create_info.sharingMode = (VkSharingMode)pBufferCreateInfo->sharingMode;
        buffer_create_info.queueFamilyIndexCount = pBufferCreateInfo->queueFamilyIndexCount;
        buffer_create_info.pQueueFamilyIndices = (const uint32_t*)pBufferCreateInfo->pQueueFamilyIndices;

        pBuffer = new VulkanBuffer();
        auto* allocation = new VulkanAllocation();
        VmaAllocationCreateInfo allocation_create_info = {};
        allocation_create_info.usage = (memoryPropertyFlags & RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ?
            VMA_MEMORY_USAGE_CPU_TO_GPU :
            VMA_MEMORY_USAGE_GPU_ONLY;

        VkResult result = vmaCreateBuffer(m_assets_allocator,
            &buffer_create_info,
            &allocation_create_info,
            &vk_buffer,
            &allocation->allocation,
            nullptr);

        ((VulkanBuffer*)pBuffer)->setResource(vk_buffer);

        if (result == VK_SUCCESS)
        {
            pAllocation = allocation;
            return true;
        }
        else
        {
            delete allocation;
            pAllocation = nullptr;
            return false;
        }
    }

    bool VulkanRHI::createBufferWithAlignment(const RHIBufferCreateInfo* pBufferCreateInfo, RHIMemoryPropertyFlags memoryPropertyFlags, RHIDeviceSize minAlignment, RHIBuffer* &pBuffer, RHIAllocation*& pAllocation)
    {
        VkBuffer vk_buffer;
        VkBufferCreateInfo buffer_create_info{};
        buffer_create_info.sType = (VkStructureType)pBufferCreateInfo->sType;
        buffer_create_info.pNext = (const void*)pBufferCreateInfo->pNext;
        buffer_create_info.flags = (VkBufferCreateFlags)pBufferCreateInfo->flags;
        buffer_create_info.size = (VkDeviceSize)pBufferCreateInfo->size;
        buffer_create_info.usage = (VkBufferUsageFlags)pBufferCreateInfo->usage;
        buffer_create_info.sharingMode = (VkSharingMode)pBufferCreateInfo->sharingMode;
        buffer_create_info.queueFamilyIndexCount = pBufferCreateInfo->queueFamilyIndexCount;
        buffer_create_info.pQueueFamilyIndices = (const uint32_t*)pBufferCreateInfo->pQueueFamilyIndices;

        pBuffer = new VulkanBuffer();
        auto* allocation = new VulkanAllocation();
        VmaAllocationCreateInfo allocation_create_info = {};
        allocation_create_info.usage = (memoryPropertyFlags & RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ?
            VMA_MEMORY_USAGE_CPU_TO_GPU :
            VMA_MEMORY_USAGE_GPU_ONLY;

        VkResult result = vmaCreateBufferWithAlignment(m_assets_allocator,
            &buffer_create_info,
            &allocation_create_info,
            minAlignment,
            &vk_buffer,
            &allocation->allocation,
            nullptr);

        ((VulkanBuffer*)pBuffer)->setResource(vk_buffer);

        if (result == VK_SUCCESS)
        {
            pAllocation = allocation;
            return true;
        }
        else
        {
            LOG_ERROR("vmaCreateBufferWithAlignment failed!");
            delete allocation;
            pAllocation = nullptr;
            return false;
        }
    }


    void VulkanRHI::copyBuffer(RHIBuffer* srcBuffer, RHIBuffer* dstBuffer, RHIDeviceSize srcOffset, RHIDeviceSize dstOffset, RHIDeviceSize size)
    {
        VkBuffer vk_src_buffer = ((VulkanBuffer*)srcBuffer)->getResource();
        VkBuffer vk_dst_buffer = ((VulkanBuffer*)dstBuffer)->getResource();
        VulkanUtil::copyBuffer(this, vk_src_buffer, vk_dst_buffer, srcOffset, dstOffset, size);
    }

    void VulkanRHI::createImage(uint32_t image_width, uint32_t image_height, RHIFormat format, RHIImageTiling image_tiling, RHIImageUsageFlags image_usage_flags, RHIMemoryPropertyFlags memory_property_flags,
        RHIImage* &image, RHIDeviceMemory* &memory, RHIImageCreateFlags image_create_flags, uint32_t array_layers, uint32_t miplevels)
    {
        VkImage vk_image;
        VkDeviceMemory vk_device_memory;
        VulkanUtil::createImage(
            m_physical_device,
            m_device,
            image_width,
            image_height,
            (VkFormat)format,
            (VkImageTiling)image_tiling,
            (VkImageUsageFlags)image_usage_flags,
            (VkMemoryPropertyFlags)memory_property_flags,
            vk_image,
            vk_device_memory,
            (VkImageCreateFlags)image_create_flags,
            array_layers,
            miplevels);

        image = new VulkanImage();
        memory = new VulkanDeviceMemory();
        ((VulkanImage*)image)->setResource(vk_image);
        ((VulkanDeviceMemory*)memory)->setResource(vk_device_memory);
    }

    void VulkanRHI::createImageView(RHIImage* image, RHIFormat format, RHIImageAspectFlags image_aspect_flags, RHIImageViewType view_type, uint32_t layout_count, uint32_t miplevels,
        RHIImageView* &image_view)
    {
        image_view = new VulkanImageView();
        VkImage vk_image = ((VulkanImage*)image)->getResource();
        VkImageView vk_image_view;
        vk_image_view = VulkanUtil::createImageView(m_device, vk_image, (VkFormat)format, image_aspect_flags, (VkImageViewType)view_type, layout_count, miplevels);
        ((VulkanImageView*)image_view)->setResource(vk_image_view);
    }

    void VulkanRHI::createGlobalImage(RHIImage* &image, RHIImageView* &image_view, RHIAllocation*& image_allocation, uint32_t texture_image_width, uint32_t texture_image_height, void* texture_image_pixels, RHIFormat texture_image_format, uint32_t miplevels)
    {
        VkImage vk_image;
        VkImageView vk_image_view;
        auto* allocation = new VulkanAllocation();
        
        VulkanUtil::createGlobalImage(this, vk_image, vk_image_view, allocation->allocation, texture_image_width, texture_image_height, texture_image_pixels, texture_image_format, miplevels);
        
        image = new VulkanImage();
        image_view = new VulkanImageView();
        image_allocation = allocation;
        ((VulkanImage*)image)->setResource(vk_image);
        ((VulkanImageView*)image_view)->setResource(vk_image_view);
    }

    void VulkanRHI::createCubeMap(RHIImage* &image, RHIImageView* &image_view, RHIAllocation*& image_allocation, uint32_t texture_image_width, uint32_t texture_image_height, std::array<void*, 6> texture_image_pixels, RHIFormat texture_image_format, uint32_t miplevels)
    {
        VkImage vk_image;
        VkImageView vk_image_view;
        auto* allocation = new VulkanAllocation();

        VulkanUtil::createCubeMap(this, vk_image, vk_image_view, allocation->allocation, texture_image_width, texture_image_height, texture_image_pixels, texture_image_format, miplevels);

        image = new VulkanImage();
        image_view = new VulkanImageView();
        image_allocation = allocation;
        ((VulkanImage*)image)->setResource(vk_image);
        ((VulkanImageView*)image_view)->setResource(vk_image_view);
    }

    void VulkanRHI::createSwapchainImageViews()
    {
        m_swapchain_imageviews.resize(m_swapchain_images.size());

        // create imageview (one for each this time) for all swapchain images
        for (size_t i = 0; i < m_swapchain_images.size(); i++)
        {
            VkImageView vk_image_view;
            vk_image_view = VulkanUtil::createImageView(m_device,
                                                                   m_swapchain_images[i],
                                                                   (VkFormat)m_swapchain_image_format,
                                                                   VK_IMAGE_ASPECT_COLOR_BIT,
                                                                   VK_IMAGE_VIEW_TYPE_2D,
                                                                   1,
                                                                   1);
            m_swapchain_imageviews[i] = new VulkanImageView();
            ((VulkanImageView*)m_swapchain_imageviews[i])->setResource(vk_image_view);
        }
    }

    void VulkanRHI::createAssetAllocator()
    {
        VmaVulkanFunctions vulkanFunctions    = {};
        vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr   = &vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo allocatorCreateInfo = {};
        allocatorCreateInfo.vulkanApiVersion       = m_vulkan_api_version;
        allocatorCreateInfo.physicalDevice         = m_physical_device;
        allocatorCreateInfo.device                 = m_device;
        allocatorCreateInfo.instance               = m_instance;
        allocatorCreateInfo.pVulkanFunctions       = &vulkanFunctions;
        if (m_ray_tracing_enabled)
        {
            // Required so VMA allocates buffers with VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, which ray
            // tracing (acceleration structure inputs, scratch, SBT) depends on.
            allocatorCreateInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        }

        vmaCreateAllocator(&allocatorCreateInfo, &m_assets_allocator);
    }

    // todo : more descriptorSet
    bool VulkanRHI::allocateDescriptorSets(const RHIDescriptorSetAllocateInfo* pAllocateInfo, RHIDescriptorSet* &pDescriptorSets)
    {
        //descriptor_set_layout
        int descriptor_set_layout_size = pAllocateInfo->descriptorSetCount;
        std::vector<VkDescriptorSetLayout> vk_descriptor_set_layout_list(descriptor_set_layout_size);
        for (int i = 0; i < descriptor_set_layout_size; ++i)
        {
            const auto& rhi_descriptor_set_layout_element = pAllocateInfo->pSetLayouts[i];
            auto& vk_descriptor_set_layout_element = vk_descriptor_set_layout_list[i];

            vk_descriptor_set_layout_element = ((VulkanDescriptorSetLayout*)rhi_descriptor_set_layout_element)->getResource();

            VulkanDescriptorSetLayout* test = ((VulkanDescriptorSetLayout*)rhi_descriptor_set_layout_element);

            test = nullptr;
        };

        VkDescriptorSetAllocateInfo descriptorset_allocate_info{};
        descriptorset_allocate_info.sType = (VkStructureType)pAllocateInfo->sType;
        descriptorset_allocate_info.pNext = (const void*)pAllocateInfo->pNext;
        descriptorset_allocate_info.descriptorPool = ((VulkanDescriptorPool*)(pAllocateInfo->descriptorPool))->getResource();
        descriptorset_allocate_info.descriptorSetCount = pAllocateInfo->descriptorSetCount;
        descriptorset_allocate_info.pSetLayouts = vk_descriptor_set_layout_list.data();

        VkDescriptorSet vk_descriptor_set;
        pDescriptorSets = new VulkanDescriptorSet;
        VkResult result = vkAllocateDescriptorSets(m_device, &descriptorset_allocate_info, &vk_descriptor_set);
        ((VulkanDescriptorSet*)pDescriptorSets)->setResource(vk_descriptor_set);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("vkAllocateDescriptorSets failed!");
            return false;
        }
    }

    bool VulkanRHI::allocateCommandBuffers(const RHICommandBufferAllocateInfo* pAllocateInfo, RHICommandBuffer* &pCommandBuffers)
    {
        VkCommandBufferAllocateInfo command_buffer_allocate_info{};
        command_buffer_allocate_info.sType = (VkStructureType)pAllocateInfo->sType;
        command_buffer_allocate_info.pNext = (const void*)pAllocateInfo->pNext;
        command_buffer_allocate_info.commandPool = ((VulkanCommandPool*)(pAllocateInfo->commandPool))->getResource();
        command_buffer_allocate_info.level = (VkCommandBufferLevel)pAllocateInfo->level;
        command_buffer_allocate_info.commandBufferCount = pAllocateInfo->commandBufferCount;

        VkCommandBuffer vk_command_buffer;
        auto* command_buffer = new VulkanCommandBuffer();
        VkResult result = vkAllocateCommandBuffers(m_device, &command_buffer_allocate_info, &vk_command_buffer);

        if (result == VK_SUCCESS)
        {
            command_buffer->setResource(vk_command_buffer);
            pCommandBuffers = command_buffer;
            return true;
        }
        else
        {
            delete command_buffer;
            pCommandBuffers = nullptr;
            LOG_ERROR("vkAllocateCommandBuffers failed!");
            return false;
        }
    }

    void VulkanRHI::createSwapchain()
    {
        // query all supports of this physical device
        SwapChainSupportDetails swapchain_support_details = querySwapChainSupport(m_physical_device);

        // choose the best or fitting format
        VkSurfaceFormatKHR chosen_surface_format =
            chooseSwapchainSurfaceFormatFromDetails(swapchain_support_details.formats);
        // choose the best or fitting present mode
        VkPresentModeKHR chosen_presentMode =
            chooseSwapchainPresentModeFromDetails(swapchain_support_details.presentModes);
        // choose the best or fitting extent
        VkExtent2D chosen_extent = chooseSwapchainExtentFromDetails(swapchain_support_details.capabilities);

        uint32_t image_count = swapchain_support_details.capabilities.minImageCount + 1;
        if (swapchain_support_details.capabilities.maxImageCount > 0 &&
            image_count > swapchain_support_details.capabilities.maxImageCount)
        {
            image_count = swapchain_support_details.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo {};
        createInfo.sType   = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_surface;

        createInfo.minImageCount    = image_count;
        createInfo.imageFormat      = chosen_surface_format.format;
        createInfo.imageColorSpace  = chosen_surface_format.colorSpace;
        createInfo.imageExtent      = chosen_extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t queueFamilyIndices[] = {m_queue_indices.graphics_family.value(), m_queue_indices.present_family.value()};

        if (m_queue_indices.graphics_family != m_queue_indices.present_family)
        {
            createInfo.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices   = queueFamilyIndices;
        }
        else
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform   = swapchain_support_details.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode    = chosen_presentMode;
        createInfo.clipped        = VK_TRUE;

        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS)
        {
            LOG_ERROR("vk create swapchain khr");
        }

        vkGetSwapchainImagesKHR(m_device, m_swapchain, &image_count, nullptr);
        m_swapchain_images.resize(image_count);
        vkGetSwapchainImagesKHR(m_device, m_swapchain, &image_count, m_swapchain_images.data());

        m_swapchain_image_format = (RHIFormat)chosen_surface_format.format;
        m_swapchain_extent.height = chosen_extent.height;
        m_swapchain_extent.width = chosen_extent.width;

        m_scissor = {{0, 0}, {m_swapchain_extent.width, m_swapchain_extent.height}};
    }

    void VulkanRHI::clearSwapchain()
    {
        for (auto imageview : m_swapchain_imageviews)
        {
            vkDestroyImageView(m_device, ((VulkanImageView*)imageview)->getResource(), NULL);
        }
        vkDestroySwapchainKHR(m_device, m_swapchain, NULL); // also swapchain images
    }

    void VulkanRHI::destroyDefaultSampler(RHIDefaultSamplerType type)
    {
        switch (type)
        {
        case Piccolo::Default_Sampler_Linear:
            VulkanUtil::destroyLinearSampler(m_device);
            delete(m_linear_sampler);
            break;
        case Piccolo::Default_Sampler_Nearest:
            VulkanUtil::destroyNearestSampler(m_device);
            delete(m_nearest_sampler);
            break;
        default:
            break;
        }
    }

    void VulkanRHI::destroyMipmappedSampler()
    {
        VulkanUtil::destroyMipmappedSampler(m_device);

        for (auto sampler : m_mipmap_sampler_map)
        {
            delete sampler.second;
        }
        m_mipmap_sampler_map.clear();
    }

    void VulkanRHI::destroyShaderModule(RHIShader* shaderModule)
    {
        if (shaderModule == nullptr || m_device == VK_NULL_HANDLE)
        {
            return;
        }

        VkShaderModule vk_shader_module = static_cast<VulkanShader*>(shaderModule)->getResource();
        vkDestroyShaderModule(m_device, vk_shader_module, nullptr);
        delete shaderModule;
    }

    void VulkanRHI::destroyPipeline(RHIPipeline*& pipeline)
    {
        if (pipeline != nullptr && m_device != nullptr)
        {
            VkPipeline vk_pipeline = static_cast<VulkanPipeline*>(pipeline)->getResource();
            if (vk_pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(m_device, vk_pipeline, nullptr);
            }
            delete pipeline;
        }
        pipeline = nullptr;
    }

    void VulkanRHI::destroyPipelineLayout(RHIPipelineLayout*& pipeline_layout)
    {
        if (pipeline_layout != nullptr && m_device != nullptr)
        {
            VkPipelineLayout vk_layout = static_cast<VulkanPipelineLayout*>(pipeline_layout)->getResource();
            if (vk_layout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(m_device, vk_layout, nullptr);
            }
            delete pipeline_layout;
        }
        pipeline_layout = nullptr;
    }

    void VulkanRHI::destroyRenderPass(RHIRenderPass*& render_pass)
    {
        if (render_pass != nullptr && m_device != nullptr)
        {
            VkRenderPass vk_render_pass = static_cast<VulkanRenderPass*>(render_pass)->getResource();
            if (vk_render_pass != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(m_device, vk_render_pass, nullptr);
            }
            delete render_pass;
        }
        render_pass = nullptr;
    }

    void VulkanRHI::destroyDescriptorSetLayout(RHIDescriptorSetLayout*& descriptor_set_layout)
    {
        if (descriptor_set_layout != nullptr && m_device != nullptr)
        {
            VkDescriptorSetLayout vk_layout =
                static_cast<VulkanDescriptorSetLayout*>(descriptor_set_layout)->getResource();
            if (vk_layout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(m_device, vk_layout, nullptr);
            }
            delete descriptor_set_layout;
        }
        descriptor_set_layout = nullptr;
    }

    void VulkanRHI::destroySemaphore(RHISemaphore* semaphore)
    {
        if (semaphore == nullptr || m_device == VK_NULL_HANDLE)
        {
            return;
        }

        vkDestroySemaphore(m_device, static_cast<VulkanSemaphore*>(semaphore)->getResource(), nullptr);
    }

    void VulkanRHI::destroySampler(RHISampler* sampler)
    {
        if (sampler == nullptr || m_device == VK_NULL_HANDLE)
        {
            return;
        }

        VkSampler vk_sampler = static_cast<VulkanSampler*>(sampler)->getResource();
        vkDestroySampler(m_device, vk_sampler, nullptr);
        delete sampler;
    }

    void VulkanRHI::destroyInstance(RHIInstance* instance)
    {
        if (instance == nullptr)
        {
            return;
        }

        vkDestroyInstance(static_cast<VulkanInstance*>(instance)->getResource(), nullptr);
    }

    void VulkanRHI::destroyImageView(RHIImageView* imageView)
    {
        if (imageView == nullptr || m_device == VK_NULL_HANDLE)
        {
            return;
        }

        vkDestroyImageView(m_device, static_cast<VulkanImageView*>(imageView)->getResource(), nullptr);
    }

    void VulkanRHI::destroyImage(RHIImage* image)
    {
        if (image == nullptr || m_device == VK_NULL_HANDLE)
        {
            return;
        }

        vkDestroyImage(m_device, static_cast<VulkanImage*>(image)->getResource(), nullptr);
    }

    void VulkanRHI::destroyFramebuffer(RHIFramebuffer* framebuffer)
    {
        if (framebuffer == nullptr || m_device == VK_NULL_HANDLE)
        {
            return;
        }

        vkDestroyFramebuffer(m_device, static_cast<VulkanFramebuffer*>(framebuffer)->getResource(), nullptr);
    }

    void VulkanRHI::destroyFence(RHIFence* fence)
    {
        if (fence == nullptr || m_device == VK_NULL_HANDLE)
        {
            return;
        }

        vkDestroyFence(m_device, static_cast<VulkanFence*>(fence)->getResource(), nullptr);
    }

    void VulkanRHI::destroyDevice()
    {
        vkDestroyDevice(m_device, nullptr);
    }

    void VulkanRHI::destroyCommandPool(RHICommandPool* commandPool)
    {
        if (commandPool == nullptr || m_device == VK_NULL_HANDLE)
        {
            return;
        }

        vkDestroyCommandPool(m_device, static_cast<VulkanCommandPool*>(commandPool)->getResource(), nullptr);
    }

    void VulkanRHI::destroyBuffer(RHIBuffer* &buffer)
    {
        if (buffer == nullptr)
        {
            return;
        }

        VkBuffer vk_buffer = static_cast<VulkanBuffer*>(buffer)->getResource();

        if (m_device != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_device, vk_buffer, nullptr);
        }
        RHI_DELETE_PTR(buffer);
    }

    void VulkanRHI::destroyBufferWithAllocation(RHIBuffer*& buffer, RHIAllocation*& allocation)
    {
        if (buffer == nullptr)
        {
            freeAllocation(allocation);
            return;
        }

        auto* vulkan_buffer = static_cast<VulkanBuffer*>(buffer);
        VkBuffer            vk_buffer = vulkan_buffer->getResource();

        if (allocation != nullptr && m_assets_allocator != VK_NULL_HANDLE &&
            static_cast<VulkanAllocation*>(allocation)->allocation != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(m_assets_allocator, vk_buffer, static_cast<VulkanAllocation*>(allocation)->allocation);
            delete static_cast<VulkanAllocation*>(allocation);
            allocation = nullptr;
            delete vulkan_buffer;
            buffer = nullptr;
            return;
        }

        destroyBuffer(buffer);
        freeAllocation(allocation);
    }

    void VulkanRHI::destroyImageWithAllocation(RHIImage*& image, RHIImageView*& image_view, RHIAllocation*& allocation)
    {
        if (image_view != nullptr)
        {
            destroyImageView(image_view);
            delete image_view;
            image_view = nullptr;
        }

        if (image == nullptr)
        {
            freeAllocation(allocation);
            return;
        }

        auto* vulkan_image = static_cast<VulkanImage*>(image);
        VkImage             vk_image = vulkan_image->getResource();

        if (allocation != nullptr && m_assets_allocator != VK_NULL_HANDLE &&
            static_cast<VulkanAllocation*>(allocation)->allocation != VK_NULL_HANDLE)
        {
            vmaDestroyImage(m_assets_allocator, vk_image, static_cast<VulkanAllocation*>(allocation)->allocation);
            delete static_cast<VulkanAllocation*>(allocation);
            allocation = nullptr;
            delete vulkan_image;
            image = nullptr;
            return;
        }

        destroyImage(image);
        delete vulkan_image;
        image = nullptr;
        freeAllocation(allocation);
    }

    void VulkanRHI::freeCommandBuffers(RHICommandPool* commandPool, uint32_t commandBufferCount, RHICommandBuffer* pCommandBuffers)
    {
        if (commandPool == nullptr || pCommandBuffers == nullptr || m_device == VK_NULL_HANDLE)
        {
            return;
        }

        VkCommandBuffer vk_command_buffer = static_cast<VulkanCommandBuffer*>(pCommandBuffers)->getResource();
        vkFreeCommandBuffers(m_device,
                             static_cast<VulkanCommandPool*>(commandPool)->getResource(),
                             commandBufferCount,
                             &vk_command_buffer);
    }

    void VulkanRHI::freeAllocation(RHIAllocation*& allocation)
    {
        if (allocation == nullptr)
        {
            return;
        }

        auto* vulkan_allocation = static_cast<VulkanAllocation*>(allocation);
        if (m_assets_allocator != VK_NULL_HANDLE && vulkan_allocation->allocation != nullptr)
        {
            vmaFreeMemory(m_assets_allocator, vulkan_allocation->allocation);
        }
        delete vulkan_allocation;
        allocation = nullptr;
    }

    void VulkanRHI::freeMemory(RHIDeviceMemory* &memory)
    {
        if (memory == nullptr)
        {
            return;
        }

        VkDeviceMemory vk_memory = static_cast<VulkanDeviceMemory*>(memory)->getResource();

        if (m_device != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_device, vk_memory, nullptr);
        }
        RHI_DELETE_PTR(memory);
    }

    bool VulkanRHI::mapMemory(RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size, RHIMemoryMapFlags flags, void** ppData)
    {
        VkResult result = vkMapMemory(m_device, ((VulkanDeviceMemory*)memory)->getResource(), offset, size, (VkMemoryMapFlags)flags, ppData);

        if (result == VK_SUCCESS)
        {
            return true;
        }
        else
        {
            LOG_ERROR("vkMapMemory failed!");
            return false;
        }
    }

    void VulkanRHI::unmapMemory(RHIDeviceMemory* memory)
    {
        vkUnmapMemory(m_device, ((VulkanDeviceMemory*)memory)->getResource());
    }

    void VulkanRHI::invalidateMappedMemoryRanges(void* pNext, RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size)
    {
        VkMappedMemoryRange mappedRange{};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = ((VulkanDeviceMemory*)memory)->getResource();
        mappedRange.offset = offset;
        mappedRange.size = size;
        vkInvalidateMappedMemoryRanges(m_device, 1, &mappedRange);
    }

    void VulkanRHI::flushMappedMemoryRanges(void* pNext, RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size)
    {
        VkMappedMemoryRange mappedRange{};
        mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mappedRange.memory = ((VulkanDeviceMemory*)memory)->getResource();
        mappedRange.offset = offset;
        mappedRange.size = size;
        vkFlushMappedMemoryRanges(m_device, 1, &mappedRange);
    }

    void VulkanRHI::recreateSwapchain()
    {
        int width  = 0;
        int height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        while (width == 0 || height == 0) // minimized 0,0, pause for now
        {
            glfwGetFramebufferSize(m_window, &width, &height);
            glfwWaitEvents();
        }

        VkResult res_wait_for_fences =
            _vkWaitForFences(m_device, k_max_frames_in_flight, m_is_frame_in_flight_fences, VK_TRUE, UINT64_MAX);
        if (VK_SUCCESS != res_wait_for_fences)
        {
            LOG_ERROR("_vkWaitForFences failed");
            return;
        }

        destroyImageView(m_depth_image_view);
        vkDestroyImage(m_device, ((VulkanImage*)m_depth_image)->getResource(), NULL);
        vkFreeMemory(m_device, m_depth_image_memory, NULL);

        for (auto imageview : m_swapchain_imageviews)
        {
            vkDestroyImageView(m_device, ((VulkanImageView*)imageview)->getResource(), NULL);
        }
        vkDestroySwapchainKHR(m_device, m_swapchain, NULL);

        createSwapchain();
        createSwapchainImageViews();
        createFramebufferImageAndView();
    }

    VkResult VulkanRHI::createDebugUtilsMessengerEXT(VkInstance                                instance,
                                                     const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                                     const VkAllocationCallbacks*              pAllocator,
                                                     VkDebugUtilsMessengerEXT*                 pDebugMessenger)
    {
        auto func =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr)
        {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        }
        else
        {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    void VulkanRHI::destroyDebugUtilsMessengerEXT(VkInstance                   instance,
                                                  VkDebugUtilsMessengerEXT     debugMessenger,
                                                  const VkAllocationCallbacks* pAllocator)
    {
        auto func =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr)
        {
            func(instance, debugMessenger, pAllocator);
        }
    }

    Piccolo::QueueFamilyIndices VulkanRHI::findQueueFamilies(VkPhysicalDevice physicalm_device) // for device and surface
    {
        QueueFamilyIndices indices;
        uint32_t           queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalm_device, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalm_device, &queue_family_count, queue_families.data());

        int i = 0;
        for (const auto& queue_family : queue_families)
        {
            if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) // if support graphics command queue
            {
                indices.graphics_family = i;
            }

            if (queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT) // if support compute command queue
            {
                indices.m_compute_family = i;
            }


            VkBool32 is_present_support = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalm_device,
                                                 i,
                                                 m_surface,
                                                 &is_present_support); // if support surface presentation
            if (is_present_support)
            {
                indices.present_family = i;
            }

            if (indices.isComplete())
            {
                break;
            }
            i++;
        }
        return indices;
    }

    bool VulkanRHI::checkDeviceExtensionSupport(VkPhysicalDevice physicalm_device)
    {
        uint32_t extension_count;
        vkEnumerateDeviceExtensionProperties(physicalm_device, nullptr, &extension_count, nullptr);

        std::vector<VkExtensionProperties> available_extensions(extension_count);
        vkEnumerateDeviceExtensionProperties(physicalm_device, nullptr, &extension_count, available_extensions.data());

        std::set<std::string> required_extensions(m_device_extensions.begin(), m_device_extensions.end());
        for (const auto& extension : available_extensions)
        {
            required_extensions.erase(extension.extensionName);
        }

        return required_extensions.empty();
    }

    bool VulkanRHI::isDeviceSuitable(VkPhysicalDevice physicalm_device)
    {
        auto queue_indices           = findQueueFamilies(physicalm_device);
        bool is_extensions_supported = checkDeviceExtensionSupport(physicalm_device);
        bool is_swapchain_adequate   = false;
        if (is_extensions_supported)
        {
            SwapChainSupportDetails swapchain_support_details = querySwapChainSupport(physicalm_device);
            is_swapchain_adequate =
                !swapchain_support_details.formats.empty() && !swapchain_support_details.presentModes.empty();
        }

        VkPhysicalDeviceFeatures physicalm_device_features;
        vkGetPhysicalDeviceFeatures(physicalm_device, &physicalm_device_features);

        if (!queue_indices.isComplete() || !is_swapchain_adequate || !physicalm_device_features.samplerAnisotropy)
        {
            return false;
        }

        return true;
    }

    Piccolo::SwapChainSupportDetails VulkanRHI::querySwapChainSupport(VkPhysicalDevice physicalm_device)
    {
        SwapChainSupportDetails details_result;

        // capabilities
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalm_device, m_surface, &details_result.capabilities);

        // formats
        uint32_t format_count;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalm_device, m_surface, &format_count, nullptr);
        if (format_count != 0)
        {
            details_result.formats.resize(format_count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(
                physicalm_device, m_surface, &format_count, details_result.formats.data());
        }

        // present modes
        uint32_t presentmode_count;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalm_device, m_surface, &presentmode_count, nullptr);
        if (presentmode_count != 0)
        {
            details_result.presentModes.resize(presentmode_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(
                physicalm_device, m_surface, &presentmode_count, details_result.presentModes.data());
        }

        return details_result;
    }

    VkFormat VulkanRHI::findDepthFormat()
    {
        return findSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                                   VK_IMAGE_TILING_OPTIMAL,
                                   VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }

    VkFormat VulkanRHI::findSupportedFormat(const std::vector<VkFormat>& candidates,
                                            VkImageTiling                tiling,
                                            VkFormatFeatureFlags         features)
    {
        for (VkFormat format : candidates)
        {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(m_physical_device, format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
            {
                return format;
            }
            else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
            {
                return format;
            }
        }

        LOG_ERROR("findSupportedFormat failed");
        return VkFormat();
    }

    VkSurfaceFormatKHR
    VulkanRHI::chooseSwapchainSurfaceFormatFromDetails(const std::vector<VkSurfaceFormatKHR>& available_surface_formats)
    {
        for (const auto& surface_format : available_surface_formats)
        {
            // TODO: select the VK_FORMAT_B8G8R8A8_SRGB surface format,
            // there is no need to do gamma correction in the fragment shader
            if (surface_format.format == VK_FORMAT_B8G8R8A8_UNORM &&
                surface_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return surface_format;
            }
        }
        return available_surface_formats[0];
    }

    VkPresentModeKHR
    VulkanRHI::chooseSwapchainPresentModeFromDetails(const std::vector<VkPresentModeKHR>& available_present_modes)
    {
        for (VkPresentModeKHR present_mode : available_present_modes)
        {
            if (VK_PRESENT_MODE_MAILBOX_KHR == present_mode)
            {
                return VK_PRESENT_MODE_MAILBOX_KHR;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D VulkanRHI::chooseSwapchainExtentFromDetails(const VkSurfaceCapabilitiesKHR& capabilities)
    {
        if (capabilities.currentExtent.width != UINT32_MAX)
        {
            return capabilities.currentExtent;
        }
        else
        {
            int width, height;
            glfwGetFramebufferSize(m_window, &width, &height);

            VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

            actualExtent.width =
                std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height =
                std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    void VulkanRHI::pushEvent(RHICommandBuffer* commond_buffer, const char* name, const float* color)
    {
        if (m_enable_debug_utils_label)
        {
            VkDebugUtilsLabelEXT label_info;
            label_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            label_info.pNext = nullptr;
            label_info.pLabelName = name;
            for (int i = 0; i < 4; ++i)
                label_info.color[i] = color[i];
            _vkCmdBeginDebugUtilsLabelEXT(((VulkanCommandBuffer*)commond_buffer)->getResource(), &label_info);
        }
    }

    void VulkanRHI::popEvent(RHICommandBuffer* commond_buffer)
    {
        if (m_enable_debug_utils_label)
        {
            _vkCmdEndDebugUtilsLabelEXT(((VulkanCommandBuffer*)commond_buffer)->getResource());
        }
    }
    bool VulkanRHI::isPointLightShadowEnabled(){ return m_enable_point_light_shadow; }

    RHICommandBuffer* VulkanRHI::getCurrentCommandBuffer() const
    {
        return m_current_command_buffer;
    }
    RHICommandBuffer* const* VulkanRHI::getCommandBufferList() const
    {
        return m_command_buffers;
    }
    RHICommandPool* VulkanRHI::getCommandPoor() const
    {
        return m_rhi_command_pool;
    }
    RHIDescriptorPool* VulkanRHI::getDescriptorPoor() const
    {
        return m_descriptor_pool;
    }
    RHIFence* const* VulkanRHI::getFenceList() const
    {
        return m_rhi_is_frame_in_flight_fences;
    }
    RHIFence* const* VulkanRHI::getCopyFenceList() const
    {
        return m_rhi_copy_fences;
    }
    RHISemaphore*& VulkanRHI::getCopyReadySemaphore(uint32_t index)
    {
        return m_copy_ready_semaphores[index % k_max_frames_in_flight];
    }
    RHISemaphore*& VulkanRHI::getCopyDoneSemaphore(uint32_t index)
    {
        return m_copy_done_semaphores[index % k_max_frames_in_flight];
    }
    QueueFamilyIndices VulkanRHI::getQueueFamilyIndices() const
    {
        return m_queue_indices;
    }
    RHIQueue* VulkanRHI::getGraphicsQueue() const
    {
        return m_graphics_queue;
    }
    RHIQueue* VulkanRHI::getComputeQueue() const
    {
        return m_compute_queue;
    }
    RHISwapChainDesc VulkanRHI::getSwapchainInfo()
    {
        RHISwapChainDesc desc;
        desc.image_format = m_swapchain_image_format;
        desc.extent = m_swapchain_extent;
        desc.viewport = &m_viewport;
        desc.scissor = &m_scissor;
        desc.imageViews = m_swapchain_imageviews;
        return desc;
    }
    RHIDepthImageDesc VulkanRHI::getDepthImageInfo() const
    {
        RHIDepthImageDesc desc;
        desc.depth_image_format = m_depth_image_format;
        desc.depth_image_view = m_depth_image_view;
        desc.depth_image = m_depth_image;
        return desc;
    }
    void VulkanRHI::setViewport(float x, float y, float width, float height, float min_depth, float max_depth)
    {
        m_viewport = {x, y, width, height, min_depth, max_depth};
    }
    RHIViewport VulkanRHI::getViewport() const
    {
        return m_viewport;
    }
    uint8_t VulkanRHI::getMaxFramesInFlight() const
    {
        return k_max_frames_in_flight;
    }
    uint8_t VulkanRHI::getCurrentFrameIndex() const
    {
        return m_current_frame_index;
    }
    uint32_t VulkanRHI::getCurrentSwapchainImageIndex() const
    {
        return m_current_swapchain_image_index;
    }
    void VulkanRHI::setCurrentFrameIndex(uint8_t index)
    {
        m_current_frame_index = index;
    }

} // namespace Piccolo
