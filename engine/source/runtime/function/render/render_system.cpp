#include "runtime/function/render/render_system.h"

#include "runtime/core/base/macro.h"

#include "runtime/resource/asset_manager/asset_manager.h"
#include "runtime/resource/config_manager/config_manager.h"

#include "runtime/function/render/render_camera.h"
#include "runtime/function/render/render_backend.h"
#include "runtime/function/render/render_shader_bytecode.h"
#include "runtime/function/render/path_tracing_diagnostics.h"
#include "runtime/function/render/render_pass.h"
#include "runtime/function/render/render_pipeline.h"
#include "runtime/function/render/render_resource.h"
#include "runtime/function/render/render_resource_base.h"
#include "runtime/function/render/render_scene.h"
#include "runtime/function/render/window_system.h"
#include "runtime/function/global/global_context.h"
#include "runtime/function/render/debugdraw/debug_draw_manager.h"

#include "runtime/function/render/passes/main_camera_pass.h"
#include "runtime/function/render/passes/particle_pass.h"

#if PICCOLO_ENABLE_VULKAN_BACKEND
#include "runtime/function/render/interface/vulkan/vulkan_rhi.h"
#endif

#if PICCOLO_ENABLE_D3D12_BACKEND && defined(_WIN32)
#include "runtime/function/render/interface/d3d12/d3d12_rhi.h"
#endif

#include <exception>
#include <stdexcept>
#include <string>

namespace
{
    using namespace Piccolo;

    std::shared_ptr<RHI> createRHIBackend(RHIBackendType backend)
    {
        switch (backend)
        {
            case RHIBackendType::Vulkan:
#if PICCOLO_ENABLE_VULKAN_BACKEND
                return std::make_shared<VulkanRHI>();
#else
                return nullptr;
#endif
            case RHIBackendType::D3D12:
#if PICCOLO_ENABLE_D3D12_BACKEND && defined(_WIN32)
                return std::make_shared<D3D12RHI>();
#else
                return nullptr;
#endif
            case RHIBackendType::Auto:
            default:
                return nullptr;
        }
    }
}

namespace Piccolo
{
    RenderSystem::~RenderSystem()
    {
        clear();
    }

