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
        RenderScene* render_scene {nullptr};

        RHIImage*     scene_output_image {nullptr};
        RHIImageView* scene_output_image_view {nullptr};
        // Review 2026-07-16: the denoise history image is copied to/from
        // scene_output_image by cmdCopyImageToImage, which requires
        // source and destination to share the same DXGI format. The
        // HDR swapchain path emits scene_output as R16G16B16A16_TYPELESS
        // while the previous denoise history was hard-coded to
        // R32G32B32A32_SFLOAT -- D3D12 validation #874 rejected the
        // cross-format copy and poisoned the command list, producing
        // the E_INVALIDARG / E_FAIL spam.
        RHIFormat     scene_output_format {RHI_FORMAT_R8G8B8A8_UNORM};

        const RHIBuffer* mesh_perframe_storage_buffer {nullptr};
    };



    class PathTracingPass : public RenderPassBase

    {

    public:

        ~PathTracingPass() override;



        void initialize(const RenderPassInitInfo* init_info) override final;

        void preparePassData(std::shared_ptr<RenderResourceBase> render_resource) override final;



        bool dispatch();

        void updateAfterFramebufferRecreate(RHIImage* scene_output_image, RHIImageView* scene_output_image_view, RHIFormat scene_output_format);

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

            // Real mip count of the IBL specular cubemap. Drives the GGX
            // importance-sampled specular IBL LOD formula on the GPU
            // (PT_SpecularIBLLod) so the roughness -> mip curve lands on the
            // right LOD for the actual cubemap, not a hardcoded "kMips = 8"
            // placeholder. Plan 2026-07-15 Phase 5 A4.
            uint32_t  cubemap_mip_count {1u};

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

        // Plan 2026-07-16 Phase 6 B4: AOV output images for the A-SVGF
        // denoise (albedo + packed normal/depth). Lazily created / resized
        // alongside the accumulation image.
        bool ensureAovImages();
        void destroyAovImages();

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
        RHIFormat     m_scene_output_format {RHI_FORMAT_R8G8B8A8_UNORM};

        RHIImageView* m_scene_output_image_view {nullptr};

        RHIImageLayout m_scene_output_image_layout {RHI_IMAGE_LAYOUT_UNDEFINED};



        RHIImage*        m_accumulation_image {nullptr};

        RHIDeviceMemory* m_accumulation_memory {nullptr};

        RHIImageView*    m_accumulation_image_view {nullptr};

        RHIImageLayout   m_accumulation_image_layout {RHI_IMAGE_LAYOUT_UNDEFINED};

        // Plan 2026-07-16 Phase 6 B4: AOV outputs for the A-SVGF-style denoise.
        //   m_aov_albedo_image : RGBA16F, RGB = primary-hit base_color,
        //                                   A   = primary-hit metallic mask.
        //   m_aov_normal_depth : RGBA16F, RGB = face-forward world normal
        //                                   packed to [0,1] from [-1,1],
        //                                   A   = linear view-space z.
        // Both are written once per pixel from the first bounce of
        // path_tracing.lib.hlsl and consumed by the denoise shader as
        // range-weight features (see path_tracing_denoise.comp.hlsl).
        RHIImage*        m_aov_albedo_image {nullptr};
        RHIDeviceMemory* m_aov_albedo_memory {nullptr};
        RHIImageView*    m_aov_albedo_image_view {nullptr};
        RHIImageLayout   m_aov_albedo_image_layout {RHI_IMAGE_LAYOUT_UNDEFINED};

        RHIImage*        m_aov_normal_depth_image {nullptr};
        RHIDeviceMemory* m_aov_normal_depth_memory {nullptr};
        RHIImageView*    m_aov_normal_depth_image_view {nullptr};
        RHIImageLayout   m_aov_normal_depth_image_layout {RHI_IMAGE_LAYOUT_UNDEFINED};

        // Plan 2026-07-12 §2.2: denoise compute pipeline + resources.
        RHIImage*        m_denoised_image {nullptr};
        RHIDeviceMemory* m_denoised_memory {nullptr};
        RHIImageView*    m_denoised_image_view {nullptr};
        // Review 2026-07-17 S8: m_denoised_image_layout was a dead
        // field -- never read or written. The denoise shader writes to
        // m_denoised_image (= m_scene_output_image) via the u2 binding,
        // and m_scene_output_image_layout already tracks the post-denoise
        // layout. Removed.

        RHIImage*        m_denoise_history_image {nullptr};
        RHIDeviceMemory* m_denoise_history_memory {nullptr};
        RHIImageView*    m_denoise_history_image_view {nullptr};
        // Review 2026-07-17 B1': layout tracker for m_denoise_history_image.
        // Required because dispatch() first-frame path needs to transition
        // UNDEFINED -> TRANSFER_DST before the first cmdCopyImageToImage
        // (Vulkan validation reports VUID-VkImageMemoryBarrier-oldLayout
        // -00020 if the src layout is not UNDEFINED), and dispatchDenoise
        // needs to know whether to transition TRANSFER_DST -> SHADER_READ_ONLY
        // (next-frame use) or leave it.
        RHIImageLayout   m_denoise_history_image_layout {RHI_IMAGE_LAYOUT_UNDEFINED};

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

        // Plan 2026-07-16 Phase 6 B4: denoise default ON. The fallback
        // 5x5 bilateral + temporal blend (extended in the review with
        // AOV-based A-SVGF range weights) is the only practical way to
        // get usable 1-4 spp path-tracing output; without it the scene
        // is dominated by Monte Carlo grain (see issue with the
        // upper-left wall noise in the test scene). Path tracing at 1
        // spp without denoise is essentially a wireframe stress test.
        bool m_denoise_enabled {true};
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

        // Plan 2026-07-16 Phase 6 B3: warn-once flag for scene-light overflow.
        // Reset whenever the light set changes so a new scene with > 256
        // lights re-emits the warning instead of staying silent.
        bool      m_light_overflow_warned {false};

        // Review 2026-07-16 B3: per-frame latch that records whether
        // updateFrameData() set frame_data.reset_accumulation=1. The
        // multi-sample loop in dispatch() consults this to break out
        // early -- a reset inside the loop would otherwise cause later
        // TraceRays to write reset_accumulation=1 and overwrite the
        // earlier sample's contribution. Cleared at the start of every
        // updateFrameData() call.
        bool      m_force_accumulation_reset {false};

        RHIExtent2D m_extent {0, 0};

        Matrix4x4 m_last_proj_view_matrix_inv {Matrix4x4::IDENTITY};

        Vector3   m_last_camera_position {Vector3::ZERO};

        bool      m_has_last_camera_state {false};

        // Plan 2026-07-12 §1.1: sliding window over camera state so a brief
        // fast-pan doesn't kill convergence. m_camera_change_streak counts
        // consecutive frames in which the camera moved; only when the streak
        // reaches m_camera_change_threshold (3 frames) do we reset the
        // accumulation. Any frame where the camera matches its prior state
        // resets the streak to 0. Plan §1.1 estimates this saves ~80% of
        // the resets an editor fly-by would otherwise trigger.
        uint32_t  m_camera_change_streak {0u};
        uint32_t  m_camera_change_threshold {3u};

        bool      m_initialize_skip_logged {false};

        bool      m_dispatch_failure_logged {false};



        RHIImageView* m_irradiance_texture_view {nullptr};

        RHIImageView* m_specular_texture_view {nullptr};

        RHISampler*   m_linear_sampler {nullptr};

        std::vector<RHIDescriptorImageInfo> m_texture_array_views;

    };

} // namespace Piccolo

