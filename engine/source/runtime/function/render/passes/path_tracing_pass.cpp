#include "runtime/function/render/passes/path_tracing_pass.h"

#include "runtime/core/base/macro.h"
#include "runtime/function/render/passes/gpu_skinning_pass.h"
#include "runtime/function/render/render_resource.h"
#include "runtime/function/render/render_scene.h"
#include "runtime/function/render/render_shader_bytecode.h"
#include "runtime/resource/config_manager/config_manager.h"
#include "runtime/function/global/global_context.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

namespace Piccolo
{
    namespace
    {
        // Export names of the path tracing HLSL library entry points (see shader/hlsl/path_tracing.lib.hlsl).
        // These belong to the path tracing pass rather than the generic RHI interface.
        constexpr const wchar_t* kPathTracingRayGenExport     = L"PathTracingRayGen";
        constexpr const wchar_t* kPathTracingMissExport       = L"PathTracingMiss";
        constexpr const wchar_t* kPathTracingShadowMissExport = L"PathTracingShadowMiss";
        constexpr const wchar_t* kPathTracingClosestHitExport = L"PathTracingClosestHit";
        constexpr const wchar_t* kPathTracingHitGroupExport   = L"PathTracingHitGroup";

        constexpr uint32_t kPathTracingMaterialTextureCount = 1024u;
        // Must match PT_MAX_LIGHTS in path_tracing_light.hlsli.
        constexpr uint32_t kPathTracingMaxLightCount         = 32u;
        // Soft-sun half-angle (degrees). Task 3 makes this configurable.
        constexpr float kPathTracingDefaultSunHalfAngleDeg   = 0.53f;

        // Light types -- must match PT_LIGHT_* in path_tracing_light.hlsli.
        constexpr uint32_t kPtLightSky         = 0u;
        constexpr uint32_t kPtLightDirectional = 1u;
        constexpr uint32_t kPtLightPoint       = 2u;

        bool matrixEquals(const Matrix4x4& lhs, const Matrix4x4& rhs)
        {
            return std::memcmp(lhs.m_mat, rhs.m_mat, sizeof(lhs.m_mat)) == 0;
        }

        bool vectorEquals(const Vector3& lhs, const Vector3& rhs)
        {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        }

        constexpr float k_path_tracing_debug_color[4] = {0.9f, 0.5f, 0.2f, 1.0f};

        void tagPathTracingSceneOutput(RHI* rhi, RHIImage* image, RHIImageView* image_view)
        {
            if (rhi == nullptr)
            {
                return;
            }
            if (image != nullptr)
            {
                rhi->setDebugObjectName(image, "PathTracing.scene_output (backup_odd)");
            }
            if (image_view != nullptr)
            {
                rhi->setDebugObjectName(image_view, "PathTracing.scene_output_view (backup_odd)");
            }
        }

        void formatPathTracingTLASDebugName(char* out, size_t out_size, uint32_t generation, bool pending)
        {
            if (out == nullptr || out_size == 0)
            {
                return;
            }
            if (pending)
            {
                std::snprintf(out, out_size, "PathTracing.tlas.pending.gen%u", generation);
            }
            else
            {
                std::snprintf(out, out_size, "PathTracing.tlas.gen%u", generation);
            }
        }
    } // namespace

    PathTracingPass::~PathTracingPass()
    {
        teardown();
    }

    void PathTracingPass::logInitializeSkipOnce(const char* reason)
    {
        if (m_initialize_skip_logged)
        {
            return;
        }

        const bool rt_supported     = m_rhi != nullptr && m_rhi->supportsRayTracing();
        const bool bytecode_available = m_rhi != nullptr && pathTracingBytecodeAvailable(*m_rhi);
        LOG_INFO("Path tracing pass skipped during initialize: {} (ray_tracing_supported={}, "
                 "path_tracing_shader_bytecode={})",
                 reason,
                 rt_supported,
                 bytecode_available);
        m_initialize_skip_logged = true;
    }

    void PathTracingPass::logDispatchFailureOnce(const char* reason)
    {
        if (m_dispatch_failure_logged)
        {
            return;
        }

        LOG_WARN("Path tracing dispatch failed: {}", reason);
        m_dispatch_failure_logged = true;
    }

    void PathTracingPass::teardown()
    {
        if (m_rhi == nullptr)
        {
            return;
        }

        m_rhi->flushAllRetiredResources();

        if (m_shader_binding_table != nullptr)
        {
            m_rhi->destroyShaderBindingTable(m_shader_binding_table);
            m_shader_binding_table = nullptr;
        }
        if (m_ray_tracing_pipeline != nullptr)
        {
            m_rhi->destroyRayTracingPipeline(m_ray_tracing_pipeline);
            m_ray_tracing_pipeline = nullptr;
        }

        destroyTopLevelAS();
        m_rhi->flushAllRetiredResources();
        destroyAccumulationImage();
        destroyDenoiseResources();
        destroySkinnedVertexFallbackBuffer();
        destroyFrameDataBuffers();
        destroyLightBuffers();

        m_descriptor_sets.clear();
        m_static_descriptors_written.clear();
        m_rhi->destroyDescriptorSetLayout(m_descriptor_set_layout);
        m_descriptor_set_layout = nullptr;
        m_rhi->destroyPipelineLayout(m_pipeline_layout);
        m_pipeline_layout = nullptr;

        m_texture_array_views.clear();
    }

    void PathTracingPass::initialize(const RenderPassInitInfo* init_info)
    {
        const auto* path_tracing_init_info = static_cast<const PathTracingPassInitInfo*>(init_info);
        if (path_tracing_init_info != nullptr)
        {
            m_scene_output_image      = path_tracing_init_info->scene_output_image;
            m_scene_output_image_view = path_tracing_init_info->scene_output_image_view;
        }

        m_render_resource_impl = std::static_pointer_cast<RenderResource>(m_render_resource);
        if (m_rhi == nullptr)
        {
            logInitializeSkipOnce("RHI is null");
            return;
        }
        if (!supportsPathTracing(*m_rhi))
        {
            if (!m_rhi->supportsRayTracing())
            {
                logInitializeSkipOnce("ray tracing is not supported by the active backend");
            }
            else
            {
                logInitializeSkipOnce("path tracing shader bytecode is unavailable");
            }
            return;
        }

        try
        {
            setupDescriptorSetLayout();
            setupPipelineLayout();
            setupDescriptorSets();
            tagPathTracingSceneOutput(m_rhi.get(), m_scene_output_image, m_scene_output_image_view);
            if (!ensureFrameDataBuffers() || !ensureLightBuffers())
            {
                logInitializeSkipOnce("failed to create path tracing frame data or light buffer");
                return;
            }
            if (!setupRayTracingPipeline() || !setupShaderBindingTable())
            {
                logInitializeSkipOnce("failed to create ray tracing pipeline or shader binding table");
                return;
            }
            for (uint32_t frame_index = 0; frame_index < static_cast<uint32_t>(m_descriptor_sets.size()); ++frame_index)
            {
                updateDescriptorSet(frame_index);
            }
            LOG_INFO("Path tracing pass initialized successfully");
        }
        catch (const std::exception& e)
        {
            LOG_WARN("Path tracing pass initialization failed and will fall back to raster: {}", e.what());
            return;
        }
    }

    void PathTracingPass::preparePassData(std::shared_ptr<RenderResourceBase> render_resource)
    {
        m_render_resource_impl = std::static_pointer_cast<RenderResource>(render_resource);
    }