    void RenderSystem::initialize(RenderSystemInitInfo init_info)
    {
        std::shared_ptr<ConfigManager> config_manager = g_runtime_global_context.m_config_manager;
        ASSERT(config_manager);
        std::shared_ptr<AssetManager> asset_manager = g_runtime_global_context.m_asset_manager;
        ASSERT(asset_manager);

        // render context initialize
        RHIInitInfo rhi_init_info;
        rhi_init_info.window_system = init_info.window_system;
        rhi_init_info.allow_fallback_to_vulkan = config_manager->getRenderBackendAllowFallback();

        const RHIBackendType configured_backend = parseRenderBackend(config_manager->getRenderBackend());
        RHIBackendType       requested_backend  = configured_backend;
        if (requested_backend == RHIBackendType::Auto)
        {
            requested_backend = getPlatformDefaultRenderBackend();
        }

        auto tryInitializeBackend = [&](RHIBackendType backend) -> bool {
            std::shared_ptr<RHI> rhi = createRHIBackend(backend);
            if (!rhi)
            {
                LOG_WARN(std::string("RHI backend ") + renderBackendToString(backend) + " is not implemented in this build");
                return false;
            }

            if (backend == RHIBackendType::D3D12 && !d3d12ShaderBytecodeAvailable())
            {
                LOG_ERROR("RHI backend initialization failed for D3D12: DXIL shader bytecode is unavailable. Install dxc.exe and reconfigure the build to enable D3D12.");
                return false;
            }

            rhi_init_info.requested_backend = backend;
            try
            {
                rhi->initialize(rhi_init_info);
                m_rhi = std::move(rhi);
                LOG_INFO(std::string("Initialized RHI backend: ") + renderBackendToString(backend));
                return true;
            }
            catch (const std::exception& e)
            {
                LOG_ERROR(std::string("RHI backend initialization failed for ") + renderBackendToString(backend) + ": " + e.what());
            }
            catch (...)
            {
                LOG_ERROR(std::string("RHI backend initialization failed for ") + renderBackendToString(backend) + ": unknown exception");
            }
            return false;
        };

        LOG_INFO(std::string("Requested RHI backend: ") + renderBackendToString(requested_backend));
        if (!tryInitializeBackend(requested_backend))
        {
            if (rhi_init_info.allow_fallback_to_vulkan && requested_backend != RHIBackendType::Vulkan)
            {
                LOG_WARN(std::string("Falling back to Vulkan backend from ") + renderBackendToString(requested_backend));
                if (!tryInitializeBackend(RHIBackendType::Vulkan))
                {
                    throw std::runtime_error("Failed to initialize both primary and fallback RHI backends");
                }
            }
            else
            {
                throw std::runtime_error(std::string("Failed to initialize RHI backend: ") + renderBackendToString(requested_backend));
            }
        }

        // global rendering resource
        GlobalRenderingRes global_rendering_res;
        const std::string& global_rendering_res_url = config_manager->getGlobalRenderingResUrl();
        asset_manager->loadAsset(global_rendering_res_url, global_rendering_res);

        // upload ibl, color grading textures
        LevelResourceDesc level_resource_desc;
        level_resource_desc.m_ibl_resource_desc.m_skybox_irradiance_map = global_rendering_res.m_skybox_irradiance_map;
        level_resource_desc.m_ibl_resource_desc.m_skybox_specular_map   = global_rendering_res.m_skybox_specular_map;
        level_resource_desc.m_ibl_resource_desc.m_brdf_map              = global_rendering_res.m_brdf_map;
        level_resource_desc.m_color_grading_resource_desc.m_color_grading_map =
            global_rendering_res.m_color_grading_map;

        m_render_resource = std::make_shared<RenderResource>();
        m_render_resource->uploadGlobalRenderResource(m_rhi, level_resource_desc);

        // setup render camera
        const CameraPose& camera_pose = global_rendering_res.m_camera_config.m_pose;
        m_render_camera               = std::make_shared<RenderCamera>();
        m_render_camera->lookAt(camera_pose.m_position, camera_pose.m_target, camera_pose.m_up);
        m_render_camera->m_zfar  = global_rendering_res.m_camera_config.m_z_far;
        m_render_camera->m_znear = global_rendering_res.m_camera_config.m_z_near;
        m_render_camera->setAspect(global_rendering_res.m_camera_config.m_aspect.x /
                                   global_rendering_res.m_camera_config.m_aspect.y);

        // setup render scene
        m_render_scene                  = std::make_shared<RenderScene>();
        m_render_scene->m_ambient_light = {global_rendering_res.m_ambient_light.toVector3()};
        m_render_scene->m_directional_light.m_direction =
            global_rendering_res.m_directional_light.m_direction.normalisedCopy();
        m_render_scene->m_directional_light.m_color = global_rendering_res.m_directional_light.m_color.toVector3();
        m_render_scene->setVisibleNodesReference();

        // initialize render pipeline
        RenderPipelineInitInfo pipeline_init_info;
        pipeline_init_info.enable_fxaa     = global_rendering_res.m_enable_fxaa;
        pipeline_init_info.render_resource = m_render_resource;

        m_render_pipeline        = std::make_shared<RenderPipeline>();
        m_render_pipeline->m_rhi = m_rhi;
        m_render_pipeline->initialize(pipeline_init_info);

        LOG_INFO("Render system startup: binary_root='{}', ini_RenderBackend='{}', configured_backend={}, "
                 "active_backend={}, ini_RenderSceneMode='{}', allow_backend_fallback={}, "
                 "ray_tracing_supported={}, path_tracing_shader_bytecode={}, path_tracing_ready={}, "
                 "requested_scene_mode={}, effective_scene_mode={}",
                 config_manager->getRootFolder().generic_string(),
                 config_manager->getRenderBackend(),
                 renderBackendToString(configured_backend),
                 renderBackendToString(m_rhi->getBackendType()),
                 config_manager->getRenderSceneMode(),
                 config_manager->getRenderBackendAllowFallback(),
                 m_rhi->supportsRayTracing(),
                 pathTracingBytecodeAvailable(*m_rhi),
                 supportsPathTracing(*m_rhi),
                 m_render_pipeline->getRequestedSceneRenderMode() == RenderSceneRenderMode::PathTracing ? "PathTracing"
                                                                                                        : "Raster",
                 m_render_pipeline->getEffectiveSceneRenderMode() == RenderSceneRenderMode::PathTracing ? "PathTracing"
                                                                                                        : "Raster");
#if PICCOLO_ENABLE_D3D12_BACKEND && defined(_WIN32)
        if (m_rhi->getBackendType() == RHIBackendType::D3D12)
        {
            LOG_INFO("D3D12 shader bytecode available: {}", d3d12ShaderBytecodeAvailable());
        }
#endif

        // descriptor set layout in main camera pass will be used when uploading resource
        std::static_pointer_cast<RenderResource>(m_render_resource)->m_mesh_descriptor_set_layout =
            &static_cast<RenderPass*>(m_render_pipeline->m_main_camera_pass.get())
                 ->m_descriptor_infos[MainCameraPass::LayoutType::_per_mesh]
                 .layout;
        std::static_pointer_cast<RenderResource>(m_render_resource)->m_material_descriptor_set_layout =
            &static_cast<RenderPass*>(m_render_pipeline->m_main_camera_pass.get())
                 ->m_descriptor_infos[MainCameraPass::LayoutType::_mesh_per_material]
                 .layout;

        logPathTracingReadinessReport(*m_rhi, *m_render_pipeline);
    }

