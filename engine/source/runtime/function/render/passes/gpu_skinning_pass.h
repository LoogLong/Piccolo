#pragma once

#include "runtime/function/render/render_pass.h"

#include <cstdint>
#include <vector>

namespace Piccolo
{
    class RenderResource;
    struct RenderMeshGPUResource;

    // Output vertex layout from the GPU skinning compute shader.
    // Must match SkinnedVertexData in gpu_skinning.hlsli (64 bytes).
    struct GpuSkinnedVertexGPUData
    {
        Vector4 position {0.0f, 0.0f, 0.0f, 1.0f};
        Vector4 normal   {0.0f, 1.0f, 0.0f, 0.0f};
        Vector4 tangent  {1.0f, 0.0f, 0.0f, 0.0f};
        Vector4 texcoord {0.0f, 0.0f, 0.0f, 0.0f};
    };
    static_assert(sizeof(GpuSkinnedVertexGPUData) == 64, "Must match SkinnedVertexData layout in HLSL");

    // Minimal input data for skinning one mesh instance.
    // No path-tracing-specific fields.
    struct CollectedSkinnedMesh
    {
        RenderMeshGPUResource* mesh {nullptr};
        uint32_t               instance_id {0};
        uint32_t               joint_count {0};
        const Matrix4x4*       joint_matrices {nullptr};
    };

    class GpuSkinningPass : public RenderPass
    {
    public:
        void initialize(const RenderPassInitInfo* init_info) override final;
        void preparePassData(std::shared_ptr<RenderResourceBase> render_resource) override final;

        bool setup();
        bool dispatch();

    private:
        bool setupSkinComputePipeline();
        bool uploadJointMatrices(const std::vector<CollectedSkinnedMesh>& instances);
        void dispatchSkinCompute(RHICommandBuffer* command_buffer,
                                 const std::vector<CollectedSkinnedMesh>& instances);

        std::shared_ptr<RenderResource> m_render_resource_impl;

        // Compute pipeline resources
        RHIDescriptorSetLayout* m_skin_compute_descriptor_set_layout {nullptr};
        RHIPipelineLayout*      m_skin_compute_pipeline_layout {nullptr};
        RHIPipeline*            m_skin_compute_pipeline {nullptr};
        RHIDescriptorSet*       m_skin_compute_descriptor_set {nullptr};

        // Joint matrix upload buffer (host-visible, mapped per frame)
        RHIBuffer*       m_joint_matrix_buffer {nullptr};
        RHIDeviceMemory* m_joint_matrix_memory {nullptr};
        size_t           m_joint_matrix_buffer_capacity {0};

        // Persistent staging buffer for SkinComputeConstants (allocated once, mapped per dispatch)
        RHIBuffer*       m_skin_constants_buffer {nullptr};
        RHIDeviceMemory* m_skin_constants_memory {nullptr};

        // Flat output buffer for skinned vertex data (GpuSkinnedVertexGPUData layout).
        // Compute shader writes per-instance data at computed offsets. The buffer handle
        // is exposed to consumers via RenderResource::getSkinnedVertexBuffer().
        RHIBuffer*       m_skinned_vertex_output_buffer {nullptr};
        RHIDeviceMemory* m_skinned_vertex_output_memory {nullptr};
        size_t           m_skinned_vertex_output_capacity {0};
    };
} // namespace Piccolo
