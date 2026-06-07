#include "common.hlsli"

TextureCube<float4> g_specular_texture : register(t1, space0);
SamplerState        g_specular_sampler : register(s1, space0);

struct SkyboxVSOutput
{
    float4 position : SV_Position;
    float3 uvw      : TEXCOORD0;
};

float4 main(SkyboxVSOutput input) : SV_Target0
{
    float3 sample_uvw = float3(input.uvw.x, input.uvw.z, input.uvw.y);
    return float4(g_specular_texture.SampleLevel(g_specular_sampler, sample_uvw, 0.0f).rgb, 1.0f);
}
