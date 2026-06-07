#include "common.hlsli"

Texture2D<float4> g_input_color : register(t0, space0);
SamplerState      g_input_sampler : register(s0, space0);

float Luma(float3 color)
{
    return dot(color, float3(0.299f, 0.578f, 0.114f));
}

float4 main(FullscreenVSOutput input) : SV_Target0
{
    uint width = 0;
    uint height = 0;
    g_input_color.GetDimensions(width, height);

    float2 texel = rcp(max(float2(float(width), float(height)), float2(1.0f, 1.0f)));

    float3 center = g_input_color.Sample(g_input_sampler, input.uv).rgb;
    float3 left   = g_input_color.Sample(g_input_sampler, input.uv + float2(-texel.x, 0.0f)).rgb;
    float3 right  = g_input_color.Sample(g_input_sampler, input.uv + float2(texel.x, 0.0f)).rgb;
    float3 up     = g_input_color.Sample(g_input_sampler, input.uv + float2(0.0f, texel.y)).rgb;
    float3 down   = g_input_color.Sample(g_input_sampler, input.uv + float2(0.0f, -texel.y)).rgb;

    float luma_min = min(Luma(center), min(min(Luma(left), Luma(right)), min(Luma(up), Luma(down))));
    float luma_max = max(Luma(center), max(max(Luma(left), Luma(right)), max(Luma(up), Luma(down))));

    if ((luma_max - luma_min) < max(0.0312f, luma_max * 0.125f))
    {
        return float4(center, 1.0f);
    }

    return float4((center + left + right + up + down) * 0.2f, 1.0f);
}
