#include "runtime/function/render/passes/ui_imgui_backend.h"

#include "runtime/core/base/macro.h"
#include "runtime/function/render/interface/rhi.h"

#include <stdexcept>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>

#if PICCOLO_ENABLE_VULKAN_BACKEND
#include "runtime/function/render/interface/vulkan/vulkan_rhi.h"
#include <backends/imgui_impl_vulkan.h>
#endif

#if PICCOLO_ENABLE_D3D12_BACKEND && defined(_WIN32)
#include "runtime/function/render/interface/d3d12/d3d12_rhi.h"
#include <backends/imgui_impl_dx12.h>
#endif

namespace Piccolo
{
#if PICCOLO_ENABLE_VULKAN_BACKEND
    // ImGui binding for the Vulkan backend. Owns the ImGui GLFW + Vulkan backend lifetime.
    class VulkanImGuiBackend final : public ImGuiRenderBackend
    {
    public:
        explicit VulkanImGuiBackend(std::shared_ptr<VulkanRHI> rhi) : m_rhi(std::move(rhi)) {}

        ~VulkanImGuiBackend() override
        {
            if (m_initialized)
            {
                ImGui_ImplVulkan_Shutdown();
                ImGui_ImplGlfw_Shutdown();
            }
        }

        bool initialize(RHIRenderPass* ui_render_pass, uint32_t ui_subpass) override
        {
            if (m_rhi == nullptr || ui_render_pass == nullptr)
            {
                return false;
            }

            if (!ImGui_ImplGlfw_InitForVulkan(m_rhi->m_window, true))
            {
                LOG_WARN("Failed to initialize ImGui GLFW backend for Vulkan");
                return false;
            }

            ImGui_ImplVulkan_InitInfo init_info = {};
            init_info.Instance                  = m_rhi->m_instance;
            init_info.PhysicalDevice            = m_rhi->m_physical_device;
            init_info.Device                    = m_rhi->m_device;
            init_info.QueueFamily               = m_rhi->getQueueFamilyIndices().graphics_family.value();
            init_info.Queue                     = ((VulkanQueue*)m_rhi->getGraphicsQueue())->getResource();
            init_info.DescriptorPool            = m_rhi->m_vk_descriptor_pool;
            init_info.Subpass                   = ui_subpass;

            // may be different from the real swapchain image count
            // see ImGui_ImplVulkanH_GetMinImageCountFromPresentMode
            init_info.MinImageCount = 3;
            init_info.ImageCount    = 3;
            if (!ImGui_ImplVulkan_Init(&init_info, ((VulkanRenderPass*)ui_render_pass)->getResource()))
            {
                LOG_WARN("Failed to initialize ImGui Vulkan renderer backend");
                ImGui_ImplGlfw_Shutdown();
                return false;
            }
            m_initialized = true;
            return true;
        }

        void newFrame() override
        {
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
        }

        void renderDrawData() override
        {
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_rhi->m_vk_current_command_buffer);
        }

        void uploadFonts() override
        {
            RHICommandBufferAllocateInfo allocInfo = {};
            allocInfo.sType                        = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level                        = RHI_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool                  = m_rhi->getCommandPoor();
            allocInfo.commandBufferCount           = 1;

            RHICommandBuffer* commandBuffer = nullptr;
            if (RHI_SUCCESS != m_rhi->allocateCommandBuffers(&allocInfo, commandBuffer) || commandBuffer == nullptr)
            {
                throw std::runtime_error("failed to allocate command buffers!");
            }

            RHICommandBufferBeginInfo beginInfo = {};
            beginInfo.sType                     = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags                     = RHI_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            if (RHI_SUCCESS != m_rhi->beginCommandBuffer(commandBuffer, &beginInfo))
            {
                throw std::runtime_error("Could not create one-time command buffer!");
            }

            auto* vulkan_command_buffer = static_cast<VulkanCommandBuffer*>(commandBuffer);
            ImGui_ImplVulkan_CreateFontsTexture(vulkan_command_buffer->getResource());

            if (RHI_SUCCESS != m_rhi->endCommandBuffer(commandBuffer))
            {
                throw std::runtime_error("failed to record command buffer!");
            }

            RHISubmitInfo submitInfo {};
            submitInfo.sType              = RHI_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &commandBuffer;

            m_rhi->queueSubmit(m_rhi->getGraphicsQueue(), 1, &submitInfo, RHI_NULL_HANDLE);
            m_rhi->queueWaitIdle(m_rhi->getGraphicsQueue());

            m_rhi->freeCommandBuffers(m_rhi->getCommandPoor(), 1, commandBuffer);

            ImGui_ImplVulkan_DestroyFontUploadObjects();
        }