    void RenderSystem::tick(float delta_time)
    {
        // process swap data between logic and render contexts
        processSwapData();

        // prepare render command context
        m_rhi->prepareContext();

        // update per-frame buffer
        m_render_resource->updatePerFrameBuffer(m_rhi, m_render_scene, m_render_camera);

        // update per-frame visible objects
        m_render_scene->updateVisibleObjects(std::static_pointer_cast<RenderResource>(m_render_resource),
                                             m_render_camera,
                                             m_rhi->usesVulkanClipSpace());

        // prepare pipeline's render passes data
        m_render_pipeline->preparePassData(m_render_resource);

        g_runtime_global_context.m_debugdraw_manager->tick(delta_time);

        // render one frame
        if (m_render_pipeline->pathTracingRender(m_rhi, m_render_resource))
        {
        }
        else if (m_render_pipeline_type == RENDER_PIPELINE_TYPE::FORWARD_PIPELINE)
        {
            m_render_pipeline->forwardRender(m_rhi, m_render_resource);
        }
        else if (m_render_pipeline_type == RENDER_PIPELINE_TYPE::DEFERRED_PIPELINE)
        {
            m_render_pipeline->deferredRender(m_rhi, m_render_resource);
        }
        else
        {
            LOG_ERROR(__FUNCTION__, "unsupported render pipeline type");
        }
    }

    void RenderSystem::clear()
    {
        if (m_render_pipeline)
        {
            m_render_pipeline->shutdownUIRenderBackend();
        }

        if (m_rhi)
        {
            m_rhi->waitForFences();
        }

        if (m_render_scene)
        {
            m_render_scene->clear();
        }
        m_render_scene.reset();

        if (m_render_pipeline)
        {
            m_render_pipeline->clear();
        }
        m_render_pipeline.reset();

        if (m_render_resource)
        {
            m_render_resource->clear();
        }
        m_render_resource.reset();

        if (m_rhi)
        {
            m_rhi->clear();
        }
        m_rhi.reset();
    }

    void RenderSystem::swapLogicRenderData() { m_swap_context.swapLogicRenderData(); }

    RenderSwapContext& RenderSystem::getSwapContext() { return m_swap_context; }

    std::shared_ptr<RenderCamera> RenderSystem::getRenderCamera() const { return m_render_camera; }

    std::shared_ptr<RHI>          RenderSystem::getRHI() const { return m_rhi; }

    void RenderSystem::updateEngineContentViewport(float offset_x, float offset_y, float width, float height)
    {
        m_rhi->setViewport(offset_x, offset_y, width, height, 0.0f, 1.0f);

        m_render_camera->setAspect(width / height);
    }

    EngineContentViewport RenderSystem::getEngineContentViewport() const
    {
        const RHIViewport viewport = m_rhi->getViewport();
        return {viewport.x, viewport.y, viewport.width, viewport.height};
    }

    uint32_t RenderSystem::getGuidOfPickedMesh(const Vector2& picked_uv)
    {
        return m_render_pipeline->getGuidOfPickedMesh(picked_uv);
    }

    GObjectID RenderSystem::getGObjectIDByMeshID(uint32_t mesh_id) const
    {
        return m_render_scene->getGObjectIDByMeshID(mesh_id);
    }

    void RenderSystem::createAxis(std::array<RenderEntity, 3> axis_entities, std::array<RenderMeshData, 3> mesh_datas)
    {
        for (int i = 0; i < axis_entities.size(); i++)
        {
            m_render_resource->uploadGameObjectRenderResource(m_rhi, axis_entities[i], mesh_datas[i]);
        }
    }

