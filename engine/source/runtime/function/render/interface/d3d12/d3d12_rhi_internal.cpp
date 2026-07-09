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
namespace d3d12_detail
{
#ifdef _WIN32
DWORD d3d12FenceTimeoutMilliseconds(uint64_t timeout)
{
    if (timeout == UINT64_MAX)
    {
        return INFINITE;
    }
    if (timeout == 0)
    {
        return 0;
    }

    const uint64_t timeout_milliseconds = (timeout + 999999ULL) / 1000000ULL;
    return static_cast<DWORD>((std::min)(timeout_milliseconds,
                                         static_cast<uint64_t>((std::numeric_limits<DWORD>::max)())));
}

bool waitForD3D12FenceValue(ID3D12Fence* fence, HANDLE event, uint64_t value, uint64_t timeout)
{
    if (fence == nullptr)
    {
        return false;
    }
    if (fence->GetCompletedValue() >= value)
    {
        return true;
    }
    if (event == nullptr || FAILED(fence->SetEventOnCompletion(value, event)))
    {
        return false;
    }

    return WaitForSingleObject(event, d3d12FenceTimeoutMilliseconds(timeout)) == WAIT_OBJECT_0;
}

uint64_t remainingD3D12FenceTimeout(uint64_t timeout, ULONGLONG start_tick)
{
    if (timeout == UINT64_MAX)
    {
        return UINT64_MAX;
    }

    const uint64_t elapsed_milliseconds = static_cast<uint64_t>(GetTickCount64() - start_tick);
    const uint64_t elapsed_nanoseconds  = elapsed_milliseconds * 1000000ULL;
    return elapsed_nanoseconds >= timeout ? 0ULL : timeout - elapsed_nanoseconds;
}
#endif
RHIDeviceSize alignUp(RHIDeviceSize value, RHIDeviceSize alignment)
{
    if (alignment == 0)
    {
        return value;
    }
    return (value + alignment - 1) / alignment * alignment;
}

bool hasFlag(uint32_t flags, uint32_t flag)
{
    return (flags & flag) != 0;
}

bool descriptorUsesSamplerHeap(RHIDescriptorType type)
{
    return type == RHI_DESCRIPTOR_TYPE_SAMPLER ||
           type == RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}

bool descriptorUsesResourceHeap(RHIDescriptorType type)
{
    return type == RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
           type == RHI_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
           type == RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
           type == RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
           type == RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
           type == RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
           type == RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC ||
           type == RHI_DESCRIPTOR_TYPE_INPUT_ATTACHMENT ||
           type == RHI_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
}

bool descriptorUsesBufferInfo(RHIDescriptorType type)
{
    return type == RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
           type == RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
           type == RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
           type == RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}

bool isTrackedDescriptorType(RHIDescriptorType type)
{
    switch (type)
    {
        case RHI_DESCRIPTOR_TYPE_SAMPLER:
        case RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case RHI_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case RHI_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case RHI_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        case RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        case RHI_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        case RHI_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            return true;
        default:
            return false;
    }
}

bool isSupportedDescriptorType(RHIDescriptorType type)
{
    return descriptorUsesSamplerHeap(type) || descriptorUsesResourceHeap(type);
}

uint32_t descriptorTypeIndex(RHIDescriptorType type)
{
    switch (type)
    {
        case RHI_DESCRIPTOR_TYPE_SAMPLER:
            return 0;
        case RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            return 1;
        case RHI_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            return 2;
        case RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return 3;
        case RHI_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            return 4;
        case RHI_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            return 5;
        case RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            return 6;
        case RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            return 7;
        case RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            return 8;
        case RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            return 9;
        case RHI_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            return 10;
        case RHI_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            return 11;
        default:
            return kTrackedDescriptorTypeCount;
    }
}

bool hasDescriptorCapacity(uint32_t required, uint32_t used, uint32_t capacity)
{
    return used <= capacity && required <= capacity - used;
}

uint32_t calculateMipLevels(uint32_t width, uint32_t height, uint32_t requested_mip_levels)
{
    if (requested_mip_levels != 0)
    {
        return requested_mip_levels;
    }

    uint32_t levels = 1;
    uint32_t size   = (std::max)(width, height);
    while (size > 1)
    {
        size = size / 2;
        ++levels;
    }
    return levels;
}

#ifdef _WIN32

bool isSamplerDescriptor(RHIDescriptorType type)
{
    return type == RHI_DESCRIPTOR_TYPE_SAMPLER ||
           type == RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}

bool isCbvSrvUavDescriptor(RHIDescriptorType type)
{
    return type == RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
           type == RHI_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
           type == RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
           type == RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
           type == RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
           type == RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
           type == RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC ||
           type == RHI_DESCRIPTOR_TYPE_INPUT_ATTACHMENT ||
           type == RHI_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
}

bool hasComputeStage(RHIShaderStageFlags stage_flags)
{
    return hasFlag(stage_flags, RHI_SHADER_STAGE_COMPUTE_BIT);
}

bool hasGraphicsStage(RHIShaderStageFlags stage_flags)
{
    const RHIShaderStageFlags graphics_stages =
        RHI_SHADER_STAGE_VERTEX_BIT |
        RHI_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
        RHI_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
        RHI_SHADER_STAGE_GEOMETRY_BIT |
        RHI_SHADER_STAGE_FRAGMENT_BIT;
    return (stage_flags & graphics_stages) != 0;
}

bool isDynamicBufferDescriptor(RHIDescriptorType type)
{
    return type == RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
           type == RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}

bool isAccelerationStructureDescriptor(RHIDescriptorType type)
{
    return type == RHI_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
}

D3D12_DESCRIPTOR_RANGE_TYPE toDescriptorRangeType(const RHIDescriptorSetLayoutBinding& binding)
{
    switch (binding.descriptorType)
    {
        case RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        case RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        case RHI_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        case RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            return hasComputeStage(binding.stageFlags) && !hasGraphicsStage(binding.stageFlags) ?
                D3D12_DESCRIPTOR_RANGE_TYPE_UAV :
                D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        case RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            if (hasGraphicsStage(binding.stageFlags) && binding.binding <= 1)
            {
                return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
            }
            return hasComputeStage(binding.stageFlags) && !hasGraphicsStage(binding.stageFlags) ?
                D3D12_DESCRIPTOR_RANGE_TYPE_UAV :
                D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        case RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case RHI_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case RHI_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        default:
            return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    }
}

D3D12_SHADER_VISIBILITY toShaderVisibility(RHIShaderStageFlags stage_flags)
{
    if (stage_flags == RHI_SHADER_STAGE_VERTEX_BIT)
    {
        return D3D12_SHADER_VISIBILITY_VERTEX;
    }
    if (stage_flags == RHI_SHADER_STAGE_FRAGMENT_BIT)
    {
        return D3D12_SHADER_VISIBILITY_PIXEL;
    }
    return D3D12_SHADER_VISIBILITY_ALL;
}

D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor(ID3D12DescriptorHeap* heap, uint32_t descriptor_size, uint32_t index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle {};
    if (heap != nullptr)
    {
        handle = heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(descriptor_size) * index;
    }
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptor(ID3D12DescriptorHeap* heap, uint32_t descriptor_size, uint32_t index)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle {};
    if (heap != nullptr)
    {
        handle = heap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<UINT64>(descriptor_size) * index;
    }
    return handle;
}

bool createDescriptorHeap(ID3D12Device* device,
                          D3D12_DESCRIPTOR_HEAP_TYPE type,
                          uint32_t descriptor_count,
                          bool shader_visible,
                          ComPtr<ID3D12DescriptorHeap>& heap,
                          uint32_t& descriptor_size,
                          uint32_t& descriptor_capacity,
                          uint32_t& descriptor_next)
{
    if (device == nullptr)
    {
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC desc {};
    desc.Type           = type;
    desc.NumDescriptors = (std::max)(1U, descriptor_count);
    desc.Flags          = shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE :
                                           D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask       = 0;

    ComPtr<ID3D12DescriptorHeap> new_heap;
    if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&new_heap))))
    {
        return false;
    }

    heap                = new_heap;
    descriptor_size     = device->GetDescriptorHandleIncrementSize(type);
    descriptor_capacity = desc.NumDescriptors;
    descriptor_next     = 0;
    return true;
}

bool createCpuDescriptorHeap(ID3D12Device* device,
                             D3D12_DESCRIPTOR_HEAP_TYPE type,
                             uint32_t descriptor_count,
                             ComPtr<ID3D12DescriptorHeap>& heap)
{
    uint32_t descriptor_size = 0;
    uint32_t descriptor_capacity = 0;
    uint32_t descriptor_next = 0;
    return createDescriptorHeap(device,
                                type,
                                descriptor_count,
                                false,
                                heap,
                                descriptor_size,
                                descriptor_capacity,
                                descriptor_next);
}

void logD3D12InfoQueueMessages(ID3D12Device* device, const char* context, UINT64 max_messages)
{
    if (device == nullptr)
    {
        return;
    }

    ComPtr<ID3D12InfoQueue> info_queue;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&info_queue))) || info_queue == nullptr)
    {
        return;
    }

    const UINT64 message_count = info_queue->GetNumStoredMessages();
    const UINT64 first_message = message_count > max_messages ? message_count - max_messages : 0;
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
            LOG_ERROR("D3D12 {} message {}: {}",
                      context != nullptr ? context : "debug",
                      static_cast<uint64_t>(message_index),
                      message->pDescription);
        }
    }
}

std::string dxgiAdapterDescriptionToUtf8(const WCHAR* description)
{
    if (description == nullptr || description[0] == L'\0')
    {
        return {};
    }

    const int required_size =
        WideCharToMultiByte(CP_UTF8, 0, description, -1, nullptr, 0, nullptr, nullptr);
    if (required_size <= 1)
    {
        return {};
    }

    std::vector<char> buffer(static_cast<size_t>(required_size), '\0');
    if (WideCharToMultiByte(CP_UTF8,
                            0,
                            description,
                            -1,
                            buffer.data(),
                            required_size,
                            nullptr,
                            nullptr) == 0)
    {
        return {};
    }
    return std::string(buffer.data());
}

