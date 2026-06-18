#include "runtime/function/render/render_resource.h"

#include "runtime/function/render/render_camera.h"
#include "runtime/function/render/render_helper.h"

#include "runtime/function/render/render_mesh.h"
#include "runtime/function/render/render_scene.h"

#include "runtime/function/render/passes/main_camera_pass.h"

#include "runtime/core/base/macro.h"

#include <stdexcept>
#include <cstring>

namespace Piccolo
{
    namespace
    {
        bool supportsD3D12PathTracingMeshInputs(const std::shared_ptr<RHI>& rhi, bool static_geometry)
        {
            return static_geometry &&
                   rhi != nullptr &&
                   rhi->getBackendType() == RHIBackendType::D3D12 &&
                   rhi->getRayTracingCapabilities().support_level == RHIRayTracingSupportLevel::Supported;
        }

        RHIBufferUsageFlags withPathTracingBuildInputUsage(RHIBufferUsageFlags usage,
                                                           const std::shared_ptr<RHI>& rhi,
                                                           bool static_geometry)
        {
            if (supportsD3D12PathTracingMeshInputs(rhi, static_geometry))
            {
                usage |= RHI_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                         RHI_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            }
            return usage;
        }
    } // namespace

    void RenderResource::clear()
    {
    }

    void RenderResource::uploadGlobalRenderResource(std::shared_ptr<RHI> rhi, LevelResourceDesc level_resource_desc)
    {
        // create and map global storage buffer
        createAndMapStorageBuffer(rhi);

        // sky box irradiance
        SkyBoxIrradianceMap skybox_irradiance_map        = level_resource_desc.m_ibl_resource_desc.m_skybox_irradiance_map;
        std::shared_ptr<TextureData> irradiace_pos_x_map = loadTextureHDR(skybox_irradiance_map.m_positive_x_map);
        std::shared_ptr<TextureData> irradiace_neg_x_map = loadTextureHDR(skybox_irradiance_map.m_negative_x_map);
        std::shared_ptr<TextureData> irradiace_pos_y_map = loadTextureHDR(skybox_irradiance_map.m_positive_y_map);
        std::shared_ptr<TextureData> irradiace_neg_y_map = loadTextureHDR(skybox_irradiance_map.m_negative_y_map);
        std::shared_ptr<TextureData> irradiace_pos_z_map = loadTextureHDR(skybox_irradiance_map.m_positive_z_map);
        std::shared_ptr<TextureData> irradiace_neg_z_map = loadTextureHDR(skybox_irradiance_map.m_negative_z_map);

        // sky box specular
        SkyBoxSpecularMap            skybox_specular_map = level_resource_desc.m_ibl_resource_desc.m_skybox_specular_map;
        std::shared_ptr<TextureData> specular_pos_x_map  = loadTextureHDR(skybox_specular_map.m_positive_x_map);
        std::shared_ptr<TextureData> specular_neg_x_map  = loadTextureHDR(skybox_specular_map.m_negative_x_map);
        std::shared_ptr<TextureData> specular_pos_y_map  = loadTextureHDR(skybox_specular_map.m_positive_y_map);
        std::shared_ptr<TextureData> specular_neg_y_map  = loadTextureHDR(skybox_specular_map.m_negative_y_map);
        std::shared_ptr<TextureData> specular_pos_z_map  = loadTextureHDR(skybox_specular_map.m_positive_z_map);
        std::shared_ptr<TextureData> specular_neg_z_map  = loadTextureHDR(skybox_specular_map.m_negative_z_map);

        // brdf
        std::shared_ptr<TextureData> brdf_map = loadTextureHDR(level_resource_desc.m_ibl_resource_desc.m_brdf_map);

        // create IBL samplers
        createIBLSamplers(rhi);

        // create IBL textures, take care of the texture order
        std::array<std::shared_ptr<TextureData>, 6> irradiance_maps = {irradiace_pos_x_map,
                                                                       irradiace_neg_x_map,
                                                                       irradiace_pos_z_map,
                                                                       irradiace_neg_z_map,
                                                                       irradiace_pos_y_map,
                                                                       irradiace_neg_y_map};
        std::array<std::shared_ptr<TextureData>, 6> specular_maps   = {specular_pos_x_map,
                                                                     specular_neg_x_map,
                                                                     specular_pos_z_map,
                                                                     specular_neg_z_map,
                                                                     specular_pos_y_map,
                                                                     specular_neg_y_map};
        createIBLTextures(rhi, irradiance_maps, specular_maps);

        // create brdf lut texture
        rhi->createGlobalImage(
            m_global_render_resource._ibl_resource._brdfLUT_texture_image,
            m_global_render_resource._ibl_resource._brdfLUT_texture_image_view,
            m_global_render_resource._ibl_resource._brdfLUT_texture_image_allocation,
            brdf_map->m_width,
            brdf_map->m_height,
            brdf_map->m_pixels,
            brdf_map->m_format);

        // color grading
        std::shared_ptr<TextureData> color_grading_map =
            loadTexture(level_resource_desc.m_color_grading_resource_desc.m_color_grading_map);

        // create color grading texture
        rhi->createGlobalImage(
            m_global_render_resource._color_grading_resource._color_grading_LUT_texture_image,
            m_global_render_resource._color_grading_resource._color_grading_LUT_texture_image_view,
            m_global_render_resource._color_grading_resource._color_grading_LUT_texture_image_allocation,
            color_grading_map->m_width,
            color_grading_map->m_height,
            color_grading_map->m_pixels,
            color_grading_map->m_format);
    }

    void RenderResource::uploadGameObjectRenderResource(std::shared_ptr<RHI> rhi,
        RenderEntity         render_entity,
        RenderMeshData       mesh_data,
        RenderMaterialData   material_data)
    {
        getOrCreateMesh(rhi, render_entity, mesh_data);
        getOrCreateMaterial(rhi, render_entity, material_data);
    }

    void RenderResource::uploadGameObjectRenderResource(std::shared_ptr<RHI> rhi,
        RenderEntity         render_entity,
        RenderMeshData       mesh_data)
    {
        getOrCreateMesh(rhi, render_entity, mesh_data);
    }

    void RenderResource::uploadGameObjectRenderResource(std::shared_ptr<RHI> rhi,
        RenderEntity         render_entity,
        RenderMaterialData   material_data)
    {
        getOrCreateMaterial(rhi, render_entity, material_data);
    }

    void RenderResource::updatePerFrameBuffer(std::shared_ptr<RHI>          rhi,
        std::shared_ptr<RenderScene>  render_scene,
        std::shared_ptr<RenderCamera> camera)
    {
        m_current_render_scene = render_scene;

        Matrix4x4 view_matrix = camera->getViewMatrix();
        Matrix4x4 proj_matrix = camera->getPersProjMatrix(rhi->getBackendType() == RHIBackendType::Vulkan);
        Vector3   camera_position = camera->position();
        Matrix4x4 proj_view_matrix = proj_matrix * view_matrix;

        // ambient light
        Vector3  ambient_light = render_scene->m_ambient_light.m_irradiance;
        uint32_t point_light_num = static_cast<uint32_t>(render_scene->m_point_light_list.m_lights.size());

        // set ubo data
        m_particle_collision_perframe_storage_buffer_object.view_matrix      = view_matrix;
        m_particle_collision_perframe_storage_buffer_object.proj_view_matrix = proj_view_matrix;
        m_particle_collision_perframe_storage_buffer_object.proj_inv_matrix  = proj_matrix.inverse();

        m_mesh_perframe_storage_buffer_object.proj_view_matrix = proj_view_matrix;
        m_mesh_perframe_storage_buffer_object.proj_view_matrix_inv = proj_view_matrix.inverse();
        m_mesh_perframe_storage_buffer_object.camera_position = camera_position;
        m_mesh_perframe_storage_buffer_object.ambient_light = ambient_light;
        m_mesh_perframe_storage_buffer_object.point_light_num = point_light_num;


        m_mesh_point_light_shadow_perframe_storage_buffer_object.point_light_num = point_light_num;
        // point lights
        for (uint32_t i = 0; i < point_light_num; i++)
        {
            Vector3 point_light_position = render_scene->m_point_light_list.m_lights[i].m_position;
            Vector3 point_light_intensity =
                render_scene->m_point_light_list.m_lights[i].m_flux / (4.0f * Math_PI);

            float radius = render_scene->m_point_light_list.m_lights[i].calculateRadius();

            m_mesh_perframe_storage_buffer_object.scene_point_lights[i].position  = point_light_position;
            m_mesh_perframe_storage_buffer_object.scene_point_lights[i].radius    = radius;
            m_mesh_perframe_storage_buffer_object.scene_point_lights[i].intensity = point_light_intensity;

            m_mesh_point_light_shadow_perframe_storage_buffer_object.point_lights_position_and_radius[i] =
                Vector4(point_light_position, radius);
        }

        // directional light
        m_mesh_perframe_storage_buffer_object.scene_directional_light.direction =
            render_scene->m_directional_light.m_direction.normalisedCopy();
        m_mesh_perframe_storage_buffer_object.scene_directional_light.color = render_scene->m_directional_light.m_color;

        // pick pass view projection matrix
        m_mesh_inefficient_pick_perframe_storage_buffer_object.proj_view_matrix = proj_view_matrix;

        m_particlebillboard_perframe_storage_buffer_object.proj_view_matrix = proj_view_matrix;
        m_particlebillboard_perframe_storage_buffer_object.right_direction  = camera->right();
        m_particlebillboard_perframe_storage_buffer_object.foward_direction = camera->forward();
        m_particlebillboard_perframe_storage_buffer_object.up_direction     = camera->up();
    }