    void RenderSystem::setVisibleAxis(std::optional<RenderEntity> axis)
    {
        m_render_scene->m_render_axis = axis;

        if (axis.has_value())
        {
            std::static_pointer_cast<RenderPipeline>(m_render_pipeline)->setAxisVisibleState(true);
        }
        else
        {
            std::static_pointer_cast<RenderPipeline>(m_render_pipeline)->setAxisVisibleState(false);
        }
    }

    void RenderSystem::setSelectedAxis(size_t selected_axis)
    {
        std::static_pointer_cast<RenderPipeline>(m_render_pipeline)->setSelectedAxis(selected_axis);
    }

    GuidAllocator<GameObjectPartId>& RenderSystem::getGOInstanceIdAllocator()
    {
        return m_render_scene->getInstanceIdAllocator();
    }

    GuidAllocator<MeshSourceDesc>& RenderSystem::getMeshAssetIdAllocator()
    {
        return m_render_scene->getMeshAssetIdAllocator();
    }

    void RenderSystem::clearForLevelReloading()
    {
        m_render_scene->clearForLevelReloading();
    }

    void RenderSystem::setRenderPipelineType(RENDER_PIPELINE_TYPE pipeline_type)
    {
        m_render_pipeline_type = pipeline_type;
    }

    void RenderSystem::initializeUIRenderBackend(WindowUI* window_ui)
    {
        m_render_pipeline->initializeUIRenderBackend(window_ui);
    }

