#include "runtime/function/render/passes/particle_pass.h"

#include "runtime/function/global/global_context.h"
#include "runtime/function/render/render_camera.h"
#include "runtime/function/render/render_shader_bytecode.h"
#include "runtime/function/render/render_system.h"

#include "core/base/macro.h"
#include <fstream>

namespace Piccolo
{
    void ParticleEmitterBufferBatch::freeUpBatch(std::shared_ptr<RHI> rhi)
    {
        rhi->destroyBuffer(m_position_render_buffer);
        rhi->destroyBuffer(m_position_device_buffer);
        rhi->destroyBuffer(m_position_host_buffer);
        rhi->destroyBuffer(m_counter_device_buffer);
        rhi->destroyBuffer(m_counter_host_buffer);
        rhi->destroyBuffer(m_counter_readback_buffer);
        for (RHIBuffer*& buffer : m_counter_readback_buffers)
        {
            rhi->destroyBuffer(buffer);
        }
        m_counter_readback_buffers.clear();
        rhi->destroyBuffer(m_indirect_dispatch_argument_buffer);
        rhi->destroyBuffer(m_alive_list_buffer);
        rhi->destroyBuffer(m_alive_list_next_buffer);
        rhi->destroyBuffer(m_dead_list_buffer);
        rhi->destroyBuffer(m_particle_component_res_buffer);

        rhi->freeMemory(m_counter_host_memory);
        rhi->freeMemory(m_counter_readback_memory);
        for (RHIDeviceMemory*& memory : m_counter_readback_memories)
        {
            rhi->freeMemory(memory);
        }
        m_counter_readback_memories.clear();
        rhi->freeMemory(m_position_host_memory);
        rhi->freeMemory(m_position_device_memory);
        rhi->freeMemory(m_counter_device_memory);
        rhi->freeMemory(m_indirect_dispatch_argument_memory);
        rhi->freeMemory(m_alive_list_memory);
        rhi->freeMemory(m_alive_list_next_memory);
        rhi->freeMemory(m_dead_list_memory);
        rhi->freeMemory(m_particle_component_res_memory);
        rhi->freeMemory(m_position_render_memory);
    }

