#include "common.hlsli"

ConstantBuffer<PerFrameData> g_per_frame : register(b0, space0);

struct SkyboxVSOutput
{
    float4 position : SV_Position;
    float3 uvw      : TEXCOORD0;
};

SkyboxVSOutput main(uint vertex_id : SV_VertexID)
{
    const float3 cube_corner_vertex_offsets[8] =
    {
        float3(1.0f, 1.0f, 1.0f),
        float3(1.0f, 1.0f, -1.0f),
        float3(1.0f, -1.0f, -1.0f),
        float3(1.0f, -1.0f, 1.0f),
        float3(-1.0f, 1.0f, 1.0f),
        float3(-1.0f, 1.0f, -1.0f),
        float3(-1.0f, -1.0f, -1.0f),
        float3(-1.0f, -1.0f, 1.0f)
    };

    const uint cube_triangle_index[36] =
    {
        0, 1, 2, 2, 3, 0,
        4, 5, 1, 1, 0, 4,
        7, 6, 5, 5, 4, 7,
        3, 2, 6, 6, 7, 3,
        4, 0, 3, 3, 7, 4,
        1, 5, 6, 6, 2, 1
    };

    float3 world_position = g_per_frame.camera_position + cube_corner_vertex_offsets[cube_triangle_index[vertex_id]];
    float4 clip_position  = mul(g_per_frame.proj_view_matrix, float4(world_position, 1.0f));
    clip_position.z       = clip_position.w * 0.99999f;

    SkyboxVSOutput output;
    output.position = clip_position;
    output.uvw      = normalize(world_position - g_per_frame.camera_position);
    return output;
}
