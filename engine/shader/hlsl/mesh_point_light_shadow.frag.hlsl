#include "common.hlsli"

cbuffer PointShadowPerFrame : register(b0, space0)
{
    uint g_point_light_count;
    uint3 g_point_light_count_padding;
    float4 g_point_lights_position_and_radius[M_MAX_POINT_LIGHT_COUNT];
};

struct PointShadowPSInput
{
    float4 position : SV_Position;
    float inv_length : TEXCOORD0;
    float3 inv_length_position_view_space : TEXCOORD1;
    nointerpolation uint light_index : TEXCOORD2;
};

struct PointShadowPSOutput
{
    float depth_target : SV_Target0;
    float depth : SV_Depth;
};

PointShadowPSOutput main(PointShadowPSInput input)
{
    float3 position_view_space = input.inv_length_position_view_space / input.inv_length;
    float point_light_radius = g_point_lights_position_and_radius[input.light_index].w;
    float ratio = length(position_view_space) / point_light_radius;

    PointShadowPSOutput output;
    output.depth_target = ratio;
    output.depth = ratio;
    return output;
}
