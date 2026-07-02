#pragma once

#include "runtime/function/render/render_pass.h"

#include <vector>

namespace Piccolo
{
    class RHI;

    enum class MainCameraRenderPassKind : uint8_t
    {
        Raster,
        PathTracingComposite,
    };

    void buildMainCameraRenderPass(RHI*                                rhi,
                                   const RenderPass::Framebuffer&      framebuffer,
                                   bool                                enable_fxaa,
                                   MainCameraRenderPassKind            kind,
                                   RHIRenderPass*&                     out_render_pass);

    void buildMainCameraSwapchainFramebuffers(RHI*                           rhi,
                                              const RenderPass::Framebuffer& framebuffer,
                                              RHIRenderPass*                 render_pass,
                                              std::vector<RHIFramebuffer*>&  out_framebuffers);
} // namespace Piccolo
