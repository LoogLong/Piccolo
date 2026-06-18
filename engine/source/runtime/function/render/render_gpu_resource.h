#pragma once

#include "runtime/function/render/interface/rhi.h"
#include "runtime/function/render/interface/rhi_allocation.h"
#include "runtime/core/math/vector2.h"
#include "runtime/core/math/vector3.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Piccolo
{
    struct RenderMeshGPUResource
    {
        bool enable_vertex_blending {false};

        uint32_t mesh_vertex_count {0};

        RHIBuffer*     mesh_vertex_position_buffer {nullptr};
        RHIAllocation* mesh_vertex_position_buffer_allocation {nullptr};

        RHIBuffer*     mesh_vertex_varying_enable_blending_buffer {nullptr};
        RHIAllocation* mesh_vertex_varying_enable_blending_buffer_allocation {nullptr};

        RHIBuffer*     mesh_vertex_joint_binding_buffer {nullptr};
        RHIAllocation* mesh_vertex_joint_binding_buffer_allocation {nullptr};

        RHIDescriptorSet* mesh_vertex_blending_descriptor_set {nullptr};

        RHIBuffer*     mesh_vertex_varying_buffer {nullptr};
        RHIAllocation* mesh_vertex_varying_buffer_allocation {nullptr};

        uint32_t mesh_index_count {0};
        RHIIndexType mesh_index_type {RHI_INDEX_TYPE_UINT16};

        RHIBuffer*     mesh_index_buffer {nullptr};
        RHIAllocation* mesh_index_buffer_allocation {nullptr};

        RHIAccelerationStructure* path_tracing_bottom_level_as {nullptr};
        bool                      path_tracing_blas_dirty {true};
        bool                      path_tracing_static_opaque_supported {false};

        std::vector<Vector3>  path_tracing_positions;
        std::vector<Vector3>  path_tracing_normals;
        std::vector<Vector3>  path_tracing_tangents;
        std::vector<Vector2>  path_tracing_texcoords;
        std::vector<uint32_t> path_tracing_indices;
        bool                  path_tracing_geometry_dirty {true};

        // Persistent per-instance skinned output buffers (multi-frame lifetime).
        // Stored here (not in RenderPathTracingCollectedInstance) because BLAS
        // and position buffers are GPU resources that survive across frames.
        struct SkinnedPathTracingResources
        {
            RHIAccelerationStructure* blas {nullptr};
            RHIBuffer*  skinned_position_buffer {nullptr};
            RHIDeviceMemory* skinned_position_memory {nullptr};
            uint32_t    vertex_count {0};
            uint32_t    index_count {0};
        };
        std::unordered_map<uint32_t, SkinnedPathTracingResources> path_tracing_skinned_resources;
    };

    struct RenderPBRMaterialGPUResource
    {
        RHIImage*      base_color_texture_image {nullptr};
        RHIImageView*  base_color_image_view {nullptr};
        RHIAllocation* base_color_image_allocation {nullptr};

        RHIImage*      metallic_roughness_texture_image {nullptr};
        RHIImageView*  metallic_roughness_image_view {nullptr};
        RHIAllocation* metallic_roughness_image_allocation {nullptr};

        RHIImage*      normal_texture_image {nullptr};
        RHIImageView*  normal_image_view {nullptr};
        RHIAllocation* normal_image_allocation {nullptr};

        RHIImage*      occlusion_texture_image {nullptr};
        RHIImageView*  occlusion_image_view {nullptr};
        RHIAllocation* occlusion_image_allocation {nullptr};

        RHIImage*      emissive_texture_image {nullptr};
        RHIImageView*  emissive_image_view {nullptr};
        RHIAllocation* emissive_image_allocation {nullptr};

        RHIBuffer*     material_uniform_buffer {nullptr};
        RHIAllocation* material_uniform_buffer_allocation {nullptr};

        RHIDescriptorSet* material_descriptor_set {nullptr};
    };
} // namespace Piccolo