    void RenderResource::ensurePathTracingBLAS(std::shared_ptr<RHI> rhi,
                                               RHICommandBuffer* command_buffer,
                                               RenderMeshGPUResource& mesh)
    {
        if (rhi == nullptr ||
            rhi->getRayTracingCapabilities().support_level == RHIRayTracingSupportLevel::Unsupported)
        {
            return;
        }

        if (command_buffer == nullptr)
        {
            LOG_WARN("Path tracing BLAS build skipped because command buffer is null");
            return;
        }

        if (!mesh.path_tracing_blas_dirty && mesh.path_tracing_bottom_level_as != nullptr)
        {
            return;
        }

        if (mesh.mesh_vertex_position_buffer == nullptr ||
            mesh.mesh_vertex_count == 0 ||
            mesh.mesh_index_buffer == nullptr ||
            mesh.mesh_index_count == 0)
        {
            LOG_WARN("Path tracing BLAS build skipped because mesh buffers are incomplete");
            return;
        }

        if (mesh.path_tracing_bottom_level_as != nullptr)
        {
            rhi->destroyAccelerationStructure(mesh.path_tracing_bottom_level_as);
            mesh.path_tracing_bottom_level_as = nullptr;
        }

        RHIAccelerationStructureGeometryDesc geometry;
        geometry.vertex_position_buffer = mesh.mesh_vertex_position_buffer;
        geometry.vertex_position_offset = 0;
        geometry.vertex_count           = mesh.mesh_vertex_count;
        geometry.vertex_stride          = sizeof(MeshVertex::VertexPosition);
        geometry.index_buffer           = mesh.mesh_index_buffer;
        geometry.index_offset           = 0;
        geometry.index_count            = mesh.mesh_index_count;
        geometry.index_type             = mesh.mesh_index_type;
        geometry.opaque                 = true;

        RHIAccelerationStructureBuildDesc build_desc;
        build_desc.type              = RHIAccelerationStructureType::BottomLevel;
        build_desc.geometries        = &geometry;
        build_desc.geometry_count    = 1;
        build_desc.prefer_fast_trace = true;
        build_desc.allow_update      = false;
        build_desc.perform_update    = false;
        build_desc.source            = nullptr;

        RHIAccelerationStructure* new_bottom_level_as = nullptr;
        if (!rhi->createAccelerationStructure(&build_desc, new_bottom_level_as) ||
            new_bottom_level_as == nullptr)
        {
            LOG_WARN("Path tracing BLAS creation failed");
            return;
        }

        if (!rhi->buildAccelerationStructure(command_buffer, &build_desc, new_bottom_level_as))
        {
            LOG_WARN("Path tracing BLAS build failed");
            rhi->destroyAccelerationStructure(new_bottom_level_as);
            return;
        }

        mesh.path_tracing_bottom_level_as = new_bottom_level_as;
        mesh.path_tracing_blas_dirty      = false;
    }

    std::vector<RenderPathTracingCollectedInstance> RenderResource::collectPathTracingInstances(RenderScene& scene)
    {
        scene.rebuildPathTracingInstances(*this, true);

        std::vector<RenderPathTracingCollectedInstance> collected_instances;
        collected_instances.reserve(scene.m_path_tracing_instances.size());

        for (RenderPathTracingInstance& source_instance : scene.m_path_tracing_instances)
        {
            if (!source_instance.enabled ||
                source_instance.entity == nullptr ||
                source_instance.mesh == nullptr ||
                source_instance.material == nullptr)
            {
                continue;
            }

            RenderPathTracingCollectedInstance collected_instance;
            collected_instance.bottom_level_as = source_instance.mesh->path_tracing_bottom_level_as;
            collected_instance.instance_id     = source_instance.instance_id;
            collected_instance.material_index  = source_instance.material_index;
            collected_instance.mesh            = source_instance.mesh;
            collected_instance.material        = source_instance.material;
            collected_instance.entity          = source_instance.entity;

            const Matrix4x4& transform = source_instance.entity->m_model_matrix;
            for (uint32_t row = 0; row < 3; ++row)
            {
                for (uint32_t column = 0; column < 4; ++column)
                {
                    collected_instance.row_major_3x4_transform[row * 4 + column] = transform[row][column];
                }
            }

            // Skinning data
            collected_instance.enable_vertex_blending = pt_instance.enable_vertex_blending;
            if (pt_instance.enable_vertex_blending && pt_instance.entity != nullptr)
            {
                collected_instance.joint_count    = static_cast<uint32_t>(pt_instance.entity->m_joint_matrices.size());
                collected_instance.joint_matrices = pt_instance.entity->m_joint_matrices.data();
            }

            collected_instances.push_back(collected_instance);
        }

        return collected_instances;
    }

    std::shared_ptr<RenderScene> RenderResource::getCurrentRenderScene() const
    {
        return m_current_render_scene.lock();
    }

    bool RenderResource::updatePathTracingSceneBuffers(std::shared_ptr<RHI> rhi,
                                                       const std::vector<RenderPathTracingCollectedInstance>& collected_instances)
    {
        m_path_tracing_vertex_data.clear();
        m_path_tracing_index_data.clear();
        m_path_tracing_material_data.clear();
        m_path_tracing_geometry_data.clear();
        m_path_tracing_instance_data.clear();
        m_path_tracing_material_texture_views.clear();

        std::map<RenderMeshGPUResource*, uint32_t> geometry_indices;

        for (uint32_t instance_index = 0; instance_index < collected_instances.size(); ++instance_index)
        {
            const RenderPathTracingCollectedInstance& source_instance = collected_instances[instance_index];

            // Build geometry record
            uint32_t geometry_index = 0;
            auto geometry_it = geometry_indices.find(source_instance.mesh);
            if (geometry_it == geometry_indices.end())
            {
                geometry_index = static_cast<uint32_t>(m_path_tracing_geometry_data.size());
                geometry_indices[source_instance.mesh] = geometry_index;

                // Append this mesh's vertices and indices to global arrays
                RenderPathTracingGeometryGPUData geometry_data{};
                geometry_data.vertex_offset = static_cast<uint32_t>(m_path_tracing_vertex_data.size());
                geometry_data.index_offset = static_cast<uint32_t>(m_path_tracing_index_data.size());
                geometry_data.index_count = static_cast<uint32_t>(source_instance.mesh->path_tracing_indices.size());

                // Append vertices
                for (size_t v = 0; v < source_instance.mesh->path_tracing_positions.size(); ++v)
                {
                    RenderPathTracingVertexGPUData vertex{};
                    vertex.position = Vector4(source_instance.mesh->path_tracing_positions[v], 1.0f);
                    vertex.normal = Vector4(source_instance.mesh->path_tracing_normals[v], 0.0f);
                    vertex.tangent = Vector4(source_instance.mesh->path_tracing_tangents[v], 0.0f);
                    vertex.texcoord = Vector4(source_instance.mesh->path_tracing_texcoords[v].x,
                                              source_instance.mesh->path_tracing_texcoords[v].y,
                                              0.0f,
                                              0.0f);
                    m_path_tracing_vertex_data.push_back(vertex);
                }

                // Append indices
                for (uint32_t idx : source_instance.mesh->path_tracing_indices)
                {
                    m_path_tracing_index_data.push_back(idx);
                }

                m_path_tracing_geometry_data.push_back(geometry_data);
            }
            else
            {
                geometry_index = geometry_it->second;
            }

            // Build material record
            RenderPathTracingMaterialGPUData material_data{};
            if (source_instance.entity != nullptr)
            {
                material_data.base_color_factor = source_instance.entity->m_base_color_factor;
                material_data.emissive_factor = Vector4(source_instance.entity->m_emissive_factor, 0.0f);
                material_data.metallic_roughness_normal_occlusion = Vector4(
                    source_instance.entity->m_metallic_factor,
                    source_instance.entity->m_roughness_factor,
                    source_instance.entity->m_normal_scale,
                    source_instance.entity->m_occlusion_strength
                );
                material_data.flags = source_instance.entity->m_double_sided ? kPathTracingMaterialFlagDoubleSided : 0u;
            }
            const uint32_t shader_material_index = static_cast<uint32_t>(m_path_tracing_material_data.size());
            m_path_tracing_material_data.push_back(material_data);

            // Build texture views record
            RenderPathTracingMaterialTextureViews texture_views{};
            if (source_instance.material != nullptr)
            {
                texture_views.base_color_image_view = source_instance.material->base_color_image_view;
                texture_views.metallic_roughness_image_view = source_instance.material->metallic_roughness_image_view;
                texture_views.normal_image_view = source_instance.material->normal_image_view;
                texture_views.emissive_image_view = source_instance.material->emissive_image_view;
            }
            m_path_tracing_material_texture_views.push_back(texture_views);

            // Update material texture indices (each material has 4 textures: base_color=0, metallic_roughness=1, normal=2, emissive=3)
            const uint32_t texture_base = shader_material_index * 4u;
            const bool     fits_in_array = (texture_base + 3u) < 1024u;
            m_path_tracing_material_data[shader_material_index].base_color_texture_index =
                fits_in_array ? texture_base + 0u : UINT32_MAX;
            m_path_tracing_material_data[shader_material_index].metallic_roughness_texture_index =
                fits_in_array ? texture_base + 1u : UINT32_MAX;
            m_path_tracing_material_data[shader_material_index].normal_texture_index =
                fits_in_array ? texture_base + 2u : UINT32_MAX;
            m_path_tracing_material_data[shader_material_index].emissive_texture_index =
                fits_in_array ? texture_base + 3u : UINT32_MAX;

            // Build instance record
            RenderPathTracingInstanceGPUData instance_data{};
            instance_data.geometry_index = geometry_index;
            instance_data.material_index = shader_material_index;
            instance_data.entity_instance_id = source_instance.instance_id;
            instance_data.flags = 0;
            m_path_tracing_instance_data.push_back(instance_data);
        }

        // Upload buffers to GPU
        const RHIBufferUsageFlags usage = RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        const RHIMemoryPropertyFlags memory_properties = RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                         RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        // Helper lambda to allocate or update a buffer
        auto update_buffer = [rhi, usage, memory_properties](
            RHIBuffer*& buffer,
            RHIDeviceMemory*& buffer_memory,
            size_t& capacity,
            const void* data,
            size_t data_size) -> bool
        {
            if (data_size == 0)
            {
                return true;
            }

            if (data_size > capacity)
            {
                // Destroy old buffer and memory
                if (buffer != nullptr)
                {
                    rhi->destroyBuffer(buffer);
                    buffer = nullptr;
                }
                if (buffer_memory != nullptr)
                {
                    rhi->freeMemory(buffer_memory);
                    buffer_memory = nullptr;
                }

                capacity = data_size * 2; // Allocate with 2x padding for future growth

                rhi->createBuffer(static_cast<RHIDeviceSize>(capacity), usage, memory_properties, buffer, buffer_memory);
                if (buffer == nullptr || buffer_memory == nullptr)
                {
                    LOG_ERROR("Failed to create path tracing buffer");
                    return false;
                }
            }

            // Map and copy data
            void* mapped_ptr = nullptr;
            if (!rhi->mapMemory(buffer_memory, 0, data_size, 0, &mapped_ptr))
            {
                LOG_ERROR("Failed to map path tracing buffer memory");
                return false;
            }

            std::memcpy(mapped_ptr, data, data_size);
            rhi->unmapMemory(buffer_memory);

            return true;
        };

        // Update all buffers
        if (!update_buffer(m_path_tracing_vertex_buffer,
                          m_path_tracing_vertex_buffer_memory,
                          m_path_tracing_vertex_buffer_capacity,
                          m_path_tracing_vertex_data.data(),
                          m_path_tracing_vertex_data.size() * sizeof(RenderPathTracingVertexGPUData)))
        {
            return false;
        }

        if (!update_buffer(m_path_tracing_index_buffer,
                          m_path_tracing_index_buffer_memory,
                          m_path_tracing_index_buffer_capacity,
                          m_path_tracing_index_data.data(),
                          m_path_tracing_index_data.size() * sizeof(uint32_t)))
        {
            return false;
        }

        if (!update_buffer(m_path_tracing_material_buffer,
                          m_path_tracing_material_buffer_memory,
                          m_path_tracing_material_buffer_capacity,
                          m_path_tracing_material_data.data(),
                          m_path_tracing_material_data.size() * sizeof(RenderPathTracingMaterialGPUData)))
        {
            return false;
        }

        if (!update_buffer(m_path_tracing_geometry_buffer,
                          m_path_tracing_geometry_buffer_memory,
                          m_path_tracing_geometry_buffer_capacity,
                          m_path_tracing_geometry_data.data(),
                          m_path_tracing_geometry_data.size() * sizeof(RenderPathTracingGeometryGPUData)))
        {
            return false;
        }

        if (!update_buffer(m_path_tracing_instance_buffer,
                          m_path_tracing_instance_buffer_memory,
                          m_path_tracing_instance_buffer_capacity,
                          m_path_tracing_instance_data.data(),
                          m_path_tracing_instance_data.size() * sizeof(RenderPathTracingInstanceGPUData)))
        {
            return false;
        }

        return true;
    }