    bool PathTracingPass::dispatch()
    {
        if (m_rhi == nullptr)
        {
            logDispatchFailureOnce("RHI is null");
            return false;
        }
        if (m_render_resource_impl == nullptr)
        {
            logDispatchFailureOnce("render resource is null");
            return false;
        }
        if (m_scene_output_image == nullptr || m_scene_output_image_view == nullptr)
        {
            logDispatchFailureOnce("scene output image is not ready");
            return false;
        }
        if (!supportsPathTracing(*m_rhi))
        {
            logDispatchFailureOnce("path tracing is not supported on the active backend");
            return false;
        }

        auto render_scene = m_render_resource_impl->getCurrentRenderScene();
        if (render_scene == nullptr)
        {
            logDispatchFailureOnce("render scene is null");
            return false;
        }

        if (m_descriptor_set_layout == nullptr)
        {
            setupDescriptorSetLayout();
        }
        if (m_pipeline_layout == nullptr)
        {
            setupPipelineLayout();
        }
        if (m_descriptor_sets.empty())
        {
            setupDescriptorSets();
        }
        if (!ensureFrameDataBuffers() || !ensureLightBuffers() || !ensureAccumulationImage())
        {
            logDispatchFailureOnce("failed to ensure frame data, light buffer, or accumulation image");
            return false;
        }
        if (!setupRayTracingPipeline() || !setupShaderBindingTable())
        {
            logDispatchFailureOnce("failed to ensure ray tracing pipeline or shader binding table");
            return false;
        }
        if (!buildTopLevelAS(*render_scene) || m_top_level_as == nullptr || m_tlas_instance_count == 0)
        {
            logDispatchFailureOnce("top-level acceleration structure is empty or failed to build");
            resetAccumulation();
            return false;
        }
        if (!updateFrameData(m_tlas_instance_count))
        {
            logDispatchFailureOnce("failed to update path tracing frame data");
            return false;
        }

        RHICommandBuffer* command_buffer = m_rhi->getCurrentCommandBuffer();
        if (command_buffer == nullptr)
        {
            logDispatchFailureOnce("command buffer is null");
            return false;
        }

        tagPathTracingSceneOutput(m_rhi.get(), m_scene_output_image, m_scene_output_image_view);

        const float* debug_color = k_path_tracing_debug_color;
        m_rhi->pushEvent(command_buffer, "PathTracing.dispatch", debug_color);

        m_rhi->pushEvent(command_buffer, "PathTracing.layoutTransitions", debug_color);
        transitionImage(m_scene_output_image,
                        m_scene_output_image_layout,
                        RHI_IMAGE_LAYOUT_GENERAL,
                        RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_INPUT_ATTACHMENT_READ_BIT,
                        RHI_ACCESS_SHADER_WRITE_BIT,
                        RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
        m_scene_output_image_layout = RHI_IMAGE_LAYOUT_GENERAL;

        if (m_accumulation_image_layout != RHI_IMAGE_LAYOUT_GENERAL)
        {
            transitionImage(m_accumulation_image,
                            m_accumulation_image_layout,
                            RHI_IMAGE_LAYOUT_GENERAL,
                            0,
                            RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_SHADER_WRITE_BIT,
                            RHI_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
            m_accumulation_image_layout = RHI_IMAGE_LAYOUT_GENERAL;
        }

        m_rhi->popEvent(command_buffer);

        const uint32_t frame_index = m_rhi->getCurrentFrameIndex();
        m_rhi->pushEvent(command_buffer, "PathTracing.updateDescriptorSet", debug_color);
        if (!updateDescriptorSet(frame_index))
        {
            m_rhi->popEvent(command_buffer);
            m_rhi->popEvent(command_buffer);
            logDispatchFailureOnce("failed to update path tracing descriptors");
            return false;
        }
        m_rhi->popEvent(command_buffer);

        m_rhi->pushEvent(command_buffer, "PathTracing.traceRays", debug_color);
        m_rhi->cmdBindPipelinePFN(command_buffer,
                                  RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                  m_ray_tracing_pipeline);
        RHIDescriptorSet* frame_descriptor_set = m_descriptor_sets[frame_index];
        m_rhi->cmdBindDescriptorSetsPFN(command_buffer,
                                        RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                        m_pipeline_layout,
                                        0,
                                        1,
                                        &frame_descriptor_set,
                                        0,
                                        nullptr);

        RHIRayTracingDispatchDesc dispatch_desc {};
        dispatch_desc.ray_tracing_pipeline = m_ray_tracing_pipeline;
        dispatch_desc.layout               = m_pipeline_layout;
        dispatch_desc.shader_binding_table = m_shader_binding_table;
        dispatch_desc.width                = m_rhi->getSwapchainInfo().extent.width;
        dispatch_desc.height               = m_rhi->getSwapchainInfo().extent.height;
        dispatch_desc.depth                = 1;

        // Plan Task 3 Step 4 / Phase 2.1: dispatch N samples per frame so
        // a still camera converges faster at the cost of FPS. The
        // accumulation math in the kernel is sample_index-agnostic; we just
        // need to re-upload the FrameData uniform with the new sample_index
        // between iterations so the per-pixel RNG seeds differ.
        const uint32_t samples_per_frame = std::max(1u, m_samples_per_frame);
        for (uint32_t s = 0; s < samples_per_frame; ++s)
        {
            if (s > 0)
            {
                ++m_sample_index;
                if (!updateFrameData(m_tlas_instance_count))
                {
                    m_rhi->popEvent(command_buffer);
                    logDispatchFailureOnce("failed to update path tracing frame data (sample-loop)");
                    return false;
                }
            }
            m_rhi->cmdTraceRays(command_buffer, &dispatch_desc);
        }
        m_rhi->popEvent(command_buffer);

        // Plan 2026-07-12 §2.2: optional vendor-SDK-less fallback denoiser.
        // Runs as a compute pass that reads m_accumulation_image and writes
        // a denoised version into m_scene_output_image (the swapchain-present
        // image). Disabled by default; opt-in via PathTracingPass::enableDenoise
        // (or future ini hook). Default-off keeps existing visuals untouched
        // for users who already have a vendor denoiser.
        //
        // Plan §1.3 (first-frame latency): skip denoise on sample_index == 0.
        // The denoise history is undefined, so a temporal blend would be
        // degenerate; the spatial-only path would blur the noisy first frame
        // into mush. Pass-through keeps the first frame visible immediately.
        // On the very first frame after a reset we also seed the history
        // buffer from the raw accumulation so the *second* frame's denoise
        // has a sane temporal prior (no flicker from undefined memory).
        if (m_denoise_enabled)
        {
            if (ensureDenoiseResources())
            {
                if (m_sample_index == 0u)
                {
                    // First-frame seed: copy accumulation -> history directly.
                    m_rhi->cmdCopyImageToImage(command_buffer,
                                              m_accumulation_image,
                                              RHI_IMAGE_ASPECT_COLOR_BIT,
                                              m_denoise_history_image,
                                              RHI_IMAGE_ASPECT_COLOR_BIT,
                                              m_extent.width,
                                              m_extent.height);
                }
                else
                {
                    dispatchDenoise(frame_index);
                }
            }
        }

        m_rhi->pushEvent(command_buffer, "PathTracing.postTraceBarriers", debug_color);
        transitionImage(m_scene_output_image,
                        RHI_IMAGE_LAYOUT_GENERAL,
                        RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        RHI_ACCESS_SHADER_WRITE_BIT,
                        RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_INPUT_ATTACHMENT_READ_BIT,
                        RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                        RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        m_scene_output_image_layout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        m_rhi->popEvent(command_buffer);
        m_rhi->popEvent(command_buffer);

        ++m_sample_index;
        if (render_scene->isPathTracingAccumulationDirty())
        {
            render_scene->clearPathTracingAccumulationDirty();
        }
        return true;
    }

    void PathTracingPass::updateAfterFramebufferRecreate(RHIImage* scene_output_image, RHIImageView* scene_output_image_view)
    {
        m_scene_output_image      = scene_output_image;
        m_scene_output_image_view = scene_output_image_view;
        tagPathTracingSceneOutput(m_rhi.get(), m_scene_output_image, m_scene_output_image_view);
        m_scene_output_image_layout = RHI_IMAGE_LAYOUT_UNDEFINED;
        destroyAccumulationImage();
        // Plan §2.2: denoise images are sized to the swapchain extent; they
        // must follow framebuffer recreates.
        destroyDenoiseResources();
        resetAccumulation();
    }

    void PathTracingPass::resetAccumulation()
    {
        m_sample_index = 0;
        m_accumulation_image_layout = RHI_IMAGE_LAYOUT_UNDEFINED;
        if (auto render_scene = m_render_resource_impl != nullptr ? m_render_resource_impl->getCurrentRenderScene() : nullptr)
        {
            render_scene->markPathTracingAccumulationDirty();
        }
    }

    void PathTracingPass::setupDescriptorSetLayout()
    {
        if (m_descriptor_set_layout != nullptr || m_rhi == nullptr)
        {
            return;
        }

        RHIDescriptorSetLayoutBinding bindings[15] {};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = RHI_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = RHI_SHADER_STAGE_RAYGEN_BIT_KHR | RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[1].binding         = 1;
        bindings[1].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = RHI_SHADER_STAGE_RAYGEN_BIT_KHR;

        bindings[2].binding         = 2;
        bindings[2].descriptorType  = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags      = RHI_SHADER_STAGE_RAYGEN_BIT_KHR | RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[3].binding         = 3;
        bindings[3].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags      = RHI_SHADER_STAGE_RAYGEN_BIT_KHR;

        bindings[4].binding         = 4;
        bindings[4].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags      = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[5].binding         = 5;
        bindings[5].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags      = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        // Material/instance/texture/sampler bindings are now read from raygen
        // (material fetch + sky sampling moved out of closest-hit into the
        // raygen path step -- see path_tracing_core.hlsli), so they must be
        // visible to the raygen stage as well as closest-hit/miss.
        bindings[6].binding         = 6;
        bindings[6].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[6].descriptorCount = 1;
        bindings[6].stageFlags      = RHI_SHADER_STAGE_RAYGEN_BIT_KHR | RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[7].binding         = 7;
        bindings[7].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[7].descriptorCount = 1;
        bindings[7].stageFlags      = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[8].binding         = 8;
        bindings[8].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[8].descriptorCount = 1;
        bindings[8].stageFlags      = RHI_SHADER_STAGE_RAYGEN_BIT_KHR | RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[9].binding         = 9;
        bindings[9].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[9].descriptorCount = 1;
        // t9 (g_irradiance_texture) -- was MISS | CLOSEST_HIT but is also
        // read from the raygen via EstimateEnvironmentAmbient on every
        // primary hit. RAYGEN_BIT_KHR is mandatory so Vulkan validation
        // doesn't trigger "shader uses descriptor but stage isn't permitted"
        // (D3D12's global root signature is permissive by default and masked
        // this gap; Vulkan isn't).
        bindings[9].stageFlags      = RHI_SHADER_STAGE_RAYGEN_BIT_KHR
                                   | RHI_SHADER_STAGE_MISS_BIT_KHR
                                   | RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[10].binding         = 10;
        bindings[10].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[10].descriptorCount = 1;
        bindings[10].stageFlags      = RHI_SHADER_STAGE_RAYGEN_BIT_KHR | RHI_SHADER_STAGE_MISS_BIT_KHR | RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[11].binding         = 11;
        bindings[11].descriptorType  = RHI_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings[11].descriptorCount = 1024;
        bindings[11].stageFlags      = RHI_SHADER_STAGE_RAYGEN_BIT_KHR | RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[12].binding         = 12;
        bindings[12].descriptorType  = RHI_DESCRIPTOR_TYPE_SAMPLER;
        bindings[12].descriptorCount = 1;
        bindings[12].stageFlags      = RHI_SHADER_STAGE_RAYGEN_BIT_KHR | RHI_SHADER_STAGE_MISS_BIT_KHR | RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[13].binding         = 1035;  // t1035: g_lights (unified light buffer)
        bindings[13].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[13].descriptorCount = 1;
        bindings[13].stageFlags      = RHI_SHADER_STAGE_RAYGEN_BIT_KHR;

        bindings[14].binding         = 1036;  // t1036: g_skinned_vertices
        bindings[14].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[14].descriptorCount = 1;
        bindings[14].stageFlags      = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        RHIDescriptorSetLayoutCreateInfo create_info {};
        create_info.sType        = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        create_info.bindingCount = static_cast<uint32_t>(std::size(bindings));
        create_info.pBindings    = bindings;
        if (RHI_SUCCESS != m_rhi->createDescriptorSetLayout(&create_info, m_descriptor_set_layout))
        {
            throw std::runtime_error("create path tracing descriptor set layout");
        }
    }

    void PathTracingPass::setupPipelineLayout()
    {
        if (m_pipeline_layout != nullptr || m_rhi == nullptr || m_descriptor_set_layout == nullptr)
        {
            return;
        }

        RHIDescriptorSetLayout* set_layouts[1] = {m_descriptor_set_layout};
        RHIPipelineLayoutCreateInfo create_info {};
        create_info.sType          = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        create_info.setLayoutCount = 1;
        create_info.pSetLayouts    = set_layouts;
        if (RHI_SUCCESS != m_rhi->createPipelineLayout(&create_info, m_pipeline_layout))
        {
            throw std::runtime_error("create path tracing pipeline layout");
        }
    }

    void PathTracingPass::setupDescriptorSets()
    {
        if (!m_descriptor_sets.empty() || m_rhi == nullptr || m_descriptor_set_layout == nullptr)
        {
            return;
        }

        const uint32_t frame_count = m_rhi->getMaxFramesInFlight();
        m_descriptor_sets.resize(frame_count, nullptr);
        m_static_descriptors_written.assign(frame_count, false);

        RHIDescriptorSetAllocateInfo allocate_info {};
        allocate_info.sType              = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool     = m_rhi->getDescriptorPoor();
        allocate_info.descriptorSetCount = 1;
        allocate_info.pSetLayouts        = &m_descriptor_set_layout;

        for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index)
        {
            if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&allocate_info, m_descriptor_sets[frame_index]))
            {
                throw std::runtime_error("allocate path tracing descriptor set");
            }

            char debug_name[64];
            std::snprintf(debug_name, sizeof(debug_name), "PathTracing.descriptor_set[%u]", frame_index);
            m_rhi->setDebugObjectName(m_descriptor_sets[frame_index], debug_name);
        }
    }

