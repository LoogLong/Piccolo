#pragma once

#include "runtime/function/render/render_pass.h"

#include <array>
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

    // Stride of RWStructuredBuffer<float3> g_skinned_positions (HLSL 16-byte element alignment).
    constexpr uint32_t kGpuSkinnedPositionStorageStrideBytes = 16u;

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

        void teardown() override;

        RHIDescriptorSetLayout** getMeshDescriptorSetLayoutAddress() { return &m_skin_mesh_descriptor_set_layout; }

    private:
        bool setupSkinComputePipeline();
        bool uploadJointMatrices(const std::vector<CollectedSkinnedMesh>& instances);
        void dispatchSkinCompute(RHICommandBuffer* command_buffer,
                                 const std::vector<CollectedSkinnedMesh>& instances);

        void updateAllFrameSharedDescriptorSets();
        void updateFrameSharedDescriptorSet(RHIDescriptorSet* frame_set);
        void ensureInstanceDescriptorSet(RenderMeshGPUResource* mesh,
                                         RenderMeshGPUResource::SkinnedMeshOutput& output,
                                         uint8_t frame_index);
        void updateInstanceDescriptorSet(RenderMeshGPUResource::SkinnedMeshOutput& output,
                                           uint8_t frame_index,
                                           RHIBuffer* position_buffer);

        std::shared_ptr<RenderResource> m_render_resource_impl;

        RHIDescriptorSetLayout* m_skin_mesh_descriptor_set_layout {nullptr};
        RHIDescriptorSetLayout* m_skin_frame_descriptor_set_layout {nullptr};
        RHIDescriptorSetLayout* m_skin_instance_descriptor_set_layout {nullptr};
        RHIPipelineLayout*      m_skin_compute_pipeline_layout {nullptr};
        RHIPipeline*            m_skin_compute_pipeline {nullptr};
        std::array<RHIDescriptorSet*, k_rhi_max_frames_in_flight> m_frame_shared_descriptor_sets {};

        RHIBuffer*       m_joint_matrix_buffer {nullptr};
        RHIDeviceMemory* m_joint_matrix_memory {nullptr};
        size_t           m_joint_matrix_buffer_capacity {0};

        RHIBuffer*       m_skin_constants_buffer {nullptr};
        RHIDeviceMemory* m_skin_constants_memory {nullptr};

        RHIBuffer*       m_skinned_vertex_output_buffer {nullptr};
        RHIDeviceMemory* m_skinned_vertex_output_memory {nullptr};
        size_t           m_skinned_vertex_output_capacity {0};
    };
} // namespace Piccolo