    void ParticlePass::copyNormalAndDepthImage()
    {
        const bool use_inline_copy = m_rhi->requiresDepthNormalCopyBeforeSubmit();
        uint8_t    index           = m_rhi->getCurrentFrameIndex() % m_rhi->getMaxFramesInFlight();
        if (!use_inline_copy)
        {
            index = m_rhi->getLastSubmittedFrameIndex();
        }

        RHICommandBuffer* copy_command_buffer =
            use_inline_copy ? m_rhi->getCurrentCommandBuffer() : m_copy_command_buffers[index];
        if (copy_command_buffer == nullptr)
        {
            LOG_ERROR("ParticlePass::copyNormalAndDepthImage: copy command buffer is unavailable");
            return;
        }

        if (!use_inline_copy)
        {
            RHICommandBufferBeginInfo command_buffer_begin_info {};
            command_buffer_begin_info.sType = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

            if (RHI_SUCCESS != m_rhi->beginCommandBufferPFN(copy_command_buffer, &command_buffer_begin_info))
            {
                throw std::runtime_error("begin particle copy command buffer");
            }
        }

        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        m_rhi->pushEvent(copy_command_buffer, "Copy Depth Image for Particle", color);

        // depth image
        RHIImageSubresourceRange subresourceRange = {RHI_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        RHIImageMemoryBarrier    imagememorybarrier {};
        imagememorybarrier.sType               = RHI_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imagememorybarrier.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
        imagememorybarrier.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
        imagememorybarrier.subresourceRange    = subresourceRange;
        {
            imagememorybarrier.oldLayout     = RHI_IMAGE_LAYOUT_UNDEFINED;
            imagememorybarrier.newLayout     = RHI_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imagememorybarrier.srcAccessMask = 0;
            imagememorybarrier.dstAccessMask = RHI_ACCESS_TRANSFER_WRITE_BIT;
            imagememorybarrier.image         = m_dst_depth_image;

            m_rhi->cmdPipelineBarrier(copy_command_buffer,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &imagememorybarrier);

            imagememorybarrier.oldLayout     = RHI_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            imagememorybarrier.newLayout     = RHI_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imagememorybarrier.srcAccessMask = RHI_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            imagememorybarrier.dstAccessMask = RHI_ACCESS_TRANSFER_READ_BIT;
            imagememorybarrier.image         = m_src_depth_image;

            m_rhi->cmdPipelineBarrier(copy_command_buffer,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &imagememorybarrier);

            const RHIExtent2D swapchain_extent = m_rhi->getSwapchainInfo().extent;
            RHIImageBlit copy_region {};
            copy_region.srcSubresource = {static_cast<RHIImageAspectFlags>(RHI_IMAGE_ASPECT_DEPTH_BIT), 0, 0, 1};
            copy_region.srcOffsets[0]  = {0, 0, 0};
            copy_region.srcOffsets[1]  = {static_cast<int32_t>(swapchain_extent.width),
                                          static_cast<int32_t>(swapchain_extent.height),
                                          1};
            copy_region.dstSubresource = {static_cast<RHIImageAspectFlags>(RHI_IMAGE_ASPECT_DEPTH_BIT), 0, 0, 1};
            copy_region.dstOffsets[0]  = {0, 0, 0};
            copy_region.dstOffsets[1]  = copy_region.srcOffsets[1];
            m_rhi->cmdCopyImageToImage(copy_command_buffer,
                                       m_src_depth_image,
                                       m_dst_depth_image,
                                       1,
                                       &copy_region);

            imagememorybarrier.oldLayout     = RHI_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imagememorybarrier.newLayout     = RHI_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            imagememorybarrier.srcAccessMask = RHI_ACCESS_TRANSFER_READ_BIT;
            imagememorybarrier.dstAccessMask =
                RHI_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | RHI_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            m_rhi->cmdPipelineBarrier(copy_command_buffer,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &imagememorybarrier);

            imagememorybarrier.image         = m_dst_depth_image;
            imagememorybarrier.oldLayout     = RHI_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imagememorybarrier.newLayout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imagememorybarrier.srcAccessMask = RHI_ACCESS_TRANSFER_WRITE_BIT;
            imagememorybarrier.dstAccessMask = RHI_ACCESS_SHADER_READ_BIT;

            m_rhi->cmdPipelineBarrier(copy_command_buffer,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &imagememorybarrier);
        }

        m_rhi->popEvent(copy_command_buffer); // end depth image copy label

        m_rhi->pushEvent(copy_command_buffer, "Copy Normal Image for Particle", color);

        // color image
        subresourceRange                    = {RHI_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        imagememorybarrier.subresourceRange = subresourceRange;
        {
            imagememorybarrier.oldLayout     = RHI_IMAGE_LAYOUT_UNDEFINED;
            imagememorybarrier.newLayout     = RHI_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imagememorybarrier.srcAccessMask = 0;
            imagememorybarrier.dstAccessMask = RHI_ACCESS_TRANSFER_WRITE_BIT;
            imagememorybarrier.image         = m_dst_normal_image;

            m_rhi->cmdPipelineBarrier(copy_command_buffer,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &imagememorybarrier);

            imagememorybarrier.oldLayout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imagememorybarrier.newLayout     = RHI_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imagememorybarrier.srcAccessMask = RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | RHI_ACCESS_SHADER_READ_BIT;
            imagememorybarrier.dstAccessMask = RHI_ACCESS_TRANSFER_READ_BIT;
            imagememorybarrier.image         = m_src_normal_image;

            m_rhi->cmdPipelineBarrier(copy_command_buffer,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &imagememorybarrier);

            const RHIExtent2D swapchain_extent = m_rhi->getSwapchainInfo().extent;
            RHIImageBlit copy_region {};
            copy_region.srcSubresource = {static_cast<RHIImageAspectFlags>(RHI_IMAGE_ASPECT_COLOR_BIT), 0, 0, 1};
            copy_region.srcOffsets[0]  = {0, 0, 0};
            copy_region.srcOffsets[1]  = {static_cast<int32_t>(swapchain_extent.width),
                                          static_cast<int32_t>(swapchain_extent.height),
                                          1};
            copy_region.dstSubresource = {static_cast<RHIImageAspectFlags>(RHI_IMAGE_ASPECT_COLOR_BIT), 0, 0, 1};
            copy_region.dstOffsets[0]  = {0, 0, 0};
            copy_region.dstOffsets[1]  = copy_region.srcOffsets[1];
            m_rhi->cmdCopyImageToImage(copy_command_buffer,
                                       m_src_normal_image,
                                       m_dst_normal_image,
                                       1,
                                       &copy_region);

            imagememorybarrier.oldLayout     = RHI_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            imagememorybarrier.newLayout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imagememorybarrier.srcAccessMask = RHI_ACCESS_TRANSFER_READ_BIT;
            imagememorybarrier.dstAccessMask = RHI_ACCESS_SHADER_READ_BIT;

            m_rhi->cmdPipelineBarrier(copy_command_buffer,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &imagememorybarrier);

            imagememorybarrier.image         = m_dst_normal_image;
            imagememorybarrier.oldLayout     = RHI_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imagememorybarrier.newLayout     = RHI_IMAGE_LAYOUT_GENERAL;
            imagememorybarrier.srcAccessMask = RHI_ACCESS_TRANSFER_WRITE_BIT;
            imagememorybarrier.dstAccessMask = RHI_ACCESS_SHADER_READ_BIT;

            m_rhi->cmdPipelineBarrier(copy_command_buffer,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      0,
                                      nullptr,
                                      1,
                                      &imagememorybarrier);
        }

        m_rhi->popEvent(copy_command_buffer);

        if (use_inline_copy)
        {
            return;
        }

        if (RHI_SUCCESS != m_rhi->endCommandBufferPFN(copy_command_buffer))
        {
            throw std::runtime_error("end particle copy command buffer");
        }

        RHIFence* copy_fence = const_cast<RHIFence*>(m_rhi->getCopyFenceList()[index]);
        if (RHI_SUCCESS != m_rhi->resetFencesPFN(1, &copy_fence))
        {
            throw std::runtime_error("reset particle copy fence");
        }

        RHISemaphore*                 copy_ready_semaphore = m_rhi->getCopyReadySemaphore(index);
        RHISemaphore*                 copy_done_semaphore  = m_rhi->getCopyDoneSemaphore(index);
        const RHISemaphore*           signal_semaphores[]  = {copy_done_semaphore};
        RHIPipelineStageFlags         wait_stages[]        = {RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        RHISubmitInfo                 submit_info {};
        submit_info.sType                   = RHI_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount      = 1;
        submit_info.pWaitSemaphores         = &copy_ready_semaphore;
        submit_info.pWaitDstStageMask       = wait_stages;
        submit_info.commandBufferCount      = 1;
        submit_info.pCommandBuffers         = &copy_command_buffer;
        submit_info.signalSemaphoreCount    = 1;
        submit_info.pSignalSemaphores       = signal_semaphores;

        if (RHI_SUCCESS != m_rhi->queueSubmit(m_rhi->getGraphicsQueue(), 1, &submit_info, copy_fence))
        {
            throw std::runtime_error("particle copy queue submit");
        }
    }

    ParticlePass::~ParticlePass() {}

    void ParticlePass::waitAllPendingGpuWork()
    {
        if (m_rhi == nullptr || !m_rhi->usesDedicatedComputeSubmission())
        {
            // Vulkan path: particle compute is submitted and waited inline in simulate().
            return;
        }

        for (uint32_t frame_index = 0; frame_index < m_compute_fences.size(); ++frame_index)
        {
            if (frame_index >= m_compute_readback_pending.size() || !m_compute_readback_pending[frame_index])
            {
                continue;
            }

            RHIFence* compute_fence = m_compute_fences[frame_index];
            if (compute_fence != nullptr)
            {
                m_rhi->waitForFencesPFN(1, &compute_fence, RHI_TRUE, UINT64_MAX);
            }
            m_compute_readback_pending[frame_index] = false;
            if (frame_index < m_compute_readback_emitters.size())
            {
                m_compute_readback_emitters[frame_index].clear();
            }
        }
    }

    void ParticlePass::teardown()
    {
        if (m_rhi == nullptr)
        {
            return;
        }

        for (ParticleEmitterBufferBatch& batch : m_emitter_buffer_batches)
        {
            batch.freeUpBatch(m_rhi);
        }
        m_emitter_buffer_batches.clear();

        teardownAttachments();

        m_rhi->destroyPipeline(m_kickoff_pipeline);
        m_rhi->destroyPipeline(m_emit_pipeline);
        m_rhi->destroyPipeline(m_simulate_pipeline);

        m_rhi->destroyBuffer(m_scene_uniform_buffer);
        m_scene_uniform_buffer = nullptr;
        m_rhi->freeMemory(m_scene_uniform_memory);
        m_scene_uniform_memory = nullptr;

        m_rhi->destroyBuffer(m_compute_uniform_buffer);
        m_compute_uniform_buffer = nullptr;
        m_rhi->freeMemory(m_compute_uniform_memory);
        m_compute_uniform_memory = nullptr;

        m_rhi->destroyBuffer(m_particle_billboard_uniform_buffer);
        m_particle_billboard_uniform_buffer = nullptr;
        m_rhi->freeMemory(m_particle_billboard_uniform_memory);
        m_particle_billboard_uniform_memory = nullptr;

        m_scene_uniform_buffer_mapped = nullptr;
        m_particle_compute_buffer_mapped = nullptr;
        m_particle_billboard_uniform_buffer_mapped = nullptr;

        if (m_compute_command_buffer != nullptr)
        {
            m_rhi->freeCommandBuffers(m_rhi->getCommandPoor(), 1, m_compute_command_buffer);
            m_compute_command_buffer = nullptr;
        }

        for (RHICommandBuffer*& copy_command_buffer : m_copy_command_buffers)
        {
            if (copy_command_buffer != nullptr)
            {
                m_rhi->freeCommandBuffers(m_rhi->getCommandPoor(), 1, copy_command_buffer);
                copy_command_buffer = nullptr;
            }
        }
        m_copy_command_buffers.clear();

        if (m_rhi->usesDedicatedComputeSubmission())
        {
            for (RHICommandBuffer*& compute_command_buffer : m_compute_command_buffers)
            {
                if (compute_command_buffer != nullptr)
                {
                    m_rhi->freeCommandBuffers(m_rhi->getCommandPoor(), 1, compute_command_buffer);
                    compute_command_buffer = nullptr;
                }
            }
            m_compute_command_buffers.clear();

            for (RHIFence*& compute_fence : m_compute_fences)
            {
                if (compute_fence != nullptr)
                {
                    m_rhi->destroyFence(compute_fence);
                    compute_fence = nullptr;
                }
            }
            m_compute_fences.clear();
        }

        if (m_fence != nullptr)
        {
            m_rhi->destroyFence(m_fence);
            m_fence = nullptr;
        }

        destroyOwnedSamplers();

        teardownCommonResources(false);
        m_render_pass = nullptr;
    }

    void ParticlePass::destroyOwnedSamplers()
    {
        if (m_rhi == nullptr)
        {
            return;
        }

        for (RHISampler* sampler : m_owned_samplers)
        {
            if (sampler != nullptr)
            {
                m_rhi->destroySampler(sampler);
            }
        }
        m_owned_samplers.clear();
    }

    void ParticlePass::teardownAttachments()
    {
        if (m_rhi == nullptr)
        {
            return;
        }

        m_rhi->destroyImageWithAllocation(m_particle_billboard_texture_image,
                                          m_particle_billboard_texture_image_view,
                                          m_particle_billboard_texture_allocation);
        m_rhi->destroyImageWithAllocation(m_piccolo_logo_texture_image,
                                          m_piccolo_logo_texture_image_view,
                                          m_piccolo_logo_texture_allocation);

        if (m_src_depth_image_view != nullptr)
        {
            m_rhi->destroyImageView(m_src_depth_image_view);
            m_src_depth_image_view = nullptr;
        }
        if (m_src_normal_image_view != nullptr)
        {
            m_rhi->destroyImageView(m_src_normal_image_view);
            m_src_normal_image_view = nullptr;
        }
        if (m_dst_depth_image != nullptr)
        {
            m_rhi->destroyImage(m_dst_depth_image);
            m_dst_depth_image = nullptr;
        }
        if (m_dst_depth_image_memory != nullptr)
        {
            m_rhi->freeMemory(m_dst_depth_image_memory);
            m_dst_depth_image_memory = nullptr;
        }
        if (m_dst_normal_image != nullptr)
        {
            m_rhi->destroyImage(m_dst_normal_image);
            m_dst_normal_image = nullptr;
        }
        if (m_dst_normal_image_memory != nullptr)
        {
            m_rhi->freeMemory(m_dst_normal_image_memory);
            m_dst_normal_image_memory = nullptr;
        }
    }

    void ParticlePass::updateAfterFramebufferRecreate()
    {
        m_rhi->destroyImage(m_dst_depth_image);
        m_rhi->freeMemory(m_dst_depth_image_memory);

        m_rhi->createImage(m_rhi->getSwapchainInfo().extent.width,
                           m_rhi->getSwapchainInfo().extent.height,
                           m_rhi->getDepthImageInfo().depth_image_format,
                           RHI_IMAGE_TILING_OPTIMAL,
                           RHI_IMAGE_USAGE_SAMPLED_BIT | RHI_IMAGE_USAGE_TRANSFER_DST_BIT,
                           RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           m_dst_depth_image,
                           m_dst_depth_image_memory,
                           0,
                           1,
                           1);

        m_rhi->destroyImage(m_dst_normal_image);
        m_rhi->freeMemory(m_dst_normal_image_memory);

        m_rhi->createImage(m_rhi->getSwapchainInfo().extent.width,
                           m_rhi->getSwapchainInfo().extent.height,
                           RHI_FORMAT_R8G8B8A8_UNORM,
                           RHI_IMAGE_TILING_OPTIMAL,
                           RHI_IMAGE_USAGE_STORAGE_BIT | RHI_IMAGE_USAGE_TRANSFER_DST_BIT,
                           RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           m_dst_normal_image,
                           m_dst_normal_image_memory,
                           0,
                           1,
                           1);

        m_rhi->createImageView(m_dst_depth_image,
                               m_rhi->getDepthImageInfo().depth_image_format,
                               RHI_IMAGE_ASPECT_DEPTH_BIT,
                               RHI_IMAGE_VIEW_TYPE_2D,
                               1,
                               1,
                               m_src_depth_image_view);

        m_rhi->createImageView(m_dst_normal_image,
                               RHI_FORMAT_R8G8B8A8_UNORM,
                               RHI_IMAGE_ASPECT_COLOR_BIT,
                               RHI_IMAGE_VIEW_TYPE_2D,
                               1,
                               1,
                               m_src_normal_image_view);

        updateDescriptorSet();
    }

    void ParticlePass::draw()
    {
        for (int i = 0; i < m_emitter_count; ++i)
        {
            float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            m_rhi->pushEvent(m_render_command_buffer, "ParticleBillboard", color);

            m_rhi->cmdBindPipelinePFN(
                m_render_command_buffer, RHI_PIPELINE_BIND_POINT_GRAPHICS, m_render_pipelines[1].pipeline);
            m_rhi->cmdSetViewportPFN(m_render_command_buffer, 0, 1, m_rhi->getSwapchainInfo().viewport);
            m_rhi->cmdSetScissorPFN(m_render_command_buffer, 0, 1, m_rhi->getSwapchainInfo().scissor);
            m_rhi->cmdBindDescriptorSetsPFN(m_render_command_buffer,
                                            RHI_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_render_pipelines[1].layout,
                                            0,
                                            1,
                                            &m_descriptor_infos[i * 3 + 2].descriptor_set,
                                            0,
                                            NULL);

            m_rhi->cmdDraw(m_render_command_buffer, 4, m_emitter_buffer_batches[i].m_num_particle, 0, 0);

            m_rhi->popEvent(m_render_command_buffer);
        }
    }

    void ParticlePass::setupAttachments()
    {
        // billboard texture
        {
            std::shared_ptr<TextureData> m_particle_billboard_texture_resource = m_render_resource->loadTextureHDR(
                m_particle_manager->getGlobalParticleRes().m_particle_billboard_texture_path);
            m_rhi->createGlobalImage(m_particle_billboard_texture_image,
                                     m_particle_billboard_texture_image_view,
                                     m_particle_billboard_texture_allocation,
                                     m_particle_billboard_texture_resource->m_width,
                                     m_particle_billboard_texture_resource->m_height,
                                     m_particle_billboard_texture_resource->m_pixels,
                                     m_particle_billboard_texture_resource->m_format);
        }

        // piccolo texture
        {
            std::shared_ptr<TextureData> m_piccolo_logo_texture_resource = m_render_resource->loadTexture(
                m_particle_manager->getGlobalParticleRes().m_piccolo_logo_texture_path, true);
            m_rhi->createGlobalImage(m_piccolo_logo_texture_image,
                                     m_piccolo_logo_texture_image_view,
                                     m_piccolo_logo_texture_allocation,
                                     m_piccolo_logo_texture_resource->m_width,
                                     m_piccolo_logo_texture_resource->m_height,
                                     m_piccolo_logo_texture_resource->m_pixels,
                                     m_piccolo_logo_texture_resource->m_format);
        }

        m_rhi->createImage(m_rhi->getSwapchainInfo().extent.width,
                           m_rhi->getSwapchainInfo().extent.height,
                           m_rhi->getDepthImageInfo().depth_image_format,
                           RHI_IMAGE_TILING_OPTIMAL,
                           RHI_IMAGE_USAGE_SAMPLED_BIT | RHI_IMAGE_USAGE_TRANSFER_DST_BIT,
                           RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           m_dst_depth_image,
                           m_dst_depth_image_memory,
                           0,
                           1,
                           1);

        m_rhi->createImage(m_rhi->getSwapchainInfo().extent.width,
                           m_rhi->getSwapchainInfo().extent.height,
                           RHI_FORMAT_R8G8B8A8_UNORM,
                           RHI_IMAGE_TILING_OPTIMAL,
                           RHI_IMAGE_USAGE_STORAGE_BIT | RHI_IMAGE_USAGE_TRANSFER_DST_BIT,
                           RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           m_dst_normal_image,
                           m_dst_normal_image_memory,
                           0,
                           1,
                           1);

        m_rhi->createImageView(m_dst_depth_image,
                               m_rhi->getDepthImageInfo().depth_image_format,
                               RHI_IMAGE_ASPECT_DEPTH_BIT,
                               RHI_IMAGE_VIEW_TYPE_2D,
                               1,
                               1,
                               m_src_depth_image_view);

        m_rhi->createImageView(m_dst_normal_image,
                               RHI_FORMAT_R8G8B8A8_UNORM,
                               RHI_IMAGE_ASPECT_COLOR_BIT,
                               RHI_IMAGE_VIEW_TYPE_2D,
                               1,
                               1,
                               m_src_normal_image_view);
    }

    void ParticlePass::setupParticleDescriptorSet()
    {
        for (int eid = 0; eid < m_emitter_count; ++eid)
        {
            RHIDescriptorSetAllocateInfo particlebillboard_global_descriptor_set_alloc_info;
            particlebillboard_global_descriptor_set_alloc_info.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            particlebillboard_global_descriptor_set_alloc_info.pNext = NULL;
            particlebillboard_global_descriptor_set_alloc_info.descriptorPool     = m_rhi->getDescriptorPoor();
            particlebillboard_global_descriptor_set_alloc_info.descriptorSetCount = 1;
            particlebillboard_global_descriptor_set_alloc_info.pSetLayouts        = &m_descriptor_infos[2].layout;

            if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&particlebillboard_global_descriptor_set_alloc_info,
                                                             m_descriptor_infos[eid * 3 + 2].descriptor_set))
            {
                throw std::runtime_error("allocate particle billboard global descriptor set");
            }

            RHIDescriptorBufferInfo particlebillboard_perframe_storage_buffer_info = {};
            particlebillboard_perframe_storage_buffer_info.offset                  = 0;
            particlebillboard_perframe_storage_buffer_info.range                   = RHI_WHOLE_SIZE;
            particlebillboard_perframe_storage_buffer_info.buffer = m_particle_billboard_uniform_buffer;

            RHIDescriptorBufferInfo particlebillboard_perdrawcall_storage_buffer_info = {};
            particlebillboard_perdrawcall_storage_buffer_info.offset                  = 0;
            particlebillboard_perdrawcall_storage_buffer_info.range                   = RHI_WHOLE_SIZE;
            particlebillboard_perdrawcall_storage_buffer_info.buffer =
                m_emitter_buffer_batches[eid].m_position_render_buffer;

            RHIWriteDescriptorSet particlebillboard_descriptor_writes_info[3];

            particlebillboard_descriptor_writes_info[0].sType      = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            particlebillboard_descriptor_writes_info[0].pNext      = NULL;
            particlebillboard_descriptor_writes_info[0].dstSet     = m_descriptor_infos[eid * 3 + 2].descriptor_set;
            particlebillboard_descriptor_writes_info[0].dstBinding = 0;
            particlebillboard_descriptor_writes_info[0].dstArrayElement = 0;
            particlebillboard_descriptor_writes_info[0].descriptorType  = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            particlebillboard_descriptor_writes_info[0].descriptorCount = 1;
            particlebillboard_descriptor_writes_info[0].pBufferInfo = &particlebillboard_perframe_storage_buffer_info;

            particlebillboard_descriptor_writes_info[1].sType      = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            particlebillboard_descriptor_writes_info[1].pNext      = NULL;
            particlebillboard_descriptor_writes_info[1].dstSet     = m_descriptor_infos[eid * 3 + 2].descriptor_set;
            particlebillboard_descriptor_writes_info[1].dstBinding = 1;
            particlebillboard_descriptor_writes_info[1].dstArrayElement = 0;
            particlebillboard_descriptor_writes_info[1].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            particlebillboard_descriptor_writes_info[1].descriptorCount = 1;
            particlebillboard_descriptor_writes_info[1].pBufferInfo =
                &particlebillboard_perdrawcall_storage_buffer_info;

            RHISampler*          sampler = nullptr;
            RHISamplerCreateInfo samplerCreateInfo {};
            samplerCreateInfo.sType            = RHI_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerCreateInfo.maxAnisotropy    = 1.0f;
            samplerCreateInfo.anisotropyEnable = true;
            samplerCreateInfo.magFilter        = RHI_FILTER_LINEAR;
            samplerCreateInfo.minFilter        = RHI_FILTER_LINEAR;
            samplerCreateInfo.mipmapMode       = RHI_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerCreateInfo.addressModeU     = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.addressModeV     = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.addressModeW     = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreateInfo.mipLodBias       = 0.0f;
            samplerCreateInfo.compareOp        = RHI_COMPARE_OP_NEVER;
            samplerCreateInfo.minLod           = 0.0f;
            samplerCreateInfo.maxLod           = 0.0f;
            samplerCreateInfo.borderColor      = RHI_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            if (RHI_SUCCESS != m_rhi->createSampler(&samplerCreateInfo, sampler))
            {
                throw std::runtime_error("create sampler error");
            }
            m_owned_samplers.push_back(sampler);

            RHIDescriptorImageInfo particle_texture_image_info = {};
            particle_texture_image_info.sampler                = sampler;
            particle_texture_image_info.imageView              = m_particle_billboard_texture_image_view;
            particle_texture_image_info.imageLayout            = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            particlebillboard_descriptor_writes_info[2].sType      = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            particlebillboard_descriptor_writes_info[2].pNext      = NULL;
            particlebillboard_descriptor_writes_info[2].dstSet     = m_descriptor_infos[eid * 3 + 2].descriptor_set;
            particlebillboard_descriptor_writes_info[2].dstBinding = 2;
            particlebillboard_descriptor_writes_info[2].dstArrayElement = 0;
            particlebillboard_descriptor_writes_info[2].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            particlebillboard_descriptor_writes_info[2].descriptorCount = 1;
            particlebillboard_descriptor_writes_info[2].pImageInfo      = &particle_texture_image_info;

            m_rhi->updateDescriptorSets(3, particlebillboard_descriptor_writes_info, 0, NULL);
        }
    }

    void ParticlePass::setEmitterCount(int count)
    {
        for (int i = 0; i < m_emitter_buffer_batches.size(); ++i)
        {
            m_emitter_buffer_batches[i].freeUpBatch(m_rhi);
        }

        m_emitter_count = count;
        m_emitter_buffer_batches.resize(m_emitter_count);
    }

    void ParticlePass::createEmitter(int id, const ParticleEmitterDesc& desc)
    {
        const RHIDeviceSize counterBufferSize = sizeof(ParticleCounter);
        ParticleCounter    counter;
        counter.alive_count           = m_emitter_buffer_batches[id].m_num_particle;
        counter.dead_count            = s_max_particles - m_emitter_buffer_batches[id].m_num_particle;
        counter.emit_count            = 0;
        counter.alive_count_after_sim = m_emitter_buffer_batches[id].m_num_particle;

        if constexpr (s_verbose_particle_alive_info)
        {
            LOG_INFO("Emitter {} info:", id);
            LOG_INFO("Dead {}, Alive {}, After sim {}, Emit {}",
                     counter.dead_count,
                     counter.alive_count,
                     counter.alive_count_after_sim,
                     counter.emit_count);
        }

        {
            const RHIDeviceSize     indirectArgumentSize = sizeof(IndirectArgumemt);
            struct IndirectArgumemt indirectargument     = {};
            indirectargument.alive_flap_bit              = 1;
            m_rhi->createBufferAndInitialize(RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT | RHI_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                                             RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                             m_emitter_buffer_batches[id].m_indirect_dispatch_argument_buffer,
                                             m_emitter_buffer_batches[id].m_indirect_dispatch_argument_memory,
                                             indirectArgumentSize,
                                             &indirectargument,
                                             indirectArgumentSize);

            const RHIDeviceSize aliveListSize = 4 * sizeof(uint32_t) * s_max_particles;
            std::vector<int>   aliveindices(s_max_particles * 4, 0);
            for (int i = 0; i < s_max_particles; ++i)
                aliveindices[i * 4] = i;

            m_rhi->createBufferAndInitialize(RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                             RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                             m_emitter_buffer_batches[id].m_alive_list_buffer,
                                             m_emitter_buffer_batches[id].m_alive_list_memory,
                                             aliveListSize,
                                             aliveindices.data(),
                                             aliveListSize);

            m_rhi->createBufferAndInitialize(RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                             RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                             m_emitter_buffer_batches[id].m_alive_list_next_buffer,
                                             m_emitter_buffer_batches[id].m_alive_list_next_memory,
                                             aliveListSize);

            const RHIDeviceSize  deadListSize = 4 * sizeof(uint32_t) * s_max_particles;
            std::vector<int32_t> deadindices(s_max_particles * 4, 0);
            for (int32_t i = 0; i < s_max_particles; ++i)
                deadindices[i * 4] = s_max_particles - 1 - i;

            m_rhi->createBufferAndInitialize(RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                             RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                             m_emitter_buffer_batches[id].m_dead_list_buffer,
                                             m_emitter_buffer_batches[id].m_dead_list_memory,
                                             deadListSize,
                                             deadindices.data(),
                                             deadListSize);
        }

        RHIFence*       fence = nullptr;
        ParticleCounter counterNext {};
        {
            m_rhi->createBufferAndInitialize(RHI_BUFFER_USAGE_TRANSFER_SRC_BIT | RHI_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                             m_emitter_buffer_batches[id].m_counter_host_buffer,
                                             m_emitter_buffer_batches[id].m_counter_host_memory,
                                             counterBufferSize,
                                             &counter,
                                             sizeof(counter));

            // Flush writes to host visible buffer
            void* mapped;

            m_rhi->mapMemory(m_emitter_buffer_batches[id].m_counter_host_memory, 0, RHI_WHOLE_SIZE, 0, &mapped);

            m_rhi->flushMappedMemoryRanges(
                nullptr, m_emitter_buffer_batches[id].m_counter_host_memory, 0, RHI_WHOLE_SIZE);

            m_rhi->unmapMemory(m_emitter_buffer_batches[id].m_counter_host_memory);

            m_rhi->createBufferAndInitialize(RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT | RHI_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                                 RHI_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                             m_emitter_buffer_batches[id].m_counter_device_buffer,
                                             m_emitter_buffer_batches[id].m_counter_device_memory,
                                             counterBufferSize);
            m_rhi->createBuffer(counterBufferSize,
                                RHI_BUFFER_USAGE_TRANSFER_DST_BIT,
                                RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                m_emitter_buffer_batches[id].m_counter_readback_buffer,
                                m_emitter_buffer_batches[id].m_counter_readback_memory);

            m_emitter_buffer_batches[id].m_counter_readback_buffers.resize(m_rhi->getMaxFramesInFlight(), nullptr);
            m_emitter_buffer_batches[id].m_counter_readback_memories.resize(m_rhi->getMaxFramesInFlight(), nullptr);
            for (uint8_t frame_index = 0; frame_index < m_rhi->getMaxFramesInFlight(); ++frame_index)
            {
                m_rhi->createBuffer(counterBufferSize,
                                    RHI_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                    m_emitter_buffer_batches[id].m_counter_readback_buffers[frame_index],
                                    m_emitter_buffer_batches[id].m_counter_readback_memories[frame_index]);
            }

            // Copy to staging buffer
            RHICommandBufferAllocateInfo cmdBufAllocateInfo {};
            cmdBufAllocateInfo.sType              = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdBufAllocateInfo.commandPool        = m_rhi->getCommandPoor();
            cmdBufAllocateInfo.level              = RHI_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdBufAllocateInfo.commandBufferCount = 1;
            RHICommandBuffer* copyCmd = nullptr;
            if (RHI_SUCCESS != m_rhi->allocateCommandBuffers(&cmdBufAllocateInfo, copyCmd))
            {
                throw std::runtime_error("alloc command buffer");
            }

            RHICommandBufferBeginInfo cmdBufInfo {};
            cmdBufInfo.sType = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            if (RHI_SUCCESS != m_rhi->beginCommandBuffer(copyCmd, &cmdBufInfo))
            {
                throw std::runtime_error("begin command buffer");
            }

            RHIBufferCopy copyRegion = {};
            copyRegion.srcOffset     = 0;
            copyRegion.dstOffset     = 0;
            copyRegion.size          = counterBufferSize;
            m_rhi->cmdCopyBuffer(copyCmd,
                                 m_emitter_buffer_batches[id].m_counter_host_buffer,
                                 m_emitter_buffer_batches[id].m_counter_device_buffer,
                                 1,
                                 &copyRegion);

            if (RHI_SUCCESS != m_rhi->endCommandBuffer(copyCmd))
            {
                throw std::runtime_error("buffer copy");
            }

            RHISubmitInfo submitInfo {};
            submitInfo.sType              = RHI_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &copyCmd;
            RHIFenceCreateInfo fenceInfo {};
            fenceInfo.sType = RHI_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = 0;
            if (RHI_SUCCESS != m_rhi->createFence(&fenceInfo, fence))
            {
                throw std::runtime_error("create fence");
            }

            // Submit to the queue
            if (RHI_SUCCESS != m_rhi->queueSubmit(m_rhi->getComputeQueue(), 1, &submitInfo, fence))
            {
                throw std::runtime_error("queue submit");
            }

            if (RHI_SUCCESS != m_rhi->waitForFencesPFN(1, &fence, RHI_TRUE, UINT64_MAX))
            {
                throw std::runtime_error("wait fence submit");
            }

            m_rhi->destroyFence(fence);
            m_rhi->freeCommandBuffers(m_rhi->getCommandPoor(), 1, copyCmd);
        }

        const RHIDeviceSize staggingBuferSize       = s_max_particles * sizeof(Particle);
        m_emitter_buffer_batches[id].m_emitter_desc = desc;

        // fill in data
        {

            m_rhi->createBufferAndInitialize(RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT | RHI_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                 RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                             m_emitter_buffer_batches[id].m_particle_component_res_buffer,
                                             m_emitter_buffer_batches[id].m_particle_component_res_memory,
                                             sizeof(ParticleEmitterDesc),
                                             &m_emitter_buffer_batches[id].m_emitter_desc,
                                             sizeof(ParticleEmitterDesc));

            if (RHI_SUCCESS != m_rhi->mapMemory(m_emitter_buffer_batches[id].m_particle_component_res_memory,
                                                0,
                                                RHI_WHOLE_SIZE,
                                                0,
                                                &m_emitter_buffer_batches[id].m_emitter_desc_mapped))
            {
                throw std::runtime_error("map emitter component res buffer");
            }

            m_rhi->createBufferAndInitialize(RHI_BUFFER_USAGE_TRANSFER_SRC_BIT | RHI_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                             m_emitter_buffer_batches[id].m_position_host_buffer,
                                             m_emitter_buffer_batches[id].m_position_host_memory,
                                             staggingBuferSize);

            // Flush writes to host visible buffer
            void* mapped;
            m_rhi->mapMemory(m_emitter_buffer_batches[id].m_position_host_memory, 0, RHI_WHOLE_SIZE, 0, &mapped);

            m_rhi->flushMappedMemoryRanges(
                nullptr, m_emitter_buffer_batches[id].m_position_host_memory, 0, RHI_WHOLE_SIZE);

            m_rhi->unmapMemory(m_emitter_buffer_batches[id].m_position_host_memory);

            m_rhi->createBufferAndInitialize(RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT | RHI_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                                 RHI_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                             m_emitter_buffer_batches[id].m_position_device_buffer,
                                             m_emitter_buffer_batches[id].m_position_device_memory,
                                             staggingBuferSize);

            m_rhi->createBufferAndInitialize(RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT | RHI_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                                 RHI_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                             m_emitter_buffer_batches[id].m_position_render_buffer,
                                             m_emitter_buffer_batches[id].m_position_render_memory,
                                             staggingBuferSize);

            // Copy to staging buffer
            RHICommandBufferAllocateInfo cmdBufAllocateInfo {};
            cmdBufAllocateInfo.sType              = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdBufAllocateInfo.commandPool        = m_rhi->getCommandPoor();
            cmdBufAllocateInfo.level              = RHI_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdBufAllocateInfo.commandBufferCount = 1;
            RHICommandBuffer* copyCmd = nullptr;
            if (RHI_SUCCESS != m_rhi->allocateCommandBuffers(&cmdBufAllocateInfo, copyCmd))
            {
                throw std::runtime_error("alloc command buffer");
            }
            RHICommandBufferBeginInfo cmdBufInfo {};
            cmdBufInfo.sType = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            if (RHI_SUCCESS != m_rhi->beginCommandBuffer(copyCmd, &cmdBufInfo))
            {
                throw std::runtime_error("begin command buffer");
            }

            RHIBufferCopy copyRegion = {};
            copyRegion.srcOffset     = 0;
            copyRegion.dstOffset     = 0;
            copyRegion.size          = staggingBuferSize;
            m_rhi->cmdCopyBuffer(copyCmd,
                                 m_emitter_buffer_batches[id].m_position_host_buffer,
                                 m_emitter_buffer_batches[id].m_position_device_buffer,
                                 1,
                                 &copyRegion);

            if (RHI_SUCCESS != m_rhi->endCommandBuffer(copyCmd))
            {
                throw std::runtime_error("buffer copy");
            }

            RHISubmitInfo submitInfo {};
            submitInfo.sType              = RHI_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &copyCmd;
            RHIFenceCreateInfo fenceInfo {};
            fenceInfo.sType = RHI_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = 0;
            if (RHI_SUCCESS != m_rhi->createFence(&fenceInfo, fence))
            {
                throw std::runtime_error("create fence");
            }

            // Submit to the queue
            if (RHI_SUCCESS != m_rhi->queueSubmit(m_rhi->getComputeQueue(), 1, &submitInfo, fence))
            {
                throw std::runtime_error("queue submit");
            }

            if (RHI_SUCCESS != m_rhi->waitForFencesPFN(1, &fence, RHI_TRUE, UINT64_MAX))
            {
                throw std::runtime_error("wait fence submit");
            }

            m_rhi->destroyFence(fence);
            m_rhi->freeCommandBuffers(m_rhi->getCommandPoor(), 1, copyCmd);
        }
    }

    void ParticlePass::initializeEmitters()
    {
        allocateDescriptorSet();
        updateDescriptorSet();
        setupParticleDescriptorSet();
    }

    void ParticlePass::setupParticlePass()
    {
        prepareUniformBuffer();
        setupDescriptorSetLayout();
        setupPipelines();
        setupAttachments();

        RHICommandBufferAllocateInfo cmdBufAllocateInfo {};
        cmdBufAllocateInfo.sType              = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufAllocateInfo.commandPool        = m_rhi->getCommandPoor();
        cmdBufAllocateInfo.level              = RHI_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufAllocateInfo.commandBufferCount = 1;
        if (RHI_SUCCESS != m_rhi->allocateCommandBuffers(&cmdBufAllocateInfo, m_compute_command_buffer))
            throw std::runtime_error("alloc compute command buffer");

        m_copy_command_buffers.resize(m_rhi->getMaxFramesInFlight(), nullptr);
        for (RHICommandBuffer*& copy_command_buffer : m_copy_command_buffers)
        {
            if (RHI_SUCCESS != m_rhi->allocateCommandBuffers(&cmdBufAllocateInfo, copy_command_buffer))
                throw std::runtime_error("alloc copy command buffer");
        }

        RHIFenceCreateInfo fenceCreateInfo {};
        fenceCreateInfo.sType = RHI_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.flags = 0;
        if (RHI_SUCCESS != m_rhi->createFence(&fenceCreateInfo, m_fence))
            throw std::runtime_error("create fence");

        if (m_rhi->usesDedicatedComputeSubmission())
        {
            m_compute_command_buffers.resize(m_rhi->getMaxFramesInFlight(), nullptr);
            for (RHICommandBuffer*& compute_command_buffer : m_compute_command_buffers)
            {
                if (RHI_SUCCESS != m_rhi->allocateCommandBuffers(&cmdBufAllocateInfo, compute_command_buffer))
                    throw std::runtime_error("alloc D3D12 compute command buffer");
                m_rhi->setCommandBufferComputeQueue(compute_command_buffer, true);
            }

            m_compute_fences.resize(m_rhi->getMaxFramesInFlight(), nullptr);
            for (RHIFence*& compute_fence : m_compute_fences)
            {
                if (RHI_SUCCESS != m_rhi->createFence(&fenceCreateInfo, compute_fence))
                    throw std::runtime_error("create D3D12 compute fence");
            }

            m_compute_readback_pending.assign(m_rhi->getMaxFramesInFlight(), false);
            m_compute_readback_emitters.resize(m_rhi->getMaxFramesInFlight());
        }
    }

    void ParticlePass::initialize(const RenderPassInitInfo* init_info)
    {
        RenderPass::initialize(nullptr);

        const ParticlePassInitInfo* _init_info = static_cast<const ParticlePassInitInfo*>(init_info);
        m_particle_manager                     = _init_info->m_particle_manager;
    }

    void ParticlePass::setupDescriptorSetLayout()
    {
        m_descriptor_infos.resize(3);

        // compute descriptor sets
        {
            RHIDescriptorSetLayoutBinding particle_layout_bindings[11] = {};
            {
                RHIDescriptorSetLayoutBinding& uniform_layout_bingding = particle_layout_bindings[0];
                uniform_layout_bingding.binding                        = 0;
                uniform_layout_bingding.descriptorType                 = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                uniform_layout_bingding.descriptorCount                = 1;
                uniform_layout_bingding.stageFlags                     = RHI_SHADER_STAGE_COMPUTE_BIT;
            }

            {
                RHIDescriptorSetLayoutBinding& storage_position_layout_binding = particle_layout_bindings[1];
                storage_position_layout_binding.binding                        = 1;
                storage_position_layout_binding.descriptorType                 = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                storage_position_layout_binding.descriptorCount                = 1;
                storage_position_layout_binding.stageFlags                     = RHI_SHADER_STAGE_COMPUTE_BIT;
            }

            {
                RHIDescriptorSetLayoutBinding& storage_counter_layout_binding = particle_layout_bindings[2];
                storage_counter_layout_binding.binding                        = 2;
                storage_counter_layout_binding.descriptorType                 = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                storage_counter_layout_binding.descriptorCount                = 1;
                storage_counter_layout_binding.stageFlags                     = RHI_SHADER_STAGE_COMPUTE_BIT;
            }

            {
                RHIDescriptorSetLayoutBinding& storage_indirectargument_layout_binding = particle_layout_bindings[3];
                storage_indirectargument_layout_binding.binding                        = 3;
                storage_indirectargument_layout_binding.descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                storage_indirectargument_layout_binding.descriptorCount = 1;
                storage_indirectargument_layout_binding.stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;
            }

            {
                RHIDescriptorSetLayoutBinding& alive_list_layout_binding = particle_layout_bindings[4];
                alive_list_layout_binding.binding                        = 4;
                alive_list_layout_binding.descriptorType                 = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                alive_list_layout_binding.descriptorCount                = 1;
                alive_list_layout_binding.stageFlags                     = RHI_SHADER_STAGE_COMPUTE_BIT;
            }

            {
                RHIDescriptorSetLayoutBinding& dead_list_layout_binding = particle_layout_bindings[5];
                dead_list_layout_binding.binding                        = 5;
                dead_list_layout_binding.descriptorType                 = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                dead_list_layout_binding.descriptorCount                = 1;
                dead_list_layout_binding.stageFlags                     = RHI_SHADER_STAGE_COMPUTE_BIT;
            }

            {
                RHIDescriptorSetLayoutBinding& alive_list_next_layout_binding = particle_layout_bindings[6];
                alive_list_next_layout_binding.binding                        = 6;
                alive_list_next_layout_binding.descriptorType                 = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                alive_list_next_layout_binding.descriptorCount                = 1;
                alive_list_next_layout_binding.stageFlags                     = RHI_SHADER_STAGE_COMPUTE_BIT;
            }

            {
                RHIDescriptorSetLayoutBinding& particle_res_layout_binding = particle_layout_bindings[7];
                particle_res_layout_binding.binding                        = 7;
                particle_res_layout_binding.descriptorType                 = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                particle_res_layout_binding.descriptorCount                = 1;
                particle_res_layout_binding.stageFlags                     = RHI_SHADER_STAGE_COMPUTE_BIT;
            }

            {
                RHIDescriptorSetLayoutBinding& scene_uniformbuffer_layout_binding = particle_layout_bindings[8];
                scene_uniformbuffer_layout_binding.binding                        = 8;
                scene_uniformbuffer_layout_binding.descriptorType                 = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                scene_uniformbuffer_layout_binding.descriptorCount                = 1;
                scene_uniformbuffer_layout_binding.stageFlags                     = RHI_SHADER_STAGE_COMPUTE_BIT;
            }

            {
                RHIDescriptorSetLayoutBinding& storage_render_position_layout_binding = particle_layout_bindings[9];
                storage_render_position_layout_binding.binding                        = 9;
                storage_render_position_layout_binding.descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                storage_render_position_layout_binding.descriptorCount = 1;
                storage_render_position_layout_binding.stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;
            }

            {
                RHIDescriptorSetLayoutBinding& piccolo_texture_layout_binding = particle_layout_bindings[10];
                piccolo_texture_layout_binding.binding                        = 10;
                piccolo_texture_layout_binding.descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                piccolo_texture_layout_binding.descriptorCount = 1;
                piccolo_texture_layout_binding.stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;
            }

            RHIDescriptorSetLayoutCreateInfo particle_descriptor_layout_create_info;
            particle_descriptor_layout_create_info.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            particle_descriptor_layout_create_info.pNext = NULL;
            particle_descriptor_layout_create_info.flags = 0;
            particle_descriptor_layout_create_info.bindingCount =
                sizeof(particle_layout_bindings) / sizeof(particle_layout_bindings[0]);
            particle_descriptor_layout_create_info.pBindings = particle_layout_bindings;

            if (RHI_SUCCESS !=
                m_rhi->createDescriptorSetLayout(&particle_descriptor_layout_create_info, m_descriptor_infos[0].layout))
            {
                throw std::runtime_error("create particle compute descriptor set layout failed");
            }
        }
        // scene depth and normal binding
        {
            RHIDescriptorSetLayoutBinding scene_global_layout_bindings[2] = {};

            RHIDescriptorSetLayoutBinding& gbuffer_normal_global_layout_input_attachment_binding =
                scene_global_layout_bindings[0];
            gbuffer_normal_global_layout_input_attachment_binding.binding         = 0;
            gbuffer_normal_global_layout_input_attachment_binding.descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            gbuffer_normal_global_layout_input_attachment_binding.descriptorCount = 1;
            gbuffer_normal_global_layout_input_attachment_binding.stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

            RHIDescriptorSetLayoutBinding& gbuffer_depth_global_layout_input_attachment_binding =
                scene_global_layout_bindings[1];
            gbuffer_depth_global_layout_input_attachment_binding.binding = 1;
            gbuffer_depth_global_layout_input_attachment_binding.descriptorType =
                RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            gbuffer_depth_global_layout_input_attachment_binding.descriptorCount = 1;
            gbuffer_depth_global_layout_input_attachment_binding.stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

            RHIDescriptorSetLayoutCreateInfo gbuffer_lighting_global_layout_create_info;
            gbuffer_lighting_global_layout_create_info.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            gbuffer_lighting_global_layout_create_info.pNext = NULL;
            gbuffer_lighting_global_layout_create_info.flags = 0;
            gbuffer_lighting_global_layout_create_info.bindingCount =
                sizeof(scene_global_layout_bindings) / sizeof(scene_global_layout_bindings[0]);
            gbuffer_lighting_global_layout_create_info.pBindings = scene_global_layout_bindings;

            if (RHI_SUCCESS != m_rhi->createDescriptorSetLayout(&gbuffer_lighting_global_layout_create_info,
                                                                m_descriptor_infos[1].layout))
                throw std::runtime_error("create scene normal and depth global layout");
        }

        {
            RHIDescriptorSetLayoutBinding particlebillboard_global_layout_bindings[3];

            RHIDescriptorSetLayoutBinding& particlebillboard_global_layout_perframe_storage_buffer_binding =
                particlebillboard_global_layout_bindings[0];
            particlebillboard_global_layout_perframe_storage_buffer_binding.binding = 0;
            particlebillboard_global_layout_perframe_storage_buffer_binding.descriptorType =
                RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            particlebillboard_global_layout_perframe_storage_buffer_binding.descriptorCount = 1;
            particlebillboard_global_layout_perframe_storage_buffer_binding.stageFlags = RHI_SHADER_STAGE_VERTEX_BIT;
            particlebillboard_global_layout_perframe_storage_buffer_binding.pImmutableSamplers = NULL;

            RHIDescriptorSetLayoutBinding& particlebillboard_global_layout_perdrawcall_storage_buffer_binding =
                particlebillboard_global_layout_bindings[1];
            particlebillboard_global_layout_perdrawcall_storage_buffer_binding.binding = 1;
            particlebillboard_global_layout_perdrawcall_storage_buffer_binding.descriptorType =
                RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            particlebillboard_global_layout_perdrawcall_storage_buffer_binding.descriptorCount = 1;
            particlebillboard_global_layout_perdrawcall_storage_buffer_binding.stageFlags = RHI_SHADER_STAGE_VERTEX_BIT;
            particlebillboard_global_layout_perdrawcall_storage_buffer_binding.pImmutableSamplers = NULL;

            RHIDescriptorSetLayoutBinding& particlebillboard_global_layout_texture_binding =
                particlebillboard_global_layout_bindings[2];
            particlebillboard_global_layout_texture_binding.binding        = 2;
            particlebillboard_global_layout_texture_binding.descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            particlebillboard_global_layout_texture_binding.descriptorCount    = 1;
            particlebillboard_global_layout_texture_binding.stageFlags         = RHI_SHADER_STAGE_FRAGMENT_BIT;
            particlebillboard_global_layout_texture_binding.pImmutableSamplers = NULL;

            RHIDescriptorSetLayoutCreateInfo particlebillboard_global_layout_create_info;
            particlebillboard_global_layout_create_info.sType = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            particlebillboard_global_layout_create_info.pNext = NULL;
            particlebillboard_global_layout_create_info.flags = 0;
            particlebillboard_global_layout_create_info.bindingCount = 3;
            particlebillboard_global_layout_create_info.pBindings    = particlebillboard_global_layout_bindings;

            if (RHI_SUCCESS != m_rhi->createDescriptorSetLayout(&particlebillboard_global_layout_create_info,
                                                                m_descriptor_infos[2].layout))
            {
                throw std::runtime_error("create particle billboard global layout");
            }
        }
    }

    void ParticlePass::setupPipelines()
    {
        m_render_pipelines.resize(2);

        // compute pipeline
        {
            RHIDescriptorSetLayout*     descriptorset_layouts[2] = {m_descriptor_infos[0].layout,
                                                                    m_descriptor_infos[1].layout};
            RHIPipelineLayoutCreateInfo pipeline_layout_create_info {};
            pipeline_layout_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipeline_layout_create_info.setLayoutCount =
                sizeof(descriptorset_layouts) / sizeof(descriptorset_layouts[0]);
            pipeline_layout_create_info.pSetLayouts = descriptorset_layouts;

            if (m_rhi->createPipelineLayout(&pipeline_layout_create_info, m_render_pipelines[0].layout) != RHI_SUCCESS)
                throw std::runtime_error("create compute pass pipe layout");
        }
        RHIComputePipelineCreateInfo computePipelineCreateInfo {};

        computePipelineCreateInfo.sType  = RHI_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCreateInfo.layout = m_render_pipelines[0].layout;
        computePipelineCreateInfo.flags  = 0;

        RHIPipelineShaderStageCreateInfo shaderStage = {};
        shaderStage.sType                            = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStage.stage                            = RHI_SHADER_STAGE_COMPUTE_BIT;
        shaderStage.pName                            = "main";
        shaderStage.pSpecializationInfo              = RHI_NULL_HANDLE;

        {
            RHIShader* kickoff_shader_module =
                m_rhi->createShaderModule(PICCOLO_RENDER_SHADER_BYTECODE(m_rhi, PARTICLE_KICKOFF_COMP));
            assert(kickoff_shader_module != RHI_NULL_HANDLE);
            shaderStage.module = kickoff_shader_module;

            computePipelineCreateInfo.pStages = &shaderStage;
            if (RHI_SUCCESS !=
                m_rhi->createComputePipelines(RHI_NULL_HANDLE, 1, &computePipelineCreateInfo, m_kickoff_pipeline))
            {
                throw std::runtime_error("create particle kickoff pipe");
            }
            m_rhi->destroyShaderModule(kickoff_shader_module);
        }

        {
            RHIShader* emit_shader_module =
                m_rhi->createShaderModule(PICCOLO_RENDER_SHADER_BYTECODE(m_rhi, PARTICLE_EMIT_COMP));
            assert(emit_shader_module != RHI_NULL_HANDLE);
            shaderStage.module = emit_shader_module;

            computePipelineCreateInfo.pStages = &shaderStage;
            if (RHI_SUCCESS !=
                m_rhi->createComputePipelines(RHI_NULL_HANDLE, 1, &computePipelineCreateInfo, m_emit_pipeline))
            {
                throw std::runtime_error("create particle emit pipe");
            }
            m_rhi->destroyShaderModule(emit_shader_module);
        }

        {
            RHIShader* simulate_shader_module =
                m_rhi->createShaderModule(PICCOLO_RENDER_SHADER_BYTECODE(m_rhi, PARTICLE_SIMULATE_COMP));
            assert(simulate_shader_module != RHI_NULL_HANDLE);
            shaderStage.module = simulate_shader_module;

            computePipelineCreateInfo.pStages = &shaderStage;

            if (RHI_SUCCESS !=
                m_rhi->createComputePipelines(RHI_NULL_HANDLE, 1, &computePipelineCreateInfo, m_simulate_pipeline))
            {
                throw std::runtime_error("create particle simulate pipe");
            }
            m_rhi->destroyShaderModule(simulate_shader_module);
        }

        // particle billboard
        {
            RHIDescriptorSetLayout*     descriptorset_layouts[1] = {m_descriptor_infos[2].layout};
            RHIPipelineLayoutCreateInfo pipeline_layout_create_info {};
            pipeline_layout_create_info.sType          = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipeline_layout_create_info.setLayoutCount = 1;
            pipeline_layout_create_info.pSetLayouts    = descriptorset_layouts;

            if (m_rhi->createPipelineLayout(&pipeline_layout_create_info, m_render_pipelines[1].layout) != RHI_SUCCESS)
            {
                throw std::runtime_error("create particle billboard pipeline layout");
            }

            RHIShader* vert_shader_module =
                m_rhi->createShaderModule(PICCOLO_RENDER_SHADER_BYTECODE(m_rhi, PARTICLEBILLBOARD_VERT));
            RHIShader* frag_shader_module =
                m_rhi->createShaderModule(PICCOLO_RENDER_SHADER_BYTECODE(m_rhi, PARTICLEBILLBOARD_FRAG));

            RHIPipelineShaderStageCreateInfo vert_pipeline_shader_stage_create_info {};
            vert_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vert_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_VERTEX_BIT;
            vert_pipeline_shader_stage_create_info.module = vert_shader_module;
            vert_pipeline_shader_stage_create_info.pName  = "main";

            RHIPipelineShaderStageCreateInfo frag_pipeline_shader_stage_create_info {};
            frag_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            frag_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_FRAGMENT_BIT;
            frag_pipeline_shader_stage_create_info.module = frag_shader_module;
            frag_pipeline_shader_stage_create_info.pName  = "main";

            RHIPipelineShaderStageCreateInfo shader_stages[] = {vert_pipeline_shader_stage_create_info,
                                                                frag_pipeline_shader_stage_create_info};

            RHIPipelineVertexInputStateCreateInfo vertex_input_state_create_info {};
            vertex_input_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertex_input_state_create_info.vertexBindingDescriptionCount   = 0;
            vertex_input_state_create_info.pVertexBindingDescriptions      = NULL;
            vertex_input_state_create_info.vertexAttributeDescriptionCount = 0;
            vertex_input_state_create_info.pVertexAttributeDescriptions    = NULL;

            RHIPipelineInputAssemblyStateCreateInfo input_assembly_create_info {};
            input_assembly_create_info.sType    = RHI_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            input_assembly_create_info.topology = RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            input_assembly_create_info.primitiveRestartEnable = RHI_FALSE;

            RHIPipelineViewportStateCreateInfo viewport_state_create_info {};
            viewport_state_create_info.sType         = RHI_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport_state_create_info.viewportCount = 1;
            viewport_state_create_info.pViewports    = m_rhi->getSwapchainInfo().viewport;
            viewport_state_create_info.scissorCount  = 1;
            viewport_state_create_info.pScissors     = m_rhi->getSwapchainInfo().scissor;

            RHIPipelineRasterizationStateCreateInfo rasterization_state_create_info {};
            rasterization_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterization_state_create_info.depthClampEnable        = RHI_FALSE;
            rasterization_state_create_info.rasterizerDiscardEnable = RHI_FALSE;
            rasterization_state_create_info.polygonMode             = RHI_POLYGON_MODE_FILL;
            rasterization_state_create_info.lineWidth               = 1.0f;
            rasterization_state_create_info.cullMode                = RHI_CULL_MODE_NONE;
            rasterization_state_create_info.frontFace               = RHI_FRONT_FACE_CLOCKWISE;
            rasterization_state_create_info.depthBiasEnable         = RHI_FALSE;
            rasterization_state_create_info.depthBiasConstantFactor = 0.0f;
            rasterization_state_create_info.depthBiasClamp          = 0.0f;
            rasterization_state_create_info.depthBiasSlopeFactor    = 0.0f;

            RHIPipelineMultisampleStateCreateInfo multisample_state_create_info {};
            multisample_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample_state_create_info.sampleShadingEnable  = RHI_FALSE;
            multisample_state_create_info.rasterizationSamples = RHI_SAMPLE_COUNT_1_BIT;

            RHIPipelineColorBlendAttachmentState color_blend_attachments[1] = {};
            color_blend_attachments[0].colorWriteMask = RHI_COLOR_COMPONENT_R_BIT | RHI_COLOR_COMPONENT_G_BIT |
                                                        RHI_COLOR_COMPONENT_B_BIT | RHI_COLOR_COMPONENT_A_BIT;
            color_blend_attachments[0].blendEnable         = RHI_TRUE;
            color_blend_attachments[0].srcColorBlendFactor = RHI_BLEND_FACTOR_ONE;
            color_blend_attachments[0].dstColorBlendFactor = RHI_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            color_blend_attachments[0].colorBlendOp        = RHI_BLEND_OP_ADD;
            color_blend_attachments[0].srcAlphaBlendFactor = RHI_BLEND_FACTOR_ONE;
            color_blend_attachments[0].dstAlphaBlendFactor = RHI_BLEND_FACTOR_ZERO;
            color_blend_attachments[0].alphaBlendOp        = RHI_BLEND_OP_ADD;

            RHIPipelineColorBlendStateCreateInfo color_blend_state_create_info = {};
            color_blend_state_create_info.sType         = RHI_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            color_blend_state_create_info.logicOpEnable = RHI_FALSE;
            color_blend_state_create_info.logicOp       = RHI_LOGIC_OP_COPY;
            color_blend_state_create_info.attachmentCount =
                sizeof(color_blend_attachments) / sizeof(color_blend_attachments[0]);
            color_blend_state_create_info.pAttachments      = &color_blend_attachments[0];
            color_blend_state_create_info.blendConstants[0] = 0.0f;
            color_blend_state_create_info.blendConstants[1] = 0.0f;
            color_blend_state_create_info.blendConstants[2] = 0.0f;
            color_blend_state_create_info.blendConstants[3] = 0.0f;

            RHIPipelineDepthStencilStateCreateInfo depth_stencil_create_info {};
            depth_stencil_create_info.sType            = RHI_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth_stencil_create_info.depthTestEnable  = RHI_TRUE;
            depth_stencil_create_info.depthWriteEnable = RHI_FALSE;
            depth_stencil_create_info.depthCompareOp   = RHI_COMPARE_OP_LESS;
            depth_stencil_create_info.depthBoundsTestEnable = RHI_FALSE;
            depth_stencil_create_info.stencilTestEnable     = RHI_FALSE;

            RHIDynamicState dynamic_states[] = {RHI_DYNAMIC_STATE_VIEWPORT, RHI_DYNAMIC_STATE_SCISSOR};

            RHIPipelineDynamicStateCreateInfo dynamic_state_create_info {};
            dynamic_state_create_info.sType             = RHI_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic_state_create_info.dynamicStateCount = 2;
            dynamic_state_create_info.pDynamicStates    = dynamic_states;

            RHIGraphicsPipelineCreateInfo pipelineInfo {};
            pipelineInfo.sType               = RHI_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount          = 2;
            pipelineInfo.pStages             = shader_stages;
            pipelineInfo.pVertexInputState   = &vertex_input_state_create_info;
            pipelineInfo.pInputAssemblyState = &input_assembly_create_info;
            pipelineInfo.pViewportState      = &viewport_state_create_info;
            pipelineInfo.pRasterizationState = &rasterization_state_create_info;
            pipelineInfo.pMultisampleState   = &multisample_state_create_info;
            pipelineInfo.pColorBlendState    = &color_blend_state_create_info;
            pipelineInfo.pDepthStencilState  = &depth_stencil_create_info;
            pipelineInfo.layout              = m_render_pipelines[1].layout;
            pipelineInfo.renderPass          = m_render_pass;
            pipelineInfo.subpass             = _main_camera_subpass_forward_lighting;
            pipelineInfo.basePipelineHandle  = RHI_NULL_HANDLE;
            pipelineInfo.pDynamicState       = &dynamic_state_create_info;

            if (m_rhi->createGraphicsPipelines(RHI_NULL_HANDLE, 1, &pipelineInfo, m_render_pipelines[1].pipeline) !=
                RHI_SUCCESS)
            {
                throw std::runtime_error("create particle billboard graphics pipeline");
            }

            m_rhi->destroyShaderModule(vert_shader_module);
            m_rhi->destroyShaderModule(frag_shader_module);
        }
    }

    void ParticlePass::allocateDescriptorSet()
    {
        if (m_emitter_count <= 0)
        {
            return;
        }

        RHIDescriptorSetAllocateInfo particle_descriptor_set_alloc_info;
        particle_descriptor_set_alloc_info.sType          = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        particle_descriptor_set_alloc_info.descriptorPool = m_rhi->getDescriptorPoor();

        m_descriptor_infos.resize(3 * m_emitter_count);
        for (int eid = 0; eid < m_emitter_count; ++eid)
        {
            particle_descriptor_set_alloc_info.pSetLayouts        = &m_descriptor_infos[0].layout;
            particle_descriptor_set_alloc_info.descriptorSetCount = 1;
            particle_descriptor_set_alloc_info.pNext              = NULL;

            if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&particle_descriptor_set_alloc_info,
                                                             m_descriptor_infos[eid * 3].descriptor_set))
                throw std::runtime_error("allocate compute descriptor set");
            particle_descriptor_set_alloc_info.pSetLayouts        = &m_descriptor_infos[1].layout;
            particle_descriptor_set_alloc_info.descriptorSetCount = 1;
            particle_descriptor_set_alloc_info.pNext              = NULL;

            if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&particle_descriptor_set_alloc_info,
                                                             m_descriptor_infos[eid * 3 + 1].descriptor_set))
                throw std::runtime_error("allocate normal and depth descriptor set failed");
        }
    }

    void ParticlePass::updateDescriptorSet()
    {
        for (int eid = 0; eid < m_emitter_count; ++eid)
        {
            // compute part
            {
                std::vector<RHIWriteDescriptorSet> computeWriteDescriptorSets {
                    {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}};

                RHIDescriptorBufferInfo uniformbufferDescriptor = {m_compute_uniform_buffer, 0, RHI_WHOLE_SIZE};
                {
                    RHIWriteDescriptorSet& descriptorset = computeWriteDescriptorSets[0];
                    descriptorset.sType                  = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorset.dstSet                 = m_descriptor_infos[eid * 3].descriptor_set;
                    descriptorset.descriptorType         = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    descriptorset.dstBinding             = 0;
                    descriptorset.pBufferInfo            = &uniformbufferDescriptor;
                    descriptorset.descriptorCount        = 1;
                }

                RHIDescriptorBufferInfo positionBufferDescriptor = {
                    m_emitter_buffer_batches[eid].m_position_device_buffer, 0, RHI_WHOLE_SIZE};
                {
                    RHIWriteDescriptorSet& descriptorset = computeWriteDescriptorSets[1];
                    descriptorset.sType                  = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorset.dstSet                 = m_descriptor_infos[eid * 3].descriptor_set;
                    descriptorset.descriptorType         = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    descriptorset.dstBinding             = 1;
                    descriptorset.pBufferInfo            = &positionBufferDescriptor;
                    descriptorset.descriptorCount        = 1;
                }

                RHIDescriptorBufferInfo counterBufferDescriptor = {
                    m_emitter_buffer_batches[eid].m_counter_device_buffer, 0, RHI_WHOLE_SIZE};
                {
                    RHIWriteDescriptorSet& descriptorset = computeWriteDescriptorSets[2];
                    descriptorset.sType                  = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorset.dstSet                 = m_descriptor_infos[eid * 3].descriptor_set;
                    descriptorset.descriptorType         = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    descriptorset.dstBinding             = 2;
                    descriptorset.pBufferInfo            = &counterBufferDescriptor;
                    descriptorset.descriptorCount        = 1;
                }

                RHIDescriptorBufferInfo indirectArgumentBufferDescriptor = {
                    m_emitter_buffer_batches[eid].m_indirect_dispatch_argument_buffer, 0, RHI_WHOLE_SIZE};
                {
                    RHIWriteDescriptorSet& descriptorset = computeWriteDescriptorSets[3];
                    descriptorset.sType                  = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorset.dstSet                 = m_descriptor_infos[eid * 3].descriptor_set;
                    descriptorset.descriptorType         = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    descriptorset.dstBinding             = 3;
                    descriptorset.pBufferInfo            = &indirectArgumentBufferDescriptor;
                    descriptorset.descriptorCount        = 1;
                }

                RHIDescriptorBufferInfo aliveListBufferDescriptor = {
                    m_emitter_buffer_batches[eid].m_alive_list_buffer, 0, RHI_WHOLE_SIZE};
                {
                    RHIWriteDescriptorSet& descriptorset = computeWriteDescriptorSets[4];
                    descriptorset.sType                  = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorset.dstSet                 = m_descriptor_infos[eid * 3].descriptor_set;
                    descriptorset.descriptorType         = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    descriptorset.dstBinding             = 4;
                    descriptorset.pBufferInfo            = &aliveListBufferDescriptor;
                    descriptorset.descriptorCount        = 1;
                }

                RHIDescriptorBufferInfo deadListBufferDescriptor = {
                    m_emitter_buffer_batches[eid].m_dead_list_buffer, 0, RHI_WHOLE_SIZE};
                {
                    RHIWriteDescriptorSet& descriptorset = computeWriteDescriptorSets[5];
                    descriptorset.sType                  = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorset.dstSet                 = m_descriptor_infos[eid * 3].descriptor_set;
                    descriptorset.descriptorType         = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    descriptorset.dstBinding             = 5;
                    descriptorset.pBufferInfo            = &deadListBufferDescriptor;
                    descriptorset.descriptorCount        = 1;
                }

                RHIDescriptorBufferInfo aliveListNextBufferDescriptor = {
                    m_emitter_buffer_batches[eid].m_alive_list_next_buffer, 0, RHI_WHOLE_SIZE};
                {
                    RHIWriteDescriptorSet& descriptorset = computeWriteDescriptorSets[6];
                    descriptorset.sType                  = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorset.dstSet                 = m_descriptor_infos[eid * 3].descriptor_set;
                    descriptorset.descriptorType         = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    descriptorset.dstBinding             = 6;
                    descriptorset.pBufferInfo            = &aliveListNextBufferDescriptor;
                    descriptorset.descriptorCount        = 1;
                }

                RHIDescriptorBufferInfo particleComponentResBufferDescriptor = {
                    m_emitter_buffer_batches[eid].m_particle_component_res_buffer, 0, RHI_WHOLE_SIZE};
                {
                    RHIWriteDescriptorSet& descriptorset = computeWriteDescriptorSets[7];
                    descriptorset.sType                  = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorset.dstSet                 = m_descriptor_infos[eid * 3].descriptor_set;
                    descriptorset.descriptorType         = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    descriptorset.dstBinding             = 7;
                    descriptorset.pBufferInfo            = &particleComponentResBufferDescriptor;
                    descriptorset.descriptorCount        = 1;
                }

                RHIDescriptorBufferInfo particleSceneUniformBufferDescriptor = {
                    m_scene_uniform_buffer, 0, RHI_WHOLE_SIZE};
                {
                    RHIWriteDescriptorSet& descriptorset = computeWriteDescriptorSets[8];
                    descriptorset.sType                  = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorset.dstSet                 = m_descriptor_infos[eid * 3].descriptor_set;
                    descriptorset.descriptorType         = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    descriptorset.dstBinding             = 8;
                    descriptorset.pBufferInfo            = &particleSceneUniformBufferDescriptor;
                    descriptorset.descriptorCount        = 1;
                }

                RHIDescriptorBufferInfo positionRenderbufferDescriptor = {
                    m_emitter_buffer_batches[eid].m_position_render_buffer, 0, RHI_WHOLE_SIZE};
                {
                    RHIWriteDescriptorSet& descriptorset = computeWriteDescriptorSets[9];
                    descriptorset.sType                  = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorset.dstSet                 = m_descriptor_infos[eid * 3].descriptor_set;
                    descriptorset.descriptorType         = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    descriptorset.dstBinding             = 9;
                    descriptorset.pBufferInfo            = &positionRenderbufferDescriptor;
                    descriptorset.descriptorCount        = 1;
                }

                RHISampler*          sampler = nullptr;
                RHISamplerCreateInfo samplerCreateInfo {};
                samplerCreateInfo.sType            = RHI_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                samplerCreateInfo.maxAnisotropy    = 1.0f;
                samplerCreateInfo.anisotropyEnable = true;
                samplerCreateInfo.magFilter        = RHI_FILTER_LINEAR;
                samplerCreateInfo.minFilter        = RHI_FILTER_LINEAR;
                samplerCreateInfo.mipmapMode       = RHI_SAMPLER_MIPMAP_MODE_LINEAR;
                samplerCreateInfo.addressModeU     = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerCreateInfo.addressModeV     = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerCreateInfo.addressModeW     = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerCreateInfo.mipLodBias       = 0.0f;
                samplerCreateInfo.compareOp        = RHI_COMPARE_OP_NEVER;
                samplerCreateInfo.minLod           = 0.0f;
                samplerCreateInfo.maxLod           = 0.0f;
                samplerCreateInfo.borderColor      = RHI_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

                if (RHI_SUCCESS != m_rhi->createSampler(&samplerCreateInfo, sampler))
                {
                    throw std::runtime_error("create sampler error");
                }
                m_owned_samplers.push_back(sampler);

                RHIDescriptorImageInfo piccolo_texture_image_info = {};
                piccolo_texture_image_info.sampler                = sampler;
                piccolo_texture_image_info.imageView              = m_piccolo_logo_texture_image_view;
                piccolo_texture_image_info.imageLayout            = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                {
                    RHIWriteDescriptorSet& descriptorset = computeWriteDescriptorSets[10];
                    descriptorset.sType                  = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    descriptorset.dstSet                 = m_descriptor_infos[eid * 3].descriptor_set;
                    descriptorset.descriptorType         = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    descriptorset.dstBinding             = 10;
                    descriptorset.pImageInfo             = &piccolo_texture_image_info;
                    descriptorset.descriptorCount        = 1;
                }

                m_rhi->updateDescriptorSets(static_cast<uint32_t>(computeWriteDescriptorSets.size()),
                                            computeWriteDescriptorSets.data(),
                                            0,
                                            NULL);
            }
            {
                RHIWriteDescriptorSet descriptor_input_attachment_writes_info[2] = {{}, {}};

                RHIDescriptorImageInfo gbuffer_normal_descriptor_image_info = {};
                gbuffer_normal_descriptor_image_info.sampler                = nullptr;
                gbuffer_normal_descriptor_image_info.imageView              = m_src_normal_image_view;
                gbuffer_normal_descriptor_image_info.imageLayout            = RHI_IMAGE_LAYOUT_GENERAL;
                {

                    RHIWriteDescriptorSet& gbuffer_normal_descriptor_input_attachment_write_info =
                        descriptor_input_attachment_writes_info[0];
                    gbuffer_normal_descriptor_input_attachment_write_info.sType =
                        RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    gbuffer_normal_descriptor_input_attachment_write_info.pNext = NULL;
                    gbuffer_normal_descriptor_input_attachment_write_info.dstSet =
                        m_descriptor_infos[eid * 3 + 1].descriptor_set;
                    gbuffer_normal_descriptor_input_attachment_write_info.dstBinding      = 0;
                    gbuffer_normal_descriptor_input_attachment_write_info.dstArrayElement = 0;
                    gbuffer_normal_descriptor_input_attachment_write_info.descriptorType =
                        RHI_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    gbuffer_normal_descriptor_input_attachment_write_info.descriptorCount = 1;
                    gbuffer_normal_descriptor_input_attachment_write_info.pImageInfo =
                        &gbuffer_normal_descriptor_image_info;
                }

                RHISampler*          sampler = nullptr;
                RHISamplerCreateInfo samplerCreateInfo {};
                samplerCreateInfo.sType            = RHI_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                samplerCreateInfo.maxAnisotropy    = 1.0f;
                samplerCreateInfo.anisotropyEnable = true;
                samplerCreateInfo.magFilter        = RHI_FILTER_NEAREST;
                samplerCreateInfo.minFilter        = RHI_FILTER_NEAREST;
                samplerCreateInfo.mipmapMode       = RHI_SAMPLER_MIPMAP_MODE_LINEAR;
                samplerCreateInfo.addressModeU     = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerCreateInfo.addressModeV     = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerCreateInfo.addressModeW     = RHI_SAMPLER_ADDRESS_MODE_REPEAT;
                samplerCreateInfo.mipLodBias       = 0.0f;
                samplerCreateInfo.compareOp        = RHI_COMPARE_OP_NEVER;
                samplerCreateInfo.minLod           = 0.0f;
                samplerCreateInfo.maxLod           = 0.0f;
                samplerCreateInfo.borderColor      = RHI_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
                if (RHI_SUCCESS != m_rhi->createSampler(&samplerCreateInfo, sampler))
                {
                    throw std::runtime_error("create sampler error");
                }
                m_owned_samplers.push_back(sampler);

                RHIDescriptorImageInfo depth_descriptor_image_info = {};
                depth_descriptor_image_info.sampler                = sampler;
                depth_descriptor_image_info.imageView              = m_src_depth_image_view;
                depth_descriptor_image_info.imageLayout            = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                {
                    RHIWriteDescriptorSet& depth_descriptor_input_attachment_write_info =
                        descriptor_input_attachment_writes_info[1];
                    depth_descriptor_input_attachment_write_info.sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    depth_descriptor_input_attachment_write_info.pNext = NULL;
                    depth_descriptor_input_attachment_write_info.dstSet =
                        m_descriptor_infos[eid * 3 + 1].descriptor_set;
                    depth_descriptor_input_attachment_write_info.dstBinding      = 1;
                    depth_descriptor_input_attachment_write_info.dstArrayElement = 0;
                    depth_descriptor_input_attachment_write_info.descriptorType =
                        RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    depth_descriptor_input_attachment_write_info.descriptorCount = 1;
                    depth_descriptor_input_attachment_write_info.pImageInfo      = &depth_descriptor_image_info;
                }

                m_rhi->updateDescriptorSets(sizeof(descriptor_input_attachment_writes_info) /
                                                sizeof(descriptor_input_attachment_writes_info[0]),
                                            descriptor_input_attachment_writes_info,
                                            0,
                                            NULL);
            }
        }
    }

    void ParticlePass::simulate()
    {
        auto read_counter = [&](ParticleEmitterID emitter_index, RHIDeviceMemory* readback_memory) {
            if (emitter_index >= m_emitter_buffer_batches.size() || readback_memory == nullptr)
            {
                return;
            }

            void* mapped = nullptr;
            m_rhi->mapMemory(readback_memory, 0, RHI_WHOLE_SIZE, 0, &mapped);

            m_rhi->invalidateMappedMemoryRanges(nullptr, readback_memory, 0, RHI_WHOLE_SIZE);

            ParticleCounter counterNext {};
            memcpy(&counterNext, mapped, sizeof(ParticleCounter));
            m_rhi->unmapMemory(readback_memory);

            if constexpr (s_verbose_particle_alive_info)
                LOG_INFO("{} {} {} {}",
                         counterNext.dead_count,
                         counterNext.alive_count,
                         counterNext.alive_count_after_sim,
                         counterNext.emit_count);
            m_emitter_buffer_batches[emitter_index].m_num_particle = counterNext.alive_count_after_sim;
        };

        auto record_emitter_compute = [&](ParticleEmitterID i, RHICommandBuffer* command_buffer, RHIBuffer* readback_buffer) {
            if (i >= m_emitter_buffer_batches.size() || command_buffer == nullptr || readback_buffer == nullptr)
            {
                return;
            }

            float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            m_rhi->pushEvent(command_buffer, "Particle compute", color);
            m_rhi->pushEvent(command_buffer, "Particle Kickoff", color);

            m_rhi->cmdBindPipelinePFN(command_buffer, RHI_PIPELINE_BIND_POINT_COMPUTE, m_kickoff_pipeline);
            RHIDescriptorSet* descriptorsets[2] = {m_descriptor_infos[i * 3].descriptor_set,
                                                   m_descriptor_infos[i * 3 + 1].descriptor_set};
            m_rhi->cmdBindDescriptorSetsPFN(command_buffer,
                                            RHI_PIPELINE_BIND_POINT_COMPUTE,
                                            m_render_pipelines[0].layout,
                                            0,
                                            2,
                                            descriptorsets,
                                            0,
                                            0);


            m_rhi->cmdDispatch(command_buffer, 1, 1, 1);

            m_rhi->popEvent(command_buffer); // end particle kickoff label

            RHIBufferMemoryBarrier bufferBarrier {};
            bufferBarrier.sType               = RHI_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            bufferBarrier.buffer              = m_emitter_buffer_batches[i].m_counter_device_buffer;
            bufferBarrier.size                = RHI_WHOLE_SIZE;
            bufferBarrier.srcAccessMask       = RHI_ACCESS_SHADER_WRITE_BIT;
            bufferBarrier.dstAccessMask       = RHI_ACCESS_SHADER_READ_BIT;
            bufferBarrier.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
            bufferBarrier.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;

            m_rhi->cmdPipelineBarrier(command_buffer,
                                      RHI_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      1,
                                      &bufferBarrier,
                                      0,
                                      nullptr);

            bufferBarrier.buffer              = m_emitter_buffer_batches[i].m_indirect_dispatch_argument_buffer;
            bufferBarrier.size                = RHI_WHOLE_SIZE;
            bufferBarrier.srcAccessMask       = RHI_ACCESS_SHADER_WRITE_BIT;
            bufferBarrier.dstAccessMask       = RHI_ACCESS_INDIRECT_COMMAND_READ_BIT | RHI_ACCESS_SHADER_READ_BIT;
            bufferBarrier.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
            bufferBarrier.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;

            m_rhi->cmdPipelineBarrier(command_buffer,
                                      RHI_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      1,
                                      &bufferBarrier,
                                      0,
                                      nullptr);

            m_rhi->pushEvent(command_buffer, "Particle Emit", color);

            m_rhi->cmdBindPipelinePFN(command_buffer, RHI_PIPELINE_BIND_POINT_COMPUTE, m_emit_pipeline);

            m_rhi->cmdDispatchIndirect(command_buffer,
                                       m_emitter_buffer_batches[i].m_indirect_dispatch_argument_buffer,
                                       s_argument_offset_emit);

            m_rhi->popEvent(command_buffer); // end particle emit label

            bufferBarrier.buffer              = m_emitter_buffer_batches[i].m_position_device_buffer;
            bufferBarrier.size                = RHI_WHOLE_SIZE;
            bufferBarrier.srcAccessMask       = RHI_ACCESS_SHADER_WRITE_BIT;
            bufferBarrier.dstAccessMask       = RHI_ACCESS_SHADER_READ_BIT;
            bufferBarrier.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
            bufferBarrier.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;

            m_rhi->cmdPipelineBarrier(command_buffer,
                                      RHI_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      1,
                                      &bufferBarrier,
                                      0,
                                      nullptr);

            bufferBarrier.buffer              = m_emitter_buffer_batches[i].m_position_render_buffer;
            bufferBarrier.size                = RHI_WHOLE_SIZE;
            bufferBarrier.srcAccessMask       = RHI_ACCESS_SHADER_WRITE_BIT;
            bufferBarrier.dstAccessMask       = RHI_ACCESS_SHADER_READ_BIT;
            bufferBarrier.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
            bufferBarrier.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;

            m_rhi->cmdPipelineBarrier(command_buffer,
                                      RHI_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      1,
                                      &bufferBarrier,
                                      0,
                                      nullptr);

            bufferBarrier.buffer              = m_emitter_buffer_batches[i].m_counter_device_buffer;
            bufferBarrier.size                = RHI_WHOLE_SIZE;
            bufferBarrier.srcAccessMask       = RHI_ACCESS_SHADER_WRITE_BIT;
            bufferBarrier.dstAccessMask       = RHI_ACCESS_SHADER_READ_BIT;
            bufferBarrier.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
            bufferBarrier.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;

            m_rhi->cmdPipelineBarrier(command_buffer,
                                      RHI_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      1,
                                      &bufferBarrier,
                                      0,
                                      nullptr);

            bufferBarrier.buffer              = m_emitter_buffer_batches[i].m_alive_list_buffer;
            bufferBarrier.size                = RHI_WHOLE_SIZE;
            bufferBarrier.srcAccessMask       = RHI_ACCESS_SHADER_WRITE_BIT;
            bufferBarrier.dstAccessMask       = RHI_ACCESS_SHADER_READ_BIT;
            bufferBarrier.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
            bufferBarrier.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;

            m_rhi->cmdPipelineBarrier(command_buffer,
                                      RHI_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      1,
                                      &bufferBarrier,
                                      0,
                                      nullptr);

            bufferBarrier.buffer              = m_emitter_buffer_batches[i].m_dead_list_buffer;
            bufferBarrier.size                = RHI_WHOLE_SIZE;
            bufferBarrier.srcAccessMask       = RHI_ACCESS_SHADER_WRITE_BIT;
            bufferBarrier.dstAccessMask       = RHI_ACCESS_SHADER_READ_BIT;
            bufferBarrier.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
            bufferBarrier.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;

            m_rhi->cmdPipelineBarrier(command_buffer,
                                      RHI_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      1,
                                      &bufferBarrier,
                                      0,
                                      nullptr);

            bufferBarrier.buffer              = m_emitter_buffer_batches[i].m_alive_list_next_buffer;
            bufferBarrier.size                = RHI_WHOLE_SIZE;
            bufferBarrier.srcAccessMask       = RHI_ACCESS_SHADER_WRITE_BIT;
            bufferBarrier.dstAccessMask       = RHI_ACCESS_SHADER_READ_BIT;
            bufferBarrier.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
            bufferBarrier.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;

            m_rhi->cmdPipelineBarrier(command_buffer,
                                      RHI_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                      RHI_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      1,
                                      &bufferBarrier,
                                      0,
                                      nullptr);

            bufferBarrier.buffer              = m_emitter_buffer_batches[i].m_indirect_dispatch_argument_buffer;
            bufferBarrier.size                = RHI_WHOLE_SIZE;
            bufferBarrier.srcAccessMask       = RHI_ACCESS_INDIRECT_COMMAND_READ_BIT;
            bufferBarrier.dstAccessMask       = RHI_ACCESS_INDIRECT_COMMAND_READ_BIT;
            bufferBarrier.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
            bufferBarrier.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;

            m_rhi->cmdPipelineBarrier(command_buffer,
                                      RHI_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                                      RHI_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      1,
                                      &bufferBarrier,
                                      0,
                                      nullptr);

            m_rhi->pushEvent(command_buffer, "Particle Simulate", color);

            m_rhi->cmdBindPipelinePFN(command_buffer, RHI_PIPELINE_BIND_POINT_COMPUTE, m_simulate_pipeline);
            m_rhi->cmdDispatchIndirect(command_buffer,
                                       m_emitter_buffer_batches[i].m_indirect_dispatch_argument_buffer,
                                       s_argument_offset_simulate);

            m_rhi->popEvent(command_buffer); // end particle simulate label

            m_rhi->pushEvent(command_buffer, "Copy Particle Counter Buffer", color);

            bufferBarrier.srcAccessMask       = RHI_ACCESS_SHADER_WRITE_BIT;
            bufferBarrier.dstAccessMask       = RHI_ACCESS_TRANSFER_READ_BIT;
            bufferBarrier.buffer              = m_emitter_buffer_batches[i].m_counter_device_buffer;
            bufferBarrier.size                = RHI_WHOLE_SIZE;
            bufferBarrier.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
            bufferBarrier.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;

            m_rhi->cmdPipelineBarrier(command_buffer,
                                      RHI_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                      RHI_PIPELINE_STAGE_TRANSFER_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      1,
                                      &bufferBarrier,
                                      0,
                                      nullptr);

            RHIBufferCopy copyRegion {};
            copyRegion.srcOffset = 0;
            copyRegion.dstOffset = 0;
            copyRegion.size      = sizeof(ParticleCounter);

            m_rhi->cmdCopyBuffer(command_buffer,
                                 m_emitter_buffer_batches[i].m_counter_device_buffer,
                                 readback_buffer,
                                 1,
                                 &copyRegion);

            bufferBarrier.srcAccessMask       = RHI_ACCESS_TRANSFER_WRITE_BIT;
            bufferBarrier.dstAccessMask       = RHI_ACCESS_HOST_READ_BIT;
            bufferBarrier.buffer              = readback_buffer;
            bufferBarrier.size                = RHI_WHOLE_SIZE;
            bufferBarrier.srcQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;
            bufferBarrier.dstQueueFamilyIndex = RHI_QUEUE_FAMILY_IGNORED;

            m_rhi->cmdPipelineBarrier(command_buffer,
                                      RHI_PIPELINE_STAGE_TRANSFER_BIT,
                                      RHI_PIPELINE_STAGE_HOST_BIT,
                                      0,
                                      0,
                                      nullptr,
                                      1,
                                      &bufferBarrier,
                                      0,
                                      nullptr);

            m_rhi->popEvent(command_buffer); // end particle counter copy label

            m_rhi->popEvent(command_buffer); // end particle compute label
        };

        if (m_rhi->usesDedicatedComputeSubmission())
        {
            const uint8_t frame_index = m_rhi->getCurrentFrameIndex() % m_rhi->getMaxFramesInFlight();
            RHIFence*     compute_fence = m_compute_fences[frame_index];
            if (m_compute_readback_pending[frame_index])
            {
                if (RHI_SUCCESS != m_rhi->waitForFencesPFN(1, &compute_fence, RHI_TRUE, UINT64_MAX))
                {
                    throw std::runtime_error("wait for D3D12 particle compute fence");
                }

                for (ParticleEmitterID emitter_index : m_compute_readback_emitters[frame_index])
                {
                    if (emitter_index < m_emitter_buffer_batches.size() &&
                        frame_index < m_emitter_buffer_batches[emitter_index].m_counter_readback_memories.size())
                    {
                        read_counter(emitter_index,
                                     m_emitter_buffer_batches[emitter_index]
                                         .m_counter_readback_memories[frame_index]);
                    }
                }
                m_compute_readback_pending[frame_index] = false;
                m_compute_readback_emitters[frame_index].clear();
            }

            if (!m_emitter_tick_indices.empty())
            {
                RHICommandBuffer* command_buffer = m_compute_command_buffers[frame_index];
                RHICommandBufferBeginInfo cmdBufInfo {};
                cmdBufInfo.sType = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                if (RHI_SUCCESS != m_rhi->beginCommandBuffer(command_buffer, &cmdBufInfo))
                {
                    throw std::runtime_error("begin D3D12 particle compute command buffer");
                }

                for (ParticleEmitterID emitter_index : m_emitter_tick_indices)
                {
                    if (emitter_index >= m_emitter_buffer_batches.size() ||
                        frame_index >= m_emitter_buffer_batches[emitter_index].m_counter_readback_buffers.size())
                    {
                        continue;
                    }

                    record_emitter_compute(emitter_index,
                                           command_buffer,
                                           m_emitter_buffer_batches[emitter_index]
                                               .m_counter_readback_buffers[frame_index]);
                }

                if (RHI_SUCCESS != m_rhi->endCommandBuffer(command_buffer))
                {
                    throw std::runtime_error("end D3D12 particle compute command buffer");
                }

                if (RHI_SUCCESS != m_rhi->resetFencesPFN(1, &compute_fence))
                {
                    throw std::runtime_error("reset D3D12 particle compute fence");
                }

                RHISubmitInfo computeSubmitInfo {};
                const RHIPipelineStageFlags waitStageMask = RHI_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                RHISemaphore*               copy_done_semaphore =
                    m_rhi->getCopyDoneSemaphore(m_rhi->getLastSubmittedFrameIndex());
                computeSubmitInfo.sType                  = RHI_STRUCTURE_TYPE_SUBMIT_INFO;
                computeSubmitInfo.waitSemaphoreCount       = 1;
                computeSubmitInfo.pWaitSemaphores          = &copy_done_semaphore;
                computeSubmitInfo.pWaitDstStageMask      = &waitStageMask;
                computeSubmitInfo.commandBufferCount     = 1;
                computeSubmitInfo.pCommandBuffers        = &command_buffer;

                if (RHI_SUCCESS != m_rhi->queueSubmit(m_rhi->getComputeQueue(), 1, &computeSubmitInfo, compute_fence))
                {
                    throw std::runtime_error("D3D12 particle compute queue submit");
                }

                m_compute_readback_pending[frame_index] = true;
                m_compute_readback_emitters[frame_index] = m_emitter_tick_indices;
            }

            m_emitter_tick_indices.clear();
            m_emitter_transform_indices.clear();
            return;
        }

        for (auto i : m_emitter_tick_indices)
        {
            RHICommandBufferBeginInfo cmdBufInfo {};
            cmdBufInfo.sType = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

            // particle compute pass
            if (RHI_SUCCESS != m_rhi->beginCommandBuffer(m_compute_command_buffer, &cmdBufInfo))
            {
                throw std::runtime_error("begin command buffer");
            }

            record_emitter_compute(i, m_compute_command_buffer, m_emitter_buffer_batches[i].m_counter_readback_buffer);

            if (RHI_SUCCESS != m_rhi->endCommandBuffer(m_compute_command_buffer))
            {
                throw std::runtime_error("end command buffer");
            }

            // Submit compute work
            m_rhi->resetFencesPFN(1, &m_fence);
            RHISubmitInfo computeSubmitInfo {};
            const RHIPipelineStageFlags waitStageMask = RHI_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            RHISemaphore*               copy_done_semaphore =
                m_rhi->getCopyDoneSemaphore(m_rhi->getLastSubmittedFrameIndex());
            computeSubmitInfo.sType                  = RHI_STRUCTURE_TYPE_SUBMIT_INFO;
            computeSubmitInfo.waitSemaphoreCount       = 1;
            computeSubmitInfo.pWaitSemaphores          = &copy_done_semaphore;
            computeSubmitInfo.pWaitDstStageMask      = &waitStageMask;
            computeSubmitInfo.commandBufferCount     = 1;
            computeSubmitInfo.pCommandBuffers        = &m_compute_command_buffer;

            if (RHI_SUCCESS != m_rhi->queueSubmit(m_rhi->getComputeQueue(), 1, &computeSubmitInfo, m_fence))
            {
                throw std::runtime_error("compute queue submit");
            }

            if (RHI_SUCCESS != m_rhi->waitForFencesPFN(1, &m_fence, RHI_TRUE, UINT64_MAX))
            {
                throw std::runtime_error("wait for fence");
            }

            read_counter(i, m_emitter_buffer_batches[i].m_counter_readback_memory);
        }
        m_emitter_tick_indices.clear();
        m_emitter_transform_indices.clear();
    }

    void ParticlePass::prepareUniformBuffer()
    {
        m_rhi->createBuffer(sizeof(m_particle_collision_perframe_storage_buffer_object),
                            RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            m_scene_uniform_buffer,
                            m_scene_uniform_memory);

        if (RHI_SUCCESS != m_rhi->mapMemory(m_scene_uniform_memory, 0, RHI_WHOLE_SIZE, 0, &m_scene_uniform_buffer_mapped))
        {
            throw std::runtime_error("map billboard uniform buffer");
        }

        m_rhi->createBufferAndInitialize(RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                         RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                         m_compute_uniform_buffer,
                                         m_compute_uniform_memory,
                                         sizeof(m_ubo));

        if (RHI_SUCCESS !=
            m_rhi->mapMemory(m_compute_uniform_memory, 0, RHI_WHOLE_SIZE, 0, &m_particle_compute_buffer_mapped))
        {
            throw std::runtime_error("map buffer");
        }

        const GlobalParticleRes& global_res = m_particle_manager->getGlobalParticleRes();

        m_ubo.emit_gap  = global_res.m_emit_gap;
        m_ubo.time_step = global_res.m_time_step;
        m_ubo.max_life  = global_res.m_max_life;
        m_ubo.gravity   = global_res.m_gravity;
        std::random_device r;
        std::seed_seq      seed {r()};
        m_random_engine.seed(seed);
        float rnd0        = m_random_engine.uniformDistribution<float>(0, 1000) * 0.001f;
        float rnd1        = m_random_engine.uniformDistribution<float>(0, 1000) * 0.001f;
        float rnd2        = m_random_engine.uniformDistribution<float>(0, 1000) * 0.001f;
        m_ubo.pack        = Vector4 {rnd0, static_cast<float>(m_rhi->getCurrentFrameIndex()), rnd1, rnd2};
        m_ubo.xemit_count = 100000;

        m_viewport_params = *m_rhi->getSwapchainInfo().viewport;
        m_ubo.viewport.x  = m_viewport_params.x;
        m_ubo.viewport.y  = m_viewport_params.y;
        m_ubo.viewport.z  = m_viewport_params.width;
        m_ubo.viewport.w  = m_viewport_params.height;
        m_ubo.extent.x    = m_rhi->getSwapchainInfo().scissor->extent.width;
        m_ubo.extent.y    = m_rhi->getSwapchainInfo().scissor->extent.height;

        memcpy(m_particle_compute_buffer_mapped, &m_ubo, sizeof(m_ubo));

        {
            m_rhi->createBuffer(sizeof(m_particlebillboard_perframe_storage_buffer_object),
                                RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                m_particle_billboard_uniform_buffer,
                                m_particle_billboard_uniform_memory);

            if (RHI_SUCCESS != m_rhi->mapMemory(m_particle_billboard_uniform_memory,
                                                0,
                                                RHI_WHOLE_SIZE,
                                                0,
                                                &m_particle_billboard_uniform_buffer_mapped))
            {
                throw std::runtime_error("map billboard uniform buffer");
            }
        }
    }

    void ParticlePass::updateEmitterTransform()
    {
        for (ParticleEmitterTransformDesc& transform_desc : m_emitter_transform_indices)
        {
            int index                                                 = transform_desc.m_id;
            m_emitter_buffer_batches[index].m_emitter_desc.m_position = transform_desc.m_position;
            m_emitter_buffer_batches[index].m_emitter_desc.m_rotation = transform_desc.m_rotation;

            memcpy(m_emitter_buffer_batches[index].m_emitter_desc_mapped,
                   &m_emitter_buffer_batches[index].m_emitter_desc,
                   sizeof(ParticleEmitterDesc));
            m_rhi->flushMappedMemoryRanges(nullptr,
                                           m_emitter_buffer_batches[index].m_particle_component_res_memory,
                                           0,
                                           sizeof(ParticleEmitterDesc));
        }
    }

    void ParticlePass::updateUniformBuffer()
    {
        std::random_device r;
        std::seed_seq      seed {r()};
        m_random_engine.seed(seed);
        float rnd0 = m_random_engine.uniformDistribution<float>(0, 1000) * 0.001f;
        float rnd1 = m_random_engine.uniformDistribution<float>(0, 1000) * 0.001f;
        float rnd2 = m_random_engine.uniformDistribution<float>(0, 1000) * 0.001f;
        m_ubo.pack = Vector4 {rnd0, rnd1, rnd2, static_cast<float>(m_rhi->getCurrentFrameIndex())};

        m_ubo.viewport.x = m_rhi->getSwapchainInfo().viewport->x;
        m_ubo.viewport.y = m_rhi->getSwapchainInfo().viewport->y;
        m_ubo.viewport.z = m_rhi->getSwapchainInfo().viewport->width;
        m_ubo.viewport.w = m_rhi->getSwapchainInfo().viewport->height;
        m_ubo.extent.x   = m_rhi->getSwapchainInfo().scissor->extent.width;
        m_ubo.extent.y   = m_rhi->getSwapchainInfo().scissor->extent.height;

        m_ubo.extent.z = g_runtime_global_context.m_render_system->getRenderCamera()->m_znear;
        m_ubo.extent.w = g_runtime_global_context.m_render_system->getRenderCamera()->m_zfar;
        memcpy(m_particle_compute_buffer_mapped, &m_ubo, sizeof(m_ubo));
    }

    void ParticlePass::preparePassData(std::shared_ptr<RenderResourceBase> render_resource)
    {
        const RenderResource* render_resource_ptr = static_cast<const RenderResource*>(render_resource.get());
        if (render_resource_ptr)
        {
            m_particle_collision_perframe_storage_buffer_object =
                render_resource_ptr->m_particle_collision_perframe_storage_buffer_object;
            memcpy(m_scene_uniform_buffer_mapped,
                   &m_particle_collision_perframe_storage_buffer_object,
                   sizeof(ParticleCollisionPerframeStorageBufferObject));

            m_particlebillboard_perframe_storage_buffer_object =
                render_resource_ptr->m_particlebillboard_perframe_storage_buffer_object;
            memcpy(m_particle_billboard_uniform_buffer_mapped,
                   &m_particlebillboard_perframe_storage_buffer_object,
                   sizeof(m_particlebillboard_perframe_storage_buffer_object));

            m_viewport_params = *m_rhi->getSwapchainInfo().viewport;
            updateUniformBuffer();
            updateEmitterTransform();
        }
    }

    void ParticlePass::setDepthAndNormalImage(RHIImage* depth_image, RHIImage* normal_image)
    {
        m_src_depth_image  = depth_image;
        m_src_normal_image = normal_image;
    }

    void ParticlePass::setRenderCommandBufferHandle(RHICommandBuffer* command_buffer)
    {
        m_render_command_buffer = command_buffer;
    }

    void ParticlePass::setRenderPassHandle(RHIRenderPass* render_pass) { m_render_pass = render_pass; }

    void ParticlePass::setTickIndices(const std::vector<ParticleEmitterID>& tick_indices)
    {
        m_emitter_tick_indices = tick_indices;
    }

    void ParticlePass::setTransformIndices(const std::vector<ParticleEmitterTransformDesc>& transform_indices)
    {
        m_emitter_transform_indices = transform_indices;
    }
} // namespace Piccolo
