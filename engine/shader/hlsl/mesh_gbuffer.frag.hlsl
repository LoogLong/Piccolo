#include "common.hlsli"

ConstantBuffer<MaterialData> g_material : register(b0, space2);

Texture2D<float4> g_base_color_texture : register(t1, space2);
Texture2D<float4> g_metallic_roughness_texture : register(t2, space2);
Texture2D<float4> g_normal_texture : register(t3, space2);
Texture2D<float4> g_occlusion_texture : register(t4, space2);
Texture2D<float4> g_emissive_texture : register(t5, space2);
SamplerState      g_base_color_sampler : register(s1, space2);
SamplerState      g_metallic_roughness_sampler : register(s2, space2);
SamplerState      g_normal_sampler : register(s3, space2);
SamplerState      g_occlusion_sampler : register(s4, space2);
SamplerState      g_emissive_sampler : register(s5, space2);

struct GBufferOutput
{
    float4 gbuffer_a : SV_Target0;
    float4 gbuffer_b : SV_Target1;
    float4 gbuffer_c : SV_Target2;
};

GBufferOutput main(MeshVSOutput input)
{
    float4 base_color_sample = g_base_color_texture.Sample(g_base_color_sampler, input.texcoord);
    float4 mr_sample         = g_metallic_roughness_texture.Sample(g_metallic_roughness_sampler, input.texcoord);
    float3 tangent_normal    = g_normal_texture.Sample(g_normal_sampler, input.texcoord).xyz * 2.0f - 1.0f;

    GBufferData gbuffer;
    gbuffer.world_normal     = CalculateTangentNormal(input.normal, input.tangent, tangent_normal);
    gbuffer.base_color       = base_color_sample.rgb * g_material.baseColorFactor.rgb;
    gbuffer.metallic         = mr_sample.b * g_material.metallicFactor;
    gbuffer.specular         = 0.5f;
    gbuffer.roughness        = mr_sample.g * g_material.roughnessFactor;
    gbuffer.shading_model_id = SHADINGMODELID_DEFAULT_LIT;

    GBufferOutput output;
    EncodeGBufferData(gbuffer, output.gbuffer_a, output.gbuffer_b, output.gbuffer_c);
    return output;
}
