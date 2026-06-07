#include "runtime/function/render/interface/d3d12/d3d12_rhi.h"

#include "runtime/function/render/window_system.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <d3dcompiler.h>
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
            RHIDeviceSize          size {0};
            RHIBufferUsageFlags    usage {0};
            RHIMemoryPropertyFlags memory_properties {0};
            D3D12_HEAP_TYPE        heap_type {D3D12_HEAP_TYPE_DEFAULT};
            D3D12_RESOURCE_STATES  current_state {D3D12_RESOURCE_STATE_COMMON};
            std::vector<uint8_t>   host_data;
            bool                   map_host_data {false};
        };

        struct D3D12RHIImage final : RHIImage
        {
            ComPtr<ID3D12Resource> resource;
            uint32_t               width {0};
            uint32_t               height {0};
            uint32_t               array_layers {1};
            uint32_t               mip_levels {1};
            RHIFormat              format {RHI_FORMAT_UNDEFINED};
            DXGI_FORMAT            dxgi_format {DXGI_FORMAT_UNKNOWN};
            RHIImageUsageFlags     usage {0};
            RHIImageCreateFlags    create_flags {0};
            RHIImageTiling         tiling {RHI_IMAGE_TILING_OPTIMAL};
            RHIMemoryPropertyFlags memory_properties {0};
            D3D12_RESOURCE_STATES  current_state {D3D12_RESOURCE_STATE_COMMON};
            uint32_t               source_bytes_per_pixel {0};
            uint32_t               resource_bytes_per_pixel {0};
        };

        struct D3D12RHIImageView final : RHIImageView
        {
            D3D12RHIImage*                    image {nullptr};
            RHIFormat                         format {RHI_FORMAT_UNDEFINED};
            DXGI_FORMAT                       dxgi_format {DXGI_FORMAT_UNKNOWN};
            RHIImageAspectFlags               aspect_flags {0};
            RHIImageViewType                  view_type {RHI_IMAGE_VIEW_TYPE_2D};
            uint32_t                          layer_count {1};
            uint32_t                          mip_levels {1};
            D3D12_CPU_DESCRIPTOR_HANDLE       cpu_descriptor {0};
            D3D12_CPU_DESCRIPTOR_HANDLE       read_only_dsv_cpu_descriptor {0};
            D3D12_DESCRIPTOR_HEAP_TYPE        descriptor_heap_type {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV};
            D3D12_SHADER_RESOURCE_VIEW_DESC   srv_desc {};
            D3D12_RENDER_TARGET_VIEW_DESC     rtv_desc {};
            D3D12_DEPTH_STENCIL_VIEW_DESC     dsv_desc {};
            D3D12_DEPTH_STENCIL_VIEW_DESC     read_only_dsv_desc {};
            D3D12_UNORDERED_ACCESS_VIEW_DESC  uav_desc {};
            bool                              has_srv {false};
            bool                              has_rtv {false};
            bool                              has_dsv {false};
            bool                              has_read_only_dsv {false};
            bool                              has_uav {false};
        };

        struct D3D12RHISampler final : RHISampler
        {
            RHISamplerCreateInfo create_info {};
            D3D12_SAMPLER_DESC   desc {};
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor {0};
        };

        struct D3D12RHIShader final : RHIShader
        {
            std::vector<unsigned char> bytecode_storage;
            D3D12_SHADER_BYTECODE      bytecode {};
        };

        struct D3D12RHIDeviceMemory final : RHIDeviceMemory
        {
            D3D12RHIBuffer* owner_buffer {nullptr};
            D3D12RHIImage*  owner_image {nullptr};
            void*           mapped_ptr {nullptr};
            RHIDeviceSize   mapped_offset {0};
            RHIDeviceSize   mapped_size {0};
            bool            mapped_resource {false};
        };
#else
        struct D3D12RHIBuffer final : RHIBuffer
        {
            RHIDeviceSize        size {0};
            RHIBufferUsageFlags  usage {0};
            std::vector<uint8_t> host_data;
            bool                 map_host_data {false};
        };

        struct D3D12RHIImage final : RHIImage
        {
            uint32_t           width {0};
            uint32_t           height {0};
            uint32_t           array_layers {1};
            uint32_t           mip_levels {1};
            RHIFormat          format {RHI_FORMAT_UNDEFINED};
            RHIImageUsageFlags usage {0};
        };

        struct D3D12RHIImageView final : RHIImageView
        {
            D3D12RHIImage*      image {nullptr};
            RHIFormat           format {RHI_FORMAT_UNDEFINED};
            RHIImageAspectFlags aspect_flags {0};
            RHIImageViewType    view_type {RHI_IMAGE_VIEW_TYPE_2D};
            uint32_t            layer_count {1};
            uint32_t            mip_levels {1};
        };

        struct D3D12RHISampler final : RHISampler
        {
            RHISamplerCreateInfo create_info {};
        };

        struct D3D12RHIShader final : RHIShader
        {
            std::vector<unsigned char> bytecode_storage;
        };

        struct D3D12RHIDeviceMemory final : RHIDeviceMemory
        {
            D3D12RHIBuffer* owner_buffer {nullptr};
            D3D12RHIImage*  owner_image {nullptr};
            void*           mapped_ptr {nullptr};
            bool            mapped_resource {false};
        };
