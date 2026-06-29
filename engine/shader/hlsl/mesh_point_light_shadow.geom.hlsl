#include "common.hlsli"

#define M_MAX_POINT_LIGHT_GEOM_VERTICES (M_MAX_POINT_LIGHT_COUNT * 2 * 3)

cbuffer PointShadowPerFrame : register(b0, space0)
{
    uint g_point_light_count;
    uint3 g_point_light_count_padding;
    float4 g_point_lights_position_and_radius[M_MAX_POINT_LIGHT_COUNT];
};

struct PointShadowVSOutput
{
    float3 world_position : TEXCOORD0;
};

struct PointShadowGSOutput
{
    float4 position : SV_Position;
    float inv_length : TEXCOORD0;
    float3 inv_length_position_view_space : TEXCOORD1;
    nointerpolation uint light_index : TEXCOORD2;
    uint layer : SV_RenderTargetArrayIndex;
};

[maxvertexcount(M_MAX_POINT_LIGHT_GEOM_VERTICES)]
void main(triangle PointShadowVSOutput input[3], inout TriangleStream<PointShadowGSOutput> output_stream)
{
    uint light_count = min(g_point_light_count, (uint)M_MAX_POINT_LIGHT_COUNT);

    [loop]
    for (uint point_light_index = 0; point_light_index < light_count; ++point_light_index)
    {
        float3 point_light_position = g_point_lights_position_and_radius[point_light_index].xyz;

        [unroll]
        for (uint layer_index = 0; layer_index < 2; ++layer_index)
        {
            [unroll]
            for (uint vertex_index = 0; vertex_index < 3; ++vertex_index)
            {
                float3 position_view_space = input[vertex_index].world_position - point_light_position;
                float3 spherical_domain = normalize(position_view_space);

                PointShadowGSOutput output;
                output.position.xy = spherical_domain.xy;
                output.position.w = (layer_index == 0 ? -spherical_domain.z : spherical_domain.z) + 1.0f;
                output.position.z = 0.5f * output.position.w;
                output.inv_length = rcp(max(length(position_view_space), 0.0001f));
                output.inv_length_position_view_space = output.inv_length * position_view_space;
                output.light_index = point_light_index;
                output.layer = layer_index + 2 * point_light_index;
                output_stream.Append(output);
            }
            output_stream.RestartStrip();
        }
    }
}
