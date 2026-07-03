#include "runtime/function/render/render_pass.h"

#include "runtime/core/base/macro.h"

#include "runtime/function/render/render_resource.h"

Piccolo::VisiableNodes Piccolo::RenderPass::m_visiable_nodes;

namespace Piccolo
{
    void RenderPass::initialize(const RenderPassInitInfo* init_info)
    {
        m_global_render_resource =
            &(std::static_pointer_cast<RenderResource>(m_render_resource)->m_global_render_resource);
    }
    void RenderPass::draw() {}

    void RenderPass::postInitialize() {}

    RHIRenderPass* RenderPass::getRenderPass() const { return m_framebuffer.render_pass; }

    std::vector<RHIImageView*> RenderPass::getFramebufferImageViews() const
    {
        std::vector<RHIImageView*> image_views;
        for (auto& attach : m_framebuffer.attachments)
        {
            image_views.push_back(attach.view);
        }
        return image_views;
    }

    std::vector<RHIDescriptorSetLayout*> RenderPass::getDescriptorSetLayouts() const
    {
        std::vector<RHIDescriptorSetLayout*> layouts;
        for (auto& desc : m_descriptor_infos)
        {
            layouts.push_back(desc.layout);
        }
        return layouts;
    }

    void RenderPass::teardownCommonResources(bool destroy_render_pass)
    {
        if (m_rhi == nullptr)
        {
            return;
        }

        if (m_framebuffer.framebuffer != nullptr)
        {
            m_rhi->destroyFramebuffer(m_framebuffer.framebuffer);
            m_framebuffer.framebuffer = nullptr;
        }

        for (auto& attachment : m_framebuffer.attachments)
        {
            if (attachment.view != nullptr)
            {
                m_rhi->destroyImageView(attachment.view);
                attachment.view = nullptr;
            }
            if (attachment.image != nullptr)
            {
                m_rhi->destroyImage(attachment.image);
                attachment.image = nullptr;
            }
            if (attachment.mem != nullptr)
            {
                m_rhi->freeMemory(attachment.mem);
                attachment.mem = nullptr;
            }
        }
        m_framebuffer.attachments.clear();

        for (auto& render_pipeline : m_render_pipelines)
        {
            m_rhi->destroyPipeline(render_pipeline.pipeline);
            m_rhi->destroyPipelineLayout(render_pipeline.layout);
        }
        m_render_pipelines.clear();

        for (auto& descriptor_info : m_descriptor_infos)
        {
            m_rhi->destroyDescriptorSetLayout(descriptor_info.layout);
            descriptor_info.layout = nullptr;
            descriptor_info.descriptor_set = nullptr;
        }
        m_descriptor_infos.clear();

        if (destroy_render_pass && m_framebuffer.render_pass != nullptr)
        {
            m_rhi->destroyRenderPass(m_framebuffer.render_pass);
            m_framebuffer.render_pass = nullptr;
        }
    }

    void RenderPass::teardown()
    {
        teardownCommonResources(true);
    }
} // namespace Piccolo
