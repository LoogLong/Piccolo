#include "common.hlsli"

struct ParticleBillboardPSInput
{
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

Texture2D<float4> g_spark_texture : register(t2, space0);
SamplerState g_spark_sampler : register(s2, space0);

float4 main(ParticleBillboardPSInput input) : SV_Target0
{
    float spark = g_spark_texture.Sample(g_spark_sampler, input.uv).r;
    return float4(4.0f * spark * input.color.rgb, spark);
}