    void RenderResource::createIBLSamplers(std::shared_ptr<RHI> rhi)
    {
        RHIPhysicalDeviceProperties physical_device_properties{};
        rhi->getPhysicalDeviceProperties(&physical_device_properties);

        RHISamplerCreateInfo samplerInfo{};
        samplerInfo.sType = RHI_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = RHI_FILTER_LINEAR;
        samplerInfo.minFilter = RHI_FILTER_LINEAR;
        samplerInfo.addressModeU = RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = RHI_TRUE;                                                // close:false
        samplerInfo.maxAnisotropy = physical_device_properties.limits.maxSamplerAnisotropy; // close :1.0f
        samplerInfo.borderColor = RHI_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = RHI_FALSE;
        samplerInfo.compareEnable = RHI_FALSE;
        samplerInfo.compareOp = RHI_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = RHI_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.maxLod = 0.0f;

        if (m_global_render_resource._ibl_resource._brdfLUT_texture_sampler != RHI_NULL_HANDLE)
        {
            rhi->destroySampler(m_global_render_resource._ibl_resource._brdfLUT_texture_sampler);
        }

        if (rhi->createSampler(&samplerInfo, m_global_render_resource._ibl_resource._brdfLUT_texture_sampler) != RHI_SUCCESS)
        {
            throw std::runtime_error("create BRDF LUT sampler");
        }

        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 8.0f; // TODO: irradiance_texture_miplevels
        samplerInfo.mipLodBias = 0.0f;

        if (m_global_render_resource._ibl_resource._irradiance_texture_sampler != RHI_NULL_HANDLE)
        {
            rhi->destroySampler(m_global_render_resource._ibl_resource._irradiance_texture_sampler);
        }

        if (rhi->createSampler(&samplerInfo, m_global_render_resource._ibl_resource._irradiance_texture_sampler) != RHI_SUCCESS)
        {
            throw std::runtime_error("create irradiance texture sampler");
        }

        if (m_global_render_resource._ibl_resource._specular_texture_sampler != RHI_NULL_HANDLE)
        {
            rhi->destroySampler(m_global_render_resource._ibl_resource._specular_texture_sampler);
        }

        if (rhi->createSampler(&samplerInfo, m_global_render_resource._ibl_resource._specular_texture_sampler) != RHI_SUCCESS)
        {
            throw std::runtime_error("create specular texture sampler");
        }
    }

    void RenderResource::createIBLTextures(std::shared_ptr<RHI>                        rhi,
        std::array<std::shared_ptr<TextureData>, 6> irradiance_maps,
        std::array<std::shared_ptr<TextureData>, 6> specular_maps)
    {
        // assume all textures have same width, height and format
        uint32_t irradiance_cubemap_miplevels =
            static_cast<uint32_t>(
                std::floor(log2(std::max(irradiance_maps[0]->m_width, irradiance_maps[0]->m_height)))) +
            1;
        rhi->createCubeMap(
            m_global_render_resource._ibl_resource._irradiance_texture_image,
            m_global_render_resource._ibl_resource._irradiance_texture_image_view,
            m_global_render_resource._ibl_resource._irradiance_texture_image_allocation,
            irradiance_maps[0]->m_width,
            irradiance_maps[0]->m_height,
            { irradiance_maps[0]->m_pixels,
             irradiance_maps[1]->m_pixels,
             irradiance_maps[2]->m_pixels,
             irradiance_maps[3]->m_pixels,
             irradiance_maps[4]->m_pixels,
             irradiance_maps[5]->m_pixels },
            irradiance_maps[0]->m_format,
            irradiance_cubemap_miplevels);

        uint32_t specular_cubemap_miplevels =
            static_cast<uint32_t>(
                std::floor(log2(std::max(specular_maps[0]->m_width, specular_maps[0]->m_height)))) +
            1;
        rhi->createCubeMap(
            m_global_render_resource._ibl_resource._specular_texture_image,
            m_global_render_resource._ibl_resource._specular_texture_image_view,
            m_global_render_resource._ibl_resource._specular_texture_image_allocation,
            specular_maps[0]->m_width,
            specular_maps[0]->m_height,
            { specular_maps[0]->m_pixels,
             specular_maps[1]->m_pixels,
             specular_maps[2]->m_pixels,
             specular_maps[3]->m_pixels,
             specular_maps[4]->m_pixels,
             specular_maps[5]->m_pixels },
            specular_maps[0]->m_format,
            specular_cubemap_miplevels);
    }