bool reserveDescriptors(uint32_t count, uint32_t& next, uint32_t capacity, uint32_t& base)
{
    if (count == 0)
    {
        base = 0;
        return true;
    }
    if (next > capacity || count > capacity - next)
    {
        return false;
    }
    base = next;
    next += count;
    return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE* findCachedDynamicDescriptorTable(
    D3D12RHICommandBuffer& command_buffer,
    const D3D12RHIDescriptorSet& descriptor_set,
    uint32_t set_index,
    const std::vector<uint32_t>& dynamic_offsets)
{
    for (auto& cache_entry : command_buffer.dynamic_descriptor_table_cache)
    {
        if (cache_entry.descriptor_set == &descriptor_set &&
            cache_entry.descriptor_set_version == descriptor_set.version &&
            cache_entry.set_index == set_index &&
            cache_entry.dynamic_offsets == dynamic_offsets)
        {
            return &cache_entry.cbv_srv_uav_gpu_base;
        }
    }
    return nullptr;
}

void rememberCachedDynamicDescriptorTable(
    D3D12RHICommandBuffer& command_buffer,
    const D3D12RHIDescriptorSet& descriptor_set,
    uint32_t set_index,
    const std::vector<uint32_t>& dynamic_offsets,
    D3D12_GPU_DESCRIPTOR_HANDLE cbv_srv_uav_gpu_base)
{
    D3D12RHICommandBuffer::DynamicDescriptorTableCacheEntry cache_entry {};
    cache_entry.descriptor_set = &descriptor_set;
    cache_entry.descriptor_set_version = descriptor_set.version;
    cache_entry.set_index = set_index;
    cache_entry.dynamic_offsets = dynamic_offsets;
    cache_entry.cbv_srv_uav_gpu_base = cbv_srv_uav_gpu_base;
    command_buffer.dynamic_descriptor_table_cache.push_back(cache_entry);
}

bool descriptorRangeFits(uint32_t first, uint32_t count, uint32_t descriptor_count)
{
    return first <= descriptor_count && count <= descriptor_count - first;
}

uint32_t dynamicDescriptorCount(const D3D12RHIDescriptorSetLayout& layout)
{
    uint32_t count = 0;
    for (const auto& range : layout.ranges)
    {
        if (descriptorUsesResourceHeap(range.binding.descriptorType) &&
            isDynamicBufferDescriptor(range.binding.descriptorType))
        {
            count += range.binding.descriptorCount;
        }
    }
    return count;
}

bool descriptorWriteHasRequiredResources(const RHIWriteDescriptorSet& write,
                                         const D3D12RHIDescriptorSetLayout::BindingRange& binding)
{
    for (uint32_t descriptor_index = 0; descriptor_index < write.descriptorCount; ++descriptor_index)
    {
        const uint32_t array_index = write.dstArrayElement + descriptor_index;
        if (descriptorUsesBufferInfo(write.descriptorType))
        {
            if (write.pBufferInfo == nullptr)
            {
                return false;
            }

            const auto* buffer = static_cast<D3D12RHIBuffer*>(write.pBufferInfo[descriptor_index].buffer);
            if (buffer == nullptr || buffer->resource == nullptr)
            {
                return false;
            }
        }
        else if (isAccelerationStructureDescriptor(write.descriptorType))
        {
            if (write.pAccelerationStructureInfo == nullptr ||
                write.pAccelerationStructureInfo->pAccelerationStructures == nullptr ||
                write.pAccelerationStructureInfo->accelerationStructureCount <= descriptor_index)
            {
                return false;
            }

            const auto* acceleration_structure = static_cast<D3D12RHIAccelerationStructure*>(
                write.pAccelerationStructureInfo->pAccelerationStructures[descriptor_index]);
            if (acceleration_structure == nullptr ||
                acceleration_structure->gpu_address == 0)
            {
                return false;
            }
        }
        else if (descriptorUsesResourceHeap(write.descriptorType))
        {
            if (write.pImageInfo == nullptr)
            {
                return false;
            }

            const auto* image_view = static_cast<D3D12RHIImageView*>(write.pImageInfo[descriptor_index].imageView);
            if (image_view == nullptr || image_view->image == nullptr || image_view->image->resource == nullptr)
            {
                return false;
            }

            if (write.descriptorType == RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE)
            {
                if (!image_view->has_uav)
                {
                    return false;
                }
            }
            else if (!image_view->has_srv)
            {
                return false;
            }
        }

        if (descriptorUsesSamplerHeap(write.descriptorType))
        {
            D3D12RHISampler* sampler = nullptr;
            if (write.pImageInfo != nullptr)
            {
                sampler = static_cast<D3D12RHISampler*>(write.pImageInfo[descriptor_index].sampler);
            }
            if (sampler == nullptr && binding.binding.pImmutableSamplers != nullptr)
            {
                sampler = static_cast<D3D12RHISampler*>(binding.binding.pImmutableSamplers[array_index]);
            }
            if (sampler == nullptr)
            {
                return false;
            }
        }
    }
    return true;
}

bool descriptorCopyHasRequiredSourceMetadata(const RHICopyDescriptorSet& copy,
                                             const D3D12RHIDescriptorSet& src_set,
                                             const D3D12RHIDescriptorSetLayout::BindingRange& src_binding)
{
    if (!descriptorUsesBufferInfo(src_binding.binding.descriptorType) &&
        !isAccelerationStructureDescriptor(src_binding.binding.descriptorType))
    {
        return true;
    }

    for (uint32_t descriptor_index = 0; descriptor_index < copy.descriptorCount; ++descriptor_index)
    {
        if (isAccelerationStructureDescriptor(src_binding.binding.descriptorType))
        {
            const auto* src_descriptor = src_set.findAccelerationStructureDescriptor(
                copy.srcBinding, copy.srcArrayElement + descriptor_index);
            if (src_descriptor == nullptr ||
                src_descriptor->descriptor_type != src_binding.binding.descriptorType ||
                src_descriptor->acceleration_structure == nullptr ||
                src_descriptor->gpu_address == 0)
            {
                return false;
            }
            continue;
        }

        const auto* src_descriptor =
            src_set.findBufferDescriptor(copy.srcBinding, copy.srcArrayElement + descriptor_index);
        if (src_descriptor == nullptr ||
            src_descriptor->descriptor_type != src_binding.binding.descriptorType ||
            src_descriptor->buffer == nullptr ||
            src_descriptor->buffer->resource == nullptr)
        {
            return false;
        }
    }
    return true;
}

D3D12_HEAP_TYPE chooseBufferHeapType(RHIBufferUsageFlags usage, RHIMemoryPropertyFlags properties)
{
    const bool host_visible = hasFlag(properties, RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    const bool storage_or_indirect =
        hasFlag(usage, RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
        hasFlag(usage, RHI_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT) ||
        hasFlag(usage, RHI_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    const bool transfer_buffer =
        hasFlag(usage, RHI_BUFFER_USAGE_TRANSFER_SRC_BIT) ||
        hasFlag(usage, RHI_BUFFER_USAGE_TRANSFER_DST_BIT);
    if (host_visible &&
        hasFlag(properties, RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
        hasFlag(usage, RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT) &&
        !hasFlag(usage, RHI_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT) &&
        !hasFlag(usage, RHI_BUFFER_USAGE_INDIRECT_BUFFER_BIT) &&
        !transfer_buffer)
    {
        return D3D12_HEAP_TYPE_UPLOAD;
    }

    if (storage_or_indirect)
    {
        return D3D12_HEAP_TYPE_DEFAULT;
    }

    if (!host_visible)
    {
        return D3D12_HEAP_TYPE_DEFAULT;
    }

    if (hasFlag(usage, RHI_BUFFER_USAGE_TRANSFER_DST_BIT) &&
        !hasFlag(usage, RHI_BUFFER_USAGE_TRANSFER_SRC_BIT))
    {
        return D3D12_HEAP_TYPE_READBACK;
    }

    return D3D12_HEAP_TYPE_UPLOAD;
}

D3D12_RESOURCE_STATES initialBufferState(D3D12_HEAP_TYPE heap_type)
{
    if (heap_type == D3D12_HEAP_TYPE_UPLOAD)
    {
        return D3D12_RESOURCE_STATE_GENERIC_READ;
    }
    if (heap_type == D3D12_HEAP_TYPE_READBACK)
    {
        return D3D12_RESOURCE_STATE_COPY_DEST;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

bool bufferHostMirrorRangeValid(const D3D12RHIBuffer& buffer, RHIDeviceSize offset, RHIDeviceSize size)
{
    const size_t host_offset = static_cast<size_t>(offset);
    const size_t host_size   = static_cast<size_t>(size);
    return host_offset <= buffer.host_data.size() && host_size <= buffer.host_data.size() - host_offset;
}

bool bufferAccessIncludesGpuWrite(RHIAccessFlags access)
{
    return hasFlag(access, RHI_ACCESS_SHADER_WRITE_BIT) ||
           hasFlag(access, RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT) ||
           hasFlag(access, RHI_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) ||
           hasFlag(access, RHI_ACCESS_TRANSFER_WRITE_BIT) ||
           hasFlag(access, RHI_ACCESS_HOST_WRITE_BIT) ||
           hasFlag(access, RHI_ACCESS_MEMORY_WRITE_BIT) ||
           hasFlag(access, RHI_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT) ||
           hasFlag(access, RHI_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT) ||
           hasFlag(access, RHI_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR) ||
           hasFlag(access, RHI_ACCESS_COMMAND_PREPROCESS_WRITE_BIT_NV);
}

bool bufferHasHostVisibleMirror(const D3D12RHIBuffer& buffer)
{
    return buffer.heap_type == D3D12_HEAP_TYPE_DEFAULT &&
           hasFlag(buffer.memory_properties, RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
           !buffer.host_data.empty();
}

bool bufferHostMirrorUploadable(const D3D12RHIBuffer& buffer)
{
    return buffer.host_data_uploadable;
}

bool mappedHostRangeContains(const D3D12RHIDeviceMemory& memory, RHIDeviceSize offset, RHIDeviceSize size)
{
    return memory.mapped_ptr != nullptr &&
           !memory.mapped_resource &&
           memory.owner_buffer != nullptr &&
           memory.owner_buffer->host_data_write_mapped &&
           offset >= memory.mapped_offset &&
           offset - memory.mapped_offset <= memory.mapped_size &&
           size <= memory.mapped_size - (offset - memory.mapped_offset);
}

bool bufferHostMirrorWholeRange(const D3D12RHIBuffer& buffer, RHIDeviceSize offset, RHIDeviceSize size)
{
    return offset == 0 &&
           size >= buffer.size &&
           static_cast<RHIDeviceSize>(buffer.host_data.size()) >= buffer.size;
}

std::vector<D3D12RHIBuffer*>& trackedHostVisibleDefaultBuffers()
{
    static std::vector<D3D12RHIBuffer*> buffers;
    return buffers;
}

void registerHostVisibleDefaultBuffer(D3D12RHIBuffer& buffer)
{
    if (!bufferHasHostVisibleMirror(buffer))
    {
        return;
    }

    auto& buffers = trackedHostVisibleDefaultBuffers();
    if (std::find(buffers.begin(), buffers.end(), &buffer) == buffers.end())
    {
        buffers.push_back(&buffer);
    }
}

void unregisterHostVisibleDefaultBuffer(D3D12RHIBuffer* buffer)
{
    if (buffer == nullptr)
    {
        return;
    }

    auto& buffers = trackedHostVisibleDefaultBuffers();
    buffers.erase(std::remove(buffers.begin(), buffers.end(), buffer), buffers.end());
}

void invalidateTrackedHostVisibleDefaultMirrors()
{
    for (auto* buffer : trackedHostVisibleDefaultBuffers())
    {
        if (buffer != nullptr)
        {
            buffer->host_data_valid = false;
            buffer->host_data_uploadable = false;
        }
    }
}

void updateBufferHostMirrorAfterCopy(D3D12RHIBuffer& src,
                                     D3D12RHIBuffer& dst,
                                     bool src_host_data_valid,
                                     bool dst_host_data_valid,
                                     RHIDeviceSize src_offset,
                                     RHIDeviceSize dst_offset,
                                     RHIDeviceSize size,
                                     const char* context)
{
    if (src_host_data_valid && dst_host_data_valid && !src.host_data.empty() && !dst.host_data.empty())
    {
        if (bufferHostMirrorRangeValid(src, src_offset, size) &&
            bufferHostMirrorRangeValid(dst, dst_offset, size))
        {
            std::memcpy(dst.host_data.data() + static_cast<size_t>(dst_offset),
                        src.host_data.data() + static_cast<size_t>(src_offset),
                        static_cast<size_t>(size));
            dst.host_data_valid = true;
            dst.host_data_uploadable = false;
            return;
        }

        LOG_ERROR("{} skipped host mirror update for invalid copy range", context);
    }

    dst.host_data_valid = false;
    dst.host_data_uploadable = false;
}

D3D12_RESOURCE_FLAGS bufferResourceFlags(RHIBufferUsageFlags usage, D3D12_HEAP_TYPE heap_type)
{
    if (heap_type == D3D12_HEAP_TYPE_DEFAULT &&
        (hasFlag(usage, RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
         hasFlag(usage, RHI_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR)))
    {
        return D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    return D3D12_RESOURCE_FLAG_NONE;
}

DXGI_FORMAT toDXGIFormat(RHIFormat format)
{
    switch (format)
    {
        case RHI_FORMAT_R8_UNORM:
            return DXGI_FORMAT_R8_UNORM;
        case RHI_FORMAT_R8G8_UNORM:
            return DXGI_FORMAT_R8G8_UNORM;
        case RHI_FORMAT_R8G8B8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RHI_FORMAT_R8G8B8_SRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case RHI_FORMAT_R8G8B8A8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RHI_FORMAT_R8G8B8A8_SRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case RHI_FORMAT_B8G8R8A8_UNORM:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case RHI_FORMAT_B8G8R8A8_SRGB:
            return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case RHI_FORMAT_R16G16B16A16_SFLOAT:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case RHI_FORMAT_R32_UINT:
            return DXGI_FORMAT_R32_UINT;
        case RHI_FORMAT_R32_SFLOAT:
            return DXGI_FORMAT_R32_FLOAT;
        case RHI_FORMAT_R32G32_SFLOAT:
            return DXGI_FORMAT_R32G32_FLOAT;
        case RHI_FORMAT_R32G32B32_SFLOAT:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case RHI_FORMAT_R32G32B32A32_SFLOAT:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case RHI_FORMAT_D16_UNORM:
            return DXGI_FORMAT_D16_UNORM;
        case RHI_FORMAT_D32_SFLOAT:
            return DXGI_FORMAT_D32_FLOAT;
        case RHI_FORMAT_D24_UNORM_S8_UINT:
            return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case RHI_FORMAT_D32_SFLOAT_S8_UINT:
            return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        default:
            return DXGI_FORMAT_UNKNOWN;
    }
}

DXGI_FORMAT toResourceDXGIFormat(RHIFormat format)
{
    switch (format)
    {
        case RHI_FORMAT_D16_UNORM:
            return DXGI_FORMAT_R16_TYPELESS;
        case RHI_FORMAT_D32_SFLOAT:
            return DXGI_FORMAT_R32_TYPELESS;
        case RHI_FORMAT_D24_UNORM_S8_UINT:
            return DXGI_FORMAT_R24G8_TYPELESS;
        case RHI_FORMAT_D32_SFLOAT_S8_UINT:
            return DXGI_FORMAT_R32G8X24_TYPELESS;
        default:
            return toDXGIFormat(format);
    }
}

DXGI_FORMAT toDSVFormat(RHIFormat format)
{
    switch (format)
    {
        case RHI_FORMAT_D16_UNORM:
            return DXGI_FORMAT_D16_UNORM;
        case RHI_FORMAT_D32_SFLOAT:
            return DXGI_FORMAT_D32_FLOAT;
        case RHI_FORMAT_D24_UNORM_S8_UINT:
            return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case RHI_FORMAT_D32_SFLOAT_S8_UINT:
            return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        default:
            return toDXGIFormat(format);
    }
}

DXGI_FORMAT toSRVFormat(RHIFormat format, DXGI_FORMAT fallback_format)
{
    switch (format)
    {
        case RHI_FORMAT_D16_UNORM:
            return DXGI_FORMAT_R16_UNORM;
        case RHI_FORMAT_D32_SFLOAT:
            return DXGI_FORMAT_R32_FLOAT;
        case RHI_FORMAT_D24_UNORM_S8_UINT:
            return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case RHI_FORMAT_D32_SFLOAT_S8_UINT:
            return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        default:
            return fallback_format;
    }
}

bool isDepthFormat(RHIFormat format)
{
    return format == RHI_FORMAT_D16_UNORM ||
           format == RHI_FORMAT_D32_SFLOAT ||
           format == RHI_FORMAT_D24_UNORM_S8_UINT ||
           format == RHI_FORMAT_D32_SFLOAT_S8_UINT;
}

uint32_t sourceBytesPerPixel(RHIFormat format)
{
    switch (format)
    {
        case RHI_FORMAT_R8_UNORM:
            return 1;
        case RHI_FORMAT_R8G8_UNORM:
            return 2;
        case RHI_FORMAT_R8G8B8_UNORM:
        case RHI_FORMAT_R8G8B8_SRGB:
            return 3;
        case RHI_FORMAT_R8G8B8A8_UNORM:
        case RHI_FORMAT_R8G8B8A8_SRGB:
        case RHI_FORMAT_B8G8R8A8_UNORM:
        case RHI_FORMAT_B8G8R8A8_SRGB:
        case RHI_FORMAT_R32_UINT:
        case RHI_FORMAT_R32_SFLOAT:
            return 4;
        case RHI_FORMAT_R32G32_SFLOAT:
            return 8;
        case RHI_FORMAT_R32G32B32_SFLOAT:
            return 12;
        case RHI_FORMAT_R16G16B16A16_SFLOAT:
        case RHI_FORMAT_R32G32B32A32_SFLOAT:
            return 16;
        default:
            return 0;
    }
}

uint32_t resourceBytesPerPixel(RHIFormat format)
{
    switch (format)
    {
        case RHI_FORMAT_R8G8B8_UNORM:
        case RHI_FORMAT_R8G8B8_SRGB:
            return 4;
        case RHI_FORMAT_R32G32B32_SFLOAT:
            return 16;
        default:
            return sourceBytesPerPixel(format);
    }
}

uint32_t mipDimension(uint32_t base, uint32_t mip_level)
{
    if (mip_level >= 31U)
    {
        return 1U;
    }
    return (std::max)(1U, base >> mip_level);
}

size_t textureMipByteSize(uint32_t width,
                          uint32_t height,
                          uint32_t bytes_per_pixel)
{
    return static_cast<size_t>(width) *
           static_cast<size_t>(height) *
           static_cast<size_t>(bytes_per_pixel);
}

bool isFloat32TextureFormat(RHIFormat format)
{
    return format == RHI_FORMAT_R32_SFLOAT ||
           format == RHI_FORMAT_R32G32_SFLOAT ||
           format == RHI_FORMAT_R32G32B32_SFLOAT ||
           format == RHI_FORMAT_R32G32B32A32_SFLOAT;
}

float readTextureComponent(const uint8_t* source_pixels,
                           uint32_t source_width,
                           uint32_t bytes_per_pixel,
                           uint32_t x,
                           uint32_t y,
                           uint32_t component,
                           bool use_float_components)
{
    const uint8_t* source_component =
        source_pixels +
        (static_cast<size_t>(y) * source_width + x) * bytes_per_pixel +
        static_cast<size_t>(component) * (use_float_components ? sizeof(float) : sizeof(uint8_t));
    if (use_float_components)
    {
        float value = 0.0f;
        std::memcpy(&value, source_component, sizeof(value));
        return value;
    }
    return static_cast<float>(*source_component);
}

float sampleTextureBilinear(const uint8_t* source_pixels,
                            uint32_t source_width,
                            uint32_t source_height,
                            uint32_t bytes_per_pixel,
                            uint32_t component,
                            float source_x,
                            float source_y,
                            bool use_float_components)
{
    const float clamped_x = std::clamp(source_x, 0.0f, static_cast<float>(source_width - 1U));
    const float clamped_y = std::clamp(source_y, 0.0f, static_cast<float>(source_height - 1U));
    const uint32_t x0 = static_cast<uint32_t>(std::floor(clamped_x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(clamped_y));
    const uint32_t x1 = (std::min)(source_width - 1U, x0 + 1U);
    const uint32_t y1 = (std::min)(source_height - 1U, y0 + 1U);
    const float tx = clamped_x - static_cast<float>(x0);
    const float ty = clamped_y - static_cast<float>(y0);

    const float c00 = readTextureComponent(source_pixels,
                                           source_width,
                                           bytes_per_pixel,
                                           x0,
                                           y0,
                                           component,
                                           use_float_components);
    const float c10 = readTextureComponent(source_pixels,
                                           source_width,
                                           bytes_per_pixel,
                                           x1,
                                           y0,
                                           component,
                                           use_float_components);
    const float c01 = readTextureComponent(source_pixels,
                                           source_width,
                                           bytes_per_pixel,
                                           x0,
                                           y1,
                                           component,
                                           use_float_components);
    const float c11 = readTextureComponent(source_pixels,
                                           source_width,
                                           bytes_per_pixel,
                                           x1,
                                           y1,
                                           component,
                                           use_float_components);
    const float row0 = c00 + (c10 - c00) * tx;
    const float row1 = c01 + (c11 - c01) * tx;
    return row0 + (row1 - row0) * ty;
}

void writeTextureComponent(uint8_t* destination_pixel,
                           uint32_t component,
                           float value,
                           bool use_float_components)
{
    if (use_float_components)
    {
        std::memcpy(destination_pixel + static_cast<size_t>(component) * sizeof(float),
                    &value,
                    sizeof(value));
        return;
    }

    const float rounded = std::round(std::clamp(value, 0.0f, 255.0f));
    destination_pixel[component] = static_cast<uint8_t>(rounded);
}

std::vector<uint8_t> generateTextureMipLevel(const uint8_t* source_pixels,
                                             uint32_t source_width,
                                             uint32_t source_height,
                                             uint32_t destination_width,
                                             uint32_t destination_height,
                                             uint32_t bytes_per_pixel,
                                             RHIFormat format)
{
    std::vector<uint8_t> destination(textureMipByteSize(destination_width,
                                                       destination_height,
                                                       bytes_per_pixel),
                                     0);
    if (source_pixels == nullptr ||
        source_width == 0 ||
        source_height == 0 ||
        destination_width == 0 ||
        destination_height == 0 ||
        bytes_per_pixel == 0)
    {
        return destination;
    }

    const bool use_float_average = isFloat32TextureFormat(format) &&
                                   bytes_per_pixel % sizeof(float) == 0;
    const uint32_t component_count =
        use_float_average ? bytes_per_pixel / static_cast<uint32_t>(sizeof(float)) :
                            bytes_per_pixel;
    const float scale_x = static_cast<float>(source_width) / static_cast<float>(destination_width);
    const float scale_y = static_cast<float>(source_height) / static_cast<float>(destination_height);

    for (uint32_t y = 0; y < destination_height; ++y)
    {
        for (uint32_t x = 0; x < destination_width; ++x)
        {
            uint8_t* dst_pixel =
                destination.data() +
                (static_cast<size_t>(y) * destination_width + x) * bytes_per_pixel;

            const float source_x0 = static_cast<float>(x) * scale_x;
            const float source_y0 = static_cast<float>(y) * scale_y;
            const float source_x1 = static_cast<float>(x + 1U) * scale_x;
            const float source_y1 = static_cast<float>(y + 1U) * scale_y;
            const uint32_t sample_x_count =
                (std::max)(1U, static_cast<uint32_t>(std::ceil(source_x1) - std::floor(source_x0)));
            const uint32_t sample_y_count =
                (std::max)(1U, static_cast<uint32_t>(std::ceil(source_y1) - std::floor(source_y0)));
            const uint32_t sample_count = sample_x_count * sample_y_count;

            for (uint32_t component = 0; component < component_count; ++component)
            {
                float sum = 0.0f;
                for (uint32_t sample_y = 0; sample_y < sample_y_count; ++sample_y)
                {
                    const float fy = (static_cast<float>(sample_y) + 0.5f) /
                                     static_cast<float>(sample_y_count);
                    for (uint32_t sample_x = 0; sample_x < sample_x_count; ++sample_x)
                    {
                        const float fx = (static_cast<float>(sample_x) + 0.5f) /
                                         static_cast<float>(sample_x_count);
                        const float source_x = source_x0 + (source_x1 - source_x0) * fx - 0.5f;
                        const float source_y = source_y0 + (source_y1 - source_y0) * fy - 0.5f;
                        sum += sampleTextureBilinear(source_pixels,
                                                     source_width,
                                                     source_height,
                                                     bytes_per_pixel,
                                                     component,
                                                     source_x,
                                                     source_y,
                                                     use_float_average);
                    }
                }
                writeTextureComponent(dst_pixel,
                                      component,
                                      sum / static_cast<float>(sample_count),
                                      use_float_average);
            }
        }
    }

    return destination;
}

void copyTextureRowToD3D12Upload(uint8_t* dst_row,
                                 const uint8_t* src_row,
                                 uint32_t width,
                                 size_t source_row_size,
                                 size_t destination_row_size,
                                 uint32_t source_bytes_per_pixel,
                                 uint32_t resource_bytes_per_pixel)
{
    if (dst_row == nullptr || src_row == nullptr)
    {
        return;
    }

    if (source_bytes_per_pixel == resource_bytes_per_pixel)
    {
        std::memcpy(dst_row, src_row, (std::min)(source_row_size, destination_row_size));
    }
    else if (source_bytes_per_pixel == 3 && resource_bytes_per_pixel == 4)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            dst_row[x * 4 + 0] = src_row[x * 3 + 0];
            dst_row[x * 4 + 1] = src_row[x * 3 + 1];
            dst_row[x * 4 + 2] = src_row[x * 3 + 2];
            dst_row[x * 4 + 3] = 255;
        }
    }
    else if (source_bytes_per_pixel == 12 && resource_bytes_per_pixel == 16)
    {
        for (uint32_t x = 0; x < width; ++x)
        {
            std::memcpy(dst_row + x * 16, src_row + x * 12, 12);
            float alpha = 1.0f;
            std::memcpy(dst_row + x * 16 + 12, &alpha, sizeof(alpha));
        }
    }
    else
    {
        const size_t row_copy_size = (std::min)(source_row_size, destination_row_size);
        std::memcpy(dst_row, src_row, row_copy_size);
    }
}

D3D12_RESOURCE_FLAGS imageResourceFlags(RHIImageUsageFlags usage)
{
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (hasFlag(usage, RHI_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
    {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (hasFlag(usage, RHI_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
    {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }
    if (hasFlag(usage, RHI_IMAGE_USAGE_STORAGE_BIT))
    {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    return flags;
}

D3D12_RESOURCE_STATES initialImageState(RHIImageUsageFlags usage)
{
    if (hasFlag(usage, RHI_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
    {
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
    if (hasFlag(usage, RHI_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
    {
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

D3D12_RESOURCE_STATES toD3D12ResourceState(RHIImageLayout layout)
{
    switch (layout)
    {
        case RHI_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR:
        case RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case RHI_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
        case RHI_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
        case RHI_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
        case RHI_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case RHI_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        case RHI_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
        case RHI_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
        case RHI_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL:
            return D3D12_RESOURCE_STATE_DEPTH_READ |
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case RHI_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return D3D12_RESOURCE_STATE_COPY_DEST;
        case RHI_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case RHI_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return D3D12_RESOURCE_STATE_PRESENT;
        case RHI_IMAGE_LAYOUT_GENERAL:
            return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case RHI_IMAGE_LAYOUT_UNDEFINED:
        default:
            return D3D12_RESOURCE_STATE_COMMON;
    }
}

D3D12_RESOURCE_STATES toD3D12BufferState(RHIAccessFlags access,
                                         RHIBufferUsageFlags usage,
                                         D3D12_HEAP_TYPE heap_type,
                                         RHIPipelineStageFlags src_stage_mask,
                                         RHIPipelineStageFlags dst_stage_mask)
{
    if (heap_type == D3D12_HEAP_TYPE_UPLOAD)
    {
        return D3D12_RESOURCE_STATE_GENERIC_READ;
    }
    if (heap_type == D3D12_HEAP_TYPE_READBACK)
    {
        return D3D12_RESOURCE_STATE_COPY_DEST;
    }

    const RHIPipelineStageFlags graphics_stage_mask =
        RHI_PIPELINE_STAGE_VERTEX_INPUT_BIT | RHI_PIPELINE_STAGE_VERTEX_SHADER_BIT |
        RHI_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
        RHI_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | RHI_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
        RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | RHI_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        RHI_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        RHI_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    const bool compute_domain_barrier =
        (src_stage_mask & RHI_PIPELINE_STAGE_COMPUTE_SHADER_BIT) != 0 &&
        (dst_stage_mask & graphics_stage_mask) == 0 &&
        (dst_stage_mask & RHI_PIPELINE_STAGE_TRANSFER_BIT) == 0 &&
        (dst_stage_mask & RHI_PIPELINE_STAGE_HOST_BIT) == 0;

    D3D12_RESOURCE_STATES state = static_cast<D3D12_RESOURCE_STATES>(0);
    if (hasFlag(access, RHI_ACCESS_TRANSFER_READ_BIT))
    {
        state |= D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    if (hasFlag(access, RHI_ACCESS_TRANSFER_WRITE_BIT))
    {
        state |= D3D12_RESOURCE_STATE_COPY_DEST;
    }
    if (hasFlag(access, RHI_ACCESS_INDIRECT_COMMAND_READ_BIT))
    {
        state |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }
    if (hasFlag(usage, RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT) &&
        (hasFlag(access, RHI_ACCESS_SHADER_READ_BIT) || hasFlag(access, RHI_ACCESS_SHADER_WRITE_BIT)))
    {
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (compute_domain_barrier && hasFlag(usage, RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT) &&
        hasFlag(access, RHI_ACCESS_INDIRECT_COMMAND_READ_BIT) &&
        !hasFlag(access, RHI_ACCESS_SHADER_READ_BIT) && !hasFlag(access, RHI_ACCESS_SHADER_WRITE_BIT))
    {
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }
    if (hasFlag(access, RHI_ACCESS_INDEX_READ_BIT))
    {
        state |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
    }
    if (hasFlag(access, RHI_ACCESS_VERTEX_ATTRIBUTE_READ_BIT) ||
        hasFlag(access, RHI_ACCESS_UNIFORM_READ_BIT))
    {
        state |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    }
    if (hasFlag(access, RHI_ACCESS_SHADER_WRITE_BIT))
    {
        state |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if ((hasFlag(access, RHI_ACCESS_SHADER_READ_BIT) ||
         hasFlag(access, RHI_ACCESS_INPUT_ATTACHMENT_READ_BIT)) &&
        !hasFlag(access, RHI_ACCESS_INDIRECT_COMMAND_READ_BIT))
    {
        if (compute_domain_barrier)
        {
            state |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }
        else
        {
            state |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }
    }

    if (state == 0)
    {
        if (hasFlag(usage, RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT))
        {
            state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        else if (hasFlag(usage, RHI_BUFFER_USAGE_INDEX_BUFFER_BIT))
        {
            state = D3D12_RESOURCE_STATE_INDEX_BUFFER;
        }
        else if (hasFlag(usage, RHI_BUFFER_USAGE_VERTEX_BUFFER_BIT))
        {
            state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        }
        else if (hasFlag(usage, RHI_BUFFER_USAGE_INDIRECT_BUFFER_BIT))
        {
            state = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        }
        else
        {
            state = D3D12_RESOURCE_STATE_COMMON;
        }
    }

    return state;
}

uint32_t d3d12SubresourceIndex(const D3D12RHIImage& image, uint32_t mip_level, uint32_t array_layer)
{
    const uint32_t mip_count = (std::max)(1U, image.mip_levels);
    return mip_level + array_layer * mip_count;
}

void appendUniqueBuffer(std::vector<D3D12RHIBuffer*>& buffers, D3D12RHIBuffer* buffer)
{
    if (buffer == nullptr)
    {
        return;
    }
    if (std::find(buffers.begin(), buffers.end(), buffer) == buffers.end())
    {
        buffers.push_back(buffer);
    }
}

void rebuildDescriptorSetBufferLists(D3D12RHIDescriptorSet& descriptor_set)
{
    descriptor_set.storage_buffers.clear();
    descriptor_set.host_visible_default_buffers.clear();
    for (const auto& descriptor : descriptor_set.buffer_descriptors)
    {
        if (descriptor.buffer == nullptr)
        {
            continue;
        }

        appendUniqueBuffer(descriptor_set.storage_buffers, descriptor.buffer);
        if (descriptor.buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT &&
            hasFlag(descriptor.buffer->memory_properties, RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
        {
            appendUniqueBuffer(descriptor_set.host_visible_default_buffers, descriptor.buffer);
        }
    }
}

void upsertBufferDescriptor(D3D12RHIDescriptorSet& descriptor_set,
                            const D3D12RHIDescriptorSet::BufferDescriptor& descriptor)
{
    if (auto* existing_descriptor = descriptor_set.findBufferDescriptor(descriptor.binding,
                                                                         descriptor.array_element))
    {
        *existing_descriptor = descriptor;
    }
    else
    {
        descriptor_set.buffer_descriptors.push_back(descriptor);
    }

    rebuildDescriptorSetBufferLists(descriptor_set);
}

void upsertAccelerationStructureDescriptor(
    D3D12RHIDescriptorSet& descriptor_set,
    const D3D12RHIDescriptorSet::AccelerationStructureDescriptor& descriptor)
{
    if (auto* existing_descriptor =
            descriptor_set.findAccelerationStructureDescriptor(descriptor.binding, descriptor.array_element))
    {
        *existing_descriptor = descriptor;
    }
    else
    {
        descriptor_set.acceleration_structure_descriptors.push_back(descriptor);
    }
}

bool formatHasStencil(RHIFormat format)
{
    return format == RHI_FORMAT_D24_UNORM_S8_UINT ||
           format == RHI_FORMAT_D32_SFLOAT_S8_UINT;
}

bool isDepthReadOnlyLayout(RHIImageLayout layout)
{
    return layout == RHI_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ||
           layout == RHI_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL ||
           layout == RHI_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL ||
           layout == RHI_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL_KHR ||
           layout == RHI_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL_KHR;
}

D3D12_RESOURCE_STATES descriptorBufferState(D3D12_DESCRIPTOR_RANGE_TYPE range_type)
{
    switch (range_type)
    {
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
        default:
            return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
}

uint32_t structuredBufferStride(const D3D12RHIDescriptorSetLayout::BindingRange& binding,
                                const D3D12RHIDescriptorSet::BufferDescriptor& descriptor,
                                RHIDeviceSize resolved_range)
{
    if (hasComputeStage(binding.binding.stageFlags) && !hasGraphicsStage(binding.binding.stageFlags))
    {
        switch (binding.binding.binding)
        {
            case 1:
            case 9:
                return 64;
            case 2:
            case 4:
            case 5:
            case 6:
                return 16;
            case 3:
                return 48;
            case 7:
                return 160;
            default:
                break;
        }
    }

    if (hasGraphicsStage(binding.binding.stageFlags))
    {
        if (isDynamicBufferDescriptor(descriptor.descriptor_type) && binding.binding.binding == 2)
        {
            return 64;
        }
        if (!isDynamicBufferDescriptor(descriptor.descriptor_type) &&
            binding.binding.binding == 0 &&
            resolved_range > 32 &&
            resolved_range % 32 == 0)
        {
            return 32;
        }
        if (!isDynamicBufferDescriptor(descriptor.descriptor_type) &&
            binding.binding.binding == 1 &&
            resolved_range > 64 &&
            resolved_range % 64 == 0)
        {
            return 64;
        }
        if (resolved_range > 0 && resolved_range <= (std::numeric_limits<uint32_t>::max)())
        {
            return static_cast<uint32_t>(resolved_range);
        }
    }

    return 4;
}

RHIDeviceSize resolvedDescriptorRange(const D3D12RHIDescriptorSet::BufferDescriptor& descriptor,
                                      RHIDeviceSize byte_offset)
{
    if (descriptor.buffer == nullptr || byte_offset >= descriptor.buffer->size)
    {
        return 0;
    }
    if (descriptor.range == RHI_WHOLE_SIZE)
    {
        return descriptor.buffer->size - byte_offset;
    }
    return (std::min)(descriptor.range, descriptor.buffer->size - byte_offset);
}

uint32_t resolvedStructuredBufferStride(uint32_t stride, RHIDeviceSize range)
{
    if (range == 0)
    {
        return 0;
    }

    const RHIDeviceSize clamped_stride =
        stride == 0 ? range : (std::min)(static_cast<RHIDeviceSize>(stride), range);
    return static_cast<uint32_t>((std::min)(clamped_stride,
                                            static_cast<RHIDeviceSize>((std::numeric_limits<uint32_t>::max)())));
}

void writeBufferDescriptor(ID3D12Device* device,
                           D3D12_CPU_DESCRIPTOR_HANDLE dst_handle,
                           const D3D12RHIDescriptorSetLayout::BindingRange& binding,
                           const D3D12RHIDescriptorSet::BufferDescriptor& descriptor,
                           RHIDeviceSize dynamic_offset)
{
    if (device == nullptr)
    {
        return;
    }

    const RHIDeviceSize byte_offset = descriptor.offset + dynamic_offset;
    const RHIDeviceSize range       = resolvedDescriptorRange(descriptor, byte_offset);
    if (descriptor.range_type == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
    {
        if (descriptor.buffer == nullptr || descriptor.buffer->resource == nullptr || range == 0)
        {
            device->CreateConstantBufferView(nullptr, dst_handle);
            return;
        }

        const RHIDeviceSize resource_width = descriptor.buffer->resource->GetDesc().Width;
        if (byte_offset >= resource_width)
        {
            device->CreateConstantBufferView(nullptr, dst_handle);
            return;
        }

        const RHIDeviceSize available_size = resource_width - byte_offset;
        const RHIDeviceSize aligned_range = alignUp(range, 256);
        if (aligned_range == 0 || aligned_range > available_size)
        {
            device->CreateConstantBufferView(nullptr, dst_handle);
            return;
        }

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc {};
        cbv_desc.BufferLocation = descriptor.buffer->resource->GetGPUVirtualAddress() + byte_offset;
        cbv_desc.SizeInBytes    = static_cast<UINT>(aligned_range);
        device->CreateConstantBufferView(&cbv_desc, dst_handle);
        return;
    }

    const uint32_t stride = resolvedStructuredBufferStride(structuredBufferStride(binding, descriptor, range),
                                                           range);
    if (descriptor.range_type == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc {};
        const bool has_valid_view_resource =
            descriptor.buffer != nullptr &&
            descriptor.buffer->resource != nullptr &&
            stride != 0 &&
            range != 0;
        uav_desc.Format                     = DXGI_FORMAT_UNKNOWN;
        uav_desc.ViewDimension              = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.StructureByteStride = has_valid_view_resource ? stride : 4;
        uav_desc.Buffer.NumElements         = 1;
        if (has_valid_view_resource)
        {
            uav_desc.Buffer.FirstElement = byte_offset / stride;
            uav_desc.Buffer.NumElements  = static_cast<UINT>((std::max)(static_cast<RHIDeviceSize>(1),
                                                                         range / stride));
        }
        device->CreateUnorderedAccessView(has_valid_view_resource ? descriptor.buffer->resource.Get() : nullptr,
                                          nullptr,
                                          &uav_desc,
                                          dst_handle);
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc {};
    const bool has_valid_view_resource =
        descriptor.buffer != nullptr &&
        descriptor.buffer->resource != nullptr &&
        stride != 0 &&
        range != 0;
    srv_desc.Format                     = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Buffer.StructureByteStride = has_valid_view_resource ? stride : 4;
    srv_desc.Buffer.NumElements         = 1;
    if (has_valid_view_resource)
    {
        srv_desc.Buffer.FirstElement = byte_offset / stride;
        srv_desc.Buffer.NumElements  = static_cast<UINT>((std::max)(static_cast<RHIDeviceSize>(1),
                                                                     range / stride));
    }
    device->CreateShaderResourceView(has_valid_view_resource ? descriptor.buffer->resource.Get() : nullptr,
                                     &srv_desc,
                                     dst_handle);
}

D3D12_TEXTURE_ADDRESS_MODE toD3D12AddressMode(RHISamplerAddressMode address_mode)
{
    switch (address_mode)
    {
        case RHI_SAMPLER_ADDRESS_MODE_REPEAT:
            return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case RHI_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
            return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        case RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
            return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        case RHI_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
            return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
        case RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
        default:
            return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    }
}

D3D12_COMPARISON_FUNC toD3D12ComparisonFunc(RHICompareOp compare_op)
{
    switch (compare_op)
    {
        case RHI_COMPARE_OP_NEVER:
            return D3D12_COMPARISON_FUNC_NEVER;
        case RHI_COMPARE_OP_LESS:
            return D3D12_COMPARISON_FUNC_LESS;
        case RHI_COMPARE_OP_EQUAL:
            return D3D12_COMPARISON_FUNC_EQUAL;
        case RHI_COMPARE_OP_LESS_OR_EQUAL:
            return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case RHI_COMPARE_OP_GREATER:
            return D3D12_COMPARISON_FUNC_GREATER;
        case RHI_COMPARE_OP_NOT_EQUAL:
            return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case RHI_COMPARE_OP_GREATER_OR_EQUAL:
            return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case RHI_COMPARE_OP_ALWAYS:
        default:
            return D3D12_COMPARISON_FUNC_ALWAYS;
    }
}

D3D12_FILTER toD3D12Filter(const RHISamplerCreateInfo& create_info)
{
    if (create_info.anisotropyEnable)
    {
        return create_info.compareEnable ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_ANISOTROPIC;
    }

    const bool linear = create_info.magFilter == RHI_FILTER_LINEAR ||
                        create_info.minFilter == RHI_FILTER_LINEAR ||
                        create_info.mipmapMode == RHI_SAMPLER_MIPMAP_MODE_LINEAR;

    if (create_info.compareEnable)
    {
        return linear ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR :
                        D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    }

    return linear ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
}

void setD3D12BorderColor(RHIBorderColor border_color, float (&out_color)[4])
{
    switch (border_color)
    {
        case RHI_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
        case RHI_BORDER_COLOR_INT_OPAQUE_WHITE:
            out_color[0] = 1.0f;
            out_color[1] = 1.0f;
            out_color[2] = 1.0f;
            out_color[3] = 1.0f;
            break;
        case RHI_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
        case RHI_BORDER_COLOR_INT_OPAQUE_BLACK:
            out_color[0] = 0.0f;
            out_color[1] = 0.0f;
            out_color[2] = 0.0f;
            out_color[3] = 1.0f;
            break;
        case RHI_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
        case RHI_BORDER_COLOR_INT_TRANSPARENT_BLACK:
        default:
            out_color[0] = 0.0f;
            out_color[1] = 0.0f;
            out_color[2] = 0.0f;
            out_color[3] = 0.0f;
            break;
    }
}

bool createCommittedBuffer(ID3D12Device* device,
                           RHIDeviceSize size,
                           RHIBufferUsageFlags usage,
                           RHIMemoryPropertyFlags properties,
                           D3D12RHIBuffer& buffer)
{
    buffer.size              = size;
    buffer.usage             = usage;
    buffer.memory_properties = properties;
    buffer.heap_type         = chooseBufferHeapType(usage, properties);
    buffer.current_state     = initialBufferState(buffer.heap_type);

    if (size == 0)
    {
        buffer.host_data.resize(static_cast<size_t>(size));
        buffer.host_data_valid = buffer.heap_type == D3D12_HEAP_TYPE_UPLOAD;
        return true;
    }

    if (device == nullptr)
    {
        buffer.host_data.clear();
        LOG_ERROR("Failed to create D3D12 buffer resource: device is null (size={})", size);
        return false;
    }

    D3D12_HEAP_PROPERTIES heap_properties {};
    heap_properties.Type                 = buffer.heap_type;
    heap_properties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_properties.CreationNodeMask     = 1;
    heap_properties.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC resource_desc {};
    resource_desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment          = 0;
    const RHIDeviceSize resource_width =
        hasFlag(usage, RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT) ?
            alignUp((std::max)(static_cast<RHIDeviceSize>(1), size), 256) :
            (std::max)(static_cast<RHIDeviceSize>(1), size);
    resource_desc.Width              = resource_width;
    resource_desc.Height             = 1;
    resource_desc.DepthOrArraySize   = 1;
    resource_desc.MipLevels          = 1;
    resource_desc.Format             = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count   = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags              = bufferResourceFlags(usage, buffer.heap_type);
    buffer.resource_flags            = resource_desc.Flags;

    const HRESULT resource_result =
        device->CreateCommittedResource(&heap_properties,
                                        D3D12_HEAP_FLAG_NONE,
                                        &resource_desc,
                                        buffer.current_state,
                                        nullptr,
                                        IID_PPV_ARGS(&buffer.resource));
    if (FAILED(resource_result))
    {
        buffer.resource.Reset();
        buffer.host_data.clear();
        buffer.host_data_valid = false;
        buffer.host_data_uploadable = false;
        const HRESULT removed_reason = device->GetDeviceRemovedReason();
        logD3D12InfoQueueMessages(device, "buffer creation failure");
        LOG_ERROR("Failed to create D3D12 buffer resource (size={}, usage={}, memory_properties={}, heap_type={}, initial_state={}, flags={}, HRESULT=0x{:08X}, removed_reason=0x{:08X})",
                  size,
                  usage,
                  properties,
                  static_cast<uint32_t>(buffer.heap_type),
                  static_cast<uint32_t>(buffer.current_state),
                  static_cast<uint32_t>(resource_desc.Flags),
                  static_cast<unsigned int>(resource_result),
                  static_cast<unsigned int>(removed_reason));
        return false;
    }

    if (hasFlag(properties, RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
    {
        buffer.host_data.resize(static_cast<size_t>(size));
    }
    buffer.host_data_valid = false;
    buffer.host_data_uploadable = false;
    return true;
}

void transitionResource(ID3D12GraphicsCommandList* command_list,
                        ID3D12Resource* resource,
                        D3D12_RESOURCE_STATES& current_state,
                        D3D12_RESOURCE_STATES target_state)
{
    if (command_list == nullptr || resource == nullptr || current_state == target_state)
    {
        return;
    }

#ifdef _WIN32
    const D3D12_COMMAND_LIST_TYPE list_type = command_list->GetType();
    const D3D12_RESOURCE_STATES compute_invalid_state_mask =
        D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_DEPTH_WRITE |
        D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER;
    const D3D12_RESOURCE_STATES sanitized_current_state =
        static_cast<D3D12_RESOURCE_STATES>(current_state & (~compute_invalid_state_mask));
    const D3D12_RESOURCE_STATES sanitized_target_state =
        static_cast<D3D12_RESOURCE_STATES>(target_state & (~compute_invalid_state_mask));
    if (list_type == D3D12_COMMAND_LIST_TYPE_COMPUTE || list_type == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        if (current_state == D3D12_RESOURCE_STATE_COMMON || target_state == D3D12_RESOURCE_STATE_COMMON)
        {
            current_state = target_state;
            return;
        }
    }

    if (list_type == D3D12_COMMAND_LIST_TYPE_COMPUTE &&
        (sanitized_current_state != current_state || sanitized_target_state != target_state))
    {
        LOG_ERROR("D3D12 compute transition sanitized for resource 0x{:X}: before={} after={} sanitized_before={} sanitized_after={}",
                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(resource)),
                  static_cast<unsigned int>(current_state),
                  static_cast<unsigned int>(target_state),
                  static_cast<unsigned int>(sanitized_current_state),
                  static_cast<unsigned int>(sanitized_target_state));
        if (sanitized_current_state == sanitized_target_state)
        {
            current_state = target_state;
            return;
        }
        current_state = sanitized_current_state;
        target_state  = sanitized_target_state;
    }
#endif

    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = resource;
    barrier.Transition.StateBefore = current_state;
    barrier.Transition.StateAfter  = target_state;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list->ResourceBarrier(1, &barrier);
    current_state = target_state;
}

void clearRootDescriptorTableCache(std::vector<D3D12_GPU_DESCRIPTOR_HANDLE>& tables,
                                   std::vector<bool>& valid)
{
    tables.clear();
    valid.clear();
}

void clearRootDescriptorTableCache(D3D12RHICommandBuffer& command_buffer,
                                   RHIPipelineBindPoint bind_point)
{
    if (bind_point == RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
    {
        clearRootDescriptorTableCache(command_buffer.ray_tracing_root_descriptor_tables,
                                      command_buffer.ray_tracing_root_descriptor_table_valid);
    }
    else if (bind_point == RHI_PIPELINE_BIND_POINT_COMPUTE)
    {
        clearRootDescriptorTableCache(command_buffer.compute_root_descriptor_tables,
                                      command_buffer.compute_root_descriptor_table_valid);
    }
    else
    {
        clearRootDescriptorTableCache(command_buffer.graphics_root_descriptor_tables,
                                      command_buffer.graphics_root_descriptor_table_valid);
    }
}

void markCommandBufferDescriptorHeapsDirty(D3D12RHICommandBuffer& command_buffer)
{
    command_buffer.descriptor_heaps_dirty = true;
    command_buffer.bound_cbv_srv_uav_heap = nullptr;
    command_buffer.bound_sampler_heap     = nullptr;
}

void markCommandBufferExternalStateDirty(D3D12RHICommandBuffer& command_buffer)
{
    markCommandBufferDescriptorHeapsDirty(command_buffer);
    command_buffer.graphics_root_signature_dirty = true;
    command_buffer.compute_root_signature_dirty  = true;
    command_buffer.ray_tracing_root_signature_dirty = true;
}

void resetCommandBufferDescriptorHeapState(D3D12RHICommandBuffer& command_buffer)
{
    markCommandBufferDescriptorHeapsDirty(command_buffer);
    command_buffer.graphics_root_signature_dirty = true;
    command_buffer.compute_root_signature_dirty  = true;
    command_buffer.ray_tracing_root_signature_dirty = true;
    clearRootDescriptorTableCache(command_buffer, RHI_PIPELINE_BIND_POINT_GRAPHICS);
    clearRootDescriptorTableCache(command_buffer, RHI_PIPELINE_BIND_POINT_COMPUTE);
    clearRootDescriptorTableCache(command_buffer, RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR);
}

void rememberRootDescriptorTable(D3D12RHICommandBuffer& command_buffer,
                                 RHIPipelineBindPoint bind_point,
                                 uint32_t root_index,
                                 D3D12_GPU_DESCRIPTOR_HANDLE descriptor)
{
    auto& tables = bind_point == RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR ?
                       command_buffer.ray_tracing_root_descriptor_tables :
                       (bind_point == RHI_PIPELINE_BIND_POINT_COMPUTE ?
                            command_buffer.compute_root_descriptor_tables :
                            command_buffer.graphics_root_descriptor_tables);
    auto& valid = bind_point == RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR ?
                      command_buffer.ray_tracing_root_descriptor_table_valid :
                      (bind_point == RHI_PIPELINE_BIND_POINT_COMPUTE ?
                           command_buffer.compute_root_descriptor_table_valid :
                           command_buffer.graphics_root_descriptor_table_valid);
    if (root_index >= tables.size())
    {
        tables.resize(root_index + 1, {});
        valid.resize(root_index + 1, false);
    }
    tables[root_index] = descriptor;
    valid[root_index]  = true;
}

bool rootSignatureDirtyForBindPoint(const D3D12RHICommandBuffer& command_buffer,
                                    RHIPipelineBindPoint bind_point)
{
    if (bind_point == RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
    {
        return command_buffer.ray_tracing_root_signature_dirty;
    }
    return bind_point == RHI_PIPELINE_BIND_POINT_COMPUTE ?
               command_buffer.compute_root_signature_dirty :
               command_buffer.graphics_root_signature_dirty;
}

bool restoreRootSignatureForDescriptorReplay(ID3D12GraphicsCommandList* command_list,
                                             D3D12RHICommandBuffer& command_buffer,
                                             RHIPipelineBindPoint bind_point)
{
    if (command_list == nullptr)
    {
        return false;
    }

    if (bind_point == RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
    {
        if (command_buffer.bound_ray_tracing_root_signature == nullptr)
        {
            return false;
        }
        if (command_buffer.ray_tracing_root_signature_dirty)
        {
            command_list->SetComputeRootSignature(command_buffer.bound_ray_tracing_root_signature);
            command_buffer.ray_tracing_root_signature_dirty = false;
        }
        return true;
    }

    if (bind_point == RHI_PIPELINE_BIND_POINT_COMPUTE)
    {
        if (command_buffer.bound_compute_root_signature == nullptr)
        {
            return false;
        }
        if (command_buffer.compute_root_signature_dirty)
        {
            command_list->SetComputeRootSignature(command_buffer.bound_compute_root_signature);
            command_buffer.compute_root_signature_dirty = false;
        }
        return true;
    }

    if (command_buffer.bound_graphics_root_signature == nullptr)
    {
        return false;
    }
    if (command_buffer.graphics_root_signature_dirty)
    {
        command_list->SetGraphicsRootSignature(command_buffer.bound_graphics_root_signature);
        command_buffer.graphics_root_signature_dirty = false;
    }
    return true;
}

void replayRootDescriptorTables(ID3D12GraphicsCommandList* command_list,
                                D3D12RHICommandBuffer& command_buffer,
                                RHIPipelineBindPoint bind_point)
{
    if (!restoreRootSignatureForDescriptorReplay(command_list, command_buffer, bind_point))
    {
        return;
    }

    if (bind_point == RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
    {
        for (uint32_t root_index = 0;
             root_index < command_buffer.ray_tracing_root_descriptor_table_valid.size() &&
             root_index < command_buffer.ray_tracing_root_descriptor_tables.size();
             ++root_index)
        {
            if (command_buffer.ray_tracing_root_descriptor_table_valid[root_index])
            {
                command_list->SetComputeRootDescriptorTable(
                    root_index,
                    command_buffer.ray_tracing_root_descriptor_tables[root_index]);
            }
        }
        return;
    }

    if (bind_point == RHI_PIPELINE_BIND_POINT_COMPUTE)
    {
        for (uint32_t root_index = 0;
             root_index < command_buffer.compute_root_descriptor_table_valid.size() &&
             root_index < command_buffer.compute_root_descriptor_tables.size();
             ++root_index)
        {
            if (command_buffer.compute_root_descriptor_table_valid[root_index])
            {
                command_list->SetComputeRootDescriptorTable(
                    root_index,
                    command_buffer.compute_root_descriptor_tables[root_index]);
            }
        }
        return;
    }

    for (uint32_t root_index = 0;
         root_index < command_buffer.graphics_root_descriptor_table_valid.size() &&
         root_index < command_buffer.graphics_root_descriptor_tables.size();
         ++root_index)
    {
        if (command_buffer.graphics_root_descriptor_table_valid[root_index])
        {
            command_list->SetGraphicsRootDescriptorTable(root_index,
                                                         command_buffer.graphics_root_descriptor_tables[root_index]);
        }
    }
}

void bindEngineDescriptorHeaps(ID3D12GraphicsCommandList* command_list,
                               D3D12RHICommandBuffer& command_buffer,
                               ID3D12DescriptorHeap* cbv_srv_uav_heap,
                               ID3D12DescriptorHeap* sampler_heap,
                               bool replay_tables,
                               RHIPipelineBindPoint replay_bind_point)
{
    if (command_list == nullptr)
    {
        return;
    }

    const bool needs_root_signature_restore =
        replay_tables && rootSignatureDirtyForBindPoint(command_buffer, replay_bind_point);
    const bool needs_bind =
        command_buffer.descriptor_heaps_dirty ||
        command_buffer.bound_cbv_srv_uav_heap != cbv_srv_uav_heap ||
        command_buffer.bound_sampler_heap != sampler_heap ||
        needs_root_signature_restore;
    if (!needs_bind)
    {
        return;
    }

    ID3D12DescriptorHeap* heaps[2] {};
    UINT heap_count = 0;
    if (cbv_srv_uav_heap != nullptr)
    {
        heaps[heap_count++] = cbv_srv_uav_heap;
    }
    if (sampler_heap != nullptr)
    {
        heaps[heap_count++] = sampler_heap;
    }

    if (heap_count > 0)
    {
        command_list->SetDescriptorHeaps(heap_count, heaps);
    }

    command_buffer.bound_cbv_srv_uav_heap = cbv_srv_uav_heap;
    command_buffer.bound_sampler_heap     = sampler_heap;
    command_buffer.descriptor_heaps_dirty = false;
    if (replay_tables && heap_count > 0)
    {
        replayRootDescriptorTables(command_list, command_buffer, replay_bind_point);
    }
}

uint32_t d3d12SubresourceCount(const D3D12RHIImage& image)
{
    return (std::max)(1U, image.mip_levels) * (std::max)(1U, image.array_layers);
}

void syncImageCurrentState(D3D12RHIImage& image)
{
    if (image.subresource_states.empty())
    {
        image.subresource_states.assign(d3d12SubresourceCount(image), image.current_state);
        return;
    }

    const D3D12_RESOURCE_STATES first_state = image.subresource_states.front();
    const bool uniform_state =
        std::all_of(image.subresource_states.begin(),
                    image.subresource_states.end(),
                    [first_state](D3D12_RESOURCE_STATES state)
                    {
                        return state == first_state;
                    });
    if (uniform_state)
    {
        image.current_state = first_state;
    }
}

void initializeImageSubresourceStates(D3D12RHIImage& image, D3D12_RESOURCE_STATES initial_state)
{
    image.current_state = initial_state;
    image.subresource_states.assign(d3d12SubresourceCount(image), initial_state);
}

void ensureImageSubresourceStates(D3D12RHIImage& image)
{
    const uint32_t subresource_count = d3d12SubresourceCount(image);
    if (image.subresource_states.size() != subresource_count)
    {
        image.subresource_states.assign(subresource_count, image.current_state);
    }
}

bool transitionImageSubresource(ID3D12GraphicsCommandList* command_list,
                                D3D12RHIImage& image,
                                uint32_t subresource,
                                D3D12_RESOURCE_STATES target_state)
{
    if (command_list == nullptr || image.resource == nullptr)
    {
        return false;
    }

    ensureImageSubresourceStates(image);
    if (subresource >= image.subresource_states.size())
    {
        return false;
    }

    D3D12_RESOURCE_STATES& current_state = image.subresource_states[subresource];
    if (current_state == target_state)
    {
        syncImageCurrentState(image);
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = image.resource.Get();
    barrier.Transition.StateBefore = current_state;
    barrier.Transition.StateAfter  = target_state;
    barrier.Transition.Subresource = subresource;
    command_list->ResourceBarrier(1, &barrier);
    current_state = target_state;
    syncImageCurrentState(image);
    return true;
}

uint32_t normalizedSubresourceCount(uint32_t total_count, uint32_t base_index, uint32_t requested_count)
{
    if (base_index >= total_count)
    {
        return 0;
    }
    if (requested_count == 0 || requested_count == (std::numeric_limits<uint32_t>::max)())
    {
        return total_count - base_index;
    }
    return (std::min)(requested_count, total_count - base_index);
}

uint32_t transitionImageSubresourceRange(ID3D12GraphicsCommandList* command_list,
                                         D3D12RHIImage& image,
                                         uint32_t base_mip_level,
                                         uint32_t level_count,
                                         uint32_t base_array_layer,
                                         uint32_t layer_count,
                                         D3D12_RESOURCE_STATES target_state)
{
    const uint32_t mip_count = (std::max)(1U, image.mip_levels);
    const uint32_t array_count = (std::max)(1U, image.array_layers);
    const uint32_t normalized_level_count =
        normalizedSubresourceCount(mip_count, base_mip_level, level_count);
    const uint32_t normalized_layer_count =
        normalizedSubresourceCount(array_count, base_array_layer, layer_count);
    uint32_t transitioned_count = 0;
    for (uint32_t layer = 0; layer < normalized_layer_count; ++layer)
    {
        for (uint32_t mip = 0; mip < normalized_level_count; ++mip)
        {
            if (transitionImageSubresource(command_list,
                                           image,
                                           d3d12SubresourceIndex(image,
                                                                 base_mip_level + mip,
                                                                 base_array_layer + layer),
                                           target_state))
            {
                ++transitioned_count;
            }
        }
    }
    return transitioned_count;
}

uint32_t transitionImageSubresourceRange(ID3D12GraphicsCommandList* command_list,
                                         D3D12RHIImage& image,
                                         const RHIImageSubresourceRange& range,
                                         D3D12_RESOURCE_STATES target_state)
{
    return transitionImageSubresourceRange(command_list,
                                           image,
                                           range.baseMipLevel,
                                           range.levelCount,
                                           range.baseArrayLayer,
                                           range.layerCount,
                                           target_state);
}

bool imageSubresourceRangeInState(D3D12RHIImage& image,
                                  const RHIImageSubresourceRange& range,
                                  D3D12_RESOURCE_STATES state)
{
    ensureImageSubresourceStates(image);
    const uint32_t mip_count = (std::max)(1U, image.mip_levels);
    const uint32_t array_count = (std::max)(1U, image.array_layers);
    const uint32_t normalized_level_count =
        normalizedSubresourceCount(mip_count, range.baseMipLevel, range.levelCount);
    const uint32_t normalized_layer_count =
        normalizedSubresourceCount(array_count, range.baseArrayLayer, range.layerCount);
    for (uint32_t layer = 0; layer < normalized_layer_count; ++layer)
    {
        for (uint32_t mip = 0; mip < normalized_level_count; ++mip)
        {
            const uint32_t subresource =
                d3d12SubresourceIndex(image, range.baseMipLevel + mip, range.baseArrayLayer + layer);
            if (subresource >= image.subresource_states.size() ||
                image.subresource_states[subresource] != state)
            {
                return false;
            }
        }
    }
    return normalized_level_count > 0 && normalized_layer_count > 0;
}

bool isValidAttachmentIndex(uint32_t attachment_index)
{
    return attachment_index != RHI_SUBPASS_EXTERNAL;
}

D3D12_RESOURCE_STATES shaderReadableAttachmentState()
{
    return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}

D3D12_RESOURCE_STATES depthReadOnlyAttachmentState()
{
    return D3D12_RESOURCE_STATE_DEPTH_READ |
           shaderReadableAttachmentState();
}

D3D12_RESOURCE_STATES inputAttachmentState(const D3D12RHIImageView* view)
{
    if (view != nullptr && view->has_dsv)
    {
        return depthReadOnlyAttachmentState();
    }
    return shaderReadableAttachmentState();
}

D3D12_RESOURCE_STATES depthAttachmentState(const D3D12RHIImageView* view,
                                           RHIImageLayout layout,
                                           bool read_only)
{
    (void)view;
    if (read_only || isDepthReadOnlyLayout(layout) ||
        layout == RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        return depthReadOnlyAttachmentState();
    }
    return D3D12_RESOURCE_STATE_DEPTH_WRITE;
}

D3D12_RESOURCE_STATES subpassAttachmentState(const D3D12RHIImageView* view, RHIImageLayout layout)
{
    if (layout == RHI_IMAGE_LAYOUT_PRESENT_SRC_KHR)
    {
        return D3D12_RESOURCE_STATE_PRESENT;
    }
    if (layout == RHI_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        return D3D12_RESOURCE_STATE_COPY_DEST;
    }
    if (layout == RHI_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    if (view != nullptr && view->has_dsv)
    {
        if (isDepthReadOnlyLayout(layout) ||
            layout == RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            return depthReadOnlyAttachmentState();
        }
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
    if (layout == RHI_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR ||
        layout == RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    if (layout == RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        return shaderReadableAttachmentState();
    }
    return toD3D12ResourceState(layout);
}

D3D12RHIImageView* framebufferAttachment(D3D12RHIFramebuffer* framebuffer,
                                         uint32_t attachment_index)
{
    if (framebuffer == nullptr ||
        !isValidAttachmentIndex(attachment_index) ||
        attachment_index >= framebuffer->attachments.size())
    {
        return nullptr;
    }
    return framebuffer->attachments[attachment_index];
}

bool subpassPreservesAttachment(const D3D12RHIRenderPass::SubpassInfo& subpass,
                                uint32_t attachment_index)
{
    return std::find(subpass.preserve_attachment_indices.begin(),
                     subpass.preserve_attachment_indices.end(),
                     attachment_index) != subpass.preserve_attachment_indices.end();
}

bool subpassAttachmentStateForUse(D3D12RHIRenderPass* render_pass,
                                  D3D12RHIFramebuffer* framebuffer,
                                  uint32_t attachment_index,
                                  uint32_t subpass_index,
                                  D3D12_RESOURCE_STATES& state)
{
    if (render_pass == nullptr ||
        framebuffer == nullptr ||
        subpass_index >= render_pass->subpasses.size() ||
        !isValidAttachmentIndex(attachment_index))
    {
        return false;
    }

    D3D12RHIImageView* view = framebufferAttachment(framebuffer, attachment_index);
    const auto& subpass = render_pass->subpasses[subpass_index];
    for (uint32_t input_index = 0; input_index < subpass.input_attachment_indices.size(); ++input_index)
    {
        if (subpass.input_attachment_indices[input_index] == attachment_index)
        {
            state = inputAttachmentState(view);
            return true;
        }
    }

    for (uint32_t color_index = 0; color_index < subpass.color_attachment_indices.size(); ++color_index)
    {
        if (subpass.color_attachment_indices[color_index] == attachment_index)
        {
            state = D3D12_RESOURCE_STATE_RENDER_TARGET;
            return true;
        }
    }

    if (subpass.depth_attachment_index == attachment_index)
    {
        const bool depth_is_input =
            std::find(subpass.input_attachment_indices.begin(),
                      subpass.input_attachment_indices.end(),
                      attachment_index) != subpass.input_attachment_indices.end();
        state = depthAttachmentState(view, subpass.depth_attachment_layout, depth_is_input);
        return true;
    }

    for (uint32_t resolve_index = 0; resolve_index < subpass.resolve_attachment_indices.size(); ++resolve_index)
    {
        if (subpass.resolve_attachment_indices[resolve_index] == attachment_index)
        {
            const RHIImageLayout resolve_layout =
                resolve_index < subpass.resolve_attachment_layouts.size() ?
                    subpass.resolve_attachment_layouts[resolve_index] :
                    RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            state = subpassAttachmentState(view, resolve_layout);
            return true;
        }
    }

    return false;
}

void addUniqueAttachmentIndex(std::vector<uint32_t>& attachment_indices,
                              uint32_t attachment_index)
{
    if (!isValidAttachmentIndex(attachment_index) ||
        std::find(attachment_indices.begin(),
                  attachment_indices.end(),
                  attachment_index) != attachment_indices.end())
    {
        return;
    }
    attachment_indices.push_back(attachment_index);
}

void collectSubpassAttachmentIndices(const D3D12RHIRenderPass::SubpassInfo& subpass,
                                     std::vector<uint32_t>& attachment_indices)
{
    for (uint32_t attachment_index : subpass.input_attachment_indices)
    {
        addUniqueAttachmentIndex(attachment_indices, attachment_index);
    }
    for (uint32_t attachment_index : subpass.color_attachment_indices)
    {
        addUniqueAttachmentIndex(attachment_indices, attachment_index);
    }
    for (uint32_t attachment_index : subpass.resolve_attachment_indices)
    {
        addUniqueAttachmentIndex(attachment_indices, attachment_index);
    }
    addUniqueAttachmentIndex(attachment_indices, subpass.depth_attachment_index);
    for (uint32_t attachment_index : subpass.preserve_attachment_indices)
    {
        addUniqueAttachmentIndex(attachment_indices, attachment_index);
    }
}

D3D12_RESOURCE_STATES attachmentStateAfterSubpass(D3D12RHIRenderPass* render_pass,
                                                  D3D12RHIFramebuffer* framebuffer,
                                                  uint32_t attachment_index,
                                                  uint32_t subpass_index)
{
    if (render_pass == nullptr ||
        framebuffer == nullptr ||
        !isValidAttachmentIndex(attachment_index) ||
        attachment_index >= render_pass->attachments.size())
    {
        return D3D12_RESOURCE_STATE_COMMON;
    }

    for (uint32_t next_subpass = subpass_index + 1;
         next_subpass < render_pass->subpasses.size();
         ++next_subpass)
    {
        D3D12_RESOURCE_STATES next_state = D3D12_RESOURCE_STATE_COMMON;
        if (subpassAttachmentStateForUse(render_pass,
                                         framebuffer,
                                         attachment_index,
                                         next_subpass,
                                         next_state))
        {
            return next_state;
        }

        if (subpassPreservesAttachment(render_pass->subpasses[next_subpass],
                                       attachment_index))
        {
            continue;
        }
    }

    D3D12RHIImageView* view = framebufferAttachment(framebuffer, attachment_index);
    return subpassAttachmentState(view, render_pass->attachments[attachment_index].finalLayout);
}

void transitionImageView(ID3D12GraphicsCommandList* command_list,
                         D3D12RHIImageView* view,
                         D3D12_RESOURCE_STATES target_state)
{
    if (view == nullptr || view->image == nullptr || view->image->resource == nullptr)
    {
        return;
    }
    transitionImageSubresourceRange(command_list,
                                    *view->image,
                                    0,
                                    view->mip_levels,
                                    0,
                                    view->layer_count,
                                    target_state);
}

D3D12_RESOURCE_STATES attachmentStateForSubpassBoundary(D3D12RHIRenderPass* render_pass,
                                                        D3D12RHIFramebuffer* framebuffer,
                                                        uint32_t attachment_index,
                                                        uint32_t previous_subpass_index,
                                                        uint32_t next_subpass_index)
{
    D3D12_RESOURCE_STATES next_state = D3D12_RESOURCE_STATE_COMMON;
    if (subpassAttachmentStateForUse(render_pass,
                                     framebuffer,
                                     attachment_index,
                                     next_subpass_index,
                                     next_state))
    {
        return next_state;
    }

    if (render_pass != nullptr &&
        next_subpass_index < render_pass->subpasses.size() &&
        subpassPreservesAttachment(render_pass->subpasses[next_subpass_index],
                                   attachment_index))
    {
        return attachmentStateAfterSubpass(render_pass,
                                           framebuffer,
                                           attachment_index,
                                           previous_subpass_index);
    }

    return attachmentStateAfterSubpass(render_pass,
                                       framebuffer,
                                       attachment_index,
                                       previous_subpass_index);
}

bool hasSubpassDependency(const D3D12RHIRenderPass* render_pass,
                          uint32_t previous_subpass_index,
                          uint32_t next_subpass_index)
{
    if (render_pass == nullptr)
    {
        return false;
    }

    return std::any_of(render_pass->dependencies.begin(),
                       render_pass->dependencies.end(),
                       [previous_subpass_index, next_subpass_index](const RHISubpassDependency& dependency) {
                           return dependency.srcSubpass == previous_subpass_index &&
                                  dependency.dstSubpass == next_subpass_index;
                       });
}

void transitionD3D12SubpassBoundary(ID3D12GraphicsCommandList* command_list,
                                    D3D12RHIRenderPass* render_pass,
                                    D3D12RHIFramebuffer* framebuffer,
                                    uint32_t previous_subpass_index,
                                    uint32_t next_subpass_index)
{
    if (command_list == nullptr ||
        render_pass == nullptr ||
        framebuffer == nullptr ||
        previous_subpass_index >= render_pass->subpasses.size() ||
        next_subpass_index >= render_pass->subpasses.size())
    {
        return;
    }

    if (!hasSubpassDependency(render_pass, previous_subpass_index, next_subpass_index) &&
        next_subpass_index != previous_subpass_index + 1)
    {
        return;
    }

    std::vector<uint32_t> attachment_indices;
    collectSubpassAttachmentIndices(render_pass->subpasses[previous_subpass_index],
                                    attachment_indices);
    collectSubpassAttachmentIndices(render_pass->subpasses[next_subpass_index],
                                    attachment_indices);

    for (uint32_t attachment_index : attachment_indices)
    {
        if (attachment_index >= render_pass->attachments.size() ||
            attachment_index >= framebuffer->attachments.size())
        {
            continue;
        }

        D3D12RHIImageView* view = framebufferAttachment(framebuffer, attachment_index);
        if (view == nullptr || view->image == nullptr || view->image->resource == nullptr)
        {
            continue;
        }

        const D3D12_RESOURCE_STATES target_state =
            attachmentStateForSubpassBoundary(render_pass,
                                              framebuffer,
                                              attachment_index,
                                              previous_subpass_index,
                                              next_subpass_index);
        transitionImageView(command_list, view, target_state);
    }
}

void finishD3D12Subpass(ID3D12GraphicsCommandList* command_list,
                        D3D12RHIRenderPass* render_pass,
                        D3D12RHIFramebuffer* framebuffer,
                        uint32_t subpass_index)
{
    if (command_list == nullptr ||
        render_pass == nullptr ||
        framebuffer == nullptr ||
        subpass_index >= render_pass->subpasses.size())
    {
        return;
    }

    const D3D12RHIRenderPass::SubpassInfo& subpass = render_pass->subpasses[subpass_index];
    for (uint32_t color_slot = 0; color_slot < subpass.resolve_attachment_indices.size(); ++color_slot)
    {
        if (color_slot >= subpass.color_attachment_indices.size())
        {
            continue;
        }

        const uint32_t source_attachment_index  = subpass.color_attachment_indices[color_slot];
        const uint32_t resolve_attachment_index = subpass.resolve_attachment_indices[color_slot];
        if (!isValidAttachmentIndex(source_attachment_index) ||
            !isValidAttachmentIndex(resolve_attachment_index) ||
            source_attachment_index >= render_pass->attachments.size() ||
            resolve_attachment_index >= render_pass->attachments.size())
        {
            continue;
        }

        D3D12RHIImageView* source_view  = framebufferAttachment(framebuffer, source_attachment_index);
        D3D12RHIImageView* resolve_view = framebufferAttachment(framebuffer, resolve_attachment_index);
        if (source_view == nullptr ||
            resolve_view == nullptr ||
            source_view->image == nullptr ||
            resolve_view->image == nullptr ||
            source_view->image->resource == nullptr ||
            resolve_view->image->resource == nullptr ||
            source_view->image->resource.Get() == resolve_view->image->resource.Get())
        {
            continue;
        }

        DXGI_FORMAT resolve_format = toDXGIFormat(render_pass->attachments[resolve_attachment_index].format);
        if (resolve_format == DXGI_FORMAT_UNKNOWN)
        {
            resolve_format = resolve_view->dxgi_format != DXGI_FORMAT_UNKNOWN ?
                                 resolve_view->dxgi_format :
                                 source_view->dxgi_format;
        }
        if (resolve_format == DXGI_FORMAT_UNKNOWN)
        {
            continue;
        }

        bool wrote_resolve_attachment = false;
        const D3D12_RESOURCE_DESC source_desc  = source_view->image->resource->GetDesc();
        const D3D12_RESOURCE_DESC resolve_desc = resolve_view->image->resource->GetDesc();
        if (source_desc.SampleDesc.Count > 1 && resolve_desc.SampleDesc.Count == 1)
        {
            transitionImageView(command_list, source_view, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            transitionImageView(command_list, resolve_view, D3D12_RESOURCE_STATE_RESOLVE_DEST);
            command_list->ResolveSubresource(resolve_view->image->resource.Get(),
                                             0,
                                             source_view->image->resource.Get(),
                                             0,
                                             resolve_format);
            wrote_resolve_attachment = true;
        }
        else if (source_desc.SampleDesc.Count == resolve_desc.SampleDesc.Count)
        {
            transitionImageView(command_list, source_view, D3D12_RESOURCE_STATE_COPY_SOURCE);
            transitionImageView(command_list, resolve_view, D3D12_RESOURCE_STATE_COPY_DEST);

            D3D12_TEXTURE_COPY_LOCATION source_location {};
            source_location.pResource        = source_view->image->resource.Get();
            source_location.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source_location.SubresourceIndex = d3d12SubresourceIndex(*source_view->image, 0, 0);

            D3D12_TEXTURE_COPY_LOCATION resolve_location {};
            resolve_location.pResource        = resolve_view->image->resource.Get();
            resolve_location.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            resolve_location.SubresourceIndex = d3d12SubresourceIndex(*resolve_view->image, 0, 0);

            command_list->CopyTextureRegion(&resolve_location, 0, 0, 0, &source_location, nullptr);
            wrote_resolve_attachment = true;
        }

        if (wrote_resolve_attachment)
        {
            transitionImageView(command_list,
                                source_view,
                                attachmentStateAfterSubpass(render_pass,
                                                           framebuffer,
                                                           source_attachment_index,
                                                           subpass_index));
            transitionImageView(command_list,
                                resolve_view,
                                attachmentStateAfterSubpass(render_pass,
                                                           framebuffer,
                                                           resolve_attachment_index,
                                                           subpass_index));
        }
    }
}

bool recordHostDataUpload(ID3D12Device* device,
                          ID3D12GraphicsCommandList* command_list,
                          std::vector<ComPtr<ID3D12Resource>>& pending_uploads,
                          D3D12RHIBuffer& buffer)
{
    if (device == nullptr ||
        command_list == nullptr ||
        buffer.resource == nullptr ||
        buffer.heap_type != D3D12_HEAP_TYPE_DEFAULT ||
        buffer.host_data.empty() ||
        !bufferHostMirrorUploadable(buffer) ||
        !hasFlag(buffer.memory_properties, RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
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
    upload_desc.Width              = (std::max)(static_cast<RHIDeviceSize>(1), buffer.size);
    upload_desc.Height             = 1;
    upload_desc.DepthOrArraySize   = 1;
    upload_desc.MipLevels          = 1;
    upload_desc.Format             = DXGI_FORMAT_UNKNOWN;
    upload_desc.SampleDesc.Count   = 1;
    upload_desc.SampleDesc.Quality = 0;
    upload_desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    upload_desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> upload_buffer;
    if (FAILED(device->CreateCommittedResource(&upload_heap_properties,
                                               D3D12_HEAP_FLAG_NONE,
                                               &upload_desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ,
                                               nullptr,
                                               IID_PPV_ARGS(&upload_buffer))))
    {
        return false;
    }

    D3D12_RANGE read_range {0, 0};
    void* mapped_data = nullptr;
    if (FAILED(upload_buffer->Map(0, &read_range, &mapped_data)) || mapped_data == nullptr)
    {
        return false;
    }
    std::memcpy(mapped_data,
                buffer.host_data.data(),
                (std::min)(static_cast<size_t>(buffer.size), buffer.host_data.size()));
    upload_buffer->Unmap(0, nullptr);

    const D3D12_RESOURCE_STATES previous_state = buffer.current_state;
    transitionResource(command_list, buffer.resource.Get(), buffer.current_state, D3D12_RESOURCE_STATE_COPY_DEST);
    buffer.host_data_valid = false;
    buffer.host_data_uploadable = false;
    command_list->CopyBufferRegion(buffer.resource.Get(), 0, upload_buffer.Get(), 0, buffer.size);
    transitionResource(command_list, buffer.resource.Get(), buffer.current_state, previous_state);
    pending_uploads.push_back(upload_buffer);
    buffer.host_data_valid = true;
    buffer.host_data_uploadable = false;
    return true;
}

bool ensureDispatchArgumentScratchBuffer(ID3D12Device* device, D3D12RHICommandBuffer& command_buffer)
{
    if (command_buffer.dispatch_argument_buffer != nullptr)
    {
        return true;
    }
    if (device == nullptr)
    {
        return false;
    }

    D3D12_HEAP_PROPERTIES heap_properties {};
    heap_properties.Type                 = D3D12_HEAP_TYPE_DEFAULT;
    heap_properties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_properties.CreationNodeMask     = 1;
    heap_properties.VisibleNodeMask      = 1;

    D3D12_RESOURCE_DESC resource_desc {};
    resource_desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width              = sizeof(D3D12_DISPATCH_ARGUMENTS);
    resource_desc.Height             = 1;
    resource_desc.DepthOrArraySize   = 1;
    resource_desc.MipLevels          = 1;
    resource_desc.Format             = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count   = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    command_buffer.dispatch_argument_buffer_state = D3D12_RESOURCE_STATE_COMMON;
    return SUCCEEDED(device->CreateCommittedResource(&heap_properties,
                                                     D3D12_HEAP_FLAG_NONE,
                                                     &resource_desc,
                                                     D3D12_RESOURCE_STATE_COMMON,
                                                     nullptr,
                                                     IID_PPV_ARGS(&command_buffer.dispatch_argument_buffer)));
}

void fillSamplerDesc(const RHISamplerCreateInfo& create_info, D3D12_SAMPLER_DESC& desc)
{
    desc.Filter         = toD3D12Filter(create_info);
    desc.AddressU       = toD3D12AddressMode(create_info.addressModeU);
    desc.AddressV       = toD3D12AddressMode(create_info.addressModeV);
    desc.AddressW       = toD3D12AddressMode(create_info.addressModeW);
    desc.MipLODBias     = create_info.mipLodBias;
    desc.MaxAnisotropy  = create_info.anisotropyEnable ? static_cast<UINT>((std::max)(1.0f, create_info.maxAnisotropy)) : 1;
    desc.ComparisonFunc = create_info.compareEnable ? toD3D12ComparisonFunc(create_info.compareOp) :
                                                       D3D12_COMPARISON_FUNC_ALWAYS;
    setD3D12BorderColor(create_info.borderColor, desc.BorderColor);
    desc.MinLOD = create_info.minLod;
    desc.MaxLOD = create_info.maxLod;
}

D3D12_PRIMITIVE_TOPOLOGY toD3D12PrimitiveTopology(RHIPrimitiveTopology topology)
{
    switch (topology)
    {
        case RHI_PRIMITIVE_TOPOLOGY_POINT_LIST:
            return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        case RHI_PRIMITIVE_TOPOLOGY_LINE_LIST:
            return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case RHI_PRIMITIVE_TOPOLOGY_LINE_STRIP:
            return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
        default:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

D3D12_PRIMITIVE_TOPOLOGY_TYPE toD3D12PrimitiveTopologyType(RHIPrimitiveTopology topology)
{
    switch (topology)
    {
        case RHI_PRIMITIVE_TOPOLOGY_POINT_LIST:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        case RHI_PRIMITIVE_TOPOLOGY_LINE_LIST:
        case RHI_PRIMITIVE_TOPOLOGY_LINE_STRIP:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case RHI_PRIMITIVE_TOPOLOGY_PATCH_LIST:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
        case RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
        case RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
        default:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
}

D3D12_FILL_MODE toD3D12FillMode(RHIPolygonMode polygon_mode)
{
    return polygon_mode == RHI_POLYGON_MODE_LINE ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
}

D3D12_CULL_MODE toD3D12CullMode(RHICullModeFlags cull_mode)
{
    if (hasFlag(cull_mode, RHI_CULL_MODE_FRONT_BIT))
    {
        return D3D12_CULL_MODE_FRONT;
    }
    if (hasFlag(cull_mode, RHI_CULL_MODE_BACK_BIT))
    {
        return D3D12_CULL_MODE_BACK;
    }
    return D3D12_CULL_MODE_NONE;
}

D3D12_BLEND toD3D12Blend(RHIBlendFactor factor)
{
    switch (factor)
    {
        case RHI_BLEND_FACTOR_ZERO:
            return D3D12_BLEND_ZERO;
        case RHI_BLEND_FACTOR_ONE:
            return D3D12_BLEND_ONE;
        case RHI_BLEND_FACTOR_SRC_COLOR:
            return D3D12_BLEND_SRC_COLOR;
        case RHI_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
            return D3D12_BLEND_INV_SRC_COLOR;
        case RHI_BLEND_FACTOR_DST_COLOR:
            return D3D12_BLEND_DEST_COLOR;
        case RHI_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
            return D3D12_BLEND_INV_DEST_COLOR;
        case RHI_BLEND_FACTOR_SRC_ALPHA:
            return D3D12_BLEND_SRC_ALPHA;
        case RHI_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
            return D3D12_BLEND_INV_SRC_ALPHA;
        case RHI_BLEND_FACTOR_DST_ALPHA:
            return D3D12_BLEND_DEST_ALPHA;
        case RHI_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
            return D3D12_BLEND_INV_DEST_ALPHA;
        case RHI_BLEND_FACTOR_CONSTANT_COLOR:
        case RHI_BLEND_FACTOR_CONSTANT_ALPHA:
            return D3D12_BLEND_BLEND_FACTOR;
        case RHI_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
        case RHI_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
            return D3D12_BLEND_INV_BLEND_FACTOR;
        case RHI_BLEND_FACTOR_SRC_ALPHA_SATURATE:
            return D3D12_BLEND_SRC_ALPHA_SAT;
        default:
            return D3D12_BLEND_ONE;
    }
}

D3D12_BLEND_OP toD3D12BlendOp(RHIBlendOp op)
{
    switch (op)
    {
        case RHI_BLEND_OP_SUBTRACT:
            return D3D12_BLEND_OP_SUBTRACT;
        case RHI_BLEND_OP_REVERSE_SUBTRACT:
            return D3D12_BLEND_OP_REV_SUBTRACT;
        case RHI_BLEND_OP_MIN:
            return D3D12_BLEND_OP_MIN;
        case RHI_BLEND_OP_MAX:
            return D3D12_BLEND_OP_MAX;
        case RHI_BLEND_OP_ADD:
        default:
            return D3D12_BLEND_OP_ADD;
    }
}

UINT8 toD3D12ColorWriteMask(RHIColorComponentFlags flags)
{
    UINT8 mask = 0;
    if (hasFlag(flags, RHI_COLOR_COMPONENT_R_BIT))
    {
        mask |= D3D12_COLOR_WRITE_ENABLE_RED;
    }
    if (hasFlag(flags, RHI_COLOR_COMPONENT_G_BIT))
    {
        mask |= D3D12_COLOR_WRITE_ENABLE_GREEN;
    }
    if (hasFlag(flags, RHI_COLOR_COMPONENT_B_BIT))
    {
        mask |= D3D12_COLOR_WRITE_ENABLE_BLUE;
    }
    if (hasFlag(flags, RHI_COLOR_COMPONENT_A_BIT))
    {
        mask |= D3D12_COLOR_WRITE_ENABLE_ALPHA;
    }
    return mask == 0 ? D3D12_COLOR_WRITE_ENABLE_ALL : mask;
}

D3D12_STENCIL_OP toD3D12StencilOp(RHIStencilOp op)
{
    switch (op)
    {
        case RHI_STENCIL_OP_ZERO:
            return D3D12_STENCIL_OP_ZERO;
        case RHI_STENCIL_OP_REPLACE:
            return D3D12_STENCIL_OP_REPLACE;
        case RHI_STENCIL_OP_INCREMENT_AND_CLAMP:
            return D3D12_STENCIL_OP_INCR_SAT;
        case RHI_STENCIL_OP_DECREMENT_AND_CLAMP:
            return D3D12_STENCIL_OP_DECR_SAT;
        case RHI_STENCIL_OP_INVERT:
            return D3D12_STENCIL_OP_INVERT;
        case RHI_STENCIL_OP_INCREMENT_AND_WRAP:
            return D3D12_STENCIL_OP_INCR;
        case RHI_STENCIL_OP_DECREMENT_AND_WRAP:
            return D3D12_STENCIL_OP_DECR;
        case RHI_STENCIL_OP_KEEP:
        default:
            return D3D12_STENCIL_OP_KEEP;
    }
}

D3D12_DEPTH_STENCILOP_DESC toD3D12StencilOpDesc(const RHIStencilOpState& state)
{
    D3D12_DEPTH_STENCILOP_DESC desc {};
    desc.StencilFailOp      = toD3D12StencilOp(state.failOp);
    desc.StencilDepthFailOp = toD3D12StencilOp(state.depthFailOp);
    desc.StencilPassOp      = toD3D12StencilOp(state.passOp);
    desc.StencilFunc        = toD3D12ComparisonFunc(state.compareOp);
    return desc;
}

UINT sampleCount(RHISampleCountFlagBits sample_count)
{
    switch (sample_count)
    {
        case RHI_SAMPLE_COUNT_2_BIT:
            return 2;
        case RHI_SAMPLE_COUNT_4_BIT:
            return 4;
        case RHI_SAMPLE_COUNT_8_BIT:
            return 8;
        case RHI_SAMPLE_COUNT_16_BIT:
            return 16;
        case RHI_SAMPLE_COUNT_32_BIT:
            return 32;
        case RHI_SAMPLE_COUNT_64_BIT:
            return 64;
        case RHI_SAMPLE_COUNT_1_BIT:
        default:
            return 1;
    }
}

const char* semanticNameForLocation(uint32_t location)
{
    switch (location)
    {
        case 0:
            return "POSITION";
        case 1:
            return "NORMAL";
        case 2:
            return "TANGENT";
        case 3:
            return "TEXCOORD";
        default:
            return "TEXCOORD";
    }
}

UINT semanticIndexForLocation(uint32_t location)
{
    return location <= 3 ? 0 : location - 3;
}

DXGI_FORMAT indexFormat(RHIIndexType index_type)
{
    return index_type == RHI_INDEX_TYPE_UINT16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
}

#if PICCOLO_D3D12_HAS_DXR
const wchar_t* rayTracingExportOrDefault(const wchar_t* export_name, const wchar_t* default_export)
{
    return export_name != nullptr ? export_name : default_export;
}

D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS
rayTracingBuildFlags(const RHIAccelerationStructureBuildDesc& build_desc)
{
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags =
        build_desc.prefer_fast_trace ?
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE :
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    if (build_desc.allow_update)
    {
        flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    }
    if (build_desc.perform_update)
    {
        flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    }
    return flags;
}

bool fillRayTracingBuildInputs(const RHIAccelerationStructureBuildDesc& build_desc,
                               std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& geometries,
                               D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs)
{
    inputs = {};
    inputs.Type = build_desc.type == RHIAccelerationStructureType::BottomLevel ?
                      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL :
                      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = rayTracingBuildFlags(build_desc);

    if (build_desc.type == RHIAccelerationStructureType::BottomLevel)
    {
        if (build_desc.geometry_count == 0 || build_desc.geometries == nullptr)
        {
            return false;
        }

        geometries.resize(build_desc.geometry_count);
        for (uint32_t geometry_index = 0; geometry_index < build_desc.geometry_count; ++geometry_index)
        {
            const auto& rhi_geometry = build_desc.geometries[geometry_index];
            auto* vertex_buffer = static_cast<D3D12RHIBuffer*>(rhi_geometry.vertex_position_buffer);
            auto* index_buffer = static_cast<D3D12RHIBuffer*>(rhi_geometry.index_buffer);
            if (vertex_buffer == nullptr ||
                vertex_buffer->resource == nullptr ||
                rhi_geometry.vertex_count == 0 ||
                rhi_geometry.vertex_stride == 0)
            {
                return false;
            }
            if (rhi_geometry.index_count > 0 && (index_buffer == nullptr || index_buffer->resource == nullptr))
            {
                return false;
            }

            D3D12_RAYTRACING_GEOMETRY_DESC& geometry = geometries[geometry_index];
            geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            geometry.Flags = rhi_geometry.opaque ?
                                 D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE :
                                 D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
            geometry.Triangles.Transform3x4 = 0;
            geometry.Triangles.IndexFormat = rhi_geometry.index_count > 0 ?
                                                 indexFormat(rhi_geometry.index_type) :
                                                 DXGI_FORMAT_UNKNOWN;
            geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            geometry.Triangles.IndexCount = rhi_geometry.index_count;
            geometry.Triangles.VertexCount = rhi_geometry.vertex_count;
            geometry.Triangles.IndexBuffer = rhi_geometry.index_count > 0 ?
                                                 index_buffer->resource->GetGPUVirtualAddress() +
                                                     rhi_geometry.index_offset :
                                                 0;
            geometry.Triangles.VertexBuffer.StartAddress =
                vertex_buffer->resource->GetGPUVirtualAddress() + rhi_geometry.vertex_position_offset;
            geometry.Triangles.VertexBuffer.StrideInBytes = rhi_geometry.vertex_stride;
        }

        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.NumDescs = static_cast<UINT>(geometries.size());
        inputs.pGeometryDescs = geometries.data();
        return true;
    }

    if (build_desc.instance_count == 0)
    {
        return false;
    }

    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = build_desc.instance_count;
    inputs.InstanceDescs = 0;
    return true;
}

bool createRayTracingBuffer(ID3D12Device* device,
                            uint64_t size,
                            D3D12_RESOURCE_STATES initial_state,
                            D3D12_RESOURCE_FLAGS flags,
                            ID3D12Resource** resource)
{
    if (device == nullptr || resource == nullptr || size == 0)
    {
        return false;
    }

    D3D12_HEAP_PROPERTIES heap_properties {};
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_properties.CreationNodeMask = 1;
    heap_properties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resource_desc {};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = alignUp(size, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = flags;

    return SUCCEEDED(device->CreateCommittedResource(&heap_properties,
                                                     D3D12_HEAP_FLAG_NONE,
                                                     &resource_desc,
                                                     initial_state,
                                                     nullptr,
                                                     __uuidof(ID3D12Resource),
                                                     reinterpret_cast<void**>(resource)));
}

bool createUploadBuffer(ID3D12Device* device,
                        uint64_t size,
                        ID3D12Resource** resource)
{
    if (device == nullptr || resource == nullptr || size == 0)
    {
        return false;
    }

    D3D12_HEAP_PROPERTIES heap_properties {};
    heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap_properties.CreationNodeMask = 1;
    heap_properties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resource_desc {};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = size;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    return SUCCEEDED(device->CreateCommittedResource(&heap_properties,
                                                     D3D12_HEAP_FLAG_NONE,
                                                     &resource_desc,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ,
                                                     nullptr,
                                                     __uuidof(ID3D12Resource),
                                                     reinterpret_cast<void**>(resource)));
}
#endif

DXGI_FORMAT toVertexDXGIFormat(RHIFormat format)
{
    switch (format)
    {
        case RHI_FORMAT_R32_SFLOAT:
            return DXGI_FORMAT_R32_FLOAT;
        case RHI_FORMAT_R32G32_SFLOAT:
            return DXGI_FORMAT_R32G32_FLOAT;
        case RHI_FORMAT_R32G32B32_SFLOAT:
            return DXGI_FORMAT_R32G32B32_FLOAT;
        case RHI_FORMAT_R32G32B32A32_SFLOAT:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case RHI_FORMAT_R32_UINT:
            return DXGI_FORMAT_R32_UINT;
        default:
            return toDXGIFormat(format);
    }
}

UINT formatByteSize(RHIFormat format)
{
    return resourceBytesPerPixel(format);
}

void endGraphicsBindingScope(D3D12RHICommandBuffer& command_buffer)
{
    command_buffer.graphics_binding_scope = {};
    command_buffer.bound_graphics_root_signature = nullptr;
    command_buffer.graphics_root_signature_dirty = true;
    clearRootDescriptorTableCache(command_buffer, RHI_PIPELINE_BIND_POINT_GRAPHICS);
}

void beginGraphicsBindingScope(D3D12RHICommandBuffer& command_buffer, D3D12RHIPipeline& pipeline)
{
    auto& scope = command_buffer.graphics_binding_scope;
    scope.valid         = true;
    scope.pipeline      = &pipeline;
    scope.layout        = pipeline.layout;
    scope.render_pass   = pipeline.graphics_render_pass;
    scope.subpass_index = pipeline.graphics_subpass_index;
    scope.vertex_strides = pipeline.vertex_strides;
}

void validateGraphicsPipelineBindContract(const D3D12RHICommandBuffer& command_buffer,
                                          const D3D12RHIPipeline& pipeline)
{
    if (pipeline.graphics_render_pass == nullptr || !command_buffer.in_render_pass)
    {
        return;
    }
    if (command_buffer.active_render_pass != pipeline.graphics_render_pass)
    {
        LOG_WARN("D3D12 graphics pipeline bind: render pass mismatch (active={}, pipeline={})",
                 static_cast<const void*>(command_buffer.active_render_pass),
                 static_cast<const void*>(pipeline.graphics_render_pass));
        return;
    }
    if (command_buffer.active_subpass_index != pipeline.graphics_subpass_index)
    {
        LOG_ERROR("D3D12 graphics pipeline bind: subpass mismatch active={} pipeline={}. "
                  "Call cmdNextSubpass before cmdBindPipeline.",
                  command_buffer.active_subpass_index,
                  pipeline.graphics_subpass_index);
    }
}

void validateGraphicsBindingScopeForDraw(const D3D12RHICommandBuffer& command_buffer)
{
    if (!command_buffer.in_render_pass)
    {
        return;
    }
    const auto& scope = command_buffer.graphics_binding_scope;
    if (!scope.valid)
    {
        LOG_ERROR("D3D12 draw inside render pass without active graphics binding scope. "
                  "Call cmdBindPipeline before draw.");
        return;
    }
    if (scope.render_pass != nullptr &&
        (command_buffer.active_render_pass != scope.render_pass ||
         command_buffer.active_subpass_index != scope.subpass_index))
    {
        LOG_ERROR("D3D12 draw with stale graphics binding scope: active=(rp={}, subpass={}) "
                  "scope=(rp={}, subpass={}). Subpass advanced without rebinding pipeline.",
                  static_cast<const void*>(command_buffer.active_render_pass),
                  command_buffer.active_subpass_index,
                  static_cast<const void*>(scope.render_pass),
                  scope.subpass_index);
    }
}

void resetCommandBufferRecordingState(D3D12RHICommandBuffer& command_buffer, uint32_t transient_descriptor_next)
{
    command_buffer.is_open                   = true;
    command_buffer.has_recorded_commands     = false;
    command_buffer.in_render_pass            = false;
    endGraphicsBindingScope(command_buffer);
    command_buffer.bound_compute_pipeline_layout     = nullptr;
    command_buffer.bound_ray_tracing_pipeline_layout = nullptr;
    command_buffer.bound_compute_root_signature    = nullptr;
    command_buffer.bound_ray_tracing_root_signature = nullptr;
    command_buffer.active_render_pass      = nullptr;
    command_buffer.active_framebuffer      = nullptr;
    command_buffer.active_render_pass_begin_info = {};
    command_buffer.active_clear_values.clear();
    command_buffer.attachment_load_ops_applied.clear();
    command_buffer.active_subpass_index    = 0;
    command_buffer.transient_cbv_srv_uav_descriptor_next = transient_descriptor_next;
    command_buffer.dynamic_descriptor_table_cache.clear();
    resetCommandBufferDescriptorHeapState(command_buffer);
}
#endif
} // namespace d3d12_detail
} // namespace Piccolo
