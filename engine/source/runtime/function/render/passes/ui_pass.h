#pragma once

#include "runtime/function/render/passes/ui_imgui_backend.h"
#include "runtime/function/render/render_pass.h"

#include <memory>

namespace Piccolo
{
    class WindowUI;

    struct UIPassInitInfo : RenderPassInitInfo
    {
        RHIRenderPass* render_pass;
    };

    class UIPass : public RenderPass
    {
    public:
        void initialize(const RenderPassInitInfo* init_info) override final;
        void initializeUIRenderBackend(WindowUI* window_ui) override final;
        void shutdownUIRenderBackend() override final;
        void draw() override final;

    private:
        WindowUI*                           m_window_ui {nullptr};
        std::unique_ptr<ImGuiRenderBackend> m_imgui_backend;
    };
} // namespace Piccolo
