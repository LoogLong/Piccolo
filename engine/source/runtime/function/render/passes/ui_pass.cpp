#include "runtime/function/render/passes/ui_pass.h"

#if PICCOLO_ENABLE_VULKAN_BACKEND
#include "runtime/function/render/interface/vulkan/vulkan_rhi.h"
#include <backends/imgui_impl_vulkan.h>
#endif

#if PICCOLO_ENABLE_D3D12_BACKEND && defined(_WIN32)
#include "runtime/function/render/interface/d3d12/d3d12_rhi.h"
#include <backends/imgui_impl_dx12.h>
#endif

#include "runtime/core/base/macro.h"

#include "runtime/resource/config_manager/config_manager.h"

#include "runtime/function/ui/window_ui.h"

#include <backends/imgui_impl_glfw.h>

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

        switch (m_rhi->getBackendType())
        {
            case RHIBackendType::Vulkan:
            {
#if PICCOLO_ENABLE_VULKAN_BACKEND
                if (!ImGui_ImplGlfw_InitForVulkan(std::static_pointer_cast<VulkanRHI>(m_rhi)->m_window, true))
                {
                    LOG_WARN("Failed to initialize ImGui GLFW backend for Vulkan");
                    break;
                }
                m_platform_backend_initialized = true;

                ImGui_ImplVulkan_InitInfo init_info = {};
                init_info.Instance                  = std::static_pointer_cast<VulkanRHI>(m_rhi)->m_instance;
                init_info.PhysicalDevice            = std::static_pointer_cast<VulkanRHI>(m_rhi)->m_physical_device;
                init_info.Device                    = std::static_pointer_cast<VulkanRHI>(m_rhi)->m_device;
                init_info.QueueFamily               = m_rhi->getQueueFamilyIndices().graphics_family.value();
                init_info.Queue                     = ((VulkanQueue*)m_rhi->getGraphicsQueue())->getResource();
                init_info.DescriptorPool            = std::static_pointer_cast<VulkanRHI>(m_rhi)->m_vk_descriptor_pool;
                init_info.Subpass                   = _main_camera_subpass_ui;

                // may be different from the real swapchain image count
                // see ImGui_ImplVulkanH_GetMinImageCountFromPresentMode
                init_info.MinImageCount = 3;
                init_info.ImageCount    = 3;
                if (!ImGui_ImplVulkan_Init(&init_info, ((VulkanRenderPass*)m_framebuffer.render_pass)->getResource()))
                {
                    LOG_WARN("Failed to initialize ImGui Vulkan renderer backend");
                    shutdownUIRenderBackend();
                    break;
                }
                m_renderer_backend_initialized = true;
                m_initialized_backend          = RHIBackendType::Vulkan;
                m_window_ui                    = window_ui;

                uploadFonts();
#else
                LOG_WARN("Vulkan UI backend is not compiled in this build");
#endif
                break;
            }
            case RHIBackendType::D3D12:
            {
#if PICCOLO_ENABLE_D3D12_BACKEND && defined(_WIN32)
                auto d3d12_rhi = std::static_pointer_cast<D3D12RHI>(m_rhi);
                if (!ImGui_ImplGlfw_InitForOther(d3d12_rhi->getWindow(), true))
                {
                    LOG_WARN("Failed to initialize ImGui GLFW backend for D3D12");
                    break;
                }
                m_platform_backend_initialized = true;

                if (!ImGui_ImplDX12_Init(d3d12_rhi->getD3D12Device(),
                                         d3d12_rhi->getMaxFramesInFlight(),
                                         d3d12_rhi->getD3D12SwapchainFormat(),
                                         d3d12_rhi->getD3D12ImGuiSrvHeap(),
                                         d3d12_rhi->getD3D12ImGuiSrvCpuHandle(),
                                         d3d12_rhi->getD3D12ImGuiSrvGpuHandle()))
                {
                    LOG_WARN("Failed to initialize ImGui D3D12 renderer backend");
                    shutdownUIRenderBackend();
                    break;
                }
                m_renderer_backend_initialized = true;
                m_initialized_backend          = RHIBackendType::D3D12;
                m_window_ui                    = window_ui;
#else
                LOG_WARN("D3D12 UI backend is not compiled in this build");
#endif
                break;
            }
            default:
                LOG_WARN("Unsupported UI render backend; skip UI backend initialization");
                break;
        }
    }

    void UIPass::shutdownUIRenderBackend()
    {
        if (m_renderer_backend_initialized)
        {
            switch (m_initialized_backend)
            {
                case RHIBackendType::Vulkan:
#if PICCOLO_ENABLE_VULKAN_BACKEND
                    ImGui_ImplVulkan_Shutdown();
#endif
                    break;
                case RHIBackendType::D3D12:
#if PICCOLO_ENABLE_D3D12_BACKEND && defined(_WIN32)
                    ImGui_ImplDX12_Shutdown();
#endif
                    break;
                default:
                    break;
            }
            m_renderer_backend_initialized = false;
        }

        if (m_platform_backend_initialized)
        {
            ImGui_ImplGlfw_Shutdown();
            m_platform_backend_initialized = false;
        }

        m_initialized_backend = RHIBackendType::Auto;
        m_window_ui           = nullptr;
    }

