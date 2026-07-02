#include "runtime/function/render/render_pipeline.h"
#include "runtime/function/render/render_shader_bytecode.h"
#include "runtime/function/render/passes/color_grading_pass.h"
#include "runtime/function/render/passes/combine_ui_pass.h"
#include "runtime/function/render/passes/directional_light_pass.h"
#include "runtime/function/render/passes/main_camera_pass.h"
#include "runtime/function/render/passes/path_tracing_pass.h"
#include "runtime/function/render/passes/pick_pass.h"
#include "runtime/function/render/passes/point_light_pass.h"
#include "runtime/function/render/passes/tone_mapping_pass.h"
#include "runtime/function/render/passes/ui_pass.h"
#include "runtime/function/render/passes/gpu_skinning_pass.h"
#include "runtime/function/render/passes/particle_pass.h"

#include "runtime/function/render/debugdraw/debug_draw_manager.h"

#include "runtime/core/base/macro.h"
#include "runtime/function/global/global_context.h"
#include "runtime/resource/config_manager/config_manager.h"

namespace Piccolo
{
    namespace
    {
        RenderSceneRenderMode parseRenderSceneMode(const std::string& mode)
        {
            if (mode == "PathTracing")
            {
                return RenderSceneRenderMode::PathTracing;
            }
            if (mode != "Raster")
            {
                LOG_WARN("Unknown RenderSceneMode '{}'; falling back to Raster", mode);
            }
            return RenderSceneRenderMode::Raster;
        }
    } // namespace

