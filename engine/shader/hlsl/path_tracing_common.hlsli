#ifndef PICCOLO_PATH_TRACING_COMMON_HLSLI
#define PICCOLO_PATH_TRACING_COMMON_HLSLI

#define PICCOLO_PATH_TRACING_INVALID_INDEX 0xffffffffu
#define PICCOLO_PATH_TRACING_MATERIAL_FLAG_DOUBLE_SIDED 1u
#define PICCOLO_PATH_TRACING_MAX_MATERIAL_TEXTURES 1024

struct PathTracingFrameData
{
    row_major float4x4 proj_view_matrix_inv;
    float3 camera_position;
    uint   sample_index;
    uint2  extent;
    uint   instance_count;
    uint   reset_accumulation;
    // Lights live in a separate StructuredBuffer<PathTracingLight> (g_lights);
    // these counts index it. ambient_light / scattered point+directional fields
    // and the dead directional_light_proj_view were removed (plan Task 2 Step 4).
    uint   light_count;          // total lights in g_lights
    uint   infinite_light_count; // directional + sky (the first N entries)
    // Max path bounces (replaces PT_PLACEHOLDER_MAX_BOUNCES, plan Task 3 Step 4).
    // Default when unset: 4. Configurable via PathTracingMaxBounces.
    uint   max_bounces;
    // Replaces the hardcoded firefly cap previously in path_tracing.lib.hlsl:101.
    // Configurable via PathTracingMaxPathIntensity (default 100).
    uint   max_path_intensity;
    // Real mip count of g_specular_texture (the IBL specular cubemap). Drives
    // PT_SpecularIBLLod's roughness -> mip formula so it lands on the right
    // LOD for the actual asset instead of a hardcoded 8. Plan 2026-07-15
    // Phase 5 A4.
    uint   cubemap_mip_count;
    uint  _padding_core;
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
