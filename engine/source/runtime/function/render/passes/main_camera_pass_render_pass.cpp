#include "runtime/function/render/passes/main_camera_pass_render_pass.h"

#include "runtime/function/render/interface/rhi.h"

#include <stdexcept>

namespace Piccolo
{
    namespace
    {
        struct ColorAttachmentOps
        {
            RHIAttachmentLoadOp  load_op;
            RHIAttachmentStoreOp store_op;
            RHIImageLayout       initial_layout;
        };

        void initColorAttachment(RHIAttachmentDescription& attachment,
                                 RHIFormat                 format,
                                 const ColorAttachmentOps& ops)
        {
            attachment.format         = format;
            attachment.samples        = RHI_SAMPLE_COUNT_1_BIT;
            attachment.loadOp         = ops.load_op;
            attachment.storeOp        = ops.store_op;
            attachment.stencilLoadOp  = RHI_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = RHI_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout  = ops.initial_layout;
            attachment.finalLayout    = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        void fillMainCameraAttachments(RHIAttachmentDescription*     attachments,
                                       const RenderPass::Framebuffer& framebuffer,
                                       RHI*                           rhi,
                                       MainCameraRenderPassKind       kind)
        {
            const ColorAttachmentOps raster_gbuffer_a_ops {RHI_ATTACHMENT_LOAD_OP_CLEAR,
                                                           RHI_ATTACHMENT_STORE_OP_STORE,
                                                           RHI_IMAGE_LAYOUT_UNDEFINED};
            const ColorAttachmentOps raster_gbuffer_bc_ops {RHI_ATTACHMENT_LOAD_OP_CLEAR,
                                                            RHI_ATTACHMENT_STORE_OP_DONT_CARE,
                                                            RHI_IMAGE_LAYOUT_UNDEFINED};
            const ColorAttachmentOps raster_backup_ops {RHI_ATTACHMENT_LOAD_OP_CLEAR,
                                                      RHI_ATTACHMENT_STORE_OP_DONT_CARE,
                                                      RHI_IMAGE_LAYOUT_UNDEFINED};
            const ColorAttachmentOps pt_gbuffer_ops {RHI_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                     RHI_ATTACHMENT_STORE_OP_DONT_CARE,
                                                     RHI_IMAGE_LAYOUT_UNDEFINED};
            const ColorAttachmentOps pt_backup_odd_ops {RHI_ATTACHMENT_LOAD_OP_LOAD,
                                                        RHI_ATTACHMENT_STORE_OP_STORE,
                                                        RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            const ColorAttachmentOps pt_backup_even_ops {RHI_ATTACHMENT_LOAD_OP_CLEAR,
                                                         RHI_ATTACHMENT_STORE_OP_DONT_CARE,
                                                         RHI_IMAGE_LAYOUT_UNDEFINED};
            const ColorAttachmentOps post_process_ops {RHI_ATTACHMENT_LOAD_OP_CLEAR,
                                                       RHI_ATTACHMENT_STORE_OP_DONT_CARE,
                                                       RHI_IMAGE_LAYOUT_UNDEFINED};

            const bool is_pt_composite = kind == MainCameraRenderPassKind::PathTracingComposite;

            initColorAttachment(attachments[_main_camera_pass_gbuffer_a],
                                framebuffer.attachments[_main_camera_pass_gbuffer_a].format,
                                is_pt_composite ? pt_gbuffer_ops : raster_gbuffer_a_ops);
            initColorAttachment(attachments[_main_camera_pass_gbuffer_b],
                                framebuffer.attachments[_main_camera_pass_gbuffer_b].format,
                                is_pt_composite ? pt_gbuffer_ops : raster_gbuffer_bc_ops);
            initColorAttachment(attachments[_main_camera_pass_gbuffer_c],
                                framebuffer.attachments[_main_camera_pass_gbuffer_c].format,
                                is_pt_composite ? pt_gbuffer_ops : raster_gbuffer_bc_ops);
            initColorAttachment(attachments[_main_camera_pass_backup_buffer_odd],
                                framebuffer.attachments[_main_camera_pass_backup_buffer_odd].format,
                                is_pt_composite ? pt_backup_odd_ops : raster_backup_ops);
            initColorAttachment(attachments[_main_camera_pass_backup_buffer_even],
                                framebuffer.attachments[_main_camera_pass_backup_buffer_even].format,
                                is_pt_composite ? pt_backup_even_ops : raster_backup_ops);
            initColorAttachment(attachments[_main_camera_pass_post_process_buffer_odd],
                                framebuffer.attachments[_main_camera_pass_post_process_buffer_odd].format,
                                post_process_ops);
            initColorAttachment(attachments[_main_camera_pass_post_process_buffer_even],
                                framebuffer.attachments[_main_camera_pass_post_process_buffer_even].format,
                                post_process_ops);

            RHIAttachmentDescription& depth = attachments[_main_camera_pass_depth];
            depth.format         = rhi->getDepthImageInfo().depth_image_format;
            depth.samples        = RHI_SAMPLE_COUNT_1_BIT;
            depth.stencilLoadOp  = RHI_ATTACHMENT_LOAD_OP_DONT_CARE;
            depth.stencilStoreOp = RHI_ATTACHMENT_STORE_OP_DONT_CARE;
            depth.initialLayout  = RHI_IMAGE_LAYOUT_UNDEFINED;
            depth.finalLayout    = RHI_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            if (is_pt_composite)
            {
                depth.loadOp  = RHI_ATTACHMENT_LOAD_OP_DONT_CARE;
                depth.storeOp = RHI_ATTACHMENT_STORE_OP_DONT_CARE;
            }
            else
            {
                depth.loadOp  = RHI_ATTACHMENT_LOAD_OP_CLEAR;
                depth.storeOp = RHI_ATTACHMENT_STORE_OP_STORE;
            }

            RHIAttachmentDescription& swapchain = attachments[_main_camera_pass_swap_chain_image];
            swapchain.format         = rhi->getSwapchainInfo().image_format;
            swapchain.samples        = RHI_SAMPLE_COUNT_1_BIT;
            swapchain.loadOp         = RHI_ATTACHMENT_LOAD_OP_CLEAR;
            swapchain.storeOp        = RHI_ATTACHMENT_STORE_OP_STORE;
            swapchain.stencilLoadOp  = RHI_ATTACHMENT_LOAD_OP_DONT_CARE;
            swapchain.stencilStoreOp = RHI_ATTACHMENT_STORE_OP_DONT_CARE;
            swapchain.initialLayout  = RHI_IMAGE_LAYOUT_UNDEFINED;
            swapchain.finalLayout    = RHI_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }

        void fillRasterEarlySubpasses(RHISubpassDescription* subpasses)
        {
            RHIAttachmentReference base_pass_color_attachments_reference[3] = {};
            base_pass_color_attachments_reference[0].attachment = _main_camera_pass_gbuffer_a;
            base_pass_color_attachments_reference[0].layout     = RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            base_pass_color_attachments_reference[1].attachment = _main_camera_pass_gbuffer_b;
            base_pass_color_attachments_reference[1].layout     = RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            base_pass_color_attachments_reference[2].attachment = _main_camera_pass_gbuffer_c;
            base_pass_color_attachments_reference[2].layout     = RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            RHIAttachmentReference base_pass_depth_attachment_reference {};
            base_pass_depth_attachment_reference.attachment = _main_camera_pass_depth;
            base_pass_depth_attachment_reference.layout     = RHI_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            RHISubpassDescription& base_pass = subpasses[_main_camera_subpass_basepass];
            base_pass.pipelineBindPoint       = RHI_PIPELINE_BIND_POINT_GRAPHICS;
            base_pass.colorAttachmentCount    = 3;
            base_pass.pColorAttachments       = base_pass_color_attachments_reference;
            base_pass.pDepthStencilAttachment = &base_pass_depth_attachment_reference;

            RHIAttachmentReference deferred_lighting_pass_input_attachments_reference[4] = {};
            deferred_lighting_pass_input_attachments_reference[0].attachment = _main_camera_pass_gbuffer_a;
            deferred_lighting_pass_input_attachments_reference[0].layout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            deferred_lighting_pass_input_attachments_reference[1].attachment = _main_camera_pass_gbuffer_b;
            deferred_lighting_pass_input_attachments_reference[1].layout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            deferred_lighting_pass_input_attachments_reference[2].attachment = _main_camera_pass_gbuffer_c;
            deferred_lighting_pass_input_attachments_reference[2].layout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            deferred_lighting_pass_input_attachments_reference[3].attachment = _main_camera_pass_depth;
            deferred_lighting_pass_input_attachments_reference[3].layout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            RHIAttachmentReference deferred_lighting_pass_color_attachment_reference {};
            deferred_lighting_pass_color_attachment_reference.attachment = _main_camera_pass_backup_buffer_odd;
            deferred_lighting_pass_color_attachment_reference.layout     = RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            RHISubpassDescription& deferred_lighting_pass = subpasses[_main_camera_subpass_deferred_lighting];
            deferred_lighting_pass.pipelineBindPoint      = RHI_PIPELINE_BIND_POINT_GRAPHICS;
            deferred_lighting_pass.inputAttachmentCount   = 4;
            deferred_lighting_pass.pInputAttachments        = deferred_lighting_pass_input_attachments_reference;
            deferred_lighting_pass.colorAttachmentCount     = 1;
            deferred_lighting_pass.pColorAttachments        = &deferred_lighting_pass_color_attachment_reference;
        }

        void fillSharedLateSubpasses(RHISubpassDescription* subpasses, bool enable_fxaa)
        {
            for (uint32_t i = 0; i < _main_camera_subpass_count; ++i)
            {
                subpasses[i].pipelineBindPoint = RHI_PIPELINE_BIND_POINT_GRAPHICS;
            }

            RHIAttachmentReference forward_lighting_color {};
            forward_lighting_color.attachment = _main_camera_pass_backup_buffer_odd;
            forward_lighting_color.layout     = RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            RHIAttachmentReference forward_lighting_depth {};
            forward_lighting_depth.attachment = _main_camera_pass_depth;
            forward_lighting_depth.layout     = RHI_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            subpasses[_main_camera_subpass_forward_lighting].colorAttachmentCount    = 1;
            subpasses[_main_camera_subpass_forward_lighting].pColorAttachments       = &forward_lighting_color;
            subpasses[_main_camera_subpass_forward_lighting].pDepthStencilAttachment = &forward_lighting_depth;

            RHIAttachmentReference tone_mapping_input {};
            tone_mapping_input.attachment = _main_camera_pass_backup_buffer_odd;
            tone_mapping_input.layout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            RHIAttachmentReference tone_mapping_color {};
            tone_mapping_color.attachment = _main_camera_pass_backup_buffer_even;
            tone_mapping_color.layout     = RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            subpasses[_main_camera_subpass_tone_mapping].inputAttachmentCount = 1;
            subpasses[_main_camera_subpass_tone_mapping].pInputAttachments    = &tone_mapping_input;
            subpasses[_main_camera_subpass_tone_mapping].colorAttachmentCount = 1;
            subpasses[_main_camera_subpass_tone_mapping].pColorAttachments    = &tone_mapping_color;

            RHIAttachmentReference color_grading_input {};
            color_grading_input.attachment = _main_camera_pass_backup_buffer_even;
            color_grading_input.layout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            RHIAttachmentReference color_grading_color {};
            color_grading_color.attachment = enable_fxaa ? _main_camera_pass_post_process_buffer_odd :
                                                           _main_camera_pass_backup_buffer_odd;
            color_grading_color.layout     = RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            subpasses[_main_camera_subpass_color_grading].inputAttachmentCount = 1;
            subpasses[_main_camera_subpass_color_grading].pInputAttachments    = &color_grading_input;
            subpasses[_main_camera_subpass_color_grading].colorAttachmentCount = 1;
            subpasses[_main_camera_subpass_color_grading].pColorAttachments    = &color_grading_color;

            RHIAttachmentReference fxaa_input {};
            fxaa_input.attachment = enable_fxaa ? _main_camera_pass_post_process_buffer_odd :
                                                  _main_camera_pass_backup_buffer_even;
            fxaa_input.layout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            RHIAttachmentReference fxaa_color {};
            fxaa_color.attachment = _main_camera_pass_backup_buffer_odd;
            fxaa_color.layout     = RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            subpasses[_main_camera_subpass_fxaa].inputAttachmentCount = 1;
            subpasses[_main_camera_subpass_fxaa].pInputAttachments    = &fxaa_input;
            subpasses[_main_camera_subpass_fxaa].colorAttachmentCount = 1;
            subpasses[_main_camera_subpass_fxaa].pColorAttachments    = &fxaa_color;

            RHIAttachmentReference ui_color {};
            ui_color.attachment = _main_camera_pass_backup_buffer_even;
            ui_color.layout     = RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            uint32_t ui_preserve_attachment = _main_camera_pass_backup_buffer_odd;
            subpasses[_main_camera_subpass_ui].colorAttachmentCount    = 1;
            subpasses[_main_camera_subpass_ui].pColorAttachments       = &ui_color;
            subpasses[_main_camera_subpass_ui].preserveAttachmentCount = 1;
            subpasses[_main_camera_subpass_ui].pPreserveAttachments    = &ui_preserve_attachment;

            RHIAttachmentReference combine_inputs[2] {};
            combine_inputs[0].attachment = _main_camera_pass_backup_buffer_odd;
            combine_inputs[0].layout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            combine_inputs[1].attachment = _main_camera_pass_backup_buffer_even;
            combine_inputs[1].layout     = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            RHIAttachmentReference combine_color {};
            combine_color.attachment = _main_camera_pass_swap_chain_image;
            combine_color.layout     = RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            subpasses[_main_camera_subpass_combine_ui].inputAttachmentCount = 2;
            subpasses[_main_camera_subpass_combine_ui].pInputAttachments    = combine_inputs;
            subpasses[_main_camera_subpass_combine_ui].colorAttachmentCount = 1;
            subpasses[_main_camera_subpass_combine_ui].pColorAttachments    = &combine_color;
        }

        void fillRasterDependencies(RHISubpassDependency* dependencies)
        {
            dependencies[0].srcSubpass      = RHI_SUBPASS_EXTERNAL;
            dependencies[0].dstSubpass      = _main_camera_subpass_deferred_lighting;
            dependencies[0].srcStageMask    = RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[0].dstStageMask      = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dependencies[0].srcAccessMask     = RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[0].dstAccessMask     = RHI_ACCESS_SHADER_READ_BIT;
            dependencies[0].dependencyFlags   = 0;

            dependencies[1].srcSubpass      = _main_camera_subpass_basepass;
            dependencies[1].dstSubpass      = _main_camera_subpass_deferred_lighting;
            dependencies[1].srcStageMask    = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                              RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[1].dstStageMask    = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                              RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[1].srcAccessMask   = RHI_ACCESS_SHADER_WRITE_BIT | RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[1].dstAccessMask     = RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            dependencies[1].dependencyFlags = RHI_DEPENDENCY_BY_REGION_BIT;

            dependencies[2].srcSubpass      = _main_camera_subpass_deferred_lighting;
            dependencies[2].dstSubpass      = _main_camera_subpass_forward_lighting;
            dependencies[2].srcStageMask    = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                              RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[2].dstStageMask    = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                              RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[2].srcAccessMask   = RHI_ACCESS_SHADER_WRITE_BIT | RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[2].dstAccessMask     = RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            dependencies[2].dependencyFlags = RHI_DEPENDENCY_BY_REGION_BIT;

            dependencies[3].srcSubpass      = _main_camera_subpass_forward_lighting;
            dependencies[3].dstSubpass      = _main_camera_subpass_tone_mapping;
            dependencies[3].srcStageMask    = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                              RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[3].dstStageMask    = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                              RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[3].srcAccessMask   = RHI_ACCESS_SHADER_WRITE_BIT | RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[3].dstAccessMask     = RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            dependencies[3].dependencyFlags = RHI_DEPENDENCY_BY_REGION_BIT;

            dependencies[4].srcSubpass      = _main_camera_subpass_tone_mapping;
            dependencies[4].dstSubpass      = _main_camera_subpass_color_grading;
            dependencies[4].srcStageMask    = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                              RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[4].dstStageMask    = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                              RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[4].srcAccessMask   = RHI_ACCESS_SHADER_WRITE_BIT | RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[4].dstAccessMask     = RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            dependencies[4].dependencyFlags = RHI_DEPENDENCY_BY_REGION_BIT;

            dependencies[5].srcSubpass      = _main_camera_subpass_color_grading;
            dependencies[5].dstSubpass      = _main_camera_subpass_fxaa;
            dependencies[5].srcStageMask    = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                              RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[5].dstStageMask    = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                              RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[5].srcAccessMask   = RHI_ACCESS_SHADER_WRITE_BIT | RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[5].dstAccessMask     = RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_COLOR_ATTACHMENT_READ_BIT;

            dependencies[6].srcSubpass      = _main_camera_subpass_fxaa;
            dependencies[6].dstSubpass      = _main_camera_subpass_ui;
            dependencies[6].srcStageMask    = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                              RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[6].dstStageMask    = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                              RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[6].srcAccessMask   = RHI_ACCESS_SHADER_WRITE_BIT | RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[6].dstAccessMask     = RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            dependencies[6].dependencyFlags = RHI_DEPENDENCY_BY_REGION_BIT;

            dependencies[7].srcSubpass      = _main_camera_subpass_ui;
            dependencies[7].dstSubpass      = _main_camera_subpass_combine_ui;
            dependencies[7].srcStageMask    = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                              RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[7].dstStageMask    = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                              RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[7].srcAccessMask   = RHI_ACCESS_SHADER_WRITE_BIT | RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[7].dstAccessMask     = RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            dependencies[7].dependencyFlags = RHI_DEPENDENCY_BY_REGION_BIT;
        }

        void fillPathTracingCompositeDependencies(RHISubpassDependency* dependencies)
        {
            dependencies[0].srcSubpass    = _main_camera_subpass_forward_lighting;
            dependencies[0].dstSubpass    = _main_camera_subpass_tone_mapping;
            dependencies[0].srcStageMask  = RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
            dependencies[0].dstStageMask  = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                            RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[0].srcAccessMask = RHI_ACCESS_SHADER_WRITE_BIT;
            dependencies[0].dstAccessMask = RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            dependencies[1].srcSubpass    = _main_camera_subpass_tone_mapping;
            dependencies[1].dstSubpass    = _main_camera_subpass_color_grading;
            dependencies[1].srcStageMask  = RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[1].dstStageMask  = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                            RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[1].srcAccessMask = RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[1].dstAccessMask = RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            dependencies[2].srcSubpass    = _main_camera_subpass_color_grading;
            dependencies[2].dstSubpass    = _main_camera_subpass_fxaa;
            dependencies[2].srcStageMask  = RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[2].dstStageMask  = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                            RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[2].srcAccessMask = RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[2].dstAccessMask = RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            dependencies[3].srcSubpass    = _main_camera_subpass_fxaa;
            dependencies[3].dstSubpass    = _main_camera_subpass_ui;
            dependencies[3].srcStageMask  = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                            RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[3].dstStageMask  = RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[3].srcAccessMask = RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[3].dstAccessMask = RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            dependencies[4].srcSubpass    = _main_camera_subpass_ui;
            dependencies[4].dstSubpass    = _main_camera_subpass_combine_ui;
            dependencies[4].srcStageMask  = RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[4].dstStageMask  = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                            RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependencies[4].srcAccessMask = RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dependencies[4].dstAccessMask = RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }
    } // namespace

    void buildMainCameraRenderPass(RHI*                           rhi,
                                   const RenderPass::Framebuffer& framebuffer,
                                   bool                           enable_fxaa,
                                   MainCameraRenderPassKind       kind,
                                   RHIRenderPass*&                out_render_pass)
    {
        RHIAttachmentDescription attachments[_main_camera_pass_attachment_count] = {};
        fillMainCameraAttachments(attachments, framebuffer, rhi, kind);

        RHISubpassDescription subpasses[_main_camera_subpass_count] = {};
        if (kind == MainCameraRenderPassKind::Raster)
        {
            fillRasterEarlySubpasses(subpasses);
        }
        fillSharedLateSubpasses(subpasses, enable_fxaa);

        RHISubpassDependency raster_dependencies[8] = {};
        RHISubpassDependency pt_dependencies[5]     = {};

        RHIRenderPassCreateInfo create_info {};
        create_info.sType           = RHI_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        create_info.attachmentCount = _main_camera_pass_attachment_count;
        create_info.pAttachments    = attachments;
        create_info.subpassCount    = _main_camera_subpass_count;
        create_info.pSubpasses      = subpasses;

        if (kind == MainCameraRenderPassKind::Raster)
        {
            fillRasterDependencies(raster_dependencies);
            create_info.dependencyCount = static_cast<uint32_t>(sizeof(raster_dependencies) / sizeof(raster_dependencies[0]));
            create_info.pDependencies   = raster_dependencies;
        }
        else
        {
            fillPathTracingCompositeDependencies(pt_dependencies);
            create_info.dependencyCount = static_cast<uint32_t>(sizeof(pt_dependencies) / sizeof(pt_dependencies[0]));
            create_info.pDependencies   = pt_dependencies;
        }

        if (rhi->createRenderPass(&create_info, out_render_pass) != RHI_SUCCESS)
        {
            throw std::runtime_error(kind == MainCameraRenderPassKind::Raster ?
                                         "failed to create render pass" :
                                         "failed to create path tracing composite render pass");
        }
    }

    void buildMainCameraSwapchainFramebuffers(RHI*                           rhi,
                                              const RenderPass::Framebuffer& framebuffer,
                                              RHIRenderPass*                 render_pass,
                                              std::vector<RHIFramebuffer*>&  out_framebuffers)
    {
        out_framebuffers.resize(rhi->getSwapchainInfo().imageViews.size());

        for (size_t i = 0; i < rhi->getSwapchainInfo().imageViews.size(); i++)
        {
            RHIImageView* framebuffer_attachments_for_image_view[_main_camera_pass_attachment_count] = {
                framebuffer.attachments[_main_camera_pass_gbuffer_a].view,
                framebuffer.attachments[_main_camera_pass_gbuffer_b].view,
                framebuffer.attachments[_main_camera_pass_gbuffer_c].view,
                framebuffer.attachments[_main_camera_pass_backup_buffer_odd].view,
                framebuffer.attachments[_main_camera_pass_backup_buffer_even].view,
                framebuffer.attachments[_main_camera_pass_post_process_buffer_odd].view,
                framebuffer.attachments[_main_camera_pass_post_process_buffer_even].view,
                rhi->getDepthImageInfo().depth_image_view,
                rhi->getSwapchainInfo().imageViews[i]};

            RHIFramebufferCreateInfo framebuffer_create_info {};
            framebuffer_create_info.sType            = RHI_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_create_info.renderPass       = render_pass;
            framebuffer_create_info.attachmentCount  = _main_camera_pass_attachment_count;
            framebuffer_create_info.pAttachments     = framebuffer_attachments_for_image_view;
            framebuffer_create_info.width            = rhi->getSwapchainInfo().extent.width;
            framebuffer_create_info.height           = rhi->getSwapchainInfo().extent.height;
            framebuffer_create_info.layers           = 1;

            if (rhi->createFramebuffer(&framebuffer_create_info, out_framebuffers[i]) != RHI_SUCCESS)
            {
                throw std::runtime_error("create main camera framebuffer");
            }
        }
    }
} // namespace Piccolo
