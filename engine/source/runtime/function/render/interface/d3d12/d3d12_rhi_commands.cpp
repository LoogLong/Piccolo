#include "runtime/function/render/interface/d3d12/d3d12_rhi.h"
#include "runtime/function/render/interface/d3d12/d3d12_rhi_internal.h"
#include "runtime/function/render/interface/d3d12/d3d12_rhi_resource.h"

#include "runtime/core/base/macro.h"
#include "runtime/function/render/window_system.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cmath>
#include <cwchar>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <d3dcompiler.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#ifdef D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT
#define PICCOLO_D3D12_HAS_DXR 1
#else
#define PICCOLO_D3D12_HAS_DXR 0
#endif
#endif


namespace Piccolo
{
using namespace d3d12_detail;

bool D3D12RHI::waitForFencesPFN(uint32_t fenceCount, RHIFence* const* pFence, RHIBool32 waitAll, uint64_t timeout)
{
#ifdef _WIN32
    if (fenceCount == 0)
    {
        return true;
    }
    if (pFence == nullptr)
    {
        return false;
    }

    const ULONGLONG start_tick = timeout == UINT64_MAX ? 0ULL : GetTickCount64();
    if (!waitAll)
    {
        std::vector<HANDLE>        wait_events;
        std::vector<D3D12RHIFence*> wait_fences;
        wait_events.reserve(fenceCount);
        wait_fences.reserve(fenceCount);

        for (uint32_t i = 0; i < fenceCount; ++i)
        {
            auto* fence = static_cast<D3D12RHIFence*>(pFence[i]);
            if (fence == nullptr || fence->fence == nullptr || fence->event == nullptr)
            {
                return false;
            }

            if (fence->fence->GetCompletedValue() >= fence->wait_value)
            {
                fence->has_pending_signal = false;
                fence->signaled           = true;
                return true;
            }
            if (FAILED(fence->fence->SetEventOnCompletion(fence->wait_value, fence->event)))
            {
                return false;
            }
            wait_events.push_back(fence->event);
            wait_fences.push_back(fence);
        }

        if (wait_events.size() <= MAXIMUM_WAIT_OBJECTS)
        {
            const DWORD wait_result = WaitForMultipleObjects(static_cast<DWORD>(wait_events.size()),
                                                             wait_events.data(),
                                                             FALSE,
                                                             d3d12FenceTimeoutMilliseconds(timeout));
            if (wait_result >= WAIT_OBJECT_0 && wait_result < WAIT_OBJECT_0 + wait_events.size())
            {
                D3D12RHIFence* completed_fence = wait_fences[wait_result - WAIT_OBJECT_0];
                completed_fence->has_pending_signal = false;
                completed_fence->signaled           = true;
                return true;
            }
            return false;
        }

        while (timeout == UINT64_MAX || remainingD3D12FenceTimeout(timeout, start_tick) > 0)
        {
            for (D3D12RHIFence* fence : wait_fences)
            {
                if (fence->fence->GetCompletedValue() >= fence->wait_value)
                {
                    fence->has_pending_signal = false;
                    fence->signaled           = true;
                    return true;
                }
            }
            Sleep(1);
        }

        return false;
    }

    for (uint32_t i = 0; i < fenceCount; ++i)
    {
        auto* fence = static_cast<D3D12RHIFence*>(pFence[i]);
        if (fence == nullptr || fence->fence == nullptr)
        {
            return false;
        }

        const bool completed = waitForD3D12FenceValue(fence->fence.Get(),
                                                      fence->event,
                                                      fence->wait_value,
                                                      remainingD3D12FenceTimeout(timeout, start_tick));
        if (!completed)
        {
            return false;
        }

        fence->has_pending_signal = false;
        fence->signaled           = true;
        if (!waitAll)
        {
            return true;
        }
    }
#else
    (void)fenceCount;
    (void)pFence;
    (void)waitAll;
    (void)timeout;
#endif
    return true;
}
bool D3D12RHI::resetFencesPFN(uint32_t fenceCount, RHIFence* const* pFences)
{
#ifdef _WIN32
    if (fenceCount > 0 && pFences == nullptr)
    {
        return false;
    }

    for (uint32_t i = 0; i < fenceCount; ++i)
    {
        auto* fence = static_cast<D3D12RHIFence*>(pFences[i]);
        if (fence == nullptr || fence->fence == nullptr)
        {
            return false;
        }

        fence->wait_value         = fence->next_signal_value + 1ULL;
        fence->has_pending_signal = true;
        fence->signaled           = false;
    }
#else
    (void)fenceCount;
    (void)pFences;
#endif
    return true;
}
bool D3D12RHI::resetCommandPoolPFN(RHICommandPool* commandPool, RHICommandPoolResetFlags flags)
{
    (void)commandPool;
    (void)flags;
    resetCommandPool();
    return true;
}
bool D3D12RHI::beginCommandBufferPFN(RHICommandBuffer* commandBuffer, const RHICommandBufferBeginInfo* pBeginInfo)
{
    (void)pBeginInfo;
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    if (d3d_command_buffer == nullptr || !ensureCommandBufferObjects(commandBuffer))
    {
        return false;
    }

    if (d3d_command_buffer->is_open)
    {
        return true;
    }

    if (FAILED(d3d_command_buffer->command_allocator->Reset()))
    {
        return false;
    }

    if (FAILED(d3d_command_buffer->command_list->Reset(d3d_command_buffer->command_allocator.Get(), nullptr)))
    {
        return false;
    }

    d3d12_detail::resetCommandBufferRecordingState(*d3d_command_buffer,
                                                   m_d3d12_transient_cbv_srv_uav_descriptor_next);
    return true;
#else
    (void)commandBuffer;
    return true;
#endif
}
bool D3D12RHI::endCommandBufferPFN(RHICommandBuffer* commandBuffer)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    if (d3d_command_buffer == nullptr || d3d_command_buffer->command_list == nullptr)
    {
        return false;
    }

    if (!d3d_command_buffer->is_open)
    {
        return true;
    }

    const HRESULT close_hr = d3d_command_buffer->command_list->Close();
    if (FAILED(close_hr))
    {
        const HRESULT removed_reason =
            m_d3d12_device != nullptr ? m_d3d12_device->GetDeviceRemovedReason() : S_OK;
        LOG_ERROR("D3D12 endCommandBuffer Close failed (close_hr=0x{:08X}, removed_reason=0x{:08X}, command_list_type={})",
                  static_cast<unsigned int>(close_hr),
                  static_cast<unsigned int>(removed_reason),
                  static_cast<unsigned int>(d3d_command_buffer->command_list_type));
        return false;
    }

    d3d_command_buffer->is_open = false;
    d3d_command_buffer->has_recorded_commands = true;
    return true;
#else
    (void)commandBuffer;
    return true;
#endif
}
void D3D12RHI::cmdBeginRenderPassPFN(RHICommandBuffer* commandBuffer, const RHIRenderPassBeginInfo* pRenderPassBegin, RHISubpassContents contents)
{
    (void)contents;
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (d3d_command_buffer == nullptr || command_list == nullptr)
    {
        if (commandBuffer != nullptr || pRenderPassBegin != nullptr)
        {
            LOG_WARN("D3D12 cmdBeginRenderPass skipped because no command list is available");
        }
        return;
    }

    if (pRenderPassBegin == nullptr)
    {
        if (commandBuffer != nullptr)
        {
            LOG_WARN("D3D12 cmdBeginRenderPass skipped because render pass begin info is null");
        }
        return;
    }

    auto* render_pass = static_cast<D3D12RHIRenderPass*>(pRenderPassBegin->renderPass);
    auto* framebuffer = static_cast<D3D12RHIFramebuffer*>(pRenderPassBegin->framebuffer);
    if (render_pass == nullptr || framebuffer == nullptr)
    {
        LOG_WARN("D3D12 cmdBeginRenderPass skipped because render pass or framebuffer is invalid");
        return;
    }

    d3d_command_buffer->active_render_pass      = pRenderPassBegin->renderPass;
    d3d_command_buffer->active_framebuffer      = pRenderPassBegin->framebuffer;
    d3d_command_buffer->active_render_pass_begin_info = *pRenderPassBegin;
    d3d_command_buffer->active_clear_values.clear();
    populateFramebufferClearValues(static_cast<uint32_t>(framebuffer->attachments.size()),
                                   reinterpret_cast<RHIImageView* const*>(framebuffer->attachments.data()),
                                   d3d_command_buffer->active_clear_values);
    d3d_command_buffer->active_render_pass_begin_info.clearValueCount =
        static_cast<uint32_t>(d3d_command_buffer->active_clear_values.size());
    d3d_command_buffer->active_render_pass_begin_info.pClearValues =
        d3d_command_buffer->active_clear_values.empty() ? nullptr : d3d_command_buffer->active_clear_values.data();

    d3d_command_buffer->attachment_load_ops_applied.assign(render_pass != nullptr ?
                                                                render_pass->attachments.size() :
                                                                0,
                                                           false);
    d3d_command_buffer->active_subpass_index    = 0;
    bindFramebufferForSubpass(commandBuffer,
                              command_list,
                              &d3d_command_buffer->active_render_pass_begin_info,
                              d3d_command_buffer->active_subpass_index,
                              true);
    d3d12_detail::endGraphicsBindingScope(*d3d_command_buffer);
    d3d_command_buffer->in_render_pass = true;

#else
    (void)commandBuffer;
    (void)pRenderPassBegin;
#endif
}
void D3D12RHI::cmdNextSubpassPFN(RHICommandBuffer* commandBuffer, RHISubpassContents contents)
{
    (void)contents;
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (d3d_command_buffer == nullptr || command_list == nullptr || !d3d_command_buffer->in_render_pass)
    {
        if (commandBuffer != nullptr)
        {
            LOG_WARN("D3D12 cmdNextSubpass skipped because no active D3D12 render pass command list is available");
        }
        return;
    }

    auto* render_pass = static_cast<D3D12RHIRenderPass*>(d3d_command_buffer->active_render_pass);
    auto* framebuffer = static_cast<D3D12RHIFramebuffer*>(d3d_command_buffer->active_framebuffer);
    finishD3D12Subpass(command_list,
                       render_pass,
                       framebuffer,
                       d3d_command_buffer->active_subpass_index);

    const uint32_t previous_subpass_index = d3d_command_buffer->active_subpass_index;
    ++d3d_command_buffer->active_subpass_index;
    if (render_pass == nullptr || d3d_command_buffer->active_subpass_index >= render_pass->subpasses.size())
    {
        LOG_WARN("D3D12 cmdNextSubpass skipped because subpass {} is outside the active render pass",
                 d3d_command_buffer->active_subpass_index);
        return;
    }

    transitionD3D12SubpassBoundary(command_list,
                                   render_pass,
                                   framebuffer,
                                   previous_subpass_index,
                                   d3d_command_buffer->active_subpass_index);

    bindFramebufferForSubpass(commandBuffer,
                              command_list,
                              &d3d_command_buffer->active_render_pass_begin_info,
                              d3d_command_buffer->active_subpass_index,
                              true);
    d3d12_detail::endGraphicsBindingScope(*d3d_command_buffer);

#else
    (void)commandBuffer;
#endif
}
void D3D12RHI::cmdEndRenderPassPFN(RHICommandBuffer* commandBuffer)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (d3d_command_buffer == nullptr || command_list == nullptr)
    {
        if (commandBuffer != nullptr)
        {
            LOG_WARN("D3D12 cmdEndRenderPass skipped because no command list is available");
        }
        return;
    }

    auto* render_pass = static_cast<D3D12RHIRenderPass*>(d3d_command_buffer->active_render_pass);
    auto* framebuffer = static_cast<D3D12RHIFramebuffer*>(d3d_command_buffer->active_framebuffer);
    if (render_pass != nullptr && framebuffer != nullptr)
    {
        finishD3D12Subpass(command_list,
                           render_pass,
                           framebuffer,
                           d3d_command_buffer->active_subpass_index);

        for (uint32_t attachment_index = 0; attachment_index < framebuffer->attachments.size(); ++attachment_index)
        {
            if (attachment_index >= render_pass->attachments.size())
            {
                continue;
            }

            auto* view = framebuffer->attachments[attachment_index];
            const D3D12_RESOURCE_STATES final_state =
                subpassAttachmentState(view, render_pass->attachments[attachment_index].finalLayout);
            if (view != nullptr && view->image != nullptr && view->d3dImage()->resource != nullptr)
            {
                transitionImageView(command_list, view, final_state);
            }
            else if (view != nullptr &&
                     view->has_rtv &&
                     render_pass->attachments[attachment_index].finalLayout == RHI_IMAGE_LAYOUT_PRESENT_SRC_KHR)
            {
                const uint32_t back_buffer_index =
                    m_current_swapchain_image_index % m_swapchain_buffer_count;
                if (back_buffer_index < m_d3d12_render_targets.size() &&
                    m_d3d12_render_targets[back_buffer_index] != nullptr)
                {
                    D3D12_RESOURCE_BARRIER barrier {};
                    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    barrier.Transition.pResource   = m_d3d12_render_targets[back_buffer_index].Get();
                    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
                    command_list->ResourceBarrier(1, &barrier);
                }
            }
        }
    }
    else if (d3d_command_buffer->in_render_pass)
    {
        LOG_WARN("D3D12 cmdEndRenderPass could not finish attachment transitions because active render pass or framebuffer is missing");
    }

    d3d_command_buffer->in_render_pass = false;
    d3d_command_buffer->active_render_pass = nullptr;
    d3d_command_buffer->active_framebuffer = nullptr;
    d3d_command_buffer->active_render_pass_begin_info = {};
    d3d_command_buffer->active_clear_values.clear();
    d3d_command_buffer->attachment_load_ops_applied.clear();
    d3d_command_buffer->active_subpass_index = 0;

