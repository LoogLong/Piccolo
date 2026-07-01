#include "runtime/function/render/passes/ui_pass.h"

#include "runtime/core/base/macro.h"

#include "runtime/function/ui/window_ui.h"

#include <imgui.h>

namespace Piccolo
{
    void UIPass::initialize(const RenderPassInitInfo* init_info)
    {
        RenderPass::initialize(nullptr);

        m_framebuffer.render_pass = static_cast<const UIPassInitInfo*>(init_info)->render_pass;
    }

    void UIPass::initializeUIRenderBackend(WindowUI* window_ui)
    {
        shutdownUIRenderBackend();

        // The concrete ImGui platform + renderer backend binding lives inside the active RHI backend,
        // so UIPass does not need to know whether it is running on Vulkan or D3D12.
        if (m_rhi->initializeImGuiRenderBackend(m_framebuffer.render_pass, _main_camera_subpass_ui))
        {
            m_renderer_backend_initialized = true;
            m_window_ui                    = window_ui;
            m_rhi->uploadImGuiFonts();
        }
        else
        {
            LOG_WARN("Failed to initialize the ImGui render backend for the active RHI");
        }
    }

    void UIPass::shutdownUIRenderBackend()
    {
        if (m_renderer_backend_initialized)
        {
            m_rhi->shutdownImGuiRenderBackend();
            m_renderer_backend_initialized = false;
        }
        m_window_ui = nullptr;
    }

    void UIPass::draw()
    {
        if (m_window_ui == nullptr)
        {
            return;
        }

        m_rhi->newFrameImGui();
        ImGui::NewFrame();

        m_window_ui->preRender();

        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "ImGUI", color);

        ImGui::Render();
        m_rhi->renderImGuiDrawData();

        m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
    }
} // namespace Piccolo
