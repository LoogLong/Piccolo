#pragma once

#include "runtime/function/render/render_pass_base.h"
#include "runtime/function/render/render_common.h"

#include <array>
#include <memory>
#include <vector>

namespace Piccolo
{
    class RenderResource;
    class RenderScene;

    struct PathTracingPassInitInfo : RenderPassInitInfo
    {
        RHIImage*     scene_output_image {nullptr};
        RHIImageView* scene_output_image_view {nullptr};
    };

    class PathTracingPass : public RenderPassBase
    {
    public:
        ~PathTracingPass() override;

        void initialize(const RenderPassInitInfo* init_info) override final;
        void preparePassData(std::shared_ptr<RenderResourceBase> render_resource) override final;

        bool dispatch();
        void updateAfterFramebufferRecreate(RHIImage* scene_output_image, RHIImageView* scene_output_image_view);
        void resetAccumulation();

    private:
        struct FrameData
        {
            Matrix4x4 proj_view_matrix_inv {Matrix4x4::IDENTITY};
            Vector3   camera_position {Vector3::ZERO};
            uint32_t  sample_index {0};
            uint32_t  extent[2] {0, 0};
            uint32_t  instance_count {0};
            uint32_t  reset_accumulation {0};
            Vector4   ambient_light {0.02f, 0.02f, 0.02f, 0.0f};
            RenderScenePointLight scene_point_lights[s_max_point_light_count] {};
            RenderSceneDirectionalLight scene_directional_light {};
            Matrix4x4 directional_light_proj_view {Matrix4x4::IDENTITY};
            uint32_t point_light_count {0};
            uint32_t _padding_light[3] {0, 0, 0};
        };

        void setupDescriptorSetLayout();
        void setupPipelineLayout();
        void setupDescriptorSet();
        bool setupRayTracingPipeline();
        bool setupShaderBindingTable();
        bool ensureFrameDataBuffer();
        bool ensureAccumulationImage();
        bool updateFrameData(uint32_t instance_count);
        bool updateDescriptorSet();
        bool buildTopLevelAS(RenderScene& scene);
        void destroyTopLevelAS();
        void destroyAccumulationImage();

        void transitionImage(RHIImage*              image,
                             RHIImageLayout        old_layout,
                             RHIImageLayout        new_layout,
                             RHIAccessFlags        src_access,
                             RHIAccessFlags        dst_access,
                             RHIPipelineStageFlags src_stage,
                             RHIPipelineStageFlags dst_stage);

        std::shared_ptr<RenderResource> m_render_resource_impl;

        RHIImage*     m_scene_output_image {nullptr};
        RHIImageView* m_scene_output_image_view {nullptr};

        RHIImage*        m_accumulation_image {nullptr};
        RHIDeviceMemory* m_accumulation_memory {nullptr};
        RHIImageView*    m_accumulation_image_view {nullptr};
        RHIImageLayout   m_accumulation_image_layout {RHI_IMAGE_LAYOUT_UNDEFINED};

        // Ping-pong accumulation: even frames write here, odd frames read from here
        RHIImage*        m_accumulation_prev_image {nullptr};
        RHIDeviceMemory* m_accumulation_prev_memory {nullptr};
        RHIImageView*    m_accumulation_prev_image_view {nullptr};
        RHIImageLayout   m_accumulation_prev_image_layout {RHI_IMAGE_LAYOUT_UNDEFINED};

        RHIBuffer*       m_frame_data_buffer {nullptr};
        RHIDeviceMemory* m_frame_data_memory {nullptr};

        RHIDescriptorSetLayout* m_descriptor_set_layout {nullptr};
        RHIDescriptorSet*       m_descriptor_set {nullptr};
        RHIPipelineLayout*      m_pipeline_layout {nullptr};
        RHIPipeline*            m_ray_tracing_pipeline {nullptr};
        RHIShaderBindingTable*  m_shader_binding_table {nullptr};

        RHIAccelerationStructure* m_top_level_as {nullptr};
        uint32_t                  m_tlas_instance_count {0};

        uint32_t  m_sample_index {0};
        RHIExtent2D m_extent {0, 0};
        Matrix4x4 m_last_proj_view_matrix_inv {Matrix4x4::IDENTITY};
        Vector3   m_last_camera_position {Vector3::ZERO};
        bool      m_has_last_camera_state {false};
        bool      m_static_descriptors_written {false};

        RHIImageView* m_irradiance_texture_view {nullptr};
        RHIImageView* m_specular_texture_view {nullptr};
        RHISampler*   m_linear_sampler {nullptr};
        std::vector<RHIDescriptorImageInfo> m_texture_array_views;
    };
} // namespace Piccolo