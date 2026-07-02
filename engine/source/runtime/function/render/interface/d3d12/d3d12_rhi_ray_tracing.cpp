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
    const uint64_t scratch_size = std::max(prebuild_info.ScratchDataSizeInBytes,
                                           prebuild_info.UpdateScratchDataSizeInBytes);
    acceleration_structure_impl->scratch_size        = scratch_size;
    acceleration_structure_impl->update_scratch_size = prebuild_info.UpdateScratchDataSizeInBytes;

    const bool result_created =
        createRayTracingBuffer(m_d3d12_device.Get(),
                               prebuild_info.ResultDataMaxSizeInBytes,
                               D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                               acceleration_structure_impl->result.ReleaseAndGetAddressOf());
    const bool scratch_created =
        createRayTracingBuffer(m_d3d12_device.Get(),
                               scratch_size,
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
        rayTracingExportOrDefault(create_info->shader_library.raygen_export, kDefaultRayGenExport);
    const wchar_t* miss_export =
        rayTracingExportOrDefault(create_info->shader_library.miss_export, kDefaultMissExport);
    const wchar_t* closest_hit_export =
        rayTracingExportOrDefault(create_info->shader_library.closest_hit_export, kDefaultClosestHitExport);
    const wchar_t* hit_group_export =
        rayTracingExportOrDefault(create_info->shader_library.hit_group_export, kDefaultHitGroupExport);

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
    shader_config.MaxPayloadSizeInBytes = kRayTracingMaxPayloadSizeBytes;
    shader_config.MaxAttributeSizeInBytes = kRayTracingMaxAttributeSizeBytes;

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
        rayTracingExportOrDefault(create_info->raygen_export, kDefaultRayGenExport);
    const wchar_t* miss_export =
        rayTracingExportOrDefault(create_info->miss_export, kDefaultMissExport);
    const wchar_t* hit_group_export =
        rayTracingExportOrDefault(create_info->hit_group_export, kDefaultHitGroupExport);
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
        assert(false && "D3D12 cmdTraceRays must not be recorded inside a render pass");
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
    // Prefer the layout supplied with the dispatch; otherwise fall back to the pipeline's own layout.
    // Do not mutate the shared pipeline object here — it may be reused across dispatches/layouts.
    D3D12RHIPipelineLayout* effective_layout =
        dispatch_desc->layout != nullptr ? static_cast<D3D12RHIPipelineLayout*>(dispatch_desc->layout)
                                         : pipeline_impl->layout;
    if (effective_layout != nullptr && effective_layout->root_signature != nullptr)
    {
        d3d_command_buffer->bound_ray_tracing_pipeline_layout = effective_layout;
        d3d_command_buffer->bound_ray_tracing_root_signature = effective_layout->root_signature.Get();
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
void D3D12RHI::destroyRayTracingPipeline(RHIPipeline*& pipeline)
{
#ifdef _WIN32
    // Deleting the wrapper releases the ComPtr-held DXR state object.
    delete static_cast<D3D12RHIPipeline*>(pipeline);
#endif
    pipeline = nullptr;
}
} // namespace Piccolo
