#pragma once

#include "runtime/function/render/render_pass.h"

#include <cstdint>
#include <vector>

namespace Piccolo
{
    struct RenderPathTracingCollectedInstance;
    class RenderResource;

    class GpuSkinningPass : public RenderPass
    {
    public:
        void initialize(const RenderPassInitInfo* init_info) override final;
        void preparePassData(std::shared_ptr<RenderResourceBase> render_resource) override final;

        bool setup();
        bool dispatch();

    private:
        bool setupSkinComputePipeline();
        bool uploadJointMatrices(const std::vector<RenderPathTracingCollectedInstance>& instances);
        void dispatchSkinCompute(RHICommandBuffer* command_buffer,
                                 const std::vector<RenderPathTracingCollectedInstance>& instances);

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

        // Flat output buffer for skinned vertex data (PathTracingVertexData layout).
        // Compute shader writes per-instance data at computed offsets. Exposed to
        // PathTracingPass via RenderResource::getSkinnedVertexBuffer().
        RHIBuffer*       m_skinned_vertex_output_buffer {nullptr};
        RHIDeviceMemory* m_skinned_vertex_output_memory {nullptr};
        size_t           m_skinned_vertex_output_capacity {0};
    };
} // namespace Piccolo