    void RenderPipeline::initialize(RenderPipelineInitInfo init_info)
    {
        if (g_runtime_global_context.m_config_manager)
        {
            m_requested_scene_render_mode =
                parseRenderSceneMode(g_runtime_global_context.m_config_manager->getRenderSceneMode());
        }

        m_point_light_shadow_pass = std::make_shared<PointLightShadowPass>();
        m_directional_light_pass  = std::make_shared<DirectionalLightShadowPass>();
        m_main_camera_pass        = std::make_shared<MainCameraPass>();
        m_tone_mapping_pass       = std::make_shared<ToneMappingPass>();
        m_color_grading_pass      = std::make_shared<ColorGradingPass>();
        m_ui_pass                 = std::make_shared<UIPass>();
        m_combine_ui_pass         = std::make_shared<CombineUIPass>();
        m_pick_pass               = std::make_shared<PickPass>();
        m_fxaa_pass               = std::make_shared<FXAAPass>();
        m_particle_pass           = std::make_shared<ParticlePass>();
        m_path_tracing_pass       = std::make_shared<PathTracingPass>();
        m_gpu_skinning_pass       = std::make_shared<GpuSkinningPass>();

        RenderPassCommonInfo pass_common_info;
        pass_common_info.rhi             = m_rhi;
        pass_common_info.render_resource = init_info.render_resource;

        m_point_light_shadow_pass->setCommonInfo(pass_common_info);
        m_directional_light_pass->setCommonInfo(pass_common_info);
        m_main_camera_pass->setCommonInfo(pass_common_info);
        m_tone_mapping_pass->setCommonInfo(pass_common_info);
        m_color_grading_pass->setCommonInfo(pass_common_info);
        m_ui_pass->setCommonInfo(pass_common_info);
        m_combine_ui_pass->setCommonInfo(pass_common_info);
        m_pick_pass->setCommonInfo(pass_common_info);
        m_fxaa_pass->setCommonInfo(pass_common_info);
        m_particle_pass->setCommonInfo(pass_common_info);
        m_path_tracing_pass->setCommonInfo(pass_common_info);
        m_gpu_skinning_pass->setCommonInfo(pass_common_info);

        m_point_light_shadow_pass->initialize(nullptr);
        m_directional_light_pass->initialize(nullptr);

        std::shared_ptr<MainCameraPass> main_camera_pass = std::static_pointer_cast<MainCameraPass>(m_main_camera_pass);
        std::shared_ptr<RenderPass>     _main_camera_pass = std::static_pointer_cast<RenderPass>(m_main_camera_pass);
        std::shared_ptr<ParticlePass> particle_pass = std::static_pointer_cast<ParticlePass>(m_particle_pass);

        ParticlePassInitInfo particle_init_info{};
        particle_init_info.m_particle_manager = g_runtime_global_context.m_particle_manager;
        m_particle_pass->initialize(&particle_init_info);

        main_camera_pass->m_point_light_shadow_color_image_view =
            std::static_pointer_cast<RenderPass>(m_point_light_shadow_pass)->getFramebufferImageViews()[0];
        main_camera_pass->m_directional_light_shadow_color_image_view =
            std::static_pointer_cast<RenderPass>(m_directional_light_pass)->m_framebuffer.attachments[0].view;

        MainCameraPassInitInfo main_camera_init_info;
        main_camera_init_info.enble_fxaa = init_info.enable_fxaa;
        main_camera_pass->setParticlePass(particle_pass);
        m_main_camera_pass->initialize(&main_camera_init_info);

        PathTracingPassInitInfo path_tracing_init_info {};
        path_tracing_init_info.scene_output_image      = main_camera_pass->getBackupOddImage();
        path_tracing_init_info.scene_output_image_view = main_camera_pass->getBackupOddImageView();
        m_path_tracing_pass->initialize(&path_tracing_init_info);

        m_gpu_skinning_pass->initialize(nullptr);
        std::static_pointer_cast<GpuSkinningPass>(m_gpu_skinning_pass)->setup();

        std::static_pointer_cast<ParticlePass>(m_particle_pass)->setupParticlePass();

        std::vector<RHIDescriptorSetLayout*> descriptor_layouts = _main_camera_pass->getDescriptorSetLayouts();
        std::static_pointer_cast<PointLightShadowPass>(m_point_light_shadow_pass)
            ->setPerMeshLayout(descriptor_layouts[MainCameraPass::LayoutType::_per_mesh]);
        std::static_pointer_cast<DirectionalLightShadowPass>(m_directional_light_pass)
            ->setPerMeshLayout(descriptor_layouts[MainCameraPass::LayoutType::_per_mesh]);

        m_point_light_shadow_pass->postInitialize();
        m_directional_light_pass->postInitialize();

        ToneMappingPassInitInfo tone_mapping_init_info;
        tone_mapping_init_info.render_pass = _main_camera_pass->getRenderPass();
        tone_mapping_init_info.input_attachment =
            _main_camera_pass->getFramebufferImageViews()[_main_camera_pass_backup_buffer_odd];
        m_tone_mapping_pass->initialize(&tone_mapping_init_info);

        ColorGradingPassInitInfo color_grading_init_info;
        color_grading_init_info.render_pass = _main_camera_pass->getRenderPass();
        color_grading_init_info.input_attachment =
            _main_camera_pass->getFramebufferImageViews()[_main_camera_pass_backup_buffer_even];
        m_color_grading_pass->initialize(&color_grading_init_info);

        UIPassInitInfo ui_init_info;
        ui_init_info.render_pass = _main_camera_pass->getRenderPass();
        m_ui_pass->initialize(&ui_init_info);

        CombineUIPassInitInfo combine_ui_init_info;
        combine_ui_init_info.render_pass = _main_camera_pass->getRenderPass();
        combine_ui_init_info.scene_input_attachment =
            _main_camera_pass->getFramebufferImageViews()[_main_camera_pass_backup_buffer_odd];
        combine_ui_init_info.ui_input_attachment =
            _main_camera_pass->getFramebufferImageViews()[_main_camera_pass_backup_buffer_even];
        m_combine_ui_pass->initialize(&combine_ui_init_info);

        PickPassInitInfo pick_init_info;
        pick_init_info.per_mesh_layout = descriptor_layouts[MainCameraPass::LayoutType::_per_mesh];
        m_pick_pass->initialize(&pick_init_info);

        FXAAPassInitInfo fxaa_init_info;
        fxaa_init_info.render_pass = _main_camera_pass->getRenderPass();
        fxaa_init_info.input_attachment =
            _main_camera_pass->getFramebufferImageViews()[_main_camera_pass_post_process_buffer_odd];
        m_fxaa_pass->initialize(&fxaa_init_info);

    }

