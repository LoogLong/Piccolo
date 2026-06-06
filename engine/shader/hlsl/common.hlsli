#ifndef PICCOLO_D3D12_COMMON_HLSLI
#define PICCOLO_D3D12_COMMON_HLSLI

#define M_MAX_POINT_LIGHT_COUNT 15
#define M_MESH_VERTEX_BLENDING_MAX_JOINT_COUNT 1024
#define M_MESH_PER_DRAWCALL_MAX_INSTANCE_COUNT 64

#define SHADINGMODELID_UNLIT 0u
#define SHADINGMODELID_DEFAULT_LIT 1u

struct DirectionalLight
{
    float3 direction;
    float  _padding_direction;
    float3 color;
    float  _padding_color;
};

struct PointLight
{
    float3 position;
    float  radius;
    float3 intensity;
    float  _padding_intensity;
};

struct PerFrameData
{
    row_major float4x4 proj_view_matrix;
    float3             camera_position;
    float              _padding_camera_position;
    float3             ambient_light;
    float              _padding_ambient_light;
    uint               point_light_num;
    uint3              _padding_point_light_num;
    PointLight         scene_point_lights[M_MAX_POINT_LIGHT_COUNT];
    DirectionalLight   scene_directional_light;
    row_major float4x4 directional_light_proj_view;
};

struct MeshInstanceData
{
    float              enable_vertex_blending;
    float3             _padding_enable_vertex_blending;
    row_major float4x4 model_matrix;
};

struct MeshPerDrawcallData
{
    MeshInstanceData mesh_instances[M_MESH_PER_DRAWCALL_MAX_INSTANCE_COUNT];
};

struct PickInstanceData
{
    row_major float4x4 model_matrix;
    uint               node_id;
    float              enable_vertex_blending;
    float2             padding;
};

struct PickPerDrawcallData
{
    PickInstanceData pick_instances[M_MESH_PER_DRAWCALL_MAX_INSTANCE_COUNT];
};

struct MeshVertexJointBindingData
{
    int4   indices;
    float4 weights;
};

struct JointMatrixData
{
    row_major float4x4 matrix;
};

struct MaterialData
{
    float4 baseColorFactor;
    float  metallicFactor;
    float  roughnessFactor;
    float  normalScale;
    float  occlusionStrength;
    float3 emissiveFactor;
    uint   is_blend;
    uint   is_double_sided;
    float3 _padding_material;
};

struct MeshVSOutput
{
    float4 position       : SV_Position;
    float3 world_position : TEXCOORD0;
    float3 normal         : TEXCOORD1;
    float3 tangent        : TEXCOORD2;
    float2 texcoord       : TEXCOORD3;
};

struct FullscreenVSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

struct GBufferData
{
    float3 world_normal;
    float3 base_color;
    float  metallic;
    float  specular;
    float  roughness;
    uint   shading_model_id;
};

float3 EncodeNormal(float3 normal)
{
    return normal * 0.5f + 0.5f;
}

float3 DecodeNormal(float3 encoded_normal)
{
    return encoded_normal * 2.0f - 1.0f;
}

float EncodeShadingModelId(uint shading_model_id)
{
    return float(shading_model_id) / 255.0f;
}

uint DecodeShadingModelId(float packed_channel)
{
    return uint(round(saturate(packed_channel) * 255.0f));
}

void EncodeGBufferData(GBufferData gbuffer, out float4 out_gbuffer_a, out float4 out_gbuffer_b, out float4 out_gbuffer_c)
{
    out_gbuffer_a = float4(EncodeNormal(normalize(gbuffer.world_normal)), 1.0f);
    out_gbuffer_b = float4(gbuffer.metallic, gbuffer.specular, gbuffer.roughness, EncodeShadingModelId(gbuffer.shading_model_id));
    out_gbuffer_c = float4(gbuffer.base_color, 1.0f);
}

GBufferData DecodeGBufferData(float4 gbuffer_a, float4 gbuffer_b, float4 gbuffer_c)
{
    GBufferData gbuffer;
    gbuffer.world_normal     = normalize(DecodeNormal(gbuffer_a.xyz));
    gbuffer.metallic         = gbuffer_b.r;
    gbuffer.specular         = gbuffer_b.g;
    gbuffer.roughness        = gbuffer_b.b;
    gbuffer.shading_model_id = DecodeShadingModelId(gbuffer_b.a);
    gbuffer.base_color       = gbuffer_c.rgb;
    return gbuffer;
}

float3 Uncharted2Tonemap(float3 x)
{
    const float A = 0.15f;
    const float B = 0.50f;
    const float C = 0.10f;
    const float D = 0.20f;
    const float E = 0.02f;
    const float F = 0.30f;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 ApplyGamma(float3 color)
{
    return pow(saturate(color), 1.0f / 2.2f);
}

float3 CalculateTangentNormal(float3 vertex_normal, float3 vertex_tangent, float3 tangent_normal)
{
    float3 n = normalize(vertex_normal);
    float3 t = normalize(vertex_tangent);
    float3 b = normalize(cross(n, t));
    return normalize(mul(float3x3(t, b, n), tangent_normal));
}

#endif
