#include "runtime/function/render/passes/gpu_skinning_pass.h"

#include "runtime/core/base/macro.h"
#include "runtime/function/render/render_resource.h"
#include "runtime/function/render/render_scene.h"
#include "runtime/function/render/render_shader_bytecode.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Piccolo
{
    namespace
    {
        RHIDescriptorSetLayoutBinding makeStorageBinding(uint32_t binding,
                                                         RHIShaderStageFlags stage_flags =
                                                             RHI_SHADER_STAGE_COMPUTE_BIT |
                                                             RHI_SHADER_STAGE_VERTEX_BIT)
        {
            RHIDescriptorSetLayoutBinding layout_binding {};
            layout_binding.binding         = binding;
            layout_binding.descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            layout_binding.descriptorCount = 1;
            layout_binding.stageFlags      = stage_flags;
            return layout_binding;
        }
    }

    void GpuSkinningPass::initialize(const RenderPassInitInfo* init_info)
    {
        RenderPass::initialize(nullptr);
    }

    void GpuSkinningPass::preparePassData(std::shared_ptr<RenderResourceBase> render_resource)
    {
        m_render_resource_impl = std::static_pointer_cast<RenderResource>(render_resource);
    }

    bool GpuSkinningPass::setup()
    {
        if (m_skin_compute_pipeline != nullptr) return true;
        if (m_rhi == nullptr) return false;
        if (!m_rhi->supportsRayTracing())
        {
            return false;
        }
        return setupSkinComputePipeline();
    }

    bool GpuSkinningPass::setupSkinComputePipeline()
    {
        if (m_skin_compute_pipeline != nullptr) return true;
        if (m_rhi == nullptr) return false;

        const std::vector<unsigned char>& bytecode =
            PICCOLO_RENDER_SHADER_BYTECODE(m_rhi, GPU_SKINNING_COMP);
        if (bytecode.empty())
        {
            return false;
        }

        // Set 0 — mesh static (t0-t3)
        {
            RHIDescriptorSetLayoutBinding bindings[4] {};
            bindings[0] = makeStorageBinding(0);
            bindings[1] = makeStorageBinding(1);
            bindings[2] = makeStorageBinding(2);
            bindings[3] = makeStorageBinding(3);

            RHIDescriptorSetLayoutCreateInfo layout_info {};
            layout_info.sType        = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layout_info.bindingCount = 4;
            layout_info.pBindings    = bindings;
            if (RHI_SUCCESS != m_rhi->createDescriptorSetLayout(&layout_info, m_skin_mesh_descriptor_set_layout))
            {
                return false;
            }
        }

        // Set 1 — frame shared (t4 joint matrices, b5 constants, u7 flat output)
        {
            RHIDescriptorSetLayoutBinding bindings[3] {};
            bindings[0] = makeStorageBinding(0);
            bindings[1].binding         = 1;
            bindings[1].descriptorType  = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;
            bindings[2].binding         = 2;
            bindings[2].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[2].descriptorCount = 1;
            bindings[2].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

            RHIDescriptorSetLayoutCreateInfo layout_info {};
            layout_info.sType        = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layout_info.bindingCount = 3;
            layout_info.pBindings    = bindings;
            if (RHI_SUCCESS != m_rhi->createDescriptorSetLayout(&layout_info, m_skin_frame_descriptor_set_layout))
            {
                return false;
            }
        }

        // Set 2 — instance dynamic (u6 skinned positions)
        {
            RHIDescriptorSetLayoutBinding bindings[1] {};
            bindings[0].binding         = 0;
            bindings[0].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

            RHIDescriptorSetLayoutCreateInfo layout_info {};
            layout_info.sType        = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layout_info.bindingCount = 1;
            layout_info.pBindings    = bindings;
            if (RHI_SUCCESS != m_rhi->createDescriptorSetLayout(&layout_info, m_skin_instance_descriptor_set_layout))
            {
                return false;
            }
        }

        // Pipeline layout with 3 descriptor sets
        {
            RHIDescriptorSetLayout* set_layouts[3] = {
                m_skin_mesh_descriptor_set_layout,
                m_skin_frame_descriptor_set_layout,
                m_skin_instance_descriptor_set_layout};
            RHIPipelineLayoutCreateInfo layout_info {};
            layout_info.sType          = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout_info.setLayoutCount = 3;
            layout_info.pSetLayouts    = set_layouts;
            if (RHI_SUCCESS != m_rhi->createPipelineLayout(&layout_info, m_skin_compute_pipeline_layout))
            {
                return false;
            }
        }

        // Per-frame shared descriptor sets
        {
            const uint32_t frame_count = m_rhi->getMaxFramesInFlight();
            for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index)
            {
                RHIDescriptorSetAllocateInfo allocate_info {};
                allocate_info.sType              = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                allocate_info.descriptorPool     = m_rhi->getDescriptorPoor();
                allocate_info.descriptorSetCount = 1;
                allocate_info.pSetLayouts        = &m_skin_frame_descriptor_set_layout;
                if (RHI_SUCCESS !=
                    m_rhi->allocateDescriptorSets(&allocate_info, m_frame_shared_descriptor_sets[frame_index]))
                {
                    return false;
                }

                char debug_name[64];
                std::snprintf(debug_name,
                              sizeof(debug_name),
                              "GpuSkinning.frame_descriptor_set[%u]",
                              frame_index);
                m_rhi->setDebugObjectName(m_frame_shared_descriptor_sets[frame_index], debug_name);
            }
        }

        // Compute pipeline
        {
            RHIComputePipelineCreateInfo pipeline_info {};
            pipeline_info.sType  = RHI_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            pipeline_info.layout = m_skin_compute_pipeline_layout;

            RHIShader* module = m_rhi->createShaderModule(bytecode);
            if (module == RHI_NULL_HANDLE) return false;

            RHIPipelineShaderStageCreateInfo stage {};
            stage.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.stage  = RHI_SHADER_STAGE_COMPUTE_BIT;
            stage.module = module;
            stage.pName  = "main";

            pipeline_info.pStages = &stage;
            if (RHI_SUCCESS !=
                m_rhi->createComputePipelines(RHI_NULL_HANDLE, 1, &pipeline_info, m_skin_compute_pipeline))
            {
                m_rhi->destroyShaderModule(module);
                return false;
            }

            m_rhi->destroyShaderModule(module);
        }

        if (m_skin_constants_buffer == nullptr)
        {
            m_rhi->createBuffer(
                16, RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                m_skin_constants_buffer, m_skin_constants_memory);
        }

        return true;
    }

    void GpuSkinningPass::flushPendingBufferDestroys(bool force_all)
    {
        if (m_rhi == nullptr)
        {
            m_pending_destroy_buffers.clear();
            return;
        }

        const uint64_t max_frames_in_flight = m_rhi->getMaxFramesInFlight();
        auto it = m_pending_destroy_buffers.begin();
        while (it != m_pending_destroy_buffers.end())
        {
            if (force_all ||
                it->queued_at_dispatch_index + max_frames_in_flight + 1U <= m_dispatch_index)
            {
                if (it->buffer != nullptr)
                {
                    m_rhi->destroyBuffer(it->buffer);
                }
                if (it->memory != nullptr)
                {
                    m_rhi->freeMemory(it->memory);
                }
                it = m_pending_destroy_buffers.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void GpuSkinningPass::queueBufferDestroy(RHIBuffer* buffer, RHIDeviceMemory* memory)
    {
        if (buffer == nullptr && memory == nullptr)
        {
            return;
        }
        m_pending_destroy_buffers.push_back({buffer, memory, m_dispatch_index});
    }

    void GpuSkinningPass::updateFrameSharedDescriptorSet(RHIDescriptorSet* frame_set)
    {
        if (frame_set == nullptr || m_skin_constants_buffer == nullptr)
        {
            return;
        }

        if (m_joint_matrix_buffer == nullptr || m_skinned_vertex_output_buffer == nullptr)
        {
            return;
        }

        RHIDescriptorBufferInfo joint_matrices_info {};
        joint_matrices_info.buffer = m_joint_matrix_buffer;
        joint_matrices_info.offset = 0;
        joint_matrices_info.range  = RHI_WHOLE_SIZE;

        RHIDescriptorBufferInfo constants_info {};
        constants_info.buffer = m_skin_constants_buffer;
        constants_info.offset = 0;
        constants_info.range  = 16;

        RHIDescriptorBufferInfo skinned_vertices_info {};
        skinned_vertices_info.buffer = m_skinned_vertex_output_buffer;
        skinned_vertices_info.offset = 0;
        skinned_vertices_info.range  = RHI_WHOLE_SIZE;

        RHIWriteDescriptorSet writes[3] {};
        writes[0].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = frame_set;
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo     = &joint_matrices_info;

        writes[1].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = frame_set;
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo     = &constants_info;

        writes[2].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = frame_set;
        writes[2].dstBinding      = 2;
        writes[2].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo     = &skinned_vertices_info;

        m_rhi->updateDescriptorSets(3, writes, 0, nullptr);
    }

    void GpuSkinningPass::updateAllFrameSharedDescriptorSets()
    {
        const uint32_t frame_count = m_rhi->getMaxFramesInFlight();
        for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index)
        {
            updateFrameSharedDescriptorSet(m_frame_shared_descriptor_sets[frame_index]);
        }
    }

    void GpuSkinningPass::ensureInstanceDescriptorSet(RenderMeshGPUResource* mesh,
                                                      RenderMeshGPUResource::SkinnedMeshOutput& output,
                                                      uint8_t frame_index)
    {
        if (output.gpu_skinning_instance_sets_allocated &&
            output.gpu_skinning_instance_sets[frame_index] != nullptr)
        {
            return;
        }

        RHIDescriptorSetAllocateInfo allocate_info {};
        allocate_info.sType              = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate_info.descriptorPool     = m_rhi->getDescriptorPoor();
        allocate_info.descriptorSetCount = 1;
        allocate_info.pSetLayouts        = &m_skin_instance_descriptor_set_layout;
        if (RHI_SUCCESS !=
            m_rhi->allocateDescriptorSets(&allocate_info, output.gpu_skinning_instance_sets[frame_index]))
        {
            throw std::runtime_error("allocate gpu skinning instance descriptor set");
        }

        output.gpu_skinning_instance_sets_allocated = true;

        char debug_name[96];
        std::snprintf(debug_name,
                      sizeof(debug_name),
                      "GpuSkinning.instance_descriptor_set[frame%u]",
                      frame_index);
        m_rhi->setDebugObjectName(output.gpu_skinning_instance_sets[frame_index], debug_name);
        (void)mesh;
    }

    void GpuSkinningPass::updateInstanceDescriptorSet(RenderMeshGPUResource::SkinnedMeshOutput& output,
                                                      uint8_t frame_index,
                                                      RHIBuffer* position_buffer)
    {
        RHIDescriptorSet* instance_set = output.gpu_skinning_instance_sets[frame_index];
        if (instance_set == nullptr || position_buffer == nullptr)
        {
            return;
        }

        RHIDescriptorBufferInfo skinned_positions_info {};
        skinned_positions_info.buffer = position_buffer;
        skinned_positions_info.offset = 0;
        skinned_positions_info.range  = RHI_WHOLE_SIZE;

        RHIWriteDescriptorSet write {};
        write.sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = instance_set;
        write.dstBinding      = 0;
        write.descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo     = &skinned_positions_info;

        m_rhi->updateDescriptorSets(1, &write, 0, nullptr);
    }

    bool GpuSkinningPass::uploadJointMatrices(const std::vector<CollectedSkinnedMesh>& instances)
    {
        uint32_t total_joint_count = 0;
        for (const auto& inst : instances)
        {
            if (inst.joint_count > 0)
            {
                total_joint_count += inst.joint_count;
            }
        }
        if (total_joint_count == 0) return true;

        size_t data_size = total_joint_count * sizeof(Matrix4x4);
        if (data_size > m_joint_matrix_buffer_capacity)
        {
            if (m_joint_matrix_buffer != nullptr)
            {
                queueBufferDestroy(m_joint_matrix_buffer, m_joint_matrix_memory);
                m_joint_matrix_buffer = nullptr;
                m_joint_matrix_memory = nullptr;
            }

            m_joint_matrix_buffer_capacity = data_size * 2;

            m_rhi->createBuffer(
                m_joint_matrix_buffer_capacity,
                RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                m_joint_matrix_buffer,
                m_joint_matrix_memory);
            m_rhi->setDebugObjectName(m_joint_matrix_buffer, "GpuSkinning.joint_matrix_buffer");
            updateAllFrameSharedDescriptorSets();
        }

        void* mapped = nullptr;
        if (!m_rhi->mapMemory(m_joint_matrix_memory, 0, data_size, 0, &mapped) || mapped == nullptr)
        {
            return false;
        }

        size_t offset = 0;
        for (const auto& inst : instances)
        {
            if (inst.joint_count > 0 && inst.joint_matrices != nullptr)
            {
                size_t bytes = inst.joint_count * sizeof(Matrix4x4);
                std::memcpy(static_cast<uint8_t*>(mapped) + offset, inst.joint_matrices, bytes);
                offset += s_mesh_vertex_blending_max_joint_count * sizeof(Matrix4x4);
            }
        }
        m_rhi->unmapMemory(m_joint_matrix_memory);
        return true;
    }

    void GpuSkinningPass::dispatchSkinCompute(RHICommandBuffer* command_buffer,
                                              const std::vector<CollectedSkinnedMesh>& instances)
    {
        if (m_skin_compute_pipeline == nullptr || command_buffer == nullptr) return;

        const uint8_t frame_index = m_rhi->getCurrentFrameIndex();
        RHIDescriptorSet* frame_set = m_frame_shared_descriptor_sets[frame_index];
        updateFrameSharedDescriptorSet(frame_set);

        uint32_t joint_matrix_offset = 0;
        uint32_t skinned_vertex_acc_offset = 0;

        for (const auto& csm : instances)
        {
            if (csm.mesh == nullptr) continue;
            if (csm.joint_count == 0 || csm.joint_matrices == nullptr) continue;

            RenderMeshGPUResource* mesh = csm.mesh;
            uint32_t vertex_count = mesh->mesh_vertex_count;
            if (vertex_count == 0) continue;

            uint32_t inst_id = csm.instance_id;
            auto& output = mesh->skinned_mesh_outputs[inst_id];
            output.skinned_vertex_offset = skinned_vertex_acc_offset;

            const size_t required_position_buffer_size =
                static_cast<size_t>(vertex_count) * kGpuSkinnedPositionStorageStrideBytes;
            if (output.skinned_position_buffer == nullptr || output.vertex_count != vertex_count ||
                output.skinned_position_buffer_size != required_position_buffer_size)
            {
                if (output.skinned_position_buffer != nullptr)
                {
                    queueBufferDestroy(output.skinned_position_buffer, output.skinned_position_memory);
                    output.skinned_position_buffer = nullptr;
                    output.skinned_position_memory = nullptr;
                }

                m_rhi->createBuffer(
                    required_position_buffer_size,
                    RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                        RHI_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                        RHI_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    output.skinned_position_buffer,
                    output.skinned_position_memory);
                char skinned_position_debug_name[64];
                std::snprintf(skinned_position_debug_name,
                              sizeof(skinned_position_debug_name),
                              "GpuSkinning.skinned_position[inst%u]",
                              inst_id);
                m_rhi->setDebugObjectName(output.skinned_position_buffer, skinned_position_debug_name);
                output.vertex_count                 = vertex_count;
                output.index_count                  = mesh->mesh_index_count;
                output.skinned_position_buffer_size = required_position_buffer_size;
            }

            ensureInstanceDescriptorSet(mesh, output, frame_index);
            updateInstanceDescriptorSet(output, frame_index, output.skinned_position_buffer);

            struct SkinComputeConstants
            {
                uint32_t vertex_count;
                uint32_t joint_matrix_offset;
                uint32_t output_vertex_offset;
                uint32_t padding;
            } constants = {vertex_count, joint_matrix_offset, skinned_vertex_acc_offset, 0};

            void* mapped_cb = nullptr;
            m_rhi->mapMemory(m_skin_constants_memory, 0, sizeof(constants), 0, &mapped_cb);
            std::memcpy(mapped_cb, &constants, sizeof(constants));
            m_rhi->unmapMemory(m_skin_constants_memory);

            if (mesh->gpu_skinning_mesh_descriptor_set == nullptr)
            {
                joint_matrix_offset += s_mesh_vertex_blending_max_joint_count;
                skinned_vertex_acc_offset += vertex_count;
                continue;
            }

            RHIDescriptorSet* sets[3] = {
                mesh->gpu_skinning_mesh_descriptor_set,
                frame_set,
                output.gpu_skinning_instance_sets[frame_index]};

            m_rhi->cmdBindPipelinePFN(command_buffer, RHI_PIPELINE_BIND_POINT_COMPUTE, m_skin_compute_pipeline);
            m_rhi->cmdBindDescriptorSetsPFN(command_buffer,
                                             RHI_PIPELINE_BIND_POINT_COMPUTE,
                                             m_skin_compute_pipeline_layout,
                                             0,
                                             3,
                                             sets,
                                             0,
                                             nullptr);

            uint32_t group_count = (vertex_count + 63) / 64;
            m_rhi->cmdDispatch(command_buffer, group_count, 1, 1);

            joint_matrix_offset += s_mesh_vertex_blending_max_joint_count;
            skinned_vertex_acc_offset += vertex_count;
        }

        RHIMemoryBarrier memory_barrier {};
        memory_barrier.sType         = RHI_STRUCTURE_TYPE_MEMORY_BARRIER;
        memory_barrier.srcAccessMask = RHI_ACCESS_SHADER_WRITE_BIT;
        memory_barrier.dstAccessMask = RHI_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | RHI_ACCESS_SHADER_READ_BIT;
        m_rhi->cmdPipelineBarrier(command_buffer,
                                   RHI_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   RHI_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                                       RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                                   0,
                                   1, &memory_barrier,
                                   0, nullptr,
                                   0, nullptr);
    }

    bool GpuSkinningPass::dispatch()
    {
        if (m_rhi == nullptr || m_render_resource_impl == nullptr) return false;
        if (m_skin_compute_pipeline == nullptr)
        {
            if (!setup()) return false;
            if (m_skin_compute_pipeline == nullptr) return false;
        }

        flushPendingBufferDestroys();

        auto render_scene = m_render_resource_impl->getCurrentRenderScene();
        if (render_scene == nullptr) return false;

        std::vector<CollectedSkinnedMesh> skinned_meshes;
        for (RenderEntity& entity : render_scene->m_render_entities)
        {
            if (!entity.m_enable_vertex_blending) continue;
            if (entity.m_blend || entity.m_base_color_factor.w < 1.0f) continue;

            RenderMeshGPUResource* mesh = nullptr;
            try
            {
                mesh = &m_render_resource_impl->getEntityMesh(entity);
            }
            catch (const std::exception&)
            {
                continue;
            }

            if (mesh == nullptr || mesh->mesh_vertex_count == 0) continue;

            CollectedSkinnedMesh csm;
            csm.mesh           = mesh;
            csm.instance_id    = entity.m_instance_id;
            csm.joint_count    = static_cast<uint32_t>(entity.m_joint_matrices.size());
            csm.joint_matrices = entity.m_joint_matrices.data();
            skinned_meshes.push_back(csm);
        }

        if (skinned_meshes.empty()) return true;

        if (!uploadJointMatrices(skinned_meshes))
        {
            LOG_WARN("GpuSkinningPass: failed to upload joint matrices");
            return false;
        }

        RHICommandBuffer* command_buffer = m_rhi->getCurrentCommandBuffer();
        if (command_buffer == nullptr) return false;

        uint32_t total_skinned_vertices = 0;
        for (const auto& csm : skinned_meshes)
        {
            total_skinned_vertices += csm.mesh->mesh_vertex_count;
        }

        size_t required_size = total_skinned_vertices * sizeof(GpuSkinnedVertexGPUData);
        if (required_size > m_skinned_vertex_output_capacity)
        {
            if (m_skinned_vertex_output_buffer != nullptr)
            {
                queueBufferDestroy(m_skinned_vertex_output_buffer, m_skinned_vertex_output_memory);
                m_skinned_vertex_output_buffer = nullptr;
                m_skinned_vertex_output_memory = nullptr;
            }

            m_skinned_vertex_output_capacity = required_size * 2;
            m_rhi->createBuffer(
                m_skinned_vertex_output_capacity,
                RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_skinned_vertex_output_buffer,
                m_skinned_vertex_output_memory);
            m_rhi->setDebugObjectName(m_skinned_vertex_output_buffer, "GpuSkinning.skinned_vertex_output_buffer");
            updateAllFrameSharedDescriptorSets();
        }

        m_render_resource_impl->setSkinnedVertexBuffer(m_skinned_vertex_output_buffer);

        dispatchSkinCompute(command_buffer, skinned_meshes);
        ++m_dispatch_index;
        return true;
    }

    void GpuSkinningPass::teardown()
    {
        flushPendingBufferDestroys(true);

        if (m_rhi == nullptr)
        {
            return;
        }

        m_rhi->destroyPipeline(m_skin_compute_pipeline);
        m_rhi->destroyPipelineLayout(m_skin_compute_pipeline_layout);
        m_rhi->destroyDescriptorSetLayout(m_skin_mesh_descriptor_set_layout);
        m_rhi->destroyDescriptorSetLayout(m_skin_frame_descriptor_set_layout);
        m_rhi->destroyDescriptorSetLayout(m_skin_instance_descriptor_set_layout);
        m_skin_compute_pipeline = nullptr;
        m_skin_compute_pipeline_layout = nullptr;
        m_skin_mesh_descriptor_set_layout = nullptr;
        m_skin_frame_descriptor_set_layout = nullptr;
        m_skin_instance_descriptor_set_layout = nullptr;
        m_frame_shared_descriptor_sets.fill(nullptr);

        if (m_joint_matrix_buffer != nullptr)
        {
            m_rhi->destroyBuffer(m_joint_matrix_buffer);
        }
        if (m_joint_matrix_memory != nullptr)
        {
            m_rhi->freeMemory(m_joint_matrix_memory);
        }
        m_joint_matrix_buffer = nullptr;
        m_joint_matrix_memory = nullptr;
        m_joint_matrix_buffer_capacity = 0;

        if (m_skin_constants_buffer != nullptr)
        {
            m_rhi->destroyBuffer(m_skin_constants_buffer);
        }
        if (m_skin_constants_memory != nullptr)
        {
            m_rhi->freeMemory(m_skin_constants_memory);
        }
        m_skin_constants_buffer = nullptr;
        m_skin_constants_memory = nullptr;

        if (m_skinned_vertex_output_buffer != nullptr)
        {
            m_rhi->destroyBuffer(m_skinned_vertex_output_buffer);
        }
        if (m_skinned_vertex_output_memory != nullptr)
        {
            m_rhi->freeMemory(m_skinned_vertex_output_memory);
        }
        m_skinned_vertex_output_buffer = nullptr;
        m_skinned_vertex_output_memory = nullptr;
        m_skinned_vertex_output_capacity = 0;

        if (m_render_resource_impl != nullptr)
        {
            m_render_resource_impl->setSkinnedVertexBuffer(nullptr);
        }
    }
} // namespace Piccolo
