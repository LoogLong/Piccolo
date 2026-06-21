#include "runtime/function/render/passes/gpu_skinning_pass.h"

#include "runtime/core/base/macro.h"
#include "runtime/function/render/render_resource.h"
#include "runtime/function/render/render_scene.h"
#include "runtime/function/render/render_shader_bytecode.h"

#include <algorithm>
#include <cstring>

namespace Piccolo
{
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
        if (m_rhi->getBackendType() != RHIBackendType::D3D12 ||
            m_rhi->getRayTracingCapabilities().support_level != RHIRayTracingSupportLevel::Supported)
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

        // Descriptor set layout: 8 bindings (t0-t4 SRV, b0 UBO, u0-u1 UAV)
        {
            RHIDescriptorSetLayoutBinding bindings[8] {};
            bindings[0].binding         = 0;
            bindings[0].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

            bindings[1].binding         = 1;
            bindings[1].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

            bindings[2].binding         = 2;
            bindings[2].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[2].descriptorCount = 1;
            bindings[2].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

            bindings[3].binding         = 3;
            bindings[3].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[3].descriptorCount = 1;
            bindings[3].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

            bindings[4].binding         = 4;
            bindings[4].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[4].descriptorCount = 1;
            bindings[4].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

            bindings[5].binding         = 5;
            bindings[5].descriptorType  = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            bindings[5].descriptorCount = 1;
            bindings[5].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

            bindings[6].binding         = 6;
            bindings[6].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[6].descriptorCount = 1;
            bindings[6].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

            bindings[7].binding         = 7;
            bindings[7].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[7].descriptorCount = 1;
            bindings[7].stageFlags      = RHI_SHADER_STAGE_COMPUTE_BIT;

            RHIDescriptorSetLayoutCreateInfo layout_info {};
            layout_info.sType        = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layout_info.bindingCount = 8;
            layout_info.pBindings    = bindings;
            if (RHI_SUCCESS != m_rhi->createDescriptorSetLayout(&layout_info, m_skin_compute_descriptor_set_layout))
            {
                return false;
            }
        }

        // Pipeline layout
        {
            RHIDescriptorSetLayout* set_layouts[1] = {m_skin_compute_descriptor_set_layout};
            RHIPipelineLayoutCreateInfo layout_info {};
            layout_info.sType          = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout_info.setLayoutCount = 1;
            layout_info.pSetLayouts    = set_layouts;
            if (RHI_SUCCESS != m_rhi->createPipelineLayout(&layout_info, m_skin_compute_pipeline_layout))
            {
                return false;
            }
        }

