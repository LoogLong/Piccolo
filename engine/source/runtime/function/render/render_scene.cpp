#include "runtime/function/render/render_scene.h"
#include "runtime/core/base/macro.h"
#include "runtime/function/render/render_helper.h"
#include "runtime/function/render/render_pass.h"
#include "runtime/function/render/render_resource.h"

#include <exception>
#include <map>

namespace Piccolo
{
    void RenderScene::clear()
    {
        m_render_entities.clear();
        m_path_tracing_instances.clear();
        m_path_tracing_entity_signatures.clear();
        m_last_path_tracing_skipped_skinned = UINT32_MAX;
        m_last_path_tracing_skipped_transparent = UINT32_MAX;
        m_last_path_tracing_skipped_missing = UINT32_MAX;
        markPathTracingSceneDirty();
    }

    void RenderScene::updateVisibleObjects(std::shared_ptr<RenderResource> render_resource,
                                           std::shared_ptr<RenderCamera>   camera,
                                           bool                            apply_vulkan_y_flip)
    {
        updateVisibleObjectsDirectionalLight(render_resource, camera);
        updateVisibleObjectsPointLight(render_resource);
        updateVisibleObjectsMainCamera(render_resource, camera, apply_vulkan_y_flip);
        updateVisibleObjectsAxis(render_resource);
        updateVisibleObjectsParticle(render_resource);
    }

    void RenderScene::setVisibleNodesReference()
    {
        RenderPass::m_visiable_nodes.p_directional_light_visible_mesh_nodes = &m_directional_light_visible_mesh_nodes;
        RenderPass::m_visiable_nodes.p_point_lights_visible_mesh_nodes      = &m_point_lights_visible_mesh_nodes;
        RenderPass::m_visiable_nodes.p_main_camera_visible_mesh_nodes       = &m_main_camera_visible_mesh_nodes;
        RenderPass::m_visiable_nodes.p_axis_node                            = &m_axis_node;
    }

    GuidAllocator<GameObjectPartId>& RenderScene::getInstanceIdAllocator() { return m_instance_id_allocator; }

    GuidAllocator<MeshSourceDesc>& RenderScene::getMeshAssetIdAllocator() { return m_mesh_asset_id_allocator; }

    GuidAllocator<MaterialSourceDesc>& RenderScene::getMaterialAssetdAllocator()
    {
        return m_material_asset_id_allocator;
    }

    void RenderScene::addInstanceIdToMap(uint32_t instance_id, GObjectID go_id)
    {
        m_mesh_object_id_map[instance_id] = go_id;
    }

    GObjectID RenderScene::getGObjectIDByMeshID(uint32_t mesh_id) const
    {
        auto find_it = m_mesh_object_id_map.find(mesh_id);
        if (find_it != m_mesh_object_id_map.end())
        {
            return find_it->second;
        }
        return GObjectID();
    }

    void RenderScene::deleteEntityByGObjectID(GObjectID go_id)
    {
        bool deleted_entity = false;
        for (auto it = m_mesh_object_id_map.begin(); it != m_mesh_object_id_map.end(); it++)
        {
            if (it->second == go_id)
            {
                m_mesh_object_id_map.erase(it);
                break;
            }
        }

        GameObjectPartId part_id = {go_id, 0};
        size_t           find_guid;
        if (m_instance_id_allocator.getElementGuid(part_id, find_guid))
        {
            for (auto it = m_render_entities.begin(); it != m_render_entities.end(); it++)
            {
                if (it->m_instance_id == find_guid)
                {
                    m_render_entities.erase(it);
                    deleted_entity = true;
                    break;
                }
            }
        }

        if (deleted_entity)
        {
            markPathTracingSceneDirty();
        }
    }

    void RenderScene::clearForLevelReloading()
    {
        m_instance_id_allocator.clear();
        m_mesh_object_id_map.clear();
        m_render_entities.clear();
        m_path_tracing_instances.clear();
        m_path_tracing_entity_signatures.clear();
        m_last_path_tracing_skipped_skinned = UINT32_MAX;
        m_last_path_tracing_skipped_transparent = UINT32_MAX;
        m_last_path_tracing_skipped_missing = UINT32_MAX;
        markPathTracingSceneDirty();
    }

