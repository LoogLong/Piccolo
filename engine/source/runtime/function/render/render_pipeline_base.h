#pragma once

#include "runtime/core/math/vector2.h"
#include "runtime/function/render/render_pass_base.h"

#include <memory>
#include <vector>

namespace Piccolo
{
    class RHI;
    class RenderResourceBase;
    class WindowUI;

    enum class RenderSceneRenderMode : uint8_t
    {
        Raster = 0,
        PathTracing
    };

    struct RenderPipelineInitInfo
    {
        bool                                enable_fxaa {false};
        std::shared_ptr<RenderResourceBase> render_resource;
    };

    class RenderPipelineBase
    {
        friend class RenderSystem;

    public:
        virtual ~RenderPipelineBase() {}

        virtual void clear() {};

        virtual void initialize(RenderPipelineInitInfo init_info) = 0;

        virtual void preparePassData(std::shared_ptr<RenderResourceBase> render_resource);
        virtual void forwardRender(std::shared_ptr<RHI> rhi, std::shared_ptr<RenderResourceBase> render_resource);
        virtual void deferredRender(std::shared_ptr<RHI> rhi, std::shared_ptr<RenderResourceBase> render_resource);
        virtual bool pathTracingRender(std::shared_ptr<RHI> rhi, std::shared_ptr<RenderResourceBase> render_resource);

        void             initializeUIRenderBackend(WindowUI* window_ui);
        void             shutdownUIRenderBackend();
        virtual uint32_t getGuidOfPickedMesh(const Vector2& picked_uv) = 0;
        RenderSceneRenderMode getRequestedSceneRenderMode() const { return m_requested_scene_render_mode; }
        RenderSceneRenderMode getEffectiveSceneRenderMode() const { return m_effective_scene_render_mode; }
        bool                  isPathTracingSceneModeActive() const
        {
            return m_effective_scene_render_mode == RenderSceneRenderMode::PathTracing;
        }
        bool hasPathTracingRuntimeFallback() const { return m_path_tracing_runtime_fallback; }

    protected:
        std::shared_ptr<RHI> m_rhi;
        RenderSceneRenderMode m_requested_scene_render_mode {RenderSceneRenderMode::Raster};
        RenderSceneRenderMode m_effective_scene_render_mode {RenderSceneRenderMode::Raster};
        bool                  m_scene_render_mode_fallback_logged {false};
        bool                  m_path_tracing_runtime_fallback {false};
        bool                  m_path_tracing_dispatch_fail_logged {false};

        std::shared_ptr<RenderPassBase> m_gpu_skinning_pass;
        std::shared_ptr<RenderPassBase> m_directional_light_pass;
        std::shared_ptr<RenderPassBase> m_point_light_shadow_pass;
        std::shared_ptr<RenderPassBase> m_main_camera_pass;
        std::shared_ptr<RenderPassBase> m_color_grading_pass;
        std::shared_ptr<RenderPassBase> m_fxaa_pass;
        std::shared_ptr<RenderPassBase> m_tone_mapping_pass;
        std::shared_ptr<RenderPassBase> m_ui_pass;
        std::shared_ptr<RenderPassBase> m_combine_ui_pass;
        std::shared_ptr<RenderPassBase> m_pick_pass;
        std::shared_ptr<RenderPassBase> m_particle_pass;
        std::shared_ptr<RenderPassBase> m_path_tracing_pass;

    };
} // namespace Piccolo