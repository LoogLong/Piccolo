#pragma once

#include "runtime/function/render/render_resource_base.h"
#include "runtime/function/render/render_type.h"
#include "runtime/function/render/render_gpu_resource.h"
#include "runtime/function/render/interface/rhi.h"

#include "runtime/function/render/render_common.h"

#include <array>
#include <cstdint>
#include <map>
#include <vector>
#include <cmath>

namespace Piccolo
{
    class RHI;
    class RenderPassBase;
    class RenderCamera;
    class RenderScene;
    class RenderEntity;

    struct RenderPathTracingCollectedInstance
    {
        RHIAccelerationStructure*       bottom_level_as {nullptr};
        std::array<float, 12>           row_major_3x4_transform {};
        uint32_t                        instance_id {0};
        uint32_t                        material_index {0};
        RenderMeshGPUResource*          mesh {nullptr};
        RenderPBRMaterialGPUResource*   material {nullptr};
        RenderEntity* entity {nullptr};
        // Skinning support -- transient per-frame data
        bool                            enable_vertex_blending {false};
        uint32_t                        joint_count {0};
        const Matrix4x4*                joint_matrices {nullptr};  // pointer to entity's joint matrices
    };

    struct RenderPathTracingVertexGPUData
    {
        Vector4 position {0.0f, 0.0f, 0.0f, 1.0f};
        Vector4 normal {0.0f, 1.0f, 0.0f, 0.0f};
        Vector4 tangent {1.0f, 0.0f, 0.0f, 0.0f};
        Vector4 texcoord {0.0f, 0.0f, 0.0f, 0.0f};
    };

    struct RenderPathTracingMaterialGPUData
    {
        Vector4 base_color_factor {1.0f, 1.0f, 1.0f, 1.0f};
        Vector4 emissive_factor {0.0f, 0.0f, 0.0f, 0.0f};
        Vector4 metallic_roughness_normal_occlusion {1.0f, 1.0f, 1.0f, 1.0f};
        uint32_t base_color_texture_index {UINT32_MAX};
        uint32_t metallic_roughness_texture_index {UINT32_MAX};
        uint32_t normal_texture_index {UINT32_MAX};
        uint32_t emissive_texture_index {UINT32_MAX};
        uint32_t flags {0};
        uint32_t _padding[3] {0, 0, 0};
    };

    struct RenderPathTracingGeometryGPUData
    {
        uint32_t vertex_offset {0};
        uint32_t index_offset {0};
        uint32_t index_count {0};
        uint32_t _padding {0};
    };

    struct RenderPathTracingInstanceGPUData
    {
        uint32_t geometry_index {0};
        uint32_t material_index {0};
        uint32_t entity_instance_id {0};
        uint32_t flags {0};
    };

    static constexpr uint32_t kPathTracingMaterialFlagDoubleSided = 1u << 0;

    struct RenderPathTracingMaterialTextureViews
    {
        RHIImageView* base_color_image_view {nullptr};
        RHIImageView* metallic_roughness_image_view {nullptr};
        RHIImageView* normal_image_view {nullptr};
        RHIImageView* emissive_image_view {nullptr};
    };

    struct IBLResource
    {
        RHIImage* _brdfLUT_texture_image;
        RHIImageView* _brdfLUT_texture_image_view;
        RHISampler* _brdfLUT_texture_sampler;
        RHIAllocation* _brdfLUT_texture_image_allocation;

        RHIImage* _irradiance_texture_image;
        RHIImageView* _irradiance_texture_image_view;
        RHISampler* _irradiance_texture_sampler;
        RHIAllocation* _irradiance_texture_image_allocation;

        RHIImage* _specular_texture_image;
        RHIImageView* _specular_texture_image_view;
        RHISampler* _specular_texture_sampler;
        RHIAllocation* _specular_texture_image_allocation;
    };

    struct IBLResourceData
    {
        void* _brdfLUT_texture_image_pixels;
        uint32_t             _brdfLUT_texture_image_width;
        uint32_t             _brdfLUT_texture_image_height;
        RHIFormat   _brdfLUT_texture_image_format;
        std::array<void*, 6> _irradiance_texture_image_pixels;
        uint32_t             _irradiance_texture_image_width;
        uint32_t             _irradiance_texture_image_height;
        RHIFormat   _irradiance_texture_image_format;
        std::array<void*, 6> _specular_texture_image_pixels;
        uint32_t             _specular_texture_image_width;
        uint32_t             _specular_texture_image_height;
        RHIFormat   _specular_texture_image_format;
    };

    struct ColorGradingResource
    {
        RHIImage* _color_grading_LUT_texture_image;
        RHIImageView* _color_grading_LUT_texture_image_view;
        RHIAllocation* _color_grading_LUT_texture_image_allocation;
    };

    struct ColorGradingResourceData
    {
        void* _color_grading_LUT_texture_image_pixels;
        uint32_t           _color_grading_LUT_texture_image_width;
        uint32_t           _color_grading_LUT_texture_image_height;
        RHIFormat _color_grading_LUT_texture_image_format;
    };