    bool RenderScene::PathTracingEntitySignature::operator==(const PathTracingEntitySignature& rhs) const
    {
        return instance_id == rhs.instance_id &&
               mesh_asset_id == rhs.mesh_asset_id &&
               material_asset_id == rhs.material_asset_id &&
               enable_vertex_blending == rhs.enable_vertex_blending &&
               blend == rhs.blend &&
               base_color_factor == rhs.base_color_factor &&
               double_sided == rhs.double_sided &&
               metallic_factor == rhs.metallic_factor &&
               roughness_factor == rhs.roughness_factor &&
               normal_scale == rhs.normal_scale &&
               occlusion_strength == rhs.occlusion_strength &&
               emissive_factor == rhs.emissive_factor;
    }

    void RenderScene::markPathTracingSceneDirty()
    {
        m_path_tracing_tlas_dirty         = true;
        m_path_tracing_accumulation_dirty = true;
    }

    void RenderScene::markPathTracingAccumulationDirty() { m_path_tracing_accumulation_dirty = true; }

    void RenderScene::clearPathTracingTLASDirty() { m_path_tracing_tlas_dirty = false; }

    void RenderScene::clearPathTracingAccumulationDirty() { m_path_tracing_accumulation_dirty = false; }

    bool RenderScene::isPathTracingTLASDirty() const { return m_path_tracing_tlas_dirty; }

    bool RenderScene::isPathTracingAccumulationDirty() const { return m_path_tracing_accumulation_dirty; }

    void RenderScene::rebuildPathTracingInstances(RenderResource& render_resource, bool log_skipped_instances)
    {
        std::vector<PathTracingEntitySignature> current_signatures;
        current_signatures.reserve(m_render_entities.size());
        for (const RenderEntity& entity : m_render_entities)
        {
            PathTracingEntitySignature signature;
            signature.instance_id            = entity.m_instance_id;
            signature.mesh_asset_id          = entity.m_mesh_asset_id;
            signature.material_asset_id      = entity.m_material_asset_id;
            signature.model_matrix           = entity.m_model_matrix;
            signature.enable_vertex_blending = entity.m_enable_vertex_blending;
            signature.blend                  = entity.m_blend;
            signature.base_color_factor      = entity.m_base_color_factor;
            signature.double_sided           = entity.m_double_sided;
            signature.metallic_factor        = entity.m_metallic_factor;
            signature.roughness_factor       = entity.m_roughness_factor;
            signature.normal_scale           = entity.m_normal_scale;
            signature.occlusion_strength     = entity.m_occlusion_strength;
            signature.emissive_factor        = entity.m_emissive_factor;
            current_signatures.push_back(signature);
        }

        if (current_signatures != m_path_tracing_entity_signatures)
        {
            m_path_tracing_entity_signatures = std::move(current_signatures);
            markPathTracingSceneDirty();
        }

        m_path_tracing_instances.clear();

        uint32_t skipped_skinned     = 0;
        uint32_t skipped_transparent = 0;
        uint32_t skipped_missing     = 0;
        std::map<size_t, uint32_t> material_indices;

        for (RenderEntity& entity : m_render_entities)
        {
            if (entity.m_blend || entity.m_base_color_factor.w < 1.0f)
            {
                ++skipped_transparent;
                continue;
            }

            RenderMeshGPUResource*        mesh     = nullptr;
            RenderPBRMaterialGPUResource* material = nullptr;
            try
            {
                mesh     = &render_resource.getEntityMesh(entity);
                material = &render_resource.getEntityMaterial(entity);
            }
            catch (const std::exception&)
            {
                ++skipped_missing;
                continue;
            }

            if (mesh == nullptr || material == nullptr)
            {
                ++skipped_missing;
                continue;
            }

            auto material_index = material_indices.find(entity.m_material_asset_id);
            if (material_index == material_indices.end())
            {
                material_index = material_indices
                                     .insert(std::make_pair(entity.m_material_asset_id,
                                                            static_cast<uint32_t>(material_indices.size())))
                                     .first;
            }

            RenderPathTracingInstance instance;
            instance.entity         = &entity;
            instance.mesh           = mesh;
            instance.material       = material;
            instance.instance_id    = entity.m_instance_id;
            instance.material_index = material_index->second;
            instance.enabled        = true;
            instance.enable_vertex_blending = entity.m_enable_vertex_blending;
            instance.joint_count            = static_cast<uint32_t>(entity.m_joint_matrices.size());
            m_path_tracing_instances.push_back(instance);

            if (mesh->path_tracing_blas_dirty)
            {
                markPathTracingSceneDirty();
            }
        }

        const bool skipped_counts_changed =
            skipped_skinned != m_last_path_tracing_skipped_skinned ||
            skipped_transparent != m_last_path_tracing_skipped_transparent ||
            skipped_missing != m_last_path_tracing_skipped_missing;

        if (log_skipped_instances && skipped_counts_changed)
        {
            if (skipped_skinned > 0 || skipped_transparent > 0)
            {
                LOG_INFO("Path tracing scene export skipped {} skinned/vertex-blended and {} transparent/blended instances",
                         skipped_skinned,
                         skipped_transparent);
            }
            if (skipped_missing > 0)
            {
                LOG_WARN("Path tracing scene export skipped {} instances with missing mesh or material GPU resources",
                         skipped_missing);
            }
        }

        m_last_path_tracing_skipped_skinned = skipped_skinned;
        m_last_path_tracing_skipped_transparent = skipped_transparent;
        m_last_path_tracing_skipped_missing = skipped_missing;
    }

