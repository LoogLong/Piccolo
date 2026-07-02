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

        // The concrete ImGui platform + renderer backend binding lives in a UI-layer adapter, so UIPass
        // does not need to know whether it is running on Vulkan or D3D12, and the RHI stays ImGui-free.
        m_imgui_backend = createImGuiRenderBackend(m_rhi);
        if (m_imgui_backend != nullptr &&
            m_imgui_backend->initialize(m_framebuffer.render_pass, _main_camera_subpass_ui))
        {
            m_imgui_backend->uploadFonts();
            m_window_ui = window_ui;
        }
        else
        {
            m_imgui_backend.reset();
            LOG_WARN("Failed to initialize the ImGui render backend for the active RHI");
        }
    }

    void UIPass::shutdownUIRenderBackend()
    {
        // RAII: destroying the adapter shuts down its ImGui platform + renderer backends.
        m_imgui_backend.reset();
        m_window_ui = nullptr;
    }

    void UIPass::draw()
    {
        if (m_window_ui == nullptr || m_imgui_backend == nullptr)
        {
            return;
        }

        m_imgui_backend->newFrame();
        ImGui::NewFrame();

        m_window_ui->preRender();

        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "ImGUI", color);

        ImGui::Render();
        m_imgui_backend->renderDrawData();

        m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
    }
} // namespace Piccolo