    struct StorageBuffer
    {
        // limits
        uint32_t _min_uniform_buffer_offset_alignment{ 256 };
        uint32_t _min_storage_buffer_offset_alignment{ 256 };
        uint32_t _max_storage_buffer_range{ 1 << 27 };
        uint32_t _non_coherent_atom_size{ 256 };

        RHIBuffer* _global_upload_ringbuffer;
        RHIDeviceMemory* _global_upload_ringbuffer_memory;
        void* _global_upload_ringbuffer_memory_pointer;
        std::vector<uint32_t> _global_upload_ringbuffers_begin;
        std::vector<uint32_t> _global_upload_ringbuffers_end;
        std::vector<uint32_t> _global_upload_ringbuffers_size;

        RHIBuffer* _global_null_descriptor_storage_buffer;
        RHIDeviceMemory* _global_null_descriptor_storage_buffer_memory;

        // axis
        RHIBuffer* _axis_inefficient_storage_buffer;
        RHIDeviceMemory* _axis_inefficient_storage_buffer_memory;
        void* _axis_inefficient_storage_buffer_memory_pointer;
    };

    struct GlobalRenderResource
    {
        IBLResource          _ibl_resource;
        ColorGradingResource _color_grading_resource;
        StorageBuffer        _storage_buffer;
    };

    class RenderResource : public RenderResourceBase
    {
    public:
        void clear() override final;

        virtual void uploadGlobalRenderResource(std::shared_ptr<RHI> rhi,
            LevelResourceDesc    level_resource_desc) override final;

        virtual void uploadGameObjectRenderResource(std::shared_ptr<RHI> rhi,
            RenderEntity         render_entity,
            RenderMeshData       mesh_data,
            RenderMaterialData   material_data) override final;

        virtual void uploadGameObjectRenderResource(std::shared_ptr<RHI> rhi,
            RenderEntity         render_entity,
            RenderMeshData       mesh_data) override final;

        virtual void uploadGameObjectRenderResource(std::shared_ptr<RHI> rhi,
            RenderEntity         render_entity,
            RenderMaterialData   material_data) override final;

        virtual void updatePerFrameBuffer(std::shared_ptr<RHI>          rhi,
            std::shared_ptr<RenderScene>  render_scene,
            std::shared_ptr<RenderCamera> camera) override final;

        RenderMeshGPUResource& getEntityMesh(RenderEntity entity);

        RenderPBRMaterialGPUResource& getEntityMaterial(RenderEntity entity);

        void ensurePathTracingBLAS(std::shared_ptr<RHI> rhi,
                                   RHICommandBuffer*    command_buffer,
                                   RenderMeshGPUResource& mesh);

        RHIAccelerationStructure* buildPathTracingBLASFromSkinned(
            std::shared_ptr<RHI> rhi,
            RHICommandBuffer* command_buffer,
            RHIBuffer* skinned_position_buffer,
            uint32_t vertex_count,
            uint32_t vertex_stride,
            RHIBuffer* index_buffer,
            uint32_t index_count,
            RHIIndexType index_type);

        std::vector<RenderPathTracingCollectedInstance> collectPathTracingInstances(RenderScene& scene);

        bool updatePathTracingSceneBuffers(std::shared_ptr<RHI> rhi,
                                           const std::vector<RenderPathTracingCollectedInstance>& collected_instances);

        RHIBuffer* getPathTracingVertexBuffer() const { return m_path_tracing_vertex_buffer; }
        RHIBuffer* getPathTracingIndexBuffer() const { return m_path_tracing_index_buffer; }
        RHIBuffer* getPathTracingMaterialBuffer() const { return m_path_tracing_material_buffer; }
        RHIBuffer* getPathTracingGeometryBuffer() const { return m_path_tracing_geometry_buffer; }
        RHIBuffer* getPathTracingInstanceBuffer() const { return m_path_tracing_instance_buffer; }
        RHIBuffer* getSkinnedVertexBuffer() const { return m_skinned_vertex_buffer; }
        void setSkinnedVertexBuffer(RHIBuffer* buf) { m_skinned_vertex_buffer = buf; }

        const std::vector<RenderPathTracingMaterialTextureViews>& getPathTracingMaterialTextureViews() const
        {
            return m_path_tracing_material_texture_views;
        }

        std::shared_ptr<RenderScene> getCurrentRenderScene() const;

        void resetRingBufferOffset(uint8_t current_frame_index);

        // global rendering resource, include IBL data, global storage buffer
        GlobalRenderResource m_global_render_resource;

