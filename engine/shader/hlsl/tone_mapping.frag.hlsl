#include "common.hlsli"

Texture2D<float4> g_input_color : register(t0, space0);

float4 main(FullscreenVSOutput input) : SV_Target0
{
    float3 color = g_input_color.Load(int3(int2(input.position.xy), 0)).rgb;
    color        = Uncharted2Tonemap(color * 4.5f);
    color        = color * rcp(Uncharted2Tonemap(float3(11.2f, 11.2f, 11.2f)));
    return float4(ApplyGamma(color), 1.0f);
}
