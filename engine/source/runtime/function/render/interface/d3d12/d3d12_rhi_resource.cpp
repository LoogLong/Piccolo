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

bool D3D12RHI::isPointLightShadowEnabled()
{
    return true;
}
bool D3D12RHI::allocateCommandBuffers(const RHICommandBufferAllocateInfo* pAllocateInfo, RHICommandBuffer* &pCommandBuffers)
{
    if (pAllocateInfo != nullptr && pAllocateInfo->commandBufferCount != 1)
    {
        return false;
    }

    auto* command_buffer = new D3D12RHICommandBuffer();
#ifdef _WIN32
    if (m_d3d12_device != nullptr && !ensureCommandBufferObjects(command_buffer))
    {
        delete command_buffer;
        pCommandBuffers = nullptr;
        return false;
    }
#endif
    pCommandBuffers = command_buffer;
    return true;
}
bool D3D12RHI::allocateDescriptorSets(const RHIDescriptorSetAllocateInfo* pAllocateInfo, RHIDescriptorSet* &pDescriptorSets)
{
    if (pAllocateInfo == nullptr || pAllocateInfo->descriptorSetCount != 1 || pAllocateInfo->pSetLayouts == nullptr ||
        pAllocateInfo->descriptorPool == nullptr)
    {
        return false;
    }

    auto* pool = static_cast<D3D12RHIDescriptorPool*>(pAllocateInfo->descriptorPool);
    auto* descriptor_set = new D3D12RHIDescriptorSet();
    descriptor_set->layout = static_cast<D3D12RHIDescriptorSetLayout*>(const_cast<RHIDescriptorSetLayout*>(pAllocateInfo->pSetLayouts[0]));
    if (pool == nullptr || descriptor_set->layout == nullptr)
    {
        delete descriptor_set;
        return false;
    }

    if (pool->enforce_limits)
    {
        if (pool->allocated_sets >= pool->max_sets ||
            !hasDescriptorCapacity(descriptor_set->layout->cbv_srv_uav_descriptor_count,
                                   pool->allocated_cbv_srv_uav_descriptors,
                                   pool->cbv_srv_uav_descriptor_count) ||
            !hasDescriptorCapacity(descriptor_set->layout->sampler_descriptor_count,
                                   pool->allocated_sampler_descriptors,
                                   pool->sampler_descriptor_count))
        {
            delete descriptor_set;
            return false;
        }

        for (uint32_t type_index = 0; type_index < kTrackedDescriptorTypeCount; ++type_index)
        {
            if (!hasDescriptorCapacity(descriptor_set->layout->descriptor_type_counts[type_index],
                                       pool->allocated_descriptor_type_counts[type_index],
                                       pool->descriptor_type_counts[type_index]))
            {
                delete descriptor_set;
                return false;
            }
        }
    }

#ifdef _WIN32
    if ((descriptor_set->layout->cbv_srv_uav_descriptor_count > 0 &&
         (m_d3d12_cbv_srv_uav_heap == nullptr || m_d3d12_cbv_srv_uav_cpu_heap == nullptr)) ||
        (descriptor_set->layout->sampler_descriptor_count > 0 &&
         (m_d3d12_sampler_heap == nullptr || m_d3d12_sampler_cpu_heap == nullptr)))
    {
        delete descriptor_set;
        return false;
    }

    uint32_t cbv_srv_uav_next = m_d3d12_cbv_srv_uav_descriptor_next;
    uint32_t sampler_next     = m_d3d12_sampler_descriptor_next;
    if (!reserveDescriptors(descriptor_set->layout->cbv_srv_uav_descriptor_count,
                            cbv_srv_uav_next,
                            m_d3d12_cbv_srv_uav_descriptor_capacity,
                            descriptor_set->cbv_srv_uav_base) ||
        !reserveDescriptors(descriptor_set->layout->sampler_descriptor_count,
                            sampler_next,
                            m_d3d12_sampler_descriptor_capacity,
                            descriptor_set->sampler_base))
    {
        delete descriptor_set;
        return false;
    }
    m_d3d12_cbv_srv_uav_descriptor_next = cbv_srv_uav_next;
    m_d3d12_sampler_descriptor_next     = sampler_next;

    descriptor_set->has_cbv_srv_uav_descriptors = descriptor_set->layout->cbv_srv_uav_descriptor_count > 0;
    descriptor_set->has_sampler_descriptors     = descriptor_set->layout->sampler_descriptor_count > 0;
    descriptor_set->cbv_srv_uav_cpu_base = cpuDescriptor(m_d3d12_cbv_srv_uav_heap.Get(),
                                                         m_d3d12_cbv_srv_uav_descriptor_size,
                                                         descriptor_set->cbv_srv_uav_base);
    descriptor_set->cbv_srv_uav_gpu_base = gpuDescriptor(m_d3d12_cbv_srv_uav_heap.Get(),
                                                         m_d3d12_cbv_srv_uav_descriptor_size,
                                                         descriptor_set->cbv_srv_uav_base);
    descriptor_set->cbv_srv_uav_staging_cpu_base = cpuDescriptor(m_d3d12_cbv_srv_uav_cpu_heap.Get(),
                                                                 m_d3d12_cbv_srv_uav_descriptor_size,
                                                                 descriptor_set->cbv_srv_uav_base);
    descriptor_set->sampler_cpu_base = cpuDescriptor(m_d3d12_sampler_heap.Get(),
                                                     m_d3d12_sampler_descriptor_size,
                                                     descriptor_set->sampler_base);
    descriptor_set->sampler_gpu_base = gpuDescriptor(m_d3d12_sampler_heap.Get(),
                                                     m_d3d12_sampler_descriptor_size,
                                                     descriptor_set->sampler_base);
    descriptor_set->sampler_staging_cpu_base = cpuDescriptor(m_d3d12_sampler_cpu_heap.Get(),
                                                             m_d3d12_sampler_descriptor_size,
                                                             descriptor_set->sampler_base);
    m_d3d12_transient_cbv_srv_uav_descriptor_next = m_d3d12_cbv_srv_uav_descriptor_next;
#endif

    ++pool->allocated_sets;
    pool->allocated_cbv_srv_uav_descriptors += descriptor_set->layout->cbv_srv_uav_descriptor_count;
    pool->allocated_sampler_descriptors += descriptor_set->layout->sampler_descriptor_count;
    for (uint32_t type_index = 0; type_index < kTrackedDescriptorTypeCount; ++type_index)
    {
        pool->allocated_descriptor_type_counts[type_index] +=
            descriptor_set->layout->descriptor_type_counts[type_index];
    }

    pDescriptorSets = descriptor_set;
    return true;
}
void D3D12RHI::createFramebufferImageAndView()
{
    if (m_depth_desc.depth_image != nullptr)
    {
        destroyImage(m_depth_desc.depth_image);
    }
    if (m_depth_desc.depth_image_view != nullptr)
    {
        destroyImageView(m_depth_desc.depth_image_view);
    }

    m_depth_desc.depth_image_format = RHI_FORMAT_D32_SFLOAT;
    RHIDeviceMemory* depth_memory = nullptr;
    createImage(m_swapchain_desc.extent.width,
                m_swapchain_desc.extent.height,
                m_depth_desc.depth_image_format,
                RHI_IMAGE_TILING_OPTIMAL,
                RHI_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                    RHI_IMAGE_USAGE_SAMPLED_BIT |
                    RHI_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_depth_desc.depth_image,
                depth_memory,
                0,
                1,
                1);
    createImageView(m_depth_desc.depth_image,
                    m_depth_desc.depth_image_format,
                    RHI_IMAGE_ASPECT_DEPTH_BIT,
                    RHI_IMAGE_VIEW_TYPE_2D,
                    1,
                    1,
                    m_depth_desc.depth_image_view);
    freeMemory(depth_memory);
    return;
}
RHISampler* D3D12RHI::getOrCreateDefaultSampler(RHIDefaultSamplerType type)
{
    RHISampler** cached_sampler = nullptr;
    switch (type)
    {
        case Piccolo::Default_Sampler_Linear:
            cached_sampler = &m_linear_sampler;
            break;
        case Piccolo::Default_Sampler_Nearest:
            cached_sampler = &m_nearest_sampler;
            break;
        default:
            return nullptr;
    }

    if (*cached_sampler != nullptr)
    {
        return *cached_sampler;
    }

    RHISamplerCreateInfo sampler_info {};
    sampler_info.sType                   = RHI_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter               = RHI_FILTER_LINEAR;
    sampler_info.minFilter               = RHI_FILTER_LINEAR;
    sampler_info.mipmapMode              = RHI_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU            = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV            = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW            = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.mipLodBias              = 0.0f;
    sampler_info.anisotropyEnable        = RHI_FALSE;
    sampler_info.maxAnisotropy           = 1.0f;
    sampler_info.compareEnable           = RHI_FALSE;
    sampler_info.compareOp               = RHI_COMPARE_OP_ALWAYS;
    sampler_info.minLod                  = 0.0f;
    sampler_info.maxLod                  = (std::numeric_limits<float>::max)();
    sampler_info.borderColor             = RHI_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sampler_info.unnormalizedCoordinates = RHI_FALSE;

    if (type == Default_Sampler_Nearest)
    {
        sampler_info.magFilter  = RHI_FILTER_NEAREST;
        sampler_info.minFilter  = RHI_FILTER_NEAREST;
        sampler_info.mipmapMode = RHI_SAMPLER_MIPMAP_MODE_NEAREST;
    }

    createSampler(&sampler_info, *cached_sampler);
    return *cached_sampler;
}
RHISampler* D3D12RHI::getOrCreateMipmapSampler(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
    {
        LOG_ERROR("width == 0 || height == 0");
        return nullptr;
    }

    const uint32_t mip_levels = calculateMipLevels(width, height, 0);
    auto find_sampler = m_mipmap_sampler_map.find(mip_levels);
    if (find_sampler != m_mipmap_sampler_map.end())
    {
        return find_sampler->second;
    }

    RHISamplerCreateInfo sampler_info {};
    sampler_info.sType                   = RHI_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter               = RHI_FILTER_LINEAR;
    sampler_info.minFilter               = RHI_FILTER_LINEAR;
    sampler_info.mipmapMode              = RHI_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU            = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV            = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW            = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.maxAnisotropy           = 1.0f;
    sampler_info.compareOp               = RHI_COMPARE_OP_ALWAYS;
    sampler_info.minLod                  = 0.0f;
    sampler_info.maxLod                  = static_cast<float>(mip_levels - 1U);
    sampler_info.borderColor             = RHI_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sampler_info.unnormalizedCoordinates = RHI_FALSE;

    RHISampler* sampler = nullptr;
    createSampler(&sampler_info, sampler);
    if (sampler != nullptr)
    {
        m_mipmap_sampler_map.insert(std::make_pair(mip_levels, sampler));
    }
    return sampler;
}
RHIShader* D3D12RHI::createShaderModule(const std::vector<unsigned char>& shader_code)
{
    auto* shader = new D3D12RHIShader();
    shader->bytecode_storage = shader_code;
#ifdef _WIN32
    if (shader->bytecode_storage.empty())
    {
        delete shader;
        throw std::runtime_error("D3D12 shader bytecode is empty. Install dxc.exe and rebuild generated DXIL headers.");
    }
    shader->bytecode.pShaderBytecode = shader->bytecode_storage.empty() ? nullptr : shader->bytecode_storage.data();
    shader->bytecode.BytecodeLength  = shader->bytecode_storage.size();
#endif
    return shader;
}
void D3D12RHI::createBuffer(RHIDeviceSize size, RHIBufferUsageFlags usage, RHIMemoryPropertyFlags properties, RHIBuffer* &buffer, RHIDeviceMemory* &buffer_memory)
{
    buffer        = nullptr;
    buffer_memory = nullptr;

    auto* d3d_buffer = new D3D12RHIBuffer();
#ifdef _WIN32
    if (!createCommittedBuffer(m_d3d12_device.Get(), size, usage, properties, *d3d_buffer))
    {
        delete d3d_buffer;
        throw std::runtime_error("Failed to create D3D12 buffer resource");
    }
    registerHostVisibleDefaultBuffer(*d3d_buffer);
#else
    d3d_buffer->size  = size;
    d3d_buffer->usage = usage;
    d3d_buffer->host_data.resize(static_cast<size_t>(size));
    d3d_buffer->host_data_valid = true;
    (void)properties;
#endif
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
        const RHIDeviceSize copy_size = (std::min)(size, static_cast<RHIDeviceSize>(datasize));
#ifdef _WIN32
        if (d3d_buffer->resource != nullptr &&
            hasFlag(properties, RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            d3d_buffer->heap_type != D3D12_HEAP_TYPE_DEFAULT)
        {
            D3D12_RANGE read_range {0, 0};
            void* mapped_data = nullptr;
            if (SUCCEEDED(d3d_buffer->resource->Map(0, &read_range, &mapped_data)) && mapped_data != nullptr)
            {
                std::memcpy(mapped_data, data, static_cast<size_t>(copy_size));
                d3d_buffer->resource->Unmap(0, nullptr);
                d3d_buffer->host_data_valid = true;
                d3d_buffer->host_data_uploadable = false;
            }
        }
        else if (d3d_buffer->resource != nullptr)
        {
            RHIBuffer*       staging_buffer = nullptr;
            RHIDeviceMemory* staging_memory = nullptr;
            createBuffer(copy_size,
                         RHI_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         staging_buffer,
                         staging_memory);

            void* mapped_data = nullptr;
            if (mapMemory(staging_memory, 0, copy_size, 0, &mapped_data) && mapped_data != nullptr)
            {
                std::memcpy(mapped_data, data, static_cast<size_t>(copy_size));
                unmapMemory(staging_memory);
                copyBuffer(staging_buffer, buffer, 0, 0, copy_size);
            }

            destroyBuffer(staging_buffer);
            freeMemory(staging_memory);
        }
#else
        const size_t host_copy_size = (std::min)(d3d_buffer->host_data.size(), static_cast<size_t>(copy_size));
        std::memcpy(d3d_buffer->host_data.data(), data, host_copy_size);
        d3d_buffer->host_data_valid = true;
        d3d_buffer->host_data_uploadable = false;
#endif
        if (!d3d_buffer->host_data.empty())
        {
            const size_t host_copy_size = (std::min)(d3d_buffer->host_data.size(), static_cast<size_t>(copy_size));
            std::memcpy(d3d_buffer->host_data.data(), data, host_copy_size);
            d3d_buffer->host_data_valid = true;
            d3d_buffer->host_data_uploadable = false;
        }
    }
    return;
}
bool D3D12RHI::createBufferWithAllocation(const RHIBufferCreateInfo* pBufferCreateInfo, RHIMemoryPropertyFlags memoryPropertyFlags, RHIBuffer* &pBuffer, RHIAllocation*& pAllocation)
{
    const RHIBufferCreateInfo default_buffer_info {RHI_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    if (pBufferCreateInfo == nullptr)
    {
        pBufferCreateInfo = &default_buffer_info;
    }

    pBuffer     = nullptr;
    // D3D12 backs buffers with committed resources, so the buffer's memory lifetime is owned by the
    // ID3D12Resource inside D3D12RHIBuffer and is released by destroyBuffer(). There is no separate
    // allocation object to hand back (unlike the Vulkan/VMA backend), so pAllocation is intentionally
    // null here; freeAllocation() is a safe no-op for D3D12 allocations.
    pAllocation = nullptr;

    auto* d3d_buffer = new D3D12RHIBuffer();
#ifdef _WIN32
    const bool created = createCommittedBuffer(m_d3d12_device.Get(),
                                               pBufferCreateInfo->size,
                                               pBufferCreateInfo->usage,
                                               memoryPropertyFlags,
                                               *d3d_buffer);
    if (!created)
    {
        delete d3d_buffer;
        return false;
    }

    registerHostVisibleDefaultBuffer(*d3d_buffer);
    pBuffer = d3d_buffer;
    return true;
#else
    d3d_buffer->size  = pBufferCreateInfo->size;
    d3d_buffer->usage = pBufferCreateInfo->usage;
    d3d_buffer->host_data.resize(static_cast<size_t>(pBufferCreateInfo->size));
    d3d_buffer->host_data_valid = true;
    pBuffer = d3d_buffer;
    return true;
#endif
}
bool D3D12RHI::createBufferWithAlignment(const RHIBufferCreateInfo* pBufferCreateInfo, RHIMemoryPropertyFlags memoryPropertyFlags, RHIDeviceSize minAlignment, RHIBuffer* &pBuffer, RHIAllocation*& pAllocation)
{
    const RHIBufferCreateInfo default_buffer_info {RHI_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    RHIBufferCreateInfo aligned_buffer_info = pBufferCreateInfo ? *pBufferCreateInfo : default_buffer_info;
    aligned_buffer_info.size = alignUp(aligned_buffer_info.size, minAlignment);
    return createBufferWithAllocation(&aligned_buffer_info, memoryPropertyFlags, pBuffer, pAllocation);
}
void D3D12RHI::copyBuffer(RHIBuffer* srcBuffer, RHIBuffer* dstBuffer, RHIDeviceSize srcOffset, RHIDeviceSize dstOffset, RHIDeviceSize size)
{
    if (srcBuffer == nullptr || dstBuffer == nullptr || size == 0)
    {
        return;
    }

    auto* src = static_cast<D3D12RHIBuffer*>(srcBuffer);
    auto* dst = static_cast<D3D12RHIBuffer*>(dstBuffer);
    if (srcOffset > src->size || dstOffset > dst->size || size > src->size - srcOffset || size > dst->size - dstOffset)
    {
        LOG_ERROR("D3D12 copyBuffer skipped invalid copy region");
        return;
    }

#ifdef _WIN32
    if (src->resource == nullptr || dst->resource == nullptr)
    {
        LOG_ERROR("D3D12 copyBuffer requires GPU resources");
        return;
    }
    if (dst->heap_type == D3D12_HEAP_TYPE_UPLOAD)
    {
        LOG_ERROR("D3D12 copyBuffer cannot copy into an upload heap destination");
        return;
    }

    const D3D12_RESOURCE_STATES src_previous_state = src->current_state;
    const D3D12_RESOURCE_STATES dst_previous_state = dst->current_state;
    const bool src_host_data_valid = src->host_data_valid;
    const bool dst_host_data_valid = dst->host_data_valid;
    const bool copied = executeImmediateCommands(
        [&](ID3D12GraphicsCommandList* command_list)
        {
            if (src->heap_type == D3D12_HEAP_TYPE_DEFAULT)
            {
                transitionResource(command_list,
                                   src->resource.Get(),
                                   src->current_state,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
            }
            if (dst->heap_type == D3D12_HEAP_TYPE_DEFAULT)
            {
                transitionResource(command_list,
                                   dst->resource.Get(),
                                   dst->current_state,
                                   D3D12_RESOURCE_STATE_COPY_DEST);
                dst->host_data_valid = false;
                dst->host_data_uploadable = false;
            }

            command_list->CopyBufferRegion(dst->resource.Get(), dstOffset, src->resource.Get(), srcOffset, size);

            if (dst->heap_type == D3D12_HEAP_TYPE_DEFAULT)
            {
                transitionResource(command_list, dst->resource.Get(), dst->current_state, dst_previous_state);
            }
            if (src->heap_type == D3D12_HEAP_TYPE_DEFAULT)
            {
                transitionResource(command_list, src->resource.Get(), src->current_state, src_previous_state);
            }
        });

    if (!copied)
    {
        LOG_ERROR("D3D12 copyBuffer command execution failed");
        return;
    }
    updateBufferHostMirrorAfterCopy(*src,
                                    *dst,
                                    src_host_data_valid,
                                    dst_host_data_valid,
                                    srcOffset,
                                    dstOffset,
                                    size,
                                    "D3D12 copyBuffer");
#else
    updateBufferHostMirrorAfterCopy(*src,
                                    *dst,
                                    src->host_data_valid,
                                    dst->host_data_valid,
                                    srcOffset,
                                    dstOffset,
                                    size,
                                    "D3D12 copyBuffer");
#endif
    return;
}
void D3D12RHI::createImage(uint32_t image_width, uint32_t image_height, RHIFormat format, RHIImageTiling image_tiling, RHIImageUsageFlags image_usage_flags, RHIMemoryPropertyFlags memory_property_flags, RHIImage* &image, RHIDeviceMemory* &memory, RHIImageCreateFlags image_create_flags, uint32_t array_layers, uint32_t miplevels)
{
    image  = nullptr;
    memory = nullptr;

    auto* d3d_image = new D3D12RHIImage();
    d3d_image->width                    = image_width;
    d3d_image->height                   = image_height;
    d3d_image->array_layers             = (std::max)(1U, array_layers);
    d3d_image->mip_levels               = calculateMipLevels(image_width, image_height, miplevels);
    d3d_image->format                   = format;
    d3d_image->usage                    = image_usage_flags;
    d3d_image->create_flags             = image_create_flags;
    d3d_image->tiling                   = image_tiling;
    d3d_image->memory_properties        = memory_property_flags;
#ifdef _WIN32
    d3d_image->dxgi_format              = toResourceDXGIFormat(format);
    initializeImageSubresourceStates(*d3d_image, initialImageState(image_usage_flags));
    d3d_image->source_bytes_per_pixel   = sourceBytesPerPixel(format);
    d3d_image->resource_bytes_per_pixel = resourceBytesPerPixel(format);

    if (m_d3d12_device == nullptr)
    {
        delete d3d_image;
        throw std::runtime_error("Failed to create D3D12 image resource: device is null");
    }

    if (image_width == 0 || image_height == 0 || d3d_image->dxgi_format == DXGI_FORMAT_UNKNOWN)
    {
        LOG_ERROR("Failed to create D3D12 image resource (width={}, height={}, format={})",
                  image_width,
                  image_height,
                  static_cast<uint32_t>(format));
        delete d3d_image;
        throw std::runtime_error("Failed to create D3D12 image resource");
    }

    {
        D3D12_HEAP_PROPERTIES heap_properties {};
        heap_properties.Type                 = D3D12_HEAP_TYPE_DEFAULT;
        heap_properties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap_properties.CreationNodeMask     = 1;
        heap_properties.VisibleNodeMask      = 1;

        D3D12_RESOURCE_DESC resource_desc {};
        resource_desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resource_desc.Alignment          = 0;
        resource_desc.Width              = image_width;
        resource_desc.Height             = image_height;
        resource_desc.DepthOrArraySize   = static_cast<UINT16>(d3d_image->array_layers);
        resource_desc.MipLevels          = static_cast<UINT16>(d3d_image->mip_levels);
        resource_desc.Format             = d3d_image->dxgi_format;
        resource_desc.SampleDesc.Count   = 1;
        resource_desc.SampleDesc.Quality = 0;
        resource_desc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resource_desc.Flags              = imageResourceFlags(image_usage_flags);

        D3D12_CLEAR_VALUE clear_value {};
        D3D12_CLEAR_VALUE* clear_value_ptr = nullptr;
        if (hasFlag(image_usage_flags, RHI_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
        {
            clear_value.Format               = toDSVFormat(format);
            clear_value.DepthStencil.Depth   = 1.0f;
            clear_value.DepthStencil.Stencil = 0;
            clear_value_ptr                  = &clear_value;
        }
        else if (hasFlag(image_usage_flags, RHI_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        {
            clear_value.Format   = d3d_image->dxgi_format;
            clear_value.Color[0] = 0.0f;
            clear_value.Color[1] = 0.0f;
            clear_value.Color[2] = 0.0f;
            clear_value.Color[3] = 0.0f;
            clear_value_ptr      = &clear_value;
        }

        const HRESULT resource_result =
            m_d3d12_device->CreateCommittedResource(&heap_properties,
                                                    D3D12_HEAP_FLAG_NONE,
                                                    &resource_desc,
                                                    d3d_image->current_state,
                                                    clear_value_ptr,
                                                    IID_PPV_ARGS(&d3d_image->resource));
        if (FAILED(resource_result))
        {
            LOG_ERROR("Failed to create D3D12 image resource (width={}, height={}, layers={}, mips={}, format={}, usage={}, HRESULT=0x{:08X})",
                      image_width,
                      image_height,
                      d3d_image->array_layers,
                      d3d_image->mip_levels,
                      static_cast<uint32_t>(format),
                      image_usage_flags,
                      static_cast<unsigned int>(resource_result));
            delete d3d_image;
            throw std::runtime_error("Failed to create D3D12 image resource");
        }
    }
#endif
    image = d3d_image;

    auto* d3d_memory = new D3D12RHIDeviceMemory();
    d3d_memory->owner_image = d3d_image;
    memory = d3d_memory;
    return;
}
void D3D12RHI::createImageView(RHIImage* image, RHIFormat format, RHIImageAspectFlags image_aspect_flags, RHIImageViewType view_type, uint32_t layout_count, uint32_t miplevels, RHIImageView* &image_view)
{
    auto* d3d_image = static_cast<D3D12RHIImage*>(image);
    auto* view      = new D3D12RHIImageView();
    view->image        = d3d_image;
    view->format       = format;
    view->aspect_flags = image_aspect_flags;
    view->view_type    = view_type;
    view->layer_count  = (std::max)(1U, layout_count);
    view->mip_levels   = (miplevels == 0 && d3d_image != nullptr) ? d3d_image->mip_levels : (std::max)(1U, miplevels);
#ifdef _WIN32
    view->dxgi_format = toDXGIFormat(format);
    const DXGI_FORMAT srv_format = toSRVFormat(format, view->dxgi_format);
    if (d3d_image != nullptr)
    {
        if (hasFlag(d3d_image->usage, RHI_IMAGE_USAGE_SAMPLED_BIT) ||
            hasFlag(d3d_image->usage, RHI_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
        {
            view->has_srv                              = true;
            view->srv_desc.Format                      = srv_format;
            view->srv_desc.Shader4ComponentMapping     = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            if (view_type == RHI_IMAGE_VIEW_TYPE_CUBE)
            {
                view->srv_desc.ViewDimension              = D3D12_SRV_DIMENSION_TEXTURECUBE;
                view->srv_desc.TextureCube.MostDetailedMip = 0;
                view->srv_desc.TextureCube.MipLevels       = view->mip_levels;
            }
            else if (view->layer_count > 1 || view_type == RHI_IMAGE_VIEW_TYPE_2D_ARRAY)
            {
                view->srv_desc.ViewDimension                  = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                view->srv_desc.Texture2DArray.MostDetailedMip = 0;
                view->srv_desc.Texture2DArray.MipLevels       = view->mip_levels;
                view->srv_desc.Texture2DArray.FirstArraySlice = 0;
                view->srv_desc.Texture2DArray.ArraySize       = view->layer_count;
            }
            else
            {
                view->srv_desc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
                view->srv_desc.Texture2D.MostDetailedMip = 0;
                view->srv_desc.Texture2D.MipLevels       = view->mip_levels;
            }
        }

        if (hasFlag(d3d_image->usage, RHI_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
            !isDepthFormat(format))
        {
            view->has_rtv                = true;
            view->descriptor_heap_type   = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            view->rtv_desc.Format        = view->dxgi_format;
            view->rtv_desc.ViewDimension = view->layer_count > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY :
                                                           D3D12_RTV_DIMENSION_TEXTURE2D;
            if (view->layer_count > 1)
            {
                view->rtv_desc.Texture2DArray.MipSlice        = 0;
                view->rtv_desc.Texture2DArray.FirstArraySlice = 0;
                view->rtv_desc.Texture2DArray.ArraySize       = view->layer_count;
            }
        }

        if (hasFlag(d3d_image->usage, RHI_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ||
            hasFlag(image_aspect_flags, RHI_IMAGE_ASPECT_DEPTH_BIT))
        {
            view->has_dsv                = true;
            view->descriptor_heap_type   = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            view->dsv_desc.Format        = toDSVFormat(format);
            view->dsv_desc.ViewDimension = view->layer_count > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DARRAY :
                                                           D3D12_DSV_DIMENSION_TEXTURE2D;
            if (view->layer_count > 1)
            {
                view->dsv_desc.Texture2DArray.MipSlice        = 0;
                view->dsv_desc.Texture2DArray.FirstArraySlice = 0;
                view->dsv_desc.Texture2DArray.ArraySize       = view->layer_count;
            }

            view->has_read_only_dsv      = true;
            view->read_only_dsv_desc     = view->dsv_desc;
            view->read_only_dsv_desc.Flags =
                static_cast<D3D12_DSV_FLAGS>(D3D12_DSV_FLAG_READ_ONLY_DEPTH |
                                             (formatHasStencil(format) ? D3D12_DSV_FLAG_READ_ONLY_STENCIL :
                                                                        D3D12_DSV_FLAG_NONE));
        }

        if (hasFlag(d3d_image->usage, RHI_IMAGE_USAGE_STORAGE_BIT))
        {
            view->has_uav                = true;
            view->uav_desc.Format        = view->dxgi_format;
            view->uav_desc.ViewDimension = view->layer_count > 1 ? D3D12_UAV_DIMENSION_TEXTURE2DARRAY :
                                                           D3D12_UAV_DIMENSION_TEXTURE2D;
            if (view->layer_count > 1)
            {
                view->uav_desc.Texture2DArray.MipSlice        = 0;
                view->uav_desc.Texture2DArray.FirstArraySlice = 0;
                view->uav_desc.Texture2DArray.ArraySize       = view->layer_count;
            }
        }
    }
#endif
    image_view = view;
    return;
}
void D3D12RHI::createGlobalImage(RHIImage* &image, RHIImageView* &image_view, RHIAllocation*& image_allocation, uint32_t texture_image_width, uint32_t texture_image_height, void* texture_image_pixels, RHIFormat texture_image_format, uint32_t miplevels)
{
    image_allocation = nullptr;
    const uint32_t image_mip_levels = texture_image_pixels != nullptr ?
                                          calculateMipLevels(texture_image_width,
                                                             texture_image_height,
                                                             miplevels) :
                                          miplevels;
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
                image_mip_levels);
#ifdef _WIN32
    (void)uploadTexture2D(image, texture_image_pixels, 1, 1);
#endif
    createImageView(image,
                    texture_image_format,
                    RHI_IMAGE_ASPECT_COLOR_BIT,
                    RHI_IMAGE_VIEW_TYPE_2D,
                    1,
                    image_mip_levels,
                    image_view);
    delete memory;
    return;
}
void D3D12RHI::createCubeMap(RHIImage* &image, RHIImageView* &image_view, RHIAllocation*& image_allocation, uint32_t texture_image_width, uint32_t texture_image_height, std::array<void*, 6> texture_image_pixels, RHIFormat texture_image_format, uint32_t miplevels)
{
    image_allocation = nullptr;
    const uint32_t image_mip_levels = calculateMipLevels(texture_image_width,
                                                         texture_image_height,
                                                         miplevels);
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
                image_mip_levels);
#ifdef _WIN32
    const uint32_t bytes_per_pixel = sourceBytesPerPixel(texture_image_format);
    if (bytes_per_pixel > 0)
    {
        const uint32_t source_mip_levels = 1;
        const size_t source_face_size = textureMipByteSize(texture_image_width,
                                                           texture_image_height,
                                                           bytes_per_pixel);
        std::vector<uint8_t> cube_pixels(source_face_size * 6, 0);
        for (uint32_t face = 0; face < 6; ++face)
        {
            if (texture_image_pixels[face] != nullptr)
            {
                std::memcpy(cube_pixels.data() + source_face_size * face,
                            texture_image_pixels[face],
                            source_face_size);
            }
        }
        (void)uploadTexture2D(image, cube_pixels.data(), 6, source_mip_levels);
    }
#endif
    createImageView(image,
                    texture_image_format,
                    RHI_IMAGE_ASPECT_COLOR_BIT,
                    RHI_IMAGE_VIEW_TYPE_CUBE,
                    6,
                    image_mip_levels,
                    image_view);
    delete memory;
    return;
}
void D3D12RHI::createCommandPool()
{
    if (!createCommandPool(nullptr, m_default_command_pool))
    {
        throw std::runtime_error("Failed to create D3D12 command pool");
    }
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
    if (pCreateInfo != nullptr && pCreateInfo->poolSizeCount > 0 && pCreateInfo->pPoolSizes == nullptr)
    {
        return false;
    }

    auto* pool = new D3D12RHIDescriptorPool();
    if (pCreateInfo != nullptr)
    {
        if (pCreateInfo->maxSets == 0)
        {
            delete pool;
            return false;
        }

        pool->enforce_limits = true;
        pool->max_sets = pCreateInfo->maxSets;
        for (uint32_t i = 0; i < pCreateInfo->poolSizeCount; ++i)
        {
            const RHIDescriptorPoolSize& pool_size = pCreateInfo->pPoolSizes[i];
            if (!isTrackedDescriptorType(pool_size.type) || !isSupportedDescriptorType(pool_size.type))
            {
                delete pool;
                return false;
            }

            const uint32_t type_index = descriptorTypeIndex(pool_size.type);
            pool->descriptor_type_counts[type_index] += pool_size.descriptorCount;
            if (descriptorUsesSamplerHeap(pool_size.type))
            {
                pool->sampler_descriptor_count += pool_size.descriptorCount;
            }
            if (descriptorUsesResourceHeap(pool_size.type))
            {
                pool->cbv_srv_uav_descriptor_count += pool_size.descriptorCount;
            }
        }
    }

#ifdef _WIN32
    const uint32_t cbv_srv_uav_required =
        pool->cbv_srv_uav_descriptor_count > 0 ? pool->cbv_srv_uav_descriptor_count :
        (!pool->enforce_limits ? 1U : 0U);
    if (m_d3d12_cbv_srv_uav_heap == nullptr)
    {
        if (cbv_srv_uav_required > 0)
        {
            const bool cbv_srv_uav_created = createDescriptorHeap(m_d3d12_device.Get(),
                                                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                                  cbv_srv_uav_required + 1,
                                                                  true,
                                                                  m_d3d12_cbv_srv_uav_heap,
                                                                  m_d3d12_cbv_srv_uav_descriptor_size,
                                                                  m_d3d12_cbv_srv_uav_descriptor_capacity,
                                                                  m_d3d12_cbv_srv_uav_descriptor_next);
            if (!cbv_srv_uav_created)
            {
                delete pool;
                return false;
            }
            if (!createCpuDescriptorHeap(m_d3d12_device.Get(),
                                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                         m_d3d12_cbv_srv_uav_descriptor_capacity,
                                         m_d3d12_cbv_srv_uav_cpu_heap))
            {
                delete pool;
                return false;
            }
            m_d3d12_cbv_srv_uav_descriptor_next = 1;
            m_d3d12_transient_cbv_srv_uav_descriptor_next = m_d3d12_cbv_srv_uav_descriptor_next;
        }
    }
    else
    {
        if (cbv_srv_uav_required > m_d3d12_cbv_srv_uav_descriptor_capacity - m_d3d12_cbv_srv_uav_descriptor_next)
        {
            delete pool;
            return false;
        }
        if (m_d3d12_cbv_srv_uav_cpu_heap == nullptr &&
            !createCpuDescriptorHeap(m_d3d12_device.Get(),
                                     D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                     m_d3d12_cbv_srv_uav_descriptor_capacity,
                                     m_d3d12_cbv_srv_uav_cpu_heap))
        {
            delete pool;
            return false;
        }
    }

    const uint32_t sampler_required =
        pool->sampler_descriptor_count > 0 ? pool->sampler_descriptor_count :
        (!pool->enforce_limits ? 1U : 0U);
    if (m_d3d12_sampler_heap == nullptr)
    {
        if (sampler_required > 0)
        {
            const bool sampler_created = createDescriptorHeap(m_d3d12_device.Get(),
                                                              D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                                              sampler_required,
                                                              true,
                                                              m_d3d12_sampler_heap,
                                                              m_d3d12_sampler_descriptor_size,
                                                              m_d3d12_sampler_descriptor_capacity,
                                                              m_d3d12_sampler_descriptor_next);
            if (!sampler_created)
            {
                delete pool;
                return false;
            }
            if (!createCpuDescriptorHeap(m_d3d12_device.Get(),
                                         D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                         m_d3d12_sampler_descriptor_capacity,
                                         m_d3d12_sampler_cpu_heap))
            {
                delete pool;
                return false;
            }
        }
    }
    else
    {
        if (sampler_required > m_d3d12_sampler_descriptor_capacity - m_d3d12_sampler_descriptor_next)
        {
            delete pool;
            return false;
        }
        if (m_d3d12_sampler_cpu_heap == nullptr &&
            !createCpuDescriptorHeap(m_d3d12_device.Get(),
                                     D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                     m_d3d12_sampler_descriptor_capacity,
                                     m_d3d12_sampler_cpu_heap))
        {
            delete pool;
            return false;
        }
    }
#endif

    delete pDescriptorPool;
    pDescriptorPool = pool;
    return true;
}
bool D3D12RHI::createDescriptorSetLayout(const RHIDescriptorSetLayoutCreateInfo* pCreateInfo, RHIDescriptorSetLayout* &pSetLayout)
{
    if (pCreateInfo == nullptr || (pCreateInfo->bindingCount > 0 && pCreateInfo->pBindings == nullptr))
    {
        return false;
    }

    auto* layout = new D3D12RHIDescriptorSetLayout();
    layout->ranges.reserve(pCreateInfo->bindingCount);
    for (uint32_t i = 0; i < pCreateInfo->bindingCount; ++i)
    {
        D3D12RHIDescriptorSetLayout::BindingRange range {};
        range.binding = pCreateInfo->pBindings[i];
        range.binding.descriptorCount = (std::max)(1U, range.binding.descriptorCount);
        if (!isTrackedDescriptorType(range.binding.descriptorType) ||
            !isSupportedDescriptorType(range.binding.descriptorType))
        {
            delete layout;
            return false;
        }

        layout->descriptor_type_counts[descriptorTypeIndex(range.binding.descriptorType)] +=
            range.binding.descriptorCount;
        if (descriptorUsesResourceHeap(range.binding.descriptorType))
        {
            range.cbv_srv_uav_offset = layout->cbv_srv_uav_descriptor_count;
            layout->cbv_srv_uav_descriptor_count += range.binding.descriptorCount;
#ifdef _WIN32
            range.cbv_srv_uav_range_type = toDescriptorRangeType(range.binding);
#endif
        }
        if (descriptorUsesSamplerHeap(range.binding.descriptorType))
        {
            range.sampler_offset = layout->sampler_descriptor_count;
            layout->sampler_descriptor_count += range.binding.descriptorCount;
        }
        layout->ranges.push_back(range);
    }

    delete pSetLayout;
    pSetLayout = layout;
    return true;
}
bool D3D12RHI::createFence(const RHIFenceCreateInfo* pCreateInfo, RHIFence* &pFence)
{
    auto* fence = new D3D12RHIFence();
#ifdef _WIN32
    if (m_d3d12_device == nullptr)
    {
        delete fence;
        pFence = nullptr;
        return false;
    }

    const bool initially_signaled =
        pCreateInfo != nullptr && (pCreateInfo->flags & RHI_FENCE_CREATE_SIGNALED_BIT) != 0;
    const uint64_t initial_value = initially_signaled ? 1ULL : 0ULL;
    if (FAILED(m_d3d12_device->CreateFence(initial_value,
                                           D3D12_FENCE_FLAG_NONE,
                                           IID_PPV_ARGS(&fence->fence))))
    {
        delete fence;
        pFence = nullptr;
        return false;
    }

    fence->event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (fence->event == nullptr)
    {
        delete fence;
        pFence = nullptr;
        return false;
    }

    fence->next_signal_value = initial_value;
    fence->wait_value        = initially_signaled ? initial_value : 1ULL;
    fence->has_pending_signal = !initially_signaled;
    fence->signaled          = initially_signaled;
#else
    (void)pCreateInfo;
#endif
    pFence = fence;
    return true;
}
bool D3D12RHI::createFramebuffer(const RHIFramebufferCreateInfo* pCreateInfo, RHIFramebuffer* &pFramebuffer)
{
    if (pCreateInfo == nullptr ||
        (pCreateInfo->attachmentCount > 0 && pCreateInfo->pAttachments == nullptr))
    {
        return false;
    }

    auto* framebuffer = new D3D12RHIFramebuffer();
    framebuffer->render_pass = static_cast<D3D12RHIRenderPass*>(pCreateInfo->renderPass);
    framebuffer->width       = pCreateInfo->width;
    framebuffer->height      = pCreateInfo->height;
    framebuffer->layers      = pCreateInfo->layers;
    framebuffer->attachments.reserve(pCreateInfo->attachmentCount);
    for (uint32_t i = 0; i < pCreateInfo->attachmentCount; ++i)
    {
        auto* view = static_cast<D3D12RHIImageView*>(pCreateInfo->pAttachments[i]);
        framebuffer->attachments.push_back(view);
#ifdef _WIN32
        if (view == nullptr || m_d3d12_device == nullptr)
        {
            continue;
        }

        if (view->has_rtv && view->cpu_descriptor.ptr == 0 && view->image != nullptr && view->image->resource != nullptr)
        {
            uint32_t descriptor_index = 0;
            if (!reserveDescriptors(1,
                                    m_d3d12_rtv_descriptor_next,
                                    m_d3d12_rtv_descriptor_capacity,
                                    descriptor_index))
            {
                delete framebuffer;
                return false;
            }
            view->descriptor_heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            view->cpu_descriptor = cpuDescriptor(m_d3d12_rtv_heap.Get(), m_d3d12_rtv_descriptor_size, descriptor_index);
            m_d3d12_device->CreateRenderTargetView(view->image->resource.Get(), &view->rtv_desc, view->cpu_descriptor);
        }

        if (view->has_dsv && view->cpu_descriptor.ptr == 0 && view->image != nullptr && view->image->resource != nullptr)
        {
            const uint32_t descriptor_count = view->has_read_only_dsv ? 2U : 1U;
            uint32_t descriptor_index = 0;
            if (!reserveDescriptors(descriptor_count,
                                    m_d3d12_dsv_descriptor_next,
                                    m_d3d12_dsv_descriptor_capacity,
                                    descriptor_index))
            {
                delete framebuffer;
                return false;
            }
            view->descriptor_heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            view->cpu_descriptor = cpuDescriptor(m_d3d12_dsv_heap.Get(), m_d3d12_dsv_descriptor_size, descriptor_index);
            m_d3d12_device->CreateDepthStencilView(view->image->resource.Get(), &view->dsv_desc, view->cpu_descriptor);
            if (view->has_read_only_dsv)
            {
                view->read_only_dsv_cpu_descriptor =
                    cpuDescriptor(m_d3d12_dsv_heap.Get(), m_d3d12_dsv_descriptor_size, descriptor_index + 1);
                m_d3d12_device->CreateDepthStencilView(view->image->resource.Get(),
                                                       &view->read_only_dsv_desc,
                                                       view->read_only_dsv_cpu_descriptor);
            }
        }
#endif
    }

    pFramebuffer = framebuffer;
    return true;
}
bool D3D12RHI::createGraphicsPipelines(RHIPipelineCache* pipelineCache, uint32_t createInfoCount, const RHIGraphicsPipelineCreateInfo* pCreateInfos, RHIPipeline* &pPipelines)
{
    (void)pipelineCache;
    if (createInfoCount == 0 || pCreateInfos == nullptr)
    {
        return false;
    }

    const RHIGraphicsPipelineCreateInfo& create_info = pCreateInfos[0];
    auto* pipeline = new D3D12RHIPipeline();
    pipeline->bind_point = RHI_PIPELINE_BIND_POINT_GRAPHICS;
    pipeline->layout = static_cast<D3D12RHIPipelineLayout*>(create_info.layout);

#ifdef _WIN32
    if (m_d3d12_device == nullptr || pipeline->layout == nullptr || pipeline->layout->root_signature == nullptr)
    {
        delete pipeline;
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc {};
    desc.pRootSignature = pipeline->layout->root_signature.Get();

    for (uint32_t i = 0; i < create_info.stageCount; ++i)
    {
        const auto& stage = create_info.pStages[i];
        auto* shader = static_cast<D3D12RHIShader*>(stage.module);
        if (shader == nullptr)
        {
            continue;
        }
        if (stage.stage == RHI_SHADER_STAGE_VERTEX_BIT)
        {
            desc.VS = shader->bytecode;
        }
        else if (stage.stage == RHI_SHADER_STAGE_FRAGMENT_BIT)
        {
            desc.PS = shader->bytecode;
        }
        else if (stage.stage == RHI_SHADER_STAGE_GEOMETRY_BIT)
        {
            desc.GS = shader->bytecode;
        }
    }

    std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements;
    std::vector<uint32_t> binding_strides;
    if (create_info.pVertexInputState != nullptr)
    {
        binding_strides.resize(create_info.pVertexInputState->vertexBindingDescriptionCount);
        for (uint32_t i = 0; i < create_info.pVertexInputState->vertexBindingDescriptionCount; ++i)
        {
            const auto& binding = create_info.pVertexInputState->pVertexBindingDescriptions[i];
            if (binding.binding >= binding_strides.size())
            {
                binding_strides.resize(binding.binding + 1, 0);
            }
            binding_strides[binding.binding] = binding.stride;
        }

        input_elements.reserve(create_info.pVertexInputState->vertexAttributeDescriptionCount);
        for (uint32_t i = 0; i < create_info.pVertexInputState->vertexAttributeDescriptionCount; ++i)
        {
            const auto& attribute = create_info.pVertexInputState->pVertexAttributeDescriptions[i];
            D3D12_INPUT_ELEMENT_DESC element {};
            element.SemanticName         = semanticNameForLocation(attribute.location);
            element.SemanticIndex        = semanticIndexForLocation(attribute.location);
            element.Format               = toVertexDXGIFormat(attribute.format);
            element.InputSlot            = attribute.binding;
            element.AlignedByteOffset    = attribute.offset;
            element.InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            element.InstanceDataStepRate = 0;
            if (create_info.pVertexInputState->pVertexBindingDescriptions != nullptr)
            {
                for (uint32_t binding_index = 0; binding_index < create_info.pVertexInputState->vertexBindingDescriptionCount; ++binding_index)
                {
                    const auto& binding = create_info.pVertexInputState->pVertexBindingDescriptions[binding_index];
                    if (binding.binding == attribute.binding && binding.inputRate == RHI_VERTEX_INPUT_RATE_INSTANCE)
                    {
                        element.InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
                        element.InstanceDataStepRate = 1;
                        break;
                    }
                }
            }
            input_elements.push_back(element);
        }
    }
    pipeline->vertex_strides = binding_strides;
    desc.InputLayout = {input_elements.data(), static_cast<UINT>(input_elements.size())};

    const RHIPrimitiveTopology topology = create_info.pInputAssemblyState != nullptr ?
        create_info.pInputAssemblyState->topology :
        RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.PrimitiveTopologyType = toD3D12PrimitiveTopologyType(topology);
    pipeline->primitive_topology = toD3D12PrimitiveTopology(topology);

    desc.RasterizerState.FillMode              = create_info.pRasterizationState != nullptr ? toD3D12FillMode(create_info.pRasterizationState->polygonMode) : D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode              = create_info.pRasterizationState != nullptr ? toD3D12CullMode(create_info.pRasterizationState->cullMode) : D3D12_CULL_MODE_BACK;
    desc.RasterizerState.FrontCounterClockwise = create_info.pRasterizationState != nullptr && create_info.pRasterizationState->frontFace == RHI_FRONT_FACE_COUNTER_CLOCKWISE;
    desc.RasterizerState.DepthBias             = create_info.pRasterizationState != nullptr && create_info.pRasterizationState->depthBiasEnable ? static_cast<INT>(create_info.pRasterizationState->depthBiasConstantFactor) : D3D12_DEFAULT_DEPTH_BIAS;
    desc.RasterizerState.DepthBiasClamp        = create_info.pRasterizationState != nullptr ? create_info.pRasterizationState->depthBiasClamp : D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc.RasterizerState.SlopeScaledDepthBias  = create_info.pRasterizationState != nullptr ? create_info.pRasterizationState->depthBiasSlopeFactor : D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc.RasterizerState.DepthClipEnable       = create_info.pRasterizationState == nullptr || !create_info.pRasterizationState->depthClampEnable;
    desc.RasterizerState.MultisampleEnable     = FALSE;
    desc.RasterizerState.AntialiasedLineEnable = FALSE;
    desc.RasterizerState.ForcedSampleCount     = 0;
    desc.RasterizerState.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    desc.DepthStencilState.DepthEnable    = create_info.pDepthStencilState != nullptr && create_info.pDepthStencilState->depthTestEnable;
    desc.DepthStencilState.DepthWriteMask = create_info.pDepthStencilState != nullptr && create_info.pDepthStencilState->depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState.DepthFunc      = create_info.pDepthStencilState != nullptr ? toD3D12ComparisonFunc(create_info.pDepthStencilState->depthCompareOp) : D3D12_COMPARISON_FUNC_LESS_EQUAL;
    desc.DepthStencilState.StencilEnable  = create_info.pDepthStencilState != nullptr && create_info.pDepthStencilState->stencilTestEnable;
    desc.DepthStencilState.StencilReadMask  = D3D12_DEFAULT_STENCIL_READ_MASK;
    desc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    if (create_info.pDepthStencilState != nullptr)
    {
        desc.DepthStencilState.FrontFace = toD3D12StencilOpDesc(create_info.pDepthStencilState->front);
        desc.DepthStencilState.BackFace  = toD3D12StencilOpDesc(create_info.pDepthStencilState->back);
    }

    desc.BlendState.AlphaToCoverageEnable  = create_info.pMultisampleState != nullptr && create_info.pMultisampleState->alphaToCoverageEnable;
    desc.BlendState.IndependentBlendEnable = TRUE;
    const uint32_t attachment_count = create_info.pColorBlendState != nullptr ?
        (std::min)(create_info.pColorBlendState->attachmentCount, static_cast<uint32_t>(D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT)) :
        1U;
    for (uint32_t i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        D3D12_RENDER_TARGET_BLEND_DESC& target = desc.BlendState.RenderTarget[i];
        target.BlendEnable           = FALSE;
        target.LogicOpEnable         = FALSE;
        target.SrcBlend              = D3D12_BLEND_ONE;
        target.DestBlend             = D3D12_BLEND_ZERO;
        target.BlendOp               = D3D12_BLEND_OP_ADD;
        target.SrcBlendAlpha         = D3D12_BLEND_ONE;
        target.DestBlendAlpha        = D3D12_BLEND_ZERO;
        target.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
        target.LogicOp               = D3D12_LOGIC_OP_NOOP;
        target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        if (create_info.pColorBlendState != nullptr && i < create_info.pColorBlendState->attachmentCount)
        {
            const auto& attachment = create_info.pColorBlendState->pAttachments[i];
            target.BlendEnable           = attachment.blendEnable;
            target.SrcBlend              = toD3D12Blend(attachment.srcColorBlendFactor);
            target.DestBlend             = toD3D12Blend(attachment.dstColorBlendFactor);
            target.BlendOp               = toD3D12BlendOp(attachment.colorBlendOp);
            target.SrcBlendAlpha         = toD3D12Blend(attachment.srcAlphaBlendFactor);
            target.DestBlendAlpha        = toD3D12Blend(attachment.dstAlphaBlendFactor);
            target.BlendOpAlpha          = toD3D12BlendOp(attachment.alphaBlendOp);
            target.RenderTargetWriteMask = toD3D12ColorWriteMask(attachment.colorWriteMask);
        }
    }

    desc.SampleMask = UINT_MAX;
    auto* render_pass = static_cast<D3D12RHIRenderPass*>(create_info.renderPass);
    const D3D12RHIRenderPass::SubpassInfo* subpass_info = nullptr;
    if (render_pass != nullptr && create_info.subpass < render_pass->subpasses.size())
    {
        subpass_info = &render_pass->subpasses[create_info.subpass];
    }

    if (subpass_info != nullptr && !subpass_info->color_attachment_indices.empty())
    {
        desc.NumRenderTargets = (std::min)(static_cast<uint32_t>(subpass_info->color_attachment_indices.size()),
                                           static_cast<uint32_t>(D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT));
        for (uint32_t i = 0; i < desc.NumRenderTargets; ++i)
        {
            const uint32_t attachment_index = subpass_info->color_attachment_indices[i];
            if (attachment_index < render_pass->attachments.size())
            {
                desc.RTVFormats[i] = toDXGIFormat(render_pass->attachments[attachment_index].format);
            }
        }
    }
    else
    {
        desc.NumRenderTargets = attachment_count;
        for (uint32_t i = 0; i < attachment_count; ++i)
        {
            desc.RTVFormats[i] = toDXGIFormat(m_swapchain_desc.image_format);
        }
    }

    desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    if (subpass_info != nullptr)
    {
        DXGI_FORMAT subpass_dsv_format = DXGI_FORMAT_UNKNOWN;
        if (subpass_info->depth_attachment_index < render_pass->attachments.size())
        {
            const RHIFormat depth_format = render_pass->attachments[subpass_info->depth_attachment_index].format;
            if (isDepthFormat(depth_format))
            {
                subpass_dsv_format = toDSVFormat(depth_format);
            }
        }

        if (subpass_dsv_format != DXGI_FORMAT_UNKNOWN)
        {
            desc.DSVFormat = subpass_dsv_format;
        }
        else
        {
            desc.DepthStencilState.DepthEnable    = FALSE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.DepthStencilState.StencilEnable  = FALSE;
        }
    }
    else if (desc.DepthStencilState.DepthEnable)
    {
        desc.DSVFormat = toDXGIFormat(m_depth_desc.depth_image_format);
    }
    desc.SampleDesc.Count = create_info.pMultisampleState != nullptr ? sampleCount(create_info.pMultisampleState->rasterizationSamples) : 1;
    desc.SampleDesc.Quality = 0;
    desc.NodeMask = 0;
    desc.CachedPSO = {};
    desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    const HRESULT graphics_pso_result =
        m_d3d12_device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline->pipeline_state));
    if (FAILED(graphics_pso_result))
    {
        ComPtr<ID3D12InfoQueue> info_queue;
        if (SUCCEEDED(m_d3d12_device.As(&info_queue)) && info_queue != nullptr)
        {
            const UINT64 message_count = info_queue->GetNumStoredMessages();
            const UINT64 first_message = message_count > 8 ? message_count - 8 : 0;
            for (UINT64 message_index = first_message; message_index < message_count; ++message_index)
            {
                SIZE_T message_size = 0;
                if (FAILED(info_queue->GetMessage(message_index, nullptr, &message_size)) || message_size == 0)
                {
                    continue;
                }
                std::vector<char> message_storage(message_size);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(message_storage.data());
                if (SUCCEEDED(info_queue->GetMessage(message_index, message, &message_size)) &&
                    message->pDescription != nullptr)
                {
                    LOG_ERROR("D3D12 message {}: {}", static_cast<uint64_t>(message_index), message->pDescription);
                }
            }
        }
        delete pipeline;
        return false;
    }
#endif

    delete pPipelines;
    pPipelines = pipeline;
    return true;
}
bool D3D12RHI::createComputePipelines(RHIPipelineCache* pipelineCache, uint32_t createInfoCount, const RHIComputePipelineCreateInfo* pCreateInfos, RHIPipeline* &pPipelines)
{
    (void)pipelineCache;
    if (createInfoCount == 0 || pCreateInfos == nullptr)
    {
        return false;
    }

    const RHIComputePipelineCreateInfo& create_info = pCreateInfos[0];
    auto* pipeline = new D3D12RHIPipeline();
    pipeline->bind_point = RHI_PIPELINE_BIND_POINT_COMPUTE;
    pipeline->layout = static_cast<D3D12RHIPipelineLayout*>(create_info.layout);

#ifdef _WIN32
    if (m_d3d12_device == nullptr || pipeline->layout == nullptr || pipeline->layout->root_signature == nullptr || create_info.pStages == nullptr)
    {
        delete pipeline;
        return false;
    }

    auto* shader = static_cast<D3D12RHIShader*>(create_info.pStages->module);
    if (shader == nullptr)
    {
        delete pipeline;
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc {};
    desc.pRootSignature = pipeline->layout->root_signature.Get();
    desc.CS             = shader->bytecode;
    desc.NodeMask       = 0;
    desc.CachedPSO      = {};
    desc.Flags          = D3D12_PIPELINE_STATE_FLAG_NONE;
    if (FAILED(m_d3d12_device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline->pipeline_state))))
    {
        delete pipeline;
        return false;
    }
#endif

    delete pPipelines;
    pPipelines = pipeline;
    return true;
}
bool D3D12RHI::createPipelineLayout(const RHIPipelineLayoutCreateInfo* pCreateInfo, RHIPipelineLayout* &pPipelineLayout)
{
    if (pCreateInfo == nullptr)
    {
        return false;
    }

    auto* layout = new D3D12RHIPipelineLayout();
    layout->set_layouts.reserve(pCreateInfo->setLayoutCount);
    layout->cbv_srv_uav_root_parameter_indices.resize(pCreateInfo->setLayoutCount, (std::numeric_limits<uint32_t>::max)());
    layout->sampler_root_parameter_indices.resize(pCreateInfo->setLayoutCount, (std::numeric_limits<uint32_t>::max)());
    for (uint32_t i = 0; i < pCreateInfo->setLayoutCount; ++i)
    {
        layout->set_layouts.push_back(static_cast<D3D12RHIDescriptorSetLayout*>(pCreateInfo->pSetLayouts[i]));
    }

#ifdef _WIN32
    std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> cbv_srv_uav_ranges(pCreateInfo->setLayoutCount);
    std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> sampler_ranges(pCreateInfo->setLayoutCount);
    std::vector<D3D12_ROOT_PARAMETER> root_parameters;
    root_parameters.reserve(pCreateInfo->setLayoutCount * 2);

    for (uint32_t set_index = 0; set_index < pCreateInfo->setLayoutCount; ++set_index)
    {
        D3D12RHIDescriptorSetLayout* set_layout = layout->set_layouts[set_index];
        if (set_layout == nullptr)
        {
            continue;
        }

        for (const auto& range_info : set_layout->ranges)
        {
            if (descriptorUsesResourceHeap(range_info.binding.descriptorType))
            {
                D3D12_DESCRIPTOR_RANGE range {};
                range.RangeType                         = range_info.cbv_srv_uav_range_type;
                range.NumDescriptors                    = range_info.binding.descriptorCount;
                range.BaseShaderRegister                = range_info.binding.binding;
                range.RegisterSpace                     = set_index;
                range.OffsetInDescriptorsFromTableStart = range_info.cbv_srv_uav_offset;
                cbv_srv_uav_ranges[set_index].push_back(range);
            }
            if (descriptorUsesSamplerHeap(range_info.binding.descriptorType))
            {
                D3D12_DESCRIPTOR_RANGE range {};
                range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                range.NumDescriptors                    = range_info.binding.descriptorCount;
                range.BaseShaderRegister                = range_info.binding.binding;
                range.RegisterSpace                     = set_index;
                range.OffsetInDescriptorsFromTableStart = range_info.sampler_offset;
                sampler_ranges[set_index].push_back(range);
            }
        }

        if (!cbv_srv_uav_ranges[set_index].empty())
        {
            D3D12_ROOT_PARAMETER parameter {};
            parameter.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
            parameter.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(cbv_srv_uav_ranges[set_index].size());
            parameter.DescriptorTable.pDescriptorRanges   = cbv_srv_uav_ranges[set_index].data();
            layout->cbv_srv_uav_root_parameter_indices[set_index] = static_cast<uint32_t>(root_parameters.size());
            root_parameters.push_back(parameter);
        }
        if (!sampler_ranges[set_index].empty())
        {
            D3D12_ROOT_PARAMETER parameter {};
            parameter.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
            parameter.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(sampler_ranges[set_index].size());
            parameter.DescriptorTable.pDescriptorRanges   = sampler_ranges[set_index].data();
            layout->sampler_root_parameter_indices[set_index] = static_cast<uint32_t>(root_parameters.size());
            root_parameters.push_back(parameter);
        }
    }

    D3D12_ROOT_SIGNATURE_DESC desc {};
    desc.NumParameters     = static_cast<UINT>(root_parameters.size());
    desc.pParameters       = root_parameters.empty() ? nullptr : root_parameters.data();
    desc.NumStaticSamplers = 0;
    desc.pStaticSamplers   = nullptr;
    desc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature_blob;
    ComPtr<ID3DBlob> error_blob;
    if (FAILED(D3D12SerializeRootSignature(&desc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           &signature_blob,
                                           &error_blob)) ||
        signature_blob == nullptr ||
        FAILED(m_d3d12_device->CreateRootSignature(0,
                                                   signature_blob->GetBufferPointer(),
                                                   signature_blob->GetBufferSize(),
                                                   IID_PPV_ARGS(&layout->root_signature))))
    {
        delete layout;
        return false;
    }
#endif

    delete pPipelineLayout;
    pPipelineLayout = layout;
    return true;
}
bool D3D12RHI::createRenderPass(const RHIRenderPassCreateInfo* pCreateInfo, RHIRenderPass* &pRenderPass)
{
    if (pCreateInfo == nullptr)
    {
        return false;
    }

    auto* render_pass = new D3D12RHIRenderPass();
    if (pCreateInfo->pAttachments != nullptr && pCreateInfo->attachmentCount > 0)
    {
        render_pass->attachments.assign(pCreateInfo->pAttachments,
                                        pCreateInfo->pAttachments + pCreateInfo->attachmentCount);
    }

    if (pCreateInfo->pSubpasses != nullptr && pCreateInfo->subpassCount > 0)
    {
        render_pass->subpasses.reserve(pCreateInfo->subpassCount);
        for (uint32_t subpass_index = 0; subpass_index < pCreateInfo->subpassCount; ++subpass_index)
        {
            const RHISubpassDescription& subpass = pCreateInfo->pSubpasses[subpass_index];
            D3D12RHIRenderPass::SubpassInfo subpass_info {};
            if (subpass.pInputAttachments != nullptr)
            {
                subpass_info.input_attachment_indices.reserve(subpass.inputAttachmentCount);
                subpass_info.input_attachment_layouts.reserve(subpass.inputAttachmentCount);
                for (uint32_t i = 0; i < subpass.inputAttachmentCount; ++i)
                {
                    subpass_info.input_attachment_indices.push_back(subpass.pInputAttachments[i].attachment);
                    subpass_info.input_attachment_layouts.push_back(subpass.pInputAttachments[i].layout);
                }
            }
            if (subpass.pColorAttachments != nullptr)
            {
                subpass_info.color_attachment_indices.reserve(subpass.colorAttachmentCount);
                subpass_info.color_attachment_layouts.reserve(subpass.colorAttachmentCount);
                for (uint32_t i = 0; i < subpass.colorAttachmentCount; ++i)
                {
                    subpass_info.color_attachment_indices.push_back(subpass.pColorAttachments[i].attachment);
                    subpass_info.color_attachment_layouts.push_back(subpass.pColorAttachments[i].layout);
                }
            }
            if (subpass.pResolveAttachments != nullptr)
            {
                subpass_info.resolve_attachment_indices.reserve(subpass.colorAttachmentCount);
                subpass_info.resolve_attachment_layouts.reserve(subpass.colorAttachmentCount);
                for (uint32_t i = 0; i < subpass.colorAttachmentCount; ++i)
                {
                    subpass_info.resolve_attachment_indices.push_back(subpass.pResolveAttachments[i].attachment);
                    subpass_info.resolve_attachment_layouts.push_back(subpass.pResolveAttachments[i].layout);
                }
            }
            if (subpass.pDepthStencilAttachment != nullptr)
            {
                subpass_info.depth_attachment_index = subpass.pDepthStencilAttachment->attachment;
                subpass_info.depth_attachment_layout = subpass.pDepthStencilAttachment->layout;
            }
            if (subpass.pPreserveAttachments != nullptr && subpass.preserveAttachmentCount > 0)
            {
                subpass_info.preserve_attachment_indices.assign(subpass.pPreserveAttachments,
                                                                subpass.pPreserveAttachments + subpass.preserveAttachmentCount);
            }
            render_pass->subpasses.push_back(subpass_info);
        }

        render_pass->color_attachment_indices = render_pass->subpasses[0].color_attachment_indices;
        render_pass->depth_attachment_index   = render_pass->subpasses[0].depth_attachment_index;
    }
    else
    {
        D3D12RHIRenderPass::SubpassInfo subpass_info {};
        for (uint32_t attachment_index = 0; attachment_index < pCreateInfo->attachmentCount; ++attachment_index)
        {
            if (isDepthFormat(pCreateInfo->pAttachments[attachment_index].format))
            {
                subpass_info.depth_attachment_index = attachment_index;
            }
            else
            {
                subpass_info.color_attachment_indices.push_back(attachment_index);
                subpass_info.color_attachment_layouts.push_back(RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            }
        }
        render_pass->subpasses.push_back(subpass_info);
        render_pass->color_attachment_indices = subpass_info.color_attachment_indices;
        render_pass->depth_attachment_index   = subpass_info.depth_attachment_index;
    }

    if (pCreateInfo->pDependencies != nullptr && pCreateInfo->dependencyCount > 0)
    {
        render_pass->dependencies.assign(pCreateInfo->pDependencies,
                                         pCreateInfo->pDependencies + pCreateInfo->dependencyCount);
    }

    delete pRenderPass;
    pRenderPass = render_pass;
    return true;
}
bool D3D12RHI::createSampler(const RHISamplerCreateInfo* pCreateInfo, RHISampler* &pSampler)
{
    if (pCreateInfo == nullptr)
    {
        return false;
    }

    auto* sampler = new D3D12RHISampler();
    sampler->create_info = *pCreateInfo;
#ifdef _WIN32
    fillSamplerDesc(*pCreateInfo, sampler->desc);
#endif
    pSampler = sampler;
    return true;
}
bool D3D12RHI::createSemaphore(const RHISemaphoreCreateInfo* pCreateInfo, RHISemaphore* &pSemaphore)
{
    (void)pCreateInfo;
    auto* semaphore = new D3D12RHISemaphore();
#ifdef _WIN32
    if (m_d3d12_device == nullptr)
    {
        delete semaphore;
        pSemaphore = nullptr;
        return false;
    }

    if (FAILED(m_d3d12_device->CreateFence(0,
                                           D3D12_FENCE_FLAG_NONE,
                                           IID_PPV_ARGS(&semaphore->fence))))
    {
        delete semaphore;
        pSemaphore = nullptr;
        return false;
    }

    semaphore->event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (semaphore->event == nullptr)
    {
        delete semaphore;
        pSemaphore = nullptr;
        return false;
    }
#endif
    pSemaphore = semaphore;
    return true;
}
void D3D12RHI::updateDescriptorSets(uint32_t descriptorWriteCount, const RHIWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const RHICopyDescriptorSet* pDescriptorCopies)
{
#ifdef _WIN32
    if (m_d3d12_device == nullptr ||
        (descriptorWriteCount > 0 && pDescriptorWrites == nullptr) ||
        (descriptorCopyCount > 0 && pDescriptorCopies == nullptr))
    {
        return;
    }

    for (uint32_t write_index = 0; write_index < descriptorWriteCount; ++write_index)
    {
        const RHIWriteDescriptorSet& write = pDescriptorWrites[write_index];
        auto* descriptor_set = static_cast<D3D12RHIDescriptorSet*>(write.dstSet);
        if (descriptor_set == nullptr || descriptor_set->layout == nullptr)
        {
            continue;
        }

        const auto* binding = descriptor_set->layout->find(write.dstBinding);
        if (binding == nullptr ||
            write.descriptorCount == 0 ||
            write.descriptorType != binding->binding.descriptorType ||
            !descriptorRangeFits(write.dstArrayElement, write.descriptorCount, binding->binding.descriptorCount) ||
            !descriptorWriteHasRequiredResources(write, *binding))
        {
            continue;
        }

        bool descriptor_set_modified = false;
        for (uint32_t descriptor_index = 0; descriptor_index < write.descriptorCount; ++descriptor_index)
        {
            const uint32_t array_index = write.dstArrayElement + descriptor_index;
            if (descriptorUsesResourceHeap(write.descriptorType))
            {
                if (!descriptor_set->has_cbv_srv_uav_descriptors ||
                    m_d3d12_cbv_srv_uav_heap == nullptr ||
                    m_d3d12_cbv_srv_uav_cpu_heap == nullptr)
                {
                    continue;
                }
                const uint32_t heap_index = descriptor_set->cbv_srv_uav_base + binding->cbv_srv_uav_offset + array_index;
                D3D12_CPU_DESCRIPTOR_HANDLE staging_handle = cpuDescriptor(m_d3d12_cbv_srv_uav_cpu_heap.Get(),
                                                                           m_d3d12_cbv_srv_uav_descriptor_size,
                                                                           heap_index);
                D3D12_CPU_DESCRIPTOR_HANDLE shader_handle = cpuDescriptor(m_d3d12_cbv_srv_uav_heap.Get(),
                                                                          m_d3d12_cbv_srv_uav_descriptor_size,
                                                                          heap_index);
                if (write.descriptorType == RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                    write.descriptorType == RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                    write.descriptorType == RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
                    write.descriptorType == RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
                {
                    const RHIDescriptorBufferInfo* buffer_info = &write.pBufferInfo[descriptor_index];
                    auto* buffer = static_cast<D3D12RHIBuffer*>(buffer_info->buffer);
                    D3D12RHIDescriptorSet::BufferDescriptor descriptor {};
                    descriptor.binding         = write.dstBinding;
                    descriptor.array_element   = array_index;
                    descriptor.descriptor_type = write.descriptorType;
                    descriptor.buffer          = buffer;
                    descriptor.offset          = buffer_info->offset;
                    descriptor.range           = buffer_info->range;
                    descriptor.range_type      = binding->cbv_srv_uav_range_type;
                    upsertBufferDescriptor(*descriptor_set, descriptor);

                    const auto* descriptor_to_write = descriptor_set->findBufferDescriptor(write.dstBinding, array_index);
                    if (descriptor_to_write != nullptr)
                    {
                        writeBufferDescriptor(m_d3d12_device.Get(), staging_handle, *binding, *descriptor_to_write, 0);
                    }
                }
                else if (write.descriptorType == RHI_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
                {
                    auto* acceleration_structure = static_cast<D3D12RHIAccelerationStructure*>(
                        write.pAccelerationStructureInfo->pAccelerationStructures[descriptor_index]);

                    D3D12RHIDescriptorSet::AccelerationStructureDescriptor descriptor {};
                    descriptor.binding = write.dstBinding;
                    descriptor.array_element = array_index;
                    descriptor.descriptor_type = write.descriptorType;
                    descriptor.acceleration_structure = acceleration_structure;
                    descriptor.gpu_address = acceleration_structure->gpu_address;
                    upsertAccelerationStructureDescriptor(*descriptor_set, descriptor);

                    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc {};
                    srv_desc.Format = DXGI_FORMAT_UNKNOWN;
                    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
                    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srv_desc.RaytracingAccelerationStructure.Location = acceleration_structure->gpu_address;
                    m_d3d12_device->CreateShaderResourceView(nullptr, &srv_desc, staging_handle);
                }
                else if (write.descriptorType == RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                {
                    const RHIDescriptorImageInfo* image_info = &write.pImageInfo[descriptor_index];
                    auto* image_view = static_cast<D3D12RHIImageView*>(image_info->imageView);
                    if (image_view != nullptr && image_view->image != nullptr && image_view->image->resource != nullptr && image_view->has_uav)
                    {
                        m_d3d12_device->CreateUnorderedAccessView(image_view->image->resource.Get(), nullptr, &image_view->uav_desc, staging_handle);
                    }
                }
                else
                {
                    const RHIDescriptorImageInfo* image_info = &write.pImageInfo[descriptor_index];
                    auto* image_view = static_cast<D3D12RHIImageView*>(image_info->imageView);
                    if (image_view != nullptr && image_view->image != nullptr && image_view->image->resource != nullptr && image_view->has_srv)
                    {
                        m_d3d12_device->CreateShaderResourceView(image_view->image->resource.Get(), &image_view->srv_desc, staging_handle);
                    }
                }
                m_d3d12_device->CopyDescriptorsSimple(1,
                                                      shader_handle,
                                                      staging_handle,
                                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                descriptor_set_modified = true;
            }

            if (descriptorUsesSamplerHeap(write.descriptorType))
            {
                if (!descriptor_set->has_sampler_descriptors ||
                    m_d3d12_sampler_heap == nullptr ||
                    m_d3d12_sampler_cpu_heap == nullptr)
                {
                    continue;
                }
                const RHIDescriptorImageInfo* image_info = write.pImageInfo != nullptr ? &write.pImageInfo[descriptor_index] : nullptr;
                auto* sampler = image_info != nullptr ? static_cast<D3D12RHISampler*>(image_info->sampler) : nullptr;
                if (sampler == nullptr && binding->binding.pImmutableSamplers != nullptr)
                {
                    sampler = static_cast<D3D12RHISampler*>(binding->binding.pImmutableSamplers[array_index]);
                }
                if (sampler != nullptr)
                {
                    const uint32_t heap_index = descriptor_set->sampler_base + binding->sampler_offset + array_index;
                    D3D12_CPU_DESCRIPTOR_HANDLE staging_handle = cpuDescriptor(m_d3d12_sampler_cpu_heap.Get(),
                                                                               m_d3d12_sampler_descriptor_size,
                                                                               heap_index);
                    D3D12_CPU_DESCRIPTOR_HANDLE shader_handle = cpuDescriptor(m_d3d12_sampler_heap.Get(),
                                                                              m_d3d12_sampler_descriptor_size,
                                                                              heap_index);
                    m_d3d12_device->CreateSampler(&sampler->desc, staging_handle);
                    m_d3d12_device->CopyDescriptorsSimple(1,
                                                          shader_handle,
                                                          staging_handle,
                                                          D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
                    descriptor_set_modified = true;
                }
            }
        }

        if (descriptor_set_modified)
        {
            ++descriptor_set->version;
        }
    }

    for (uint32_t copy_index = 0; copy_index < descriptorCopyCount; ++copy_index)
    {
        const RHICopyDescriptorSet& copy = pDescriptorCopies[copy_index];
        auto* src_set = static_cast<D3D12RHIDescriptorSet*>(copy.srcSet);
        auto* dst_set = static_cast<D3D12RHIDescriptorSet*>(copy.dstSet);
        if (src_set == nullptr || dst_set == nullptr || src_set->layout == nullptr || dst_set->layout == nullptr)
        {
            continue;
        }
        const auto* src_binding = src_set->layout->find(copy.srcBinding);
        const auto* dst_binding = dst_set->layout->find(copy.dstBinding);
        if (src_binding == nullptr || dst_binding == nullptr ||
            copy.descriptorCount == 0 ||
            src_binding->binding.descriptorType != dst_binding->binding.descriptorType ||
            !descriptorRangeFits(copy.srcArrayElement, copy.descriptorCount, src_binding->binding.descriptorCount) ||
            !descriptorRangeFits(copy.dstArrayElement, copy.descriptorCount, dst_binding->binding.descriptorCount) ||
            !descriptorCopyHasRequiredSourceMetadata(copy, *src_set, *src_binding))
        {
            continue;
        }

        bool dst_set_modified = false;
        if (descriptorUsesResourceHeap(src_binding->binding.descriptorType) &&
            descriptorUsesResourceHeap(dst_binding->binding.descriptorType))
        {
            if (!src_set->has_cbv_srv_uav_descriptors || !dst_set->has_cbv_srv_uav_descriptors ||
                m_d3d12_cbv_srv_uav_heap == nullptr ||
                m_d3d12_cbv_srv_uav_cpu_heap == nullptr)
            {
                continue;
            }
            const uint32_t src_index = src_set->cbv_srv_uav_base + src_binding->cbv_srv_uav_offset + copy.srcArrayElement;
            const uint32_t dst_index = dst_set->cbv_srv_uav_base + dst_binding->cbv_srv_uav_offset + copy.dstArrayElement;
            m_d3d12_device->CopyDescriptorsSimple(copy.descriptorCount,
                                                  cpuDescriptor(m_d3d12_cbv_srv_uav_cpu_heap.Get(), m_d3d12_cbv_srv_uav_descriptor_size, dst_index),
                                                  cpuDescriptor(m_d3d12_cbv_srv_uav_cpu_heap.Get(), m_d3d12_cbv_srv_uav_descriptor_size, src_index),
                                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            m_d3d12_device->CopyDescriptorsSimple(copy.descriptorCount,
                                                  cpuDescriptor(m_d3d12_cbv_srv_uav_heap.Get(), m_d3d12_cbv_srv_uav_descriptor_size, dst_index),
                                                  cpuDescriptor(m_d3d12_cbv_srv_uav_cpu_heap.Get(), m_d3d12_cbv_srv_uav_descriptor_size, dst_index),
                                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            dst_set_modified = true;

            if (descriptorUsesBufferInfo(src_binding->binding.descriptorType) &&
                descriptorUsesBufferInfo(dst_binding->binding.descriptorType))
            {
                for (uint32_t descriptor_index = 0; descriptor_index < copy.descriptorCount; ++descriptor_index)
                {
                    const auto* src_descriptor =
                        src_set->findBufferDescriptor(copy.srcBinding, copy.srcArrayElement + descriptor_index);
                    if (src_descriptor == nullptr)
                    {
                        continue;
                    }

                    D3D12RHIDescriptorSet::BufferDescriptor dst_descriptor = *src_descriptor;
                    dst_descriptor.binding         = copy.dstBinding;
                    dst_descriptor.array_element   = copy.dstArrayElement + descriptor_index;
                    dst_descriptor.descriptor_type = dst_binding->binding.descriptorType;
                    dst_descriptor.range_type      = dst_binding->cbv_srv_uav_range_type;
                    upsertBufferDescriptor(*dst_set, dst_descriptor);
                }
            }
            else if (isAccelerationStructureDescriptor(src_binding->binding.descriptorType) &&
                     isAccelerationStructureDescriptor(dst_binding->binding.descriptorType))
            {
                for (uint32_t descriptor_index = 0; descriptor_index < copy.descriptorCount; ++descriptor_index)
                {
                    const auto* src_descriptor =
                        src_set->findAccelerationStructureDescriptor(copy.srcBinding,
                                                                     copy.srcArrayElement + descriptor_index);
                    if (src_descriptor == nullptr)
                    {
                        continue;
                    }

                    D3D12RHIDescriptorSet::AccelerationStructureDescriptor dst_descriptor = *src_descriptor;
                    dst_descriptor.binding         = copy.dstBinding;
                    dst_descriptor.array_element   = copy.dstArrayElement + descriptor_index;
                    dst_descriptor.descriptor_type = dst_binding->binding.descriptorType;
                    upsertAccelerationStructureDescriptor(*dst_set, dst_descriptor);
                }
            }
        }

        if (descriptorUsesSamplerHeap(src_binding->binding.descriptorType) &&
            descriptorUsesSamplerHeap(dst_binding->binding.descriptorType))
        {
            if (!src_set->has_sampler_descriptors || !dst_set->has_sampler_descriptors ||
                m_d3d12_sampler_heap == nullptr ||
                m_d3d12_sampler_cpu_heap == nullptr)
            {
                continue;
            }
            const uint32_t src_index = src_set->sampler_base + src_binding->sampler_offset + copy.srcArrayElement;
            const uint32_t dst_index = dst_set->sampler_base + dst_binding->sampler_offset + copy.dstArrayElement;
            m_d3d12_device->CopyDescriptorsSimple(copy.descriptorCount,
                                                  cpuDescriptor(m_d3d12_sampler_cpu_heap.Get(), m_d3d12_sampler_descriptor_size, dst_index),
                                                  cpuDescriptor(m_d3d12_sampler_cpu_heap.Get(), m_d3d12_sampler_descriptor_size, src_index),
                                                  D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
            m_d3d12_device->CopyDescriptorsSimple(copy.descriptorCount,
                                                  cpuDescriptor(m_d3d12_sampler_heap.Get(), m_d3d12_sampler_descriptor_size, dst_index),
                                                  cpuDescriptor(m_d3d12_sampler_cpu_heap.Get(), m_d3d12_sampler_descriptor_size, dst_index),
                                                  D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
            dst_set_modified = true;
        }

        if (dst_set_modified)
        {
            ++dst_set->version;
        }
    }
#else
    (void)descriptorWriteCount;
    (void)pDescriptorWrites;
    (void)descriptorCopyCount;
    (void)pDescriptorCopies;
#endif
    return;
}
void D3D12RHI::destroyDefaultSampler(RHIDefaultSamplerType type)
{
    switch (type)
    {
        case Piccolo::Default_Sampler_Linear:
            delete static_cast<D3D12RHISampler*>(m_linear_sampler);
            m_linear_sampler = nullptr;
            break;
        case Piccolo::Default_Sampler_Nearest:
            delete static_cast<D3D12RHISampler*>(m_nearest_sampler);
            m_nearest_sampler = nullptr;
            break;
        default:
            break;
    }
    return;
}
void D3D12RHI::destroyMipmappedSampler()
{
    for (auto& sampler : m_mipmap_sampler_map)
    {
        delete static_cast<D3D12RHISampler*>(sampler.second);
    }
    m_mipmap_sampler_map.clear();
    return;
}
void D3D12RHI::destroyShaderModule(RHIShader* shader)
{
    delete shader;
}
void D3D12RHI::destroyPipeline(RHIPipeline*& pipeline)
{
    delete static_cast<D3D12RHIPipeline*>(pipeline);
    pipeline = nullptr;
}
void D3D12RHI::destroyPipelineLayout(RHIPipelineLayout*& pipeline_layout)
{
    delete static_cast<D3D12RHIPipelineLayout*>(pipeline_layout);
    pipeline_layout = nullptr;
}
void D3D12RHI::destroyRenderPass(RHIRenderPass*& render_pass)
{
    delete static_cast<D3D12RHIRenderPass*>(render_pass);
    render_pass = nullptr;
}
void D3D12RHI::destroyDescriptorSetLayout(RHIDescriptorSetLayout*& descriptor_set_layout)
{
    delete static_cast<D3D12RHIDescriptorSetLayout*>(descriptor_set_layout);
    descriptor_set_layout = nullptr;
}
void D3D12RHI::destroySemaphore(RHISemaphore* semaphore)
{
    delete static_cast<D3D12RHISemaphore*>(semaphore);
}
void D3D12RHI::destroySampler(RHISampler* sampler)
{
    if (sampler == nullptr)
    {
        return;
    }

    if (sampler == m_linear_sampler)
    {
        m_linear_sampler = nullptr;
    }
    if (sampler == m_nearest_sampler)
    {
        m_nearest_sampler = nullptr;
    }
    for (auto iterator = m_mipmap_sampler_map.begin(); iterator != m_mipmap_sampler_map.end();)
    {
        if (iterator->second == sampler)
        {
            iterator = m_mipmap_sampler_map.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }

    delete static_cast<D3D12RHISampler*>(sampler);
    return;
}
void D3D12RHI::destroyInstance(RHIInstance* instance)
{
    delete instance;
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
    delete static_cast<D3D12RHIFramebuffer*>(framebuffer);
}
void D3D12RHI::destroyFence(RHIFence* fence)
{
    if (fence == nullptr)
    {
        return;
    }

    for (auto*& frame_fence : m_frame_fences)
    {
        if (frame_fence == fence)
        {
            delete static_cast<D3D12RHIFence*>(frame_fence);
            frame_fence = nullptr;
            return;
        }
    }

    delete static_cast<D3D12RHIFence*>(fence);
    return;
}
void D3D12RHI::destroyCommandPool(RHICommandPool* commandPool)
{
    if (commandPool == nullptr)
    {
        return;
    }

    if (commandPool == m_default_command_pool)
    {
        delete m_default_command_pool;
        m_default_command_pool = nullptr;
        return;
    }

    delete commandPool;
    return;
}
void D3D12RHI::destroyBuffer(RHIBuffer* &buffer)
{
    if (buffer == nullptr)
    {
        return;
    }

    auto* d3d_buffer = static_cast<D3D12RHIBuffer*>(buffer);
#ifdef _WIN32
    unregisterHostVisibleDefaultBuffer(d3d_buffer);
#endif
    delete d3d_buffer;
    buffer = nullptr;
    return;
}
void D3D12RHI::destroyBufferWithAllocation(RHIBuffer*& buffer, RHIAllocation*& allocation)
{
    destroyBuffer(buffer);
    freeAllocation(allocation);
}
void D3D12RHI::destroyImageWithAllocation(RHIImage*& image, RHIImageView*& image_view, RHIAllocation*& allocation)
{
    if (image_view != nullptr)
    {
        destroyImageView(image_view);
        delete image_view;
        image_view = nullptr;
    }
    if (image != nullptr)
    {
        destroyImage(image);
        delete image;
        image = nullptr;
    }
    freeAllocation(allocation);
}
void D3D12RHI::freeCommandBuffers(RHICommandPool* commandPool, uint32_t commandBufferCount, RHICommandBuffer* pCommandBuffers)
{
    (void)commandPool;
    (void)commandBufferCount;
    delete static_cast<D3D12RHICommandBuffer*>(pCommandBuffers);
}
void D3D12RHI::freeAllocation(RHIAllocation*& allocation)
{
    delete allocation;
    allocation = nullptr;
}
void D3D12RHI::freeMemory(RHIDeviceMemory* &memory)
{
    delete static_cast<D3D12RHIDeviceMemory*>(memory);
    memory = nullptr;
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

#ifdef _WIN32
    auto* d3d_buffer = d3d_memory->owner_buffer;
    if (d3d_buffer->resource != nullptr &&
        !d3d_buffer->map_host_data &&
        (d3d_buffer->heap_type == D3D12_HEAP_TYPE_UPLOAD || d3d_buffer->heap_type == D3D12_HEAP_TYPE_READBACK))
    {
        if (offset > d3d_buffer->size)
        {
            return false;
        }
        const RHIDeviceSize requested = (size == RHI_WHOLE_SIZE) ? (d3d_buffer->size - offset) : size;
        if (requested > d3d_buffer->size - offset)
        {
            return false;
        }

        D3D12_RANGE read_range = d3d_buffer->heap_type == D3D12_HEAP_TYPE_READBACK ?
                                     D3D12_RANGE {static_cast<SIZE_T>(offset),
                                                  static_cast<SIZE_T>(offset + requested)} :
                                     D3D12_RANGE {0, 0};
        void* mapped_base = nullptr;
        if (FAILED(d3d_buffer->resource->Map(0, &read_range, &mapped_base)) || mapped_base == nullptr)
        {
            return false;
        }

        d3d_memory->mapped_ptr    = static_cast<uint8_t*>(mapped_base) + offset;
        d3d_memory->mapped_offset = offset;
        d3d_memory->mapped_size   = requested;
        d3d_memory->mapped_resource = true;
        *ppData                   = d3d_memory->mapped_ptr;
        return true;
    }

    if (d3d_buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT &&
        !d3d_buffer->map_host_data &&
        !bufferHasHostVisibleMirror(*d3d_buffer))
    {
        return false;
    }
    if (d3d_buffer->heap_type != D3D12_HEAP_TYPE_DEFAULT && !d3d_buffer->map_host_data)
    {
        return false;
    }
#endif

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
    d3d_memory->mapped_resource = false;
#ifdef _WIN32
    d3d_memory->mapped_offset = offset;
    d3d_memory->mapped_size   = requested;
    if (bufferHasHostVisibleMirror(*d3d_buffer))
    {
        d3d_buffer->host_data_write_mapped = true;
        d3d_buffer->host_data_uploadable =
            bufferHostMirrorWholeRange(*d3d_buffer, offset, requested);
    }
#endif
    *ppData                = d3d_memory->mapped_ptr;
    return true;
}
void D3D12RHI::unmapMemory(RHIDeviceMemory* memory)
{
    if (memory)
    {
        auto* d3d_memory = static_cast<D3D12RHIDeviceMemory*>(memory);
#ifdef _WIN32
        if (d3d_memory->owner_buffer != nullptr &&
            d3d_memory->owner_buffer->resource != nullptr &&
            d3d_memory->mapped_ptr != nullptr &&
            d3d_memory->mapped_resource)
        {
            D3D12_RANGE written_range {static_cast<SIZE_T>(d3d_memory->mapped_offset),
                                       static_cast<SIZE_T>(d3d_memory->mapped_offset + d3d_memory->mapped_size)};
            D3D12_RANGE read_only_unmap_range {0, 0};
            if (d3d_memory->owner_buffer->heap_type == D3D12_HEAP_TYPE_UPLOAD &&
                bufferHostMirrorRangeValid(*d3d_memory->owner_buffer,
                                           d3d_memory->mapped_offset,
                                           d3d_memory->mapped_size))
            {
                std::memcpy(d3d_memory->owner_buffer->host_data.data() +
                                static_cast<size_t>(d3d_memory->mapped_offset),
                            d3d_memory->mapped_ptr,
                            static_cast<size_t>(d3d_memory->mapped_size));
                d3d_memory->owner_buffer->host_data_valid = true;
                d3d_memory->owner_buffer->host_data_uploadable = false;
            }
            d3d_memory->owner_buffer->resource->Unmap(
                0,
                d3d_memory->owner_buffer->heap_type == D3D12_HEAP_TYPE_READBACK ? &read_only_unmap_range :
                                                                                   &written_range);
        }
        else if (d3d_memory->owner_buffer != nullptr &&
                 d3d_memory->mapped_ptr != nullptr &&
                 bufferHasHostVisibleMirror(*d3d_memory->owner_buffer))
        {
            if (bufferHostMirrorRangeValid(*d3d_memory->owner_buffer,
                                           d3d_memory->mapped_offset,
                                           d3d_memory->mapped_size) &&
                bufferHostMirrorWholeRange(*d3d_memory->owner_buffer,
                                           d3d_memory->mapped_offset,
                                           d3d_memory->mapped_size))
            {
                d3d_memory->owner_buffer->host_data_valid = true;
                d3d_memory->owner_buffer->host_data_uploadable = true;
            }
            else
            {
                d3d_memory->owner_buffer->host_data_valid = false;
                d3d_memory->owner_buffer->host_data_uploadable = false;
            }
            d3d_memory->owner_buffer->host_data_write_mapped = false;
        }
#endif
        d3d_memory->mapped_ptr = nullptr;
        d3d_memory->mapped_resource = false;
#ifdef _WIN32
        d3d_memory->mapped_offset = 0;
        d3d_memory->mapped_size   = 0;
#endif
    }
    return;
}
void D3D12RHI::invalidateMappedMemoryRanges(void* pNext, RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size)
{
    (void)pNext;
#ifdef _WIN32
    auto* d3d_memory = static_cast<D3D12RHIDeviceMemory*>(memory);
    if (d3d_memory == nullptr || d3d_memory->owner_buffer == nullptr)
    {
        return;
    }

    auto* buffer = d3d_memory->owner_buffer;
    if (buffer->resource == nullptr || buffer->heap_type != D3D12_HEAP_TYPE_READBACK || buffer->host_data.empty())
    {
        return;
    }

    if (offset > buffer->size)
    {
        return;
    }
    const RHIDeviceSize copy_size = size == RHI_WHOLE_SIZE ? buffer->size - offset : size;
    if (copy_size > buffer->size - offset)
    {
        return;
    }

    void* mapped_base = nullptr;
    D3D12_RANGE read_range {static_cast<SIZE_T>(offset), static_cast<SIZE_T>(offset + copy_size)};
    if (SUCCEEDED(buffer->resource->Map(0, &read_range, &mapped_base)) && mapped_base != nullptr)
    {
        if (buffer->host_data.size() < offset + copy_size)
        {
            buffer->host_data.resize(static_cast<size_t>(offset + copy_size));
        }
        std::memcpy(buffer->host_data.data() + static_cast<size_t>(offset),
                    static_cast<uint8_t*>(mapped_base) + offset,
                    static_cast<size_t>(copy_size));
        D3D12_RANGE written_range {0, 0};
        buffer->resource->Unmap(0, &written_range);
        buffer->map_host_data = true;
        buffer->host_data_valid = true;
        buffer->host_data_uploadable = false;
    }
    else
    {
        buffer->host_data_valid = false;
        buffer->host_data_uploadable = false;
    }
#else
    (void)memory;
    (void)offset;
    (void)size;
#endif
    return;
}
void D3D12RHI::flushMappedMemoryRanges(void* pNext, RHIDeviceMemory* memory, RHIDeviceSize offset, RHIDeviceSize size)
{
    (void)pNext;
#ifdef _WIN32
    auto* d3d_memory = static_cast<D3D12RHIDeviceMemory*>(memory);
    if (d3d_memory == nullptr || d3d_memory->owner_buffer == nullptr)
    {
        return;
    }

    auto* buffer = d3d_memory->owner_buffer;
    if (offset > buffer->size)
    {
        return;
    }
    const RHIDeviceSize flush_size = size == RHI_WHOLE_SIZE ? buffer->size - offset : size;
    if (flush_size > buffer->size - offset)
    {
        return;
    }

    if (buffer->heap_type == D3D12_HEAP_TYPE_UPLOAD)
    {
        if (d3d_memory->mapped_resource &&
            d3d_memory->mapped_ptr != nullptr &&
            offset >= d3d_memory->mapped_offset &&
            offset - d3d_memory->mapped_offset <= d3d_memory->mapped_size &&
            flush_size <= d3d_memory->mapped_size - (offset - d3d_memory->mapped_offset) &&
            bufferHostMirrorRangeValid(*buffer, offset, flush_size))
        {
            std::memcpy(buffer->host_data.data() + static_cast<size_t>(offset),
                        static_cast<uint8_t*>(d3d_memory->mapped_ptr) +
                            static_cast<size_t>(offset - d3d_memory->mapped_offset),
                        static_cast<size_t>(flush_size));
            buffer->host_data_valid = true;
            buffer->host_data_uploadable = false;
        }
        return;
    }

    if (buffer->resource == nullptr ||
        !bufferHasHostVisibleMirror(*buffer))
    {
        return;
    }

    if (!mappedHostRangeContains(*d3d_memory, offset, flush_size) ||
        !bufferHostMirrorRangeValid(*buffer, offset, flush_size) ||
        !bufferHostMirrorWholeRange(*buffer, offset, flush_size))
    {
        return;
    }
    buffer->host_data_valid = true;
    buffer->host_data_uploadable = true;

    auto* current_command_buffer = static_cast<D3D12RHICommandBuffer*>(m_current_command_buffer);
    auto* current_command_list = d3d12CommandListFor(m_current_command_buffer);
    if (current_command_buffer != nullptr && current_command_buffer->is_open && current_command_list != nullptr)
    {
        (void)recordHostDataUpload(m_d3d12_device.Get(),
                                   current_command_list,
                                   m_pending_upload_buffers,
                                   *buffer);
    }
    else
    {
        (void)executeImmediateCommands(
            [&](ID3D12GraphicsCommandList* command_list)
            {
                (void)recordHostDataUpload(m_d3d12_device.Get(),
                                           command_list,
                                           m_pending_upload_buffers,
                                           *buffer);
            });
    }
#else
    (void)memory;
#endif
    return;
}
RHISemaphore*& D3D12RHI::getTextureCopySemaphore(uint32_t index)
{
    (void)index;
    if (m_texture_copy_semaphore == nullptr)
    {
        RHISemaphoreCreateInfo create_info {};
        if (!createSemaphore(&create_info, m_texture_copy_semaphore))
        {
            m_texture_copy_semaphore = nullptr;
        }
    }
    return m_texture_copy_semaphore;
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
} // namespace Piccolo
