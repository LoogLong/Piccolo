#include "common.hlsli"

Texture2D<float4> g_input_color : register(t0, space0);
Texture2D<float4> g_color_grading_lut : register(t1, space0);
SamplerState      g_color_grading_lut_sampler : register(s1, space0);

float4 main(FullscreenVSOutput input) : SV_Target0
{
    float4 color = g_input_color.Load(int3(int2(input.position.xy), 0));
    return color;
}
