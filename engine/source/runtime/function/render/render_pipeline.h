#pragma once

#include "runtime/function/render/render_pipeline_base.h"

namespace Piccolo
{
    class RenderPipeline : public RenderPipelineBase
    {
    public:
        void clear() override;

        virtual void initialize(RenderPipelineInitInfo init_info) override final;

        virtual void forwardRender(std::shared_ptr<RHI>                rhi,
                                   std::shared_ptr<RenderResourceBase> render_resource) override final;

        virtual void deferredRender(std::shared_ptr<RHI>                rhi,
                                    std::shared_ptr<RenderResourceBase> render_resource) override final;

        virtual bool pathTracingRender(std::shared_ptr<RHI>                rhi,
                                       std::shared_ptr<RenderResourceBase> render_resource) override final;

        void passUpdateAfterRecreateSwapchain();

        virtual uint32_t getGuidOfPickedMesh(const Vector2& picked_uv) override final;

        void setAxisVisibleState(bool state);

        void setSelectedAxis(size_t selected_axis);

    private:
        void updateSceneRenderMode(RHI& rhi);
        void logRasterPathOnce(const char* pipeline_name);

        bool                  m_path_tracing_frame_logged {false};
        bool                  m_path_tracing_device_lost_logged {false};
        bool                  m_raster_path_logged {false};
    };
} // namespace Piccolo