    void RenderScene::updateVisibleObjectsDirectionalLight(std::shared_ptr<RenderResource> render_resource,
                                                           std::shared_ptr<RenderCamera>   camera)
    {
        Matrix4x4 directional_light_proj_view = CalculateDirectionalLightCamera(*this, *camera);

        render_resource->m_mesh_perframe_storage_buffer_object.directional_light_proj_view =
            directional_light_proj_view;
        render_resource->m_mesh_directional_light_shadow_perframe_storage_buffer_object.light_proj_view =
            directional_light_proj_view;

        m_directional_light_visible_mesh_nodes.clear();

        ClusterFrustum frustum =
            CreateClusterFrustumFromMatrix(directional_light_proj_view, -1.0, 1.0, -1.0, 1.0, 0.0, 1.0);

        for (const RenderEntity& entity : m_render_entities)
        {
            BoundingBox mesh_asset_bounding_box {entity.m_bounding_box.getMinCorner(),
                                                 entity.m_bounding_box.getMaxCorner()};

            if (TiledFrustumIntersectBox(frustum, BoundingBoxTransform(mesh_asset_bounding_box, entity.m_model_matrix)))
            {
                m_directional_light_visible_mesh_nodes.emplace_back();
                RenderMeshNode& temp_node = m_directional_light_visible_mesh_nodes.back();

                temp_node.model_matrix = &entity.m_model_matrix;

                assert(entity.m_joint_matrices.size() <= s_mesh_vertex_blending_max_joint_count);
                if (!entity.m_joint_matrices.empty())
                {
                    temp_node.joint_count    = static_cast<uint32_t>(entity.m_joint_matrices.size());
                    temp_node.joint_matrices = entity.m_joint_matrices.data();
                }
                temp_node.node_id = entity.m_instance_id;

                RenderMeshGPUResource& mesh_asset           = render_resource->getEntityMesh(entity);
                temp_node.ref_mesh               = &mesh_asset;
                temp_node.enable_vertex_blending = entity.m_enable_vertex_blending;

                RenderPBRMaterialGPUResource& material_asset = render_resource->getEntityMaterial(entity);
                temp_node.ref_material            = &material_asset;
            }
        }
    }

