#pragma once

#include "runtime/function/framework/object/object_id_allocator.h"

#include "runtime/function/render/light.h"
#include "runtime/function/render/render_common.h"
#include "runtime/function/render/render_entity.h"
#include "runtime/function/render/render_guid_allocator.h"
#include "runtime/function/render/render_object.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace Piccolo
{
    class RenderResource;
    class RenderCamera;

    struct RenderPathTracingInstance
    {
        RenderEntity*                 entity {nullptr};
        RenderMeshGPUResource*        mesh {nullptr};
        RenderPBRMaterialGPUResource* material {nullptr};
        uint32_t                      instance_id {0};
        uint32_t                      material_index {0};
        bool                          enabled {true};
    };

    class RenderScene
    {
    public:
        // light
        AmbientLight      m_ambient_light;
        PDirectionalLight m_directional_light;
        PointLightList    m_point_light_list;

        // render entities
        std::vector<RenderEntity> m_render_entities;
        std::vector<RenderPathTracingInstance> m_path_tracing_instances;
        bool                                   m_path_tracing_tlas_dirty {true};
        bool                                   m_path_tracing_accumulation_dirty {true};

        // axis, for editor
        std::optional<RenderEntity> m_render_axis;

        // visible objects (updated per frame)
        std::vector<RenderMeshNode> m_directional_light_visible_mesh_nodes;
        std::vector<RenderMeshNode> m_point_lights_visible_mesh_nodes;
        std::vector<RenderMeshNode> m_main_camera_visible_mesh_nodes;
        RenderAxisNode              m_axis_node;

        // clear
        void clear();

        // update visible objects in each frame
        void updateVisibleObjects(std::shared_ptr<RenderResource> render_resource,
                                  std::shared_ptr<RenderCamera>   camera,
                                  bool                            apply_vulkan_y_flip);

        // set visible nodes ptr in render pass
        void setVisibleNodesReference();

        GuidAllocator<GameObjectPartId>&   getInstanceIdAllocator();
        GuidAllocator<MeshSourceDesc>&     getMeshAssetIdAllocator();
        GuidAllocator<MaterialSourceDesc>& getMaterialAssetdAllocator();

        void      addInstanceIdToMap(uint32_t instance_id, GObjectID go_id);
        GObjectID getGObjectIDByMeshID(uint32_t mesh_id) const;
        void      deleteEntityByGObjectID(GObjectID go_id);

        void clearForLevelReloading();

        void rebuildPathTracingInstances(RenderResource& render_resource, bool log_skipped_instances);
        void markPathTracingSceneDirty();
        void markPathTracingAccumulationDirty();
        void clearPathTracingTLASDirty();
        void clearPathTracingAccumulationDirty();
        bool isPathTracingTLASDirty() const;
        bool isPathTracingAccumulationDirty() const;

    private:
        struct PathTracingEntitySignature
        {
            uint32_t  instance_id {0};
            size_t    mesh_asset_id {0};
            size_t    material_asset_id {0};
            Matrix4x4 model_matrix {Matrix4x4::IDENTITY};
            bool      enable_vertex_blending {false};
            bool      blend {false};
            Vector4   base_color_factor {1.0f, 1.0f, 1.0f, 1.0f};

            bool operator==(const PathTracingEntitySignature& rhs) const;
        };

        GuidAllocator<GameObjectPartId>   m_instance_id_allocator;
        GuidAllocator<MeshSourceDesc>     m_mesh_asset_id_allocator;
        GuidAllocator<MaterialSourceDesc> m_material_asset_id_allocator;

        std::unordered_map<uint32_t, GObjectID> m_mesh_object_id_map;
        std::vector<PathTracingEntitySignature> m_path_tracing_entity_signatures;

        void updateVisibleObjectsDirectionalLight(std::shared_ptr<RenderResource> render_resource,
                                                  std::shared_ptr<RenderCamera>   camera);
        void updateVisibleObjectsPointLight(std::shared_ptr<RenderResource> render_resource);
        void updateVisibleObjectsMainCamera(std::shared_ptr<RenderResource> render_resource,
                                            std::shared_ptr<RenderCamera>   camera,
                                            bool                            apply_vulkan_y_flip);
        void updateVisibleObjectsAxis(std::shared_ptr<RenderResource> render_resource);
        void updateVisibleObjectsParticle(std::shared_ptr<RenderResource> render_resource);
    };
} // namespace Piccolo
