#pragma once

#include "runtime/function/render/interface/rhi.h"

#include <array>
#include <limits>
#include <vector>

#ifdef _WIN32
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

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
struct D3D12RHIPipelineLayout;
struct D3D12RHIDescriptorSet;

#ifdef _WIN32
struct D3D12RHIBuffer final : RHIBuffer
{
    ComPtr<ID3D12Resource> resource;
    RHIDeviceSize          size {0};
    RHIBufferUsageFlags    usage {0};
    RHIMemoryPropertyFlags memory_properties {0};
    D3D12_HEAP_TYPE        heap_type {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_STATES  current_state {D3D12_RESOURCE_STATE_COMMON};
    D3D12_RESOURCE_FLAGS   resource_flags {D3D12_RESOURCE_FLAG_NONE};
    RHICrossQueueDomainFlags registered_domains {RHI_CROSS_QUEUE_DOMAIN_NONE};
    bool requires_cross_queue_handoff() const
    {
        return (registered_domains & (RHI_CROSS_QUEUE_DOMAIN_GRAPHICS | RHI_CROSS_QUEUE_DOMAIN_COMPUTE)) ==
               (RHI_CROSS_QUEUE_DOMAIN_GRAPHICS | RHI_CROSS_QUEUE_DOMAIN_COMPUTE);
    }
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
    RHICrossQueueDomainFlags registered_domains {RHI_CROSS_QUEUE_DOMAIN_NONE};
    bool requires_cross_queue_handoff() const
    {
        return (registered_domains & (RHI_CROSS_QUEUE_DOMAIN_GRAPHICS | RHI_CROSS_QUEUE_DOMAIN_COMPUTE)) ==
               (RHI_CROSS_QUEUE_DOMAIN_GRAPHICS | RHI_CROSS_QUEUE_DOMAIN_COMPUTE);
    }
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

struct D3D12GraphicsBindingScope
{
    bool                    valid {false};
    RHIPipeline*            pipeline {nullptr};
    D3D12RHIPipelineLayout* layout {nullptr};
    RHIRenderPass*          render_pass {nullptr};
    uint32_t                subpass_index {0};
    std::vector<uint32_t>   vertex_strides;
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
    D3D12_COMMAND_LIST_TYPE           command_list_type {D3D12_COMMAND_LIST_TYPE_DIRECT};
    bool                              is_open {false};
    bool                              has_recorded_commands {false};
    bool                              in_render_pass {false};
    D3D12GraphicsBindingScope         graphics_binding_scope {};
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
    RHIRenderPass*        graphics_render_pass {nullptr};
    uint32_t              graphics_subpass_index {0};
    uint32_t              graphics_primary_rtv_format {0};
    uint32_t              graphics_num_render_targets {0};
    std::array<uint32_t, 8> graphics_rtv_formats {};
#ifdef _WIN32
    ComPtr<ID3D12PipelineState> pipeline_state;
#if PICCOLO_D3D12_HAS_DXR
    ComPtr<ID3D12StateObject> state_object;
    ComPtr<ID3D12StateObjectProperties> state_object_properties;
#endif
    D3D_PRIMITIVE_TOPOLOGY primitive_topology {D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST};
#endif
};

} // namespace d3d12_detail
} // namespace Piccolo