    RenderMeshGPUResource&
        RenderResource::getOrCreateMesh(std::shared_ptr<RHI> rhi, RenderEntity entity, RenderMeshData mesh_data)
    {
        size_t assetid = entity.m_mesh_asset_id;

        auto it = m_meshes.find(assetid);
        if (it != m_meshes.end())
        {
            return it->second;
        }
        else
        {
            RenderMeshGPUResource temp;
            auto       res = m_meshes.insert(std::make_pair(assetid, std::move(temp)));
            assert(res.second);

            uint32_t index_buffer_size = static_cast<uint32_t>(mesh_data.m_static_mesh_data.m_index_buffer->m_size);
            void* index_buffer_data = mesh_data.m_static_mesh_data.m_index_buffer->m_data;

            uint32_t vertex_buffer_size = static_cast<uint32_t>(mesh_data.m_static_mesh_data.m_vertex_buffer->m_size);
            MeshVertexDataDefinition* vertex_buffer_data =
                reinterpret_cast<MeshVertexDataDefinition*>(mesh_data.m_static_mesh_data.m_vertex_buffer->m_data);

            RenderMeshGPUResource& now_mesh = res.first->second;

            if (mesh_data.m_skeleton_binding_buffer)
            {
                uint32_t joint_binding_buffer_size = (uint32_t)mesh_data.m_skeleton_binding_buffer->m_size;
                MeshVertexBindingDataDefinition* joint_binding_buffer_data =
                    reinterpret_cast<MeshVertexBindingDataDefinition*>(mesh_data.m_skeleton_binding_buffer->m_data);
                updateMeshData(rhi,
                               true,
                               index_buffer_size,
                               index_buffer_data,
                               vertex_buffer_size,
                               vertex_buffer_data,
                               joint_binding_buffer_size,
                               joint_binding_buffer_data,
                               now_mesh);
            }
            else
            {
                updateMeshData(rhi,
                               false,
                               index_buffer_size,
                               index_buffer_data,
                               vertex_buffer_size,
                               vertex_buffer_data,
                               0,
                               NULL,
                               now_mesh);
            }

            return now_mesh;
        }
    }

