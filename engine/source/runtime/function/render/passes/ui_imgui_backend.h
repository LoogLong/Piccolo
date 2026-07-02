#pragma once

#include <cstdint>
#include <memory>

namespace Piccolo
{
    class RHI;
    class RHIRenderPass;

    // UI-layer abstraction over the ImGui platform + renderer backend binding (ImGui_ImplGlfw +
    // ImGui_Impl{Vulkan,DX12}). Keeping this in the UI/render layer — rather than on the RHI — means
    // the RHI stays free of any ImGui dependency, while backend-specific ImGui code and the cast to the
    // concrete RHI are confined to the per-backend adapters below.
    //
    // Lifetime is RAII: constructing/initializing binds the backend, destruction shuts it down.
    class ImGuiRenderBackend
    {
    public:
        virtual ~ImGuiRenderBackend() = default;

        // Bind the ImGui platform + renderer backends. ui_render_pass / ui_subpass are used by backends
        // that need them (Vulkan); backends that do not (D3D12) ignore them. Returns false on failure.
        virtual bool initialize(RHIRenderPass* ui_render_pass, uint32_t ui_subpass) = 0;
        virtual void newFrame()                                                     = 0;
        virtual void renderDrawData()                                               = 0;
        virtual void uploadFonts()                                                  = 0;
    };

    // Creates the ImGui render backend matching the active RHI. Returns nullptr when the backend is not
    // compiled in or the RHI type is unknown. This factory is the only place that branches on the
    // concrete backend type for UI rendering.
    std::unique_ptr<ImGuiRenderBackend> createImGuiRenderBackend(const std::shared_ptr<RHI>& rhi);
} // namespace Piccolo