#endif

        struct D3D12RHICommandBuffer final : RHICommandBuffer
        {
            bool owns_recording {false};
        };

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

        struct D3D12RHIDescriptorPool final : RHIDescriptorPool
        {
            bool enforce_limits {false};
            uint32_t max_sets {0};
            uint32_t allocated_sets {0};
            std::array<uint32_t, 11> descriptor_type_counts {};
            std::array<uint32_t, 11> allocated_descriptor_type_counts {};
            uint32_t cbv_srv_uav_descriptor_count {0};
            uint32_t allocated_cbv_srv_uav_descriptors {0};
            uint32_t sampler_descriptor_count {0};
            uint32_t allocated_sampler_descriptors {0};
        };

        struct D3D12RHIDescriptorSetLayout final : RHIDescriptorSetLayout
        {
            struct BindingRange
            {
                RHIDescriptorSetLayoutBinding binding {};
                uint32_t cbv_srv_uav_offset {0};
                uint32_t sampler_offset {0};
#ifdef _WIN32
                D3D12_DESCRIPTOR_RANGE_TYPE cbv_srv_uav_range_type {D3D12_DESCRIPTOR_RANGE_TYPE_SRV};
#endif
            };

            std::vector<BindingRange> ranges;
            std::array<uint32_t, 11> descriptor_type_counts {};
            uint32_t cbv_srv_uav_descriptor_count {0};
            uint32_t sampler_descriptor_count {0};

            const BindingRange* find(uint32_t binding) const
            {
                for (const auto& range : ranges)
                {
                    if (range.binding.binding == binding)
                    {
                        return &range;
                    }
                }
                return nullptr;
            }
        };

        struct D3D12RHIDescriptorSet final : RHIDescriptorSet
        {
            struct BufferDescriptor
            {
                uint32_t binding {0};
                uint32_t array_element {0};
                RHIDescriptorType descriptor_type {RHI_DESCRIPTOR_TYPE_MAX_ENUM};
                D3D12RHIBuffer* buffer {nullptr};
                RHIDeviceSize offset {0};
                RHIDeviceSize range {0};
#ifdef _WIN32
                D3D12_DESCRIPTOR_RANGE_TYPE range_type {D3D12_DESCRIPTOR_RANGE_TYPE_SRV};
#endif
            };

            D3D12RHIDescriptorSetLayout* layout {nullptr};
            uint32_t cbv_srv_uav_base {0};
            uint32_t sampler_base {0};
            bool has_cbv_srv_uav_descriptors {false};
            bool has_sampler_descriptors {false};
#ifdef _WIN32
            D3D12_CPU_DESCRIPTOR_HANDLE cbv_srv_uav_cpu_base {0};
            D3D12_GPU_DESCRIPTOR_HANDLE cbv_srv_uav_gpu_base {0};
            D3D12_CPU_DESCRIPTOR_HANDLE sampler_cpu_base {0};
            D3D12_GPU_DESCRIPTOR_HANDLE sampler_gpu_base {0};
#endif
            std::vector<D3D12RHIBuffer*> storage_buffers;
            std::vector<D3D12RHIBuffer*> host_visible_default_buffers;
            std::vector<BufferDescriptor> buffer_descriptors;

            BufferDescriptor* findBufferDescriptor(uint32_t binding, uint32_t array_element)
            {
                for (auto& descriptor : buffer_descriptors)
                {
                    if (descriptor.binding == binding && descriptor.array_element == array_element)
                    {
                        return &descriptor;
                    }
                }
                return nullptr;
            }

            const BufferDescriptor* findBufferDescriptor(uint32_t binding, uint32_t array_element) const
            {
                for (const auto& descriptor : buffer_descriptors)
                {
                    if (descriptor.binding == binding && descriptor.array_element == array_element)
                    {
                        return &descriptor;
                    }
                }
                return nullptr;
            }
        };

        struct D3D12RHIPipelineLayout final : RHIPipelineLayout
        {
            std::vector<D3D12RHIDescriptorSetLayout*> set_layouts;
            std::vector<uint32_t> cbv_srv_uav_root_parameter_indices;
            std::vector<uint32_t> sampler_root_parameter_indices;
#ifdef _WIN32
            ComPtr<ID3D12RootSignature> root_signature;
#endif
        };

        struct D3D12RHIRenderPass final : RHIRenderPass
        {
            struct SubpassInfo
            {
                std::vector<uint32_t> input_attachment_indices;
                std::vector<RHIImageLayout> input_attachment_layouts;
                std::vector<uint32_t> color_attachment_indices;
                uint32_t depth_attachment_index {(std::numeric_limits<uint32_t>::max)()};
                RHIImageLayout depth_attachment_layout {RHI_IMAGE_LAYOUT_UNDEFINED};
            };

            std::vector<RHIAttachmentDescription> attachments;
            std::vector<uint32_t> color_attachment_indices;
            uint32_t depth_attachment_index {(std::numeric_limits<uint32_t>::max)()};
            std::vector<SubpassInfo> subpasses;
        };

        struct D3D12RHIFramebuffer final : RHIFramebuffer
        {
            D3D12RHIRenderPass* render_pass {nullptr};
            std::vector<D3D12RHIImageView*> attachments;
            uint32_t width {0};
            uint32_t height {0};
            uint32_t layers {1};
        };

        struct D3D12RHIPipeline final : RHIPipeline
        {
            RHIPipelineBindPoint bind_point {RHI_PIPELINE_BIND_POINT_GRAPHICS};
            D3D12RHIPipelineLayout* layout {nullptr};
            std::vector<uint32_t> vertex_strides;
#ifdef _WIN32
            ComPtr<ID3D12PipelineState> pipeline_state;
            D3D_PRIMITIVE_TOPOLOGY primitive_topology {D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST};
#endif
        };

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
                   type == RHI_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        }

        bool descriptorUsesBufferInfo(RHIDescriptorType type)
        {
            return type == RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                   type == RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
                   type == RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                   type == RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        }

        constexpr uint32_t kTrackedDescriptorTypeCount = 11;

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
            return static_cast<uint32_t>(type);
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
        constexpr uint32_t kInvalidRootParameterIndex = (std::numeric_limits<uint32_t>::max)();

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
                   type == RHI_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
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

        D3D12_DESCRIPTOR_RANGE_TYPE toDescriptorRangeType(const RHIDescriptorSetLayoutBinding& binding)
        {
            switch (binding.descriptorType)
            {
                case RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                case RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                    return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                case RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                    return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
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
            if (!descriptorUsesBufferInfo(src_binding.binding.descriptorType))
            {
                return true;
            }

            for (uint32_t descriptor_index = 0; descriptor_index < copy.descriptorCount; ++descriptor_index)
            {
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
            const bool storage_or_indirect =
                hasFlag(usage, RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
                hasFlag(usage, RHI_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT) ||
                hasFlag(usage, RHI_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
            if (storage_or_indirect)
            {
                return D3D12_HEAP_TYPE_DEFAULT;
            }

            if (!hasFlag(properties, RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
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

        D3D12_RESOURCE_FLAGS bufferResourceFlags(RHIBufferUsageFlags usage, D3D12_HEAP_TYPE heap_type)
        {
            if (heap_type == D3D12_HEAP_TYPE_DEFAULT && hasFlag(usage, RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT))
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
                case RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                    return D3D12_RESOURCE_STATE_RENDER_TARGET;
                case RHI_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                    return D3D12_RESOURCE_STATE_DEPTH_WRITE;
                case RHI_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
                case RHI_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
                case RHI_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL:
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
                                                 D3D12_HEAP_TYPE heap_type)
        {
            if (heap_type == D3D12_HEAP_TYPE_UPLOAD)
            {
                return D3D12_RESOURCE_STATE_GENERIC_READ;
            }
            if (heap_type == D3D12_HEAP_TYPE_READBACK)
            {
                return D3D12_RESOURCE_STATE_COPY_DEST;
            }

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
                (hasFlag(access, RHI_ACCESS_SHADER_READ_BIT) || hasFlag(access, RHI_ACCESS_SHADER_WRITE_BIT)) &&
                !hasFlag(access, RHI_ACCESS_INDIRECT_COMMAND_READ_BIT))
            {
                return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
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
                state |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
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

            if (descriptor.buffer != nullptr)
            {
                appendUniqueBuffer(descriptor_set.storage_buffers, descriptor.buffer);
                if (descriptor.buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT &&
                    hasFlag(descriptor.buffer->memory_properties, RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
                {
                    appendUniqueBuffer(descriptor_set.host_visible_default_buffers, descriptor.buffer);
                }
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

                D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc {};
                cbv_desc.BufferLocation = descriptor.buffer->resource->GetGPUVirtualAddress() + byte_offset;
                cbv_desc.SizeInBytes    = static_cast<UINT>(alignUp(range, 256));
                device->CreateConstantBufferView(&cbv_desc, dst_handle);
                return;
            }

            const uint32_t stride = structuredBufferStride(binding, descriptor, range);
            if (descriptor.range_type == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc {};
                uav_desc.Format                     = DXGI_FORMAT_UNKNOWN;
                uav_desc.ViewDimension              = D3D12_UAV_DIMENSION_BUFFER;
                uav_desc.Buffer.StructureByteStride = stride;
                if (descriptor.buffer != nullptr && descriptor.buffer->resource != nullptr && stride != 0)
                {
                    uav_desc.Buffer.FirstElement = byte_offset / stride;
                    uav_desc.Buffer.NumElements  = static_cast<UINT>(range / stride);
                }
                device->CreateUnorderedAccessView(descriptor.buffer != nullptr ? descriptor.buffer->resource.Get() : nullptr,
                                                  nullptr,
                                                  &uav_desc,
                                                  dst_handle);
                return;
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc {};
            srv_desc.Format                     = DXGI_FORMAT_UNKNOWN;
            srv_desc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
            srv_desc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv_desc.Buffer.StructureByteStride = stride;
            if (descriptor.buffer != nullptr && descriptor.buffer->resource != nullptr && stride != 0)
            {
                srv_desc.Buffer.FirstElement = byte_offset / stride;
                srv_desc.Buffer.NumElements  = static_cast<UINT>(range / stride);
            }
            device->CreateShaderResourceView(descriptor.buffer != nullptr ? descriptor.buffer->resource.Get() : nullptr,
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

            if (size == 0 || device == nullptr)
            {
                buffer.host_data.resize(static_cast<size_t>(size));
                return true;
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
            resource_desc.Width              = (std::max)(static_cast<RHIDeviceSize>(1), size);
            resource_desc.Height             = 1;
            resource_desc.DepthOrArraySize   = 1;
            resource_desc.MipLevels          = 1;
            resource_desc.Format             = DXGI_FORMAT_UNKNOWN;
            resource_desc.SampleDesc.Count   = 1;
            resource_desc.SampleDesc.Quality = 0;
            resource_desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            resource_desc.Flags              = bufferResourceFlags(usage, buffer.heap_type);

            if (FAILED(device->CreateCommittedResource(&heap_properties,
                                                       D3D12_HEAP_FLAG_NONE,
                                                       &resource_desc,
                                                       buffer.current_state,
                                                       nullptr,
                                                       IID_PPV_ARGS(&buffer.resource))))
            {
                buffer.host_data.resize(static_cast<size_t>(size));
                return false;
            }

            if (hasFlag(properties, RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            {
                buffer.host_data.resize(static_cast<size_t>(size));
            }
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
            command_list->CopyBufferRegion(buffer.resource.Get(), 0, upload_buffer.Get(), 0, buffer.size);
            transitionResource(command_list, buffer.resource.Get(), buffer.current_state, previous_state);
            pending_uploads.push_back(upload_buffer);
            return true;
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
#endif
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
        if (!createDescriptorHeap(m_d3d12_device.Get(),
                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                  65536,
                                  true,
                                  m_d3d12_cbv_srv_uav_heap,
                                  m_d3d12_cbv_srv_uav_descriptor_size,
                                  m_d3d12_cbv_srv_uav_descriptor_capacity,
                                  m_d3d12_cbv_srv_uav_descriptor_next))
        {
            throw std::runtime_error("Failed to create D3D12 CBV/SRV/UAV descriptor heap");
        }
        m_d3d12_cbv_srv_uav_descriptor_next = 1;
        m_d3d12_transient_cbv_srv_uav_descriptor_next = m_d3d12_cbv_srv_uav_descriptor_next;
        if (!createDescriptorHeap(m_d3d12_device.Get(),
                                  D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                  2048,
                                  true,
                                  m_d3d12_sampler_heap,
                                  m_d3d12_sampler_descriptor_size,
                                  m_d3d12_sampler_descriptor_capacity,
                                  m_d3d12_sampler_descriptor_next))
        {
            throw std::runtime_error("Failed to create D3D12 sampler descriptor heap");
        }
        createFence();

        m_dummy_command_pool    = new D3D12RHICommandPool();
        m_dummy_descriptor_pool = new D3D12RHIDescriptorPool();
        m_dummy_graphics_queue  = new D3D12RHIQueue();
        m_dummy_compute_queue   = new D3D12RHIQueue();

        for (auto& command_buffer : m_dummy_command_buffers)
        {
            command_buffer = new D3D12RHICommandBuffer();
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
        createSwapchainImageViews();
        createFramebufferImageAndView();

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
        m_d3d12_sampler_heap.Reset();
        m_d3d12_cbv_srv_uav_heap.Reset();
        m_d3d12_dsv_heap.Reset();
        m_d3d12_rtv_heap.Reset();
        m_d3d12_swapchain.Reset();
        m_d3d12_command_list.Reset();
        m_d3d12_command_allocator.Reset();
        m_d3d12_command_queue.Reset();
        m_d3d12_device.Reset();
        m_dxgi_factory.Reset();
        m_d3d12_rtv_descriptor_size         = 0;
        m_d3d12_dsv_descriptor_size         = 0;
        m_d3d12_cbv_srv_uav_descriptor_size     = 0;
        m_d3d12_sampler_descriptor_size         = 0;
        m_d3d12_rtv_descriptor_capacity     = 0;
        m_d3d12_dsv_descriptor_capacity     = 0;
        m_d3d12_cbv_srv_uav_descriptor_capacity = 0;
        m_d3d12_sampler_descriptor_capacity     = 0;
        m_d3d12_rtv_descriptor_next         = 0;
        m_d3d12_dsv_descriptor_next         = 0;
        m_d3d12_cbv_srv_uav_descriptor_next     = 0;
        m_d3d12_transient_cbv_srv_uav_descriptor_next = 0;
        m_d3d12_sampler_descriptor_next         = 0;
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
        for (auto*& image : m_owned_swapchain_images)
        {
            delete image;
            image = nullptr;
        }
        m_owned_swapchain_images.clear();

#ifdef _WIN32
        m_pending_texture_readbacks.clear();
        m_pending_upload_buffers.clear();
        m_d3d12_dispatch_command_signature.Reset();
#endif

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

bool D3D12RHI::isPointLightShadowEnabled()
{
    return true;
}

bool D3D12RHI::allocateCommandBuffers(const RHICommandBufferAllocateInfo* pAllocateInfo, RHICommandBuffer* &pCommandBuffers)
{
    (void)pAllocateInfo;
    if (pCommandBuffers == nullptr)
    {
        pCommandBuffers = new D3D12RHICommandBuffer();
    }
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
    descriptor_set->sampler_cpu_base = cpuDescriptor(m_d3d12_sampler_heap.Get(),
                                                     m_d3d12_sampler_descriptor_size,
                                                     descriptor_set->sampler_base);
    descriptor_set->sampler_gpu_base = gpuDescriptor(m_d3d12_sampler_heap.Get(),
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

void D3D12RHI::createSwapchain()
{
    m_swapchain_desc.extent       = {m_window_width, m_window_height};
    m_swapchain_desc.image_format = RHI_FORMAT_R8G8B8A8_UNORM;
    m_swapchain_desc.viewport     = &m_swapchain_viewport;
    m_swapchain_desc.scissor      = &m_swapchain_scissor;
    m_swapchain_viewport.x        = 0.0f;
    m_swapchain_viewport.y        = 0.0f;
    m_swapchain_viewport.width    = static_cast<float>(m_window_width);
    m_swapchain_viewport.height   = static_cast<float>(m_window_height);
    m_swapchain_viewport.minDepth = 0.0f;
    m_swapchain_viewport.maxDepth = 1.0f;
    m_swapchain_scissor.offset    = {0, 0};
    m_swapchain_scissor.extent    = {m_window_width, m_window_height};
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
                                                                       0);
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
        m_current_frame_index = static_cast<uint8_t>(m_d3d12_swapchain->GetCurrentBackBufferIndex());
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
        image->current_state           = D3D12_RESOURCE_STATE_PRESENT;
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

    RHISampler* sampler = nullptr;
    createSampler(&sampler_info, sampler);
    return sampler;
}

RHISampler* D3D12RHI::getOrCreateMipmapSampler(uint32_t width, uint32_t height)
{
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
    sampler_info.maxLod                  = static_cast<float>(calculateMipLevels(width, height, 0));
    sampler_info.borderColor             = RHI_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sampler_info.unnormalizedCoordinates = RHI_FALSE;

    RHISampler* sampler = nullptr;
    createSampler(&sampler_info, sampler);
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
    auto* d3d_buffer = new D3D12RHIBuffer();
#ifdef _WIN32
    (void)createCommittedBuffer(m_d3d12_device.Get(), size, usage, properties, *d3d_buffer);
#else
    d3d_buffer->size  = size;
    d3d_buffer->usage = usage;
    d3d_buffer->host_data.resize(static_cast<size_t>(size));
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
#endif
        if (!d3d_buffer->host_data.empty())
        {
            const size_t host_copy_size = (std::min)(d3d_buffer->host_data.size(), static_cast<size_t>(copy_size));
            std::memcpy(d3d_buffer->host_data.data(), data, host_copy_size);
        }
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

    if (pAllocation)
    {
        *pAllocation = nullptr;
    }

    auto* d3d_buffer = new D3D12RHIBuffer();
#ifdef _WIN32
    const bool created = createCommittedBuffer(m_d3d12_device.Get(),
                                               pBufferCreateInfo->size,
                                               pBufferCreateInfo->usage,
                                               RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                               *d3d_buffer);
    pBuffer = d3d_buffer;
    return created;
#else
    d3d_buffer->size  = pBufferCreateInfo->size;
    d3d_buffer->usage = pBufferCreateInfo->usage;
    d3d_buffer->host_data.resize(static_cast<size_t>(pBufferCreateInfo->size));
    pBuffer = d3d_buffer;
    return true;
#endif
}

bool D3D12RHI::createBufferWithAlignmentVMA( VmaAllocator allocator, const RHIBufferCreateInfo* pBufferCreateInfo, const VmaAllocationCreateInfo* pAllocationCreateInfo, RHIDeviceSize minAlignment, RHIBuffer* &pBuffer, VmaAllocation* pAllocation, VmaAllocationInfo* pAllocationInfo)
{
    const RHIBufferCreateInfo default_buffer_info {RHI_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    RHIBufferCreateInfo aligned_buffer_info = pBufferCreateInfo ? *pBufferCreateInfo : default_buffer_info;
    aligned_buffer_info.size = alignUp(aligned_buffer_info.size, minAlignment);
    return createBufferVMA(allocator, &aligned_buffer_info, pAllocationCreateInfo, pBuffer, pAllocation, pAllocationInfo);
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
        return;
    }

#ifdef _WIN32
    if (src->resource != nullptr &&
        dst->resource != nullptr &&
        dst->heap_type != D3D12_HEAP_TYPE_UPLOAD)
    {
        const D3D12_RESOURCE_STATES src_previous_state = src->current_state;
        const D3D12_RESOURCE_STATES dst_previous_state = dst->current_state;
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

        if (copied)
        {
            if (!src->host_data.empty() && !dst->host_data.empty())
            {
                std::memcpy(dst->host_data.data() + static_cast<size_t>(dstOffset),
                            src->host_data.data() + static_cast<size_t>(srcOffset),
                            static_cast<size_t>(size));
            }
            return;
        }
    }
#endif

    if (!src->host_data.empty() && !dst->host_data.empty())
    {
        const size_t src_offset = static_cast<size_t>(srcOffset);
        const size_t dst_offset = static_cast<size_t>(dstOffset);
        const size_t copy_size  = static_cast<size_t>(size);
        if (src_offset + copy_size <= src->host_data.size() && dst_offset + copy_size <= dst->host_data.size())
        {
            std::memcpy(dst->host_data.data() + dst_offset, src->host_data.data() + src_offset, copy_size);
        }
    }
    return;
}

void D3D12RHI::createImage(uint32_t image_width, uint32_t image_height, RHIFormat format, RHIImageTiling image_tiling, RHIImageUsageFlags image_usage_flags, RHIMemoryPropertyFlags memory_property_flags, RHIImage* &image, RHIDeviceMemory* &memory, RHIImageCreateFlags image_create_flags, uint32_t array_layers, uint32_t miplevels)
{
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
    d3d_image->current_state            = initialImageState(image_usage_flags);
    d3d_image->source_bytes_per_pixel   = sourceBytesPerPixel(format);
    d3d_image->resource_bytes_per_pixel = resourceBytesPerPixel(format);

    if (m_d3d12_device != nullptr &&
        image_width > 0 &&
        image_height > 0 &&
        d3d_image->dxgi_format != DXGI_FORMAT_UNKNOWN)
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

        (void)m_d3d12_device->CreateCommittedResource(&heap_properties,
                                                      D3D12_HEAP_FLAG_NONE,
                                                      &resource_desc,
                                                      d3d_image->current_state,
                                                      clear_value_ptr,
                                                      IID_PPV_ARGS(&d3d_image->resource));
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

void D3D12RHI::createGlobalImage(RHIImage* &image, RHIImageView* &image_view, VmaAllocation& image_allocation, uint32_t texture_image_width, uint32_t texture_image_height, void* texture_image_pixels, RHIFormat texture_image_format, uint32_t miplevels)
{
    (void)image_allocation;
    const uint32_t uploaded_mip_levels = texture_image_pixels != nullptr ? 1U : miplevels;
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
                uploaded_mip_levels);
#ifdef _WIN32
    (void)uploadTexture2D(image, texture_image_pixels, 1);
#endif
    createImageView(image,
                    texture_image_format,
                    RHI_IMAGE_ASPECT_COLOR_BIT,
                    RHI_IMAGE_VIEW_TYPE_2D,
                    1,
                    uploaded_mip_levels,
                    image_view);
    delete memory;
    return;
}

void D3D12RHI::createCubeMap(RHIImage* &image, RHIImageView* &image_view, VmaAllocation& image_allocation, uint32_t texture_image_width, uint32_t texture_image_height, std::array<void*, 6> texture_image_pixels, RHIFormat texture_image_format, uint32_t miplevels)
{
    (void)image_allocation;
    const uint32_t uploaded_mip_levels = 1;
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
                uploaded_mip_levels);
#ifdef _WIN32
    const uint32_t bytes_per_pixel = sourceBytesPerPixel(texture_image_format);
    if (bytes_per_pixel > 0)
    {
        const size_t face_size = static_cast<size_t>(texture_image_width) *
                                 static_cast<size_t>(texture_image_height) *
                                 bytes_per_pixel;
        std::vector<uint8_t> cube_pixels(face_size * 6, 0);
        for (uint32_t face = 0; face < 6; ++face)
        {
            if (texture_image_pixels[face] != nullptr)
            {
                std::memcpy(cube_pixels.data() + face_size * face, texture_image_pixels[face], face_size);
            }
        }
        (void)uploadTexture2D(image, cube_pixels.data(), 6);
    }
#endif
    createImageView(image,
                    texture_image_format,
                    RHI_IMAGE_ASPECT_COLOR_BIT,
                    RHI_IMAGE_VIEW_TYPE_CUBE,
                    6,
                    uploaded_mip_levels,
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
            m_d3d12_cbv_srv_uav_descriptor_next = 1;
            m_d3d12_transient_cbv_srv_uav_descriptor_next = m_d3d12_cbv_srv_uav_descriptor_next;
        }
    }
    else if (cbv_srv_uav_required > m_d3d12_cbv_srv_uav_descriptor_capacity - m_d3d12_cbv_srv_uav_descriptor_next)
    {
        delete pool;
        return false;
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
        }
    }
    else if (sampler_required > m_d3d12_sampler_descriptor_capacity - m_d3d12_sampler_descriptor_next)
    {
        delete pool;
        return false;
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
    (void)pCreateInfo;
    if (pFence == nullptr)
    {
        pFence = new D3D12RHIFence();
    }
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

    delete pFramebuffer;
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
    if (subpass_info != nullptr &&
        subpass_info->depth_attachment_index < render_pass->attachments.size())
    {
        desc.DSVFormat = toDXGIFormat(render_pass->attachments[subpass_info->depth_attachment_index].format);
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

    if (FAILED(m_d3d12_device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline->pipeline_state))))
    {
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
                for (uint32_t i = 0; i < subpass.colorAttachmentCount; ++i)
                {
                    subpass_info.color_attachment_indices.push_back(subpass.pColorAttachments[i].attachment);
                }
            }
            if (subpass.pDepthStencilAttachment != nullptr)
            {
                subpass_info.depth_attachment_index = subpass.pDepthStencilAttachment->attachment;
                subpass_info.depth_attachment_layout = subpass.pDepthStencilAttachment->layout;
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
            }
        }
        render_pass->subpasses.push_back(subpass_info);
        render_pass->color_attachment_indices = subpass_info.color_attachment_indices;
        render_pass->depth_attachment_index   = subpass_info.depth_attachment_index;
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
    delete pSampler;
    pSampler = sampler;
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

    waitForGpu();

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
    m_d3d12_transient_cbv_srv_uav_descriptor_next = m_d3d12_cbv_srv_uav_descriptor_next;
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
    m_active_render_pass      = pRenderPassBegin != nullptr ? pRenderPassBegin->renderPass : nullptr;
    m_active_framebuffer      = pRenderPassBegin != nullptr ? pRenderPassBegin->framebuffer : nullptr;
    m_active_subpass_index    = 0;
    bindFramebufferForSubpass(pRenderPassBegin, m_active_subpass_index, true);
    m_in_render_pass = true;
#endif
}

void D3D12RHI::cmdNextSubpassPFN(RHICommandBuffer* commandBuffer, RHISubpassContents contents)
{
    (void)commandBuffer;
    (void)contents;
#ifdef _WIN32
    if (!m_in_render_pass)
    {
        return;
    }

    ++m_active_subpass_index;
    RHIRenderPassBeginInfo render_pass_begin_info {};
    render_pass_begin_info.renderPass  = m_active_render_pass;
    render_pass_begin_info.framebuffer = m_active_framebuffer;
    render_pass_begin_info.renderArea.offset = m_swapchain_scissor.offset;
    render_pass_begin_info.renderArea.extent = m_swapchain_scissor.extent;
    bindFramebufferForSubpass(&render_pass_begin_info, m_active_subpass_index, false);
#endif
}

void D3D12RHI::cmdEndRenderPassPFN(RHICommandBuffer* commandBuffer)
{
    (void)commandBuffer;
#ifdef _WIN32
    if (m_d3d12_command_list == nullptr)
    {
        return;
    }

    auto* render_pass = static_cast<D3D12RHIRenderPass*>(m_active_render_pass);
    auto* framebuffer = static_cast<D3D12RHIFramebuffer*>(m_active_framebuffer);
    if (render_pass != nullptr && framebuffer != nullptr)
    {
        for (uint32_t attachment_index = 0; attachment_index < framebuffer->attachments.size(); ++attachment_index)
        {
            if (attachment_index >= render_pass->attachments.size())
            {
                continue;
            }

            auto* view = framebuffer->attachments[attachment_index];
            const D3D12_RESOURCE_STATES final_state =
                toD3D12ResourceState(render_pass->attachments[attachment_index].finalLayout);
            if (view != nullptr && view->image != nullptr && view->image->resource != nullptr)
            {
                transitionResource(m_d3d12_command_list.Get(),
                                   view->image->resource.Get(),
                                   view->image->current_state,
                                   final_state);
            }
            else if (view != nullptr &&
                     view->has_rtv &&
                     render_pass->attachments[attachment_index].finalLayout == RHI_IMAGE_LAYOUT_PRESENT_SRC_KHR)
            {
                const uint32_t back_buffer_index = m_current_frame_index % m_swapchain_buffer_count;
                if (back_buffer_index < m_d3d12_render_targets.size() &&
                    m_d3d12_render_targets[back_buffer_index] != nullptr)
                {
                    D3D12_RESOURCE_BARRIER barrier {};
                    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    barrier.Transition.pResource   = m_d3d12_render_targets[back_buffer_index].Get();
                    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
                    m_d3d12_command_list->ResourceBarrier(1, &barrier);
                }
            }
        }
    }

    m_in_render_pass = false;
    m_active_render_pass = nullptr;
    m_active_framebuffer = nullptr;
    m_active_subpass_index = 0;
#endif
}

void D3D12RHI::cmdBindPipelinePFN(RHICommandBuffer* commandBuffer, RHIPipelineBindPoint pipelineBindPoint, RHIPipeline* pipeline)
{
    (void)commandBuffer;
#ifdef _WIN32
    auto* d3d_pipeline = static_cast<D3D12RHIPipeline*>(pipeline);
    if (m_d3d12_command_list == nullptr || d3d_pipeline == nullptr)
    {
        return;
    }

    if (d3d_pipeline->pipeline_state != nullptr)
    {
        m_d3d12_command_list->SetPipelineState(d3d_pipeline->pipeline_state.Get());
    }
    if (d3d_pipeline->layout != nullptr && d3d_pipeline->layout->root_signature != nullptr)
    {
        if (pipelineBindPoint == RHI_PIPELINE_BIND_POINT_COMPUTE)
        {
            m_d3d12_command_list->SetComputeRootSignature(d3d_pipeline->layout->root_signature.Get());
        }
        else
        {
            m_bound_graphics_pipeline = pipeline;
            m_d3d12_command_list->SetGraphicsRootSignature(d3d_pipeline->layout->root_signature.Get());
            m_d3d12_command_list->IASetPrimitiveTopology(d3d_pipeline->primitive_topology);
        }
    }
#else
    (void)pipelineBindPoint;
    (void)pipeline;
#endif
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
#ifdef _WIN32
    (void)commandBuffer;
    if (m_d3d12_command_list == nullptr || pBuffers == nullptr || bindingCount == 0)
    {
        return;
    }

    const auto* bound_pipeline = static_cast<const D3D12RHIPipeline*>(m_bound_graphics_pipeline);
    std::vector<D3D12_VERTEX_BUFFER_VIEW> views;
    views.reserve(bindingCount);
    for (uint32_t i = 0; i < bindingCount; ++i)
    {
        auto* buffer = static_cast<D3D12RHIBuffer*>(pBuffers[i]);
        if (buffer == nullptr || buffer->resource == nullptr)
        {
            views.push_back({});
            continue;
        }

        const RHIDeviceSize offset = pOffsets != nullptr ? pOffsets[i] : 0;
        if (buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT)
        {
            transitionResource(m_d3d12_command_list.Get(),
                               buffer->resource.Get(),
                               buffer->current_state,
                               D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        }
        D3D12_VERTEX_BUFFER_VIEW view {};
        view.BufferLocation = buffer->resource->GetGPUVirtualAddress() + offset;
        view.SizeInBytes    = offset < buffer->size ? static_cast<UINT>(buffer->size - offset) : 0;
        const uint32_t binding_index = firstBinding + i;
        if (bound_pipeline != nullptr && binding_index < bound_pipeline->vertex_strides.size())
        {
            view.StrideInBytes = bound_pipeline->vertex_strides[binding_index];
        }
        views.push_back(view);
    }

    m_d3d12_command_list->IASetVertexBuffers(firstBinding, static_cast<UINT>(views.size()), views.data());
#else
    (void)commandBuffer;
    (void)firstBinding;
    (void)bindingCount;
    (void)pBuffers;
    (void)pOffsets;
#endif
    return;
}

void D3D12RHI::cmdBindIndexBufferPFN(RHICommandBuffer* commandBuffer, RHIBuffer* buffer, RHIDeviceSize offset, RHIIndexType indexType)
{
#ifdef _WIN32
    (void)commandBuffer;
    auto* d3d_buffer = static_cast<D3D12RHIBuffer*>(buffer);
    if (m_d3d12_command_list == nullptr || d3d_buffer == nullptr || d3d_buffer->resource == nullptr)
    {
        return;
    }

    if (d3d_buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT)
    {
        transitionResource(m_d3d12_command_list.Get(),
                           d3d_buffer->resource.Get(),
                           d3d_buffer->current_state,
                           D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }

    D3D12_INDEX_BUFFER_VIEW view {};
    view.BufferLocation = d3d_buffer->resource->GetGPUVirtualAddress() + offset;
    view.SizeInBytes    = offset < d3d_buffer->size ? static_cast<UINT>(d3d_buffer->size - offset) : 0;
    view.Format         = indexType == RHI_INDEX_TYPE_UINT32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    m_d3d12_command_list->IASetIndexBuffer(&view);
#else
    (void)commandBuffer;
    (void)buffer;
    (void)offset;
    (void)indexType;
#endif
    return;
}

void D3D12RHI::cmdBindDescriptorSetsPFN( RHICommandBuffer* commandBuffer, RHIPipelineBindPoint pipelineBindPoint, RHIPipelineLayout* layout, uint32_t firstSet, uint32_t descriptorSetCount, const RHIDescriptorSet* const* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets)
{
    (void)commandBuffer;
#ifdef _WIN32
    auto* d3d_layout = static_cast<D3D12RHIPipelineLayout*>(layout);
    if (m_d3d12_command_list == nullptr || d3d_layout == nullptr || pDescriptorSets == nullptr)
    {
        return;
    }

    uint32_t preflight_dynamic_offset_index = 0;
    uint32_t preflight_transient_next = m_d3d12_transient_cbv_srv_uav_descriptor_next;
    for (uint32_t i = 0; i < descriptorSetCount; ++i)
    {
        const uint32_t set_index = firstSet + i;
        if (set_index >= d3d_layout->set_layouts.size() || pDescriptorSets[i] == nullptr)
        {
            continue;
        }

        const auto* descriptor_set = static_cast<const D3D12RHIDescriptorSet*>(pDescriptorSets[i]);
        const auto* set_layout = descriptor_set->layout;
        if (set_layout == nullptr)
        {
            continue;
        }

        const uint32_t required_dynamic_descriptor_count = dynamicDescriptorCount(*set_layout);
        if (required_dynamic_descriptor_count == 0)
        {
            continue;
        }

        if (pDynamicOffsets == nullptr ||
            preflight_dynamic_offset_index > dynamicOffsetCount ||
            required_dynamic_descriptor_count > dynamicOffsetCount - preflight_dynamic_offset_index ||
            !descriptor_set->has_cbv_srv_uav_descriptors ||
            m_d3d12_cbv_srv_uav_heap == nullptr)
        {
            return;
        }

        uint32_t unused_transient_base = 0;
        if (!reserveDescriptors(set_layout->cbv_srv_uav_descriptor_count,
                                preflight_transient_next,
                                m_d3d12_cbv_srv_uav_descriptor_capacity,
                                unused_transient_base))
        {
            return;
        }

        preflight_dynamic_offset_index += required_dynamic_descriptor_count;
    }

    ID3D12DescriptorHeap* heaps[2] {};
    UINT heap_count = 0;
    if (m_d3d12_cbv_srv_uav_heap != nullptr)
    {
        heaps[heap_count++] = m_d3d12_cbv_srv_uav_heap.Get();
    }
    if (m_d3d12_sampler_heap != nullptr)
    {
        heaps[heap_count++] = m_d3d12_sampler_heap.Get();
    }
    if (heap_count > 0)
    {
        m_d3d12_command_list->SetDescriptorHeaps(heap_count, heaps);
    }

    uint32_t dynamic_offset_index = 0;
    for (uint32_t i = 0; i < descriptorSetCount; ++i)
    {
        const uint32_t set_index = firstSet + i;
        if (set_index >= d3d_layout->set_layouts.size() || pDescriptorSets[i] == nullptr)
        {
            continue;
        }

        const auto* descriptor_set = static_cast<const D3D12RHIDescriptorSet*>(pDescriptorSets[i]);
        const auto* set_layout = descriptor_set->layout;
        if (set_layout == nullptr)
        {
            continue;
        }

        for (auto* buffer : descriptor_set->host_visible_default_buffers)
        {
            if (buffer != nullptr)
            {
                (void)recordHostDataUpload(m_d3d12_device.Get(),
                                           m_d3d12_command_list.Get(),
                                           m_pending_upload_buffers,
                                           *buffer);
            }
        }

        for (const auto& buffer_descriptor : descriptor_set->buffer_descriptors)
        {
            auto* buffer = buffer_descriptor.buffer;
            if (buffer != nullptr && buffer->resource != nullptr && buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT)
            {
                transitionResource(m_d3d12_command_list.Get(),
                                    buffer->resource.Get(),
                                    buffer->current_state,
                                    descriptorBufferState(buffer_descriptor.range_type));
            }
        }

        const uint32_t required_dynamic_descriptor_count = dynamicDescriptorCount(*set_layout);
        const bool has_dynamic_buffer_descriptors = required_dynamic_descriptor_count > 0;
        if (has_dynamic_buffer_descriptors &&
            (pDynamicOffsets == nullptr ||
             dynamic_offset_index > dynamicOffsetCount ||
             required_dynamic_descriptor_count > dynamicOffsetCount - dynamic_offset_index))
        {
            return;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE cbv_srv_uav_gpu_base = descriptor_set->cbv_srv_uav_gpu_base;
        if (has_dynamic_buffer_descriptors &&
            descriptor_set->has_cbv_srv_uav_descriptors &&
            set_layout->cbv_srv_uav_descriptor_count > 0)
        {
            uint32_t transient_base = 0;
            if (reserveDescriptors(set_layout->cbv_srv_uav_descriptor_count,
                                   m_d3d12_transient_cbv_srv_uav_descriptor_next,
                                   m_d3d12_cbv_srv_uav_descriptor_capacity,
                                   transient_base))
            {
                D3D12_CPU_DESCRIPTOR_HANDLE transient_cpu_base = cpuDescriptor(m_d3d12_cbv_srv_uav_heap.Get(),
                                                                              m_d3d12_cbv_srv_uav_descriptor_size,
                                                                              transient_base);
                m_d3d12_device->CopyDescriptorsSimple(set_layout->cbv_srv_uav_descriptor_count,
                                                      transient_cpu_base,
                                                      descriptor_set->cbv_srv_uav_cpu_base,
                                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                for (const auto& range : set_layout->ranges)
                {
                    if (!descriptorUsesResourceHeap(range.binding.descriptorType) ||
                        !isDynamicBufferDescriptor(range.binding.descriptorType))
                    {
                        continue;
                    }

                    for (uint32_t array_index = 0; array_index < range.binding.descriptorCount; ++array_index)
                    {
                        const RHIDeviceSize dynamic_offset =
                            (pDynamicOffsets != nullptr && dynamic_offset_index < dynamicOffsetCount) ?
                                pDynamicOffsets[dynamic_offset_index] :
                                0;
                        ++dynamic_offset_index;

                        const auto* buffer_descriptor =
                            descriptor_set->findBufferDescriptor(range.binding.binding, array_index);
                        if (buffer_descriptor == nullptr)
                        {
                            continue;
                        }

                        const uint32_t descriptor_index =
                            transient_base + range.cbv_srv_uav_offset + array_index;
                        D3D12_CPU_DESCRIPTOR_HANDLE dynamic_dst = cpuDescriptor(m_d3d12_cbv_srv_uav_heap.Get(),
                                                                               m_d3d12_cbv_srv_uav_descriptor_size,
                                                                               descriptor_index);
                        writeBufferDescriptor(m_d3d12_device.Get(),
                                              dynamic_dst,
                                              range,
                                              *buffer_descriptor,
                                              dynamic_offset);
                    }
                }

                cbv_srv_uav_gpu_base = gpuDescriptor(m_d3d12_cbv_srv_uav_heap.Get(),
                                                     m_d3d12_cbv_srv_uav_descriptor_size,
                                                     transient_base);
            }
            else
            {
                return;
            }
        }
        if (descriptor_set->has_cbv_srv_uav_descriptors &&
            set_index < d3d_layout->cbv_srv_uav_root_parameter_indices.size() &&
            d3d_layout->cbv_srv_uav_root_parameter_indices[set_index] != kInvalidRootParameterIndex)
        {
            const uint32_t root_index = d3d_layout->cbv_srv_uav_root_parameter_indices[set_index];
            if (pipelineBindPoint == RHI_PIPELINE_BIND_POINT_COMPUTE)
            {
                m_d3d12_command_list->SetComputeRootDescriptorTable(root_index, cbv_srv_uav_gpu_base);
            }
            else
            {
                m_d3d12_command_list->SetGraphicsRootDescriptorTable(root_index, cbv_srv_uav_gpu_base);
            }
        }
        if (descriptor_set->has_sampler_descriptors &&
            set_index < d3d_layout->sampler_root_parameter_indices.size() &&
            d3d_layout->sampler_root_parameter_indices[set_index] != kInvalidRootParameterIndex)
        {
            const uint32_t root_index = d3d_layout->sampler_root_parameter_indices[set_index];
            if (pipelineBindPoint == RHI_PIPELINE_BIND_POINT_COMPUTE)
            {
                m_d3d12_command_list->SetComputeRootDescriptorTable(root_index, descriptor_set->sampler_gpu_base);
            }
            else
            {
                m_d3d12_command_list->SetGraphicsRootDescriptorTable(root_index, descriptor_set->sampler_gpu_base);
            }
        }
    }
#else
    (void)pipelineBindPoint;
    (void)layout;
    (void)firstSet;
    (void)descriptorSetCount;
    (void)pDescriptorSets;
#endif
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
    (void)commandBuffer;
#ifdef _WIN32
    if (m_d3d12_command_list == nullptr ||
        pAttachments == nullptr ||
        attachmentCount == 0 ||
        (rectCount > 0 && pRects == nullptr))
    {
        return;
    }

    auto* render_pass = static_cast<D3D12RHIRenderPass*>(m_active_render_pass);
    auto* framebuffer = static_cast<D3D12RHIFramebuffer*>(m_active_framebuffer);
    if (render_pass == nullptr || framebuffer == nullptr || m_active_subpass_index >= render_pass->subpasses.size())
    {
        return;
    }

    std::vector<D3D12_RECT> clear_rects;
    clear_rects.reserve(rectCount);
    for (uint32_t rect_index = 0; rect_index < rectCount; ++rect_index)
    {
        D3D12_RECT rect {};
        rect.left   = pRects[rect_index].rect.offset.x;
        rect.top    = pRects[rect_index].rect.offset.y;
        rect.right  = pRects[rect_index].rect.offset.x + static_cast<LONG>(pRects[rect_index].rect.extent.width);
        rect.bottom = pRects[rect_index].rect.offset.y + static_cast<LONG>(pRects[rect_index].rect.extent.height);
        clear_rects.push_back(rect);
    }

    const D3D12RHIRenderPass::SubpassInfo& subpass = render_pass->subpasses[m_active_subpass_index];
    for (uint32_t attachment_index = 0; attachment_index < attachmentCount; ++attachment_index)
    {
        const RHIClearAttachment& clear_attachment = pAttachments[attachment_index];
        if (hasFlag(clear_attachment.aspectMask, RHI_IMAGE_ASPECT_COLOR_BIT))
        {
            if (clear_attachment.colorAttachment >= subpass.color_attachment_indices.size())
            {
                continue;
            }
            const uint32_t framebuffer_attachment_index =
                subpass.color_attachment_indices[clear_attachment.colorAttachment];
            if (framebuffer_attachment_index >= framebuffer->attachments.size())
            {
                continue;
            }

            auto* view = framebuffer->attachments[framebuffer_attachment_index];
            if (view == nullptr || !view->has_rtv || view->cpu_descriptor.ptr == 0)
            {
                continue;
            }

            const FLOAT color[4] = {clear_attachment.clearValue.color.float32[0],
                                    clear_attachment.clearValue.color.float32[1],
                                    clear_attachment.clearValue.color.float32[2],
                                    clear_attachment.clearValue.color.float32[3]};
            m_d3d12_command_list->ClearRenderTargetView(view->cpu_descriptor,
                                                        color,
                                                        static_cast<UINT>(clear_rects.size()),
                                                        clear_rects.empty() ? nullptr : clear_rects.data());
        }

        if (hasFlag(clear_attachment.aspectMask, RHI_IMAGE_ASPECT_DEPTH_BIT) ||
            hasFlag(clear_attachment.aspectMask, RHI_IMAGE_ASPECT_STENCIL_BIT))
        {
            if (subpass.depth_attachment_index >= framebuffer->attachments.size() ||
                subpass.depth_attachment_index >= render_pass->attachments.size())
            {
                continue;
            }
            auto* depth_view = framebuffer->attachments[subpass.depth_attachment_index];
            if (depth_view == nullptr || !depth_view->has_dsv || depth_view->cpu_descriptor.ptr == 0)
            {
                continue;
            }

            D3D12_CLEAR_FLAGS clear_flags = static_cast<D3D12_CLEAR_FLAGS>(0);
            if (hasFlag(clear_attachment.aspectMask, RHI_IMAGE_ASPECT_DEPTH_BIT))
            {
                clear_flags = static_cast<D3D12_CLEAR_FLAGS>(clear_flags | D3D12_CLEAR_FLAG_DEPTH);
            }
            if (hasFlag(clear_attachment.aspectMask, RHI_IMAGE_ASPECT_STENCIL_BIT) &&
                formatHasStencil(render_pass->attachments[subpass.depth_attachment_index].format))
            {
                clear_flags = static_cast<D3D12_CLEAR_FLAGS>(clear_flags | D3D12_CLEAR_FLAG_STENCIL);
            }
            if (clear_flags == 0)
            {
                continue;
            }

            m_d3d12_command_list->ClearDepthStencilView(depth_view->cpu_descriptor,
                                                        clear_flags,
                                                        clear_attachment.clearValue.depthStencil.depth,
                                                        static_cast<UINT8>(clear_attachment.clearValue.depthStencil.stencil),
                                                        static_cast<UINT>(clear_rects.size()),
                                                        clear_rects.empty() ? nullptr : clear_rects.data());
        }
    }
#else
    (void)attachmentCount;
    (void)pAttachments;
    (void)rectCount;
    (void)pRects;
#endif
    return;
}

bool D3D12RHI::beginCommandBuffer(RHICommandBuffer* commandBuffer, const RHICommandBufferBeginInfo* pBeginInfo)
{
    return beginCommandBufferPFN(commandBuffer, pBeginInfo);
}

void D3D12RHI::cmdCopyImageToBuffer(RHICommandBuffer* commandBuffer, RHIImage* srcImage, RHIImageLayout srcImageLayout, RHIBuffer* dstBuffer, uint32_t regionCount, const RHIBufferImageCopy* pRegions)
{
    (void)commandBuffer;
    (void)srcImageLayout;
#ifdef _WIN32
    auto* src = static_cast<D3D12RHIImage*>(srcImage);
    auto* dst = static_cast<D3D12RHIBuffer*>(dstBuffer);
    if (m_d3d12_device == nullptr ||
        m_d3d12_command_list == nullptr ||
        src == nullptr ||
        dst == nullptr ||
        src->resource == nullptr ||
        pRegions == nullptr ||
        regionCount == 0 ||
        src->resource_bytes_per_pixel == 0)
    {
        return;
    }

    transitionResource(m_d3d12_command_list.Get(),
                       src->resource.Get(),
                       src->current_state,
                       D3D12_RESOURCE_STATE_COPY_SOURCE);

    const D3D12_RESOURCE_DESC texture_desc = src->resource->GetDesc();
    for (uint32_t region_index = 0; region_index < regionCount; ++region_index)
    {
        const RHIBufferImageCopy& region = pRegions[region_index];
        const uint32_t layer_count = (std::max)(1U, region.imageSubresource.layerCount);
        const uint32_t row_length = region.bufferRowLength == 0 ? region.imageExtent.width : region.bufferRowLength;
        const uint32_t image_height = region.bufferImageHeight == 0 ? region.imageExtent.height : region.bufferImageHeight;
        const uint32_t destination_row_pitch = row_length * src->resource_bytes_per_pixel;
        const RHIDeviceSize destination_layer_pitch =
            static_cast<RHIDeviceSize>(destination_row_pitch) * image_height;

        for (uint32_t layer = 0; layer < layer_count; ++layer)
        {
            const uint32_t array_layer = region.imageSubresource.baseArrayLayer + layer;
            if (array_layer >= src->array_layers || region.imageSubresource.mipLevel >= src->mip_levels)
            {
                continue;
            }

            const uint32_t subresource = d3d12SubresourceIndex(*src, region.imageSubresource.mipLevel, array_layer);
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
            UINT row_count = 0;
            UINT64 row_size = 0;
            UINT64 readback_buffer_size = 0;
            m_d3d12_device->GetCopyableFootprints(&texture_desc,
                                                  subresource,
                                                  1,
                                                  0,
                                                  &footprint,
                                                  &row_count,
                                                  &row_size,
                                                  &readback_buffer_size);
            if (readback_buffer_size == 0)
            {
                continue;
            }

            D3D12_HEAP_PROPERTIES readback_heap_properties {};
            readback_heap_properties.Type                 = D3D12_HEAP_TYPE_READBACK;
            readback_heap_properties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            readback_heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            readback_heap_properties.CreationNodeMask     = 1;
            readback_heap_properties.VisibleNodeMask      = 1;

            D3D12_RESOURCE_DESC readback_desc {};
            readback_desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
            readback_desc.Alignment          = 0;
            readback_desc.Width              = readback_buffer_size;
            readback_desc.Height             = 1;
            readback_desc.DepthOrArraySize   = 1;
            readback_desc.MipLevels          = 1;
            readback_desc.Format             = DXGI_FORMAT_UNKNOWN;
            readback_desc.SampleDesc.Count   = 1;
            readback_desc.SampleDesc.Quality = 0;
            readback_desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            readback_desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

            ComPtr<ID3D12Resource> readback_buffer;
            if (FAILED(m_d3d12_device->CreateCommittedResource(&readback_heap_properties,
                                                               D3D12_HEAP_FLAG_NONE,
                                                               &readback_desc,
                                                               D3D12_RESOURCE_STATE_COPY_DEST,
                                                               nullptr,
                                                               IID_PPV_ARGS(&readback_buffer))))
            {
                continue;
            }

            D3D12_TEXTURE_COPY_LOCATION dst_location {};
            dst_location.pResource       = readback_buffer.Get();
            dst_location.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst_location.PlacedFootprint = footprint;

            D3D12_TEXTURE_COPY_LOCATION src_location {};
            src_location.pResource        = src->resource.Get();
            src_location.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src_location.SubresourceIndex = subresource;

            D3D12_BOX source_box {};
            source_box.left   = static_cast<UINT>((std::max)(0, region.imageOffset.x));
            source_box.top    = static_cast<UINT>((std::max)(0, region.imageOffset.y));
            source_box.front  = static_cast<UINT>((std::max)(0, region.imageOffset.z));
            source_box.right  = source_box.left + region.imageExtent.width;
            source_box.bottom = source_box.top + region.imageExtent.height;
            source_box.back   = source_box.front + (std::max)(1U, region.imageExtent.depth);

            m_d3d12_command_list->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, &source_box);

            D3D12PendingTextureReadback pending_readback {};
            pending_readback.destination_buffer    = dstBuffer;
            pending_readback.destination_offset    = region.bufferOffset + destination_layer_pitch * layer;
            pending_readback.destination_row_pitch = destination_row_pitch;
            pending_readback.row_count             = (std::min)(row_count, region.imageExtent.height);
            pending_readback.row_size              = (std::min)(static_cast<uint32_t>(row_size),
                                                                region.imageExtent.width * src->resource_bytes_per_pixel);
            pending_readback.footprint             = footprint;
            pending_readback.readback_buffer       = readback_buffer;
            m_pending_texture_readbacks.push_back(pending_readback);
        }
    }
#else
    (void)srcImage;
    (void)dstBuffer;
    (void)regionCount;
    (void)pRegions;
#endif
    return;
}

void D3D12RHI::cmdCopyImageToImage(RHICommandBuffer* commandBuffer, RHIImage* srcImage, RHIImageAspectFlagBits srcFlag, RHIImage* dstImage, RHIImageAspectFlagBits dstFlag, uint32_t width, uint32_t height)
{
    (void)commandBuffer;
    (void)srcFlag;
    (void)dstFlag;
#ifdef _WIN32
    auto* src = static_cast<D3D12RHIImage*>(srcImage);
    auto* dst = static_cast<D3D12RHIImage*>(dstImage);
    if (m_d3d12_command_list == nullptr ||
        src == nullptr ||
        dst == nullptr ||
        src->resource == nullptr ||
        dst->resource == nullptr)
    {
        return;
    }

    transitionResource(m_d3d12_command_list.Get(), src->resource.Get(), src->current_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transitionResource(m_d3d12_command_list.Get(), dst->resource.Get(), dst->current_state, D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12_TEXTURE_COPY_LOCATION src_location {};
    src_location.pResource        = src->resource.Get();
    src_location.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src_location.SubresourceIndex = d3d12SubresourceIndex(*src, 0, 0);

    D3D12_TEXTURE_COPY_LOCATION dst_location {};
    dst_location.pResource        = dst->resource.Get();
    dst_location.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_location.SubresourceIndex = d3d12SubresourceIndex(*dst, 0, 0);

    D3D12_BOX source_box {};
    source_box.left   = 0;
    source_box.top    = 0;
    source_box.front  = 0;
    source_box.right  = (std::min)(width, src->width);
    source_box.bottom = (std::min)(height, src->height);
    source_box.back   = 1;
    m_d3d12_command_list->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, &source_box);
#else
    (void)srcImage;
    (void)dstImage;
    (void)width;
    (void)height;
#endif
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
#ifdef _WIN32
        auto* src = static_cast<D3D12RHIBuffer*>(srcBuffer);
        auto* dst = static_cast<D3D12RHIBuffer*>(dstBuffer);
        const RHIBufferCopy& region = pRegions[i];
        if (m_d3d12_command_list != nullptr &&
            src->resource != nullptr &&
            dst->resource != nullptr &&
            dst->heap_type != D3D12_HEAP_TYPE_UPLOAD &&
            region.srcOffset <= src->size &&
            region.dstOffset <= dst->size &&
            region.size <= src->size - region.srcOffset &&
            region.size <= dst->size - region.dstOffset)
        {
            if (src->heap_type == D3D12_HEAP_TYPE_DEFAULT)
            {
                transitionResource(m_d3d12_command_list.Get(),
                                   src->resource.Get(),
                                   src->current_state,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE);
            }
            if (dst->heap_type == D3D12_HEAP_TYPE_DEFAULT)
            {
                transitionResource(m_d3d12_command_list.Get(),
                                   dst->resource.Get(),
                                   dst->current_state,
                                   D3D12_RESOURCE_STATE_COPY_DEST);
            }
            m_d3d12_command_list->CopyBufferRegion(dst->resource.Get(),
                                                   region.dstOffset,
                                                   src->resource.Get(),
                                                   region.srcOffset,
                                                   region.size);
            dst->map_host_data = false;
            if (!src->host_data.empty() && !dst->host_data.empty())
            {
                std::memcpy(dst->host_data.data() + static_cast<size_t>(region.dstOffset),
                            src->host_data.data() + static_cast<size_t>(region.srcOffset),
                            static_cast<size_t>(region.size));
            }
            continue;
        }
#endif
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
#ifdef _WIN32
    auto* d3d_buffer = static_cast<D3D12RHIBuffer*>(buffer);
    if (m_d3d12_command_list == nullptr ||
        d3d_buffer == nullptr ||
        d3d_buffer->resource == nullptr ||
        !ensureDispatchCommandSignature())
    {
        return;
    }

    if (d3d_buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT)
    {
        transitionResource(m_d3d12_command_list.Get(),
                           d3d_buffer->resource.Get(),
                           d3d_buffer->current_state,
                           D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    }

    m_d3d12_command_list->ExecuteIndirect(m_d3d12_dispatch_command_signature.Get(),
                                          1,
                                          d3d_buffer->resource.Get(),
                                          offset,
                                          nullptr,
                                          0);
#else
    (void)buffer;
    (void)offset;
#endif
    return;
}

void D3D12RHI::cmdPipelineBarrier(RHICommandBuffer* commandBuffer, RHIPipelineStageFlags srcStageMask, RHIPipelineStageFlags dstStageMask, RHIDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const RHIMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const RHIBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const RHIImageMemoryBarrier* pImageMemoryBarriers)
{
    (void)commandBuffer;
    (void)srcStageMask;
    (void)dstStageMask;
    (void)dependencyFlags;
#ifdef _WIN32
    if (m_d3d12_command_list == nullptr)
    {
        return;
    }

    if (pMemoryBarriers != nullptr)
    {
        for (uint32_t barrier_index = 0; barrier_index < memoryBarrierCount; ++barrier_index)
        {
            const RHIMemoryBarrier& memory_barrier = pMemoryBarriers[barrier_index];
            if (hasFlag(memory_barrier.srcAccessMask, RHI_ACCESS_SHADER_WRITE_BIT) ||
                hasFlag(memory_barrier.dstAccessMask, RHI_ACCESS_SHADER_WRITE_BIT) ||
                hasFlag(memory_barrier.srcAccessMask, RHI_ACCESS_MEMORY_WRITE_BIT) ||
                hasFlag(memory_barrier.dstAccessMask, RHI_ACCESS_MEMORY_WRITE_BIT))
            {
                D3D12_RESOURCE_BARRIER barrier {};
                barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.Flags         = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.UAV.pResource = nullptr;
                m_d3d12_command_list->ResourceBarrier(1, &barrier);
            }
        }
    }

    if (pBufferMemoryBarriers != nullptr)
    {
        for (uint32_t barrier_index = 0; barrier_index < bufferMemoryBarrierCount; ++barrier_index)
        {
            const RHIBufferMemoryBarrier& buffer_barrier = pBufferMemoryBarriers[barrier_index];
            auto* buffer = static_cast<D3D12RHIBuffer*>(buffer_barrier.buffer);
            if (buffer == nullptr || buffer->resource == nullptr)
            {
                continue;
            }

            const D3D12_RESOURCE_STATES target_state =
                toD3D12BufferState(buffer_barrier.dstAccessMask, buffer->usage, buffer->heap_type);
            if (buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT)
            {
                if (buffer->current_state == target_state &&
                    (hasFlag(buffer_barrier.srcAccessMask, RHI_ACCESS_SHADER_WRITE_BIT) ||
                     hasFlag(buffer_barrier.dstAccessMask, RHI_ACCESS_SHADER_WRITE_BIT)))
                {
                    D3D12_RESOURCE_BARRIER barrier {};
                    barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    barrier.Flags         = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    barrier.UAV.pResource = buffer->resource.Get();
                    m_d3d12_command_list->ResourceBarrier(1, &barrier);
                }
                else
                {
                    transitionResource(m_d3d12_command_list.Get(),
                                       buffer->resource.Get(),
                                       buffer->current_state,
                                       target_state);
                }
            }
        }
    }

    if (pImageMemoryBarriers != nullptr)
    {
        for (uint32_t barrier_index = 0; barrier_index < imageMemoryBarrierCount; ++barrier_index)
        {
            const RHIImageMemoryBarrier& image_barrier = pImageMemoryBarriers[barrier_index];
            auto* image = static_cast<D3D12RHIImage*>(image_barrier.image);
            if (image == nullptr || image->resource == nullptr)
            {
                continue;
            }

            const D3D12_RESOURCE_STATES target_state = toD3D12ResourceState(image_barrier.newLayout);
            if (image->current_state == target_state &&
                (hasFlag(image_barrier.srcAccessMask, RHI_ACCESS_SHADER_WRITE_BIT) ||
                 hasFlag(image_barrier.dstAccessMask, RHI_ACCESS_SHADER_WRITE_BIT)))
            {
                D3D12_RESOURCE_BARRIER barrier {};
                barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.Flags         = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.UAV.pResource = image->resource.Get();
                m_d3d12_command_list->ResourceBarrier(1, &barrier);
            }
            else
            {
                transitionResource(m_d3d12_command_list.Get(),
                                   image->resource.Get(),
                                   image->current_state,
                                   target_state);
            }
        }
    }
#else
    (void)memoryBarrierCount;
    (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount;
    (void)pBufferMemoryBarriers;
    (void)imageMemoryBarrierCount;
    (void)pImageMemoryBarriers;
#endif
    return;
}

bool D3D12RHI::endCommandBuffer(RHICommandBuffer* commandBuffer)
{
    return endCommandBufferPFN(commandBuffer);
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

        for (uint32_t descriptor_index = 0; descriptor_index < write.descriptorCount; ++descriptor_index)
        {
            const uint32_t array_index = write.dstArrayElement + descriptor_index;
            if (descriptorUsesResourceHeap(write.descriptorType))
            {
                if (!descriptor_set->has_cbv_srv_uav_descriptors || m_d3d12_cbv_srv_uav_heap == nullptr)
                {
                    continue;
                }
                const uint32_t heap_index = descriptor_set->cbv_srv_uav_base + binding->cbv_srv_uav_offset + array_index;
                D3D12_CPU_DESCRIPTOR_HANDLE dst_handle = cpuDescriptor(m_d3d12_cbv_srv_uav_heap.Get(),
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
                        writeBufferDescriptor(m_d3d12_device.Get(), dst_handle, *binding, *descriptor_to_write, 0);
                    }
                }
                else if (write.descriptorType == RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                {
                    const RHIDescriptorImageInfo* image_info = &write.pImageInfo[descriptor_index];
                    auto* image_view = static_cast<D3D12RHIImageView*>(image_info->imageView);
                    if (image_view != nullptr && image_view->image != nullptr && image_view->image->resource != nullptr && image_view->has_uav)
                    {
                        m_d3d12_device->CreateUnorderedAccessView(image_view->image->resource.Get(), nullptr, &image_view->uav_desc, dst_handle);
                    }
                }
                else
                {
                    const RHIDescriptorImageInfo* image_info = &write.pImageInfo[descriptor_index];
                    auto* image_view = static_cast<D3D12RHIImageView*>(image_info->imageView);
                    if (image_view != nullptr && image_view->image != nullptr && image_view->image->resource != nullptr && image_view->has_srv)
                    {
                        m_d3d12_device->CreateShaderResourceView(image_view->image->resource.Get(), &image_view->srv_desc, dst_handle);
                    }
                }
            }

            if (descriptorUsesSamplerHeap(write.descriptorType))
            {
                if (!descriptor_set->has_sampler_descriptors || m_d3d12_sampler_heap == nullptr)
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
                    D3D12_CPU_DESCRIPTOR_HANDLE dst_handle = cpuDescriptor(m_d3d12_sampler_heap.Get(),
                                                                           m_d3d12_sampler_descriptor_size,
                                                                           heap_index);
                    m_d3d12_device->CreateSampler(&sampler->desc, dst_handle);
                }
            }
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

        if (descriptorUsesResourceHeap(src_binding->binding.descriptorType) &&
            descriptorUsesResourceHeap(dst_binding->binding.descriptorType))
        {
            if (!src_set->has_cbv_srv_uav_descriptors || !dst_set->has_cbv_srv_uav_descriptors ||
                m_d3d12_cbv_srv_uav_heap == nullptr)
            {
                continue;
            }
            const uint32_t src_index = src_set->cbv_srv_uav_base + src_binding->cbv_srv_uav_offset + copy.srcArrayElement;
            const uint32_t dst_index = dst_set->cbv_srv_uav_base + dst_binding->cbv_srv_uav_offset + copy.dstArrayElement;
            m_d3d12_device->CopyDescriptorsSimple(copy.descriptorCount,
                                                  cpuDescriptor(m_d3d12_cbv_srv_uav_heap.Get(), m_d3d12_cbv_srv_uav_descriptor_size, dst_index),
                                                  cpuDescriptor(m_d3d12_cbv_srv_uav_heap.Get(), m_d3d12_cbv_srv_uav_descriptor_size, src_index),
                                                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

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
        }

        if (descriptorUsesSamplerHeap(src_binding->binding.descriptorType) &&
            descriptorUsesSamplerHeap(dst_binding->binding.descriptorType))
        {
            if (!src_set->has_sampler_descriptors || !dst_set->has_sampler_descriptors ||
                m_d3d12_sampler_heap == nullptr)
            {
                continue;
            }
            const uint32_t src_index = src_set->sampler_base + src_binding->sampler_offset + copy.srcArrayElement;
            const uint32_t dst_index = dst_set->sampler_base + dst_binding->sampler_offset + copy.dstArrayElement;
            m_d3d12_device->CopyDescriptorsSimple(copy.descriptorCount,
                                                  cpuDescriptor(m_d3d12_sampler_heap.Get(), m_d3d12_sampler_descriptor_size, dst_index),
                                                  cpuDescriptor(m_d3d12_sampler_heap.Get(), m_d3d12_sampler_descriptor_size, src_index),
                                                  D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
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
    waitForGpu();
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

    waitForGpu();

    (void)m_d3d12_command_allocator->Reset();
    (void)m_d3d12_command_list->Reset(m_d3d12_command_allocator.Get(), nullptr);
    m_command_list_open = true;
    m_d3d12_transient_cbv_srv_uav_descriptor_next = m_d3d12_cbv_srv_uav_descriptor_next;
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
    auto* command_buffer = new D3D12RHICommandBuffer();
#ifdef _WIN32
    if (!m_command_list_open)
    {
        RHICommandBufferBeginInfo begin_info {};
        begin_info.sType = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (beginCommandBufferPFN(command_buffer, &begin_info))
        {
            command_buffer->owns_recording = true;
        }
    }
#endif
    return command_buffer;
}

void D3D12RHI::endSingleTimeCommands(RHICommandBuffer* command_buffer)
{
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(command_buffer);
#ifdef _WIN32
    if (d3d_command_buffer != nullptr && d3d_command_buffer->owns_recording)
    {
        if (m_d3d12_command_queue != nullptr && m_d3d12_command_list != nullptr)
        {
            if (m_command_list_open)
            {
                if (FAILED(m_d3d12_command_list->Close()))
                {
                    delete d3d_command_buffer;
                    m_command_list_open = false;
                    return;
                }
                m_command_list_open = false;
            }

            ID3D12CommandList* command_lists[] = {m_d3d12_command_list.Get()};
            m_d3d12_command_queue->ExecuteCommandLists(1, command_lists);
            waitForGpu();
        }
    }
#endif
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
        m_d3d12_transient_cbv_srv_uav_descriptor_next = m_d3d12_cbv_srv_uav_descriptor_next;
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

        D3D12_RANGE read_range {0, 0};
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
            d3d_memory->owner_buffer->resource->Unmap(0, &written_range);
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
    (void)offset;
    (void)size;
#ifdef _WIN32
    auto* d3d_memory = static_cast<D3D12RHIDeviceMemory*>(memory);
    if (d3d_memory == nullptr || d3d_memory->owner_buffer == nullptr)
    {
        return;
    }

    auto* buffer = d3d_memory->owner_buffer;
    if (buffer->resource == nullptr ||
        buffer->heap_type != D3D12_HEAP_TYPE_DEFAULT ||
        buffer->host_data.empty())
    {
        return;
    }

    if (m_command_list_open && m_d3d12_command_list != nullptr)
    {
        (void)recordHostDataUpload(m_d3d12_device.Get(),
                                   m_d3d12_command_list.Get(),
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
        if (!createDescriptorHeap(m_d3d12_device.Get(),
                                  D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                  1024,
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
                                  256,
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

    GLFWwindow* D3D12RHI::getWindow() const
    {
        return m_window;
    }

    ID3D12Device* D3D12RHI::getD3D12Device() const
    {
        return m_d3d12_device.Get();
    }

    ID3D12GraphicsCommandList* D3D12RHI::getD3D12CommandList() const
    {
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

    bool D3D12RHI::uploadTexture2D(RHIImage* image, const void* texture_pixels, uint32_t layer_count)
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

        D3D12_RESOURCE_DESC texture_desc = d3d_image->resource->GetDesc();
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(layer_count);
        std::vector<UINT>                               row_counts(layer_count);
        std::vector<UINT64>                             row_sizes(layer_count);
        UINT64 upload_buffer_size = 0;
        m_d3d12_device->GetCopyableFootprints(&texture_desc,
                                              0,
                                              layer_count,
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
        const size_t source_row_size = static_cast<size_t>(d3d_image->width) * d3d_image->source_bytes_per_pixel;
        const size_t source_layer_size = source_row_size * static_cast<size_t>(d3d_image->height);
        for (uint32_t layer = 0; layer < layer_count; ++layer)
        {
            const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint = footprints[layer];
            for (UINT row = 0; row < row_counts[layer]; ++row)
            {
                uint8_t* dst_row = mapped_data + footprint.Offset + static_cast<size_t>(row) * footprint.Footprint.RowPitch;
                const uint8_t* src_row = source_pixels + static_cast<size_t>(layer) * source_layer_size +
                                         static_cast<size_t>(row) * source_row_size;
                std::memset(dst_row, 0, footprint.Footprint.RowPitch);
                if (d3d_image->source_bytes_per_pixel == d3d_image->resource_bytes_per_pixel)
                {
                    std::memcpy(dst_row, src_row, (std::min)(source_row_size, static_cast<size_t>(row_sizes[layer])));
                }
                else if (d3d_image->source_bytes_per_pixel == 3 && d3d_image->resource_bytes_per_pixel == 4)
                {
                    for (uint32_t x = 0; x < d3d_image->width; ++x)
                    {
                        dst_row[x * 4 + 0] = src_row[x * 3 + 0];
                        dst_row[x * 4 + 1] = src_row[x * 3 + 1];
                        dst_row[x * 4 + 2] = src_row[x * 3 + 2];
                        dst_row[x * 4 + 3] = 255;
                    }
                }
                else if (d3d_image->source_bytes_per_pixel == 12 && d3d_image->resource_bytes_per_pixel == 16)
                {
                    for (uint32_t x = 0; x < d3d_image->width; ++x)
                    {
                        std::memcpy(dst_row + x * 16, src_row + x * 12, 12);
                        float alpha = 1.0f;
                        std::memcpy(dst_row + x * 16 + 12, &alpha, sizeof(alpha));
                    }
                }
                else
                {
                    const size_t row_copy_size = (std::min)(source_row_size,
                                                            static_cast<size_t>(footprint.Footprint.RowPitch));
                    std::memcpy(dst_row, src_row, row_copy_size);
                }
            }
        }
        upload_buffer->Unmap(0, nullptr);

        const D3D12_RESOURCE_STATES final_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        return executeImmediateCommands(
            [&](ID3D12GraphicsCommandList* command_list)
            {
                transitionResource(command_list,
                                   d3d_image->resource.Get(),
                                   d3d_image->current_state,
                                   D3D12_RESOURCE_STATE_COPY_DEST);
                for (uint32_t layer = 0; layer < layer_count; ++layer)
                {
                    D3D12_TEXTURE_COPY_LOCATION dst_location {};
                    dst_location.pResource        = d3d_image->resource.Get();
                    dst_location.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    dst_location.SubresourceIndex = layer;

                    D3D12_TEXTURE_COPY_LOCATION src_location {};
                    src_location.pResource       = upload_buffer.Get();
                    src_location.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    src_location.PlacedFootprint = footprints[layer];

                    command_list->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, nullptr);
                }
                transitionResource(command_list,
                                   d3d_image->resource.Get(),
                                   d3d_image->current_state,
                                   final_state);
            });
    }

    void D3D12RHI::bindFramebufferForSubpass(const RHIRenderPassBeginInfo* pRenderPassBegin,
                                             uint32_t subpass_index,
                                             bool clear_attachments)
    {
        auto* render_pass = static_cast<D3D12RHIRenderPass*>(pRenderPassBegin != nullptr ?
                                                                 pRenderPassBegin->renderPass :
                                                                 m_active_render_pass);
        auto* framebuffer = static_cast<D3D12RHIFramebuffer*>(pRenderPassBegin != nullptr ?
                                                                  pRenderPassBegin->framebuffer :
                                                                  m_active_framebuffer);
        if (m_d3d12_command_list == nullptr ||
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
            if (attachment_index >= framebuffer->attachments.size() ||
                attachment_index >= render_pass->attachments.size())
            {
                continue;
            }

            auto* view = framebuffer->attachments[attachment_index];
            if (view == nullptr || view->image == nullptr || view->image->resource == nullptr)
            {
                continue;
            }

            const D3D12_RESOURCE_STATES shader_read_state =
                view->has_dsv ?
                    (D3D12_RESOURCE_STATE_DEPTH_READ |
                     D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) :
                    (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            transitionResource(m_d3d12_command_list.Get(),
                               view->image->resource.Get(),
                               view->image->current_state,
                               shader_read_state);
        }

        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv_handles;
        rtv_handles.reserve(subpass.color_attachment_indices.size());

        for (uint32_t attachment_index : subpass.color_attachment_indices)
        {
            if (attachment_index >= framebuffer->attachments.size() ||
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
                transitionResource(m_d3d12_command_list.Get(),
                                   view->image->resource.Get(),
                                   view->image->current_state,
                                   D3D12_RESOURCE_STATE_RENDER_TARGET);
            }
            else
            {
                const uint32_t back_buffer_index = m_current_frame_index % m_swapchain_buffer_count;
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
                    m_d3d12_command_list->ResourceBarrier(1, &barrier);
                }
            }

            rtv_handles.push_back(view->cpu_descriptor);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle {};
        const bool has_depth_attachment =
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
                        depth_read_only ?
                            (D3D12_RESOURCE_STATE_DEPTH_READ |
                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) :
                            D3D12_RESOURCE_STATE_DEPTH_WRITE;
                    transitionResource(m_d3d12_command_list.Get(),
                                       depth_view->image->resource.Get(),
                                       depth_view->image->current_state,
                                       depth_state);
                }
            }
        }

        m_d3d12_command_list->OMSetRenderTargets(static_cast<UINT>(rtv_handles.size()),
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

        m_d3d12_command_list->RSSetViewports(1, &d3d_viewport);
        m_d3d12_command_list->RSSetScissorRects(1, &d3d_scissor);

        if (!clear_attachments ||
            pRenderPassBegin == nullptr ||
            pRenderPassBegin->pClearValues == nullptr ||
            pRenderPassBegin->clearValueCount == 0)
        {
            return;
        }

        for (uint32_t color_slot = 0; color_slot < subpass.color_attachment_indices.size(); ++color_slot)
        {
            const uint32_t attachment_index = subpass.color_attachment_indices[color_slot];
            if (attachment_index >= pRenderPassBegin->clearValueCount ||
                attachment_index >= render_pass->attachments.size() ||
                color_slot >= rtv_handles.size() ||
                render_pass->attachments[attachment_index].loadOp != RHI_ATTACHMENT_LOAD_OP_CLEAR)
            {
                continue;
            }

            const auto& clear_color = pRenderPassBegin->pClearValues[attachment_index].color;
            const FLOAT color[4] = {clear_color.float32[0],
                                    clear_color.float32[1],
                                    clear_color.float32[2],
                                    clear_color.float32[3]};
            m_d3d12_command_list->ClearRenderTargetView(rtv_handles[color_slot], color, 0, nullptr);
        }

        if (has_depth_attachment &&
            dsv_handle.ptr != 0 &&
            subpass.depth_attachment_index < pRenderPassBegin->clearValueCount &&
            (render_pass->attachments[subpass.depth_attachment_index].loadOp == RHI_ATTACHMENT_LOAD_OP_CLEAR ||
             render_pass->attachments[subpass.depth_attachment_index].stencilLoadOp == RHI_ATTACHMENT_LOAD_OP_CLEAR))
        {
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
            m_d3d12_command_list->ClearDepthStencilView(dsv_handle,
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

        resolvePendingTextureReadbacks();
        m_pending_upload_buffers.clear();
    }
#endif
} // namespace Piccolo