    RenderPBRMaterialGPUResource& RenderResource::getOrCreateMaterial(std::shared_ptr<RHI> rhi,
        RenderEntity         entity,
        RenderMaterialData   material_data)
    {
        size_t assetid = entity.m_material_asset_id;

        auto it = m_pbr_materials.find(assetid);
        if (it != m_pbr_materials.end())
        {
            return it->second;
        }
        else
        {
            RenderPBRMaterialGPUResource temp;
            auto              res = m_pbr_materials.insert(std::make_pair(assetid, std::move(temp)));
            assert(res.second);

            float empty_image[] = { 0.5f, 0.5f, 0.5f, 0.5f };

            void* base_color_image_pixels = empty_image;
            uint32_t           base_color_image_width = 1;
            uint32_t           base_color_image_height = 1;
            RHIFormat base_color_image_format = RHIFormat::RHI_FORMAT_R8G8B8A8_SRGB;
            if (material_data.m_base_color_texture)
            {
                base_color_image_pixels = material_data.m_base_color_texture->m_pixels;
                base_color_image_width = static_cast<uint32_t>(material_data.m_base_color_texture->m_width);
                base_color_image_height = static_cast<uint32_t>(material_data.m_base_color_texture->m_height);
                base_color_image_format = material_data.m_base_color_texture->m_format;
            }

            void* metallic_roughness_image_pixels = empty_image;
            uint32_t           metallic_roughness_width = 1;
            uint32_t           metallic_roughness_height = 1;
            RHIFormat metallic_roughness_format = RHIFormat::RHI_FORMAT_R8G8B8A8_UNORM;
            if (material_data.m_metallic_roughness_texture)
            {
                metallic_roughness_image_pixels = material_data.m_metallic_roughness_texture->m_pixels;
                metallic_roughness_width = static_cast<uint32_t>(material_data.m_metallic_roughness_texture->m_width);
                metallic_roughness_height = static_cast<uint32_t>(material_data.m_metallic_roughness_texture->m_height);
                metallic_roughness_format = material_data.m_metallic_roughness_texture->m_format;
            }

            void* normal_roughness_image_pixels = empty_image;
            uint32_t           normal_roughness_width = 1;
            uint32_t           normal_roughness_height = 1;
            RHIFormat normal_roughness_format = RHIFormat::RHI_FORMAT_R8G8B8A8_UNORM;
            if (material_data.m_normal_texture)
            {
                normal_roughness_image_pixels = material_data.m_normal_texture->m_pixels;
                normal_roughness_width = static_cast<uint32_t>(material_data.m_normal_texture->m_width);
                normal_roughness_height = static_cast<uint32_t>(material_data.m_normal_texture->m_height);
                normal_roughness_format = material_data.m_normal_texture->m_format;
            }

            void* occlusion_image_pixels = empty_image;
            uint32_t           occlusion_image_width = 1;
            uint32_t           occlusion_image_height = 1;
            RHIFormat occlusion_image_format = RHIFormat::RHI_FORMAT_R8G8B8A8_UNORM;
            if (material_data.m_occlusion_texture)
            {
                occlusion_image_pixels = material_data.m_occlusion_texture->m_pixels;
                occlusion_image_width = static_cast<uint32_t>(material_data.m_occlusion_texture->m_width);
                occlusion_image_height = static_cast<uint32_t>(material_data.m_occlusion_texture->m_height);
                occlusion_image_format = material_data.m_occlusion_texture->m_format;
            }

            void* emissive_image_pixels = empty_image;
            uint32_t           emissive_image_width = 1;
            uint32_t           emissive_image_height = 1;
            RHIFormat emissive_image_format = RHIFormat::RHI_FORMAT_R8G8B8A8_UNORM;
            if (material_data.m_emissive_texture)
            {
                emissive_image_pixels = material_data.m_emissive_texture->m_pixels;
                emissive_image_width  = static_cast<uint32_t>(material_data.m_emissive_texture->m_width);
                emissive_image_height = static_cast<uint32_t>(material_data.m_emissive_texture->m_height);
                emissive_image_format = material_data.m_emissive_texture->m_format;
            }

            RenderPBRMaterialGPUResource& now_material = res.first->second;

            // similiarly to the vertex/index buffer, we should allocate the uniform
            // buffer in DEVICE_LOCAL memory and use the temp stage buffer to copy the
            // data
            {
                // temporary staging buffer

                RHIDeviceSize buffer_size = sizeof(MeshPerMaterialUniformBufferObject);

                RHIBuffer* inefficient_staging_buffer = RHI_NULL_HANDLE;
                RHIDeviceMemory* inefficient_staging_buffer_memory = RHI_NULL_HANDLE;
                rhi->createBuffer(
                    buffer_size,
                    RHI_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    inefficient_staging_buffer,
                    inefficient_staging_buffer_memory);
                // RHI_BUFFER_USAGE_TRANSFER_SRC_BIT: buffer can be used as source in a
                // memory transfer operation

                void* staging_buffer_data = nullptr;
                rhi->mapMemory(
                    inefficient_staging_buffer_memory,
                    0,
                    buffer_size,
                    0,
                    &staging_buffer_data);
                
                MeshPerMaterialUniformBufferObject& material_uniform_buffer_info =
                    (*static_cast<MeshPerMaterialUniformBufferObject*>(staging_buffer_data));
                material_uniform_buffer_info.is_blend = entity.m_blend;
                material_uniform_buffer_info.is_double_sided = entity.m_double_sided;
                material_uniform_buffer_info.baseColorFactor = entity.m_base_color_factor;
                material_uniform_buffer_info.metallicFactor = entity.m_metallic_factor;
                material_uniform_buffer_info.roughnessFactor = entity.m_roughness_factor;
                material_uniform_buffer_info.normalScale = entity.m_normal_scale;
                material_uniform_buffer_info.occlusionStrength = entity.m_occlusion_strength;
                material_uniform_buffer_info.emissiveFactor = entity.m_emissive_factor;

                rhi->unmapMemory(inefficient_staging_buffer_memory);

                // allocate asset uniform buffer in device local memory
                RHIBufferCreateInfo bufferInfo = { RHI_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
                bufferInfo.size = buffer_size;
                bufferInfo.usage = RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT | RHI_BUFFER_USAGE_TRANSFER_DST_BIT;

                rhi->createBufferWithAlignment(
                    &bufferInfo,
                    RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    m_global_render_resource._storage_buffer._min_uniform_buffer_offset_alignment,
                    now_material.material_uniform_buffer,
                    now_material.material_uniform_buffer_allocation);

                // use the data from staging buffer
                rhi->copyBuffer(inefficient_staging_buffer, now_material.material_uniform_buffer, 0, 0, buffer_size);

                // release staging buffer
                rhi->destroyBuffer(inefficient_staging_buffer);
                rhi->freeMemory(inefficient_staging_buffer_memory);
            }

            TextureDataToUpdate update_texture_data;
            update_texture_data.base_color_image_pixels         = base_color_image_pixels;
            update_texture_data.base_color_image_width          = base_color_image_width;
            update_texture_data.base_color_image_height         = base_color_image_height;
            update_texture_data.base_color_image_format         = base_color_image_format;
            update_texture_data.metallic_roughness_image_pixels = metallic_roughness_image_pixels;
            update_texture_data.metallic_roughness_image_width  = metallic_roughness_width;
            update_texture_data.metallic_roughness_image_height = metallic_roughness_height;
            update_texture_data.metallic_roughness_image_format = metallic_roughness_format;
            update_texture_data.normal_roughness_image_pixels   = normal_roughness_image_pixels;
            update_texture_data.normal_roughness_image_width    = normal_roughness_width;
            update_texture_data.normal_roughness_image_height   = normal_roughness_height;
            update_texture_data.normal_roughness_image_format   = normal_roughness_format;
            update_texture_data.occlusion_image_pixels          = occlusion_image_pixels;
            update_texture_data.occlusion_image_width           = occlusion_image_width;
            update_texture_data.occlusion_image_height          = occlusion_image_height;
            update_texture_data.occlusion_image_format          = occlusion_image_format;
            update_texture_data.emissive_image_pixels           = emissive_image_pixels;
            update_texture_data.emissive_image_width            = emissive_image_width;
            update_texture_data.emissive_image_height           = emissive_image_height;
            update_texture_data.emissive_image_format           = emissive_image_format;
            update_texture_data.now_material                    = &now_material;

            updateTextureImageData(rhi, update_texture_data);

            RHIDescriptorSetAllocateInfo material_descriptor_set_alloc_info;
            material_descriptor_set_alloc_info.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            material_descriptor_set_alloc_info.pNext = NULL;
            material_descriptor_set_alloc_info.descriptorPool = rhi->getDescriptorPoor();
            material_descriptor_set_alloc_info.descriptorSetCount = 1;
            material_descriptor_set_alloc_info.pSetLayouts        = m_material_descriptor_set_layout;

            if (RHI_SUCCESS != rhi->allocateDescriptorSets(
                &material_descriptor_set_alloc_info,
                now_material.material_descriptor_set))
            {
                throw std::runtime_error("allocate material descriptor set");
            }

            RHIDescriptorBufferInfo material_uniform_buffer_info = {};
            material_uniform_buffer_info.offset = 0;
            material_uniform_buffer_info.range = sizeof(MeshPerMaterialUniformBufferObject);
            material_uniform_buffer_info.buffer = now_material.material_uniform_buffer;

            RHIDescriptorImageInfo base_color_image_info = {};
            base_color_image_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            base_color_image_info.imageView = now_material.base_color_image_view;
            base_color_image_info.sampler = rhi->getOrCreateMipmapSampler(base_color_image_width,
                                                                          base_color_image_height);

            RHIDescriptorImageInfo metallic_roughness_image_info = {};
            metallic_roughness_image_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            metallic_roughness_image_info.imageView = now_material.metallic_roughness_image_view;
            metallic_roughness_image_info.sampler = rhi->getOrCreateMipmapSampler(metallic_roughness_width,
                                                                                  metallic_roughness_height);

            RHIDescriptorImageInfo normal_roughness_image_info = {};
            normal_roughness_image_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            normal_roughness_image_info.imageView = now_material.normal_image_view;
            normal_roughness_image_info.sampler = rhi->getOrCreateMipmapSampler(normal_roughness_width,
                                                                                normal_roughness_height);

            RHIDescriptorImageInfo occlusion_image_info = {};
            occlusion_image_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            occlusion_image_info.imageView = now_material.occlusion_image_view;
            occlusion_image_info.sampler = rhi->getOrCreateMipmapSampler(occlusion_image_width,occlusion_image_height);

            RHIDescriptorImageInfo emissive_image_info = {};
            emissive_image_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            emissive_image_info.imageView = now_material.emissive_image_view;
            emissive_image_info.sampler = rhi->getOrCreateMipmapSampler(emissive_image_width, emissive_image_height);

            RHIWriteDescriptorSet mesh_descriptor_writes_info[6];

            mesh_descriptor_writes_info[0].sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            mesh_descriptor_writes_info[0].pNext = NULL;
            mesh_descriptor_writes_info[0].dstSet = now_material.material_descriptor_set;
            mesh_descriptor_writes_info[0].dstBinding = 0;
            mesh_descriptor_writes_info[0].dstArrayElement = 0;
            mesh_descriptor_writes_info[0].descriptorType = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            mesh_descriptor_writes_info[0].descriptorCount = 1;
            mesh_descriptor_writes_info[0].pBufferInfo = &material_uniform_buffer_info;

            mesh_descriptor_writes_info[1].sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            mesh_descriptor_writes_info[1].pNext = NULL;
            mesh_descriptor_writes_info[1].dstSet = now_material.material_descriptor_set;
            mesh_descriptor_writes_info[1].dstBinding = 1;
            mesh_descriptor_writes_info[1].dstArrayElement = 0;
            mesh_descriptor_writes_info[1].descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            mesh_descriptor_writes_info[1].descriptorCount = 1;
            mesh_descriptor_writes_info[1].pImageInfo = &base_color_image_info;

            mesh_descriptor_writes_info[2] = mesh_descriptor_writes_info[1];
            mesh_descriptor_writes_info[2].dstBinding = 2;
            mesh_descriptor_writes_info[2].pImageInfo = &metallic_roughness_image_info;

            mesh_descriptor_writes_info[3] = mesh_descriptor_writes_info[1];
            mesh_descriptor_writes_info[3].dstBinding = 3;
            mesh_descriptor_writes_info[3].pImageInfo = &normal_roughness_image_info;

            mesh_descriptor_writes_info[4] = mesh_descriptor_writes_info[1];
            mesh_descriptor_writes_info[4].dstBinding = 4;
            mesh_descriptor_writes_info[4].pImageInfo = &occlusion_image_info;

            mesh_descriptor_writes_info[5] = mesh_descriptor_writes_info[1];
            mesh_descriptor_writes_info[5].dstBinding = 5;
            mesh_descriptor_writes_info[5].pImageInfo = &emissive_image_info;

            rhi->updateDescriptorSets(6, mesh_descriptor_writes_info, 0, nullptr);

            return now_material;
        }
    }

    void RenderResource::updateMeshData(std::shared_ptr<RHI>                   rhi,
                                        bool                                   enable_vertex_blending,
                                        uint32_t                               index_buffer_size,
                                        void*                                  index_buffer_data,
                                        uint32_t                               vertex_buffer_size,
                                        MeshVertexDataDefinition const*        vertex_buffer_data,
                                        uint32_t                               joint_binding_buffer_size,
                                        MeshVertexBindingDataDefinition const* joint_binding_buffer_data,
                                        RenderMeshGPUResource&                            now_mesh)
    {
        now_mesh.enable_vertex_blending = enable_vertex_blending;
        assert(0 == (vertex_buffer_size % sizeof(MeshVertexDataDefinition)));
        now_mesh.mesh_vertex_count = vertex_buffer_size / sizeof(MeshVertexDataDefinition);
        now_mesh.path_tracing_static_opaque_supported = !enable_vertex_blending;
        now_mesh.path_tracing_blas_dirty              = true;
        updateVertexBuffer(rhi,
                           enable_vertex_blending,
                           vertex_buffer_size,
                           vertex_buffer_data,
                           joint_binding_buffer_size,
                           joint_binding_buffer_data,
                           index_buffer_size,
                           reinterpret_cast<uint16_t*>(index_buffer_data),
                           now_mesh);
        assert(0 == (index_buffer_size % sizeof(uint16_t)));
        now_mesh.mesh_index_count = index_buffer_size / sizeof(uint16_t);
        now_mesh.mesh_index_type  = RHI_INDEX_TYPE_UINT16;
        updateIndexBuffer(rhi, index_buffer_size, index_buffer_data, now_mesh);
    }

    void RenderResource::updateVertexBuffer(std::shared_ptr<RHI>                   rhi,
                                            bool                                   enable_vertex_blending,
                                            uint32_t                               vertex_buffer_size,
                                            MeshVertexDataDefinition const*        vertex_buffer_data,
                                            uint32_t                               joint_binding_buffer_size,
                                            MeshVertexBindingDataDefinition const* joint_binding_buffer_data,
                                            uint32_t                               index_buffer_size,
                                            uint16_t*                              index_buffer_data,
                                            RenderMeshGPUResource&                            now_mesh)
    {
        if (enable_vertex_blending)
        {
            assert(0 == (vertex_buffer_size % sizeof(MeshVertexDataDefinition)));
            uint32_t vertex_count = vertex_buffer_size / sizeof(MeshVertexDataDefinition);
            assert(0 == (index_buffer_size % sizeof(uint16_t)));
            uint32_t index_count = index_buffer_size / sizeof(uint16_t);

            RHIDeviceSize vertex_position_buffer_size = sizeof(MeshVertex::VertexPosition) * vertex_count;
            RHIDeviceSize vertex_varying_enable_blending_buffer_size =
                sizeof(MeshVertex::VertexVaryingEnableBlending) * vertex_count;
            RHIDeviceSize vertex_varying_buffer_size = sizeof(MeshVertex::VertexVarying) * vertex_count;
            RHIDeviceSize vertex_joint_binding_buffer_size =
                sizeof(MeshVertex::VertexJointBinding) * index_count;

            RHIDeviceSize vertex_position_buffer_offset = 0;
            RHIDeviceSize vertex_varying_enable_blending_buffer_offset =
                vertex_position_buffer_offset + vertex_position_buffer_size;
            RHIDeviceSize vertex_varying_buffer_offset =
                vertex_varying_enable_blending_buffer_offset + vertex_varying_enable_blending_buffer_size;
            RHIDeviceSize vertex_joint_binding_buffer_offset = vertex_varying_buffer_offset + vertex_varying_buffer_size;

            // temporary staging buffer
            RHIDeviceSize inefficient_staging_buffer_size =
                vertex_position_buffer_size + vertex_varying_enable_blending_buffer_size + vertex_varying_buffer_size +
                vertex_joint_binding_buffer_size;
            RHIBuffer* inefficient_staging_buffer = RHI_NULL_HANDLE;
            RHIDeviceMemory* inefficient_staging_buffer_memory = RHI_NULL_HANDLE;
            rhi->createBuffer(inefficient_staging_buffer_size,
                              RHI_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              inefficient_staging_buffer,
                              inefficient_staging_buffer_memory);

            void* inefficient_staging_buffer_data;
            rhi->mapMemory(inefficient_staging_buffer_memory,
                           0,
                           RHI_WHOLE_SIZE,
                           0,
                           &inefficient_staging_buffer_data);

            MeshVertex::VertexPosition* mesh_vertex_positions =
                reinterpret_cast<MeshVertex::VertexPosition*>(
                    reinterpret_cast<uintptr_t>(inefficient_staging_buffer_data) + vertex_position_buffer_offset);
            MeshVertex::VertexVaryingEnableBlending* mesh_vertex_blending_varyings =
                reinterpret_cast<MeshVertex::VertexVaryingEnableBlending*>(
                    reinterpret_cast<uintptr_t>(inefficient_staging_buffer_data) +
                    vertex_varying_enable_blending_buffer_offset);
            MeshVertex::VertexVarying* mesh_vertex_varyings =
                reinterpret_cast<MeshVertex::VertexVarying*>(
                    reinterpret_cast<uintptr_t>(inefficient_staging_buffer_data) + vertex_varying_buffer_offset);
            MeshVertex::VertexJointBinding* mesh_vertex_joint_binding =
                reinterpret_cast<MeshVertex::VertexJointBinding*>(
                    reinterpret_cast<uintptr_t>(inefficient_staging_buffer_data) + vertex_joint_binding_buffer_offset);

            for (uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index)
            {
                Vector3 normal = Vector3(vertex_buffer_data[vertex_index].nx,
                    vertex_buffer_data[vertex_index].ny,
                    vertex_buffer_data[vertex_index].nz);
                Vector3 tangent = Vector3(vertex_buffer_data[vertex_index].tx,
                    vertex_buffer_data[vertex_index].ty,
                    vertex_buffer_data[vertex_index].tz);

                mesh_vertex_positions[vertex_index].position = Vector3(vertex_buffer_data[vertex_index].x,
                    vertex_buffer_data[vertex_index].y,
                    vertex_buffer_data[vertex_index].z);

                mesh_vertex_blending_varyings[vertex_index].normal = normal;
                mesh_vertex_blending_varyings[vertex_index].tangent = tangent;

                mesh_vertex_varyings[vertex_index].texcoord =
                    Vector2(vertex_buffer_data[vertex_index].u, vertex_buffer_data[vertex_index].v);
            }

            now_mesh.path_tracing_positions.resize(vertex_count);
            now_mesh.path_tracing_normals.resize(vertex_count);
            now_mesh.path_tracing_tangents.resize(vertex_count);
            now_mesh.path_tracing_texcoords.resize(vertex_count);
            for (uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index)
            {
                now_mesh.path_tracing_positions[vertex_index] = mesh_vertex_positions[vertex_index].position;
                now_mesh.path_tracing_normals[vertex_index]   = mesh_vertex_blending_varyings[vertex_index].normal;
                now_mesh.path_tracing_tangents[vertex_index]  = mesh_vertex_blending_varyings[vertex_index].tangent;
                now_mesh.path_tracing_texcoords[vertex_index] = mesh_vertex_varyings[vertex_index].texcoord;
            }

            now_mesh.path_tracing_indices.resize(index_count);
            for (uint32_t index_index = 0; index_index < index_count; ++index_index)
            {
                now_mesh.path_tracing_indices[index_index] = static_cast<uint32_t>(index_buffer_data[index_index]);
            }
            now_mesh.path_tracing_geometry_dirty = true;

            for (uint32_t index_index = 0; index_index < index_count; ++index_index)
            {
                uint32_t vertex_buffer_index = index_buffer_data[index_index];

                // TODO: move to assets loading process

                mesh_vertex_joint_binding[index_index].indices[0] = joint_binding_buffer_data[vertex_buffer_index].m_index0;
                mesh_vertex_joint_binding[index_index].indices[1] = joint_binding_buffer_data[vertex_buffer_index].m_index1;
                mesh_vertex_joint_binding[index_index].indices[2] = joint_binding_buffer_data[vertex_buffer_index].m_index2;
                mesh_vertex_joint_binding[index_index].indices[3] = joint_binding_buffer_data[vertex_buffer_index].m_index3;

                float inv_total_weight = joint_binding_buffer_data[vertex_buffer_index].m_weight0 +
                                         joint_binding_buffer_data[vertex_buffer_index].m_weight1 +
                                         joint_binding_buffer_data[vertex_buffer_index].m_weight2 +
                                         joint_binding_buffer_data[vertex_buffer_index].m_weight3;

                inv_total_weight = (inv_total_weight != 0.0) ? 1 / inv_total_weight : 1.0;

                mesh_vertex_joint_binding[index_index].weights =
                    Vector4(joint_binding_buffer_data[vertex_buffer_index].m_weight0 * inv_total_weight,
                        joint_binding_buffer_data[vertex_buffer_index].m_weight1 * inv_total_weight,
                        joint_binding_buffer_data[vertex_buffer_index].m_weight2 * inv_total_weight,
                        joint_binding_buffer_data[vertex_buffer_index].m_weight3 * inv_total_weight);
            }

            rhi->unmapMemory(inefficient_staging_buffer_memory);

            // allocate asset vertex buffers in device local memory
            RHIBufferCreateInfo bufferInfo = { RHI_STRUCTURE_TYPE_BUFFER_CREATE_INFO };

            const RHIBufferUsageFlags vertex_buffer_usage = withPathTracingBuildInputUsage(
                RHI_BUFFER_USAGE_VERTEX_BUFFER_BIT | RHI_BUFFER_USAGE_TRANSFER_DST_BIT,
                rhi,
                !enable_vertex_blending);
            bufferInfo.usage = vertex_buffer_usage;
            bufferInfo.size = vertex_position_buffer_size;
            rhi->createBufferWithAllocation(&bufferInfo,
                                            RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                            now_mesh.mesh_vertex_position_buffer,
                                            now_mesh.mesh_vertex_position_buffer_allocation);
            bufferInfo.size = vertex_varying_enable_blending_buffer_size;
            rhi->createBufferWithAllocation(&bufferInfo,
                                            RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                            now_mesh.mesh_vertex_varying_enable_blending_buffer,
                                            now_mesh.mesh_vertex_varying_enable_blending_buffer_allocation);
            bufferInfo.size = vertex_varying_buffer_size;
            rhi->createBufferWithAllocation(&bufferInfo,
                                            RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                            now_mesh.mesh_vertex_varying_buffer,
                                            now_mesh.mesh_vertex_varying_buffer_allocation);

            bufferInfo.usage = RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT | RHI_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufferInfo.size = vertex_joint_binding_buffer_size;
            rhi->createBufferWithAllocation(&bufferInfo,
                                            RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                            now_mesh.mesh_vertex_joint_binding_buffer,
                                            now_mesh.mesh_vertex_joint_binding_buffer_allocation);

            // use the data from staging buffer
            rhi->copyBuffer(inefficient_staging_buffer,
                            now_mesh.mesh_vertex_position_buffer,
                            vertex_position_buffer_offset,
                            0,
                            vertex_position_buffer_size);
            rhi->copyBuffer(inefficient_staging_buffer,
                            now_mesh.mesh_vertex_varying_enable_blending_buffer,
                            vertex_varying_enable_blending_buffer_offset,
                            0,
                            vertex_varying_enable_blending_buffer_size);
            rhi->copyBuffer(inefficient_staging_buffer,
                            now_mesh.mesh_vertex_varying_buffer,
                            vertex_varying_buffer_offset,
                            0,
                            vertex_varying_buffer_size);
            rhi->copyBuffer(inefficient_staging_buffer,
                            now_mesh.mesh_vertex_joint_binding_buffer,
                            vertex_joint_binding_buffer_offset,
                            0,
                            vertex_joint_binding_buffer_size);

            // release staging buffer
            rhi->destroyBuffer(inefficient_staging_buffer);
            rhi->freeMemory(inefficient_staging_buffer_memory);

            // update descriptor set
            RHIDescriptorSetAllocateInfo mesh_vertex_blending_per_mesh_descriptor_set_alloc_info;
            mesh_vertex_blending_per_mesh_descriptor_set_alloc_info.sType =
                RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            mesh_vertex_blending_per_mesh_descriptor_set_alloc_info.pNext = NULL;
            mesh_vertex_blending_per_mesh_descriptor_set_alloc_info.descriptorPool = rhi->getDescriptorPoor();
            mesh_vertex_blending_per_mesh_descriptor_set_alloc_info.descriptorSetCount = 1;
            mesh_vertex_blending_per_mesh_descriptor_set_alloc_info.pSetLayouts        = m_mesh_descriptor_set_layout;

            if (RHI_SUCCESS != rhi->allocateDescriptorSets(
                &mesh_vertex_blending_per_mesh_descriptor_set_alloc_info,
                now_mesh.mesh_vertex_blending_descriptor_set))
            {
                throw std::runtime_error("allocate mesh vertex blending per mesh descriptor set");
            }

            RHIDescriptorBufferInfo mesh_vertex_Joint_binding_storage_buffer_info = {};
            mesh_vertex_Joint_binding_storage_buffer_info.offset = 0;
            mesh_vertex_Joint_binding_storage_buffer_info.range = vertex_joint_binding_buffer_size;
            mesh_vertex_Joint_binding_storage_buffer_info.buffer = now_mesh.mesh_vertex_joint_binding_buffer;
            assert(mesh_vertex_Joint_binding_storage_buffer_info.range <
                m_global_render_resource._storage_buffer._max_storage_buffer_range);

            RHIDescriptorSet* descriptor_set_to_write = now_mesh.mesh_vertex_blending_descriptor_set;

            RHIWriteDescriptorSet descriptor_writes[1];

            RHIWriteDescriptorSet& mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info =
                descriptor_writes[0];
            mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info.sType =
                RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info.pNext = NULL;
            mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info.dstSet = descriptor_set_to_write;
            mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info.dstBinding = 0;
            mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info.dstArrayElement = 0;
            mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info.descriptorType =
                RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info.descriptorCount = 1;
            mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info.pBufferInfo =
                &mesh_vertex_Joint_binding_storage_buffer_info;

            rhi->updateDescriptorSets((sizeof(descriptor_writes) / sizeof(descriptor_writes[0])),
                                      descriptor_writes,
                                      0,
                                      NULL);
        }
        else
        {
            assert(0 == (vertex_buffer_size % sizeof(MeshVertexDataDefinition)));
            uint32_t vertex_count = vertex_buffer_size / sizeof(MeshVertexDataDefinition);

            RHIDeviceSize vertex_position_buffer_size = sizeof(MeshVertex::VertexPosition) * vertex_count;
            RHIDeviceSize vertex_varying_enable_blending_buffer_size =
                sizeof(MeshVertex::VertexVaryingEnableBlending) * vertex_count;
            RHIDeviceSize vertex_varying_buffer_size = sizeof(MeshVertex::VertexVarying) * vertex_count;

            RHIDeviceSize vertex_position_buffer_offset = 0;
            RHIDeviceSize vertex_varying_enable_blending_buffer_offset =
                vertex_position_buffer_offset + vertex_position_buffer_size;
            RHIDeviceSize vertex_varying_buffer_offset =
                vertex_varying_enable_blending_buffer_offset + vertex_varying_enable_blending_buffer_size;

            // temporary staging buffer
            RHIDeviceSize inefficient_staging_buffer_size =
                vertex_position_buffer_size + vertex_varying_enable_blending_buffer_size + vertex_varying_buffer_size;
            RHIBuffer* inefficient_staging_buffer = RHI_NULL_HANDLE;
            RHIDeviceMemory* inefficient_staging_buffer_memory = RHI_NULL_HANDLE;
            rhi->createBuffer(inefficient_staging_buffer_size,
                              RHI_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              inefficient_staging_buffer,
                              inefficient_staging_buffer_memory);

            void* inefficient_staging_buffer_data;
            rhi->mapMemory(inefficient_staging_buffer_memory,
                           0,
                           RHI_WHOLE_SIZE,
                           0,
                           &inefficient_staging_buffer_data);

            MeshVertex::VertexPosition* mesh_vertex_positions =
                reinterpret_cast<MeshVertex::VertexPosition*>(
                    reinterpret_cast<uintptr_t>(inefficient_staging_buffer_data) + vertex_position_buffer_offset);
            MeshVertex::VertexVaryingEnableBlending* mesh_vertex_blending_varyings =
                reinterpret_cast<MeshVertex::VertexVaryingEnableBlending*>(
                    reinterpret_cast<uintptr_t>(inefficient_staging_buffer_data) +
                    vertex_varying_enable_blending_buffer_offset);
            MeshVertex::VertexVarying* mesh_vertex_varyings =
                reinterpret_cast<MeshVertex::VertexVarying*>(
                    reinterpret_cast<uintptr_t>(inefficient_staging_buffer_data) + vertex_varying_buffer_offset);

            for (uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index)
            {
                Vector3 normal = Vector3(vertex_buffer_data[vertex_index].nx,
                    vertex_buffer_data[vertex_index].ny,
                    vertex_buffer_data[vertex_index].nz);
                Vector3 tangent = Vector3(vertex_buffer_data[vertex_index].tx,
                    vertex_buffer_data[vertex_index].ty,
                    vertex_buffer_data[vertex_index].tz);

                mesh_vertex_positions[vertex_index].position = Vector3(vertex_buffer_data[vertex_index].x,
                    vertex_buffer_data[vertex_index].y,
                    vertex_buffer_data[vertex_index].z);

                mesh_vertex_blending_varyings[vertex_index].normal = normal;
                mesh_vertex_blending_varyings[vertex_index].tangent = tangent;

                mesh_vertex_varyings[vertex_index].texcoord =
                    Vector2(vertex_buffer_data[vertex_index].u, vertex_buffer_data[vertex_index].v);
            }

            now_mesh.path_tracing_positions.resize(vertex_count);
            now_mesh.path_tracing_normals.resize(vertex_count);
            now_mesh.path_tracing_tangents.resize(vertex_count);
            now_mesh.path_tracing_texcoords.resize(vertex_count);
            for (uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index)
            {
                now_mesh.path_tracing_positions[vertex_index] = mesh_vertex_positions[vertex_index].position;
                now_mesh.path_tracing_normals[vertex_index]   = mesh_vertex_blending_varyings[vertex_index].normal;
                now_mesh.path_tracing_tangents[vertex_index]  = mesh_vertex_blending_varyings[vertex_index].tangent;
                now_mesh.path_tracing_texcoords[vertex_index] = mesh_vertex_varyings[vertex_index].texcoord;
            }

            assert(0 == (index_buffer_size % sizeof(uint16_t)));
            const uint32_t index_count = index_buffer_size / sizeof(uint16_t);
            now_mesh.path_tracing_indices.resize(index_count);
            for (uint32_t index_index = 0; index_index < index_count; ++index_index)
            {
                now_mesh.path_tracing_indices[index_index] = static_cast<uint32_t>(index_buffer_data[index_index]);
            }
            now_mesh.path_tracing_geometry_dirty = true;

            rhi->unmapMemory(inefficient_staging_buffer_memory);

            // allocate asset vertex buffers in device local memory
            RHIBufferCreateInfo bufferInfo = { RHI_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            const RHIBufferUsageFlags vertex_buffer_usage = withPathTracingBuildInputUsage(
                RHI_BUFFER_USAGE_VERTEX_BUFFER_BIT | RHI_BUFFER_USAGE_TRANSFER_DST_BIT,
                rhi,
                !enable_vertex_blending);
            bufferInfo.usage = vertex_buffer_usage;

            bufferInfo.size = vertex_position_buffer_size;
            rhi->createBufferWithAllocation(&bufferInfo,
                                            RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                            now_mesh.mesh_vertex_position_buffer,
                                            now_mesh.mesh_vertex_position_buffer_allocation);
            bufferInfo.size = vertex_varying_enable_blending_buffer_size;
            rhi->createBufferWithAllocation(&bufferInfo,
                                            RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                            now_mesh.mesh_vertex_varying_enable_blending_buffer,
                                            now_mesh.mesh_vertex_varying_enable_blending_buffer_allocation);
            bufferInfo.size = vertex_varying_buffer_size;
            rhi->createBufferWithAllocation(&bufferInfo,
                                            RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                            now_mesh.mesh_vertex_varying_buffer,
                                            now_mesh.mesh_vertex_varying_buffer_allocation);

            // use the data from staging buffer
            rhi->copyBuffer(inefficient_staging_buffer,
                            now_mesh.mesh_vertex_position_buffer,
                            vertex_position_buffer_offset,
                            0,
                            vertex_position_buffer_size);
            rhi->copyBuffer(inefficient_staging_buffer,
                            now_mesh.mesh_vertex_varying_enable_blending_buffer,
                            vertex_varying_enable_blending_buffer_offset,
                            0,
                            vertex_varying_enable_blending_buffer_size);
            rhi->copyBuffer(inefficient_staging_buffer,
                            now_mesh.mesh_vertex_varying_buffer,
                            vertex_varying_buffer_offset,
                            0,
                            vertex_varying_buffer_size);

            // release staging buffer
            rhi->destroyBuffer(inefficient_staging_buffer);
            rhi->freeMemory(inefficient_staging_buffer_memory);

            // update descriptor set
            RHIDescriptorSetAllocateInfo mesh_vertex_blending_per_mesh_descriptor_set_alloc_info;
            mesh_vertex_blending_per_mesh_descriptor_set_alloc_info.sType =
                RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            mesh_vertex_blending_per_mesh_descriptor_set_alloc_info.pNext = NULL;
            mesh_vertex_blending_per_mesh_descriptor_set_alloc_info.descriptorPool = rhi->getDescriptorPoor();
            mesh_vertex_blending_per_mesh_descriptor_set_alloc_info.descriptorSetCount = 1;
            mesh_vertex_blending_per_mesh_descriptor_set_alloc_info.pSetLayouts        = m_mesh_descriptor_set_layout;

            if (RHI_SUCCESS != rhi->allocateDescriptorSets(
                &mesh_vertex_blending_per_mesh_descriptor_set_alloc_info,
                now_mesh.mesh_vertex_blending_descriptor_set))
            {
                throw std::runtime_error("allocate mesh vertex blending per mesh descriptor set");
            }

            RHIDescriptorBufferInfo mesh_vertex_Joint_binding_storage_buffer_info = {};
            mesh_vertex_Joint_binding_storage_buffer_info.offset = 0;
            mesh_vertex_Joint_binding_storage_buffer_info.range = 1;
            mesh_vertex_Joint_binding_storage_buffer_info.buffer =
                m_global_render_resource._storage_buffer._global_null_descriptor_storage_buffer;
            assert(mesh_vertex_Joint_binding_storage_buffer_info.range <
                m_global_render_resource._storage_buffer._max_storage_buffer_range);

            RHIDescriptorSet* descriptor_set_to_write = now_mesh.mesh_vertex_blending_descriptor_set;

            RHIWriteDescriptorSet descriptor_writes[1];

            RHIWriteDescriptorSet& mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info =
                descriptor_writes[0];
            mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info.sType =
                RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info.pNext = NULL;
            mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info.dstSet = descriptor_set_to_write;
            mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info.dstBinding = 0;
            mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info.dstArrayElement = 0;
            mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info.descriptorType =
                RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info.descriptorCount = 1;
            mesh_vertex_blending_vertex_Joint_binding_storage_buffer_write_info.pBufferInfo =
                &mesh_vertex_Joint_binding_storage_buffer_info;

            rhi->updateDescriptorSets((sizeof(descriptor_writes) / sizeof(descriptor_writes[0])),
                                      descriptor_writes,
                                      0,
                                      NULL);
        }
    }

    void RenderResource::updateIndexBuffer(std::shared_ptr<RHI> rhi,
                                           uint32_t             index_buffer_size,
                                           void*                index_buffer_data,
                                           RenderMeshGPUResource&          now_mesh)
    {
        // temp staging buffer
        RHIDeviceSize buffer_size = index_buffer_size;

        RHIBuffer* inefficient_staging_buffer;
        RHIDeviceMemory* inefficient_staging_buffer_memory;
        rhi->createBuffer(buffer_size,
                          RHI_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          inefficient_staging_buffer,
                          inefficient_staging_buffer_memory);

        void* staging_buffer_data;
        rhi->mapMemory(inefficient_staging_buffer_memory, 0, buffer_size, 0, &staging_buffer_data);
        memcpy(staging_buffer_data, index_buffer_data, (size_t)buffer_size);
        rhi->unmapMemory(inefficient_staging_buffer_memory);

        // allocate asset index buffer in device local memory
        RHIBufferCreateInfo bufferInfo = { RHI_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = buffer_size;
        bufferInfo.usage = withPathTracingBuildInputUsage(
            RHI_BUFFER_USAGE_INDEX_BUFFER_BIT | RHI_BUFFER_USAGE_TRANSFER_DST_BIT,
            rhi,
            now_mesh.path_tracing_static_opaque_supported);

        rhi->createBufferWithAllocation(&bufferInfo,
                                        RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                        now_mesh.mesh_index_buffer,
                                        now_mesh.mesh_index_buffer_allocation);

        // use the data from staging buffer
        rhi->copyBuffer( inefficient_staging_buffer, now_mesh.mesh_index_buffer, 0, 0, buffer_size);

        // release temp staging buffer
        rhi->destroyBuffer(inefficient_staging_buffer);
        rhi->freeMemory(inefficient_staging_buffer_memory);

        assert(0 == (index_buffer_size % sizeof(uint16_t)));
        const uint32_t index_count = index_buffer_size / sizeof(uint16_t);
        now_mesh.path_tracing_indices.resize(index_count);
        const uint16_t* source_indices = reinterpret_cast<const uint16_t*>(index_buffer_data);
        for (uint32_t index_index = 0; index_index < index_count; ++index_index)
        {
            now_mesh.path_tracing_indices[index_index] = static_cast<uint32_t>(source_indices[index_index]);
        }
        now_mesh.path_tracing_geometry_dirty = true;
    }

    void RenderResource::updateTextureImageData(std::shared_ptr<RHI> rhi, const TextureDataToUpdate& texture_data)
    {
        rhi->createGlobalImage(
            texture_data.now_material->base_color_texture_image,
            texture_data.now_material->base_color_image_view,
            texture_data.now_material->base_color_image_allocation,
            texture_data.base_color_image_width,
            texture_data.base_color_image_height,
            texture_data.base_color_image_pixels,
            texture_data.base_color_image_format);

        rhi->createGlobalImage(
            texture_data.now_material->metallic_roughness_texture_image,
            texture_data.now_material->metallic_roughness_image_view,
            texture_data.now_material->metallic_roughness_image_allocation,
            texture_data.metallic_roughness_image_width,
            texture_data.metallic_roughness_image_height,
            texture_data.metallic_roughness_image_pixels,
            texture_data.metallic_roughness_image_format);

        rhi->createGlobalImage(
            texture_data.now_material->normal_texture_image,
            texture_data.now_material->normal_image_view,
            texture_data.now_material->normal_image_allocation,
            texture_data.normal_roughness_image_width,
            texture_data.normal_roughness_image_height,
            texture_data.normal_roughness_image_pixels,
            texture_data.normal_roughness_image_format);

        rhi->createGlobalImage(
            texture_data.now_material->occlusion_texture_image,
            texture_data.now_material->occlusion_image_view,
            texture_data.now_material->occlusion_image_allocation,
            texture_data.occlusion_image_width,
            texture_data.occlusion_image_height,
            texture_data.occlusion_image_pixels,
            texture_data.occlusion_image_format);

        rhi->createGlobalImage(
            texture_data.now_material->emissive_texture_image,
            texture_data.now_material->emissive_image_view,
            texture_data.now_material->emissive_image_allocation,
            texture_data.emissive_image_width,
            texture_data.emissive_image_height,
            texture_data.emissive_image_pixels,
            texture_data.emissive_image_format);
    }

    RenderMeshGPUResource& RenderResource::getEntityMesh(RenderEntity entity)
    {
        size_t assetid = entity.m_mesh_asset_id;

        auto it = m_meshes.find(assetid);
        if (it != m_meshes.end())
        {
            return it->second;
        }
        else
        {
            throw std::runtime_error("failed to get entity mesh");
        }
    }

    RenderPBRMaterialGPUResource& RenderResource::getEntityMaterial(RenderEntity entity)
    {
        size_t assetid = entity.m_material_asset_id;

        auto it = m_pbr_materials.find(assetid);
        if (it != m_pbr_materials.end())
        {
            return it->second;
        }
        else
        {
            throw std::runtime_error("failed to get entity material");
        }
    }

    void RenderResource::resetRingBufferOffset(uint8_t current_frame_index)
    {
        m_global_render_resource._storage_buffer._global_upload_ringbuffers_end[current_frame_index] =
            m_global_render_resource._storage_buffer._global_upload_ringbuffers_begin[current_frame_index];
    }

    void RenderResource::createAndMapStorageBuffer(std::shared_ptr<RHI> rhi)
    {
        StorageBuffer& _storage_buffer = m_global_render_resource._storage_buffer;
        uint32_t       frames_in_flight = rhi->getMaxFramesInFlight();

        RHIPhysicalDeviceProperties properties;
        rhi->getPhysicalDeviceProperties(&properties);

        _storage_buffer._min_uniform_buffer_offset_alignment =
            static_cast<uint32_t>(properties.limits.minUniformBufferOffsetAlignment);
        _storage_buffer._min_storage_buffer_offset_alignment =
            static_cast<uint32_t>(properties.limits.minStorageBufferOffsetAlignment);
        _storage_buffer._max_storage_buffer_range = properties.limits.maxStorageBufferRange;
        _storage_buffer._non_coherent_atom_size = properties.limits.nonCoherentAtomSize;

        // In Vulkan, the storage buffer should be pre-allocated.
        // The size is 128MB in NVIDIA D3D11
        // driver(https://developer.nvidia.com/content/constant-buffers-without-constant-pain-0).
        uint32_t global_storage_buffer_size = 1024 * 1024 * 128;
        rhi->createBuffer(global_storage_buffer_size,
                          RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          _storage_buffer._global_upload_ringbuffer,
                          _storage_buffer._global_upload_ringbuffer_memory);

        _storage_buffer._global_upload_ringbuffers_begin.resize(frames_in_flight);
        _storage_buffer._global_upload_ringbuffers_end.resize(frames_in_flight);
        _storage_buffer._global_upload_ringbuffers_size.resize(frames_in_flight);
        for (uint32_t i = 0; i < frames_in_flight; ++i)
        {
            _storage_buffer._global_upload_ringbuffers_begin[i] = (global_storage_buffer_size * i) / frames_in_flight;
            _storage_buffer._global_upload_ringbuffers_size[i] =
                (global_storage_buffer_size * (i + 1)) / frames_in_flight -
                (global_storage_buffer_size * i) / frames_in_flight;
        }

        // axis
        rhi->createBuffer(sizeof(AxisStorageBufferObject),
                          RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          _storage_buffer._axis_inefficient_storage_buffer,
                          _storage_buffer._axis_inefficient_storage_buffer_memory);

        // null descriptor
        rhi->createBuffer(64,
                          RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          0,
                          _storage_buffer._global_null_descriptor_storage_buffer,
                          _storage_buffer._global_null_descriptor_storage_buffer_memory);

        // TODO: Unmap when program terminates
        rhi->mapMemory(_storage_buffer._global_upload_ringbuffer_memory,
                       0,
                       RHI_WHOLE_SIZE,
                       0,
                       &_storage_buffer._global_upload_ringbuffer_memory_pointer);

        rhi->mapMemory(_storage_buffer._axis_inefficient_storage_buffer_memory,
                       0,
                       RHI_WHOLE_SIZE,
                       0,
                       &_storage_buffer._axis_inefficient_storage_buffer_memory_pointer);

        static_assert(64 >= sizeof(MeshVertex::VertexJointBinding), "");
    }
} // namespace Piccolo