#else
    (void)commandBuffer;
#endif
}
void D3D12RHI::cmdBindPipelinePFN(RHICommandBuffer* commandBuffer, RHIPipelineBindPoint pipelineBindPoint, RHIPipeline* pipeline)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    auto* d3d_pipeline = static_cast<D3D12RHIPipeline*>(pipeline);
    if (d3d_command_buffer == nullptr || command_list == nullptr || d3d_pipeline == nullptr)
    {
        return;
    }

    if (pipelineBindPoint == RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
    {
#if PICCOLO_D3D12_HAS_DXR
        ComPtr<ID3D12GraphicsCommandList4> command_list4;
        if (FAILED(command_list->QueryInterface(IID_PPV_ARGS(&command_list4))) || command_list4 == nullptr)
        {
            LOG_WARN("D3D12 cmdBindPipeline skipped ray tracing pipeline because command list4 is unavailable");
            return;
        }
        if (d3d_pipeline->state_object != nullptr)
        {
            command_list4->SetPipelineState1(d3d_pipeline->state_object.Get());
        }
        if (d3d_pipeline->layout != nullptr && d3d_pipeline->layout->root_signature != nullptr)
        {
            auto* root_signature = d3d_pipeline->layout->root_signature.Get();
            if (d3d_command_buffer->bound_ray_tracing_pipeline_layout != d3d_pipeline->layout ||
                d3d_command_buffer->bound_ray_tracing_root_signature != root_signature)
            {
                clearRootDescriptorTableCache(*d3d_command_buffer, RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR);
            }
            d3d_command_buffer->bound_ray_tracing_pipeline_layout = d3d_pipeline->layout;
            d3d_command_buffer->bound_ray_tracing_root_signature = root_signature;
            command_list->SetComputeRootSignature(root_signature);
            d3d_command_buffer->ray_tracing_root_signature_dirty = false;
        }
#else
        LOG_WARN("D3D12 cmdBindPipeline skipped ray tracing pipeline because this SDK does not expose DXR");
#endif
        return;
    }

    if (pipelineBindPoint == RHI_PIPELINE_BIND_POINT_GRAPHICS)
    {
        d3d12_detail::validateGraphicsPipelineBindContract(*d3d_command_buffer, *d3d_pipeline);
        d3d12_detail::beginGraphicsBindingScope(*d3d_command_buffer, *d3d_pipeline);
    }

    if (d3d_pipeline->pipeline_state != nullptr)
    {
        command_list->SetPipelineState(d3d_pipeline->pipeline_state.Get());
    }
    if (d3d_pipeline->layout != nullptr && d3d_pipeline->layout->root_signature != nullptr)
    {
        auto* root_signature = d3d_pipeline->layout->root_signature.Get();
        if (pipelineBindPoint == RHI_PIPELINE_BIND_POINT_COMPUTE)
        {
            if (d3d_command_buffer->bound_compute_pipeline_layout != d3d_pipeline->layout ||
                d3d_command_buffer->bound_compute_root_signature != root_signature)
            {
                clearRootDescriptorTableCache(*d3d_command_buffer, RHI_PIPELINE_BIND_POINT_COMPUTE);
            }
            d3d_command_buffer->bound_compute_pipeline_layout = d3d_pipeline->layout;
            d3d_command_buffer->bound_compute_root_signature = root_signature;
            command_list->SetComputeRootSignature(root_signature);
            d3d_command_buffer->compute_root_signature_dirty = false;
        }
        else
        {
            if (d3d_command_buffer->graphics_binding_scope.layout != d3d_pipeline->layout ||
                d3d_command_buffer->bound_graphics_root_signature != root_signature)
            {
                clearRootDescriptorTableCache(*d3d_command_buffer, RHI_PIPELINE_BIND_POINT_GRAPHICS);
            }
            d3d_command_buffer->bound_graphics_root_signature = root_signature;
            command_list->SetGraphicsRootSignature(root_signature);
            d3d_command_buffer->graphics_root_signature_dirty = false;
            command_list->IASetPrimitiveTopology(d3d_pipeline->primitive_topology);
        }
    }
#else
    (void)commandBuffer;
    (void)pipelineBindPoint;
    (void)pipeline;
#endif
    return;
}
void D3D12RHI::cmdSetViewportPFN(RHICommandBuffer* commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const RHIViewport* pViewports)
{
#ifdef _WIN32
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (command_list == nullptr || pViewports == nullptr || viewportCount == 0)
    {
        return;
    }

    std::vector<D3D12_VIEWPORT> d3d_viewports;
    d3d_viewports.reserve(viewportCount);
    for (uint32_t i = 0; i < viewportCount; ++i)
    {
        const auto& viewport = pViewports[i];
        D3D12_VIEWPORT d3d_viewport {};
        d3d_viewport.TopLeftX = viewport.x;
        d3d_viewport.TopLeftY = viewport.y;
        d3d_viewport.Width    = viewport.width;
        d3d_viewport.Height   = viewport.height;
        d3d_viewport.MinDepth = viewport.minDepth;
        d3d_viewport.MaxDepth = viewport.maxDepth;
        d3d_viewports.push_back(d3d_viewport);
    }

    if (firstViewport < d3d_viewports.size())
    {
        command_list->RSSetViewports(viewportCount - firstViewport, d3d_viewports.data() + firstViewport);
    }
#else
    (void)commandBuffer;
    (void)firstViewport;
    (void)viewportCount;
    (void)pViewports;
#endif
}
void D3D12RHI::cmdSetScissorPFN(RHICommandBuffer* commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const RHIRect2D* pScissors)
{
#ifdef _WIN32
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (command_list == nullptr || pScissors == nullptr || scissorCount == 0)
    {
        return;
    }

    std::vector<D3D12_RECT> d3d_scissors;
    d3d_scissors.reserve(scissorCount);
    for (uint32_t i = 0; i < scissorCount; ++i)
    {
        const auto& scissor = pScissors[i];
        D3D12_RECT d3d_scissor {};
        d3d_scissor.left   = scissor.offset.x;
        d3d_scissor.top    = scissor.offset.y;
        d3d_scissor.right  = scissor.offset.x + static_cast<LONG>(scissor.extent.width);
        d3d_scissor.bottom = scissor.offset.y + static_cast<LONG>(scissor.extent.height);
        d3d_scissors.push_back(d3d_scissor);
    }

    if (firstScissor < d3d_scissors.size())
    {
        command_list->RSSetScissorRects(scissorCount - firstScissor, d3d_scissors.data() + firstScissor);
    }
#else
    (void)commandBuffer;
    (void)firstScissor;
    (void)scissorCount;
    (void)pScissors;
#endif
}
void D3D12RHI::cmdBindVertexBuffersPFN( RHICommandBuffer* commandBuffer, uint32_t firstBinding, uint32_t bindingCount, RHIBuffer* const* pBuffers, const RHIDeviceSize* pOffsets)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (d3d_command_buffer == nullptr || command_list == nullptr || pBuffers == nullptr || bindingCount == 0)
    {
        return;
    }

    const auto& strides = d3d_command_buffer->graphics_binding_scope.vertex_strides;
    std::vector<D3D12_VERTEX_BUFFER_VIEW> views;
    views.reserve(bindingCount);
    for (uint32_t i = 0; i < bindingCount; ++i)
    {
        auto* buffer = static_cast<D3D12RHIBuffer*>(pBuffers[i]);
        if (buffer == nullptr || buffer->resource == nullptr)
        {
            views.push_back({});
            continue;
        }

        const RHIDeviceSize offset = pOffsets != nullptr ? pOffsets[i] : 0;
        if (buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT)
        {
            transitionResource(command_list,
                               buffer->resource.Get(),
                               buffer->current_state,
                               D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        }
        D3D12_VERTEX_BUFFER_VIEW view {};
        view.BufferLocation = buffer->resource->GetGPUVirtualAddress() + offset;
        view.SizeInBytes    = offset < buffer->size ? static_cast<UINT>(buffer->size - offset) : 0;
        const uint32_t binding_index = firstBinding + i;
        if (!strides.empty() && binding_index < strides.size())
        {
            view.StrideInBytes = strides[binding_index];
        }
        views.push_back(view);
    }

    command_list->IASetVertexBuffers(firstBinding, static_cast<UINT>(views.size()), views.data());
#else
    (void)commandBuffer;
    (void)firstBinding;
    (void)bindingCount;
    (void)pBuffers;
    (void)pOffsets;
#endif
    return;
}
void D3D12RHI::cmdBindIndexBufferPFN(RHICommandBuffer* commandBuffer, RHIBuffer* buffer, RHIDeviceSize offset, RHIIndexType indexType)
{
#ifdef _WIN32
    auto* command_list = d3d12CommandListFor(commandBuffer);
    auto* d3d_buffer = static_cast<D3D12RHIBuffer*>(buffer);
    if (command_list == nullptr || d3d_buffer == nullptr || d3d_buffer->resource == nullptr)
    {
        return;
    }

    if (d3d_buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT)
    {
        transitionResource(command_list,
                           d3d_buffer->resource.Get(),
                           d3d_buffer->current_state,
                           D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }

    D3D12_INDEX_BUFFER_VIEW view {};
    view.BufferLocation = d3d_buffer->resource->GetGPUVirtualAddress() + offset;
    view.SizeInBytes    = offset < d3d_buffer->size ? static_cast<UINT>(d3d_buffer->size - offset) : 0;
    view.Format         = indexType == RHI_INDEX_TYPE_UINT32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    command_list->IASetIndexBuffer(&view);
#else
    (void)commandBuffer;
    (void)buffer;
    (void)offset;
    (void)indexType;
#endif
    return;
}
void D3D12RHI::cmdBindDescriptorSetsPFN( RHICommandBuffer* commandBuffer, RHIPipelineBindPoint pipelineBindPoint, RHIPipelineLayout* layout, uint32_t firstSet, uint32_t descriptorSetCount, const RHIDescriptorSet* const* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    auto* d3d_layout = static_cast<D3D12RHIPipelineLayout*>(layout);
    if (d3d_command_buffer == nullptr ||
        command_list == nullptr ||
        d3d_layout == nullptr ||
        (pDescriptorSets == nullptr && descriptorSetCount > 0))
    {
        if (commandBuffer != nullptr ||
            layout != nullptr ||
            pDescriptorSets != nullptr ||
            descriptorSetCount > 0)
        {
            LOG_WARN("D3D12 cmdBindDescriptorSets skipped because command buffer, command list, layout, or descriptor sets are invalid");
        }
        return;
    }

    const D3D12_COMMAND_LIST_TYPE list_type = command_list->GetType();
    uint32_t preflight_dynamic_offset_index = 0;
    uint32_t preflight_transient_next = d3d_command_buffer->transient_cbv_srv_uav_descriptor_next;
    for (uint32_t i = 0; i < descriptorSetCount; ++i)
    {
        const uint32_t set_index = firstSet + i;
        if (set_index >= d3d_layout->set_layouts.size() || pDescriptorSets[i] == nullptr)
        {
            continue;
        }

        const auto* descriptor_set = static_cast<const D3D12RHIDescriptorSet*>(pDescriptorSets[i]);
        const auto* set_layout = descriptor_set->layout;
        if (set_layout == nullptr)
        {
            continue;
        }

        const uint32_t required_dynamic_descriptor_count = dynamicDescriptorCount(*set_layout);
        if (required_dynamic_descriptor_count == 0)
        {
            continue;
        }

        if (pDynamicOffsets == nullptr ||
            preflight_dynamic_offset_index > dynamicOffsetCount ||
            required_dynamic_descriptor_count > dynamicOffsetCount - preflight_dynamic_offset_index ||
            !descriptor_set->has_cbv_srv_uav_descriptors ||
            m_d3d12_cbv_srv_uav_heap == nullptr ||
            m_d3d12_cbv_srv_uav_cpu_heap == nullptr)
        {
            LOG_WARN("D3D12 cmdBindDescriptorSets skipped dynamic descriptors for set {} (required_dynamic_descriptors={}, provided_dynamic_offsets={}, has_resource_descriptors={})",
                     set_index,
                     required_dynamic_descriptor_count,
                     dynamicOffsetCount,
                     descriptor_set->has_cbv_srv_uav_descriptors);
            return;
        }

        std::vector<uint32_t> dynamic_offsets(pDynamicOffsets + preflight_dynamic_offset_index,
                                              pDynamicOffsets + preflight_dynamic_offset_index +
                                                  required_dynamic_descriptor_count);
        if (findCachedDynamicDescriptorTable(*d3d_command_buffer,
                                             *descriptor_set,
                                             set_index,
                                             dynamic_offsets) == nullptr)
        {
            uint32_t unused_transient_base = 0;
            if (!reserveDescriptors(set_layout->cbv_srv_uav_descriptor_count,
                                    preflight_transient_next,
                                    m_d3d12_cbv_srv_uav_descriptor_capacity,
                                    unused_transient_base))
            {
                LOG_WARN("D3D12 cmdBindDescriptorSets could not reserve {} transient descriptors for set {}",
                         set_layout->cbv_srv_uav_descriptor_count,
                         set_index);
                return;
            }
        }

        preflight_dynamic_offset_index += required_dynamic_descriptor_count;
    }

    bindEngineDescriptorHeaps(command_list,
                              *d3d_command_buffer,
                              m_d3d12_cbv_srv_uav_heap.Get(),
                              m_d3d12_sampler_heap.Get(),
                              true,
                              pipelineBindPoint);

    uint32_t dynamic_offset_index = 0;
    for (uint32_t i = 0; i < descriptorSetCount; ++i)
    {
        const uint32_t set_index = firstSet + i;
        if (set_index >= d3d_layout->set_layouts.size() || pDescriptorSets[i] == nullptr)
        {
            continue;
        }

        const auto* descriptor_set = static_cast<const D3D12RHIDescriptorSet*>(pDescriptorSets[i]);
        const auto* set_layout = descriptor_set->layout;
        if (set_layout == nullptr)
        {
            continue;
        }

        for (auto* buffer : descriptor_set->host_visible_default_buffers)
        {
            if (buffer != nullptr)
            {
                (void)recordHostDataUpload(m_d3d12_device.Get(),
                                           command_list,
                                           m_pending_upload_buffers,
                                           *buffer);
            }
        }

        for (const auto& buffer_descriptor : descriptor_set->buffer_descriptors)
        {
            auto* buffer = buffer_descriptor.buffer;
            if (buffer != nullptr && buffer->resource != nullptr && buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT)
            {
                D3D12_RESOURCE_STATES target_state =
                    descriptorBufferState(buffer_descriptor.range_type);
                if (buffer_descriptor.range_type == D3D12_DESCRIPTOR_RANGE_TYPE_SRV &&
                    hasFlag(buffer->usage, RHI_BUFFER_USAGE_INDIRECT_BUFFER_BIT))
                {
                    target_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                }

                // Portable handoff for cross-domain buffers (declared by the owning pass).
                // Publish a compute-portable SRV state on graphics for vertex-only bindings.
                if (buffer->requires_cross_queue_handoff() &&
                    list_type == D3D12_COMMAND_LIST_TYPE_DIRECT &&
                    buffer_descriptor.range_type == D3D12_DESCRIPTOR_RANGE_TYPE_SRV &&
                    (target_state & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) != 0)
                {
                    const auto* layout_binding_range = set_layout->find(buffer_descriptor.binding);
                    const uint32_t stage_flags =
                        layout_binding_range != nullptr ? layout_binding_range->binding.stageFlags : 0;
                    const bool has_vertex_stage   = hasFlag(stage_flags, RHI_SHADER_STAGE_VERTEX_BIT);
                    const bool has_fragment_stage = hasFlag(stage_flags, RHI_SHADER_STAGE_FRAGMENT_BIT);
                    if (has_vertex_stage && !has_fragment_stage)
                    {
                        target_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                    }
                }
                transitionResource(command_list,
                                   buffer->resource.Get(),
                                   buffer->current_state,
                                   target_state);
            }
        }

        const uint32_t required_dynamic_descriptor_count = dynamicDescriptorCount(*set_layout);
        const bool has_dynamic_buffer_descriptors = required_dynamic_descriptor_count > 0;
        if (has_dynamic_buffer_descriptors &&
            (pDynamicOffsets == nullptr ||
             dynamic_offset_index > dynamicOffsetCount ||
             required_dynamic_descriptor_count > dynamicOffsetCount - dynamic_offset_index))
        {
            LOG_WARN("D3D12 cmdBindDescriptorSets skipped set {} because dynamic offsets are incomplete (required={}, provided={})",
                     set_index,
                     required_dynamic_descriptor_count,
                     dynamicOffsetCount);
            return;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE cbv_srv_uav_gpu_base = descriptor_set->cbv_srv_uav_gpu_base;
        if (has_dynamic_buffer_descriptors &&
            descriptor_set->has_cbv_srv_uav_descriptors &&
            set_layout->cbv_srv_uav_descriptor_count > 0)
        {
            std::vector<uint32_t> dynamic_offsets(pDynamicOffsets + dynamic_offset_index,
                                                  pDynamicOffsets + dynamic_offset_index +
                                                      required_dynamic_descriptor_count);
            if (auto* cached_gpu_base =
                    findCachedDynamicDescriptorTable(*d3d_command_buffer,
                                                     *descriptor_set,
                                                     set_index,
                                                     dynamic_offsets))
            {
                cbv_srv_uav_gpu_base = *cached_gpu_base;
                dynamic_offset_index += required_dynamic_descriptor_count;
            }
            else
            {
                uint32_t transient_base = 0;
                if (reserveDescriptors(set_layout->cbv_srv_uav_descriptor_count,
                                       d3d_command_buffer->transient_cbv_srv_uav_descriptor_next,
                                       m_d3d12_cbv_srv_uav_descriptor_capacity,
                                       transient_base))
                {
                    m_d3d12_transient_cbv_srv_uav_descriptor_next =
                        (std::max)(m_d3d12_transient_cbv_srv_uav_descriptor_next,
                                   d3d_command_buffer->transient_cbv_srv_uav_descriptor_next);

                    D3D12_CPU_DESCRIPTOR_HANDLE transient_cpu_base = cpuDescriptor(m_d3d12_cbv_srv_uav_heap.Get(),
                                                                                  m_d3d12_cbv_srv_uav_descriptor_size,
                                                                                  transient_base);
                    m_d3d12_device->CopyDescriptorsSimple(set_layout->cbv_srv_uav_descriptor_count,
                                                          transient_cpu_base,
                                                          descriptor_set->cbv_srv_uav_staging_cpu_base,
                                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                    for (const auto& range : set_layout->ranges)
                    {
                        if (!descriptorUsesResourceHeap(range.binding.descriptorType) ||
                            !isDynamicBufferDescriptor(range.binding.descriptorType))
                        {
                            continue;
                        }

                        for (uint32_t array_index = 0; array_index < range.binding.descriptorCount; ++array_index)
                        {
                            const RHIDeviceSize dynamic_offset =
                                (pDynamicOffsets != nullptr && dynamic_offset_index < dynamicOffsetCount) ?
                                    pDynamicOffsets[dynamic_offset_index] :
                                    0;
                            ++dynamic_offset_index;

                            const auto* buffer_descriptor =
                                descriptor_set->findBufferDescriptor(range.binding.binding, array_index);
                            if (buffer_descriptor == nullptr)
                            {
                                continue;
                            }

                            const uint32_t descriptor_index =
                                transient_base + range.cbv_srv_uav_offset + array_index;
                            D3D12_CPU_DESCRIPTOR_HANDLE dynamic_dst = cpuDescriptor(m_d3d12_cbv_srv_uav_heap.Get(),
                                                                                   m_d3d12_cbv_srv_uav_descriptor_size,
                                                                                   descriptor_index);
                            writeBufferDescriptor(m_d3d12_device.Get(),
                                                  dynamic_dst,
                                                  range,
                                                  *buffer_descriptor,
                                                  dynamic_offset);
                        }
                    }

                    cbv_srv_uav_gpu_base = gpuDescriptor(m_d3d12_cbv_srv_uav_heap.Get(),
                                                         m_d3d12_cbv_srv_uav_descriptor_size,
                                                         transient_base);
                    rememberCachedDynamicDescriptorTable(*d3d_command_buffer,
                                                         *descriptor_set,
                                                         set_index,
                                                         dynamic_offsets,
                                                         cbv_srv_uav_gpu_base);
                }
                else
                {
                    LOG_WARN("D3D12 cmdBindDescriptorSets could not reserve {} transient descriptors for set {}",
                             set_layout->cbv_srv_uav_descriptor_count,
                             set_index);
                    return;
                }
            }
        }
        if (descriptor_set->has_cbv_srv_uav_descriptors &&
            set_index < d3d_layout->cbv_srv_uav_root_parameter_indices.size() &&
            d3d_layout->cbv_srv_uav_root_parameter_indices[set_index] != kInvalidRootParameterIndex)
        {
            const uint32_t root_index = d3d_layout->cbv_srv_uav_root_parameter_indices[set_index];
            if (pipelineBindPoint == RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
            {
                command_list->SetComputeRootDescriptorTable(root_index, cbv_srv_uav_gpu_base);
            }
            else if (pipelineBindPoint == RHI_PIPELINE_BIND_POINT_COMPUTE)
            {
                command_list->SetComputeRootDescriptorTable(root_index, cbv_srv_uav_gpu_base);
            }
            else
            {
                command_list->SetGraphicsRootDescriptorTable(root_index, cbv_srv_uav_gpu_base);
            }
            rememberRootDescriptorTable(*d3d_command_buffer, pipelineBindPoint, root_index, cbv_srv_uav_gpu_base);
        }
        if (descriptor_set->has_sampler_descriptors &&
            set_index < d3d_layout->sampler_root_parameter_indices.size() &&
            d3d_layout->sampler_root_parameter_indices[set_index] != kInvalidRootParameterIndex)
        {
            const uint32_t root_index = d3d_layout->sampler_root_parameter_indices[set_index];
            if (pipelineBindPoint == RHI_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
            {
                command_list->SetComputeRootDescriptorTable(root_index, descriptor_set->sampler_gpu_base);
            }
            else if (pipelineBindPoint == RHI_PIPELINE_BIND_POINT_COMPUTE)
            {
                command_list->SetComputeRootDescriptorTable(root_index, descriptor_set->sampler_gpu_base);
            }
            else
            {
                command_list->SetGraphicsRootDescriptorTable(root_index, descriptor_set->sampler_gpu_base);
            }
            rememberRootDescriptorTable(*d3d_command_buffer,
                                        pipelineBindPoint,
                                        root_index,
                                        descriptor_set->sampler_gpu_base);
        }
    }
#else
    (void)commandBuffer;
    (void)pipelineBindPoint;
    (void)layout;
    (void)firstSet;
    (void)descriptorSetCount;
    (void)pDescriptorSets;
#endif
    return;
}
void D3D12RHI::cmdDrawIndexedPFN(RHICommandBuffer* commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (command_list == nullptr)
    {
        if (commandBuffer != nullptr && indexCount > 0 && instanceCount > 0)
        {
            LOG_WARN("D3D12 cmdDrawIndexed skipped because no command list is available");
        }
        return;
    }

    if (d3d_command_buffer != nullptr)
    {
        d3d12_detail::validateGraphicsBindingScopeForDraw(*d3d_command_buffer);
        bindEngineDescriptorHeaps(command_list,
                                  *d3d_command_buffer,
                                  m_d3d12_cbv_srv_uav_heap.Get(),
                                  m_d3d12_sampler_heap.Get(),
                                  true,
                                  RHI_PIPELINE_BIND_POINT_GRAPHICS);
    }
    command_list->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
#else
    (void)commandBuffer;
    (void)indexCount;
    (void)instanceCount;
    (void)firstIndex;
    (void)vertexOffset;
    (void)firstInstance;
#endif
}
void D3D12RHI::cmdClearAttachmentsPFN(RHICommandBuffer* commandBuffer, uint32_t attachmentCount, const RHIClearAttachment* pAttachments, uint32_t rectCount, const RHIClearRect* pRects)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (d3d_command_buffer == nullptr ||
        command_list == nullptr ||
        pAttachments == nullptr ||
        attachmentCount == 0 ||
        (rectCount > 0 && pRects == nullptr))
    {
        return;
    }

    auto* render_pass = static_cast<D3D12RHIRenderPass*>(d3d_command_buffer->active_render_pass);
    auto* framebuffer = static_cast<D3D12RHIFramebuffer*>(d3d_command_buffer->active_framebuffer);
    if (render_pass == nullptr || framebuffer == nullptr || d3d_command_buffer->active_subpass_index >= render_pass->subpasses.size())
    {
        return;
    }

    std::vector<D3D12_RECT> clear_rects;
    clear_rects.reserve(rectCount);
    for (uint32_t rect_index = 0; rect_index < rectCount; ++rect_index)
    {
        D3D12_RECT rect {};
        rect.left   = pRects[rect_index].rect.offset.x;
        rect.top    = pRects[rect_index].rect.offset.y;
        rect.right  = pRects[rect_index].rect.offset.x + static_cast<LONG>(pRects[rect_index].rect.extent.width);
        rect.bottom = pRects[rect_index].rect.offset.y + static_cast<LONG>(pRects[rect_index].rect.extent.height);
        clear_rects.push_back(rect);
    }

    const D3D12RHIRenderPass::SubpassInfo& subpass = render_pass->subpasses[d3d_command_buffer->active_subpass_index];
    for (uint32_t attachment_index = 0; attachment_index < attachmentCount; ++attachment_index)
    {
        const RHIClearAttachment& clear_attachment = pAttachments[attachment_index];
        if (hasFlag(clear_attachment.aspectMask, RHI_IMAGE_ASPECT_COLOR_BIT))
        {
            if (clear_attachment.colorAttachment >= subpass.color_attachment_indices.size())
            {
                continue;
            }
            const uint32_t framebuffer_attachment_index =
                subpass.color_attachment_indices[clear_attachment.colorAttachment];
            if (framebuffer_attachment_index >= framebuffer->attachments.size())
            {
                continue;
            }

            auto* view = framebuffer->attachments[framebuffer_attachment_index];
            if (view == nullptr || !view->has_rtv || view->cpu_descriptor.ptr == 0)
            {
                continue;
            }

            const FLOAT color[4] = {clear_attachment.clearValue.color.float32[0],
                                    clear_attachment.clearValue.color.float32[1],
                                    clear_attachment.clearValue.color.float32[2],
                                    clear_attachment.clearValue.color.float32[3]};
            command_list->ClearRenderTargetView(view->cpu_descriptor,
                                                color,
                                                static_cast<UINT>(clear_rects.size()),
                                                clear_rects.empty() ? nullptr : clear_rects.data());
        }

        if (hasFlag(clear_attachment.aspectMask, RHI_IMAGE_ASPECT_DEPTH_BIT) ||
            hasFlag(clear_attachment.aspectMask, RHI_IMAGE_ASPECT_STENCIL_BIT))
        {
            if (subpass.depth_attachment_index >= framebuffer->attachments.size() ||
                subpass.depth_attachment_index >= render_pass->attachments.size())
            {
                continue;
            }
            auto* depth_view = framebuffer->attachments[subpass.depth_attachment_index];
            if (depth_view == nullptr || !depth_view->has_dsv || depth_view->cpu_descriptor.ptr == 0)
            {
                continue;
            }

            D3D12_CLEAR_FLAGS clear_flags = static_cast<D3D12_CLEAR_FLAGS>(0);
            if (hasFlag(clear_attachment.aspectMask, RHI_IMAGE_ASPECT_DEPTH_BIT))
            {
                clear_flags = static_cast<D3D12_CLEAR_FLAGS>(clear_flags | D3D12_CLEAR_FLAG_DEPTH);
            }
            if (hasFlag(clear_attachment.aspectMask, RHI_IMAGE_ASPECT_STENCIL_BIT) &&
                formatHasStencil(render_pass->attachments[subpass.depth_attachment_index].format))
            {
                clear_flags = static_cast<D3D12_CLEAR_FLAGS>(clear_flags | D3D12_CLEAR_FLAG_STENCIL);
            }
            if (clear_flags == 0)
            {
                continue;
            }

            command_list->ClearDepthStencilView(depth_view->cpu_descriptor,
                                                clear_flags,
                                                clear_attachment.clearValue.depthStencil.depth,
                                                static_cast<UINT8>(clear_attachment.clearValue.depthStencil.stencil),
                                                static_cast<UINT>(clear_rects.size()),
                                                clear_rects.empty() ? nullptr : clear_rects.data());
        }
    }
#else
    (void)commandBuffer;
    (void)attachmentCount;
    (void)pAttachments;
    (void)rectCount;
    (void)pRects;
#endif
    return;
}
bool D3D12RHI::beginCommandBuffer(RHICommandBuffer* commandBuffer, const RHICommandBufferBeginInfo* pBeginInfo)
{
    return beginCommandBufferPFN(commandBuffer, pBeginInfo);
}
void D3D12RHI::cmdCopyImageToBuffer(RHICommandBuffer* commandBuffer, RHIImage* srcImage, RHIImageLayout srcImageLayout, RHIBuffer* dstBuffer, uint32_t regionCount, const RHIBufferImageCopy* pRegions)
{
    (void)srcImageLayout;
#ifdef _WIN32
    auto* command_list = d3d12CommandListFor(commandBuffer);
    auto* src = static_cast<D3D12RHIImage*>(srcImage);
    auto* dst = static_cast<D3D12RHIBuffer*>(dstBuffer);
    if (regionCount == 0)
    {
        return;
    }
    if (m_d3d12_device == nullptr || command_list == nullptr)
    {
        if (commandBuffer != nullptr || srcImage != nullptr || dstBuffer != nullptr || pRegions != nullptr)
        {
            LOG_WARN("D3D12 cmdCopyImageToBuffer skipped because device or command list is unavailable");
        }
        return;
    }
    if (src == nullptr || dst == nullptr || src->resource == nullptr)
    {
        if (srcImage != nullptr || dstBuffer != nullptr)
        {
            LOG_WARN("D3D12 cmdCopyImageToBuffer skipped because source image or destination buffer is invalid");
        }
        return;
    }
    if (pRegions == nullptr)
    {
        LOG_WARN("D3D12 cmdCopyImageToBuffer skipped because copy regions are null while regionCount is {}",
                 regionCount);
        return;
    }
    if (src->resource_bytes_per_pixel == 0)
    {
        LOG_WARN("D3D12 cmdCopyImageToBuffer skipped because source image has an unknown byte size");
        return;
    }

    const D3D12_RESOURCE_DESC texture_desc = src->resource->GetDesc();
    for (uint32_t region_index = 0; region_index < regionCount; ++region_index)
    {
        const RHIBufferImageCopy& region = pRegions[region_index];
        const uint32_t layer_count = (std::max)(1U, region.imageSubresource.layerCount);
        const uint32_t row_length = region.bufferRowLength == 0 ? region.imageExtent.width : region.bufferRowLength;
        const uint32_t image_height = region.bufferImageHeight == 0 ? region.imageExtent.height : region.bufferImageHeight;
        const uint32_t destination_row_pitch = row_length * src->resource_bytes_per_pixel;
        const RHIDeviceSize destination_layer_pitch =
            static_cast<RHIDeviceSize>(destination_row_pitch) * image_height;

        for (uint32_t layer = 0; layer < layer_count; ++layer)
        {
            const uint32_t array_layer = region.imageSubresource.baseArrayLayer + layer;
            if (array_layer >= src->array_layers || region.imageSubresource.mipLevel >= src->mip_levels)
            {
                continue;
            }

            const uint32_t subresource = d3d12SubresourceIndex(*src, region.imageSubresource.mipLevel, array_layer);
            transitionImageSubresource(command_list,
                                       *src,
                                       subresource,
                                       D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
            UINT row_count = 0;
            UINT64 row_size = 0;
            UINT64 readback_buffer_size = 0;
            m_d3d12_device->GetCopyableFootprints(&texture_desc,
                                                  subresource,
                                                  1,
                                                  0,
                                                  &footprint,
                                                  &row_count,
                                                  &row_size,
                                                  &readback_buffer_size);
            if (readback_buffer_size == 0)
            {
                continue;
            }

            D3D12_HEAP_PROPERTIES readback_heap_properties {};
            readback_heap_properties.Type                 = D3D12_HEAP_TYPE_READBACK;
            readback_heap_properties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            readback_heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            readback_heap_properties.CreationNodeMask     = 1;
            readback_heap_properties.VisibleNodeMask      = 1;

            D3D12_RESOURCE_DESC readback_desc {};
            readback_desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
            readback_desc.Alignment          = 0;
            readback_desc.Width              = readback_buffer_size;
            readback_desc.Height             = 1;
            readback_desc.DepthOrArraySize   = 1;
            readback_desc.MipLevels          = 1;
            readback_desc.Format             = DXGI_FORMAT_UNKNOWN;
            readback_desc.SampleDesc.Count   = 1;
            readback_desc.SampleDesc.Quality = 0;
            readback_desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            readback_desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

            ComPtr<ID3D12Resource> readback_buffer;
            if (FAILED(m_d3d12_device->CreateCommittedResource(&readback_heap_properties,
                                                               D3D12_HEAP_FLAG_NONE,
                                                               &readback_desc,
                                                               D3D12_RESOURCE_STATE_COPY_DEST,
                                                               nullptr,
                                                               IID_PPV_ARGS(&readback_buffer))))
            {
                continue;
            }

            D3D12_TEXTURE_COPY_LOCATION dst_location {};
            dst_location.pResource       = readback_buffer.Get();
            dst_location.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst_location.PlacedFootprint = footprint;

            D3D12_TEXTURE_COPY_LOCATION src_location {};
            src_location.pResource        = src->resource.Get();
            src_location.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src_location.SubresourceIndex = subresource;

            D3D12_BOX source_box {};
            source_box.left   = static_cast<UINT>((std::max)(0, region.imageOffset.x));
            source_box.top    = static_cast<UINT>((std::max)(0, region.imageOffset.y));
            source_box.front  = static_cast<UINT>((std::max)(0, region.imageOffset.z));
            source_box.right  = source_box.left + region.imageExtent.width;
            source_box.bottom = source_box.top + region.imageExtent.height;
            source_box.back   = source_box.front + (std::max)(1U, region.imageExtent.depth);

            command_list->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, &source_box);

            D3D12PendingTextureReadback pending_readback {};
            pending_readback.destination_buffer    = dstBuffer;
            pending_readback.destination_offset    = region.bufferOffset + destination_layer_pitch * layer;
            pending_readback.destination_row_pitch = destination_row_pitch;
            pending_readback.row_count             = (std::min)(row_count, region.imageExtent.height);
            pending_readback.row_size              = (std::min)(static_cast<uint32_t>(row_size),
                                                                region.imageExtent.width * src->resource_bytes_per_pixel);
            pending_readback.footprint             = footprint;
            pending_readback.readback_buffer       = readback_buffer;
            m_pending_texture_readbacks.push_back(pending_readback);
        }
    }
#else
    (void)commandBuffer;
    (void)srcImage;
    (void)dstBuffer;
    (void)regionCount;
    (void)pRegions;
#endif
    return;
}
void D3D12RHI::cmdCopyImageToImage(RHICommandBuffer* commandBuffer, RHIImage* srcImage, RHIImage* dstImage, uint32_t regionCount, const RHIImageBlit* pRegions)
{
#ifdef _WIN32
    auto* command_list = d3d12CommandListFor(commandBuffer);
    auto* src = static_cast<D3D12RHIImage*>(srcImage);
    auto* dst = static_cast<D3D12RHIImage*>(dstImage);
    if (regionCount == 0)
    {
        return;
    }
    if (command_list == nullptr)
    {
        if (commandBuffer != nullptr || srcImage != nullptr || dstImage != nullptr || pRegions != nullptr)
        {
            LOG_WARN("D3D12 cmdCopyImageToImage skipped because no command list is available");
        }
        return;
    }
    if (src == nullptr ||
        dst == nullptr ||
        src->resource == nullptr ||
        dst->resource == nullptr)
    {
        if (srcImage != nullptr || dstImage != nullptr)
        {
            LOG_WARN("D3D12 cmdCopyImageToImage skipped because source or destination image is invalid");
        }
        return;
    }
    if (pRegions == nullptr)
    {
        LOG_WARN("D3D12 cmdCopyImageToImage skipped because copy regions are null while regionCount is {}",
                 regionCount);
        return;
    }

    const auto clamp_offset = [](int32_t value, uint32_t limit) -> UINT {
        if (value <= 0)
        {
            return 0;
        }
        return static_cast<UINT>((std::min)(static_cast<uint32_t>(value), limit));
    };
    const auto mip_dimension = [](uint32_t base, uint32_t mip_level) -> uint32_t {
        if (mip_level >= 31U)
        {
            return 1U;
        }
        return (std::max)(1U, base >> mip_level);
    };

    for (uint32_t region_index = 0; region_index < regionCount; ++region_index)
    {
        const RHIImageBlit& region = pRegions[region_index];
        if (region.srcSubresource.mipLevel >= src->mip_levels ||
            region.dstSubresource.mipLevel >= dst->mip_levels)
        {
            continue;
        }

        const uint32_t layer_count =
            (std::max)(1U,
                       (std::min)((std::max)(1U, region.srcSubresource.layerCount),
                                  (std::max)(1U, region.dstSubresource.layerCount)));
        for (uint32_t layer = 0; layer < layer_count; ++layer)
        {
            const uint32_t src_layer = region.srcSubresource.baseArrayLayer + layer;
            const uint32_t dst_layer = region.dstSubresource.baseArrayLayer + layer;
            if (src_layer >= src->array_layers || dst_layer >= dst->array_layers)
            {
                continue;
            }

            const uint32_t src_width  = mip_dimension(src->width, region.srcSubresource.mipLevel);
            const uint32_t src_height = mip_dimension(src->height, region.srcSubresource.mipLevel);
            const uint32_t dst_width  = mip_dimension(dst->width, region.dstSubresource.mipLevel);
            const uint32_t dst_height = mip_dimension(dst->height, region.dstSubresource.mipLevel);

            D3D12_BOX source_box {};
            source_box.left   = clamp_offset(region.srcOffsets[0].x, src_width);
            source_box.top    = clamp_offset(region.srcOffsets[0].y, src_height);
            source_box.front  = clamp_offset(region.srcOffsets[0].z, 1);
            source_box.right  = clamp_offset(region.srcOffsets[1].x, src_width);
            source_box.bottom = clamp_offset(region.srcOffsets[1].y, src_height);
            source_box.back   = clamp_offset(region.srcOffsets[1].z, 1);
            if (source_box.back <= source_box.front)
            {
                source_box.back = source_box.front + 1;
            }
            if (source_box.back > 1)
            {
                source_box.back = 1;
            }
            if (source_box.left >= source_box.right ||
                source_box.top >= source_box.bottom ||
                source_box.front >= source_box.back)
            {
                continue;
            }

            const UINT dst_x = clamp_offset(region.dstOffsets[0].x, dst_width);
            const UINT dst_y = clamp_offset(region.dstOffsets[0].y, dst_height);
            const UINT dst_z = clamp_offset(region.dstOffsets[0].z, 1);
            if (dst_x >= dst_width || dst_y >= dst_height || dst_z >= 1)
            {
                continue;
            }

            const UINT copy_width  = (std::min)(source_box.right - source_box.left, static_cast<UINT>(dst_width - dst_x));
            const UINT copy_height = (std::min)(source_box.bottom - source_box.top, static_cast<UINT>(dst_height - dst_y));
            const UINT copy_depth  = (std::min)(source_box.back - source_box.front, static_cast<UINT>(1 - dst_z));
            if (copy_width == 0 || copy_height == 0 || copy_depth == 0)
            {
                continue;
            }
            source_box.right  = source_box.left + copy_width;
            source_box.bottom = source_box.top + copy_height;
            source_box.back   = source_box.front + copy_depth;

            D3D12_TEXTURE_COPY_LOCATION src_location {};
            src_location.pResource        = src->resource.Get();
            src_location.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src_location.SubresourceIndex = d3d12SubresourceIndex(*src, region.srcSubresource.mipLevel, src_layer);
            transitionImageSubresource(command_list,
                                       *src,
                                       src_location.SubresourceIndex,
                                       D3D12_RESOURCE_STATE_COPY_SOURCE);

            D3D12_TEXTURE_COPY_LOCATION dst_location {};
            dst_location.pResource        = dst->resource.Get();
            dst_location.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst_location.SubresourceIndex = d3d12SubresourceIndex(*dst, region.dstSubresource.mipLevel, dst_layer);
            transitionImageSubresource(command_list,
                                       *dst,
                                       dst_location.SubresourceIndex,
                                       D3D12_RESOURCE_STATE_COPY_DEST);

            command_list->CopyTextureRegion(&dst_location, dst_x, dst_y, dst_z, &src_location, &source_box);
        }
    }
#else
    (void)commandBuffer;
    (void)srcImage;
    (void)dstImage;
    (void)regionCount;
    (void)pRegions;
#endif
    return;
}
void D3D12RHI::cmdCopyBuffer(RHICommandBuffer* commandBuffer, RHIBuffer* srcBuffer, RHIBuffer* dstBuffer, uint32_t regionCount, RHIBufferCopy* pRegions)
{
#ifdef _WIN32
    auto* command_list = d3d12CommandListFor(commandBuffer);
    auto* src = static_cast<D3D12RHIBuffer*>(srcBuffer);
    auto* dst = static_cast<D3D12RHIBuffer*>(dstBuffer);
    if (command_list == nullptr || src == nullptr || dst == nullptr)
    {
        LOG_ERROR("D3D12 cmdCopyBuffer requires an open command list and valid buffers");
        return;
    }
    if (src->resource == nullptr || dst->resource == nullptr)
    {
        LOG_ERROR("D3D12 cmdCopyBuffer requires GPU resources");
        return;
    }
    if (dst->heap_type == D3D12_HEAP_TYPE_UPLOAD)
    {
        LOG_ERROR("D3D12 cmdCopyBuffer cannot copy into an upload heap destination");
        return;
    }

    RHIBufferCopy default_region {};
    const RHIBufferCopy* regions = pRegions;
    if (regions == nullptr || regionCount == 0)
    {
        default_region.srcOffset = 0;
        default_region.dstOffset = 0;
        default_region.size = (std::min)(src->size, dst->size);
        regions = &default_region;
        regionCount = default_region.size > 0 ? 1 : 0;
    }

    for (uint32_t i = 0; i < regionCount; ++i)
    {
        const RHIBufferCopy& region = regions[i];
        if (region.srcOffset > src->size ||
            region.dstOffset > dst->size ||
            region.size > src->size - region.srcOffset ||
            region.size > dst->size - region.dstOffset)
        {
            LOG_ERROR("D3D12 cmdCopyBuffer skipped invalid copy region");
            continue;
        }

        const bool src_host_data_valid = src->host_data_valid;
        const bool dst_host_data_valid = dst->host_data_valid;
        if (src->heap_type == D3D12_HEAP_TYPE_DEFAULT)
        {
            transitionResource(command_list,
                               src->resource.Get(),
                               src->current_state,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
        }
        if (dst->heap_type == D3D12_HEAP_TYPE_DEFAULT)
        {
            transitionResource(command_list,
                               dst->resource.Get(),
                               dst->current_state,
                               D3D12_RESOURCE_STATE_COPY_DEST);
            dst->host_data_valid = false;
            dst->host_data_uploadable = false;
        }
        command_list->CopyBufferRegion(dst->resource.Get(),
                                       region.dstOffset,
                                       src->resource.Get(),
                                       region.srcOffset,
                                       region.size);
        dst->map_host_data = false;
        dst->host_data_write_mapped = false;
        updateBufferHostMirrorAfterCopy(*src,
                                        *dst,
                                        src_host_data_valid,
                                        dst_host_data_valid,
                                        region.srcOffset,
                                        region.dstOffset,
                                        region.size,
                                        "D3D12 cmdCopyBuffer");
    }
#else
    (void)commandBuffer;
    if (srcBuffer == nullptr || dstBuffer == nullptr)
    {
        return;
    }

    auto* src = static_cast<D3D12RHIBuffer*>(srcBuffer);
    auto* dst = static_cast<D3D12RHIBuffer*>(dstBuffer);
    if (src == nullptr || dst == nullptr)
    {
        return;
    }

    RHIBufferCopy default_region {};
    const RHIBufferCopy* regions = pRegions;
    if (pRegions == nullptr || regionCount == 0)
    {
        default_region.srcOffset = 0;
        default_region.dstOffset = 0;
        default_region.size = (std::min)(static_cast<RHIDeviceSize>(src->host_data.size()),
                                         static_cast<RHIDeviceSize>(dst->host_data.size()));
        regions = &default_region;
        regionCount = default_region.size > 0 ? 1 : 0;
    }

    for (uint32_t i = 0; i < regionCount; ++i)
    {
        const RHIBufferCopy& region = regions[i];
        if (region.srcOffset <= src->host_data.size() &&
            region.dstOffset <= dst->host_data.size() &&
            region.size <= src->host_data.size() - region.srcOffset &&
            region.size <= dst->host_data.size() - region.dstOffset)
        {
            updateBufferHostMirrorAfterCopy(*src,
                                            *dst,
                                            src->host_data_valid,
                                            dst->host_data_valid,
                                            region.srcOffset,
                                            region.dstOffset,
                                            region.size,
                                            "D3D12 cmdCopyBuffer");
        }
    }
#endif
    return;
}
void D3D12RHI::cmdDraw(RHICommandBuffer* commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    if (auto* command_list = d3d12CommandListFor(commandBuffer))
    {
        if (d3d_command_buffer != nullptr)
        {
            bindEngineDescriptorHeaps(command_list,
                                      *d3d_command_buffer,
                                      m_d3d12_cbv_srv_uav_heap.Get(),
                                      m_d3d12_sampler_heap.Get(),
                                      true,
                                      RHI_PIPELINE_BIND_POINT_GRAPHICS);
        }
        command_list->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
    }
    else if (commandBuffer != nullptr && vertexCount > 0 && instanceCount > 0)
    {
        LOG_WARN("D3D12 cmdDraw skipped because no command list is available");
        assert(false && "D3D12 cmdDraw invoked without a valid command list");
    }
#else
    (void)commandBuffer;
    (void)vertexCount;
    (void)instanceCount;
    (void)firstVertex;
    (void)firstInstance;
#endif
    return;
}
void D3D12RHI::cmdDispatch(RHICommandBuffer* commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    if (auto* command_list = d3d12CommandListFor(commandBuffer))
    {
        if (d3d_command_buffer != nullptr)
        {
            bindEngineDescriptorHeaps(command_list,
                                      *d3d_command_buffer,
                                      m_d3d12_cbv_srv_uav_heap.Get(),
                                      m_d3d12_sampler_heap.Get(),
                                      true,
                                      RHI_PIPELINE_BIND_POINT_COMPUTE);
        }
        command_list->Dispatch(groupCountX, groupCountY, groupCountZ);
    }
    else if (commandBuffer != nullptr && groupCountX > 0 && groupCountY > 0 && groupCountZ > 0)
    {
        LOG_WARN("D3D12 cmdDispatch skipped because no command list is available");
        assert(false && "D3D12 cmdDispatch invoked without a valid command list");
    }
#else
    (void)commandBuffer;
    (void)groupCountX;
    (void)groupCountY;
    (void)groupCountZ;
#endif
    return;
}
void D3D12RHI::cmdDispatchIndirect(RHICommandBuffer* commandBuffer, RHIBuffer* buffer, RHIDeviceSize offset)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(commandBuffer);
    auto* command_list = d3d12CommandListFor(commandBuffer);
    auto* d3d_buffer = static_cast<D3D12RHIBuffer*>(buffer);
    if (command_list == nullptr)
    {
        if (commandBuffer != nullptr || buffer != nullptr)
        {
            LOG_WARN("D3D12 cmdDispatchIndirect skipped because no command list is available");
        }
        return;
    }
    if (d3d_buffer == nullptr || d3d_buffer->resource == nullptr)
    {
        if (buffer != nullptr)
        {
            LOG_WARN("D3D12 cmdDispatchIndirect skipped because the indirect argument buffer has no D3D12 resource");
        }
        return;
    }
    if (!ensureDispatchCommandSignature())
    {
        LOG_WARN("D3D12 cmdDispatchIndirect skipped because the dispatch command signature is unavailable");
        return;
    }

    if (d3d_command_buffer != nullptr)
    {
        d3d12_detail::validateGraphicsBindingScopeForDraw(*d3d_command_buffer);
        bindEngineDescriptorHeaps(command_list,
                                  *d3d_command_buffer,
                                  m_d3d12_cbv_srv_uav_heap.Get(),
                                  m_d3d12_sampler_heap.Get(),
                                  true,
                                  RHI_PIPELINE_BIND_POINT_COMPUTE);
    }
    ID3D12Resource* argument_resource = d3d_buffer->resource.Get();
    UINT64          argument_offset   = offset;
    const bool storage_indirect_buffer =
        hasFlag(d3d_buffer->usage, RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT) &&
        hasFlag(d3d_buffer->usage, RHI_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    if (storage_indirect_buffer && d3d_command_buffer != nullptr)
    {
        if (!ensureDispatchArgumentScratchBuffer(m_d3d12_device.Get(), *d3d_command_buffer))
        {
            LOG_WARN("D3D12 cmdDispatchIndirect skipped because the scratch argument buffer is unavailable");
            return;
        }

        const D3D12_RESOURCE_STATES compute_invalid_state_mask =
            D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_DEPTH_WRITE |
            D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER;
        if ((d3d_buffer->current_state & compute_invalid_state_mask) != 0)
        {
            d3d_buffer->current_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        transitionResource(command_list,
                           d3d_buffer->resource.Get(),
                           d3d_buffer->current_state,
                           D3D12_RESOURCE_STATE_COPY_SOURCE);
        transitionResource(command_list,
                           d3d_command_buffer->dispatch_argument_buffer.Get(),
                           d3d_command_buffer->dispatch_argument_buffer_state,
                           D3D12_RESOURCE_STATE_COPY_DEST);
        command_list->CopyBufferRegion(d3d_command_buffer->dispatch_argument_buffer.Get(),
                                       0,
                                       d3d_buffer->resource.Get(),
                                       offset,
                                       sizeof(D3D12_DISPATCH_ARGUMENTS));
        transitionResource(command_list,
                           d3d_command_buffer->dispatch_argument_buffer.Get(),
                           d3d_command_buffer->dispatch_argument_buffer_state,
                           D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        transitionResource(command_list,
                           d3d_buffer->resource.Get(),
                           d3d_buffer->current_state,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        argument_resource = d3d_command_buffer->dispatch_argument_buffer.Get();
        argument_offset   = 0;
    }
    else if (d3d_buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT)
    {
        transitionResource(command_list,
                           d3d_buffer->resource.Get(),
                           d3d_buffer->current_state,
                           D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    }

    const HRESULT removed_before =
        m_d3d12_device != nullptr ? m_d3d12_device->GetDeviceRemovedReason() : S_OK;
    command_list->ExecuteIndirect(m_d3d12_dispatch_command_signature.Get(),
                                  1,
                                  argument_resource,
                                  argument_offset,
                                  nullptr,
                                  0);
    const HRESULT removed_after =
        m_d3d12_device != nullptr ? m_d3d12_device->GetDeviceRemovedReason() : S_OK;
    if (FAILED(removed_after) || removed_after != removed_before)
    {
        LOG_ERROR("D3D12 cmdDispatchIndirect device removed/transition (removed_before=0x{:08X}, removed_after=0x{:08X})",
                  static_cast<unsigned int>(removed_before),
                  static_cast<unsigned int>(removed_after));
    }
#else
    (void)commandBuffer;
    (void)buffer;
    (void)offset;
#endif
    return;
}
void D3D12RHI::cmdPipelineBarrier(RHICommandBuffer* commandBuffer, RHIPipelineStageFlags srcStageMask, RHIPipelineStageFlags dstStageMask, RHIDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const RHIMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const RHIBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const RHIImageMemoryBarrier* pImageMemoryBarriers)
{
    (void)dependencyFlags;
#ifdef _WIN32
    auto* command_list = d3d12CommandListFor(commandBuffer);
    if (command_list == nullptr)
    {
        const bool has_barrier_work =
            (memoryBarrierCount > 0 && pMemoryBarriers != nullptr) ||
            (bufferMemoryBarrierCount > 0 && pBufferMemoryBarriers != nullptr) ||
            (imageMemoryBarrierCount > 0 && pImageMemoryBarriers != nullptr);
        if (commandBuffer != nullptr && has_barrier_work)
        {
            LOG_WARN("D3D12 cmdPipelineBarrier skipped because no command list is available");
        }
        return;
    }

    const RHIPipelineStageFlags graphics_stage_mask =
        RHI_PIPELINE_STAGE_VERTEX_INPUT_BIT | RHI_PIPELINE_STAGE_VERTEX_SHADER_BIT |
        RHI_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
        RHI_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | RHI_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
        RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | RHI_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        RHI_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        RHI_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    const bool compute_domain_barrier =
        (srcStageMask & RHI_PIPELINE_STAGE_COMPUTE_SHADER_BIT) != 0 &&
        (dstStageMask & graphics_stage_mask) == 0 &&
        (dstStageMask & RHI_PIPELINE_STAGE_TRANSFER_BIT) == 0 &&
        (dstStageMask & RHI_PIPELINE_STAGE_HOST_BIT) == 0;
    const D3D12_RESOURCE_STATES compute_invalid_state_mask =
        D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_DEPTH_WRITE |
        D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER;

    if (pMemoryBarriers != nullptr)
    {
        for (uint32_t barrier_index = 0; barrier_index < memoryBarrierCount; ++barrier_index)
        {
            const RHIMemoryBarrier& memory_barrier = pMemoryBarriers[barrier_index];
            if (bufferAccessIncludesGpuWrite(memory_barrier.srcAccessMask) ||
                bufferAccessIncludesGpuWrite(memory_barrier.dstAccessMask))
            {
                invalidateTrackedHostVisibleDefaultMirrors();

                D3D12_RESOURCE_BARRIER barrier {};
                barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.Flags         = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.UAV.pResource = nullptr;
                command_list->ResourceBarrier(1, &barrier);
            }
        }
    }

    if (pBufferMemoryBarriers != nullptr)
    {
        for (uint32_t barrier_index = 0; barrier_index < bufferMemoryBarrierCount; ++barrier_index)
        {
            const RHIBufferMemoryBarrier& buffer_barrier = pBufferMemoryBarriers[barrier_index];
            auto* buffer = static_cast<D3D12RHIBuffer*>(buffer_barrier.buffer);
            if (buffer == nullptr || buffer->resource == nullptr)
            {
                continue;
            }

            const D3D12_RESOURCE_STATES target_state = toD3D12BufferState(buffer_barrier.dstAccessMask,
                                                                       buffer->usage,
                                                                       buffer->heap_type,
                                                                       srcStageMask,
                                                                       dstStageMask);
            if (buffer->heap_type == D3D12_HEAP_TYPE_DEFAULT)
            {
                if (bufferAccessIncludesGpuWrite(buffer_barrier.srcAccessMask) ||
                    bufferAccessIncludesGpuWrite(buffer_barrier.dstAccessMask))
                {
                    buffer->host_data_valid = false;
                    buffer->host_data_uploadable = false;
                }

                const bool compute_shader_sync =
                    compute_domain_barrier &&
                    (hasFlag(buffer_barrier.srcAccessMask, RHI_ACCESS_SHADER_READ_BIT) ||
                     hasFlag(buffer_barrier.dstAccessMask, RHI_ACCESS_SHADER_READ_BIT) ||
                     hasFlag(buffer_barrier.srcAccessMask, RHI_ACCESS_SHADER_WRITE_BIT) ||
                     hasFlag(buffer_barrier.dstAccessMask, RHI_ACCESS_SHADER_WRITE_BIT));
                const bool storage_indirect_only_sync =
                    hasFlag(buffer->usage, RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT) &&
                    hasFlag(buffer->usage, RHI_BUFFER_USAGE_INDIRECT_BUFFER_BIT) &&
                    hasFlag(buffer_barrier.srcAccessMask, RHI_ACCESS_INDIRECT_COMMAND_READ_BIT) &&
                    hasFlag(buffer_barrier.dstAccessMask, RHI_ACCESS_INDIRECT_COMMAND_READ_BIT) &&
                    !hasFlag(buffer_barrier.srcAccessMask, RHI_ACCESS_SHADER_READ_BIT) &&
                    !hasFlag(buffer_barrier.dstAccessMask, RHI_ACCESS_SHADER_READ_BIT) &&
                    !hasFlag(buffer_barrier.srcAccessMask, RHI_ACCESS_SHADER_WRITE_BIT) &&
                    !hasFlag(buffer_barrier.dstAccessMask, RHI_ACCESS_SHADER_WRITE_BIT);
                const bool current_state_invalid_on_compute =
                    compute_domain_barrier &&
                    (buffer->current_state & compute_invalid_state_mask) != 0;
                const bool target_state_invalid_on_compute =
                    compute_domain_barrier &&
                    (target_state & compute_invalid_state_mask) != 0;

                if (compute_shader_sync || storage_indirect_only_sync ||
                    current_state_invalid_on_compute || target_state_invalid_on_compute)
                {
                    D3D12_RESOURCE_BARRIER barrier {};
                    barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    barrier.Flags         = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    barrier.UAV.pResource = buffer->resource.Get();
                    command_list->ResourceBarrier(1, &barrier);
                    if (storage_indirect_only_sync)
                    {
                        buffer->current_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                    }
                    else
                    {
                        buffer->current_state = target_state;
                    }
                }
                else if (buffer->current_state == target_state &&
                         (hasFlag(buffer_barrier.srcAccessMask, RHI_ACCESS_SHADER_WRITE_BIT) ||
                          hasFlag(buffer_barrier.dstAccessMask, RHI_ACCESS_SHADER_WRITE_BIT)))
                {
                    D3D12_RESOURCE_BARRIER barrier {};
                    barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    barrier.Flags         = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    barrier.UAV.pResource = buffer->resource.Get();
                    command_list->ResourceBarrier(1, &barrier);
                }
                else
                {
                    transitionResource(command_list,
                                       buffer->resource.Get(),
                                       buffer->current_state,
                                       target_state);
                }
            }
        }
    }

    if (pImageMemoryBarriers != nullptr)
    {
        for (uint32_t barrier_index = 0; barrier_index < imageMemoryBarrierCount; ++barrier_index)
        {
            const RHIImageMemoryBarrier& image_barrier = pImageMemoryBarriers[barrier_index];
            auto* image = static_cast<D3D12RHIImage*>(image_barrier.image);
            if (image == nullptr || image->resource == nullptr)
            {
                continue;
            }

            const D3D12_RESOURCE_STATES target_state = toD3D12ResourceState(image_barrier.newLayout);
            const bool needs_uav_barrier =
                hasFlag(image_barrier.srcAccessMask, RHI_ACCESS_SHADER_WRITE_BIT) ||
                hasFlag(image_barrier.dstAccessMask, RHI_ACCESS_SHADER_WRITE_BIT);
            if (imageSubresourceRangeInState(*image, image_barrier.subresourceRange, target_state))
            {
                if (needs_uav_barrier)
                {
                    D3D12_RESOURCE_BARRIER barrier {};
                    barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    barrier.Flags         = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    barrier.UAV.pResource = image->resource.Get();
                    command_list->ResourceBarrier(1, &barrier);
                }
            }
            else
            {
                transitionImageSubresourceRange(command_list,
                                                *image,
                                                image_barrier.subresourceRange,
                                                target_state);
                if (needs_uav_barrier)
                {
                    D3D12_RESOURCE_BARRIER barrier {};
                    barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    barrier.Flags         = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    barrier.UAV.pResource = image->resource.Get();
                    command_list->ResourceBarrier(1, &barrier);
                }
            }
        }
    }
#else
    (void)commandBuffer;
    (void)memoryBarrierCount;
    (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount;
    (void)pBufferMemoryBarriers;
    (void)imageMemoryBarrierCount;
    (void)pImageMemoryBarriers;
#endif
    return;
}
bool D3D12RHI::endCommandBuffer(RHICommandBuffer* commandBuffer)
{
    return endCommandBufferPFN(commandBuffer);
}
bool D3D12RHI::queueSubmit(RHIQueue* queue, uint32_t submitCount, const RHISubmitInfo* pSubmits, RHIFence* fence)
{
#ifdef _WIN32
    auto* d3d_queue = static_cast<D3D12RHIQueue*>(queue);
    ID3D12CommandQueue* command_queue =
        (d3d_queue != nullptr && d3d_queue->command_queue != nullptr) ? d3d_queue->command_queue :
                                                                        m_d3d12_command_queue.Get();
    if (command_queue == nullptr)
    {
        return false;
    }

    if (pSubmits != nullptr)
    {
        for (uint32_t submit_index = 0; submit_index < submitCount; ++submit_index)
        {
            const RHISubmitInfo& submit = pSubmits[submit_index];

            for (uint32_t semaphore_index = 0; semaphore_index < submit.waitSemaphoreCount; ++semaphore_index)
            {
                if (submit.pWaitSemaphores == nullptr)
                {
                    return false;
                }

                auto* semaphore = static_cast<D3D12RHISemaphore*>(submit.pWaitSemaphores[semaphore_index]);
                if (semaphore != nullptr &&
                    semaphore->fence != nullptr &&
                    semaphore->has_pending_signal)
                {
                    if (FAILED(command_queue->Wait(semaphore->fence.Get(), semaphore->wait_value)))
                    {
                        return false;
                    }
                    semaphore->has_pending_signal = false;
                }
            }

            std::vector<ID3D12CommandList*> submit_command_lists;
            for (uint32_t command_buffer_index = 0; command_buffer_index < submit.commandBufferCount; ++command_buffer_index)
            {
                if (submit.pCommandBuffers == nullptr)
                {
                    return false;
                }

                auto* d3d_command_buffer =
                    static_cast<D3D12RHICommandBuffer*>(submit.pCommandBuffers[command_buffer_index]);
                if (d3d_command_buffer == nullptr || d3d_command_buffer->command_list == nullptr)
                {
                    continue;
                }

                if (d3d_command_buffer->is_open)
                {
                    if (FAILED(d3d_command_buffer->command_list->Close()))
                    {
                        d3d_command_buffer->is_open = false;
                        return false;
                    }
                    d3d_command_buffer->is_open = false;
                    d3d_command_buffer->has_recorded_commands = true;
                }

                if (d3d_command_buffer->has_recorded_commands)
                {
                    submit_command_lists.push_back(d3d_command_buffer->command_list.Get());
                }
            }

            if (!submit_command_lists.empty())
            {
                command_queue->ExecuteCommandLists(static_cast<UINT>(submit_command_lists.size()),
                                                 submit_command_lists.data());
            }

            for (uint32_t semaphore_index = 0; semaphore_index < submit.signalSemaphoreCount; ++semaphore_index)
            {
                if (submit.pSignalSemaphores == nullptr)
                {
                    return false;
                }

                auto* semaphore =
                    static_cast<D3D12RHISemaphore*>(const_cast<RHISemaphore*>(submit.pSignalSemaphores[semaphore_index]));
                if (semaphore == nullptr || semaphore->fence == nullptr)
                {
                    return false;
                }

                const uint64_t signal_value = semaphore->next_signal_value + 1ULL;
                if (FAILED(command_queue->Signal(semaphore->fence.Get(), signal_value)))
                {
                    return false;
                }
                semaphore->next_signal_value = signal_value;
                semaphore->wait_value        = signal_value;
                semaphore->has_pending_signal = true;
            }
        }
    }

    if (fence != nullptr)
    {
        auto* d3d_fence = static_cast<D3D12RHIFence*>(fence);
        if (d3d_fence == nullptr || d3d_fence->fence == nullptr)
        {
            return false;
        }

        if (!d3d_fence->has_pending_signal)
        {
            d3d_fence->wait_value = d3d_fence->next_signal_value + 1ULL;
        }

        if (FAILED(command_queue->Signal(d3d_fence->fence.Get(), d3d_fence->wait_value)))
        {
            return false;
        }

        d3d_fence->next_signal_value = (std::max)(d3d_fence->next_signal_value, d3d_fence->wait_value);
        d3d_fence->has_pending_signal = true;
        d3d_fence->signaled           = false;
    }
    return true;
#else
    (void)queue;
    (void)fence;
    (void)submitCount;
    (void)pSubmits;
    return true;
#endif
}
bool D3D12RHI::queueWaitIdle(RHIQueue* queue)
{
#ifdef _WIN32
    auto* d3d_queue = static_cast<D3D12RHIQueue*>(queue);
    if (d3d_queue != nullptr && d3d_queue->command_queue != nullptr && d3d_queue->command_queue != m_d3d12_command_queue.Get())
    {
        const uint64_t signal_value = ++m_d3d12_fence_value;
        if (FAILED(d3d_queue->command_queue->Signal(m_d3d12_fence.Get(), signal_value)))
        {
            return false;
        }
        return waitForD3D12FenceValue(m_d3d12_fence.Get(), m_d3d12_fence_event, signal_value, UINT64_MAX);
    }
#else
    (void)queue;
#endif
    waitForGpu();
    return true;
}
void D3D12RHI::resetCommandPool()
{
#ifdef _WIN32
    if (auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(m_current_command_buffer))
    {
        if (d3d_command_buffer->is_open)
        {
            return;
        }
        d3d_command_buffer->has_recorded_commands = false;
        d3d_command_buffer->dynamic_descriptor_table_cache.clear();
    }
    m_d3d12_transient_cbv_srv_uav_descriptor_next = m_d3d12_cbv_srv_uav_descriptor_next;
#endif
    return;
}
bool D3D12RHI::waitForFences()
{
#ifdef _WIN32
    RHIFence* current_frame_fence = m_frame_fences[m_current_frame_index % m_frame_fences.size()];
    RHIFence* current_copy_fence  = m_copy_fences[m_current_frame_index % m_copy_fences.size()];
    RHIFence* fences[]            = {current_frame_fence, current_copy_fence};
    if (!waitForFencesPFN(2, fences, RHI_TRUE, UINT64_MAX))
    {
        LOG_ERROR("D3D12 waitForFences failed for frame {}", static_cast<uint32_t>(m_current_frame_index));
        return false;
    }
    onFrameSlotReady(m_current_frame_index);
#endif
    return true;
}

void D3D12RHI::waitAllFramesInFlight()
{
#ifdef _WIN32
    if (!m_frame_fences.empty())
    {
        if (!waitForFencesPFN(static_cast<uint32_t>(m_frame_fences.size()),
                              m_frame_fences.data(),
                              RHI_TRUE,
                              UINT64_MAX))
        {
            LOG_ERROR("D3D12 waitAllFramesInFlight failed for frame fences");
        }
    }
    if (!m_copy_fences.empty())
    {
        if (!waitForFencesPFN(static_cast<uint32_t>(m_copy_fences.size()),
                              m_copy_fences.data(),
                              RHI_TRUE,
                              UINT64_MAX))
        {
            LOG_ERROR("D3D12 waitAllFramesInFlight failed for copy fences");
        }
    }
#endif
}

void D3D12RHI::waitDeviceIdle()
{
#ifdef _WIN32
    waitAllFramesInFlight();
    if (m_graphics_queue != nullptr)
    {
        queueWaitIdle(m_graphics_queue);
    }
    if (m_compute_queue != nullptr)
    {
        queueWaitIdle(m_compute_queue);
    }
#endif
}
RHICommandBuffer* D3D12RHI::getCurrentCommandBuffer() const
{
    return m_current_command_buffer;
}
RHICommandBuffer* const* D3D12RHI::getCommandBufferList() const
{
    return m_frame_command_buffers.data();
}
RHICommandPool* D3D12RHI::getCommandPoor() const
{
    return m_default_command_pool;
}
RHIDescriptorPool* D3D12RHI::getDescriptorPoor() const
{
    return m_default_descriptor_pool;
}
RHIFence* const* D3D12RHI::getFenceList() const
{
    return m_frame_fences.data();
}
RHIFence* const* D3D12RHI::getCopyFenceList() const
{
    return m_copy_fences.data();
}
RHISemaphore*& D3D12RHI::getCopyReadySemaphore(uint32_t index)
{
    return m_copy_ready_semaphores[index % m_copy_ready_semaphores.size()];
}
RHISemaphore*& D3D12RHI::getCopyDoneSemaphore(uint32_t index)
{
    return m_copy_done_semaphores[index % m_copy_done_semaphores.size()];
}
void D3D12RHI::setCommandBufferComputeQueue(RHICommandBuffer* command_buffer, bool use_compute_queue)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(command_buffer);
    if (d3d_command_buffer == nullptr)
    {
        return;
    }

    const D3D12_COMMAND_LIST_TYPE requested_type =
        use_compute_queue ? D3D12_COMMAND_LIST_TYPE_COMPUTE : D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (d3d_command_buffer->command_list != nullptr &&
        d3d_command_buffer->command_list_type != requested_type)
    {
        LOG_ERROR(
            "D3D12 setCommandBufferComputeQueue called after command list creation; use "
            "RHICommandBufferAllocateInfo::queueBindPoint at allocation instead");
        return;
    }

    d3d_command_buffer->command_list_type = requested_type;
#else
    (void)command_buffer;
    (void)use_compute_queue;
#endif
}
QueueFamilyIndices D3D12RHI::getQueueFamilyIndices() const
{
    QueueFamilyIndices indices;
    indices.graphics_family = 0;
    indices.present_family  = 0;
    indices.m_compute_family = 0;
    return indices;
}
RHIQueue* D3D12RHI::getGraphicsQueue() const
{
    return m_graphics_queue;
}
RHIQueue* D3D12RHI::getComputeQueue() const
{
    return m_compute_queue;
}
RHICommandBuffer* D3D12RHI::beginSingleTimeCommands()
{
    auto* command_buffer = new D3D12RHICommandBuffer();
#ifdef _WIN32
    RHICommandBufferBeginInfo begin_info {};
    begin_info.sType = RHI_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (beginCommandBufferPFN(command_buffer, &begin_info))
    {
        command_buffer->owns_recording = true;
    }
#endif
    return command_buffer;
}
void D3D12RHI::endSingleTimeCommands(RHICommandBuffer* command_buffer)
{
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(command_buffer);
#ifdef _WIN32
    if (d3d_command_buffer != nullptr && d3d_command_buffer->owns_recording)
    {
        if (m_d3d12_command_queue != nullptr && d3d_command_buffer->command_list != nullptr)
        {
            if (d3d_command_buffer->is_open)
            {
                if (FAILED(d3d_command_buffer->command_list->Close()))
                {
                    d3d_command_buffer->is_open = false;
                    delete d3d_command_buffer;
                    return;
                }
                d3d_command_buffer->is_open = false;
                d3d_command_buffer->has_recorded_commands = true;
            }

            ID3D12CommandList* command_lists[] = {d3d_command_buffer->command_list.Get()};
            m_d3d12_command_queue->ExecuteCommandLists(1, command_lists);
            waitForGpu();
        }
    }
#endif
    delete d3d_command_buffer;
    return;
}
bool D3D12RHI::prepareBeforePass(std::function<void()> passUpdateAfterRecreateSwapchain)
{
    (void)passUpdateAfterRecreateSwapchain;
#ifdef _WIN32
    int framebuffer_width  = static_cast<int>(m_window_width);
    int framebuffer_height = static_cast<int>(m_window_height);
    if (m_window != nullptr)
    {
        glfwGetFramebufferSize(m_window, &framebuffer_width, &framebuffer_height);
    }

    if (framebuffer_width <= 0 || framebuffer_height <= 0)
    {
        return true;
    }

    const uint32_t requested_width  = static_cast<uint32_t>(framebuffer_width);
    const uint32_t requested_height = static_cast<uint32_t>(framebuffer_height);
    const bool     needs_recreate   = !m_d3d12_swapchain ||
                                    requested_width != m_window_width ||
                                    requested_height != m_window_height ||
                                    requested_width != m_swapchain_desc.extent.width ||
                                    requested_height != m_swapchain_desc.extent.height ||
                                    m_swapchain_desc.imageViews.size() != m_swapchain_buffer_count;
    if (needs_recreate)
    {
        recreateSwapchain();

        const bool recreated = m_d3d12_swapchain != nullptr &&
                               m_swapchain_desc.extent.width == requested_width &&
                               m_swapchain_desc.extent.height == requested_height &&
                               m_swapchain_desc.imageViews.size() == m_swapchain_buffer_count;
        if (recreated && passUpdateAfterRecreateSwapchain)
        {
            passUpdateAfterRecreateSwapchain();
        }
        return true;
    }

    if (!m_d3d12_swapchain)
    {
        return true;
    }

    m_current_swapchain_image_index = m_d3d12_swapchain->GetCurrentBackBufferIndex();
    m_current_command_buffer = m_frame_command_buffers[m_current_frame_index % m_frame_command_buffers.size()];

    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(m_current_command_buffer);
    if (d3d_command_buffer != nullptr && ensureCommandBufferObjects(m_current_command_buffer))
    {
        if (!d3d_command_buffer->is_open)
        {
            if (FAILED(d3d_command_buffer->command_allocator->Reset()) ||
                FAILED(d3d_command_buffer->command_list->Reset(d3d_command_buffer->command_allocator.Get(), nullptr)))
            {
                LOG_ERROR("D3D12 prepareBeforePass failed to reset command buffer for frame {}",
                          static_cast<uint32_t>(m_current_frame_index));
                return true;
            }
            d3d12_detail::resetCommandBufferRecordingState(*d3d_command_buffer,
                                                           m_d3d12_transient_cbv_srv_uav_descriptor_next);
        }
    }
#endif
    return false;
}
void D3D12RHI::submitRendering(std::function<void()> passUpdateAfterRecreateSwapchain)
{
#ifdef _WIN32
    auto* d3d_command_buffer = static_cast<D3D12RHICommandBuffer*>(m_current_command_buffer);
    if (!m_d3d12_swapchain ||
        !m_d3d12_command_queue ||
        d3d_command_buffer == nullptr ||
        d3d_command_buffer->command_list == nullptr)
    {
        LOG_WARN("D3D12 submitRendering skipped because swapchain, command queue, or current command list is unavailable");
        return;
    }

    auto update_current_frame = [this]() {
        if (m_d3d12_swapchain != nullptr)
        {
            m_current_swapchain_image_index = m_d3d12_swapchain->GetCurrentBackBufferIndex();
        }
        m_current_frame_index =
            static_cast<uint8_t>((m_current_frame_index + 1U) % (std::max)(1U, m_swapchain_buffer_count));
        m_current_command_buffer = m_frame_command_buffers[m_current_frame_index % m_frame_command_buffers.size()];
    };

    if (d3d_command_buffer->is_open)
    {
        const HRESULT close_result = d3d_command_buffer->command_list->Close();
        if (FAILED(close_result))
        {
            const HRESULT removed_reason =
                m_d3d12_device != nullptr ? m_d3d12_device->GetDeviceRemovedReason() : S_OK;
            LOG_ERROR("D3D12 submitRendering command list close failed (HRESULT=0x{:08X}, removed_reason=0x{:08X})",
                      static_cast<unsigned int>(close_result),
                      static_cast<unsigned int>(removed_reason));
            return;
        }
        d3d_command_buffer->is_open = false;
    }
    d3d_command_buffer->has_recorded_commands = true;

    RHIFence* current_frame_fence = m_frame_fences[m_current_frame_index % m_frame_fences.size()];
    if (current_frame_fence != nullptr && !resetFencesPFN(1, &current_frame_fence))
    {
        LOG_ERROR("D3D12 submitRendering failed to reset frame fence for frame {}",
                  static_cast<uint32_t>(m_current_frame_index));
        return;
    }

    RHICommandBuffer* submit_command_buffer = m_current_command_buffer;
    RHISemaphore*     copy_ready_semaphore =
        m_copy_ready_semaphores[m_current_frame_index % m_copy_ready_semaphores.size()];
    const RHISemaphore* signal_semaphores[] = {copy_ready_semaphore};
    RHISubmitInfo     submit_info {};
    submit_info.sType              = RHI_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers    = &submit_command_buffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores    = signal_semaphores;
    if (!queueSubmit(m_graphics_queue, 1, &submit_info, current_frame_fence))
    {
        const HRESULT removed_reason =
            m_d3d12_device != nullptr ? m_d3d12_device->GetDeviceRemovedReason() : S_OK;
        LOG_ERROR("D3D12 submitRendering queue submit failed (removed_reason=0x{:08X})",
                  static_cast<unsigned int>(removed_reason));
        return;
    }

    const UINT present_flags = m_allow_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0;
    const HRESULT present_result = m_d3d12_swapchain->Present(0, present_flags);
    if (FAILED(present_result))
    {
        waitForGpu();

        const HRESULT removed_reason =
            m_d3d12_device != nullptr ? m_d3d12_device->GetDeviceRemovedReason() : S_OK;
        LOG_ERROR("D3D12 submitRendering Present failed (HRESULT=0x{:08X}, removed_reason=0x{:08X})",
                  static_cast<unsigned int>(present_result),
                  static_cast<unsigned int>(removed_reason));

        if (present_result == DXGI_ERROR_DEVICE_REMOVED || present_result == DXGI_ERROR_DEVICE_RESET)
        {
            LOG_ERROR("D3D12 submitRendering detected device loss during Present (HRESULT=0x{:08X}, removed_reason=0x{:08X})",
                      static_cast<unsigned int>(present_result),
                      static_cast<unsigned int>(removed_reason));
            return;
        }

        int framebuffer_width  = static_cast<int>(m_window_width);
        int framebuffer_height = static_cast<int>(m_window_height);
        if (m_window != nullptr)
        {
            glfwGetFramebufferSize(m_window, &framebuffer_width, &framebuffer_height);
        }

        if (framebuffer_width <= 0 || framebuffer_height <= 0)
        {
            LOG_ERROR("D3D12 submitRendering cannot recover swapchain after Present failure because framebuffer size is invalid (width={}, height={}, HRESULT=0x{:08X}, removed_reason=0x{:08X})",
                      framebuffer_width,
                      framebuffer_height,
                      static_cast<unsigned int>(present_result),
                      static_cast<unsigned int>(removed_reason));
            return;
        }

        const uint32_t requested_width  = static_cast<uint32_t>(framebuffer_width);
        const uint32_t requested_height = static_cast<uint32_t>(framebuffer_height);
        recreateSwapchain();
        if (m_d3d12_swapchain != nullptr &&
            m_swapchain_desc.extent.width == requested_width &&
            m_swapchain_desc.extent.height == requested_height &&
            m_swapchain_desc.imageViews.size() == m_swapchain_buffer_count)
        {
            if (passUpdateAfterRecreateSwapchain)
            {
                passUpdateAfterRecreateSwapchain();
            }
            update_current_frame();
            return;
        }

        LOG_ERROR("D3D12 submitRendering failed to recover swapchain after Present failure (HRESULT=0x{:08X}, removed_reason=0x{:08X})",
                  static_cast<unsigned int>(present_result),
                  static_cast<unsigned int>(removed_reason));
        return;
    }

    update_current_frame();
#else
    (void)passUpdateAfterRecreateSwapchain;
#endif
    return;
}
void D3D12RHI::pushEvent(RHICommandBuffer* commond_buffer, const char* name, const float* color)
{
#ifdef _WIN32
    (void)color;

    auto* command_list = d3d12CommandListFor(commond_buffer);
    if (command_list == nullptr || name == nullptr || name[0] == '\0')
    {
        return;
    }

    constexpr UINT pix_event_ansi_version = 1;
    const UINT     event_name_size        = static_cast<UINT>(std::strlen(name) + 1);
    command_list->BeginEvent(pix_event_ansi_version, name, event_name_size);
#else
    (void)commond_buffer;
    (void)name;
    (void)color;
#endif
    return;
}
void D3D12RHI::popEvent(RHICommandBuffer* commond_buffer)
{
#ifdef _WIN32
    auto* command_list = d3d12CommandListFor(commond_buffer);
    if (command_list != nullptr)
    {
        command_list->EndEvent();
    }
#else
    (void)commond_buffer;
#endif
    return;
}
} // namespace Piccolo