#if PICCOLO_ENABLE_VULKAN_BACKEND
    void UIPass::uploadFonts()
    {
        switch (m_rhi->getBackendType())
        {
            case RHIBackendType::Vulkan:
                break;
            case RHIBackendType::D3D12:
            default:
                return;
        }

        RHICommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType                       = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level                       = RHI_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool                 = m_rhi->getCommandPoor();
        allocInfo.commandBufferCount          = 1;

        RHICommandBuffer* commandBuffer = nullptr;
        if (RHI_SUCCESS != m_rhi->allocateCommandBuffers(&allocInfo, commandBuffer) || commandBuffer == nullptr)
        {
            throw std::runtime_error("failed to allocate command buffers!");
        }

        RHICommandBufferBeginInfo beginInfo = {};
        beginInfo.sType                    = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags                    = RHI_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

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
#else
    void UIPass::uploadFonts()
    {
    }
#endif

    void UIPass::draw()
    {
        if (m_window_ui)
        {
            switch (m_rhi->getBackendType())
            {
                case RHIBackendType::Vulkan:
                {
#if PICCOLO_ENABLE_VULKAN_BACKEND
                    ImGui_ImplVulkan_NewFrame();
                    ImGui_ImplGlfw_NewFrame();
                    ImGui::NewFrame();

                    m_window_ui->preRender();

                    float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "ImGUI", color);

                    ImGui::Render();

                    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), std::static_pointer_cast<VulkanRHI>(m_rhi)->m_vk_current_command_buffer);

                    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
#else
                    LOG_WARN("Vulkan UI backend is not compiled in this build");
#endif
                    break;
                }
                case RHIBackendType::D3D12:
                {
#if PICCOLO_ENABLE_D3D12_BACKEND && defined(_WIN32)
                    auto d3d12_rhi = std::static_pointer_cast<D3D12RHI>(m_rhi);

                    ImGui_ImplDX12_NewFrame();
                    ImGui_ImplGlfw_NewFrame();
                    ImGui::NewFrame();

                    m_window_ui->preRender();

                    float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "ImGUI", color);

                    ImGui::Render();

                    ID3D12DescriptorHeap* descriptor_heaps[] = {d3d12_rhi->getD3D12ImGuiSrvHeap()};
                    d3d12_rhi->getD3D12CommandList()->SetDescriptorHeaps(1, descriptor_heaps);
                    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), d3d12_rhi->getD3D12CommandList());

                    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
                    break;
#else
                    LOG_WARN("D3D12 UI backend is not compiled in this build");
                    return;
#endif
                }
                default:
                    return;
            }
        }
    }
} // namespace Piccolo