        // Allocate descriptor set
        {
            RHIDescriptorSetAllocateInfo allocate_info {};
            allocate_info.sType              = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocate_info.descriptorPool     = m_rhi->getDescriptorPoor();
            allocate_info.descriptorSetCount = 1;
            allocate_info.pSetLayouts        = &m_skin_compute_descriptor_set_layout;
            if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&allocate_info, m_skin_compute_descriptor_set))
            {
                return false;
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

        // Allocate persistent constants buffer (16 bytes, mapped per dispatch)
        if (m_skin_constants_buffer == nullptr)
        {
            m_rhi->createBuffer(
                16, RHI_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                m_skin_constants_buffer, m_skin_constants_memory);
        }

        return true;
    }

    bool GpuSkinningPass::uploadJointMatrices(const std::vector<RenderPathTracingCollectedInstance>& instances)
    {
        uint32_t total_joint_count = 0;
        for (const auto& inst : instances)
        {
            if (inst.enable_vertex_blending && inst.joint_count > 0)
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
                m_rhi->destroyBuffer(m_joint_matrix_buffer);
                m_joint_matrix_buffer = nullptr;
            }
            if (m_joint_matrix_memory != nullptr)
            {
                m_rhi->freeMemory(m_joint_matrix_memory);
                m_joint_matrix_memory = nullptr;
            }

            m_joint_matrix_buffer_capacity = data_size * 2;

            m_rhi->createBuffer(
                m_joint_matrix_buffer_capacity,
                RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                RHI_MEMORY_PROPERTY_HOST_VISIBLE_BIT | RHI_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                m_joint_matrix_buffer,
                m_joint_matrix_memory);
        }

        void* mapped = nullptr;
        if (!m_rhi->mapMemory(m_joint_matrix_memory, 0, data_size, 0, &mapped) || mapped == nullptr)
        {
            return false;
        }

        size_t offset = 0;
        for (const auto& inst : instances)
        {
            if (inst.enable_vertex_blending && inst.joint_count > 0 && inst.joint_matrices != nullptr)
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
                                              const std::vector<RenderPathTracingCollectedInstance>& instances)
    {
        if (m_skin_compute_pipeline == nullptr || command_buffer == nullptr) return;

        uint32_t joint_matrix_offset = 0;
        uint32_t skinned_vertex_acc_offset = 0;

        for (const auto& inst : instances)
        {
            if (!inst.enable_vertex_blending || inst.mesh == nullptr) continue;
            if (inst.joint_count == 0 || inst.joint_matrices == nullptr) continue;

            RenderMeshGPUResource* mesh = inst.mesh;
            uint32_t vertex_count = mesh->mesh_vertex_count;
            if (vertex_count == 0) continue;

            // Per-instance skinned resources (for BLAS geometry source)
            uint32_t inst_id = inst.instance_id;
            auto& resources = mesh->path_tracing_skinned_resources[inst_id];

            // Create per-instance position buffer if needed
            if (resources.skinned_position_buffer == nullptr || resources.vertex_count != vertex_count)
            {
                if (resources.skinned_position_buffer != nullptr)
                    m_rhi->destroyBuffer(resources.skinned_position_buffer);
                if (resources.skinned_position_memory != nullptr)
                    m_rhi->freeMemory(resources.skinned_position_memory);

                m_rhi->createBuffer(
                    vertex_count * sizeof(float) * 3,
                    RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                        RHI_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                        RHI_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    resources.skinned_position_buffer,
                    resources.skinned_position_memory);
                resources.vertex_count = vertex_count;
                resources.index_count  = mesh->mesh_index_count;
            }

            RHIWriteDescriptorSet writes[8] {};

            // Write 0: rest-pose positions (t0)
            RHIDescriptorBufferInfo rest_positions_info {};
            rest_positions_info.buffer = mesh->mesh_vertex_position_buffer;
            rest_positions_info.offset = 0;
            rest_positions_info.range  = RHI_WHOLE_SIZE;
            writes[0].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet          = m_skin_compute_descriptor_set;
            writes[0].dstBinding      = 0;
            writes[0].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo     = &rest_positions_info;

            // Write 1: joint bindings (t1)
            RHIDescriptorBufferInfo joint_bindings_info {};
            joint_bindings_info.buffer = mesh->mesh_vertex_joint_binding_buffer;
            joint_bindings_info.offset = 0;
            joint_bindings_info.range  = RHI_WHOLE_SIZE;
            writes[1].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet          = m_skin_compute_descriptor_set;
            writes[1].dstBinding      = 1;
            writes[1].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo     = &joint_bindings_info;

            // Write 2: rest-pose normal+tangent interleaved (t2)
            RHIDescriptorBufferInfo normal_tangent_info {};
            normal_tangent_info.buffer = mesh->mesh_vertex_varying_enable_blending_buffer;
            normal_tangent_info.offset = 0;
            normal_tangent_info.range  = RHI_WHOLE_SIZE;
            writes[2].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet          = m_skin_compute_descriptor_set;
            writes[2].dstBinding      = 2;
            writes[2].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[2].descriptorCount = 1;
            writes[2].pBufferInfo     = &normal_tangent_info;

            // Write 3: rest-pose texcoords (t3)
            RHIDescriptorBufferInfo texcoords_info {};
            texcoords_info.buffer = mesh->mesh_vertex_varying_buffer;
            texcoords_info.offset = 0;
            texcoords_info.range  = RHI_WHOLE_SIZE;
            writes[3].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[3].dstSet          = m_skin_compute_descriptor_set;
            writes[3].dstBinding      = 3;
            writes[3].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[3].descriptorCount = 1;
            writes[3].pBufferInfo     = &texcoords_info;

            // Write 4: joint matrices (t4)
            RHIDescriptorBufferInfo joint_matrices_info {};
            joint_matrices_info.buffer = m_joint_matrix_buffer;
            joint_matrices_info.offset = joint_matrix_offset * sizeof(Matrix4x4);
            joint_matrices_info.range  = RHI_WHOLE_SIZE;
            writes[4].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[4].dstSet          = m_skin_compute_descriptor_set;
            writes[4].dstBinding      = 4;
            writes[4].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[4].descriptorCount = 1;
            writes[4].pBufferInfo     = &joint_matrices_info;

            // Write 5: SkinComputeConstants (b5) — persistent buffer, mapped per dispatch
            {
                struct { uint32_t vc; uint32_t jmo; uint32_t ovo; uint32_t pad; }
                    constants = { vertex_count, joint_matrix_offset, skinned_vertex_acc_offset, 0 };
                void* mapped_cb = nullptr;
                m_rhi->mapMemory(m_skin_constants_memory, 0, 16, 0, &mapped_cb);
                std::memcpy(mapped_cb, &constants, sizeof(constants));
                m_rhi->unmapMemory(m_skin_constants_memory);
            }

            RHIDescriptorBufferInfo constants_info {};
            constants_info.buffer = m_skin_constants_buffer;
            constants_info.offset = 0;
            constants_info.range  = 16;
            writes[5].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[5].dstSet          = m_skin_compute_descriptor_set;
            writes[5].dstBinding      = 5;
            writes[5].descriptorType  = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[5].descriptorCount = 1;
            writes[5].pBufferInfo     = &constants_info;

            // Write 6: skinned positions → PER-INSTANCE buffer (BLAS geometry source), offset 0
            RHIDescriptorBufferInfo skinned_positions_info {};
            skinned_positions_info.buffer = resources.skinned_position_buffer;
            skinned_positions_info.offset = 0;
            skinned_positions_info.range  = RHI_WHOLE_SIZE;
            writes[6].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[6].dstSet          = m_skin_compute_descriptor_set;
            writes[6].dstBinding      = 6;
            writes[6].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[6].descriptorCount = 1;
            writes[6].pBufferInfo     = &skinned_positions_info;

            // Write 7: skinned vertex data → FLAT buffer at cumulative offset
            RHIDescriptorBufferInfo skinned_vertices_info {};
            skinned_vertices_info.buffer = m_skinned_vertex_output_buffer;
            skinned_vertices_info.offset = skinned_vertex_acc_offset * sizeof(RenderPathTracingVertexGPUData);
            skinned_vertices_info.range  = RHI_WHOLE_SIZE;
            writes[7].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[7].dstSet          = m_skin_compute_descriptor_set;
            writes[7].dstBinding      = 7;
            writes[7].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[7].descriptorCount = 1;
            writes[7].pBufferInfo     = &skinned_vertices_info;

            m_rhi->updateDescriptorSets(8, writes, 0, nullptr);

            m_rhi->cmdBindPipelinePFN(command_buffer, RHI_PIPELINE_BIND_POINT_COMPUTE, m_skin_compute_pipeline);
            m_rhi->cmdBindDescriptorSetsPFN(command_buffer, RHI_PIPELINE_BIND_POINT_COMPUTE,
                                             m_skin_compute_pipeline_layout, 0, 1,
                                             &m_skin_compute_descriptor_set, 0, nullptr);

            uint32_t group_count = (vertex_count + 63) / 64;
            m_rhi->cmdDispatch(command_buffer, group_count, 1, 1);

            joint_matrix_offset += s_mesh_vertex_blending_max_joint_count;
            skinned_vertex_acc_offset += vertex_count;
        }

        // UAV barrier: compute writes → acceleration structure build reads
        RHIMemoryBarrier memory_barrier {};
        memory_barrier.sType         = RHI_STRUCTURE_TYPE_MEMORY_BARRIER;
        memory_barrier.srcAccessMask = RHI_ACCESS_SHADER_WRITE_BIT;
        memory_barrier.dstAccessMask = RHI_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        m_rhi->cmdPipelineBarrier(command_buffer,
                                   RHI_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   RHI_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
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

        auto render_scene = m_render_resource_impl->getCurrentRenderScene();
        if (render_scene == nullptr) return false;

        auto collected_instances = m_render_resource_impl->collectPathTracingInstances(*render_scene);

        const bool has_skinned = std::any_of(collected_instances.begin(), collected_instances.end(),
            [](const RenderPathTracingCollectedInstance& i) { return i.enable_vertex_blending; });

        if (!has_skinned) return true;

        if (!uploadJointMatrices(collected_instances))
        {
            LOG_WARN("GpuSkinningPass: failed to upload joint matrices");
            return false;
        }

        RHICommandBuffer* command_buffer = m_rhi->getCurrentCommandBuffer();
        if (command_buffer == nullptr) return false;

        // Ensure flat skinned vertex output buffer is allocated
        uint32_t total_skinned_vertices = 0;
        for (const auto& inst : collected_instances)
        {
            if (inst.enable_vertex_blending && inst.mesh != nullptr)
            {
                total_skinned_vertices += inst.mesh->mesh_vertex_count;
            }
        }

        size_t required_size = total_skinned_vertices * sizeof(RenderPathTracingVertexGPUData);
        if (required_size > m_skinned_vertex_output_capacity)
        {
            if (m_skinned_vertex_output_buffer != nullptr)
                m_rhi->destroyBuffer(m_skinned_vertex_output_buffer);
            if (m_skinned_vertex_output_memory != nullptr)
                m_rhi->freeMemory(m_skinned_vertex_output_memory);

            m_skinned_vertex_output_capacity = required_size * 2;
            m_rhi->createBuffer(
                m_skinned_vertex_output_capacity,
                RHI_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_skinned_vertex_output_buffer,
                m_skinned_vertex_output_memory);
        }

        // Expose the flat vertex buffer to other passes via RenderResource
        m_render_resource_impl->setSkinnedVertexBuffer(m_skinned_vertex_output_buffer);

        dispatchSkinCompute(command_buffer, collected_instances);
        return true;
    }
} // namespace Piccolo