    private:
        std::shared_ptr<VulkanRHI> m_rhi;
        bool                       m_initialized {false};
    };
#endif // PICCOLO_ENABLE_VULKAN_BACKEND

#if PICCOLO_ENABLE_D3D12_BACKEND && defined(_WIN32)
    // ImGui binding for the D3D12 backend. Owns the ImGui GLFW + DX12 backend lifetime.
    class D3D12ImGuiBackend final : public ImGuiRenderBackend
    {
    public:
        explicit D3D12ImGuiBackend(std::shared_ptr<D3D12RHI> rhi) : m_rhi(std::move(rhi)) {}

        ~D3D12ImGuiBackend() override
        {
            if (m_initialized)
            {
                ImGui_ImplDX12_Shutdown();
                ImGui_ImplGlfw_Shutdown();
            }
        }

        bool initialize(RHIRenderPass* /*ui_render_pass*/, uint32_t /*ui_subpass*/) override
        {
            if (m_rhi == nullptr)
            {
                return false;
            }

            if (!ImGui_ImplGlfw_InitForOther(m_rhi->getWindow(), true))
            {
                LOG_WARN("Failed to initialize ImGui GLFW backend for D3D12");
                return false;
            }

            if (!ImGui_ImplDX12_Init(m_rhi->getD3D12Device(),
                                     m_rhi->getMaxFramesInFlight(),
                                     m_rhi->getD3D12UiRenderTargetFormat(),
                                     m_rhi->getD3D12ImGuiSrvHeap(),
                                     m_rhi->getD3D12ImGuiSrvCpuHandle(),
                                     m_rhi->getD3D12ImGuiSrvGpuHandle()))
            {
                LOG_WARN("Failed to initialize ImGui D3D12 renderer backend");
                ImGui_ImplGlfw_Shutdown();
                return false;
            }
            m_initialized = true;
            return true;
        }

        void newFrame() override
        {
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplGlfw_NewFrame();
        }

        void renderDrawData() override
        {
            ID3D12DescriptorHeap* descriptor_heaps[] = {m_rhi->getD3D12ImGuiSrvHeap()};
            m_rhi->getD3D12CommandList()->SetDescriptorHeaps(1, descriptor_heaps);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_rhi->getD3D12CommandList());
        }

        void uploadFonts() override
        {
            // DX12 backend uploads fonts lazily on first render; nothing to do here.
        }

    private:
        std::shared_ptr<D3D12RHI> m_rhi;
        bool                      m_initialized {false};
    };
#endif // PICCOLO_ENABLE_D3D12_BACKEND && _WIN32

    std::unique_ptr<ImGuiRenderBackend> createImGuiRenderBackend(const std::shared_ptr<RHI>& rhi)
    {
        if (rhi == nullptr)
        {
            return nullptr;
        }

        switch (rhi->getBackendType())
        {
            case RHIBackendType::Vulkan:
#if PICCOLO_ENABLE_VULKAN_BACKEND
                return std::make_unique<VulkanImGuiBackend>(std::static_pointer_cast<VulkanRHI>(rhi));
#else
                LOG_WARN("Vulkan UI backend is not compiled in this build");
                return nullptr;
#endif
            case RHIBackendType::D3D12:
#if PICCOLO_ENABLE_D3D12_BACKEND && defined(_WIN32)
                return std::make_unique<D3D12ImGuiBackend>(std::static_pointer_cast<D3D12RHI>(rhi));
#else
                LOG_WARN("D3D12 UI backend is not compiled in this build");
                return nullptr;
#endif
            default:
                LOG_WARN("Unsupported UI render backend; skip UI backend initialization");
                return nullptr;
        }
    }
} // namespace Piccolo
