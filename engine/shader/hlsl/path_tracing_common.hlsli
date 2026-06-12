#ifndef PICCOLO_PATH_TRACING_COMMON_HLSLI
#define PICCOLO_PATH_TRACING_COMMON_HLSLI

#define PICCOLO_PATH_TRACING_INVALID_INDEX 0xffffffffu
#define PICCOLO_PATH_TRACING_MATERIAL_FLAG_DOUBLE_SIDED 1u

struct PathTracingFrameData
{
    row_major float4x4 proj_view_matrix_inv;
    float3 camera_position;
    uint sample_index;
    uint2 extent;
    uint instance_count;
    uint _padding;
};

struct PathTracingVertexData
{
    float4 position;
    float4 normal;
    float4 tangent;
    float4 texcoord;
};

struct PathTracingMaterialData
{
    float4 base_color_factor;
    float4 emissive_factor;
    float4 metallic_roughness_normal_occlusion;
    uint base_color_texture_index;
    uint metallic_roughness_texture_index;
    uint normal_texture_index;
    uint emissive_texture_index;
    uint flags;
    uint3 _padding;
};

struct PathTracingGeometryData
{
    uint vertex_offset;
    uint index_offset;
    uint index_count;
    uint _padding;
};

struct PathTracingInstanceData
{
    uint geometry_index;
    uint material_index;
    uint entity_instance_id;
    uint flags;
};

#endif