        // storage buffer objects
        MeshPerframeStorageBufferObject                 m_mesh_perframe_storage_buffer_object;
        MeshPointLightShadowPerframeStorageBufferObject m_mesh_point_light_shadow_perframe_storage_buffer_object;
        MeshDirectionalLightShadowPerframeStorageBufferObject
                                                       m_mesh_directional_light_shadow_perframe_storage_buffer_object;
        AxisStorageBufferObject                        m_axis_storage_buffer_object;
        MeshInefficientPickPerframeStorageBufferObject m_mesh_inefficient_pick_perframe_storage_buffer_object;
        ParticleBillboardPerframeStorageBufferObject   m_particlebillboard_perframe_storage_buffer_object;
        ParticleCollisionPerframeStorageBufferObject   m_particle_collision_perframe_storage_buffer_object;

        // cached mesh and material
        std::map<size_t, RenderMeshGPUResource>        m_meshes;
        std::map<size_t, RenderPBRMaterialGPUResource> m_pbr_materials;

        // descriptor set layout in main camera pass will be used when uploading resource
        RHIDescriptorSetLayout* const* m_mesh_descriptor_set_layout {nullptr};
        RHIDescriptorSetLayout* const* m_material_descriptor_set_layout {nullptr};

    private:
        std::weak_ptr<RenderScene> m_current_render_scene;

        // Path tracing scene data
        std::vector<RenderPathTracingVertexGPUData> m_path_tracing_vertex_data;
        std::vector<uint32_t> m_path_tracing_index_data;
        std::vector<RenderPathTracingMaterialGPUData> m_path_tracing_material_data;
        std::vector<RenderPathTracingGeometryGPUData> m_path_tracing_geometry_data;
        std::vector<RenderPathTracingInstanceGPUData> m_path_tracing_instance_data;
        std::vector<RenderPathTracingMaterialTextureViews> m_path_tracing_material_texture_views;
        
        RHIBuffer* m_path_tracing_vertex_buffer {nullptr};
        RHIDeviceMemory* m_path_tracing_vertex_buffer_memory {nullptr};
        RHIBuffer* m_path_tracing_index_buffer {nullptr};
        RHIDeviceMemory* m_path_tracing_index_buffer_memory {nullptr};
        RHIBuffer* m_path_tracing_material_buffer {nullptr};
        RHIDeviceMemory* m_path_tracing_material_buffer_memory {nullptr};
        RHIBuffer* m_path_tracing_geometry_buffer {nullptr};
        RHIDeviceMemory* m_path_tracing_geometry_buffer_memory {nullptr};
        RHIBuffer* m_path_tracing_instance_buffer {nullptr};
        RHIDeviceMemory* m_path_tracing_instance_buffer_memory {nullptr};
        RHIBuffer* m_skinned_vertex_buffer {nullptr};
        
        size_t m_path_tracing_vertex_buffer_capacity {0};
        size_t m_path_tracing_index_buffer_capacity {0};
        size_t m_path_tracing_material_buffer_capacity {0};
        size_t m_path_tracing_geometry_buffer_capacity {0};
        size_t m_path_tracing_instance_buffer_capacity {0};

        void createAndMapStorageBuffer(std::shared_ptr<RHI> rhi);
        void createIBLSamplers(std::shared_ptr<RHI> rhi);
        void createIBLTextures(std::shared_ptr<RHI>                        rhi,
                               std::array<std::shared_ptr<TextureData>, 6> irradiance_maps,
                               std::array<std::shared_ptr<TextureData>, 6> specular_maps);

        RenderMeshGPUResource& getOrCreateMesh(std::shared_ptr<RHI> rhi, RenderEntity entity, RenderMeshData mesh_data);
        RenderPBRMaterialGPUResource&
        getOrCreateMaterial(std::shared_ptr<RHI> rhi, RenderEntity entity, RenderMaterialData material_data);

        void updateMeshData(std::shared_ptr<RHI>                          rhi,
                            bool                                          enable_vertex_blending,
                            uint32_t                                      index_buffer_size,
                            void*                                         index_buffer_data,
                            uint32_t                                      vertex_buffer_size,
                            struct MeshVertexDataDefinition const*        vertex_buffer_data,
                            uint32_t                                      joint_binding_buffer_size,
                            struct MeshVertexBindingDataDefinition const* joint_binding_buffer_data,
                            RenderMeshGPUResource&                                   now_mesh);
        void updateVertexBuffer(std::shared_ptr<RHI>                          rhi,
                                bool                                          enable_vertex_blending,
                                uint32_t                                      vertex_buffer_size,
                                struct MeshVertexDataDefinition const*        vertex_buffer_data,
                                uint32_t                                      joint_binding_buffer_size,
                                struct MeshVertexBindingDataDefinition const* joint_binding_buffer_data,
                                uint32_t                                      index_buffer_size,
                                uint16_t*                                     index_buffer_data,
                                RenderMeshGPUResource&                                   now_mesh);
        void updateIndexBuffer(std::shared_ptr<RHI> rhi,
                               uint32_t             index_buffer_size,
                               void*                index_buffer_data,
                               RenderMeshGPUResource&          now_mesh);
        void updateTextureImageData(std::shared_ptr<RHI> rhi, const TextureDataToUpdate& texture_data);
    };
} // namespace Piccolo
