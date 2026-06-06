#include "common.hlsli"

ConstantBuffer<PerFrameData> g_per_frame : register(b0, space0);
ConstantBuffer<MaterialData> g_material : register(b0, space2);

Texture2D<float4> g_base_color_texture : register(t1, space2);
Texture2D<float4> g_metallic_roughness_texture : register(t2, space2);
Texture2D<float4> g_normal_texture : register(t3, space2);
SamplerState      g_base_color_sampler : register(s1, space2);
SamplerState      g_metallic_roughness_sampler : register(s2, space2);
SamplerState      g_normal_sampler : register(s3, space2);

float4 main(MeshVSOutput input) : SV_Target0
{
    float4 base_color = g_base_color_texture.Sample(g_base_color_sampler, input.texcoord) * g_material.baseColorFactor;
    float3 normal_sample = g_normal_texture.Sample(g_normal_sampler, input.texcoord).xyz * 2.0f - 1.0f;
    float3 normal = CalculateTangentNormal(input.normal, input.tangent, normal_sample);

    float3 light_direction = normalize(-g_per_frame.scene_directional_light.direction);
    float3 light_color = max(g_per_frame.scene_directional_light.color, float3(0.2f, 0.2f, 0.2f));
    float lighting = saturate(dot(normal, light_direction));
    float3 ambient = max(g_per_frame.ambient_light, float3(0.05f, 0.05f, 0.05f));

    return float4(base_color.rgb * (ambient + lighting * light_color), base_color.a);
}
