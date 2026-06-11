#include "runtime/function/render/interface/d3d12/d3d12_rhi.h"

#include "runtime/core/base/macro.h"
#include "runtime/function/render/window_system.h"

#include <algorithm>
#include <array>
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
    namespace
    {
#ifdef _WIN32
        struct D3D12RHIPipelineLayout;
        struct D3D12RHIDescriptorSet;

        struct D3D12RHIBuffer final : RHIBuffer
        {
            ComPtr<ID3D12Resource> resource;
            RHIDeviceSize          size {0};
            RHIBufferUsageFlags    usage {0};
            RHIMemoryPropertyFlags memory_properties {0};
            D3D12_HEAP_TYPE        heap_type {D3D12_HEAP_TYPE_DEFAULT};
            D3D12_RESOURCE_STATES  current_state {D3D12_RESOURCE_STATE_COMMON};
            std::vector<uint8_t>   host_data;
            bool                   host_data_valid {false};
            bool                   host_data_write_mapped {false};
            bool                   host_data_uploadable {false};
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
            std::vector<D3D12_RESOURCE_STATES> subresource_states;
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
            bool                 host_data_valid {false};
            bool                 host_data_write_mapped {false};
            bool                 host_data_uploadable {false};
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

        struct D3D12RHIAccelerationStructure final : RHIAccelerationStructure
        {
#ifdef _WIN32
            ComPtr<ID3D12Resource> result;
            ComPtr<ID3D12Resource> scratch;
            ComPtr<ID3D12Resource> instance_upload;
            D3D12_GPU_VIRTUAL_ADDRESS gpu_address {0};
#else
            uint64_t gpu_address {0};
#endif
            RHIAccelerationStructureType type {RHIAccelerationStructureType::BottomLevel};
            bool                         allow_update {false};
            uint64_t                     result_size {0};
            uint64_t                     scratch_size {0};
            uint64_t                     update_scratch_size {0};
        };

        struct D3D12RHIShaderBindingTable final : RHIShaderBindingTable
        {
#ifdef _WIN32
            ComPtr<ID3D12Resource> resource;
            D3D12_GPU_VIRTUAL_ADDRESS raygen_start {0};
            uint64_t raygen_size {0};
            D3D12_GPU_VIRTUAL_ADDRESS miss_start {0};
            uint64_t miss_size {0};
            uint64_t miss_stride {0};
            D3D12_GPU_VIRTUAL_ADDRESS hit_group_start {0};
            uint64_t hit_group_size {0};
            uint64_t hit_group_stride {0};
#endif
        };

        struct D3D12RHICommandBuffer final : RHICommandBuffer
        {
#ifdef _WIN32
            struct DynamicDescriptorTableCacheEntry
            {
                const D3D12RHIDescriptorSet* descriptor_set {nullptr};
                uint64_t                     descriptor_set_version {0};
                uint32_t                     set_index {0};
                std::vector<uint32_t>        dynamic_offsets;
                D3D12_GPU_DESCRIPTOR_HANDLE  cbv_srv_uav_gpu_base {0};
            };

            ComPtr<ID3D12CommandAllocator>    command_allocator;
            ComPtr<ID3D12GraphicsCommandList> command_list;
            bool                              is_open {false};
            bool                              has_recorded_commands {false};
            bool                              in_render_pass {false};
            RHIPipeline*                      bound_graphics_pipeline {nullptr};
            D3D12RHIPipelineLayout*           bound_graphics_pipeline_layout {nullptr};
            D3D12RHIPipelineLayout*           bound_compute_pipeline_layout {nullptr};
            D3D12RHIPipelineLayout*           bound_ray_tracing_pipeline_layout {nullptr};
            ID3D12RootSignature*              bound_graphics_root_signature {nullptr};
            ID3D12RootSignature*              bound_compute_root_signature {nullptr};
            ID3D12RootSignature*              bound_ray_tracing_root_signature {nullptr};
            bool                              graphics_root_signature_dirty {true};
            bool                              compute_root_signature_dirty {true};
            bool                              ray_tracing_root_signature_dirty {true};
            RHIRenderPass*                    active_render_pass {nullptr};
            RHIFramebuffer*                   active_framebuffer {nullptr};
            RHIRenderPassBeginInfo            active_render_pass_begin_info {};
            std::vector<RHIClearValue>        active_clear_values;
            std::vector<bool>                 attachment_load_ops_applied;
            uint32_t                          active_subpass_index {0};
            uint32_t                          transient_cbv_srv_uav_descriptor_next {0};
            bool                              descriptor_heaps_dirty {true};
            ID3D12DescriptorHeap*             bound_cbv_srv_uav_heap {nullptr};
            ID3D12DescriptorHeap*             bound_sampler_heap {nullptr};
            std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> graphics_root_descriptor_tables;
            std::vector<bool>                 graphics_root_descriptor_table_valid;
            std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> compute_root_descriptor_tables;
            std::vector<bool>                 compute_root_descriptor_table_valid;
            std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> ray_tracing_root_descriptor_tables;
            std::vector<bool>                 ray_tracing_root_descriptor_table_valid;
            std::vector<DynamicDescriptorTableCacheEntry> dynamic_descriptor_table_cache;
            ComPtr<ID3D12Resource>            dispatch_argument_buffer;
            D3D12_RESOURCE_STATES             dispatch_argument_buffer_state {D3D12_RESOURCE_STATE_COPY_DEST};
#endif
            bool owns_recording {false};
        };

        struct D3D12RHICommandPool final : RHICommandPool
        {
        };

        struct D3D12RHIQueue final : RHIQueue
        {
#ifdef _WIN32
            ID3D12CommandQueue* command_queue {nullptr};
            D3D12_COMMAND_LIST_TYPE command_list_type {D3D12_COMMAND_LIST_TYPE_DIRECT};
#endif
        };

        struct D3D12RHIFence final : RHIFence
        {
#ifdef _WIN32
            ~D3D12RHIFence()
            {
                if (event != nullptr)
                {
                    CloseHandle(event);
                    event = nullptr;
                }
            }

            ComPtr<ID3D12Fence> fence;
            HANDLE              event {nullptr};
            uint64_t            next_signal_value {0};
            uint64_t            wait_value {0};
            bool                has_pending_signal {false};
            bool                signaled {false};
#endif
        };

        struct D3D12RHISemaphore final : RHISemaphore
        {
#ifdef _WIN32
            ~D3D12RHISemaphore()
            {
                if (event != nullptr)
                {
                    CloseHandle(event);
                    event = nullptr;
                }
            }

            ComPtr<ID3D12Fence> fence;
            HANDLE              event {nullptr};
            uint64_t            next_signal_value {0};
            uint64_t            wait_value {0};
            bool                has_pending_signal {false};
#endif
        };

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

        struct D3D12RHIDescriptorPool final : RHIDescriptorPool
        {
            bool enforce_limits {false};
            uint32_t max_sets {0};
            uint32_t allocated_sets {0};
            std::array<uint32_t, 12> descriptor_type_counts {};
            std::array<uint32_t, 12> allocated_descriptor_type_counts {};
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
            std::array<uint32_t, 12> descriptor_type_counts {};
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

            struct AccelerationStructureDescriptor
            {
                uint32_t binding {0};
                uint32_t array_element {0};
                RHIDescriptorType descriptor_type {RHI_DESCRIPTOR_TYPE_MAX_ENUM};
                D3D12RHIAccelerationStructure* acceleration_structure {nullptr};
#ifdef _WIN32
                D3D12_GPU_VIRTUAL_ADDRESS gpu_address {0};
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
            D3D12_CPU_DESCRIPTOR_HANDLE cbv_srv_uav_staging_cpu_base {0};
            D3D12_CPU_DESCRIPTOR_HANDLE sampler_cpu_base {0};
            D3D12_GPU_DESCRIPTOR_HANDLE sampler_gpu_base {0};
            D3D12_CPU_DESCRIPTOR_HANDLE sampler_staging_cpu_base {0};
#endif
            uint64_t version {1};
            std::vector<D3D12RHIBuffer*> storage_buffers;
            std::vector<D3D12RHIBuffer*> host_visible_default_buffers;
            std::vector<BufferDescriptor> buffer_descriptors;
            std::vector<AccelerationStructureDescriptor> acceleration_structure_descriptors;

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

            AccelerationStructureDescriptor* findAccelerationStructureDescriptor(uint32_t binding,
                                                                                 uint32_t array_element)
            {
                for (auto& descriptor : acceleration_structure_descriptors)
                {
                    if (descriptor.binding == binding && descriptor.array_element == array_element)
                    {
                        return &descriptor;
                    }
                }
                return nullptr;
            }

            const AccelerationStructureDescriptor*
            findAccelerationStructureDescriptor(uint32_t binding, uint32_t array_element) const
            {
                for (const auto& descriptor : acceleration_structure_descriptors)
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
                std::vector<RHIImageLayout> color_attachment_layouts;
                std::vector<uint32_t> resolve_attachment_indices;
                std::vector<RHIImageLayout> resolve_attachment_layouts;
                std::vector<uint32_t> preserve_attachment_indices;
                uint32_t depth_attachment_index {(std::numeric_limits<uint32_t>::max)()};
                RHIImageLayout depth_attachment_layout {RHI_IMAGE_LAYOUT_UNDEFINED};
            };

            std::vector<RHIAttachmentDescription> attachments;
            std::vector<uint32_t> color_attachment_indices;
            uint32_t depth_attachment_index {(std::numeric_limits<uint32_t>::max)()};
            std::vector<SubpassInfo> subpasses;
            std::vector<RHISubpassDependency> dependencies;
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
#if PICCOLO_D3D12_HAS_DXR
            ComPtr<ID3D12StateObject> state_object;
            ComPtr<ID3D12StateObjectProperties> state_object_properties;
#endif
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

        constexpr uint32_t kTrackedDescriptorTypeCount = 12;

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

        void logD3D12InfoQueueMessages(ID3D12Device* device, const char* context, UINT64 max_messages = 16)
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
                (hasFlag(access, RHI_ACCESS_SHADER_READ_BIT) || hasFlag(access, RHI_ACCESS_SHADER_WRITE_BIT)))
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

            command_buffer.dispatch_argument_buffer_state = D3D12_RESOURCE_STATE_COPY_DEST;
            return SUCCEEDED(device->CreateCommittedResource(&heap_properties,
                                                             D3D12_HEAP_FLAG_NONE,
                                                             &resource_desc,
                                                             command_buffer.dispatch_argument_buffer_state,
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
                                  2048,
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
        // D3D12 command buffers are currently backed by DIRECT command lists. Keep compute submissions on
        // the direct queue until command pools can allocate queue-typed command lists and resource ownership
        // transitions are modeled explicitly.
        d3d_compute_queue->command_queue     = m_d3d12_command_queue.Get();
        d3d_compute_queue->command_list_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        LOG_INFO("D3D12 compute queue uses the graphics/direct command queue");
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

        RHISemaphoreCreateInfo texture_copy_semaphore_info {};
        if (!createSemaphore(&texture_copy_semaphore_info, m_texture_copy_semaphore))
        {
            throw std::runtime_error("Failed to create D3D12 texture copy semaphore");
        }

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

        delete static_cast<D3D12RHISemaphore*>(m_texture_copy_semaphore);
        m_texture_copy_semaphore = nullptr;

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

bool D3D12RHI::waitForFencesPFN(uint32_t fenceCount, RHIFence* const* pFence, RHIBool32 waitAll, uint64_t timeout)
{
#ifdef _WIN32
    if (fenceCount == 0)
    {
        return true;
    }
    if (pFence == nullptr)
    {
        return false;
    }

    const ULONGLONG start_tick = timeout == UINT64_MAX ? 0ULL : GetTickCount64();
    if (!waitAll)
    {
        std::vector<HANDLE>        wait_events;
        std::vector<D3D12RHIFence*> wait_fences;
        wait_events.reserve(fenceCount);
        wait_fences.reserve(fenceCount);

        for (uint32_t i = 0; i < fenceCount; ++i)
        {
            auto* fence = static_cast<D3D12RHIFence*>(pFence[i]);
            if (fence == nullptr || fence->fence == nullptr || fence->event == nullptr)
            {
                return false;
            }

            if (fence->fence->GetCompletedValue() >= fence->wait_value)
            {
                fence->has_pending_signal = false;
                fence->signaled           = true;
                return true;
            }
            if (FAILED(fence->fence->SetEventOnCompletion(fence->wait_value, fence->event)))
            {
                return false;
            }
            wait_events.push_back(fence->event);
            wait_fences.push_back(fence);
        }

        if (wait_events.size() <= MAXIMUM_WAIT_OBJECTS)
        {
            const DWORD wait_result = WaitForMultipleObjects(static_cast<DWORD>(wait_events.size()),
                                                             wait_events.data(),
                                                             FALSE,
                                                             d3d12FenceTimeoutMilliseconds(timeout));
            if (wait_result >= WAIT_OBJECT_0 && wait_result < WAIT_OBJECT_0 + wait_events.size())
            {
                D3D12RHIFence* completed_fence = wait_fences[wait_result - WAIT_OBJECT_0];
                completed_fence->has_pending_signal = false;
                completed_fence->signaled           = true;
                return true;
            }
            return false;
        }

        while (timeout == UINT64_MAX || remainingD3D12FenceTimeout(timeout, start_tick) > 0)
        {
            for (D3D12RHIFence* fence : wait_fences)
            {
                if (fence->fence->GetCompletedValue() >= fence->wait_value)
                {
                    fence->has_pending_signal = false;
                    fence->signaled           = true;
                    return true;
                }
            }
            Sleep(1);
        }

        return false;
    }

    for (uint32_t i = 0; i < fenceCount; ++i)
    {
        auto* fence = static_cast<D3D12RHIFence*>(pFence[i]);
        if (fence == nullptr || fence->fence == nullptr)
        {
            return false;
        }

        const bool completed = waitForD3D12FenceValue(fence->fence.Get(),
                                                      fence->event,
                                                      fence->wait_value,
                                                      remainingD3D12FenceTimeout(timeout, start_tick));
        if (!completed)
        {
            return false;
        }

        fence->has_pending_signal = false;
        fence->signaled           = true;
        if (!waitAll)
        {
            return true;
        }
    }
#else
    (void)fenceCount;
    (void)pFence;
    (void)waitAll;
    (void)timeout;
#endif
    return true;
}

bool D3D12RHI::resetFencesPFN(uint32_t fenceCount, RHIFence* const* pFences)
{
#ifdef _WIN32
    if (fenceCount > 0 && pFences == nullptr)
    {
        return false;
    }

    for (uint32_t i = 0; i < fenceCount; ++i)
    {
        auto* fence = static_cast<D3D12RHIFence*>(pFences[i]);
        if (fence == nullptr || fence->fence == nullptr)
        {
            return false;
        }

        fence->wait_value         = fence->next_signal_value + 1ULL;
        fence->has_pending_signal = true;
        fence->signaled           = false;
    }
#else
    (void)fenceCount;
    (void)pFences;
#endif
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
    (void)pBeginInfo;
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    if (d3d_command_buffer == nullptr || !ensureCommandBufferObjects(commandBuffer))
    {
        return false;
    }

    if (d3d_command_buffer->is_open)
    {
        return true;
    }

    if (FAILED(d3d_command_buffer->command_allocator->Reset()))
    {
        return false;
    }

    if (FAILED(d3d_command_buffer->command_list->Reset(d3d_command_buffer->command_allocator.Get(), nullptr)))
    {
        return false;
    }

    d3d_command_buffer->is_open = true;
    d3d_command_buffer->has_recorded_commands = false;
    d3d_command_buffer->in_render_pass = false;
    d3d_command_buffer->bound_graphics_pipeline = nullptr;
    d3d_command_buffer->bound_graphics_pipeline_layout = nullptr;
    d3d_command_buffer->bound_compute_pipeline_layout = nullptr;
    d3d_command_buffer->bound_ray_tracing_pipeline_layout = nullptr;
    d3d_command_buffer->bound_graphics_root_signature = nullptr;
    d3d_command_buffer->bound_compute_root_signature = nullptr;
    d3d_command_buffer->bound_ray_tracing_root_signature = nullptr;
    d3d_command_buffer->active_render_pass = nullptr;
    d3d_command_buffer->active_framebuffer = nullptr;
    d3d_command_buffer->active_render_pass_begin_info = {};
    d3d_command_buffer->active_clear_values.clear();
    d3d_command_buffer->attachment_load_ops_applied.clear();
    d3d_command_buffer->active_subpass_index = 0;
    d3d_command_buffer->transient_cbv_srv_uav_descriptor_next = m_d3d12_transient_cbv_srv_uav_descriptor_next;
    d3d_command_buffer->dynamic_descriptor_table_cache.clear();
    resetCommandBufferDescriptorHeapState(*d3d_command_buffer);
    return true;
#else
    (void)commandBuffer;
    return true;
#endif
}

bool D3D12RHI::endCommandBufferPFN(RHICommandBuffer* commandBuffer)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    if (d3d_command_buffer == nullptr || d3d_command_buffer->command_list == nullptr)
    {
        return false;
    }

    if (!d3d_command_buffer->is_open)
    {
        return true;
    }

    if (FAILED(d3d_command_buffer->command_list->Close()))
    {
        return false;
    }

    d3d_command_buffer->is_open = false;
    d3d_command_buffer->has_recorded_commands = true;
    return true;
#else
    (void)commandBuffer;
    return true;
#endif
}

void D3D12RHI::cmdBeginRenderPassPFN(RHICommandBuffer* commandBuffer, const RHIRenderPassBeginInfo* pRenderPassBegin, RHISubpassContents contents)
{
    (void)contents;
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (d3d_command_buffer == nullptr || command_list == nullptr)
    {
        if (commandBuffer != nullptr || pRenderPassBegin != nullptr)
        {
            LOG_WARN("D3D12 cmdBeginRenderPass skipped because no command list is available");
        }
        return;
    }

    if (pRenderPassBegin == nullptr)
    {
        if (commandBuffer != nullptr)
        {
            LOG_WARN("D3D12 cmdBeginRenderPass skipped because render pass begin info is null");
        }
        return;
    }

    auto* render_pass = static_cast<D3D12RHIRenderPass*>(pRenderPassBegin->renderPass);
    auto* framebuffer = static_cast<D3D12RHIFramebuffer*>(pRenderPassBegin->framebuffer);
    if (render_pass == nullptr || framebuffer == nullptr)
    {
        LOG_WARN("D3D12 cmdBeginRenderPass skipped because render pass or framebuffer is invalid");
        return;
    }

    d3d_command_buffer->active_render_pass      = pRenderPassBegin->renderPass;
    d3d_command_buffer->active_framebuffer      = pRenderPassBegin->framebuffer;
    d3d_command_buffer->active_render_pass_begin_info = *pRenderPassBegin;
    d3d_command_buffer->active_clear_values.clear();
    if (pRenderPassBegin->pClearValues != nullptr && pRenderPassBegin->clearValueCount > 0)
    {
        d3d_command_buffer->active_clear_values.assign(pRenderPassBegin->pClearValues,
                                                       pRenderPassBegin->pClearValues + pRenderPassBegin->clearValueCount);
        d3d_command_buffer->active_render_pass_begin_info.pClearValues =
            d3d_command_buffer->active_clear_values.data();
    }
    else
    {
        d3d_command_buffer->active_render_pass_begin_info.clearValueCount = 0;
        d3d_command_buffer->active_render_pass_begin_info.pClearValues    = nullptr;
    }

    d3d_command_buffer->attachment_load_ops_applied.assign(render_pass != nullptr ?
                                                                render_pass->attachments.size() :
                                                                0,
                                                           false);
    d3d_command_buffer->active_subpass_index    = 0;
    bindFramebufferForSubpass(commandBuffer,
                              command_list,
                              &d3d_command_buffer->active_render_pass_begin_info,
                              d3d_command_buffer->active_subpass_index,
                              true);
    d3d_command_buffer->in_render_pass = true;
#else
    (void)commandBuffer;
    (void)pRenderPassBegin;
#endif
}

void D3D12RHI::cmdNextSubpassPFN(RHICommandBuffer* commandBuffer, RHISubpassContents contents)
{
    (void)contents;
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (d3d_command_buffer == nullptr || command_list == nullptr || !d3d_command_buffer->in_render_pass)
    {
        if (commandBuffer != nullptr)
        {
            LOG_WARN("D3D12 cmdNextSubpass skipped because no active D3D12 render pass command list is available");
        }
        return;
    }

    auto* render_pass = static_cast<D3D12RHIRenderPass*>(d3d_command_buffer->active_render_pass);
    auto* framebuffer = static_cast<D3D12RHIFramebuffer*>(d3d_command_buffer->active_framebuffer);
    finishD3D12Subpass(command_list,
                       render_pass,
                       framebuffer,
                       d3d_command_buffer->active_subpass_index);

    const uint32_t previous_subpass_index = d3d_command_buffer->active_subpass_index;
    ++d3d_command_buffer->active_subpass_index;
    if (render_pass == nullptr || d3d_command_buffer->active_subpass_index >= render_pass->subpasses.size())
    {
        LOG_WARN("D3D12 cmdNextSubpass skipped because subpass {} is outside the active render pass",
                 d3d_command_buffer->active_subpass_index);
        return;
    }

    transitionD3D12SubpassBoundary(command_list,
                                   render_pass,
                                   framebuffer,
                                   previous_subpass_index,
                                   d3d_command_buffer->active_subpass_index);

    bindFramebufferForSubpass(commandBuffer,
                              command_list,
                              &d3d_command_buffer->active_render_pass_begin_info,
                              d3d_command_buffer->active_subpass_index,
                              true);
#else
    (void)commandBuffer;
#endif
}

void D3D12RHI::cmdEndRenderPassPFN(RHICommandBuffer* commandBuffer)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (d3d_command_buffer == nullptr || command_list == nullptr)
    {
        if (commandBuffer != nullptr)
        {
            LOG_WARN("D3D12 cmdEndRenderPass skipped because no command list is available");
        }
        return;
    }

    auto* render_pass = static_cast<D3D12RHIRenderPass*>(d3d_command_buffer->active_render_pass);
    auto* framebuffer = static_cast<D3D12RHIFramebuffer*>(d3d_command_buffer->active_framebuffer);
    if (render_pass != nullptr && framebuffer != nullptr)
    {
        finishD3D12Subpass(command_list,
                           render_pass,
                           framebuffer,
                           d3d_command_buffer->active_subpass_index);

        for (uint32_t attachment_index = 0; attachment_index < framebuffer->attachments.size(); ++attachment_index)
        {
            if (attachment_index >= render_pass->attachments.size())
            {
                continue;
            }

            auto* view = framebuffer->attachments[attachment_index];
            const D3D12_RESOURCE_STATES final_state =
                subpassAttachmentState(view, render_pass->attachments[attachment_index].finalLayout);
            if (view != nullptr && view->image != nullptr && view->image->resource != nullptr)
            {
                transitionImageView(command_list, view, final_state);
            }
            else if (view != nullptr &&
                     view->has_rtv &&
                     render_pass->attachments[attachment_index].finalLayout == RHI_IMAGE_LAYOUT_PRESENT_SRC_KHR)
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
                    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
                    command_list->ResourceBarrier(1, &barrier);
                }
            }
        }
    }
    else if (d3d_command_buffer->in_render_pass)
    {
        LOG_WARN("D3D12 cmdEndRenderPass could not finish attachment transitions because active render pass or framebuffer is missing");
    }

    d3d_command_buffer->in_render_pass = false;
    d3d_command_buffer->active_render_pass = nullptr;
    d3d_command_buffer->active_framebuffer = nullptr;
    d3d_command_buffer->active_render_pass_begin_info = {};
    d3d_command_buffer->active_clear_values.clear();
    d3d_command_buffer->attachment_load_ops_applied.clear();
    d3d_command_buffer->active_subpass_index = 0;
#else
    (void)commandBuffer;
#endif
}

void D3D12RHI::cmdBindPipelinePFN(RHICommandBuffer* commandBuffer, RHIPipelineBindPoint pipelineBindPoint, RHIPipeline* pipeline)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    auto* d3d_pipeline = static_cast<D3D12RHIPipeline*>(pipeline);
    if (d3d_command_buffer == nullptr || command_list == nullptr || d3d_pipeline == nullptr)
    {
        return;
    }

    if (pipelineBindPoint == RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
    {
#if PICCOLO_D3D12_HAS_DXR
        ComPtr<ID3D12GraphicsCommandList4> command_list4;
        if (FAILED(command_list->QueryInterface(IID_PPV_ARGS(&command_list4))) || command_list4 == nullptr)
        {
            LOG_WARN("D3D12 cmdBindPipeline skipped ray tracing pipeline because command list4 is unavailable");
            return;
        }
        if (d3d_pipeline->state_object != nullptr)
        {
            command_list4->SetPipelineState1(d3d_pipeline->state_object.Get());
        }
        if (d3d_pipeline->layout != nullptr && d3d_pipeline->layout->root_signature != nullptr)
        {
            auto* root_signature = d3d_pipeline->layout->root_signature.Get();
            if (d3d_command_buffer->bound_ray_tracing_pipeline_layout != d3d_pipeline->layout ||
                d3d_command_buffer->bound_ray_tracing_root_signature != root_signature)
            {
                clearRootDescriptorTableCache(*d3d_command_buffer, RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR);
            }
            d3d_command_buffer->bound_ray_tracing_pipeline_layout = d3d_pipeline->layout;
            d3d_command_buffer->bound_ray_tracing_root_signature = root_signature;
            command_list->SetComputeRootSignature(root_signature);
            d3d_command_buffer->ray_tracing_root_signature_dirty = false;
        }
#else
        LOG_WARN("D3D12 cmdBindPipeline skipped ray tracing pipeline because this SDK does not expose DXR");
#endif
        return;
    }

    if (d3d_pipeline->pipeline_state != nullptr)
    {
        command_list->SetPipelineState(d3d_pipeline->pipeline_state.Get());
    }
    if (d3d_pipeline->layout != nullptr && d3d_pipeline->layout->root_signature != nullptr)
    {
        auto* root_signature = d3d_pipeline->layout->root_signature.Get();
        if (pipelineBindPoint == RHI_PIPELINE_BIND_POINT_COMPUTE)
        {
            if (d3d_command_buffer->bound_compute_pipeline_layout != d3d_pipeline->layout ||
                d3d_command_buffer->bound_compute_root_signature != root_signature)
            {
                clearRootDescriptorTableCache(*d3d_command_buffer, RHI_PIPELINE_BIND_POINT_COMPUTE);
            }
            d3d_command_buffer->bound_compute_pipeline_layout = d3d_pipeline->layout;
            d3d_command_buffer->bound_compute_root_signature = root_signature;
            command_list->SetComputeRootSignature(root_signature);
            d3d_command_buffer->compute_root_signature_dirty = false;
        }
        else
        {
            if (d3d_command_buffer->bound_graphics_pipeline_layout != d3d_pipeline->layout ||
                d3d_command_buffer->bound_graphics_root_signature != root_signature)
            {
                clearRootDescriptorTableCache(*d3d_command_buffer, RHI_PIPELINE_BIND_POINT_GRAPHICS);
            }
            d3d_command_buffer->bound_graphics_pipeline = pipeline;
            d3d_command_buffer->bound_graphics_pipeline_layout = d3d_pipeline->layout;
            d3d_command_buffer->bound_graphics_root_signature = root_signature;
            command_list->SetGraphicsRootSignature(root_signature);
            d3d_command_buffer->graphics_root_signature_dirty = false;
            command_list->IASetPrimitiveTopology(d3d_pipeline->primitive_topology);
        }
    }
#else
    (void)commandBuffer;
    (void)pipelineBindPoint;
    (void)pipeline;
#endif
    return;
}

void D3D12RHI::cmdSetViewportPFN(RHICommandBuffer* commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const RHIViewport* pViewports)
{
#ifdef _WIN32
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (command_list == nullptr || pViewports == nullptr || viewportCount == 0)
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
        command_list->RSSetViewports(viewportCount - firstViewport, d3d_viewports.data() + firstViewport);
    }
#else
    (void)commandBuffer;
    (void)firstViewport;
    (void)viewportCount;
    (void)pViewports;
#endif
}