    void RenderPipeline::forwardRender(std::shared_ptr<RHI> rhi, std::shared_ptr<RenderResourceBase> render_resource)
    {
        RHI*            render_rhi      = rhi.get();
        RenderResource* render_resource_impl = static_cast<RenderResource*>(render_resource.get());

        render_resource_impl->resetRingBufferOffset(render_rhi->getCurrentFrameIndex());

        render_rhi->waitForFences();

        render_rhi->resetCommandPool();

        bool recreate_swapchain =
            render_rhi->prepareBeforePass(std::bind(&RenderPipeline::passUpdateAfterRecreateSwapchain, this));
        if (recreate_swapchain)
        {
            return;
        }

        static_cast<DirectionalLightShadowPass*>(m_directional_light_pass.get())->draw();

        static_cast<PointLightShadowPass*>(m_point_light_shadow_pass.get())->draw();

        ColorGradingPass& color_grading_pass = *(static_cast<ColorGradingPass*>(m_color_grading_pass.get()));
        FXAAPass&         fxaa_pass          = *(static_cast<FXAAPass*>(m_fxaa_pass.get()));
        ToneMappingPass&  tone_mapping_pass  = *(static_cast<ToneMappingPass*>(m_tone_mapping_pass.get()));
        UIPass&           ui_pass            = *(static_cast<UIPass*>(m_ui_pass.get()));
        CombineUIPass&    combine_ui_pass    = *(static_cast<CombineUIPass*>(m_combine_ui_pass.get()));
        ParticlePass&     particle_pass      = *(static_cast<ParticlePass*>(m_particle_pass.get()));

        const uint32_t current_swapchain_image_index = render_rhi->getCurrentSwapchainImageIndex();

        static_cast<ParticlePass*>(m_particle_pass.get())
            ->setRenderCommandBufferHandle(
                static_cast<MainCameraPass*>(m_main_camera_pass.get())->getRenderCommandBuffer());

        static_cast<MainCameraPass*>(m_main_camera_pass.get())
            ->drawForward(color_grading_pass,
                          fxaa_pass,
                          tone_mapping_pass,
                          ui_pass,
                          combine_ui_pass,
                          particle_pass,
                          current_swapchain_image_index);

        
        g_runtime_global_context.m_debugdraw_manager->draw(current_swapchain_image_index);

        if (render_rhi->requiresDepthNormalCopyBeforeSubmit())
        {
            static_cast<ParticlePass*>(m_particle_pass.get())->copyNormalAndDepthImage();
        }

        render_rhi->submitRendering(std::bind(&RenderPipeline::passUpdateAfterRecreateSwapchain, this));
        if (!render_rhi->requiresDepthNormalCopyBeforeSubmit())
        {
            static_cast<ParticlePass*>(m_particle_pass.get())->copyNormalAndDepthImage();
        }
        static_cast<ParticlePass*>(m_particle_pass.get())->simulate();
    }

    void RenderPipeline::deferredRender(std::shared_ptr<RHI> rhi, std::shared_ptr<RenderResourceBase> render_resource)
    {
        RHI*            render_rhi      = rhi.get();
        RenderResource* render_resource_impl = static_cast<RenderResource*>(render_resource.get());

        render_resource_impl->resetRingBufferOffset(render_rhi->getCurrentFrameIndex());

        render_rhi->waitForFences();

        render_rhi->resetCommandPool();

        bool recreate_swapchain =
            render_rhi->prepareBeforePass(std::bind(&RenderPipeline::passUpdateAfterRecreateSwapchain, this));
        if (recreate_swapchain)
        {
            return;
        }

        static_cast<DirectionalLightShadowPass*>(m_directional_light_pass.get())->draw();

        static_cast<PointLightShadowPass*>(m_point_light_shadow_pass.get())->draw();

        ColorGradingPass& color_grading_pass = *(static_cast<ColorGradingPass*>(m_color_grading_pass.get()));
        FXAAPass&         fxaa_pass          = *(static_cast<FXAAPass*>(m_fxaa_pass.get()));
        ToneMappingPass&  tone_mapping_pass  = *(static_cast<ToneMappingPass*>(m_tone_mapping_pass.get()));
        UIPass&           ui_pass            = *(static_cast<UIPass*>(m_ui_pass.get()));
        CombineUIPass&    combine_ui_pass    = *(static_cast<CombineUIPass*>(m_combine_ui_pass.get()));
        ParticlePass&     particle_pass      = *(static_cast<ParticlePass*>(m_particle_pass.get()));

        const uint32_t current_swapchain_image_index = render_rhi->getCurrentSwapchainImageIndex();

        static_cast<ParticlePass*>(m_particle_pass.get())
            ->setRenderCommandBufferHandle(
                static_cast<MainCameraPass*>(m_main_camera_pass.get())->getRenderCommandBuffer());

        static_cast<MainCameraPass*>(m_main_camera_pass.get())
            ->draw(color_grading_pass,
                   fxaa_pass,
                   tone_mapping_pass,
                   ui_pass,
                   combine_ui_pass,
                   particle_pass,
                   current_swapchain_image_index);
                   
        g_runtime_global_context.m_debugdraw_manager->draw(current_swapchain_image_index);

        if (render_rhi->requiresDepthNormalCopyBeforeSubmit())
        {
            static_cast<ParticlePass*>(m_particle_pass.get())->copyNormalAndDepthImage();
        }

        render_rhi->submitRendering(std::bind(&RenderPipeline::passUpdateAfterRecreateSwapchain, this));
        if (!render_rhi->requiresDepthNormalCopyBeforeSubmit())
        {
            static_cast<ParticlePass*>(m_particle_pass.get())->copyNormalAndDepthImage();
        }
        static_cast<ParticlePass*>(m_particle_pass.get())->simulate();
    }

