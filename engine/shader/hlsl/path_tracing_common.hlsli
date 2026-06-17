#ifndef PICCOLO_PATH_TRACING_COMMON_HLSLI
#define PICCOLO_PATH_TRACING_COMMON_HLSLI

#define PICCOLO_PATH_TRACING_INVALID_INDEX 0xffffffffu
#define PICCOLO_PATH_TRACING_MATERIAL_FLAG_DOUBLE_SIDED 1u
#define PICCOLO_PATH_TRACING_MAX_MATERIAL_TEXTURES 1024

struct PathTracingFrameData
{
    row_major float4x4 proj_view_matrix_inv;
    float3 camera_position;
    uint sample_index;
    uint2 extent;
    uint instance_count;
    uint reset_accumulation;
    float4 ambient_light;
    PointLight scene_point_lights[M_MAX_POINT_LIGHT_COUNT];
    DirectionalLight scene_directional_light;
    row_major float4x4 directional_light_proj_view;
    uint point_light_count;
    uint3 _padding_light;
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
