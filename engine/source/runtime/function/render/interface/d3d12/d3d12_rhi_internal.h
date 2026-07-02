#pragma once

#include "runtime/function/render/interface/d3d12/d3d12_rhi_resource.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#ifdef _WIN32
#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#endif

namespace Piccolo
{
class D3D12RHI;

namespace d3d12_detail
{
constexpr uint32_t kTrackedDescriptorTypeCount = 12;

#ifdef _WIN32
constexpr uint32_t kCbvSrvUavHeapDescriptorCount = 65536;
constexpr uint32_t kSamplerHeapDescriptorCount   = 2048;
constexpr uint32_t kRtvHeapDescriptorCount       = 1024;
constexpr uint32_t kDsvHeapDescriptorCount       = 256;
// DXR shader configuration limits (bytes) for the path tracing pipeline.
constexpr uint32_t kRayTracingMaxPayloadSizeBytes   = 32;
constexpr uint32_t kRayTracingMaxAttributeSizeBytes = 8;

constexpr uint32_t kInvalidRootParameterIndex = (std::numeric_limits<uint32_t>::max)();
#endif

RHIDeviceSize alignUp(RHIDeviceSize value, RHIDeviceSize alignment);
bool hasFlag(uint32_t flags, uint32_t flag);
bool descriptorUsesSamplerHeap(RHIDescriptorType type);
bool descriptorUsesResourceHeap(RHIDescriptorType type);
bool descriptorUsesBufferInfo(RHIDescriptorType type);
bool isTrackedDescriptorType(RHIDescriptorType type);
bool isSupportedDescriptorType(RHIDescriptorType type);
uint32_t descriptorTypeIndex(RHIDescriptorType type);
bool hasDescriptorCapacity(uint32_t required, uint32_t used, uint32_t capacity);
uint32_t calculateMipLevels(uint32_t width, uint32_t height, uint32_t requested_mip_levels);

#ifdef _WIN32
DWORD d3d12FenceTimeoutMilliseconds(uint64_t timeout);
bool waitForD3D12FenceValue(ID3D12Fence* fence, HANDLE event, uint64_t value, uint64_t timeout);
uint64_t remainingD3D12FenceTimeout(uint64_t timeout, ULONGLONG start_tick);
bool isSamplerDescriptor(RHIDescriptorType type);
bool isCbvSrvUavDescriptor(RHIDescriptorType type);
bool hasComputeStage(RHIShaderStageFlags stage_flags);
bool hasGraphicsStage(RHIShaderStageFlags stage_flags);
bool isDynamicBufferDescriptor(RHIDescriptorType type);
bool isAccelerationStructureDescriptor(RHIDescriptorType type);
D3D12_DESCRIPTOR_RANGE_TYPE toDescriptorRangeType(const RHIDescriptorSetLayoutBinding& binding);
D3D12_SHADER_VISIBILITY toShaderVisibility(RHIShaderStageFlags stage_flags);
D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor(ID3D12DescriptorHeap* heap, uint32_t descriptor_size, uint32_t index);
D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptor(ID3D12DescriptorHeap* heap, uint32_t descriptor_size, uint32_t index);
bool createDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t descriptor_count, bool shader_visible, ComPtr<ID3D12DescriptorHeap>& heap, uint32_t& descriptor_size, uint32_t& descriptor_capacity, uint32_t& descriptor_next);
bool createCpuDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t descriptor_count, ComPtr<ID3D12DescriptorHeap>& heap);
void logD3D12InfoQueueMessages(ID3D12Device* device, const char* context, UINT64 max_messages = 16);
std::string dxgiAdapterDescriptionToUtf8(const WCHAR* description);
bool reserveDescriptors(uint32_t count, uint32_t& next, uint32_t capacity, uint32_t& base);
void rememberCachedDynamicDescriptorTable( D3D12RHICommandBuffer& command_buffer, const D3D12RHIDescriptorSet& descriptor_set, uint32_t set_index, const std::vector<uint32_t>& dynamic_offsets, D3D12_GPU_DESCRIPTOR_HANDLE cbv_srv_uav_gpu_base);
bool descriptorRangeFits(uint32_t first, uint32_t count, uint32_t descriptor_count);
uint32_t dynamicDescriptorCount(const D3D12RHIDescriptorSetLayout& layout);
bool descriptorWriteHasRequiredResources(const RHIWriteDescriptorSet& write, const D3D12RHIDescriptorSetLayout::BindingRange& binding);
bool descriptorCopyHasRequiredSourceMetadata(const RHICopyDescriptorSet& copy, const D3D12RHIDescriptorSet& src_set, const D3D12RHIDescriptorSetLayout::BindingRange& src_binding);
D3D12_HEAP_TYPE chooseBufferHeapType(RHIBufferUsageFlags usage, RHIMemoryPropertyFlags properties);
D3D12_RESOURCE_STATES initialBufferState(D3D12_HEAP_TYPE heap_type);
bool bufferHostMirrorRangeValid(const D3D12RHIBuffer& buffer, RHIDeviceSize offset, RHIDeviceSize size);
bool bufferAccessIncludesGpuWrite(RHIAccessFlags access);
bool bufferHasHostVisibleMirror(const D3D12RHIBuffer& buffer);
bool bufferHostMirrorUploadable(const D3D12RHIBuffer& buffer);
bool mappedHostRangeContains(const D3D12RHIDeviceMemory& memory, RHIDeviceSize offset, RHIDeviceSize size);
bool bufferHostMirrorWholeRange(const D3D12RHIBuffer& buffer, RHIDeviceSize offset, RHIDeviceSize size);
void registerHostVisibleDefaultBuffer(D3D12RHIBuffer& buffer);
void unregisterHostVisibleDefaultBuffer(D3D12RHIBuffer* buffer);
void invalidateTrackedHostVisibleDefaultMirrors();
void updateBufferHostMirrorAfterCopy(D3D12RHIBuffer& src, D3D12RHIBuffer& dst, bool src_host_data_valid, bool dst_host_data_valid, RHIDeviceSize src_offset, RHIDeviceSize dst_offset, RHIDeviceSize size, const char* context);
D3D12_RESOURCE_FLAGS bufferResourceFlags(RHIBufferUsageFlags usage, D3D12_HEAP_TYPE heap_type);
DXGI_FORMAT toDXGIFormat(RHIFormat format);
DXGI_FORMAT toResourceDXGIFormat(RHIFormat format);
DXGI_FORMAT toDSVFormat(RHIFormat format);
DXGI_FORMAT toSRVFormat(RHIFormat format, DXGI_FORMAT fallback_format);
bool isDepthFormat(RHIFormat format);
uint32_t sourceBytesPerPixel(RHIFormat format);
uint32_t resourceBytesPerPixel(RHIFormat format);
uint32_t mipDimension(uint32_t base, uint32_t mip_level);
bool isFloat32TextureFormat(RHIFormat format);
void writeTextureComponent(uint8_t* destination_pixel, uint32_t component, float value, bool use_float_components);
void copyTextureRowToD3D12Upload(uint8_t* dst_row, const uint8_t* src_row, uint32_t width, size_t source_row_size, size_t destination_row_size, uint32_t source_bytes_per_pixel, uint32_t resource_bytes_per_pixel);
D3D12_RESOURCE_FLAGS imageResourceFlags(RHIImageUsageFlags usage);
D3D12_RESOURCE_STATES initialImageState(RHIImageUsageFlags usage);
D3D12_RESOURCE_STATES toD3D12ResourceState(RHIImageLayout layout);
D3D12_RESOURCE_STATES toD3D12BufferState(RHIAccessFlags access, RHIBufferUsageFlags usage, D3D12_HEAP_TYPE heap_type);
uint32_t d3d12SubresourceIndex(const D3D12RHIImage& image, uint32_t mip_level, uint32_t array_layer);
void appendUniqueBuffer(std::vector<D3D12RHIBuffer*>& buffers, D3D12RHIBuffer* buffer);
void rebuildDescriptorSetBufferLists(D3D12RHIDescriptorSet& descriptor_set);
void upsertBufferDescriptor(D3D12RHIDescriptorSet& descriptor_set, const D3D12RHIDescriptorSet::BufferDescriptor& descriptor);
void upsertAccelerationStructureDescriptor( D3D12RHIDescriptorSet& descriptor_set, const D3D12RHIDescriptorSet::AccelerationStructureDescriptor& descriptor);
bool formatHasStencil(RHIFormat format);
bool isDepthReadOnlyLayout(RHIImageLayout layout);
D3D12_RESOURCE_STATES descriptorBufferState(D3D12_DESCRIPTOR_RANGE_TYPE range_type);
uint32_t structuredBufferStride(const D3D12RHIDescriptorSetLayout::BindingRange& binding, const D3D12RHIDescriptorSet::BufferDescriptor& descriptor, RHIDeviceSize resolved_range);
RHIDeviceSize resolvedDescriptorRange(const D3D12RHIDescriptorSet::BufferDescriptor& descriptor, RHIDeviceSize byte_offset);
uint32_t resolvedStructuredBufferStride(uint32_t stride, RHIDeviceSize range);
void writeBufferDescriptor(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE dst_handle, const D3D12RHIDescriptorSetLayout::BindingRange& binding, const D3D12RHIDescriptorSet::BufferDescriptor& descriptor, RHIDeviceSize dynamic_offset);
D3D12_TEXTURE_ADDRESS_MODE toD3D12AddressMode(RHISamplerAddressMode address_mode);
D3D12_COMPARISON_FUNC toD3D12ComparisonFunc(RHICompareOp compare_op);
D3D12_FILTER toD3D12Filter(const RHISamplerCreateInfo& create_info);
bool createCommittedBuffer(ID3D12Device* device, RHIDeviceSize size, RHIBufferUsageFlags usage, RHIMemoryPropertyFlags properties, D3D12RHIBuffer& buffer);
void transitionResource(ID3D12GraphicsCommandList* command_list, ID3D12Resource* resource, D3D12_RESOURCE_STATES& current_state, D3D12_RESOURCE_STATES target_state);
D3D12_GPU_DESCRIPTOR_HANDLE* findCachedDynamicDescriptorTable(D3D12RHICommandBuffer& command_buffer, const D3D12RHIDescriptorSet& descriptor_set, uint32_t set_index, const std::vector<uint32_t>& dynamic_offsets);
size_t textureMipByteSize(uint32_t width, uint32_t height, uint32_t bytes_per_pixel);
std::vector<uint8_t> generateTextureMipLevel(const uint8_t* source_pixels, uint32_t source_width, uint32_t source_height, uint32_t destination_width, uint32_t destination_height, uint32_t bytes_per_pixel, RHIFormat format);
void setD3D12BorderColor(RHIBorderColor border_color, float (&out_color)[4]);
void clearRootDescriptorTableCache(std::vector<D3D12_GPU_DESCRIPTOR_HANDLE>& tables, std::vector<bool>& valid);
void clearRootDescriptorTableCache(D3D12RHICommandBuffer& command_buffer, RHIPipelineBindPoint bind_point);
void markCommandBufferDescriptorHeapsDirty(D3D12RHICommandBuffer& command_buffer);
void markCommandBufferExternalStateDirty(D3D12RHICommandBuffer& command_buffer);
void resetCommandBufferDescriptorHeapState(D3D12RHICommandBuffer& command_buffer);
void rememberRootDescriptorTable(D3D12RHICommandBuffer& command_buffer, RHIPipelineBindPoint bind_point, uint32_t root_index, D3D12_GPU_DESCRIPTOR_HANDLE descriptor);
bool rootSignatureDirtyForBindPoint(const D3D12RHICommandBuffer& command_buffer, RHIPipelineBindPoint bind_point);
bool restoreRootSignatureForDescriptorReplay(ID3D12GraphicsCommandList* command_list, D3D12RHICommandBuffer& command_buffer, RHIPipelineBindPoint bind_point);
void replayRootDescriptorTables(ID3D12GraphicsCommandList* command_list, D3D12RHICommandBuffer& command_buffer, RHIPipelineBindPoint bind_point);
void bindEngineDescriptorHeaps(ID3D12GraphicsCommandList* command_list, D3D12RHICommandBuffer& command_buffer, ID3D12DescriptorHeap* cbv_srv_uav_heap, ID3D12DescriptorHeap* sampler_heap, bool replay_tables, RHIPipelineBindPoint replay_bind_point);
uint32_t d3d12SubresourceCount(const D3D12RHIImage& image);
void syncImageCurrentState(D3D12RHIImage& image);
void initializeImageSubresourceStates(D3D12RHIImage& image, D3D12_RESOURCE_STATES initial_state);
void ensureImageSubresourceStates(D3D12RHIImage& image);
bool transitionImageSubresource(ID3D12GraphicsCommandList* command_list, D3D12RHIImage& image, uint32_t subresource, D3D12_RESOURCE_STATES target_state);
uint32_t normalizedSubresourceCount(uint32_t total_count, uint32_t base_index, uint32_t requested_count);
uint32_t transitionImageSubresourceRange(ID3D12GraphicsCommandList* command_list, D3D12RHIImage& image, uint32_t base_mip_level, uint32_t level_count, uint32_t base_array_layer, uint32_t layer_count, D3D12_RESOURCE_STATES target_state);
bool imageSubresourceRangeInState(D3D12RHIImage& image, const RHIImageSubresourceRange& range, D3D12_RESOURCE_STATES state);
bool isValidAttachmentIndex(uint32_t attachment_index);
D3D12_RESOURCE_STATES shaderReadableAttachmentState();
D3D12_RESOURCE_STATES depthReadOnlyAttachmentState();
D3D12_RESOURCE_STATES inputAttachmentState(const D3D12RHIImageView* view);
D3D12_RESOURCE_STATES depthAttachmentState(const D3D12RHIImageView* view, RHIImageLayout layout, bool read_only);
D3D12_RESOURCE_STATES subpassAttachmentState(const D3D12RHIImageView* view, RHIImageLayout layout);
D3D12RHIImageView* framebufferAttachment(D3D12RHIFramebuffer* framebuffer, uint32_t attachment_index);
bool subpassPreservesAttachment(const D3D12RHIRenderPass::SubpassInfo& subpass, uint32_t attachment_index);
bool subpassAttachmentStateForUse(D3D12RHIRenderPass* render_pass, D3D12RHIFramebuffer* framebuffer, uint32_t attachment_index, uint32_t subpass_index, D3D12_RESOURCE_STATES& state);
void addUniqueAttachmentIndex(std::vector<uint32_t>& attachment_indices, uint32_t attachment_index);
void collectSubpassAttachmentIndices(const D3D12RHIRenderPass::SubpassInfo& subpass, std::vector<uint32_t>& attachment_indices);
D3D12_RESOURCE_STATES attachmentStateAfterSubpass(D3D12RHIRenderPass* render_pass, D3D12RHIFramebuffer* framebuffer, uint32_t attachment_index, uint32_t subpass_index);
void transitionImageView(ID3D12GraphicsCommandList* command_list, D3D12RHIImageView* view, D3D12_RESOURCE_STATES target_state);
D3D12_RESOURCE_STATES attachmentStateForSubpassBoundary(D3D12RHIRenderPass* render_pass, D3D12RHIFramebuffer* framebuffer, uint32_t attachment_index, uint32_t previous_subpass_index, uint32_t next_subpass_index);
bool hasSubpassDependency(const D3D12RHIRenderPass* render_pass, uint32_t previous_subpass_index, uint32_t next_subpass_index);
void transitionD3D12SubpassBoundary(ID3D12GraphicsCommandList* command_list, D3D12RHIRenderPass* render_pass, D3D12RHIFramebuffer* framebuffer, uint32_t previous_subpass_index, uint32_t next_subpass_index);
void finishD3D12Subpass(ID3D12GraphicsCommandList* command_list, D3D12RHIRenderPass* render_pass, D3D12RHIFramebuffer* framebuffer, uint32_t subpass_index);
bool recordHostDataUpload(ID3D12Device* device, ID3D12GraphicsCommandList* command_list, std::vector<ComPtr<ID3D12Resource>>& pending_uploads, D3D12RHIBuffer& buffer);
bool ensureDispatchArgumentScratchBuffer(ID3D12Device* device, D3D12RHICommandBuffer& command_buffer);
void fillSamplerDesc(const RHISamplerCreateInfo& create_info, D3D12_SAMPLER_DESC& desc);
D3D12_PRIMITIVE_TOPOLOGY toD3D12PrimitiveTopology(RHIPrimitiveTopology topology);
D3D12_PRIMITIVE_TOPOLOGY_TYPE toD3D12PrimitiveTopologyType(RHIPrimitiveTopology topology);
D3D12_FILL_MODE toD3D12FillMode(RHIPolygonMode polygon_mode);
D3D12_CULL_MODE toD3D12CullMode(RHICullModeFlags cull_mode);
D3D12_BLEND toD3D12Blend(RHIBlendFactor factor);
D3D12_BLEND_OP toD3D12BlendOp(RHIBlendOp op);
D3D12_STENCIL_OP toD3D12StencilOp(RHIStencilOp op);
D3D12_DEPTH_STENCILOP_DESC toD3D12StencilOpDesc(const RHIStencilOpState& state);
UINT8 toD3D12ColorWriteMask(RHIColorComponentFlags flags);
UINT sampleCount(RHISampleCountFlagBits sample_count);
const char* semanticNameForLocation(uint32_t location);
UINT semanticIndexForLocation(uint32_t location);
DXGI_FORMAT indexFormat(RHIIndexType index_type);
const wchar_t* rayTracingExportOrDefault(const wchar_t* export_name, const wchar_t* default_export);
D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS rayTracingBuildFlags(const RHIAccelerationStructureBuildDesc& build_desc);
bool fillRayTracingBuildInputs(const RHIAccelerationStructureBuildDesc& build_desc, std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& geometries, D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs);
bool createRayTracingBuffer(ID3D12Device* device, uint64_t size, D3D12_RESOURCE_STATES initial_state, D3D12_RESOURCE_FLAGS flags, ID3D12Resource** resource);
bool createUploadBuffer(ID3D12Device* device, uint64_t size, ID3D12Resource** resource);
DXGI_FORMAT toVertexDXGIFormat(RHIFormat format);
UINT formatByteSize(RHIFormat format);
#endif // _WIN32

} // namespace d3d12_detail
} // namespace Piccolo