    void RenderScene::updateVisibleObjectsPointLight(std::shared_ptr<RenderResource> render_resource)
    {
        m_point_lights_visible_mesh_nodes.clear();

        std::vector<BoundingSphere> point_lights_bounding_spheres;
        uint32_t                    point_light_num = static_cast<uint32_t>(m_point_light_list.m_lights.size());
        point_lights_bounding_spheres.resize(point_light_num);
        for (size_t i = 0; i < point_light_num; i++)
        {
            point_lights_bounding_spheres[i].m_center = m_point_light_list.m_lights[i].m_position;
            point_lights_bounding_spheres[i].m_radius = m_point_light_list.m_lights[i].calculateRadius();
        }

        for (const RenderEntity& entity : m_render_entities)
        {
            BoundingBox mesh_asset_bounding_box {entity.m_bounding_box.getMinCorner(),
                                                 entity.m_bounding_box.getMaxCorner()};

            bool intersect_with_point_lights = true;
            for (size_t i = 0; i < point_light_num; i++)
            {
                if (!BoxIntersectsWithSphere(BoundingBoxTransform(mesh_asset_bounding_box, entity.m_model_matrix),
                                             point_lights_bounding_spheres[i]))
                {
                    intersect_with_point_lights = false;
                    break;
                }
            }

            if (intersect_with_point_lights)
            {
                m_point_lights_visible_mesh_nodes.emplace_back();
                RenderMeshNode& temp_node = m_point_lights_visible_mesh_nodes.back();

                temp_node.model_matrix = &entity.m_model_matrix;

                assert(entity.m_joint_matrices.size() <= s_mesh_vertex_blending_max_joint_count);
                if (!entity.m_joint_matrices.empty())
                {
                    temp_node.joint_count    = static_cast<uint32_t>(entity.m_joint_matrices.size());
                    temp_node.joint_matrices = entity.m_joint_matrices.data();
                }
                temp_node.node_id = entity.m_instance_id;

                RenderMeshGPUResource& mesh_asset           = render_resource->getEntityMesh(entity);
                temp_node.ref_mesh               = &mesh_asset;
                temp_node.enable_vertex_blending = entity.m_enable_vertex_blending;

                RenderPBRMaterialGPUResource& material_asset = render_resource->getEntityMaterial(entity);
                temp_node.ref_material            = &material_asset;
            }
        }
    }

    void RenderScene::updateVisibleObjectsMainCamera(std::shared_ptr<RenderResource> render_resource,
                                                     std::shared_ptr<RenderCamera>   camera,
                                                     bool                            apply_vulkan_y_flip)
    {
        m_main_camera_visible_mesh_nodes.clear();

        Matrix4x4 view_matrix      = camera->getViewMatrix();
        Matrix4x4 proj_matrix      = camera->getPersProjMatrix(apply_vulkan_y_flip);
        Matrix4x4 proj_view_matrix = proj_matrix * view_matrix;

        ClusterFrustum f = CreateClusterFrustumFromMatrix(proj_view_matrix, -1.0, 1.0, -1.0, 1.0, 0.0, 1.0);

        for (const RenderEntity& entity : m_render_entities)
        {
            BoundingBox mesh_asset_bounding_box {entity.m_bounding_box.getMinCorner(),
                                                 entity.m_bounding_box.getMaxCorner()};

            if (TiledFrustumIntersectBox(f, BoundingBoxTransform(mesh_asset_bounding_box, entity.m_model_matrix)))
            {
                m_main_camera_visible_mesh_nodes.emplace_back();
                RenderMeshNode& temp_node = m_main_camera_visible_mesh_nodes.back();
                temp_node.model_matrix    = &entity.m_model_matrix;

                assert(entity.m_joint_matrices.size() <= s_mesh_vertex_blending_max_joint_count);
                if (!entity.m_joint_matrices.empty())
                {
                    temp_node.joint_count    = static_cast<uint32_t>(entity.m_joint_matrices.size());
                    temp_node.joint_matrices = entity.m_joint_matrices.data();
                }
                temp_node.node_id = entity.m_instance_id;

                RenderMeshGPUResource& mesh_asset           = render_resource->getEntityMesh(entity);
                temp_node.ref_mesh               = &mesh_asset;
                temp_node.enable_vertex_blending = entity.m_enable_vertex_blending;

                RenderPBRMaterialGPUResource& material_asset = render_resource->getEntityMaterial(entity);
                temp_node.ref_material            = &material_asset;
            }
        }
    }

    void RenderScene::updateVisibleObjectsAxis(std::shared_ptr<RenderResource> render_resource)
    {
        if (m_render_axis.has_value())
        {
            RenderEntity& axis = *m_render_axis;

            m_axis_node.model_matrix = axis.m_model_matrix;
            m_axis_node.node_id      = axis.m_instance_id;

            RenderMeshGPUResource& mesh_asset             = render_resource->getEntityMesh(axis);
            m_axis_node.ref_mesh               = &mesh_asset;
            m_axis_node.enable_vertex_blending = axis.m_enable_vertex_blending;
        }
    }

    void RenderScene::updateVisibleObjectsParticle(std::shared_ptr<RenderResource> render_resource)
    {
        // TODO
    }
} // namespace Piccolo