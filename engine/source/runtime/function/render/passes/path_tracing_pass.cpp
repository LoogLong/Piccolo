#include "runtime/function/render/passes/path_tracing_pass.h"

#include "runtime/core/base/macro.h"
#include "runtime/function/render/render_resource.h"
#include "runtime/function/render/render_scene.h"
#include "runtime/function/render/render_shader_bytecode.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Piccolo
{
    namespace
    {
        static constexpr uint32_t kPathTracingMaxMaterialTextureCount = 1024;

        bool matrixEquals(const Matrix4x4& lhs, const Matrix4x4& rhs)
        {
            return std::memcmp(lhs.m_mat, rhs.m_mat, sizeof(lhs.m_mat)) == 0;
        }

        bool vectorEquals(const Vector3& lhs, const Vector3& rhs)
        {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        }
    } // namespace

    void PathTracingPass::initialize(const RenderPassInitInfo* init_info)
    {
        const auto* path_tracing_init_info = static_cast<const PathTracingPassInitInfo*>(init_info);
        if (path_tracing_init_info != nullptr)
        {
            m_scene_output_image      = path_tracing_init_info->scene_output_image;
            m_scene_output_image_view = path_tracing_init_info->scene_output_image_view;
        }

        m_render_resource_impl = std::static_pointer_cast<RenderResource>(m_render_resource);
        if (m_rhi == nullptr ||
            m_rhi->getBackendType() != RHIBackendType::D3D12 ||
            m_rhi->getRayTracingCapabilities().support_level != RHIRayTracingSupportLevel::Supported)
        {
            return;
        }

        try
        {
            setupDescriptorSetLayout();
            setupPipelineLayout();
            setupDescriptorSet();
            if (!ensureFrameDataBuffer())
            {
                return;
            }
            if (!setupRayTracingPipeline() || !setupShaderBindingTable())
            {
                return;
            }
            updateDescriptorSet();
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
        if (m_rhi == nullptr ||
            m_render_resource_impl == nullptr ||
            m_scene_output_image == nullptr ||
            m_scene_output_image_view == nullptr ||
            m_rhi->getBackendType() != RHIBackendType::D3D12 ||
            m_rhi->getRayTracingCapabilities().support_level != RHIRayTracingSupportLevel::Supported)
        {
            return false;
        }

        auto render_scene = m_render_resource_impl->getCurrentRenderScene();
        if (render_scene == nullptr)
        {
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
        if (m_descriptor_set == nullptr)
        {
            setupDescriptorSet();
        }
        if (!ensureFrameDataBuffer() || !ensureAccumulationImage())
        {
            return false;
        }
        if (!setupRayTracingPipeline() || !setupShaderBindingTable())
        {
            return false;
        }
        if (!buildTopLevelAS(*render_scene) || m_top_level_as == nullptr || m_tlas_instance_count == 0)
        {
            resetAccumulation();
            return false;
        }
        if (!updateFrameData(m_tlas_instance_count) || !updateDescriptorSet())
        {
            return false;
        }

        RHICommandBuffer* command_buffer = m_rhi->getCurrentCommandBuffer();
        if (command_buffer == nullptr)
        {
            return false;
        }

        transitionImage(m_scene_output_image,
                        RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        RHI_IMAGE_LAYOUT_GENERAL,
                        RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_INPUT_ATTACHMENT_READ_BIT,
                        RHI_ACCESS_SHADER_WRITE_BIT,
                        RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
        transitionImage(m_accumulation_image,
                        m_accumulation_image_layout,
                        RHI_IMAGE_LAYOUT_GENERAL,
                        0,
                        RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_SHADER_WRITE_BIT,
                        RHI_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
        m_accumulation_image_layout = RHI_IMAGE_LAYOUT_GENERAL;

        m_rhi->cmdBindPipelinePFN(command_buffer,
                                  RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                  m_ray_tracing_pipeline);
        m_rhi->cmdBindDescriptorSetsPFN(command_buffer,
                                        RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                        m_pipeline_layout,
                                        0,
                                        1,
                                        &m_descriptor_set,
                                        0,
                                        nullptr);

        RHIRayTracingDispatchDesc dispatch_desc {};
        dispatch_desc.ray_tracing_pipeline = m_ray_tracing_pipeline;
        dispatch_desc.layout               = m_pipeline_layout;
        dispatch_desc.shader_binding_table = m_shader_binding_table;
        dispatch_desc.width                = m_rhi->getSwapchainInfo().extent.width;
        dispatch_desc.height               = m_rhi->getSwapchainInfo().extent.height;
        dispatch_desc.depth                = 1;
        m_rhi->cmdTraceRays(command_buffer, &dispatch_desc);

        transitionImage(m_scene_output_image,
                        RHI_IMAGE_LAYOUT_GENERAL,
                        RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        RHI_ACCESS_SHADER_WRITE_BIT,
                        RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_INPUT_ATTACHMENT_READ_BIT,
                        RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                        RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        transitionImage(m_accumulation_image,
                        RHI_IMAGE_LAYOUT_GENERAL,
                        RHI_IMAGE_LAYOUT_GENERAL,
                        RHI_ACCESS_SHADER_WRITE_BIT,
                        RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_SHADER_WRITE_BIT,
                        RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                        RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

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
        destroyAccumulationImage();
        resetAccumulation();
        m_descriptor_set_dirty = true;
    }

    void PathTracingPass::resetAccumulation()
    {
        m_sample_index = 0;
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

        RHIDescriptorSetLayoutBinding bindings[13] {};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = RHI_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = RHI_SHADER_STAGE_RAYGEN_BIT_KHR;

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

        bindings[6].binding         = 6;
        bindings[6].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[6].descriptorCount = 1;
        bindings[6].stageFlags      = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[7].binding         = 7;
        bindings[7].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[7].descriptorCount = 1;
        bindings[7].stageFlags      = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[8].binding         = 8;
        bindings[8].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[8].descriptorCount = 1;
        bindings[8].stageFlags      = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[9].binding         = 9;
        bindings[9].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[9].descriptorCount = kPathTracingMaxMaterialTextureCount;
        bindings[9].stageFlags      = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[10].binding         = 1033;
        bindings[10].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[10].descriptorCount = kPathTracingMaxMaterialTextureCount;
        bindings[10].stageFlags      = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[11].binding         = 2057;
        bindings[11].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[11].descriptorCount = kPathTracingMaxMaterialTextureCount;
        bindings[11].stageFlags      = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        bindings[12].binding         = 3081;
        bindings[12].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[12].descriptorCount = kPathTracingMaxMaterialTextureCount;
        bindings[12].stageFlags      = RHI_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

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

    void PathTracingPass::setupDescriptorSet()
    {
        if (m_descriptor_set != nullptr || m_rhi == nullptr || m_descriptor_set_layout == nullptr)
        {
            return;
        }

        RHIDescriptorSetAllocateInfo allocate_info {};
        allocate_info.sType              = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool     = m_rhi->getDescriptorPoor();
        allocate_info.descriptorSetCount = 1;
        allocate_info.pSetLayouts        = &m_descriptor_set_layout;
        if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&allocate_info, m_descriptor_set))
        {
            throw std::runtime_error("allocate path tracing descriptor set");
        }
        m_descriptor_set_dirty = true;
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

        const std::vector<unsigned char>& bytecode = PICCOLO_D3D12_PATH_TRACING_LIB;
        if (bytecode.empty())
        {
            LOG_WARN("D3D12 path tracing shader library is missing; falling back to raster");
            return false;
        }

        RHIRayTracingPipelineCreateInfo create_info {};
        create_info.layout                                    = m_pipeline_layout;
        create_info.max_recursion_depth                       = 1;
        create_info.shader_library.bytecode                   = bytecode.data();
        create_info.shader_library.bytecode_size              = bytecode.size();
        create_info.shader_library.raygen_export              = kPathTracingRayGenExport;
        create_info.shader_library.miss_export                = kPathTracingMissExport;
        create_info.shader_library.closest_hit_export         = kPathTracingClosestHitExport;
        create_info.shader_library.hit_group_export           = kPathTracingHitGroupExport;
        return m_rhi->createRayTracingPipeline(&create_info, m_ray_tracing_pipeline) &&
               m_ray_tracing_pipeline != nullptr;
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
        create_info.hit_group_export     = kPathTracingHitGroupExport;
        return m_rhi->createShaderBindingTable(&create_info, m_shader_binding_table) &&
               m_shader_binding_table != nullptr;
    }

    bool PathTracingPass::ensureFrameDataBuffer()
    {
        if (m_frame_data_buffer != nullptr)
        {
            return true;
        }
        if (m_rhi == nullptr)
        {
            return false;
        }

        m_rhi->createBuffer(sizeof(FrameData),
                            RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            m_frame_data_buffer,
                            m_frame_data_memory);
        m_descriptor_set_dirty = true;
        return m_frame_data_buffer != nullptr && m_frame_data_memory != nullptr;
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
        resetAccumulation();
        m_descriptor_set_dirty = true;
        return m_accumulation_image != nullptr && m_accumulation_image_view != nullptr;
    }

    bool PathTracingPass::updateFrameData(uint32_t instance_count)
    {
        if (m_rhi == nullptr || m_render_resource_impl == nullptr || m_frame_data_memory == nullptr)
        {
            return false;
        }

        const Matrix4x4& current_proj_view_inv =
            m_render_resource_impl->m_mesh_perframe_storage_buffer_object.proj_view_matrix_inv;
        const Vector3& current_camera_position =
            m_render_resource_impl->m_mesh_perframe_storage_buffer_object.camera_position;
        const RHIExtent2D extent = m_rhi->getSwapchainInfo().extent;

        const bool camera_changed =
            !m_has_last_camera_state ||
            !matrixEquals(m_last_proj_view_matrix_inv, current_proj_view_inv) ||
            !vectorEquals(m_last_camera_position, current_camera_position) ||
            m_extent.width != extent.width ||
            m_extent.height != extent.height;
        if (camera_changed)
        {
            m_sample_index = 0;
            m_last_proj_view_matrix_inv = current_proj_view_inv;
            m_last_camera_position      = current_camera_position;
            m_has_last_camera_state     = true;
        }

        if (auto render_scene = m_render_resource_impl->getCurrentRenderScene())
        {
            if (render_scene->isPathTracingAccumulationDirty())
            {
                m_sample_index = 0;
            }
        }

        FrameData frame_data {};
        const MeshPerframeStorageBufferObject& raster_frame =
            m_render_resource_impl->m_mesh_perframe_storage_buffer_object;
        frame_data.proj_view_matrix_inv = current_proj_view_inv;
        frame_data.camera_position      = current_camera_position;
        frame_data.sample_index         = m_sample_index;
        frame_data.extent[0]            = extent.width;
        frame_data.extent[1]            = extent.height;
        frame_data.instance_count       = instance_count;
        frame_data.ambient_light        = Vector4(raster_frame.ambient_light, 0.0f);
        frame_data.scene_directional_light = raster_frame.scene_directional_light;
        frame_data.directional_light_proj_view = raster_frame.directional_light_proj_view;
        frame_data.point_light_count = std::min(raster_frame.point_light_num, s_max_point_light_count);
        for (uint32_t light_index = 0; light_index < frame_data.point_light_count; ++light_index)
        {
            frame_data.scene_point_lights[light_index] = raster_frame.scene_point_lights[light_index];
        }

        void* mapped_data = nullptr;
        if (!m_rhi->mapMemory(m_frame_data_memory, 0, sizeof(FrameData), 0, &mapped_data) ||
            mapped_data == nullptr)
        {
            return false;
        }
        std::memcpy(mapped_data, &frame_data, sizeof(FrameData));
        m_rhi->unmapMemory(m_frame_data_memory);
        return true;
    }

    bool PathTracingPass::updateDescriptorSet()
    {
        if (m_rhi == nullptr ||
            m_descriptor_set == nullptr ||
            m_top_level_as == nullptr ||
            m_scene_output_image_view == nullptr ||
            m_accumulation_image_view == nullptr ||
            m_frame_data_buffer == nullptr ||
            m_render_resource_impl == nullptr ||
            m_render_resource_impl->getPathTracingVertexBuffer() == nullptr ||
            m_render_resource_impl->getPathTracingIndexBuffer() == nullptr ||
            m_render_resource_impl->getPathTracingMaterialBuffer() == nullptr ||
            m_render_resource_impl->getPathTracingGeometryBuffer() == nullptr ||
            m_render_resource_impl->getPathTracingInstanceBuffer() == nullptr)
        {
            return false;
        }

        RHIDescriptorImageInfo scene_output_info {};
        scene_output_info.imageView   = m_scene_output_image_view;
        scene_output_info.imageLayout = RHI_IMAGE_LAYOUT_GENERAL;

        RHIDescriptorImageInfo accumulation_info {};
        accumulation_info.imageView   = m_accumulation_image_view;
        accumulation_info.imageLayout = RHI_IMAGE_LAYOUT_GENERAL;

        RHIDescriptorBufferInfo frame_data_info {};
        frame_data_info.buffer = m_frame_data_buffer;
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

        const auto& material_texture_views = m_render_resource_impl->getPathTracingMaterialTextureViews();
        RHIImageView* fallback_base_color = nullptr;
        RHIImageView* fallback_metallic_roughness = nullptr;
        RHIImageView* fallback_normal = nullptr;
        RHIImageView* fallback_emissive = nullptr;
        for (const auto& views : material_texture_views)
        {
            if (fallback_base_color == nullptr && views.base_color_image_view != nullptr)
            {
                fallback_base_color = views.base_color_image_view;
            }
            if (fallback_metallic_roughness == nullptr && views.metallic_roughness_image_view != nullptr)
            {
                fallback_metallic_roughness = views.metallic_roughness_image_view;
            }
            if (fallback_normal == nullptr && views.normal_image_view != nullptr)
            {
                fallback_normal = views.normal_image_view;
            }
            if (fallback_emissive == nullptr && views.emissive_image_view != nullptr)
            {
                fallback_emissive = views.emissive_image_view;
            }
        }
        if (fallback_base_color == nullptr ||
            fallback_metallic_roughness == nullptr ||
            fallback_normal == nullptr ||
            fallback_emissive == nullptr)
        {
            return false;
        }

        RHISampler* default_sampler = m_rhi->getOrCreateDefaultSampler(Default_Sampler_Linear);
        if (default_sampler == nullptr)
        {
            return false;
        }

        std::vector<RHIDescriptorImageInfo> base_color_infos(kPathTracingMaxMaterialTextureCount);
        std::vector<RHIDescriptorImageInfo> metallic_roughness_infos(kPathTracingMaxMaterialTextureCount);
        std::vector<RHIDescriptorImageInfo> normal_infos(kPathTracingMaxMaterialTextureCount);
        std::vector<RHIDescriptorImageInfo> emissive_infos(kPathTracingMaxMaterialTextureCount);

        for (uint32_t texture_index = 0; texture_index < kPathTracingMaxMaterialTextureCount; ++texture_index)
        {
            base_color_infos[texture_index].imageView = fallback_base_color;
            base_color_infos[texture_index].sampler = default_sampler;
            base_color_infos[texture_index].imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            metallic_roughness_infos[texture_index].imageView = fallback_metallic_roughness;
            metallic_roughness_infos[texture_index].sampler = default_sampler;
            metallic_roughness_infos[texture_index].imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            normal_infos[texture_index].imageView = fallback_normal;
            normal_infos[texture_index].sampler = default_sampler;
            normal_infos[texture_index].imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            emissive_infos[texture_index].imageView = fallback_emissive;
            emissive_infos[texture_index].sampler = default_sampler;
            emissive_infos[texture_index].imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        const uint32_t material_count =
            std::min(static_cast<uint32_t>(material_texture_views.size()), kPathTracingMaxMaterialTextureCount);
        for (uint32_t material_index = 0; material_index < material_count; ++material_index)
        {
            const RenderPathTracingMaterialTextureViews& views = material_texture_views[material_index];
            if (views.base_color_image_view != nullptr)
            {
                base_color_infos[material_index].imageView = views.base_color_image_view;
            }
            if (views.metallic_roughness_image_view != nullptr)
            {
                metallic_roughness_infos[material_index].imageView = views.metallic_roughness_image_view;
            }
            if (views.normal_image_view != nullptr)
            {
                normal_infos[material_index].imageView = views.normal_image_view;
            }
            if (views.emissive_image_view != nullptr)
            {
                emissive_infos[material_index].imageView = views.emissive_image_view;
            }
        }

        RHIAccelerationStructure* top_level_as = m_top_level_as;
        RHIWriteDescriptorSetAccelerationStructure acceleration_structure_info {};
        acceleration_structure_info.accelerationStructureCount = 1;
        acceleration_structure_info.pAccelerationStructures    = &top_level_as;

        RHIWriteDescriptorSet writes[13] {};
        writes[0].sType                      = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet                     = m_descriptor_set;
        writes[0].dstBinding                 = 0;
        writes[0].descriptorType             = RHI_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        writes[0].descriptorCount            = 1;
        writes[0].pAccelerationStructureInfo = &acceleration_structure_info;

        writes[1].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = m_descriptor_set;
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo      = &scene_output_info;

        writes[2].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = m_descriptor_set;
        writes[2].dstBinding      = 2;
        writes[2].descriptorType  = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo     = &frame_data_info;

        writes[3].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet          = m_descriptor_set;
        writes[3].dstBinding      = 3;
        writes[3].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo      = &accumulation_info;

        writes[4].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet          = m_descriptor_set;
        writes[4].dstBinding      = 4;
        writes[4].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].pBufferInfo     = &vertex_buffer_info;

        writes[5].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet          = m_descriptor_set;
        writes[5].dstBinding      = 5;
        writes[5].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].descriptorCount = 1;
        writes[5].pBufferInfo     = &index_buffer_info;

        writes[6].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet          = m_descriptor_set;
        writes[6].dstBinding      = 6;
        writes[6].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[6].descriptorCount = 1;
        writes[6].pBufferInfo     = &material_buffer_info;

        writes[7].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet          = m_descriptor_set;
        writes[7].dstBinding      = 7;
        writes[7].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[7].descriptorCount = 1;
        writes[7].pBufferInfo     = &geometry_buffer_info;

        writes[8].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[8].dstSet          = m_descriptor_set;
        writes[8].dstBinding      = 8;
        writes[8].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[8].descriptorCount = 1;
        writes[8].pBufferInfo     = &instance_buffer_info;

        writes[9].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[9].dstSet          = m_descriptor_set;
        writes[9].dstBinding      = 9;
        writes[9].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[9].descriptorCount = kPathTracingMaxMaterialTextureCount;
        writes[9].pImageInfo      = base_color_infos.data();

        writes[10].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[10].dstSet          = m_descriptor_set;
        writes[10].dstBinding      = 1033;
        writes[10].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[10].descriptorCount = kPathTracingMaxMaterialTextureCount;
        writes[10].pImageInfo      = metallic_roughness_infos.data();

        writes[11].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[11].dstSet          = m_descriptor_set;
        writes[11].dstBinding      = 2057;
        writes[11].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[11].descriptorCount = kPathTracingMaxMaterialTextureCount;
        writes[11].pImageInfo      = normal_infos.data();

        writes[12].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[12].dstSet          = m_descriptor_set;
        writes[12].dstBinding      = 3081;
        writes[12].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[12].descriptorCount = kPathTracingMaxMaterialTextureCount;
        writes[12].pImageInfo      = emissive_infos.data();

        m_rhi->updateDescriptorSets(static_cast<uint32_t>(std::size(writes)), writes, 0, nullptr);
        m_descriptor_set_dirty = false;
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
        for (RenderPathTracingCollectedInstance& instance : collected_instances)
        {
            if (instance.mesh != nullptr)
            {
                m_render_resource_impl->ensurePathTracingBLAS(m_rhi, command_buffer, *instance.mesh);
                instance.bottom_level_as = instance.mesh->path_tracing_bottom_level_as;
            }
        }

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

        // Assign shader instance indices
        for (uint32_t instance_index = 0; instance_index < collected_instances.size(); ++instance_index)
        {
            collected_instances[instance_index].shader_instance_index = instance_index;
        }

        // Update path tracing scene buffers
        if (!m_render_resource_impl->updatePathTracingSceneBuffers(m_rhi, collected_instances))
        {
            return false;
        }
        m_descriptor_set_dirty = true;

        const bool tlas_dirty =
            scene.isPathTracingTLASDirty() ||
            m_top_level_as == nullptr ||
            m_tlas_instance_count != collected_instances.size();
        if (!tlas_dirty)
        {
            return true;
        }

        std::vector<RHIAccelerationStructureInstanceDesc> instances;
        instances.reserve(collected_instances.size());
        for (const RenderPathTracingCollectedInstance& collected_instance : collected_instances)
        {
            RHIAccelerationStructureInstanceDesc instance_desc {};
            instance_desc.bottom_level_as          = collected_instance.bottom_level_as;
            instance_desc.row_major_3x4_transform  = collected_instance.row_major_3x4_transform.data();
            instance_desc.instance_id              = collected_instance.instance_id;
            instance_desc.hit_group_index          = 0;
            instance_desc.instance_mask            = 0xFF;
            instance_desc.force_opaque             = true;
            instances.push_back(instance_desc);
        }

        RHIAccelerationStructureBuildDesc build_desc {};
        build_desc.type              = RHIAccelerationStructureType::TopLevel;
        build_desc.instances         = instances.data();
        build_desc.instance_count    = static_cast<uint32_t>(instances.size());
        build_desc.prefer_fast_trace = true;
        build_desc.allow_update      = false;
        build_desc.perform_update    = false;
        build_desc.source            = nullptr;

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
        m_tlas_instance_count = static_cast<uint32_t>(instances.size());
        scene.clearPathTracingTLASDirty();
        resetAccumulation();
        m_descriptor_set_dirty = true;
        return true;
    }

    void PathTracingPass::destroyTopLevelAS()
    {
        if (m_rhi != nullptr && m_top_level_as != nullptr)
        {
            m_rhi->destroyAccelerationStructure(m_top_level_as);
        }
        m_top_level_as = nullptr;
        m_tlas_instance_count = 0;
        m_descriptor_set_dirty = true;
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
} // namespace Piccolo