    void RenderSystem::processSwapData()
    {
        RenderSwapData& swap_data = m_swap_context.getRenderSwapData();

        std::shared_ptr<AssetManager> asset_manager = g_runtime_global_context.m_asset_manager;
        ASSERT(asset_manager);

        // TODO: update global resources if needed
        if (swap_data.m_level_resource_desc.has_value())
        {
            m_render_resource->uploadGlobalRenderResource(m_rhi, *swap_data.m_level_resource_desc);

            // reset level resource swap data to a clean state
            m_swap_context.resetLevelRsourceSwapData();
        }

        // update game object if needed
        if (swap_data.m_game_object_resource_desc.has_value())
        {
            while (!swap_data.m_game_object_resource_desc->isEmpty())
            {
                GameObjectDesc gobject = swap_data.m_game_object_resource_desc->getNextProcessObject();

                for (size_t part_index = 0; part_index < gobject.getObjectParts().size(); part_index++)
                {
                    const auto&      game_object_part = gobject.getObjectParts()[part_index];
                    GameObjectPartId part_id          = {gobject.getId(), part_index};

                    bool is_entity_in_scene = m_render_scene->getInstanceIdAllocator().hasElement(part_id);

                    RenderEntity render_entity;
                    render_entity.m_instance_id =
                        static_cast<uint32_t>(m_render_scene->getInstanceIdAllocator().allocGuid(part_id));
                    render_entity.m_model_matrix = game_object_part.m_transform_desc.m_transform_matrix;

                    m_render_scene->addInstanceIdToMap(render_entity.m_instance_id, gobject.getId());

                    // mesh properties
                    MeshSourceDesc mesh_source    = {game_object_part.m_mesh_desc.m_mesh_file};
                    bool           is_mesh_loaded = m_render_scene->getMeshAssetIdAllocator().hasElement(mesh_source);

                    RenderMeshData mesh_data;
                    if (!is_mesh_loaded)
                    {
                        mesh_data = m_render_resource->loadMeshData(mesh_source, render_entity.m_bounding_box);
                    }
                    else
                    {
                        render_entity.m_bounding_box = m_render_resource->getCachedBoudingBox(mesh_source);
                    }

                    render_entity.m_mesh_asset_id = m_render_scene->getMeshAssetIdAllocator().allocGuid(mesh_source);
                    render_entity.m_enable_vertex_blending =
                        game_object_part.m_skeleton_animation_result.m_transforms.size() > 1; // take care
                    render_entity.m_joint_matrices.resize(
                        game_object_part.m_skeleton_animation_result.m_transforms.size());
                    for (size_t i = 0; i < game_object_part.m_skeleton_animation_result.m_transforms.size(); ++i)
                    {
                        render_entity.m_joint_matrices[i] =
                            game_object_part.m_skeleton_animation_result.m_transforms[i].m_matrix;
                    }

                    // material properties
                    MaterialSourceDesc material_source;
                    if (game_object_part.m_material_desc.m_with_texture)
                    {
                        material_source = {game_object_part.m_material_desc.m_base_color_texture_file,
                                           game_object_part.m_material_desc.m_metallic_roughness_texture_file,
                                           game_object_part.m_material_desc.m_normal_texture_file,
                                           game_object_part.m_material_desc.m_occlusion_texture_file,
                                           game_object_part.m_material_desc.m_emissive_texture_file};
                    }
                    else
                    {
                        // TODO: move to default material definition json file
                        material_source = {
                            asset_manager->getFullPath("asset/texture/default/albedo.jpg").generic_string(),
                            asset_manager->getFullPath("asset/texture/default/mr.jpg").generic_string(),
                            asset_manager->getFullPath("asset/texture/default/normal.jpg").generic_string(),
                            "",
                            ""};
                    }
                    bool is_material_loaded = m_render_scene->getMaterialAssetdAllocator().hasElement(material_source);

                    RenderMaterialData material_data;
                    if (!is_material_loaded)
                    {
                        material_data = m_render_resource->loadMaterialData(material_source);
                    }

                    render_entity.m_material_asset_id =
                        m_render_scene->getMaterialAssetdAllocator().allocGuid(material_source);

                    // create game object on the graphics api side
                    if (!is_mesh_loaded)
                    {
                        m_render_resource->uploadGameObjectRenderResource(m_rhi, render_entity, mesh_data);
                    }

                    if (!is_material_loaded)
                    {
                        m_render_resource->uploadGameObjectRenderResource(m_rhi, render_entity, material_data);
                    }

                    // add object to render scene if needed
                    if (!is_entity_in_scene)
                    {
                        m_render_scene->m_render_entities.push_back(render_entity);
                    }
                    else
                    {
                        for (auto& entity : m_render_scene->m_render_entities)
                        {
                            if (entity.m_instance_id == render_entity.m_instance_id)
                            {
                                entity = render_entity;
                                break;
                            }
                        }
                    }
                }
                // after finished processing, pop this game object
                swap_data.m_game_object_resource_desc->pop();
            }

            // reset game object swap data to a clean state
            m_swap_context.resetGameObjectResourceSwapData();
        }

        // remove deleted objects
        if (swap_data.m_game_object_to_delete.has_value())
        {
            while (!swap_data.m_game_object_to_delete->isEmpty())
            {
                GameObjectDesc gobject = swap_data.m_game_object_to_delete->getNextProcessObject();
                m_render_scene->deleteEntityByGObjectID(gobject.getId());
                swap_data.m_game_object_to_delete->pop();
            }

            m_swap_context.resetGameObjectToDelete();
        }

        // process camera swap data
        if (swap_data.m_camera_swap_data.has_value())
        {
            if (swap_data.m_camera_swap_data->m_fov_x.has_value())
            {
                m_render_camera->setFOVx(*swap_data.m_camera_swap_data->m_fov_x);
            }

            if (swap_data.m_camera_swap_data->m_view_matrix.has_value())
            {
                m_render_camera->setMainViewMatrix(*swap_data.m_camera_swap_data->m_view_matrix);
            }

            if (swap_data.m_camera_swap_data->m_camera_type.has_value())
            {
                m_render_camera->setCurrentCameraType(*swap_data.m_camera_swap_data->m_camera_type);
            }

            m_swap_context.resetCameraSwapData();
        }

        if (swap_data.m_particle_submit_request.has_value())
        {
            std::shared_ptr<ParticlePass> particle_pass =
                std::static_pointer_cast<ParticlePass>(m_render_pipeline->m_particle_pass);

            int emitter_count = swap_data.m_particle_submit_request->getEmitterCount();
            particle_pass->setEmitterCount(emitter_count);

            for (int index = 0; index < emitter_count; ++index)
            {
                const ParticleEmitterDesc& desc = swap_data.m_particle_submit_request->getEmitterDesc(index);
                particle_pass->createEmitter(index, desc);
            }

            particle_pass->initializeEmitters();

            m_swap_context.resetPartilceBatchSwapData();
        }
        if (swap_data.m_emitter_tick_request.has_value())
        {
            std::static_pointer_cast<ParticlePass>(m_render_pipeline->m_particle_pass)
                ->setTickIndices(swap_data.m_emitter_tick_request->m_emitter_indices);
            m_swap_context.resetEmitterTickSwapData();
        }

        if (swap_data.m_emitter_transform_request.has_value())
        {
            std::static_pointer_cast<ParticlePass>(m_render_pipeline->m_particle_pass)
                ->setTransformIndices(swap_data.m_emitter_transform_request->m_transform_descs);
            m_swap_context.resetEmitterTransformSwapData();
        }
    }
} // namespace Piccolo