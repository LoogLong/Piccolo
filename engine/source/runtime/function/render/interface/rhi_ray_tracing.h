#pragma once

#include "runtime/function/render/interface/rhi_struct.h"

#include <cstddef>
#include <cstdint>

namespace Piccolo
{
    enum class RHIRayTracingSupportLevel : uint8_t
    {
        Unsupported = 0,
        Supported
    };

    enum class RHIAccelerationStructureType : uint8_t
    {
        BottomLevel = 0,
        TopLevel
    };

    struct RHIRayTracingCapabilities
    {
        RHIRayTracingSupportLevel support_level {RHIRayTracingSupportLevel::Unsupported};
        uint32_t                  max_recursion_depth {0};
        uint32_t                  shader_group_handle_size {0};
        uint32_t                  shader_group_handle_alignment {0};
        uint32_t                  shader_binding_table_alignment {0};
        bool                      supports_inline_ray_tracing {false};
    };

    static constexpr const wchar_t* kPathTracingRayGenExport     = L"PathTracingRayGen";
    static constexpr const wchar_t* kPathTracingMissExport       = L"PathTracingMiss";
    static constexpr const wchar_t* kPathTracingClosestHitExport = L"PathTracingClosestHit";
    static constexpr const wchar_t* kPathTracingHitGroupExport   = L"PathTracingHitGroup";

    struct RHIAccelerationStructureGeometryDesc
    {
        RHIBuffer*      vertex_position_buffer {nullptr};
        RHIDeviceSize   vertex_position_offset {0};
        uint32_t        vertex_count {0};
        uint32_t        vertex_stride {0};
        RHIBuffer*      index_buffer {nullptr};
        RHIDeviceSize   index_offset {0};
        uint32_t        index_count {0};
        RHIIndexType    index_type {RHI_INDEX_TYPE_UINT16};
        bool            opaque {true};
    };

    struct RHIAccelerationStructureInstanceDesc
    {
        RHIAccelerationStructure* bottom_level_as {nullptr};
        const float*              row_major_3x4_transform {nullptr};
        uint32_t                  instance_id {0};
        uint32_t                  hit_group_index {0};
        uint8_t                   instance_mask {0xFF};
        bool                      force_opaque {true};
    };

    struct RHIAccelerationStructureBuildDesc
    {
        RHIAccelerationStructureType                type {RHIAccelerationStructureType::BottomLevel};
        const RHIAccelerationStructureGeometryDesc* geometries {nullptr};
        uint32_t                                    geometry_count {0};
        const RHIAccelerationStructureInstanceDesc* instances {nullptr};
        uint32_t                                    instance_count {0};
        bool                                        prefer_fast_trace {true};
        bool                                        allow_update {false};
        bool                                        perform_update {false};
        RHIAccelerationStructure*                   source {nullptr};
    };

    struct RHIRayTracingShaderLibrary
    {
        const unsigned char* bytecode {nullptr};
        size_t               bytecode_size {0};
        const wchar_t*       raygen_export {nullptr};
        const wchar_t*       miss_export {nullptr};
        const wchar_t*       closest_hit_export {nullptr};
        const wchar_t*       hit_group_export {nullptr};
    };

    struct RHIRayTracingPipelineCreateInfo
    {
        RHIRayTracingShaderLibrary shader_library {};
        RHIPipelineLayout*         layout {nullptr};
        uint32_t                   max_recursion_depth {1};
    };

    struct RHIShaderBindingTableCreateInfo
    {
        RHIPipeline*    ray_tracing_pipeline {nullptr};
        const wchar_t*  raygen_export {nullptr};
        const wchar_t*  miss_export {nullptr};
        const wchar_t*  hit_group_export {nullptr};
    };

    struct RHIRayTracingDispatchDesc
    {
        RHIPipeline*           ray_tracing_pipeline {nullptr};
        RHIPipelineLayout*     layout {nullptr};
        RHIShaderBindingTable* shader_binding_table {nullptr};
        uint32_t               width {0};
        uint32_t               height {0};
        uint32_t               depth {1};
    };
} // namespace Piccolo