void D3D12RHI::cmdSetScissorPFN(RHICommandBuffer* commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const RHIRect2D* pScissors)
{
#ifdef _WIN32
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (command_list == nullptr || pScissors == nullptr || scissorCount == 0)
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
        command_list->RSSetScissorRects(scissorCount - firstScissor, d3d_scissors.data() + firstScissor);
    }
#else
    (void)commandBuffer;
    (void)firstScissor;
    (void)scissorCount;
    (void)pScissors;
#endif
}

void D3D12RHI::cmdBindVertexBuffersPFN( RHICommandBuffer* commandBuffer, uint32_t firstBinding, uint32_t bindingCount, RHIBuffer* const* pBuffers, const RHIDeviceSize* pOffsets)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (d3d_command_buffer == nullptr || command_list == nullptr || pBuffers == nullptr || bindingCount == 0)
    {
        return;
    }

    const auto* bound_pipeline = static_cast<const D3D12RHIPipeline*>(d3d_command_buffer->bound_graphics_pipeline);
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
            transitionResource(command_list,
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

    command_list->IASetVertexBuffers(firstBinding, static_cast<UINT>(views.size()), views.data());
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
    auto* command_list = d3d12CommandListFor(commandBuffer);
    auto* d3d_buffer = static_cast<D3D12RHIBuffer*>(buffer);
    if (command_list == nullptr || d3d_buffer == nullptr || d3d_buffer->resource == nullptr)
    {
        return;
    }

    if (d3d_buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT)
    {
        transitionResource(command_list,
                           d3d_buffer->resource.Get(),
                           d3d_buffer->current_state,
                           D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }

    D3D12_INDEX_BUFFER_VIEW view {};
    view.BufferLocation = d3d_buffer->resource->GetGPUVirtualAddress() + offset;
    view.SizeInBytes    = offset < d3d_buffer->size ? static_cast<UINT>(d3d_buffer->size - offset) : 0;
    view.Format         = indexType == RHI_INDEX_TYPE_UINT32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    command_list->IASetIndexBuffer(&view);
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
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    auto* d3d_layout = static_cast<D3D12RHIPipelineLayout*>(layout);
    if (d3d_command_buffer == nullptr ||
        command_list == nullptr ||
        d3d_layout == nullptr ||
        (pDescriptorSets == nullptr && descriptorSetCount > 0))
    {
        if (commandBuffer != nullptr ||
            layout != nullptr ||
            pDescriptorSets != nullptr ||
            descriptorSetCount > 0)
        {
            LOG_WARN("D3D12 cmdBindDescriptorSets skipped because command buffer, command list, layout, or descriptor sets are invalid");
        }
        return;
    }

    uint32_t preflight_dynamic_offset_index = 0;
    uint32_t preflight_transient_next = d3d_command_buffer->transient_cbv_srv_uav_descriptor_next;
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
            m_d3d12_cbv_srv_uav_heap == nullptr ||
            m_d3d12_cbv_srv_uav_cpu_heap == nullptr)
        {
            LOG_WARN("D3D12 cmdBindDescriptorSets skipped dynamic descriptors for set {} (required_dynamic_descriptors={}, provided_dynamic_offsets={}, has_resource_descriptors={})",
                     set_index,
                     required_dynamic_descriptor_count,
                     dynamicOffsetCount,
                     descriptor_set->has_cbv_srv_uav_descriptors);
            return;
        }

        std::vector<uint32_t> dynamic_offsets(pDynamicOffsets + preflight_dynamic_offset_index,
                                              pDynamicOffsets + preflight_dynamic_offset_index +
                                                  required_dynamic_descriptor_count);
        if (findCachedDynamicDescriptorTable(*d3d_command_buffer,
                                             *descriptor_set,
                                             set_index,
                                             dynamic_offsets) == nullptr)
        {
            uint32_t unused_transient_base = 0;
            if (!reserveDescriptors(set_layout->cbv_srv_uav_descriptor_count,
                                    preflight_transient_next,
                                    m_d3d12_cbv_srv_uav_descriptor_capacity,
                                    unused_transient_base))
            {
                LOG_WARN("D3D12 cmdBindDescriptorSets could not reserve {} transient descriptors for set {}",
                         set_layout->cbv_srv_uav_descriptor_count,
                         set_index);
                return;
            }
        }

        preflight_dynamic_offset_index += required_dynamic_descriptor_count;
    }

    bindEngineDescriptorHeaps(command_list,
                              *d3d_command_buffer,
                              m_d3d12_cbv_srv_uav_heap.Get(),
                              m_d3d12_sampler_heap.Get(),
                              true,
                              pipelineBindPoint);

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
                                           command_list,
                                           m_pending_upload_buffers,
                                           *buffer);
            }
        }

        for (const auto& buffer_descriptor : descriptor_set->buffer_descriptors)
        {
            auto* buffer = buffer_descriptor.buffer;
            if (buffer != nullptr && buffer->resource != nullptr && buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT)
            {
                transitionResource(command_list,
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
            LOG_WARN("D3D12 cmdBindDescriptorSets skipped set {} because dynamic offsets are incomplete (required={}, provided={})",
                     set_index,
                     required_dynamic_descriptor_count,
                     dynamicOffsetCount);
            return;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE cbv_srv_uav_gpu_base = descriptor_set->cbv_srv_uav_gpu_base;
        if (has_dynamic_buffer_descriptors &&
            descriptor_set->has_cbv_srv_uav_descriptors &&
            set_layout->cbv_srv_uav_descriptor_count > 0)
        {
            std::vector<uint32_t> dynamic_offsets(pDynamicOffsets + dynamic_offset_index,
                                                  pDynamicOffsets + dynamic_offset_index +
                                                      required_dynamic_descriptor_count);
            if (auto* cached_gpu_base =
                    findCachedDynamicDescriptorTable(*d3d_command_buffer,
                                                     *descriptor_set,
                                                     set_index,
                                                     dynamic_offsets))
            {
                cbv_srv_uav_gpu_base = *cached_gpu_base;
                dynamic_offset_index += required_dynamic_descriptor_count;
            }
            else
            {
                uint32_t transient_base = 0;
                if (reserveDescriptors(set_layout->cbv_srv_uav_descriptor_count,
                                       d3d_command_buffer->transient_cbv_srv_uav_descriptor_next,
                                       m_d3d12_cbv_srv_uav_descriptor_capacity,
                                       transient_base))
                {
                    m_d3d12_transient_cbv_srv_uav_descriptor_next =
                        (std::max)(m_d3d12_transient_cbv_srv_uav_descriptor_next,
                                   d3d_command_buffer->transient_cbv_srv_uav_descriptor_next);

                    D3D12_CPU_DESCRIPTOR_HANDLE transient_cpu_base = cpuDescriptor(m_d3d12_cbv_srv_uav_heap.Get(),
                                                                                  m_d3d12_cbv_srv_uav_descriptor_size,
                                                                                  transient_base);
                    m_d3d12_device->CopyDescriptorsSimple(set_layout->cbv_srv_uav_descriptor_count,
                                                          transient_cpu_base,
                                                          descriptor_set->cbv_srv_uav_staging_cpu_base,
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
                    rememberCachedDynamicDescriptorTable(*d3d_command_buffer,
                                                         *descriptor_set,
                                                         set_index,
                                                         dynamic_offsets,
                                                         cbv_srv_uav_gpu_base);
                }
                else
                {
                    LOG_WARN("D3D12 cmdBindDescriptorSets could not reserve {} transient descriptors for set {}",
                             set_layout->cbv_srv_uav_descriptor_count,
                             set_index);
                    return;
                }
            }
        }
        if (descriptor_set->has_cbv_srv_uav_descriptors &&
            set_index < d3d_layout->cbv_srv_uav_root_parameter_indices.size() &&
            d3d_layout->cbv_srv_uav_root_parameter_indices[set_index] != kInvalidRootParameterIndex)
        {
            const uint32_t root_index = d3d_layout->cbv_srv_uav_root_parameter_indices[set_index];
            if (pipelineBindPoint == RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
            {
                command_list->SetComputeRootDescriptorTable(root_index, cbv_srv_uav_gpu_base);
            }
            else if (pipelineBindPoint == RHI_PIPELINE_BIND_POINT_COMPUTE)
            {
                command_list->SetComputeRootDescriptorTable(root_index, cbv_srv_uav_gpu_base);
            }
            else
            {
                command_list->SetGraphicsRootDescriptorTable(root_index, cbv_srv_uav_gpu_base);
            }
            rememberRootDescriptorTable(*d3d_command_buffer, pipelineBindPoint, root_index, cbv_srv_uav_gpu_base);
        }
        if (descriptor_set->has_sampler_descriptors &&
            set_index < d3d_layout->sampler_root_parameter_indices.size() &&
            d3d_layout->sampler_root_parameter_indices[set_index] != kInvalidRootParameterIndex)
        {
            const uint32_t root_index = d3d_layout->sampler_root_parameter_indices[set_index];
            if (pipelineBindPoint == RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
            {
                command_list->SetComputeRootDescriptorTable(root_index, descriptor_set->sampler_gpu_base);
            }
            else if (pipelineBindPoint == RHI_PIPELINE_BIND_POINT_COMPUTE)
            {
                command_list->SetComputeRootDescriptorTable(root_index, descriptor_set->sampler_gpu_base);
            }
            else
            {
                command_list->SetGraphicsRootDescriptorTable(root_index, descriptor_set->sampler_gpu_base);
            }
            rememberRootDescriptorTable(*d3d_command_buffer,
                                        pipelineBindPoint,
                                        root_index,
                                        descriptor_set->sampler_gpu_base);
        }
    }
#else
    (void)commandBuffer;
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
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (command_list == nullptr)
    {
        if (commandBuffer != nullptr && indexCount > 0 && instanceCount > 0)
        {
            LOG_WARN("D3D12 cmdDrawIndexed skipped because no command list is available");
        }
        return;
    }

    if (d3d_command_buffer != nullptr)
    {
        bindEngineDescriptorHeaps(command_list,
                                  *d3d_command_buffer,
                                  m_d3d12_cbv_srv_uav_heap.Get(),
                                  m_d3d12_sampler_heap.Get(),
                                  true,
                                  RHI_PIPELINE_BIND_POINT_GRAPHICS);
    }
    command_list->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
#else
    (void)commandBuffer;
    (void)indexCount;
    (void)instanceCount;
    (void)firstIndex;
    (void)vertexOffset;
    (void)firstInstance;
#endif
}

void D3D12RHI::cmdClearAttachmentsPFN(RHICommandBuffer* commandBuffer, uint32_t attachmentCount, const RHIClearAttachment* pAttachments, uint32_t rectCount, const RHIClearRect* pRects)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (d3d_command_buffer == nullptr ||
        command_list == nullptr ||
        pAttachments == nullptr ||
        attachmentCount == 0 ||
        (rectCount > 0 && pRects == nullptr))
    {
        return;
    }

    auto* render_pass = static_cast<D3D12RHIRenderPass*>(d3d_command_buffer->active_render_pass);
    auto* framebuffer = static_cast<D3D12RHIFramebuffer*>(d3d_command_buffer->active_framebuffer);
    if (render_pass == nullptr || framebuffer == nullptr || d3d_command_buffer->active_subpass_index >= render_pass->subpasses.size())
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

    const D3D12RHIRenderPass::SubpassInfo& subpass = render_pass->subpasses[d3d_command_buffer->active_subpass_index];
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
            command_list->ClearRenderTargetView(view->cpu_descriptor,
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

            command_list->ClearDepthStencilView(depth_view->cpu_descriptor,
                                                clear_flags,
                                                clear_attachment.clearValue.depthStencil.depth,
                                                static_cast<UINT8>(clear_attachment.clearValue.depthStencil.stencil),
                                                static_cast<UINT>(clear_rects.size()),
                                                clear_rects.empty() ? nullptr : clear_rects.data());
        }
    }
#else
    (void)commandBuffer;
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
    (void)srcImageLayout;
#ifdef _WIN32
    auto* command_list = d3d12CommandListFor(commandBuffer);
    auto* src = static_cast<D3D12RHIImage*>(srcImage);
    auto* dst = static_cast<D3D12RHIBuffer*>(dstBuffer);
    if (regionCount == 0)
    {
        return;
    }
    if (m_d3d12_device == nullptr || command_list == nullptr)
    {
        if (commandBuffer != nullptr || srcImage != nullptr || dstBuffer != nullptr || pRegions != nullptr)
        {
            LOG_WARN("D3D12 cmdCopyImageToBuffer skipped because device or command list is unavailable");
        }
        return;
    }
    if (src == nullptr || dst == nullptr || src->resource == nullptr)
    {
        if (srcImage != nullptr || dstBuffer != nullptr)
        {
            LOG_WARN("D3D12 cmdCopyImageToBuffer skipped because source image or destination buffer is invalid");
        }
        return;
    }
    if (pRegions == nullptr)
    {
        LOG_WARN("D3D12 cmdCopyImageToBuffer skipped because copy regions are null while regionCount is {}",
                 regionCount);
        return;
    }
    if (src->resource_bytes_per_pixel == 0)
    {
        LOG_WARN("D3D12 cmdCopyImageToBuffer skipped because source image has an unknown byte size");
        return;
    }

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
            transitionImageSubresource(command_list,
                                       *src,
                                       subresource,
                                       D3D12_RESOURCE_STATE_COPY_SOURCE);
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

            command_list->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, &source_box);

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
    (void)commandBuffer;
    (void)srcImage;
    (void)dstBuffer;
    (void)regionCount;
    (void)pRegions;
#endif
    return;
}

void D3D12RHI::cmdCopyImageToImage(RHICommandBuffer* commandBuffer, RHIImage* srcImage, RHIImage* dstImage, uint32_t regionCount, const RHIImageBlit* pRegions)
{
#ifdef _WIN32
    auto* command_list = d3d12CommandListFor(commandBuffer);
    auto* src = static_cast<D3D12RHIImage*>(srcImage);
    auto* dst = static_cast<D3D12RHIImage*>(dstImage);
    if (regionCount == 0)
    {
        return;
    }
    if (command_list == nullptr)
    {
        if (commandBuffer != nullptr || srcImage != nullptr || dstImage != nullptr || pRegions != nullptr)
        {
            LOG_WARN("D3D12 cmdCopyImageToImage skipped because no command list is available");
        }
        return;
    }
    if (src == nullptr ||
        dst == nullptr ||
        src->resource == nullptr ||
        dst->resource == nullptr)
    {
        if (srcImage != nullptr || dstImage != nullptr)
        {
            LOG_WARN("D3D12 cmdCopyImageToImage skipped because source or destination image is invalid");
        }
        return;
    }
    if (pRegions == nullptr)
    {
        LOG_WARN("D3D12 cmdCopyImageToImage skipped because copy regions are null while regionCount is {}",
                 regionCount);
        return;
    }

    const auto clamp_offset = [](int32_t value, uint32_t limit) -> UINT {
        if (value <= 0)
        {
            return 0;
        }
        return static_cast<UINT>((std::min)(static_cast<uint32_t>(value), limit));
    };
    const auto mip_dimension = [](uint32_t base, uint32_t mip_level) -> uint32_t {
        if (mip_level >= 31U)
        {
            return 1U;
        }
        return (std::max)(1U, base >> mip_level);
    };

    for (uint32_t region_index = 0; region_index < regionCount; ++region_index)
    {
        const RHIImageBlit& region = pRegions[region_index];
        if (region.srcSubresource.mipLevel >= src->mip_levels ||
            region.dstSubresource.mipLevel >= dst->mip_levels)
        {
            continue;
        }

        const uint32_t layer_count =
            (std::max)(1U,
                       (std::min)((std::max)(1U, region.srcSubresource.layerCount),
                                  (std::max)(1U, region.dstSubresource.layerCount)));
        for (uint32_t layer = 0; layer < layer_count; ++layer)
        {
            const uint32_t src_layer = region.srcSubresource.baseArrayLayer + layer;
            const uint32_t dst_layer = region.dstSubresource.baseArrayLayer + layer;
            if (src_layer >= src->array_layers || dst_layer >= dst->array_layers)
            {
                continue;
            }

            const uint32_t src_width  = mip_dimension(src->width, region.srcSubresource.mipLevel);
            const uint32_t src_height = mip_dimension(src->height, region.srcSubresource.mipLevel);
            const uint32_t dst_width  = mip_dimension(dst->width, region.dstSubresource.mipLevel);
            const uint32_t dst_height = mip_dimension(dst->height, region.dstSubresource.mipLevel);

            D3D12_BOX source_box {};
            source_box.left   = clamp_offset(region.srcOffsets[0].x, src_width);
            source_box.top    = clamp_offset(region.srcOffsets[0].y, src_height);
            source_box.front  = clamp_offset(region.srcOffsets[0].z, 1);
            source_box.right  = clamp_offset(region.srcOffsets[1].x, src_width);
            source_box.bottom = clamp_offset(region.srcOffsets[1].y, src_height);
            source_box.back   = clamp_offset(region.srcOffsets[1].z, 1);
            if (source_box.back <= source_box.front)
            {
                source_box.back = source_box.front + 1;
            }
            if (source_box.back > 1)
            {
                source_box.back = 1;
            }
            if (source_box.left >= source_box.right ||
                source_box.top >= source_box.bottom ||
                source_box.front >= source_box.back)
            {
                continue;
            }

            const UINT dst_x = clamp_offset(region.dstOffsets[0].x, dst_width);
            const UINT dst_y = clamp_offset(region.dstOffsets[0].y, dst_height);
            const UINT dst_z = clamp_offset(region.dstOffsets[0].z, 1);
            if (dst_x >= dst_width || dst_y >= dst_height || dst_z >= 1)
            {
                continue;
            }

            const UINT copy_width  = (std::min)(source_box.right - source_box.left, static_cast<UINT>(dst_width - dst_x));
            const UINT copy_height = (std::min)(source_box.bottom - source_box.top, static_cast<UINT>(dst_height - dst_y));
            const UINT copy_depth  = (std::min)(source_box.back - source_box.front, static_cast<UINT>(1 - dst_z));
            if (copy_width == 0 || copy_height == 0 || copy_depth == 0)
            {
                continue;
            }
            source_box.right  = source_box.left + copy_width;
            source_box.bottom = source_box.top + copy_height;
            source_box.back   = source_box.front + copy_depth;

            D3D12_TEXTURE_COPY_LOCATION src_location {};
            src_location.pResource        = src->resource.Get();
            src_location.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src_location.SubresourceIndex = d3d12SubresourceIndex(*src, region.srcSubresource.mipLevel, src_layer);
            transitionImageSubresource(command_list,
                                       *src,
                                       src_location.SubresourceIndex,
                                       D3D12_RESOURCE_STATE_COPY_SOURCE);

            D3D12_TEXTURE_COPY_LOCATION dst_location {};
            dst_location.pResource        = dst->resource.Get();
            dst_location.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst_location.SubresourceIndex = d3d12SubresourceIndex(*dst, region.dstSubresource.mipLevel, dst_layer);
            transitionImageSubresource(command_list,
                                       *dst,
                                       dst_location.SubresourceIndex,
                                       D3D12_RESOURCE_STATE_COPY_DEST);

            command_list->CopyTextureRegion(&dst_location, dst_x, dst_y, dst_z, &src_location, &source_box);
        }
    }
#else
    (void)commandBuffer;
    (void)srcImage;
    (void)dstImage;
    (void)regionCount;
    (void)pRegions;
#endif
    return;
}

void D3D12RHI::cmdCopyBuffer(RHICommandBuffer* commandBuffer, RHIBuffer* srcBuffer, RHIBuffer* dstBuffer, uint32_t regionCount, RHIBufferCopy* pRegions)
{
#ifdef _WIN32
    auto* command_list = d3d12CommandListFor(commandBuffer);
    auto* src = static_cast<D3D12RHIBuffer*>(srcBuffer);
    auto* dst = static_cast<D3D12RHIBuffer*>(dstBuffer);
    if (command_list == nullptr || src == nullptr || dst == nullptr)
    {
        LOG_ERROR("D3D12 cmdCopyBuffer requires an open command list and valid buffers");
        return;
    }
    if (src->resource == nullptr || dst->resource == nullptr)
    {
        LOG_ERROR("D3D12 cmdCopyBuffer requires GPU resources");
        return;
    }
    if (dst->heap_type == D3D12_HEAP_TYPE_UPLOAD)
    {
        LOG_ERROR("D3D12 cmdCopyBuffer cannot copy into an upload heap destination");
        return;
    }

    RHIBufferCopy default_region {};
    const RHIBufferCopy* regions = pRegions;
    if (regions == nullptr || regionCount == 0)
    {
        default_region.srcOffset = 0;
        default_region.dstOffset = 0;
        default_region.size = (std::min)(src->size, dst->size);
        regions = &default_region;
        regionCount = default_region.size > 0 ? 1 : 0;
    }

    for (uint32_t i = 0; i < regionCount; ++i)
    {
        const RHIBufferCopy& region = regions[i];
        if (region.srcOffset > src->size ||
            region.dstOffset > dst->size ||
            region.size > src->size - region.srcOffset ||
            region.size > dst->size - region.dstOffset)
        {
            LOG_ERROR("D3D12 cmdCopyBuffer skipped invalid copy region");
            continue;
        }

        const bool src_host_data_valid = src->host_data_valid;
        const bool dst_host_data_valid = dst->host_data_valid;
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
        command_list->CopyBufferRegion(dst->resource.Get(),
                                       region.dstOffset,
                                       src->resource.Get(),
                                       region.srcOffset,
                                       region.size);
        dst->map_host_data = false;
        dst->host_data_write_mapped = false;
        updateBufferHostMirrorAfterCopy(*src,
                                        *dst,
                                        src_host_data_valid,
                                        dst_host_data_valid,
                                        region.srcOffset,
                                        region.dstOffset,
                                        region.size,
                                        "D3D12 cmdCopyBuffer");
    }
#else
    (void)commandBuffer;
    if (srcBuffer == nullptr || dstBuffer == nullptr)
    {
        return;
    }

    auto* src = static_cast<D3D12RHIBuffer*>(srcBuffer);
    auto* dst = static_cast<D3D12RHIBuffer*>(dstBuffer);
    if (src == nullptr || dst == nullptr)
    {
        return;
    }

    RHIBufferCopy default_region {};
    const RHIBufferCopy* regions = pRegions;
    if (pRegions == nullptr || regionCount == 0)
    {
        default_region.srcOffset = 0;
        default_region.dstOffset = 0;
        default_region.size = (std::min)(static_cast<RHIDeviceSize>(src->host_data.size()),
                                         static_cast<RHIDeviceSize>(dst->host_data.size()));
        regions = &default_region;
        regionCount = default_region.size > 0 ? 1 : 0;
    }

    for (uint32_t i = 0; i < regionCount; ++i)
    {
        const RHIBufferCopy& region = regions[i];
        if (region.srcOffset <= src->host_data.size() &&
            region.dstOffset <= dst->host_data.size() &&
            region.size <= src->host_data.size() - region.srcOffset &&
            region.size <= dst->host_data.size() - region.dstOffset)
        {
            updateBufferHostMirrorAfterCopy(*src,
                                            *dst,
                                            src->host_data_valid,
                                            dst->host_data_valid,
                                            region.srcOffset,
                                            region.dstOffset,
                                            region.size,
                                            "D3D12 cmdCopyBuffer");
        }
    }
#endif
    return;
}

void D3D12RHI::cmdDraw(RHICommandBuffer* commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    if (auto* command_list = d3d12CommandListFor(commandBuffer))
    {
        if (d3d_command_buffer != nullptr)
        {
            bindEngineDescriptorHeaps(command_list,
                                      *d3d_command_buffer,
                                      m_d3d12_cbv_srv_uav_heap.Get(),
                                      m_d3d12_sampler_heap.Get(),
                                      true,
                                      RHI_PIPELINE_BIND_POINT_GRAPHICS);
        }
        command_list->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
    }
    else if (commandBuffer != nullptr && vertexCount > 0 && instanceCount > 0)
    {
        LOG_WARN("D3D12 cmdDraw skipped because no command list is available");
    }
#else
    (void)commandBuffer;
    (void)vertexCount;
    (void)instanceCount;
    (void)firstVertex;
    (void)firstInstance;
#endif
    return;
}

void D3D12RHI::cmdDispatch(RHICommandBuffer* commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    if (auto* command_list = d3d12CommandListFor(commandBuffer))
    {
        if (d3d_command_buffer != nullptr)
        {
            bindEngineDescriptorHeaps(command_list,
                                      *d3d_command_buffer,
                                      m_d3d12_cbv_srv_uav_heap.Get(),
                                      m_d3d12_sampler_heap.Get(),
                                      true,
                                      RHI_PIPELINE_BIND_POINT_COMPUTE);
        }
        command_list->Dispatch(groupCountX, groupCountY, groupCountZ);
    }
    else if (commandBuffer != nullptr && groupCountX > 0 && groupCountY > 0 && groupCountZ > 0)
    {
        LOG_WARN("D3D12 cmdDispatch skipped because no command list is available");
    }
#else
    (void)commandBuffer;
    (void)groupCountX;
    (void)groupCountY;
    (void)groupCountZ;
#endif
    return;
}

void D3D12RHI::cmdDispatchIndirect(RHICommandBuffer* commandBuffer, RHIBuffer* buffer, RHIDeviceSize offset)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    auto* d3d_buffer = static_cast<D3D12RHIBuffer*>(buffer);
    if (command_list == nullptr)
    {
        if (commandBuffer != nullptr || buffer != nullptr)
        {
            LOG_WARN("D3D12 cmdDispatchIndirect skipped because no command list is available");
        }
        return;
    }
    if (d3d_buffer == nullptr || d3d_buffer->resource == nullptr)
    {
        if (buffer != nullptr)
        {
            LOG_WARN("D3D12 cmdDispatchIndirect skipped because the indirect argument buffer has no D3D12 resource");
        }
        return;
    }
    if (!ensureDispatchCommandSignature())
    {
        LOG_WARN("D3D12 cmdDispatchIndirect skipped because the dispatch command signature is unavailable");
        return;
    }

    if (d3d_command_buffer != nullptr)
    {
        bindEngineDescriptorHeaps(command_list,
                                  *d3d_command_buffer,
                                  m_d3d12_cbv_srv_uav_heap.Get(),
                                  m_d3d12_sampler_heap.Get(),
                                  true,
                                  RHI_PIPELINE_BIND_POINT_COMPUTE);
    }
    ID3D12Resource* argument_resource = d3d_buffer->resource.Get();
    UINT64          argument_offset   = offset;
    const bool storage_indirect_buffer =
        hasFlag(d3d_buffer->usage, RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT) &&
        hasFlag(d3d_buffer->usage, RHI_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    if (storage_indirect_buffer && d3d_command_buffer != nullptr)
    {
        if (!ensureDispatchArgumentScratchBuffer(m_d3d12_device.Get(), *d3d_command_buffer))
        {
            LOG_WARN("D3D12 cmdDispatchIndirect skipped because the scratch argument buffer is unavailable");
            return;
        }

        transitionResource(command_list,
                           d3d_buffer->resource.Get(),
                           d3d_buffer->current_state,
                           D3D12_RESOURCE_STATE_COPY_SOURCE);
        transitionResource(command_list,
                           d3d_command_buffer->dispatch_argument_buffer.Get(),
                           d3d_command_buffer->dispatch_argument_buffer_state,
                           D3D12_RESOURCE_STATE_COPY_DEST);
        command_list->CopyBufferRegion(d3d_command_buffer->dispatch_argument_buffer.Get(),
                                       0,
                                       d3d_buffer->resource.Get(),
                                       offset,
                                       sizeof(D3D12_DISPATCH_ARGUMENTS));
        transitionResource(command_list,
                           d3d_command_buffer->dispatch_argument_buffer.Get(),
                           d3d_command_buffer->dispatch_argument_buffer_state,
                           D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        transitionResource(command_list,
                           d3d_buffer->resource.Get(),
                           d3d_buffer->current_state,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        argument_resource = d3d_command_buffer->dispatch_argument_buffer.Get();
        argument_offset   = 0;
    }
    else if (d3d_buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT)
    {
        transitionResource(command_list,
                           d3d_buffer->resource.Get(),
                           d3d_buffer->current_state,
                           D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    }

    command_list->ExecuteIndirect(m_d3d12_dispatch_command_signature.Get(),
                                  1,
                                  argument_resource,
                                  argument_offset,
                                  nullptr,
                                  0);
#else
    (void)commandBuffer;
    (void)buffer;
    (void)offset;
#endif
    return;
}

RHIRayTracingCapabilities D3D12RHI::getRayTracingCapabilities() const
{
    RHIRayTracingCapabilities capabilities {};
#if defined(_WIN32) && PICCOLO_D3D12_HAS_DXR
    ComPtr<ID3D12Device5> device5;
    if (m_d3d12_device == nullptr ||
        FAILED(m_d3d12_device.As(&device5)) ||
        device5 == nullptr)
    {
        return capabilities;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 {};
    if (FAILED(device5->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))) ||
        options5.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
    {
        return capabilities;
    }

    capabilities.support_level = RHIRayTracingSupportLevel::Supported;
    capabilities.max_recursion_depth = D3D12_RAYTRACING_MAX_DECLARABLE_TRACE_RECURSION_DEPTH;
    capabilities.shader_group_handle_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    capabilities.shader_group_handle_alignment = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;
    capabilities.shader_binding_table_alignment = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    capabilities.supports_inline_ray_tracing = options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
#endif
    return capabilities;
}

bool D3D12RHI::createAccelerationStructure(const RHIAccelerationStructureBuildDesc* build_desc,
                                           RHIAccelerationStructure*& acceleration_structure)
{
    acceleration_structure = nullptr;
#if defined(_WIN32) && PICCOLO_D3D12_HAS_DXR
    if (build_desc == nullptr)
    {
        return false;
    }

    ComPtr<ID3D12Device5> device5;
    if (m_d3d12_device == nullptr ||
        FAILED(m_d3d12_device.As(&device5)) ||
        device5 == nullptr ||
        getRayTracingCapabilities().support_level != RHIRayTracingSupportLevel::Supported)
    {
        return false;
    }

    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs {};
    if (!fillRayTracingBuildInputs(*build_desc, geometries, inputs))
    {
        return false;
    }

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild_info {};
    device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild_info);
    if (prebuild_info.ResultDataMaxSizeInBytes == 0 || prebuild_info.ScratchDataSizeInBytes == 0)
    {
        return false;
    }

    auto* acceleration_structure_impl = new D3D12RHIAccelerationStructure();
    acceleration_structure_impl->type = build_desc->type;
    acceleration_structure_impl->allow_update = build_desc->allow_update;
    acceleration_structure_impl->result_size = prebuild_info.ResultDataMaxSizeInBytes;
    acceleration_structure_impl->scratch_size = prebuild_info.ScratchDataSizeInBytes;
    acceleration_structure_impl->update_scratch_size = prebuild_info.UpdateScratchDataSizeInBytes;

    const bool result_created =
        createRayTracingBuffer(m_d3d12_device.Get(),
                               prebuild_info.ResultDataMaxSizeInBytes,
                               D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                               acceleration_structure_impl->result.ReleaseAndGetAddressOf());
    const bool scratch_created =
        createRayTracingBuffer(m_d3d12_device.Get(),
                               prebuild_info.ScratchDataSizeInBytes,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                               acceleration_structure_impl->scratch.ReleaseAndGetAddressOf());
    if (!result_created || !scratch_created)
    {
        logD3D12InfoQueueMessages(m_d3d12_device.Get(), "ray tracing acceleration structure allocation failure");
        delete acceleration_structure_impl;
        return false;
    }

    acceleration_structure_impl->gpu_address =
        acceleration_structure_impl->result->GetGPUVirtualAddress();
    acceleration_structure = acceleration_structure_impl;
    return true;
#else
    (void)build_desc;
    return false;
#endif
}

bool D3D12RHI::buildAccelerationStructure(RHICommandBuffer* command_buffer,
                                          const RHIAccelerationStructureBuildDesc* build_desc,
                                          RHIAccelerationStructure* acceleration_structure)
{
#if defined(_WIN32) && PICCOLO_D3D12_HAS_DXR
    auto* acceleration_structure_impl = static_cast<D3D12RHIAccelerationStructure*>(acceleration_structure);
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(command_buffer);
    auto* command_list = d3d12CommandListFor(command_buffer);
    if (build_desc == nullptr ||
        acceleration_structure_impl == nullptr ||
        acceleration_structure_impl->result == nullptr ||
        acceleration_structure_impl->scratch == nullptr ||
        command_list == nullptr)
    {
        return false;
    }

    ComPtr<ID3D12GraphicsCommandList4> command_list4;
    if (FAILED(command_list->QueryInterface(IID_PPV_ARGS(&command_list4))) || command_list4 == nullptr)
    {
        return false;
    }

    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs {};
    if (!fillRayTracingBuildInputs(*build_desc, geometries, inputs))
    {
        return false;
    }

    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instance_descs;
    if (build_desc->type == RHIAccelerationStructureType::BottomLevel)
    {
        for (uint32_t geometry_index = 0; geometry_index < build_desc->geometry_count; ++geometry_index)
        {
            auto* vertex_buffer = static_cast<D3D12RHIBuffer*>(build_desc->geometries[geometry_index].vertex_position_buffer);
            auto* index_buffer = static_cast<D3D12RHIBuffer*>(build_desc->geometries[geometry_index].index_buffer);
            if (vertex_buffer != nullptr && vertex_buffer->resource != nullptr && vertex_buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT)
            {
                transitionResource(command_list,
                                   vertex_buffer->resource.Get(),
                                   vertex_buffer->current_state,
                                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
            if (index_buffer != nullptr && index_buffer->resource != nullptr && index_buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT)
            {
                transitionResource(command_list,
                                   index_buffer->resource.Get(),
                                   index_buffer->current_state,
                                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
        }
    }
    else
    {
        if (build_desc->instances == nullptr || build_desc->instance_count == 0)
        {
            return false;
        }

        instance_descs.resize(build_desc->instance_count);
        for (uint32_t instance_index = 0; instance_index < build_desc->instance_count; ++instance_index)
        {
            const auto& rhi_instance = build_desc->instances[instance_index];
            auto* bottom_level_as = static_cast<D3D12RHIAccelerationStructure*>(rhi_instance.bottom_level_as);
            if (bottom_level_as == nullptr || bottom_level_as->gpu_address == 0)
            {
                return false;
            }

            D3D12_RAYTRACING_INSTANCE_DESC& instance_desc = instance_descs[instance_index];
            if (rhi_instance.row_major_3x4_transform != nullptr)
            {
                std::memcpy(instance_desc.Transform,
                            rhi_instance.row_major_3x4_transform,
                            sizeof(instance_desc.Transform));
            }
            else
            {
                instance_desc.Transform[0][0] = 1.0f;
                instance_desc.Transform[1][1] = 1.0f;
                instance_desc.Transform[2][2] = 1.0f;
            }
            instance_desc.InstanceID = rhi_instance.instance_id & 0xFFFFFFu;
            instance_desc.InstanceMask = rhi_instance.instance_mask;
            instance_desc.InstanceContributionToHitGroupIndex = rhi_instance.hit_group_index & 0xFFFFFFu;
            instance_desc.Flags = rhi_instance.force_opaque ?
                                      D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE :
                                      D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            instance_desc.AccelerationStructure = bottom_level_as->gpu_address;
        }

        const uint64_t instance_buffer_size =
            alignUp(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instance_descs.size(),
                    D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT);
        if (!createUploadBuffer(m_d3d12_device.Get(),
                                instance_buffer_size,
                                acceleration_structure_impl->instance_upload.ReleaseAndGetAddressOf()))
        {
            return false;
        }

        void* mapped_instances = nullptr;
        D3D12_RANGE read_range {0, 0};
        if (FAILED(acceleration_structure_impl->instance_upload->Map(0, &read_range, &mapped_instances)) ||
            mapped_instances == nullptr)
        {
            return false;
        }
        std::memcpy(mapped_instances,
                    instance_descs.data(),
                    sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instance_descs.size());
        acceleration_structure_impl->instance_upload->Unmap(0, nullptr);
        inputs.InstanceDescs = acceleration_structure_impl->instance_upload->GetGPUVirtualAddress();
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_as_desc {};
    build_as_desc.Inputs = inputs;
    build_as_desc.DestAccelerationStructureData =
        acceleration_structure_impl->result->GetGPUVirtualAddress();
    build_as_desc.ScratchAccelerationStructureData =
        acceleration_structure_impl->scratch->GetGPUVirtualAddress();
    if (build_desc->perform_update && build_desc->source != nullptr)
    {
        auto* source = static_cast<D3D12RHIAccelerationStructure*>(build_desc->source);
        if (source != nullptr && source->result != nullptr)
        {
            build_as_desc.SourceAccelerationStructureData = source->result->GetGPUVirtualAddress();
        }
    }

    command_list4->BuildRaytracingAccelerationStructure(&build_as_desc, 0, nullptr);

    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = acceleration_structure_impl->result.Get();
    command_list->ResourceBarrier(1, &barrier);
    if (d3d_command_buffer != nullptr)
    {
        d3d_command_buffer->has_recorded_commands = true;
    }
    return true;
#else
    (void)command_buffer;
    (void)build_desc;
    (void)acceleration_structure;
    return false;
#endif
}

bool D3D12RHI::createRayTracingPipeline(const RHIRayTracingPipelineCreateInfo* create_info,
                                        RHIPipeline*& pipeline)
{
    pipeline = nullptr;
#if defined(_WIN32) && PICCOLO_D3D12_HAS_DXR
    if (create_info == nullptr ||
        create_info->shader_library.bytecode == nullptr ||
        create_info->shader_library.bytecode_size == 0 ||
        create_info->layout == nullptr)
    {
        return false;
    }

    ComPtr<ID3D12Device5> device5;
    if (m_d3d12_device == nullptr ||
        FAILED(m_d3d12_device.As(&device5)) ||
        device5 == nullptr ||
        getRayTracingCapabilities().support_level != RHIRayTracingSupportLevel::Supported)
    {
        return false;
    }

    auto* d3d_layout = static_cast<D3D12RHIPipelineLayout*>(create_info->layout);
    if (d3d_layout == nullptr || d3d_layout->root_signature == nullptr)
    {
        return false;
    }

    auto* pipeline_impl = new D3D12RHIPipeline();
    pipeline_impl->bind_point = RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
    pipeline_impl->layout = d3d_layout;

    const wchar_t* raygen_export =
        rayTracingExportOrDefault(create_info->shader_library.raygen_export, kPathTracingRayGenExport);
    const wchar_t* miss_export =
        rayTracingExportOrDefault(create_info->shader_library.miss_export, kPathTracingMissExport);
    const wchar_t* closest_hit_export =
        rayTracingExportOrDefault(create_info->shader_library.closest_hit_export, kPathTracingClosestHitExport);
    const wchar_t* hit_group_export =
        rayTracingExportOrDefault(create_info->shader_library.hit_group_export, kPathTracingHitGroupExport);

    D3D12_EXPORT_DESC exports[3] {};
    exports[0].Name = raygen_export;
    exports[1].Name = miss_export;
    exports[2].Name = closest_hit_export;

    D3D12_DXIL_LIBRARY_DESC library_desc {};
    library_desc.DXILLibrary.pShaderBytecode = create_info->shader_library.bytecode;
    library_desc.DXILLibrary.BytecodeLength = create_info->shader_library.bytecode_size;
    library_desc.NumExports = 3;
    library_desc.pExports = exports;

    D3D12_HIT_GROUP_DESC hit_group_desc {};
    hit_group_desc.HitGroupExport = hit_group_export;
    hit_group_desc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hit_group_desc.ClosestHitShaderImport = closest_hit_export;

    D3D12_RAYTRACING_SHADER_CONFIG shader_config {};
    shader_config.MaxPayloadSizeInBytes = 16;
    shader_config.MaxAttributeSizeInBytes = 8;

    D3D12_GLOBAL_ROOT_SIGNATURE global_root_signature {};
    global_root_signature.pGlobalRootSignature = d3d_layout->root_signature.Get();

    D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config {};
    pipeline_config.MaxTraceRecursionDepth =
        (std::max)(1U, create_info->max_recursion_depth);

    D3D12_STATE_SUBOBJECT subobjects[5] {};
    subobjects[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subobjects[0].pDesc = &library_desc;
    subobjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subobjects[1].pDesc = &hit_group_desc;
    subobjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    subobjects[2].pDesc = &shader_config;
    subobjects[3].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subobjects[3].pDesc = &global_root_signature;
    subobjects[4].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    subobjects[4].pDesc = &pipeline_config;

    D3D12_STATE_OBJECT_DESC state_object_desc {};
    state_object_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    state_object_desc.NumSubobjects = static_cast<UINT>(std::size(subobjects));
    state_object_desc.pSubobjects = subobjects;

    const HRESULT state_object_result =
        device5->CreateStateObject(&state_object_desc, IID_PPV_ARGS(&pipeline_impl->state_object));
    if (FAILED(state_object_result) ||
        pipeline_impl->state_object == nullptr ||
        FAILED(pipeline_impl->state_object.As(&pipeline_impl->state_object_properties)) ||
        pipeline_impl->state_object_properties == nullptr)
    {
        logD3D12InfoQueueMessages(m_d3d12_device.Get(), "ray tracing pipeline creation failure");
        LOG_ERROR("D3D12 ray tracing pipeline creation failed (HRESULT=0x{:08X})",
                  static_cast<unsigned int>(state_object_result));
        delete pipeline_impl;
        return false;
    }

    pipeline = pipeline_impl;
    return true;
#else
    (void)create_info;
    return false;
#endif
}

bool D3D12RHI::createShaderBindingTable(const RHIShaderBindingTableCreateInfo* create_info,
                                        RHIShaderBindingTable*& shader_binding_table)
{
    shader_binding_table = nullptr;
#if defined(_WIN32) && PICCOLO_D3D12_HAS_DXR
    if (create_info == nullptr || create_info->ray_tracing_pipeline == nullptr)
    {
        return false;
    }

    auto* pipeline_impl = static_cast<D3D12RHIPipeline*>(create_info->ray_tracing_pipeline);
    if (pipeline_impl == nullptr ||
        pipeline_impl->state_object_properties == nullptr ||
        pipeline_impl->bind_point != RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
    {
        return false;
    }

    const wchar_t* raygen_export =
        rayTracingExportOrDefault(create_info->raygen_export, kPathTracingRayGenExport);
    const wchar_t* miss_export =
        rayTracingExportOrDefault(create_info->miss_export, kPathTracingMissExport);
    const wchar_t* hit_group_export =
        rayTracingExportOrDefault(create_info->hit_group_export, kPathTracingHitGroupExport);
    const void* raygen_identifier = pipeline_impl->state_object_properties->GetShaderIdentifier(raygen_export);
    const void* miss_identifier = pipeline_impl->state_object_properties->GetShaderIdentifier(miss_export);
    const void* hit_group_identifier = pipeline_impl->state_object_properties->GetShaderIdentifier(hit_group_export);
    if (raygen_identifier == nullptr || miss_identifier == nullptr || hit_group_identifier == nullptr)
    {
        return false;
    }

    const uint64_t record_size = alignUp(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
                                         D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    const uint64_t table_size = alignUp(record_size * 3,
                                        D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    auto* shader_binding_table_impl = new D3D12RHIShaderBindingTable();
    if (!createUploadBuffer(m_d3d12_device.Get(),
                            table_size,
                            shader_binding_table_impl->resource.ReleaseAndGetAddressOf()))
    {
        delete shader_binding_table_impl;
        return false;
    }

    uint8_t* mapped_records = nullptr;
    D3D12_RANGE read_range {0, 0};
    if (FAILED(shader_binding_table_impl->resource->Map(0,
                                                        &read_range,
                                                        reinterpret_cast<void**>(&mapped_records))) ||
        mapped_records == nullptr)
    {
        delete shader_binding_table_impl;
        return false;
    }
    std::memcpy(mapped_records + record_size * 0, raygen_identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    std::memcpy(mapped_records + record_size * 1, miss_identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    std::memcpy(mapped_records + record_size * 2, hit_group_identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    shader_binding_table_impl->resource->Unmap(0, nullptr);

    const D3D12_GPU_VIRTUAL_ADDRESS table_start =
        shader_binding_table_impl->resource->GetGPUVirtualAddress();
    shader_binding_table_impl->raygen_start = table_start;
    shader_binding_table_impl->raygen_size = record_size;
    shader_binding_table_impl->miss_start = table_start + record_size;
    shader_binding_table_impl->miss_size = record_size;
    shader_binding_table_impl->miss_stride = record_size;
    shader_binding_table_impl->hit_group_start = table_start + record_size * 2;
    shader_binding_table_impl->hit_group_size = record_size;
    shader_binding_table_impl->hit_group_stride = record_size;
    shader_binding_table = shader_binding_table_impl;
    return true;
#else
    (void)create_info;
    return false;
#endif
}

void D3D12RHI::cmdTraceRays(RHICommandBuffer* command_buffer, const RHIRayTracingDispatchDesc* dispatch_desc)
{
#if defined(_WIN32) && PICCOLO_D3D12_HAS_DXR
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(command_buffer);
    auto* pipeline_impl = dispatch_desc != nullptr ?
                              static_cast<D3D12RHIPipeline*>(dispatch_desc->ray_tracing_pipeline) :
                              nullptr;
    auto* shader_binding_table = dispatch_desc != nullptr ?
                                     static_cast<D3D12RHIShaderBindingTable*>(dispatch_desc->shader_binding_table) :
                                     nullptr;
    auto* command_list = d3d12CommandListFor(command_buffer);
    if (d3d_command_buffer == nullptr ||
        pipeline_impl == nullptr ||
        shader_binding_table == nullptr ||
        command_list == nullptr ||
        dispatch_desc->width == 0 ||
        dispatch_desc->height == 0)
    {
        return;
    }
    if (d3d_command_buffer->in_render_pass)
    {
        LOG_WARN("D3D12 cmdTraceRays skipped because DXR dispatch cannot run inside a render pass");
        return;
    }

    ComPtr<ID3D12GraphicsCommandList4> command_list4;
    if (FAILED(command_list->QueryInterface(IID_PPV_ARGS(&command_list4))) || command_list4 == nullptr)
    {
        return;
    }

    if (pipeline_impl->state_object != nullptr)
    {
        command_list4->SetPipelineState1(pipeline_impl->state_object.Get());
    }
    if (dispatch_desc->layout != nullptr && dispatch_desc->layout != pipeline_impl->layout)
    {
        pipeline_impl->layout = static_cast<D3D12RHIPipelineLayout*>(dispatch_desc->layout);
    }
    if (pipeline_impl->layout != nullptr && pipeline_impl->layout->root_signature != nullptr)
    {
        d3d_command_buffer->bound_ray_tracing_pipeline_layout = pipeline_impl->layout;
        d3d_command_buffer->bound_ray_tracing_root_signature = pipeline_impl->layout->root_signature.Get();
        command_list->SetComputeRootSignature(d3d_command_buffer->bound_ray_tracing_root_signature);
        d3d_command_buffer->ray_tracing_root_signature_dirty = false;
    }

    bindEngineDescriptorHeaps(command_list,
                              *d3d_command_buffer,
                              m_d3d12_cbv_srv_uav_heap.Get(),
                              m_d3d12_sampler_heap.Get(),
                              true,
                              RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR);
    replayRootDescriptorTables(command_list, *d3d_command_buffer, RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR);

    D3D12_DISPATCH_RAYS_DESC d3d_dispatch_desc {};
    d3d_dispatch_desc.RayGenerationShaderRecord.StartAddress = shader_binding_table->raygen_start;
    d3d_dispatch_desc.RayGenerationShaderRecord.SizeInBytes = shader_binding_table->raygen_size;
    d3d_dispatch_desc.MissShaderTable.StartAddress = shader_binding_table->miss_start;
    d3d_dispatch_desc.MissShaderTable.SizeInBytes = shader_binding_table->miss_size;
    d3d_dispatch_desc.MissShaderTable.StrideInBytes = shader_binding_table->miss_stride;
    d3d_dispatch_desc.HitGroupTable.StartAddress = shader_binding_table->hit_group_start;
    d3d_dispatch_desc.HitGroupTable.SizeInBytes = shader_binding_table->hit_group_size;
    d3d_dispatch_desc.HitGroupTable.StrideInBytes = shader_binding_table->hit_group_stride;
    d3d_dispatch_desc.Width = dispatch_desc->width;
    d3d_dispatch_desc.Height = dispatch_desc->height;
    d3d_dispatch_desc.Depth = dispatch_desc->depth == 0 ? 1 : dispatch_desc->depth;
    command_list4->DispatchRays(&d3d_dispatch_desc);
    d3d_command_buffer->has_recorded_commands = true;
#else
    (void)command_buffer;
    (void)dispatch_desc;
#endif
}

void D3D12RHI::destroyAccelerationStructure(RHIAccelerationStructure*& acceleration_structure)
{
#if defined(_WIN32) && PICCOLO_D3D12_HAS_DXR
    delete static_cast<D3D12RHIAccelerationStructure*>(acceleration_structure);
#endif
    acceleration_structure = nullptr;
}

void D3D12RHI::destroyShaderBindingTable(RHIShaderBindingTable*& shader_binding_table)
{
#if defined(_WIN32) && PICCOLO_D3D12_HAS_DXR
    delete static_cast<D3D12RHIShaderBindingTable*>(shader_binding_table);
#endif
    shader_binding_table = nullptr;
}

void D3D12RHI::cmdPipelineBarrier(RHICommandBuffer* commandBuffer, RHIPipelineStageFlags srcStageMask, RHIPipelineStageFlags dstStageMask, RHIDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const RHIMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const RHIBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const RHIImageMemoryBarrier* pImageMemoryBarriers)
{
    (void)srcStageMask;
    (void)dstStageMask;
    (void)dependencyFlags;
#ifdef _WIN32
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (command_list == nullptr)
    {
        const bool has_barrier_work =
            (memoryBarrierCount > 0 && pMemoryBarriers != nullptr) ||
            (bufferMemoryBarrierCount > 0 && pBufferMemoryBarriers != nullptr) ||
            (imageMemoryBarrierCount > 0 && pImageMemoryBarriers != nullptr);
        if (commandBuffer != nullptr && has_barrier_work)
        {
            LOG_WARN("D3D12 cmdPipelineBarrier skipped because no command list is available");
        }
        return;
    }

    if (pMemoryBarriers != nullptr)
    {
        for (uint32_t barrier_index = 0; barrier_index < memoryBarrierCount; ++barrier_index)
        {
            const RHIMemoryBarrier& memory_barrier = pMemoryBarriers[barrier_index];
            if (bufferAccessIncludesGpuWrite(memory_barrier.srcAccessMask) ||
                bufferAccessIncludesGpuWrite(memory_barrier.dstAccessMask))
            {
                invalidateTrackedHostVisibleDefaultMirrors();

                D3D12_RESOURCE_BARRIER barrier {};
                barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.Flags         = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.UAV.pResource = nullptr;
                command_list->ResourceBarrier(1, &barrier);
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
                if (bufferAccessIncludesGpuWrite(buffer_barrier.srcAccessMask) ||
                    bufferAccessIncludesGpuWrite(buffer_barrier.dstAccessMask))
                {
                    buffer->host_data_valid = false;
                    buffer->host_data_uploadable = false;
                }

                if (buffer->current_state == target_state &&
                    (hasFlag(buffer_barrier.srcAccessMask, RHI_ACCESS_SHADER_WRITE_BIT) ||
                     hasFlag(buffer_barrier.dstAccessMask, RHI_ACCESS_SHADER_WRITE_BIT)))
                {
                    D3D12_RESOURCE_BARRIER barrier {};
                    barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    barrier.Flags         = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    barrier.UAV.pResource = buffer->resource.Get();
                    command_list->ResourceBarrier(1, &barrier);
                }
                else
                {
                    transitionResource(command_list,
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
            const bool needs_uav_barrier =
                hasFlag(image_barrier.srcAccessMask, RHI_ACCESS_SHADER_WRITE_BIT) ||
                hasFlag(image_barrier.dstAccessMask, RHI_ACCESS_SHADER_WRITE_BIT);
            if (imageSubresourceRangeInState(*image, image_barrier.subresourceRange, target_state))
            {
                if (needs_uav_barrier)
                {
                    D3D12_RESOURCE_BARRIER barrier {};
                    barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    barrier.Flags         = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    barrier.UAV.pResource = image->resource.Get();
                    command_list->ResourceBarrier(1, &barrier);
                }
            }
            else
            {
                transitionImageSubresourceRange(command_list,
                                                *image,
                                                image_barrier.subresourceRange,
                                                target_state);
                if (needs_uav_barrier)
                {
                    D3D12_RESOURCE_BARRIER barrier {};
                    barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    barrier.Flags         = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    barrier.UAV.pResource = image->resource.Get();
                    command_list->ResourceBarrier(1, &barrier);
                }
            }
        }
    }
#else
    (void)commandBuffer;
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

bool D3D12RHI::queueSubmit(RHIQueue* queue, uint32_t submitCount, const RHISubmitInfo* pSubmits, RHIFence* fence)
{
#ifdef _WIN32
    auto* d3d_queue = static_cast<D3D12RHIQueue*>(queue);
    ID3D12CommandQueue* command_queue =
        (d3d_queue != nullptr && d3d_queue->command_queue != nullptr) ? d3d_queue->command_queue :
                                                                        m_d3d12_command_queue.Get();
    if (command_queue == nullptr)
    {
        return false;
    }

    if (pSubmits != nullptr)
    {
        for (uint32_t submit_index = 0; submit_index < submitCount; ++submit_index)
        {
            const RHISubmitInfo& submit = pSubmits[submit_index];

            for (uint32_t semaphore_index = 0; semaphore_index < submit.waitSemaphoreCount; ++semaphore_index)
            {
                if (submit.pWaitSemaphores == nullptr)
                {
                    return false;
                }

                auto* semaphore = static_cast<D3D12RHISemaphore*>(submit.pWaitSemaphores[semaphore_index]);
                if (semaphore != nullptr &&
                    semaphore->fence != nullptr &&
                    semaphore->has_pending_signal)
                {
                    if (FAILED(command_queue->Wait(semaphore->fence.Get(), semaphore->wait_value)))
                    {
                        return false;
                    }
                    semaphore->has_pending_signal = false;
                }
            }

            std::vector<ID3D12CommandList*> submit_command_lists;
            for (uint32_t command_buffer_index = 0; command_buffer_index < submit.commandBufferCount; ++command_buffer_index)
            {
                if (submit.pCommandBuffers == nullptr)
                {
                    return false;
                }

                auto* d3d_command_buffer =
                    static_cast<D3D12RHICommandBuffer*>(submit.pCommandBuffers[command_buffer_index]);
                if (d3d_command_buffer == nullptr || d3d_command_buffer->command_list == nullptr)
                {
                    continue;
                }

                if (d3d_command_buffer->is_open)
                {
                    if (FAILED(d3d_command_buffer->command_list->Close()))
                    {
                        d3d_command_buffer->is_open = false;
                        return false;
                    }
                    d3d_command_buffer->is_open = false;
                    d3d_command_buffer->has_recorded_commands = true;
                }

                if (d3d_command_buffer->has_recorded_commands)
                {
                    submit_command_lists.push_back(d3d_command_buffer->command_list.Get());
                }
            }

            if (!submit_command_lists.empty())
            {
                command_queue->ExecuteCommandLists(static_cast<UINT>(submit_command_lists.size()), submit_command_lists.data());
            }

            for (uint32_t semaphore_index = 0; semaphore_index < submit.signalSemaphoreCount; ++semaphore_index)
            {
                if (submit.pSignalSemaphores == nullptr)
                {
                    return false;
                }

                auto* semaphore =
                    static_cast<D3D12RHISemaphore*>(const_cast<RHISemaphore*>(submit.pSignalSemaphores[semaphore_index]));
                if (semaphore == nullptr || semaphore->fence == nullptr)
                {
                    return false;
                }

                const uint64_t signal_value = semaphore->next_signal_value + 1ULL;
                if (FAILED(command_queue->Signal(semaphore->fence.Get(), signal_value)))
                {
                    return false;
                }
                semaphore->next_signal_value = signal_value;
                semaphore->wait_value        = signal_value;
                semaphore->has_pending_signal = true;
            }
        }
    }

    if (fence != nullptr)
    {
        auto* d3d_fence = static_cast<D3D12RHIFence*>(fence);
        if (d3d_fence == nullptr || d3d_fence->fence == nullptr)
        {
            return false;
        }

        if (!d3d_fence->has_pending_signal)
        {
            d3d_fence->wait_value = d3d_fence->next_signal_value + 1ULL;
        }

        if (FAILED(command_queue->Signal(d3d_fence->fence.Get(), d3d_fence->wait_value)))
        {
            return false;
        }

        d3d_fence->next_signal_value = (std::max)(d3d_fence->next_signal_value, d3d_fence->wait_value);
        d3d_fence->has_pending_signal = true;
        d3d_fence->signaled           = false;
    }
    return true;
#else
    (void)queue;
    (void)fence;
    (void)submitCount;
    (void)pSubmits;
    return true;
#endif
}

bool D3D12RHI::queueWaitIdle(RHIQueue* queue)
{
#ifdef _WIN32
    auto* d3d_queue = static_cast<D3D12RHIQueue*>(queue);
    if (d3d_queue != nullptr && d3d_queue->command_queue != nullptr && d3d_queue->command_queue != m_d3d12_command_queue.Get())
    {
        const uint64_t signal_value = ++m_d3d12_fence_value;
        if (FAILED(d3d_queue->command_queue->Signal(m_d3d12_fence.Get(), signal_value)))
        {
            return false;
        }
        return waitForD3D12FenceValue(m_d3d12_fence.Get(), m_d3d12_fence_event, signal_value, UINT64_MAX);
    }
#else
    (void)queue;
#endif
    waitForGpu();
    return true;
}

void D3D12RHI::resetCommandPool()
{
#ifdef _WIN32
    if (auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(m_current_command_buffer))
    {
        if (d3d_command_buffer->is_open)
        {
            return;
        }
        d3d_command_buffer->has_recorded_commands = false;
        d3d_command_buffer->dynamic_descriptor_table_cache.clear();
    }
    m_d3d12_transient_cbv_srv_uav_descriptor_next = m_d3d12_cbv_srv_uav_descriptor_next;
#endif
    return;
}

void D3D12RHI::waitForFences()
{
#ifdef _WIN32
    RHIFence* current_frame_fence = m_frame_fences[m_current_frame_index % m_frame_fences.size()];
    if (current_frame_fence != nullptr && !waitForFencesPFN(1, &current_frame_fence, RHI_TRUE, UINT64_MAX))
    {
        LOG_ERROR("D3D12 waitForFences failed for frame {}", static_cast<uint32_t>(m_current_frame_index));
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

RHICommandBuffer* D3D12RHI::getCurrentCommandBuffer() const
{
    return m_current_command_buffer;
}

RHICommandBuffer* const* D3D12RHI::getCommandBufferList() const
{
    return m_frame_command_buffers.data();
}

RHICommandPool* D3D12RHI::getCommandPoor() const
{
    return m_default_command_pool;
}

RHIDescriptorPool* D3D12RHI::getDescriptorPoor() const
{
    return m_default_descriptor_pool;
}

RHIFence* const* D3D12RHI::getFenceList() const
{
    return m_frame_fences.data();
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
    return m_graphics_queue;
}

RHIQueue* D3D12RHI::getComputeQueue() const
{
    return m_compute_queue;
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

RHICommandBuffer* D3D12RHI::beginSingleTimeCommands()
{
    auto* command_buffer = new D3D12RHICommandBuffer();
#ifdef _WIN32
    RHICommandBufferBeginInfo begin_info {};
    begin_info.sType = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (beginCommandBufferPFN(command_buffer, &begin_info))
    {
        command_buffer->owns_recording = true;
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
        if (m_d3d12_command_queue != nullptr && d3d_command_buffer->command_list != nullptr)
        {
            if (d3d_command_buffer->is_open)
            {
                if (FAILED(d3d_command_buffer->command_list->Close()))
                {
                    d3d_command_buffer->is_open = false;
                    delete d3d_command_buffer;
                    return;
                }
                d3d_command_buffer->is_open = false;
                d3d_command_buffer->has_recorded_commands = true;
            }

            ID3D12CommandList* command_lists[] = {d3d_command_buffer->command_list.Get()};
            m_d3d12_command_queue->ExecuteCommandLists(1, command_lists);
            waitForGpu();
        }
    }
#endif
    delete d3d_command_buffer;
    return;
}

bool D3D12RHI::prepareBeforePass(std::function<void()> passUpdateAfterRecreateSwapchain)
{
    (void)passUpdateAfterRecreateSwapchain;
#ifdef _WIN32
    int framebuffer_width  = static_cast<int>(m_window_width);
    int framebuffer_height = static_cast<int>(m_window_height);
    if (m_window != nullptr)
    {
        glfwGetFramebufferSize(m_window, &framebuffer_width, &framebuffer_height);
    }

    if (framebuffer_width <= 0 || framebuffer_height <= 0)
    {
        return true;
    }

    const uint32_t requested_width  = static_cast<uint32_t>(framebuffer_width);
    const uint32_t requested_height = static_cast<uint32_t>(framebuffer_height);
    const bool     needs_recreate   = !m_d3d12_swapchain ||
                                    requested_width != m_window_width ||
                                    requested_height != m_window_height ||
                                    requested_width != m_swapchain_desc.extent.width ||
                                    requested_height != m_swapchain_desc.extent.height ||
                                    m_swapchain_desc.imageViews.size() != m_swapchain_buffer_count;
    if (needs_recreate)
    {
        recreateSwapchain();

        const bool recreated = m_d3d12_swapchain != nullptr &&
                               m_swapchain_desc.extent.width == requested_width &&
                               m_swapchain_desc.extent.height == requested_height &&
                               m_swapchain_desc.imageViews.size() == m_swapchain_buffer_count;
        if (recreated && passUpdateAfterRecreateSwapchain)
        {
            passUpdateAfterRecreateSwapchain();
        }
        return true;
    }

    if (!m_d3d12_swapchain)
    {
        return true;
    }

    m_current_swapchain_image_index = m_d3d12_swapchain->GetCurrentBackBufferIndex();
    m_current_command_buffer = m_frame_command_buffers[m_current_frame_index % m_frame_command_buffers.size()];

    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(m_current_command_buffer);
    if (d3d_command_buffer != nullptr && ensureCommandBufferObjects(m_current_command_buffer))
    {
        if (!d3d_command_buffer->is_open)
        {
            if (FAILED(d3d_command_buffer->command_allocator->Reset()) ||
                FAILED(d3d_command_buffer->command_list->Reset(d3d_command_buffer->command_allocator.Get(), nullptr)))
            {
                logD3D12InfoQueueMessages(m_d3d12_device.Get(), "prepareBeforePass command buffer reset failure");
                LOG_ERROR("D3D12 prepareBeforePass failed to reset command buffer for frame {}",
                          static_cast<uint32_t>(m_current_frame_index));
                return true;
            }
            d3d_command_buffer->is_open = true;
            d3d_command_buffer->has_recorded_commands = false;
            d3d_command_buffer->in_render_pass = false;
            d3d_command_buffer->bound_graphics_pipeline = nullptr;
            d3d_command_buffer->bound_graphics_pipeline_layout = nullptr;
            d3d_command_buffer->bound_compute_pipeline_layout = nullptr;
            d3d_command_buffer->bound_ray_tracing_pipeline_layout = nullptr;
            d3d_command_buffer->bound_graphics_root_signature = nullptr;
            d3d_command_buffer->bound_compute_root_signature = nullptr;
            d3d_command_buffer->bound_ray_tracing_root_signature = nullptr;
            d3d_command_buffer->active_render_pass = nullptr;
            d3d_command_buffer->active_framebuffer = nullptr;
            d3d_command_buffer->active_subpass_index = 0;
            d3d_command_buffer->transient_cbv_srv_uav_descriptor_next = m_d3d12_transient_cbv_srv_uav_descriptor_next;
            d3d_command_buffer->dynamic_descriptor_table_cache.clear();
            resetCommandBufferDescriptorHeapState(*d3d_command_buffer);
        }
    }
#endif
    return false;
}

void D3D12RHI::submitRendering(std::function<void()> passUpdateAfterRecreateSwapchain)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(m_current_command_buffer);
    if (!m_d3d12_swapchain ||
        !m_d3d12_command_queue ||
        d3d_command_buffer == nullptr ||
        d3d_command_buffer->command_list == nullptr)
    {
        LOG_WARN("D3D12 submitRendering skipped because swapchain, command queue, or current command list is unavailable");
        return;
    }

    auto update_current_frame = [this]() {
        if (m_d3d12_swapchain != nullptr)
        {
            m_current_swapchain_image_index = m_d3d12_swapchain->GetCurrentBackBufferIndex();
        }
        m_current_frame_index =
            static_cast<uint8_t>((m_current_frame_index + 1U) % (std::max)(1U, m_swapchain_buffer_count));
        m_current_command_buffer = m_frame_command_buffers[m_current_frame_index % m_frame_command_buffers.size()];
    };

    if (d3d_command_buffer->is_open)
    {
        const HRESULT close_result = d3d_command_buffer->command_list->Close();
        if (FAILED(close_result))
        {
            const HRESULT removed_reason =
                m_d3d12_device != nullptr ? m_d3d12_device->GetDeviceRemovedReason() : S_OK;
            logD3D12InfoQueueMessages(m_d3d12_device.Get(), "submitRendering command list close failure");
            LOG_ERROR("D3D12 submitRendering command list close failed (HRESULT=0x{:08X}, removed_reason=0x{:08X})",
                      static_cast<unsigned int>(close_result),
                      static_cast<unsigned int>(removed_reason));
            return;
        }
        d3d_command_buffer->is_open = false;
    }
    d3d_command_buffer->has_recorded_commands = true;

    RHIFence* current_frame_fence = m_frame_fences[m_current_frame_index % m_frame_fences.size()];
    if (current_frame_fence != nullptr && !resetFencesPFN(1, &current_frame_fence))
    {
        LOG_ERROR("D3D12 submitRendering failed to reset frame fence for frame {}",
                  static_cast<uint32_t>(m_current_frame_index));
        return;
    }

    RHICommandBuffer* submit_command_buffer = m_current_command_buffer;
    RHISubmitInfo     submit_info {};
    submit_info.sType              = RHI_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers    = &submit_command_buffer;
    if (!queueSubmit(m_graphics_queue, 1, &submit_info, current_frame_fence))
    {
        const HRESULT removed_reason =
            m_d3d12_device != nullptr ? m_d3d12_device->GetDeviceRemovedReason() : S_OK;
        logD3D12InfoQueueMessages(m_d3d12_device.Get(), "submitRendering queue submit failure");
        LOG_ERROR("D3D12 submitRendering queue submit failed (removed_reason=0x{:08X})",
                  static_cast<unsigned int>(removed_reason));
        return;
    }

    const UINT present_flags = m_allow_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0;
    const HRESULT present_result = m_d3d12_swapchain->Present(0, present_flags);
    if (FAILED(present_result))
    {
        waitForGpu();

        const HRESULT removed_reason =
            m_d3d12_device != nullptr ? m_d3d12_device->GetDeviceRemovedReason() : S_OK;
        logD3D12InfoQueueMessages(m_d3d12_device.Get(), "submitRendering present failure");
        LOG_ERROR("D3D12 submitRendering Present failed (HRESULT=0x{:08X}, removed_reason=0x{:08X})",
                  static_cast<unsigned int>(present_result),
                  static_cast<unsigned int>(removed_reason));

        if (present_result == DXGI_ERROR_DEVICE_REMOVED || present_result == DXGI_ERROR_DEVICE_RESET)
        {
            LOG_ERROR("D3D12 submitRendering detected device loss during Present (HRESULT=0x{:08X}, removed_reason=0x{:08X})",
                      static_cast<unsigned int>(present_result),
                      static_cast<unsigned int>(removed_reason));
            return;
        }

        int framebuffer_width  = static_cast<int>(m_window_width);
        int framebuffer_height = static_cast<int>(m_window_height);
        if (m_window != nullptr)
        {
            glfwGetFramebufferSize(m_window, &framebuffer_width, &framebuffer_height);
        }

        if (framebuffer_width <= 0 || framebuffer_height <= 0)
        {
            LOG_ERROR("D3D12 submitRendering cannot recover swapchain after Present failure because framebuffer size is invalid (width={}, height={}, HRESULT=0x{:08X}, removed_reason=0x{:08X})",
                      framebuffer_width,
                      framebuffer_height,
                      static_cast<unsigned int>(present_result),
                      static_cast<unsigned int>(removed_reason));
            return;
        }

        const uint32_t requested_width  = static_cast<uint32_t>(framebuffer_width);
        const uint32_t requested_height = static_cast<uint32_t>(framebuffer_height);
        recreateSwapchain();
        if (m_d3d12_swapchain != nullptr &&
            m_swapchain_desc.extent.width == requested_width &&
            m_swapchain_desc.extent.height == requested_height &&
            m_swapchain_desc.imageViews.size() == m_swapchain_buffer_count)
        {
            if (passUpdateAfterRecreateSwapchain)
            {
                passUpdateAfterRecreateSwapchain();
            }
            update_current_frame();
            return;
        }

        LOG_ERROR("D3D12 submitRendering failed to recover swapchain after Present failure (HRESULT=0x{:08X}, removed_reason=0x{:08X})",
                  static_cast<unsigned int>(present_result),
                  static_cast<unsigned int>(removed_reason));
        return;
    }

    update_current_frame();
#else
    (void)passUpdateAfterRecreateSwapchain;
#endif
    return;
}

void D3D12RHI::pushEvent(RHICommandBuffer* commond_buffer, const char* name, const float* color)
{
#ifdef _WIN32
    (void)color;

    auto* command_list = d3d12CommandListFor(commond_buffer);
    if (command_list == nullptr || name == nullptr || name[0] == '\0')
    {
        return;
    }

    constexpr UINT pix_event_ansi_version = 1;
    const UINT     event_name_size        = static_cast<UINT>(std::strlen(name) + 1);
    command_list->BeginEvent(pix_event_ansi_version, name, event_name_size);
#else
    (void)commond_buffer;
    (void)name;
    (void)color;
#endif
    return;
}

void D3D12RHI::popEvent(RHICommandBuffer* commond_buffer)
{
#ifdef _WIN32
    auto* command_list = d3d12CommandListFor(commond_buffer);
    if (command_list != nullptr)
    {
        command_list->EndEvent();
    }
#else
    (void)commond_buffer;
#endif
    return;
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
    return;
}

void D3D12RHI::destroySemaphore(RHISemaphore* semaphore)
{
    delete static_cast<D3D12RHISemaphore*>(semaphore);
    return;
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
    delete static_cast<D3D12RHIFramebuffer*>(framebuffer);
    return;
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
    auto* d3d_buffer = static_cast<D3D12RHIBuffer*>(buffer);
#ifdef _WIN32
    unregisterHostVisibleDefaultBuffer(d3d_buffer);
#endif
    delete d3d_buffer;
    buffer = nullptr;
    return;
}

void D3D12RHI::freeCommandBuffers(RHICommandPool* commandPool, uint32_t commandBufferCount, RHICommandBuffer* pCommandBuffers)
{
    (void)commandPool;
    (void)commandBufferCount;
    delete static_cast<D3D12RHICommandBuffer*>(pCommandBuffers);
    return;
}

void D3D12RHI::freeAllocation(RHIAllocation*& allocation)
{
    delete allocation;
    allocation = nullptr;
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



#ifdef _WIN32
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
                m_d3d12_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
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
                                                  D3D12_COMMAND_LIST_TYPE_DIRECT,
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
#endif
} // namespace Piccolo