    void RenderPipeline::passUpdateAfterRecreateSwapchain()
    {
        m_path_tracing_runtime_fallback      = false;
        m_path_tracing_dispatch_fail_logged  = false;
        m_scene_render_mode_fallback_logged  = false;

        MainCameraPass&   main_camera_pass   = *(static_cast<MainCameraPass*>(m_main_camera_pass.get()));
        ColorGradingPass& color_grading_pass = *(static_cast<ColorGradingPass*>(m_color_grading_pass.get()));
        FXAAPass&         fxaa_pass          = *(static_cast<FXAAPass*>(m_fxaa_pass.get()));
        ToneMappingPass&  tone_mapping_pass  = *(static_cast<ToneMappingPass*>(m_tone_mapping_pass.get()));
        CombineUIPass&    combine_ui_pass    = *(static_cast<CombineUIPass*>(m_combine_ui_pass.get()));
        PickPass&         pick_pass          = *(static_cast<PickPass*>(m_pick_pass.get()));
        ParticlePass&     particle_pass      = *(static_cast<ParticlePass*>(m_particle_pass.get()));
        PathTracingPass&  path_tracing_pass  = *(static_cast<PathTracingPass*>(m_path_tracing_pass.get()));

        main_camera_pass.updateAfterFramebufferRecreate();
        tone_mapping_pass.updateAfterFramebufferRecreate(
            main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_odd]);
        color_grading_pass.updateAfterFramebufferRecreate(
            main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_even]);
        fxaa_pass.updateAfterFramebufferRecreate(
            main_camera_pass.getFramebufferImageViews()[_main_camera_pass_post_process_buffer_odd]);
        combine_ui_pass.updateAfterFramebufferRecreate(
            main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_odd],
            main_camera_pass.getFramebufferImageViews()[_main_camera_pass_backup_buffer_even]);
        path_tracing_pass.updateAfterFramebufferRecreate(main_camera_pass.getBackupOddImage(),
                                                         main_camera_pass.getBackupOddImageView());
        pick_pass.recreateFramebuffer();
        particle_pass.updateAfterFramebufferRecreate();
        g_runtime_global_context.m_debugdraw_manager->updateAfterRecreateSwapchain();
    }
    uint32_t RenderPipeline::getGuidOfPickedMesh(const Vector2& picked_uv)
    {
        PickPass& pick_pass = *(static_cast<PickPass*>(m_pick_pass.get()));
        return pick_pass.pick(picked_uv);
    }

    void RenderPipeline::setAxisVisibleState(bool state)
    {
        MainCameraPass& main_camera_pass = *(static_cast<MainCameraPass*>(m_main_camera_pass.get()));
        main_camera_pass.m_is_show_axis  = state;
    }

    void RenderPipeline::setSelectedAxis(size_t selected_axis)
    {
        MainCameraPass& main_camera_pass = *(static_cast<MainCameraPass*>(m_main_camera_pass.get()));
        main_camera_pass.m_selected_axis = selected_axis;
    }

    bool RenderPipeline::pathTracingRender(std::shared_ptr<RHI> rhi, std::shared_ptr<RenderResourceBase> render_resource)
    {
        RHI*            render_rhi          = rhi.get();
        RenderResource* render_resource_impl = static_cast<RenderResource*>(render_resource.get());

        render_resource_impl->resetRingBufferOffset(render_rhi->getCurrentFrameIndex());
        updateSceneRenderMode(*render_rhi);

        if (m_effective_scene_render_mode != RenderSceneRenderMode::PathTracing)
        {
            return false;
        }

        render_rhi->waitForFences();
        render_rhi->resetCommandPool();

        bool recreate_swapchain =
            render_rhi->prepareBeforePass(std::bind(&RenderPipeline::passUpdateAfterRecreateSwapchain, this));
        if (recreate_swapchain)
        {
            return true;
        }

        static_cast<GpuSkinningPass*>(m_gpu_skinning_pass.get())->dispatch();

        PathTracingPass& path_tracing_pass = *(static_cast<PathTracingPass*>(m_path_tracing_pass.get()));
        if (!path_tracing_pass.dispatch())
        {
            if (!m_path_tracing_dispatch_fail_logged)
            {
                LOG_WARN("Path tracing dispatch failed; skipping raster fallback for this frame and disabling path "
                         "tracing until the next swapchain recreate or scene mode change.");
                m_path_tracing_dispatch_fail_logged = true;
            }
            m_path_tracing_runtime_fallback = true;
            return false;
        }

        ParticlePass&    particle_pass   = *(static_cast<ParticlePass*>(m_particle_pass.get()));
        UIPass&           ui_pass         = *(static_cast<UIPass*>(m_ui_pass.get()));
        CombineUIPass&    combine_ui_pass = *(static_cast<CombineUIPass*>(m_combine_ui_pass.get()));

        const uint32_t current_swapchain_image_index = render_rhi->getCurrentSwapchainImageIndex();
        static_cast<MainCameraPass*>(m_main_camera_pass.get())
            ->drawPathTracing(particle_pass,
                              ui_pass,
                              combine_ui_pass,
                              current_swapchain_image_index);

        g_runtime_global_context.m_debugdraw_manager->draw(current_swapchain_image_index);
        render_rhi->submitRendering(std::bind(&RenderPipeline::passUpdateAfterRecreateSwapchain, this));
        static_cast<ParticlePass*>(m_particle_pass.get())->simulate();
        return true;
    }

    void RenderPipeline::updateSceneRenderMode(RHI& rhi)
    {
        if (m_requested_scene_render_mode != RenderSceneRenderMode::PathTracing)
        {
            m_effective_scene_render_mode     = RenderSceneRenderMode::Raster;
            m_path_tracing_runtime_fallback   = false;
            m_path_tracing_dispatch_fail_logged = false;
            return;
        }

        if (m_path_tracing_runtime_fallback)
        {
            m_effective_scene_render_mode = RenderSceneRenderMode::Raster;
            return;
        }

        if (supportsPathTracing(rhi))
        {
            m_effective_scene_render_mode = RenderSceneRenderMode::PathTracing;
            return;
        }

        if (!m_scene_render_mode_fallback_logged)
        {
            if (!rhi.supportsRayTracing())
            {
                LOG_WARN("RenderSceneMode=PathTracing requested but ray tracing is unavailable on the '{}' backend; "
                         "falling back to Raster.",
                         rhi.getBackendType() == RHIBackendType::D3D12 ? "D3D12" : "Vulkan");
            }
            else
            {
                LOG_WARN("RenderSceneMode=PathTracing requested but path tracing shader bytecode is missing for the "
                         "'{}' backend (build dxc output if required); falling back to Raster.",
                         rhi.getBackendType() == RHIBackendType::D3D12 ? "D3D12" : "Vulkan");
            }
            m_scene_render_mode_fallback_logged = true;
        }

        m_effective_scene_render_mode = RenderSceneRenderMode::Raster;
    }
} // namespace Piccolo