#pragma once



#include "runtime/function/render/render_pass_base.h"

#include "runtime/function/render/render_common.h"



#include <array>

#include <cstdint>

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

        void teardown() override;



    private:

        // Matches the shader PathTracingLight (48 bytes: 3 registers of 16B).
        struct PathTracingLightGpu
        {
            Vector3  position {};
            float    param0 {0.0f};
            Vector3  direction {};
            float    param1 {0.0f};
            Vector3  color {};
            uint32_t type {0u};
        };

        struct FrameData

        {

            Matrix4x4 proj_view_matrix_inv {Matrix4x4::IDENTITY};

            Vector3   camera_position {Vector3::ZERO};

            uint32_t  sample_index {0};

            uint32_t  extent[2] {0, 0};

            uint32_t  instance_count {0};

            uint32_t  reset_accumulation {0};

            // Lights live in a separate StructuredBuffer<PathTracingLight>
            // (g_lights, binding 1035); these counts index it.

            uint32_t  light_count {0};

            uint32_t  infinite_light_count {0};

            // Replaces PT_PLACEHOLDER_MAX_BOUNCES (plan Task 3 Step 4).
            uint32_t  max_bounces {4u};

            // Firefly cap; replaces hardcoded 100 in the kernel (plan Task 3
            // Step 6, plumbing completed in the optimization plan).
            uint32_t  max_path_intensity {100u};

            uint32_t  _padding_core {0};

        };



        void setupDescriptorSetLayout();

        void setupPipelineLayout();

        void setupDescriptorSets();

        bool setupRayTracingPipeline();

        bool setupShaderBindingTable();

        bool ensureFrameDataBuffers();

        void destroyFrameDataBuffers();

        // Unified light buffer (g_lights). Per-frame buffers, like FrameData, so
        // frames in flight do not race the CPU upload. buildLightBuffer also
        // detects light-parameter changes and signals reset (plan Task 2 Step 6).
        bool ensureLightBuffers();

        void destroyLightBuffers();

        bool buildLightBuffer(uint32_t  frame_index,
                              uint32_t& light_count,
                              uint32_t& infinite_light_count,
                              bool&     lights_changed);

        bool ensureAccumulationImage();

        // Plan 2026-07-12 §2.2: vendor-SDK-less fallback denoiser wiring.
        // The denoise compute pass consumes m_accumulation_image and writes
        // m_denoised_image (a copy of m_scene_output_image format/size).
        // m_denoise_history_image is the previous denoised output, used
        // for the temporal blend. enableDenoise() / disableDenoise() flip
        // m_denoise_enabled so the dispatch loop can branch.
        bool ensureDenoiseResources();
        void destroyDenoiseResources();
        void enableDenoise() { m_denoise_enabled = true; }
        void disableDenoise() { m_denoise_enabled = false; }
        void dispatchDenoise(uint32_t frame_index);

        bool ensureSkinnedVertexFallbackBuffer();

        void destroySkinnedVertexFallbackBuffer();

        bool updateFrameData(uint32_t instance_count);

        bool updateDescriptorSet(uint32_t frame_index);

        bool buildTopLevelAS(RenderScene& scene);

        void destroyTopLevelAS();

        void destroyAccumulationImage();

        void invalidateStaticDescriptors();

        void logInitializeSkipOnce(const char* reason);

        void logDispatchFailureOnce(const char* reason);



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

        RHIImageLayout m_scene_output_image_layout {RHI_IMAGE_LAYOUT_UNDEFINED};



        RHIImage*        m_accumulation_image {nullptr};

        RHIDeviceMemory* m_accumulation_memory {nullptr};

        RHIImageView*    m_accumulation_image_view {nullptr};

        RHIImageLayout   m_accumulation_image_layout {RHI_IMAGE_LAYOUT_UNDEFINED};

        // Plan 2026-07-12 §2.2: denoise compute pipeline + resources.
        RHIImage*        m_denoised_image {nullptr};
        RHIDeviceMemory* m_denoised_memory {nullptr};
        RHIImageView*    m_denoised_image_view {nullptr};
        RHIImageLayout   m_denoised_image_layout {RHI_IMAGE_LAYOUT_UNDEFINED};

        RHIImage*        m_denoise_history_image {nullptr};
        RHIDeviceMemory* m_denoise_history_memory {nullptr};
        RHIImageView*    m_denoise_history_image_view {nullptr};

        std::vector<RHIBuffer*>       m_denoise_constants_buffers;
        std::vector<RHIDeviceMemory*> m_denoise_constants_memories;

        RHIDescriptorSetLayout* m_denoise_descriptor_set_layout {nullptr};
        RHIPipelineLayout*      m_denoise_pipeline_layout {nullptr};
        RHIPipeline*            m_denoise_pipeline {nullptr};
        std::vector<RHIDescriptorSet*> m_denoise_descriptor_sets;
        // We allocate the denoise descriptor set from the engine's global
        // descriptor pool (RHI::getDescriptorPoor() returns it); sets are
        // destroyed individually via destroyDescriptorSet(nullptr, set)
        // without a pool argument, matching the existing pass pattern.

        bool m_denoise_enabled {false};
        bool m_denoise_diagnostics_logged {false};



        std::vector<RHIBuffer*>       m_frame_data_buffers;

        std::vector<RHIDeviceMemory*> m_frame_data_memories;

        std::vector<RHIBuffer*>           m_light_buffers;

        std::vector<RHIDeviceMemory*>     m_light_memories;

        // Last uploaded light set; used to detect changes and reset accumulation.
        std::vector<PathTracingLightGpu>  m_last_lights;



        RHIBuffer*       m_skinned_vertex_fallback_buffer {nullptr};

        RHIDeviceMemory* m_skinned_vertex_fallback_memory {nullptr};



        RHIDescriptorSetLayout* m_descriptor_set_layout {nullptr};

        std::vector<RHIDescriptorSet*> m_descriptor_sets;

        std::vector<bool>              m_static_descriptors_written;

        RHIPipelineLayout*      m_pipeline_layout {nullptr};

        RHIPipeline*            m_ray_tracing_pipeline {nullptr};

        RHIShaderBindingTable*  m_shader_binding_table {nullptr};



        RHIAccelerationStructure* m_top_level_as {nullptr};

        uint32_t                  m_tlas_instance_count {0};

        uint32_t                  m_top_level_as_generation {0};

        uint32_t  m_sample_index {0};

        // Cached spp/frame read from config (PathTracingMaxSamplesPerFrame).
        // Default 1; can be raised for offline-quality snapshots (FPS
        // drops proportionally, but a still camera converges faster).
        uint32_t  m_samples_per_frame {1u};

        // Tier-1 quality preset (plan 2026-07-12 §3) -> denoiser strength
        // mapping. 0.85 (Performance, default) keeps the kernel output
        // mostly intact; 0.25 (Quality) trusts the raw accumulation more.
        // Consumed by §2.2's spatial-bilateral pass when it lands.
        float     m_denoiser_strength {0.85f};

        // Plan Task 5 diagnostics: prints path-tracing config once per process.
        bool      m_diagnostics_logged {false};

        RHIExtent2D m_extent {0, 0};

        Matrix4x4 m_last_proj_view_matrix_inv {Matrix4x4::IDENTITY};

        Vector3   m_last_camera_position {Vector3::ZERO};

        bool      m_has_last_camera_state {false};

        bool      m_initialize_skip_logged {false};

        bool      m_dispatch_failure_logged {false};



        RHIImageView* m_irradiance_texture_view {nullptr};

        RHIImageView* m_specular_texture_view {nullptr};

        RHISampler*   m_linear_sampler {nullptr};

        std::vector<RHIDescriptorImageInfo> m_texture_array_views;

    };

} // namespace Piccolo