    bool PathTracingPass::ensureFrameDataBuffers()
    {
        if (m_rhi == nullptr)
        {
            return false;
        }

        const uint32_t frame_count = m_rhi->getMaxFramesInFlight();
        if (m_frame_data_buffers.size() == frame_count && m_frame_data_memories.size() == frame_count)
        {
            return m_frame_data_buffers[0] != nullptr && m_frame_data_memories[0] != nullptr;
        }

        destroyFrameDataBuffers();
        m_frame_data_buffers.resize(frame_count, nullptr);
        m_frame_data_memories.resize(frame_count, nullptr);

        for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index)
        {
            m_rhi->createBuffer(sizeof(FrameData),
                                RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                m_frame_data_buffers[frame_index],
                                m_frame_data_memories[frame_index]);
            if (m_frame_data_buffers[frame_index] == nullptr || m_frame_data_memories[frame_index] == nullptr)
            {
                return false;
            }

            char debug_name[64];
            std::snprintf(debug_name, sizeof(debug_name), "PathTracing.frame_data[%u]", frame_index);
            m_rhi->setDebugObjectName(m_frame_data_buffers[frame_index], debug_name);
        }

        return true;
    }

    void PathTracingPass::destroyFrameDataBuffers()
    {
        if (m_rhi == nullptr)
        {
            m_frame_data_buffers.clear();
            m_frame_data_memories.clear();
            return;
        }

        for (RHIBuffer* buffer : m_frame_data_buffers)
        {
            if (buffer != nullptr)
            {
                m_rhi->destroyBuffer(buffer);
            }
        }
        for (RHIDeviceMemory* memory : m_frame_data_memories)
        {
            if (memory != nullptr)
            {
                m_rhi->freeMemory(memory);
            }
        }
        m_frame_data_buffers.clear();
        m_frame_data_memories.clear();
    }

    bool PathTracingPass::ensureLightBuffers()
    {
        if (m_rhi == nullptr)
        {
            return false;
        }

        const uint32_t frame_count = m_rhi->getMaxFramesInFlight();
        if (m_light_buffers.size() == frame_count && m_light_memories.size() == frame_count)
        {
            return m_light_buffers[0] != nullptr && m_light_memories[0] != nullptr;
        }

        destroyLightBuffers();
        m_light_buffers.resize(frame_count, nullptr);
        m_light_memories.resize(frame_count, nullptr);

        const size_t buffer_size = static_cast<size_t>(kPathTracingMaxLightCount) * sizeof(PathTracingLightGpu);
        for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index)
        {
            m_rhi->createBuffer(buffer_size,
                                RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                m_light_buffers[frame_index],
                                m_light_memories[frame_index]);
            if (m_light_buffers[frame_index] == nullptr || m_light_memories[frame_index] == nullptr)
            {
                return false;
            }

            char debug_name[64];
            std::snprintf(debug_name, sizeof(debug_name), "PathTracing.lights[%u]", frame_index);
            m_rhi->setDebugObjectName(m_light_buffers[frame_index], debug_name);
        }
        return true;
    }

    void PathTracingPass::destroyLightBuffers()
    {
        if (m_rhi == nullptr)
        {
            m_light_buffers.clear();
            m_light_memories.clear();
            return;
        }
        for (RHIBuffer* buffer : m_light_buffers)
        {
            if (buffer != nullptr)
            {
                m_rhi->destroyBuffer(buffer);
            }
        }
        for (RHIDeviceMemory* memory : m_light_memories)
        {
            if (memory != nullptr)
            {
                m_rhi->freeMemory(memory);
            }
        }
        m_light_buffers.clear();
        m_light_memories.clear();
    }

    bool PathTracingPass::buildLightBuffer(uint32_t  frame_index,
                                           uint32_t& light_count,
                                           uint32_t& infinite_light_count,
                                           bool&     lights_changed)
    {
        light_count          = 0;
        infinite_light_count = 0;
        lights_changed       = false;

        if (m_rhi == nullptr || m_render_resource_impl == nullptr)
        {
            return false;
        }
        if (frame_index >= m_light_buffers.size() || frame_index >= m_light_memories.size())
        {
            return false;
        }
        RHIDeviceMemory* light_memory = m_light_memories[frame_index];
        if (light_memory == nullptr)
        {
            return false;
        }

        const MeshPerframeStorageBufferObject& raster_frame =
            m_render_resource_impl->m_mesh_perframe_storage_buffer_object;

        // Build the unified light list: infinite (directional) first, then points.
        std::vector<PathTracingLightGpu> lights;
        lights.reserve(1u + raster_frame.point_light_num);

        {
            PathTracingLightGpu dir {};
            dir.type      = kPtLightDirectional;
            dir.direction = raster_frame.scene_directional_light.direction;
            // Bug fix 2026-07-12 (B): the raster pipeline's directional-light
            // color is a unit-candela value (1.0 ~= noon sun in deferred
            // shading units). At 1 spp/frame with a 0.53 deg soft-sun cone,
            // the NEE sample inside the cone is rare; without a scale the
            // scene reads as nearly black. Multiply the color by
            // PathTracingSunIrradianceScale (default 5.0, see config_manager.h)
            // so the rare sun-aligned NEE sample carries physically meaningful
            // energy. Raster pipeline is untouched -- only the PT light buffer
            // sees the scaled value.
            Vector3 sun_color = raster_frame.scene_directional_light.color;
            float  sun_scale  = 1.0f;
            if (auto cfg = g_runtime_global_context.m_config_manager)
            {
                sun_scale = cfg->getPathTracingSunIrradianceScale();
            }
            if (sun_scale > 0.0f)
            {
                sun_color = sun_color * sun_scale;
            }
            dir.color     = sun_color;
            // Soft-sun half-angle. Configurable via PathTracingDirectionalAngleDeg
            // (plan Task 3 Step 4); falls back to the compile-time default if
            // the key is absent / non-numeric / 0.
            float half_angle_deg = kPathTracingDefaultSunHalfAngleDeg;
            if (auto cfg = g_runtime_global_context.m_config_manager)
            {
                half_angle_deg = cfg->getPathTracingDirectionalAngleDeg();
                if (half_angle_deg <= 0.0f) half_angle_deg = kPathTracingDefaultSunHalfAngleDeg;
            }
            const float half_angle_rad = half_angle_deg * (3.14159265358979f / 180.0f);
            dir.param0 = std::sin(half_angle_rad);
            lights.push_back(dir);
        }
        // Sky light (Task 4 M3 fix): outdoor/sky-box scenes need a sky entry to
        // feed the diffuse env contribution via NEE. Until cube-side access is
        // available on the CPU, we use a neutral representative tint for the
        // diagnostic color field (the actual Li comes from sampling
        // g_irradiance_texture in the shader). We always emit the sky light;
        // for interior scenes its shadow ray terminates on the first interior
        // wall and the sample contributes nothing, which is the correct
        // behaviour for a source behind opaque geometry.
        {
            PathTracingLightGpu sky {};
            sky.type     = kPtLightSky;
            sky.color    = Vector3(0.5f, 0.55f, 0.65f); // neutral sky-tint; unused by SampleLight
            lights.push_back(sky);
        }
        infinite_light_count = static_cast<uint32_t>(lights.size());

        const uint32_t point_count = std::min(raster_frame.point_light_num, s_max_point_light_count);
        for (uint32_t i = 0; i < point_count; ++i)
        {
            const RenderScenePointLight& pl = raster_frame.scene_point_lights[i];
            PathTracingLightGpu p {};
            p.type     = kPtLightPoint;
            p.position = pl.position;
            p.color    = pl.intensity;
            p.param0   = 0.0f; // delta point light (radius 0)
            lights.push_back(p);
        }
        light_count = static_cast<uint32_t>(lights.size());

        // Detect changes -> reset accumulation (plan Task 2 Step 6).
        if (m_last_lights.size() != lights.size() ||
            std::memcmp(m_last_lights.data(),
                        lights.data(),
                        lights.size() * sizeof(PathTracingLightGpu)) != 0)
        {
            lights_changed = true;
            m_last_lights = lights;
        }

        // Upload (pad to kPathTracingMaxLightCount with zeroed entries).
        std::vector<PathTracingLightGpu> upload_lights(kPathTracingMaxLightCount);
        const uint32_t copy_count = std::min(light_count, kPathTracingMaxLightCount);
        for (uint32_t i = 0; i < copy_count; ++i)
        {
            upload_lights[i] = lights[i];
        }

        void* mapped_data = nullptr;
        const size_t upload_bytes = upload_lights.size() * sizeof(PathTracingLightGpu);
        if (!m_rhi->mapMemory(light_memory, 0, upload_bytes, 0, &mapped_data) || mapped_data == nullptr)
        {
            return false;
        }
        std::memcpy(mapped_data, upload_lights.data(), upload_bytes);
        m_rhi->unmapMemory(light_memory);

        light_count = copy_count; // cap to the buffer size the shader sees
        return true;
    }

    bool PathTracingPass::setupRayTracingPipeline()
    {
        if (m_ray_tracing_pipeline != nullptr)
        {
            return true;
        }
        if (m_rhi == nullptr || m_pipeline_layout == nullptr)
        {
            return false;
        }

        // Backend-neutral selection: SPIR-V for Vulkan, DXIL for D3D12 (same HLSL source).
        const std::vector<unsigned char>& bytecode = PICCOLO_RENDER_SHADER_BYTECODE(m_rhi, PATH_TRACING_LIB);
        if (bytecode.empty())
        {
            LOG_WARN("Path tracing shader library is missing for the active backend; falling back to raster");
            return false;
        }

        RHIRayTracingPipelineCreateInfo create_info {};
        create_info.layout                                    = m_pipeline_layout;
        // Iterative path loop lives in raygen; the closest-hit shader no longer
        // recurses (no indirect TraceRay) and shadow rays use
        // ACCEPT_FIRST_HIT_AND_END_SEARCH (not nested). All TraceRay calls
        // originate from raygen at depth 1, so the pipeline only needs depth 1
        // (lower driver stack overhead, less payload pressure).
        create_info.max_recursion_depth                       = 1;
        create_info.shader_library.bytecode                   = bytecode.data();
        create_info.shader_library.bytecode_size              = bytecode.size();
        create_info.shader_library.raygen_export              = kPathTracingRayGenExport;
        create_info.shader_library.miss_export                = kPathTracingMissExport;
        create_info.shader_library.shadow_miss_export         = kPathTracingShadowMissExport;
        create_info.shader_library.closest_hit_export         = kPathTracingClosestHitExport;
        create_info.shader_library.hit_group_export           = kPathTracingHitGroupExport;
        if (!m_rhi->createRayTracingPipeline(&create_info, m_ray_tracing_pipeline) ||
            m_ray_tracing_pipeline == nullptr)
        {
            return false;
        }

        m_rhi->setDebugObjectName(m_ray_tracing_pipeline, "PathTracing.rt_pipeline");
        return true;
    }

    bool PathTracingPass::setupShaderBindingTable()
    {
        if (m_shader_binding_table != nullptr)
        {
            return true;
        }
        if (m_rhi == nullptr || m_ray_tracing_pipeline == nullptr)
        {
            return false;
        }

        RHIShaderBindingTableCreateInfo create_info {};
        create_info.ray_tracing_pipeline = m_ray_tracing_pipeline;
        create_info.raygen_export        = kPathTracingRayGenExport;
        create_info.miss_export          = kPathTracingMissExport;
        create_info.shadow_miss_export   = kPathTracingShadowMissExport;
        create_info.hit_group_export     = kPathTracingHitGroupExport;
        return m_rhi->createShaderBindingTable(&create_info, m_shader_binding_table) &&
               m_shader_binding_table != nullptr;
    }

    void PathTracingPass::destroySkinnedVertexFallbackBuffer()
    {
        if (m_rhi == nullptr)
        {
            m_skinned_vertex_fallback_buffer = nullptr;
            m_skinned_vertex_fallback_memory = nullptr;
            return;
        }
        if (m_skinned_vertex_fallback_buffer != nullptr)
        {
            m_rhi->destroyBuffer(m_skinned_vertex_fallback_buffer);
            m_skinned_vertex_fallback_buffer = nullptr;
        }
        if (m_skinned_vertex_fallback_memory != nullptr)
        {
            m_rhi->freeMemory(m_skinned_vertex_fallback_memory);
            m_skinned_vertex_fallback_memory = nullptr;
        }
    }

    bool PathTracingPass::ensureSkinnedVertexFallbackBuffer()
    {
        if (m_rhi == nullptr)
        {
            return false;
        }
        if (m_skinned_vertex_fallback_buffer != nullptr && m_skinned_vertex_fallback_memory != nullptr)
        {
            return true;
        }

        destroySkinnedVertexFallbackBuffer();
        static constexpr size_t k_fallback_skinned_vertex_buffer_size = 16;
        m_rhi->createBuffer(k_fallback_skinned_vertex_buffer_size,
                            RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            m_skinned_vertex_fallback_buffer,
                            m_skinned_vertex_fallback_memory);
        return m_skinned_vertex_fallback_buffer != nullptr && m_skinned_vertex_fallback_memory != nullptr;
    }

    bool PathTracingPass::ensureAccumulationImage()
    {
        if (m_rhi == nullptr)
        {
            return false;
        }
        const RHIExtent2D extent = m_rhi->getSwapchainInfo().extent;
        if (m_accumulation_image != nullptr &&
            m_extent.width == extent.width &&
            m_extent.height == extent.height)
        {
            return true;
        }

        destroyAccumulationImage();
        if (extent.width == 0 || extent.height == 0)
        {
            return false;
        }

        m_rhi->createImage(extent.width,
                           extent.height,
                           RHI_FORMAT_R32G32B32A32_SFLOAT,
                           RHI_IMAGE_TILING_OPTIMAL,
                           RHI_IMAGE_USAGE_STORAGE_BIT,
                           RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           m_accumulation_image,
                           m_accumulation_memory,
                           0,
                           1,
                           1);
        m_rhi->createImageView(m_accumulation_image,
                               RHI_FORMAT_R32G32B32A32_SFLOAT,
                               RHI_IMAGE_ASPECT_COLOR_BIT,
                               RHI_IMAGE_VIEW_TYPE_2D,
                               1,
                               1,
                               m_accumulation_image_view);
        m_accumulation_image_layout = RHI_IMAGE_LAYOUT_UNDEFINED;
        m_extent = extent;
        m_rhi->setDebugObjectName(m_accumulation_image, "PathTracing.accumulation");
        m_rhi->setDebugObjectName(m_accumulation_image_view, "PathTracing.accumulation_view");
        resetAccumulation();
        return m_accumulation_image != nullptr && m_accumulation_image_view != nullptr;
    }

    bool PathTracingPass::updateFrameData(uint32_t instance_count)
    {
        if (m_rhi == nullptr || m_render_resource_impl == nullptr || m_frame_data_memories.empty())
        {
            return false;
        }

        const uint32_t frame_index = m_rhi->getCurrentFrameIndex();
        if (frame_index >= m_frame_data_buffers.size() || frame_index >= m_frame_data_memories.size())
        {
            return false;
        }

        RHIDeviceMemory* frame_data_memory = m_frame_data_memories[frame_index];
        if (frame_data_memory == nullptr)
        {
            return false;
        }

        const Matrix4x4& current_proj_view_inv =
            m_render_resource_impl->m_mesh_perframe_storage_buffer_object.proj_view_matrix_inv;
        const Vector3& current_camera_position =
            m_render_resource_impl->m_mesh_perframe_storage_buffer_object.camera_position;
        const RHIExtent2D extent = m_rhi->getSwapchainInfo().extent;

        bool resetting = false;

        const bool camera_changed =
            !m_has_last_camera_state ||
            !matrixEquals(m_last_proj_view_matrix_inv, current_proj_view_inv) ||
            !vectorEquals(m_last_camera_position, current_camera_position) ||
            m_extent.width != extent.width ||
            m_extent.height != extent.height;

        if (camera_changed)
        {
            // Plan §1.1: don't reset on a single-frame jitter. Only when
            // the streak of consecutive camera changes reaches the
            // threshold do we wipe the accumulation. A still frame resets
            // the streak so the next pan starts fresh.
            ++m_camera_change_streak;
            m_last_proj_view_matrix_inv = current_proj_view_inv;
            m_last_camera_position      = current_camera_position;
            m_has_last_camera_state     = true;
            if (m_camera_change_streak >= m_camera_change_threshold)
            {
                m_sample_index = 0;
                resetting = true;
                m_camera_change_streak = 0u;
            }
        }
        else
        {
            m_camera_change_streak = 0u;
        }

        if (auto render_scene = m_render_resource_impl->getCurrentRenderScene())
        {
            if (render_scene->isPathTracingAccumulationDirty())
            {
                m_sample_index = 0;
                resetting = true;
            }
        }

        // Build the unified light buffer; reset accumulation when light params
        // change (plan Task 2 Step 6). Done before uploading FrameData so the
        // reset sample_index + light counts are consistent this frame.
        uint32_t light_count          = 0;
        uint32_t infinite_light_count = 0;
        bool     lights_changed       = false;
        if (!buildLightBuffer(frame_index, light_count, infinite_light_count, lights_changed))
        {
            return false;
        }
        if (lights_changed)
        {
            m_sample_index = 0;
            resetting = true;
        }

        FrameData frame_data {};
        // Raygen maps top-left pixel → NDC y = -1 (no UV Y flip). Y-up backends
        // (D3D12) need the inverse converted into that NDC space; Y-down (Vulkan)
        // already matches.
        Matrix4x4 proj_view_inv_for_rays = current_proj_view_inv;
        if (m_rhi->getClipSpaceConvention() == ClipSpaceConvention::YUpNDC)
        {
            const Matrix4x4 y_flip(1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
            proj_view_inv_for_rays = current_proj_view_inv * y_flip;
        }
        frame_data.proj_view_matrix_inv = proj_view_inv_for_rays;
        frame_data.camera_position      = current_camera_position;
        frame_data.sample_index         = m_sample_index;
        frame_data.extent[0]            = extent.width;
        frame_data.extent[1]            = extent.height;
        frame_data.instance_count       = instance_count;
        frame_data.reset_accumulation   = resetting ? 1u : 0u;
        frame_data.light_count          = light_count;
        frame_data.infinite_light_count = infinite_light_count;

        // Read tunable path tracing knobs from config. max_bounces must be at
        // least 1 (one bounce = primary ray); clamp silently.
        uint32_t max_bounces = 8u;
        uint32_t max_path_intensity = 100u;
        uint32_t samples_per_frame = 1u;
        // Tier-1 performance budget (plan 2026-07-12 §0). Defaults match the
        // conservative RTX 3060 floor; projects targeting 4070 should raise
        // these via PathTracingFpsBudget / PathTracingVramBudgetMb /
        // PathTracingConvergenceBudgetS in their *.ini.
        uint32_t fps_budget = 30u;
        uint32_t vram_budget_mb = 1500u;
        float    convergence_budget_s = 1.5f;
        if (auto cfg = g_runtime_global_context.m_config_manager)
        {
            max_bounces = cfg->getPathTracingMaxBounces();
            max_path_intensity = cfg->getPathTracingMaxPathIntensity();
            samples_per_frame = cfg->getPathTracingMaxSamplesPerFrame();
            fps_budget = cfg->getPathTracingFpsBudget();
            vram_budget_mb = cfg->getPathTracingVramBudgetMb();
            convergence_budget_s = cfg->getPathTracingConvergenceBudgetS();
        }
        if (max_bounces == 0u) max_bounces = 8u;
        if (max_path_intensity == 0u) max_path_intensity = 100u;
        if (samples_per_frame == 0u) samples_per_frame = 1u;
        if (fps_budget == 0u) fps_budget = 30u;
        if (vram_budget_mb == 0u) vram_budget_mb = 1500u;
        if (convergence_budget_s <= 0.0f) convergence_budget_s = 1.5f;

        // Tier-1 quality preset (plan 2026-07-12 §3). When the user picks a
        // preset we override samples_per_frame with the preset's "spp"
        // value, but only if they did not also hand-set MaxSamplesPerFrame
        // to something other than the default 1 (we treat default 1 as
        // "no override"). denoiser_strength is stored on m_denoiser_strength
        // and consumed by §2.2's spatial-bilateral pass when it lands.
        uint32_t quality_preset = 0u;
        if (auto cfg2 = g_runtime_global_context.m_config_manager)
        {
            quality_preset = cfg2->getPathTracingQualityPreset();
        }
        uint32_t preset_spp = 1u;
        switch (quality_preset)
        {
            case 0u: preset_spp = 1u;  m_denoiser_strength = 0.85f; break; // Performance
            case 1u: preset_spp = 2u;  m_denoiser_strength = 0.60f; break; // Balanced
            case 2u: preset_spp = 4u;  m_denoiser_strength = 0.25f; break; // Quality
            case 3u: preset_spp = 1u;  m_denoiser_strength = 1.00f; break; // Interactive (half-rate, very strong)
            default: preset_spp = 1u;  m_denoiser_strength = 0.85f; break;
        }
        if (quality_preset <= 3u)
        {
            samples_per_frame = preset_spp;
        }
        frame_data.max_bounces = max_bounces;
        frame_data.max_path_intensity = max_path_intensity;
        // Real cubemap mip count from the IBL resource (set by RenderResource::
        // createIBLTextures). Plan 2026-07-15 Phase 5 A4. Fall back to 1 if
        // the IBL resource has not been initialised yet (defensive: avoids
        // divide-by-zero in PT_SpecularIBLLod on a brand-new frame).
        if (m_render_resource_impl != nullptr)
        {
            frame_data.cubemap_mip_count =
                m_render_resource_impl->m_global_render_resource._ibl_resource._specular_texture_image_miplevels;
        }
        if (frame_data.cubemap_mip_count == 0u)
        {
            frame_data.cubemap_mip_count = 1u;
        }
        m_samples_per_frame = samples_per_frame;

        // Plan Task 5 diagnostics: print tunable values once so the user can
        // confirm the config was actually read. (The render_sample_index is
        // included by the render pipeline's own log on first PT frame.)
        if (!m_diagnostics_logged)
        {
            m_diagnostics_logged = true;
            LOG_INFO("PathTracing diagnostics: max_bounces={}, light_count={},"
                     " infinite_light_count={}, max_path_intensity={}, samples_per_frame={},"
                     " cubemap_mip_count={}",
                     max_bounces, light_count, infinite_light_count,
                     max_path_intensity, samples_per_frame, frame_data.cubemap_mip_count);
            // Tier-1 performance budget banner (plan 2026-07-12 §0). Reading
            // these back from the same config lets ops confirm a regression
            // is not from a stale ini after a tier promotion.
            LOG_INFO("PathTracing tier-1 budget: fps>={}, vram<={}MB, "
                     "convergence<={:.2f}s to SSIM>=0.95",
                     fps_budget, vram_budget_mb, convergence_budget_s);
            // Tier-1 quality preset (plan 2026-07-12 §3) banner: the active
            // preset name, spp/frame and the spatial denoiser strength it
            // maps to. Editor UI later can flip this live; for now ini is
            // the only entry point.
            static const char* preset_names[4] = {
                "Performance", "Balanced", "Quality", "Interactive"};
            const uint32_t preset_idx = (quality_preset <= 3u) ? quality_preset : 0u;
            LOG_INFO("PathTracing tier-1 preset: {} (spp/frame={}, denoiser_strength={:.2f})",
                     preset_names[preset_idx], samples_per_frame, m_denoiser_strength);
        }

        void* mapped_data = nullptr;
        if (!m_rhi->mapMemory(frame_data_memory, 0, sizeof(FrameData), 0, &mapped_data) ||
            mapped_data == nullptr)
        {
            return false;
        }
        std::memcpy(mapped_data, &frame_data, sizeof(FrameData));
        m_rhi->unmapMemory(frame_data_memory);
        return true;
    }

    bool PathTracingPass::updateDescriptorSet(uint32_t frame_index)
    {
        if (m_rhi == nullptr ||
            frame_index >= m_descriptor_sets.size() ||
            m_descriptor_sets[frame_index] == nullptr ||
            m_top_level_as == nullptr ||
            m_scene_output_image_view == nullptr ||
            m_accumulation_image_view == nullptr ||
            frame_index >= m_frame_data_buffers.size() ||
            m_frame_data_buffers[frame_index] == nullptr ||
            frame_index >= m_light_buffers.size() ||
            m_light_buffers[frame_index] == nullptr ||
            m_render_resource_impl == nullptr ||
            m_render_resource_impl->getPathTracingVertexBuffer() == nullptr ||
            m_render_resource_impl->getPathTracingIndexBuffer() == nullptr ||
            m_render_resource_impl->getPathTracingMaterialBuffer() == nullptr ||
            m_render_resource_impl->getPathTracingGeometryBuffer() == nullptr ||
            m_render_resource_impl->getPathTracingInstanceBuffer() == nullptr)
        {
            return false;
        }

        if (m_specular_texture_view == nullptr || m_linear_sampler == nullptr)
        {
            m_irradiance_texture_view = m_render_resource_impl->m_global_render_resource._ibl_resource._irradiance_texture_image_view;
            m_specular_texture_view   = m_render_resource_impl->m_global_render_resource._ibl_resource._specular_texture_image_view;
            m_linear_sampler          = m_render_resource_impl->m_global_render_resource._ibl_resource._irradiance_texture_sampler;
        }

        RHIImageView* default_material_texture_view =
            m_render_resource_impl->getPathTracingDefaultMaterialTextureView();
        if (default_material_texture_view == nullptr)
        {
            return false;
        }

        if (m_texture_array_views.empty())
        {
            const RHIDescriptorImageInfo fallback_info {
                nullptr, default_material_texture_view, RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            m_texture_array_views.assign(kPathTracingMaterialTextureCount, fallback_info);

            const auto fallback_texture_view = [&](RHIImageView* view) {
                return view != nullptr ? view : default_material_texture_view;
            };
            const auto& material_records = m_render_resource_impl->getPathTracingMaterialTextureViews();
            for (uint32_t material_index = 0; material_index < material_records.size(); ++material_index)
            {
                const RenderPBRMaterialGPUResource* material = material_records[material_index].material;
                const uint32_t texture_base = material_index * 4u;
                if (texture_base + 3u >= kPathTracingMaterialTextureCount)
                {
                    break;
                }

                if (material == nullptr)
                {
                    for (uint32_t slot = 0; slot < 4u; ++slot)
                    {
                        m_texture_array_views[texture_base + slot] = fallback_info;
                    }
                    continue;
                }

                m_texture_array_views[texture_base + 0] = {
                    nullptr, fallback_texture_view(material->base_color_image_view), RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                m_texture_array_views[texture_base + 1] = {
                    nullptr,
                    fallback_texture_view(material->metallic_roughness_image_view),
                    RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                m_texture_array_views[texture_base + 2] = {
                    nullptr, fallback_texture_view(material->normal_image_view), RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                m_texture_array_views[texture_base + 3] = {
                    nullptr, fallback_texture_view(material->emissive_image_view), RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            }
        }

        if (m_specular_texture_view == nullptr || m_linear_sampler == nullptr || m_irradiance_texture_view == nullptr)
        {
            return false;
        }

        RHIBuffer* skinned_vertex_buffer = m_render_resource_impl->getSkinnedVertexBuffer();
        if (skinned_vertex_buffer == nullptr)
        {
            if (!ensureSkinnedVertexFallbackBuffer())
            {
                return false;
            }
            skinned_vertex_buffer = m_skinned_vertex_fallback_buffer;
        }

        RHIDescriptorImageInfo scene_output_info {};
        scene_output_info.imageView   = m_scene_output_image_view;
        scene_output_info.imageLayout = RHI_IMAGE_LAYOUT_GENERAL;

        RHIDescriptorImageInfo accumulation_info {};
        accumulation_info.imageView   = m_accumulation_image_view;
        accumulation_info.imageLayout = RHI_IMAGE_LAYOUT_GENERAL;

        RHIDescriptorBufferInfo frame_data_info {};
        frame_data_info.buffer = m_frame_data_buffers[frame_index];
        frame_data_info.offset = 0;
        frame_data_info.range  = sizeof(FrameData);

        RHIDescriptorBufferInfo vertex_buffer_info {};
        vertex_buffer_info.buffer = m_render_resource_impl->getPathTracingVertexBuffer();
        vertex_buffer_info.offset = 0;
        vertex_buffer_info.range  = RHI_WHOLE_SIZE;

        RHIDescriptorBufferInfo index_buffer_info {};
        index_buffer_info.buffer = m_render_resource_impl->getPathTracingIndexBuffer();
        index_buffer_info.offset = 0;
        index_buffer_info.range  = RHI_WHOLE_SIZE;

        RHIDescriptorBufferInfo material_buffer_info {};
        material_buffer_info.buffer = m_render_resource_impl->getPathTracingMaterialBuffer();
        material_buffer_info.offset = 0;
        material_buffer_info.range  = RHI_WHOLE_SIZE;

        RHIDescriptorBufferInfo geometry_buffer_info {};
        geometry_buffer_info.buffer = m_render_resource_impl->getPathTracingGeometryBuffer();
        geometry_buffer_info.offset = 0;
        geometry_buffer_info.range  = RHI_WHOLE_SIZE;

        RHIDescriptorBufferInfo instance_buffer_info {};
        instance_buffer_info.buffer = m_render_resource_impl->getPathTracingInstanceBuffer();
        instance_buffer_info.offset = 0;
        instance_buffer_info.range  = RHI_WHOLE_SIZE;

        RHIDescriptorBufferInfo skinned_vertex_buffer_info {};
        skinned_vertex_buffer_info.buffer = skinned_vertex_buffer;
        skinned_vertex_buffer_info.offset = 0;
        skinned_vertex_buffer_info.range  = RHI_WHOLE_SIZE;

        RHIDescriptorBufferInfo light_buffer_info {};
        light_buffer_info.buffer = m_light_buffers[frame_index];
        light_buffer_info.offset = 0;
        light_buffer_info.range  = RHI_WHOLE_SIZE;

        RHIAccelerationStructure* top_level_as = m_top_level_as;
        RHIWriteDescriptorSetAccelerationStructure acceleration_structure_info {};
        acceleration_structure_info.accelerationStructureCount = 1;
        acceleration_structure_info.pAccelerationStructures    = &top_level_as;

        RHIDescriptorImageInfo irradiance_info {};
        irradiance_info.imageView   = m_irradiance_texture_view;
        irradiance_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        irradiance_info.sampler     = m_linear_sampler;

        RHIDescriptorImageInfo specular_info {};
        specular_info.imageView   = m_specular_texture_view;
        specular_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        specular_info.sampler     = m_linear_sampler;

        RHIDescriptorSet* descriptor_set = m_descriptor_sets[frame_index];
        RHIWriteDescriptorSet writes[15] {};
        writes[0].sType                      = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet                     = descriptor_set;
        writes[0].dstBinding                 = 0;
        writes[0].descriptorType             = RHI_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        writes[0].descriptorCount            = 1;
        writes[0].pAccelerationStructureInfo = &acceleration_structure_info;

        writes[1].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = descriptor_set;
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo      = &scene_output_info;

        writes[2].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = descriptor_set;
        writes[2].dstBinding      = 2;
        writes[2].descriptorType  = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo     = &frame_data_info;

        writes[3].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet          = descriptor_set;
        writes[3].dstBinding      = 3;
        writes[3].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo      = &accumulation_info;

        writes[4].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet          = descriptor_set;
        writes[4].dstBinding      = 4;
        writes[4].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].pBufferInfo     = &vertex_buffer_info;

        writes[5].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet          = descriptor_set;
        writes[5].dstBinding      = 5;
        writes[5].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].descriptorCount = 1;
        writes[5].pBufferInfo     = &index_buffer_info;

        writes[6].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet          = descriptor_set;
        writes[6].dstBinding      = 6;
        writes[6].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[6].descriptorCount = 1;
        writes[6].pBufferInfo     = &material_buffer_info;

        writes[7].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet          = descriptor_set;
        writes[7].dstBinding      = 7;
        writes[7].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[7].descriptorCount = 1;
        writes[7].pBufferInfo     = &geometry_buffer_info;

        writes[8].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[8].dstSet          = descriptor_set;
        writes[8].dstBinding      = 8;
        writes[8].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[8].descriptorCount = 1;
        writes[8].pBufferInfo     = &instance_buffer_info;

        writes[9].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[9].dstSet          = descriptor_set;
        writes[9].dstBinding      = 9;
        writes[9].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[9].descriptorCount = 1;
        writes[9].pImageInfo      = &irradiance_info;

        writes[10].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[10].dstSet          = descriptor_set;
        writes[10].dstBinding      = 10;
        writes[10].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[10].descriptorCount = 1;
        writes[10].pImageInfo      = &specular_info;

        writes[11].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[11].dstSet          = descriptor_set;
        writes[11].dstBinding      = 11;
        writes[11].descriptorType  = RHI_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[11].descriptorCount = kPathTracingMaterialTextureCount;
        writes[11].pImageInfo      = m_texture_array_views.data();

        writes[12].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[12].dstSet          = descriptor_set;
        writes[12].dstBinding      = 12;
        writes[12].descriptorType  = RHI_DESCRIPTOR_TYPE_SAMPLER;
        writes[12].descriptorCount = 1;
        writes[12].pImageInfo      = &specular_info;

        writes[13].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[13].dstSet          = descriptor_set;
        writes[13].dstBinding      = 1035;  // t1035: g_lights
        writes[13].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[13].descriptorCount = 1;
        writes[13].pBufferInfo     = &light_buffer_info;

        writes[14].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[14].dstSet          = descriptor_set;
        writes[14].dstBinding      = 1036;  // t1036: g_skinned_vertices
        writes[14].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[14].descriptorCount = 1;
        writes[14].pBufferInfo     = &skinned_vertex_buffer_info;

        // Write static descriptors once (u1=scene_output, t9=irradiance, t10=specular, s12=sampler,
        // t11=texture_array); dynamic descriptors every frame.
        if (m_static_descriptors_written[frame_index])
        {
            // Compact: write only dynamic bindings (skip static)
            RHIWriteDescriptorSet dynamic_writes[10];
            uint32_t j = 0;
            for (uint32_t i = 0; i < std::size(writes); ++i)
            {
                // Skip static bindings: u1(1), t9(9), t10(10), t11(11), s12(12)
                if (i != 1 && i != 9 && i != 10 && i != 11 && i != 12)
                    dynamic_writes[j++] = writes[i];
            }
            m_rhi->updateDescriptorSets(j, dynamic_writes, 0, nullptr);
        }
        else
        {
            m_rhi->updateDescriptorSets(static_cast<uint32_t>(std::size(writes)), writes, 0, nullptr);
            m_static_descriptors_written[frame_index] = true;
        }
        return true;
    }

    bool PathTracingPass::buildTopLevelAS(RenderScene& scene)
    {
        if (m_rhi == nullptr || m_render_resource_impl == nullptr)
        {
            return false;
        }

        RHICommandBuffer* command_buffer = m_rhi->getCurrentCommandBuffer();
        if (command_buffer == nullptr)
        {
            return false;
        }

        std::vector<RenderPathTracingCollectedInstance> collected_instances =
            m_render_resource_impl->collectPathTracingInstances(scene);

        // ---- Ensure BLAS for static meshes + check for skinned instances ----
        // Merge has_skinned detection into the static BLAS loop to avoid a separate pass.
        bool has_skinned = false;
        std::unordered_set<RenderMeshGPUResource*> processed_meshes;
        for (RenderPathTracingCollectedInstance& instance : collected_instances)
        {
            if (instance.enable_vertex_blending)
            {
                has_skinned = true;
                continue; // skinned meshes handled after compute dispatch
            }
            if (instance.mesh == nullptr)
            {
                continue;
            }

            if (!processed_meshes.insert(instance.mesh).second)
            {
                continue;
            }

            m_render_resource_impl->ensurePathTracingBLAS(m_rhi, command_buffer, *instance.mesh);
            instance.bottom_level_as = instance.mesh->path_tracing_bottom_level_as;
        }

        // ---- Check if TLAS needs rebuilding ----
        const bool tlas_dirty =
            has_skinned ||
            scene.isPathTracingTLASDirty() ||
            m_top_level_as == nullptr ||
            m_tlas_instance_count != collected_instances.size();
        if (!tlas_dirty)
        {
            return true;
        }

        // ---- Build per-instance BLAS for skinned instances + track meshes for orphan cleanup ----
        std::unordered_set<uint32_t> active_skinned_instance_ids;
        std::unordered_set<RenderMeshGPUResource*> skinned_meshes_in_frame;
        for (RenderPathTracingCollectedInstance& instance : collected_instances)
        {
            if (!instance.enable_vertex_blending || instance.mesh == nullptr)
            {
                continue;
            }

            RenderMeshGPUResource* mesh = instance.mesh;
            uint32_t inst_id = instance.instance_id;
            active_skinned_instance_ids.insert(inst_id);
            skinned_meshes_in_frame.insert(mesh);  // track for orphan cleanup below

            // Look up skinned position buffer from GpuSkinningPass output
            auto& outputs = mesh->skinned_mesh_outputs;
            auto out_it = outputs.find(inst_id);
            if (out_it == outputs.end())
            {
                continue; // GpuSkinningPass hasn't produced output for this instance yet
            }
            auto& skinned_output = out_it->second;
            if (skinned_output.skinned_position_buffer == nullptr)
            {
                continue;
            }
            if (!mesh->path_tracing_index_blas_input_ready)
            {
                continue;
            }
            if (mesh->gpu_skinning_mesh_descriptor_set == nullptr)
            {
                continue; // GpuSkinning compute was skipped — positions are not valid
            }

            // Per-instance BLAS: full rebuild each frame; defer destruction of the previous BLAS
            // until it is no longer referenced by in-flight command buffers.
            auto& pt_resources = mesh->path_tracing_skinned_resources[inst_id];
            RHIAccelerationStructure* previous_blas = pt_resources.blas;

            pt_resources.blas = m_render_resource_impl->buildPathTracingBLASFromSkinned(
                m_rhi,
                command_buffer,
                skinned_output.skinned_position_buffer,
                skinned_output.vertex_count,
                kGpuSkinnedPositionStorageStrideBytes,
                mesh->mesh_index_buffer,
                skinned_output.index_count,
                mesh->mesh_index_type);

            if (pt_resources.blas == nullptr)
            {
                pt_resources.blas = previous_blas;
            }
            else if (previous_blas != nullptr && previous_blas != pt_resources.blas)
            {
                m_rhi->retireAccelerationStructure(m_rhi->getCurrentFrameIndex(), previous_blas);
            }

            instance.bottom_level_as = pt_resources.blas;
        }

        // ---- Clean up orphaned per-instance BLAS ----
        // Iterate only the skinned meshes present this frame (tracked above),
        // not the full collected_instances list.
        for (auto* mesh : skinned_meshes_in_frame)
        {
            auto& map = mesh->path_tracing_skinned_resources;
            for (auto it = map.begin(); it != map.end(); )
            {
                if (active_skinned_instance_ids.count(it->first) == 0)
                {
                    if (it->second.blas != nullptr)
                    {
                        m_rhi->retireAccelerationStructure(m_rhi->getCurrentFrameIndex(), it->second.blas);
                    }
                    it = map.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        // ---- Assign shader instance indices and filter ----
        collected_instances.erase(std::remove_if(collected_instances.begin(),
                                                 collected_instances.end(),
                                                 [](const RenderPathTracingCollectedInstance& instance)
                                                 {
                                                     return instance.bottom_level_as == nullptr;
                                                 }),
                                  collected_instances.end());
        if (collected_instances.empty())
        {
            destroyTopLevelAS();
            m_tlas_instance_count = 0;
            return false;
        }

        // ---- Update scene buffers with FILTERED instances ----
        // Full rebuild only when static data changed (new meshes, first frame).
        // Otherwise only instance+geometry buffers are updated (transforms + skinned vertex_offset).
        const bool static_data_changed = scene.isPathTracingTLASDirty();
        if (static_data_changed)
        {
            invalidateStaticDescriptors();
        }
        if (!m_render_resource_impl->updatePathTracingSceneBuffers(m_rhi,
                                                                    collected_instances,
                                                                    static_data_changed))
        {
            return false;
        }

        std::vector<RHIAccelerationStructureInstanceDesc> instances;
        instances.reserve(collected_instances.size());
        for (const RenderPathTracingCollectedInstance& collected_instance : collected_instances)
        {
            RHIAccelerationStructureInstanceDesc instance_desc {};
            instance_desc.bottom_level_as         = collected_instance.bottom_level_as;
            instance_desc.row_major_3x4_transform = collected_instance.row_major_3x4_transform.data();
            instance_desc.instance_id             = collected_instance.instance_id;
            instance_desc.hit_group_index         = 0;
            instance_desc.instance_mask           = 0xFF;
            instance_desc.force_opaque            = true;
            instances.push_back(instance_desc);
        }

        // ---- Build or update TLAS ----
        const uint32_t instance_count = static_cast<uint32_t>(instances.size());

        RHIAccelerationStructureBuildDesc build_desc {};
        build_desc.type              = RHIAccelerationStructureType::TopLevel;
        build_desc.instances         = instances.data();
        build_desc.instance_count    = instance_count;
        build_desc.prefer_fast_trace = true;
        build_desc.allow_update      = true;

        const bool can_update_in_place =
            m_top_level_as != nullptr && !static_data_changed && m_tlas_instance_count == instance_count;
        if (can_update_in_place)
        {
            build_desc.perform_update = true;
            build_desc.source         = m_top_level_as;
            if (!m_rhi->buildAccelerationStructure(command_buffer, &build_desc, m_top_level_as))
            {
                return false;
            }
            return true;
        }

        build_desc.perform_update = false;
        build_desc.source         = nullptr;

        RHIAccelerationStructure* new_top_level_as = nullptr;
        if (!m_rhi->createAccelerationStructure(&build_desc, new_top_level_as) ||
            new_top_level_as == nullptr)
        {
            return false;
        }
        if (!m_rhi->buildAccelerationStructure(command_buffer, &build_desc, new_top_level_as))
        {
            m_rhi->destroyAccelerationStructure(new_top_level_as);
            return false;
        }

        destroyTopLevelAS();
        m_top_level_as        = new_top_level_as;
        m_tlas_instance_count = instance_count;
        ++m_top_level_as_generation;
        char tlas_debug_name[64];
        formatPathTracingTLASDebugName(tlas_debug_name, sizeof(tlas_debug_name), m_top_level_as_generation, false);
        m_rhi->setDebugObjectName(m_top_level_as, tlas_debug_name);
        scene.clearPathTracingTLASDirty();
        if (static_data_changed)
        {
            resetAccumulation();
        }
        return true;
    }

    void PathTracingPass::invalidateStaticDescriptors()
    {
        if (!m_static_descriptors_written.empty())
        {
            m_static_descriptors_written.assign(m_static_descriptors_written.size(), false);
        }
        m_texture_array_views.clear();
        m_specular_texture_view   = nullptr;
        m_irradiance_texture_view = nullptr;
        m_linear_sampler          = nullptr;
    }

    void PathTracingPass::destroyTopLevelAS()
    {
        if (m_top_level_as != nullptr)
        {
            if (m_rhi != nullptr && m_top_level_as_generation > 0)
            {
                char tlas_debug_name[64];
                formatPathTracingTLASDebugName(tlas_debug_name,
                                               sizeof(tlas_debug_name),
                                               m_top_level_as_generation,
                                               true);
                m_rhi->setDebugObjectName(m_top_level_as, tlas_debug_name);
            }
            if (m_rhi != nullptr)
            {
                m_rhi->retireAccelerationStructure(m_rhi->getCurrentFrameIndex(), m_top_level_as);
            }
            else
            {
                m_top_level_as = nullptr;
            }
        }
        m_tlas_instance_count = 0;
    }

    void PathTracingPass::destroyAccumulationImage()
    {
        if (m_rhi == nullptr)
        {
            m_accumulation_image      = nullptr;
            m_accumulation_image_view = nullptr;
            m_accumulation_memory     = nullptr;
            m_accumulation_image_layout = RHI_IMAGE_LAYOUT_UNDEFINED;
            return;
        }
        if (m_accumulation_image_view != nullptr)
        {
            m_rhi->destroyImageView(m_accumulation_image_view);
            m_accumulation_image_view = nullptr;
        }
        if (m_accumulation_image != nullptr)
        {
            m_rhi->destroyImage(m_accumulation_image);
            m_accumulation_image = nullptr;
        }
        if (m_accumulation_memory != nullptr)
        {
            m_rhi->freeMemory(m_accumulation_memory);
            m_accumulation_memory = nullptr;
        }
        m_extent = {0, 0};
        m_accumulation_image_layout = RHI_IMAGE_LAYOUT_UNDEFINED;
    }


    void PathTracingPass::transitionImage(RHIImage*              image,
                                          RHIImageLayout        old_layout,
                                          RHIImageLayout        new_layout,
                                          RHIAccessFlags        src_access,
                                          RHIAccessFlags        dst_access,
                                          RHIPipelineStageFlags src_stage,
                                          RHIPipelineStageFlags dst_stage)
    {
        if (m_rhi == nullptr || image == nullptr)
        {
            return;
        }

        RHIImageMemoryBarrier barrier {};
        barrier.sType               = RHI_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask       = src_access;
        barrier.dstAccessMask       = dst_access;
        barrier.oldLayout           = old_layout;
        barrier.newLayout           = new_layout;
        barrier.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
        barrier.image               = image;
        barrier.subresourceRange.aspectMask     = RHI_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        m_rhi->cmdPipelineBarrier(m_rhi->getCurrentCommandBuffer(),
                                  src_stage,
                                  dst_stage,
                                  0,
                                  0,
                                  nullptr,
                                  0,
                                  nullptr,
                                  1,
                                  &barrier);
    }

    // Plan 2026-07-12 §2.2: vendor-SDK-less fallback denoiser. Sets up a
    // compute pipeline + per-frame constant buffer + scene-output sized
    // history image. The denoise shader (path_tracing_denoise.comp.hlsl)
    // does 5x5 spatial bilateral weighted by luminance/RGB similarity,
    // then blends with the previous denoised output (m_denoise_history_image).
    //
    // Disabled by default (m_denoise_enabled = false in the header); tests
    // enable it via enableDenoise() so the rest of the tier-1 path stays
    // untouched for projects that already use a vendor denoiser.
    bool PathTracingPass::ensureDenoiseResources()
    {
        if (m_rhi == nullptr)
        {
            return false;
        }

        const RHIExtent2D extent = m_rhi->getSwapchainInfo().extent;
        if (extent.width == 0 || extent.height == 0)
        {
            return false;
        }
        if (m_denoise_history_image != nullptr &&
            m_extent.width == extent.width &&
            m_extent.height == extent.height &&
            m_denoise_pipeline != nullptr)
        {
            return true;
        }

        // Recreate the swapchain-sized images and the pipeline if anything
        // resized or wasn't built before. Idempotent on re-entry.
        destroyDenoiseResources();
        m_extent = extent;

        const std::vector<unsigned char>& bytecode =
            PICCOLO_RENDER_SHADER_BYTECODE(m_rhi, PATH_TRACING_DENOISE_COMP);
        if (bytecode.empty())
        {
            LOG_ERROR("PathTracing denoise: shader bytecode missing (PATH_TRACING_DENOISE_COMP). "
                      "Rebuild shaders or disable the denoiser via disableDenoise().");
            return false;
        }

        // Descriptor set layout: 2 sampled images (current + history) + 1
        // storage image (scene output) + 1 uniform buffer (constants).
        {
            RHIDescriptorSetLayoutBinding bindings[4] {};
            bindings[0].binding         = 0;
            bindings[0].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;
            bindings[1].binding         = 1;
            bindings[1].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;
            bindings[2].binding         = 2;
            bindings[2].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            bindings[2].descriptorCount = 1;
            bindings[2].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;
            bindings[3].binding         = 3;
            bindings[3].descriptorType  = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            bindings[3].descriptorCount = 1;
            bindings[3].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

            RHIDescriptorSetLayoutCreateInfo layout_info {};
            layout_info.sType        = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layout_info.bindingCount = 4;
            layout_info.pBindings    = bindings;
            if (RHI_SUCCESS != m_rhi->createDescriptorSetLayout(&layout_info, m_denoise_descriptor_set_layout))
            {
                return false;
            }
        }

        {
            RHIPipelineLayoutCreateInfo layout_info {};
            layout_info.sType          = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout_info.setLayoutCount = 1;
            layout_info.pSetLayouts    = &m_denoise_descriptor_set_layout;
            if (RHI_SUCCESS != m_rhi->createPipelineLayout(&layout_info, m_denoise_pipeline_layout))
            {
                return false;
            }
        }

        {
            RHIShader* module = m_rhi->createShaderModule(bytecode);
            if (module == RHI_NULL_HANDLE) return false;

            RHIPipelineShaderStageCreateInfo stage {};
            stage.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.stage  = RHI_SHADER_STAGE_COMPUTE_BIT;
            stage.module = module;
            stage.pName  = "main";

            RHIComputePipelineCreateInfo pipeline_info {};
            pipeline_info.sType  = RHI_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            pipeline_info.layout = m_denoise_pipeline_layout;
            pipeline_info.pStages = &stage;
            const bool ok =
                (RHI_SUCCESS == m_rhi->createComputePipelines(RHI_NULL_HANDLE, 1, &pipeline_info, m_denoise_pipeline));
            m_rhi->destroyShaderModule(module);
            if (!ok)
            {
                return false;
            }
        }

        // History image: same size as scene output, RGBA32F. Lazily written
        // by the dispatch (frame_index == 0 uses identity).
        m_rhi->createImage(extent.width,
                           extent.height,
                           RHI_FORMAT_R32G32B32A32_SFLOAT,
                           RHI_IMAGE_TILING_OPTIMAL,
                           RHI_IMAGE_USAGE_STORAGE_BIT | RHI_IMAGE_USAGE_SAMPLED_BIT,
                           RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           m_denoise_history_image,
                           m_denoise_history_memory,
                           0,
                           1,
                           1);
        m_rhi->createImageView(m_denoise_history_image,
                               RHI_FORMAT_R32G32B32A32_SFLOAT,
                               RHI_IMAGE_ASPECT_COLOR_BIT,
                               RHI_IMAGE_VIEW_TYPE_2D,
                               1,
                               1,
                               m_denoise_history_image_view);

        // Per-frame constants (strength + frame_index + extent). One buffer
        // per in-flight frame.
        const uint32_t frame_count = std::max(1u, static_cast<uint32_t>(m_rhi->getMaxFramesInFlight()));
        m_denoise_constants_buffers.assign(frame_count, nullptr);
        m_denoise_constants_memories.assign(frame_count, nullptr);
        for (uint32_t i = 0; i < frame_count; ++i)
        {
            m_rhi->createBuffer(sizeof(float) * 4u,
                                RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                m_denoise_constants_buffers[i],
                                m_denoise_constants_memories[i]);
        }

        // Single static descriptor set; per-frame varying resource is the
        // uniform buffer (rewritten each dispatch).
        m_denoise_descriptor_sets.assign(1, nullptr);
        RHIDescriptorSetAllocateInfo allocate_info {};
        allocate_info.sType              = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool     = m_rhi->getDescriptorPoor();
        allocate_info.descriptorSetCount = 1;
        allocate_info.pSetLayouts        = &m_denoise_descriptor_set_layout;
        if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&allocate_info, m_denoise_descriptor_sets[0]))
        {
            return false;
        }

        if (!m_denoise_diagnostics_logged)
        {
            m_denoise_diagnostics_logged = true;
            LOG_INFO("PathTracing §2.2 fallback denoiser enabled (5x5 bilateral + temporal).");
        }
        return true;
    }

    void PathTracingPass::destroyDenoiseResources()
    {
        if (m_rhi != nullptr)
        {
            RHIDescriptorPool* pool = m_rhi->getDescriptorPoor();
            for (auto* ds : m_denoise_descriptor_sets)
            {
                if (ds != nullptr) m_rhi->freeDescriptorSets(pool, 1, &ds);
            }
            for (auto* buf : m_denoise_constants_buffers)
            {
                if (buf != nullptr) m_rhi->destroyBuffer(buf);
            }
            for (auto* mem : m_denoise_constants_memories)
            {
                if (mem != nullptr) m_rhi->freeMemory(mem);
            }
            if (m_denoise_history_image_view != nullptr)
            {
                m_rhi->destroyImageView(m_denoise_history_image_view);
            }
            if (m_denoise_history_image != nullptr)
            {
                m_rhi->destroyImage(m_denoise_history_image);
            }
            if (m_denoise_pipeline != nullptr)
            {
                m_rhi->destroyPipeline(m_denoise_pipeline);
            }
            if (m_denoise_pipeline_layout != nullptr)
            {
                m_rhi->destroyPipelineLayout(m_denoise_pipeline_layout);
            }
            if (m_denoise_descriptor_set_layout != nullptr)
            {
                m_rhi->destroyDescriptorSetLayout(m_denoise_descriptor_set_layout);
            }
        }
        m_denoise_descriptor_sets.clear();
        m_denoise_constants_buffers.clear();
        m_denoise_constants_memories.clear();
        m_denoise_history_image       = nullptr;
        m_denoise_history_memory      = nullptr;
        m_denoise_history_image_view  = nullptr;
        m_denoise_pipeline            = nullptr;
        m_denoise_pipeline_layout     = nullptr;
        m_denoise_descriptor_set_layout = nullptr;
    }


    void PathTracingPass::dispatchDenoise(uint32_t frame_index)
    {
        if (m_rhi == nullptr || m_denoise_pipeline == nullptr ||
            m_accumulation_image == nullptr || m_accumulation_image_view == nullptr ||
            m_scene_output_image == nullptr || m_scene_output_image_view == nullptr ||
            m_denoise_history_image == nullptr || m_denoise_history_image_view == nullptr)
        {
            return;
        }
        if (frame_index >= m_denoise_constants_buffers.size() ||
            m_denoise_constants_buffers[frame_index] == nullptr)
        {
            return;
        }

        RHICommandBuffer* command_buffer = m_rhi->getCurrentCommandBuffer();
        if (command_buffer == nullptr) return;

        // Bind current (m_accumulation_image, m_accumulation_image_view) and
        // history (m_denoise_history_image, m_denoise_history_image_view)
        // as combined-image-samplers, scene output as storage image, and
        // constants as uniform buffer.
        RHISampler* linear = m_rhi->getOrCreateDefaultSampler(Default_Sampler_Linear);

        RHIDescriptorImageInfo current_info  = {};
        current_info.sampler                  = linear;
        current_info.imageView                = m_accumulation_image_view;
        current_info.imageLayout              = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        RHIDescriptorImageInfo history_info  = {};
        history_info.sampler                  = linear;
        history_info.imageView                = m_denoise_history_image_view;
        history_info.imageLayout              = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        RHIDescriptorImageInfo output_info   = {};
        output_info.sampler                   = nullptr;
        output_info.imageView                 = m_scene_output_image_view;
        output_info.imageLayout               = RHI_IMAGE_LAYOUT_GENERAL;

        RHIWriteDescriptorSet writes[4] {};
        writes[0].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = m_denoise_descriptor_sets[0];
        writes[0].dstBinding      = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo      = &current_info;

        writes[1].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = m_denoise_descriptor_sets[0];
        writes[1].dstBinding      = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo      = &history_info;

        writes[2].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = m_denoise_descriptor_sets[0];
        writes[2].dstBinding      = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo      = &output_info;

        RHIDescriptorBufferInfo buffer_info = {};
        buffer_info.offset = 0;
        buffer_info.range  = sizeof(float) * 4u;
        buffer_info.buffer = m_denoise_constants_buffers[frame_index];

        writes[3].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet          = m_denoise_descriptor_sets[0];
        writes[3].dstBinding      = 3;
        writes[3].dstArrayElement = 0;
        writes[3].descriptorType  = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo     = &buffer_info;

        m_rhi->updateDescriptorSets(4, writes, 0, nullptr);

        // Upload constants (strength, frame_index, extent.x, extent.y).
        struct DenoiseConstants
        {
            float strength;
            uint32_t frame_index;
            uint32_t extent_w;
            uint32_t extent_h;
        } constants;
        constants.strength   = m_denoiser_strength;
        constants.frame_index = m_sample_index;
        constants.extent_w   = m_extent.width;
        constants.extent_h   = m_extent.height;
        void* mapped = nullptr;
        m_rhi->mapMemory(m_denoise_constants_memories[frame_index], 0, sizeof(constants), 0, &mapped);
        std::memcpy(mapped, &constants, sizeof(constants));
        m_rhi->unmapMemory(m_denoise_constants_memories[frame_index]);

        m_rhi->cmdBindPipelinePFN(command_buffer, RHI_PIPELINE_BIND_POINT_COMPUTE, m_denoise_pipeline);
        m_rhi->cmdBindDescriptorSetsPFN(command_buffer,
                                         RHI_PIPELINE_BIND_POINT_COMPUTE,
                                         m_denoise_pipeline_layout,
                                         0,
                                         1,
                                         &m_denoise_descriptor_sets[0],
                                         0,
                                         nullptr);
        const uint32_t group_x = (m_extent.width  + 7u) / 8u;
        const uint32_t group_y = (m_extent.height + 7u) / 8u;
        m_rhi->cmdDispatch(command_buffer, group_x, group_y, 1);

        // Image copy scene_output -> history_image so the next frame's
        // temporal blend sees the right prior frame. Single history buffer
        // is enough for the §2.2 fallback (no ping-pong needed).
        m_rhi->cmdCopyImageToImage(command_buffer,
                                  m_scene_output_image,
                                  RHI_IMAGE_ASPECT_COLOR_BIT,
                                  m_denoise_history_image,
                                  RHI_IMAGE_ASPECT_COLOR_BIT,
                                  m_extent.width,
                                  m_extent.height);
    }
} // namespace Piccolo