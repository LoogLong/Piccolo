#include "common.hlsli"

ConstantBuffer<PerFrameData> g_per_frame : register(b0, space0);
ConstantBuffer<MaterialData> g_material : register(b0, space2);

Texture2D<float4> g_brdf_lut : register(t3, space0);
TextureCube<float4> g_irradiance_texture : register(t4, space0);
TextureCube<float4> g_specular_texture : register(t5, space0);
Texture2DArray<float> g_point_lights_shadow : register(t6, space0);
Texture2D<float> g_directional_light_shadow : register(t7, space0);
SamplerState g_brdf_lut_sampler : register(s3, space0);
SamplerState g_irradiance_sampler : register(s4, space0);
SamplerState g_specular_sampler : register(s5, space0);
SamplerState g_point_lights_shadow_sampler : register(s6, space0);
SamplerState g_directional_light_shadow_sampler : register(s7, space0);

Texture2D<float4> g_base_color_texture : register(t1, space2);
Texture2D<float4> g_metallic_roughness_texture : register(t2, space2);
Texture2D<float4> g_normal_texture : register(t3, space2);
SamplerState      g_base_color_sampler : register(s1, space2);
SamplerState      g_metallic_roughness_sampler : register(s2, space2);
SamplerState      g_normal_sampler : register(s3, space2);

float4 main(MeshVSOutput input) : SV_Target0
{
    float4 base_color = g_base_color_texture.Sample(g_base_color_sampler, input.texcoord) * g_material.baseColorFactor;
    float4 metallic_roughness = g_metallic_roughness_texture.Sample(g_metallic_roughness_sampler, input.texcoord);
    float3 normal_sample = g_normal_texture.Sample(g_normal_sampler, input.texcoord).xyz * 2.0f - 1.0f;
    float3 n = CalculateTangentNormal(input.normal, input.tangent, normal_sample);
    float metallic = metallic_roughness.b * g_material.metallicFactor;
    float dielectric_specular = 0.04f;
    float roughness = metallic_roughness.g * g_material.roughnessFactor;

    float3 v = normalize(g_per_frame.camera_position - input.world_position);
    float3 r = reflect(-v, n);

    float3 origin_samplecube_n = float3(n.x, n.z, n.y);
    float3 origin_samplecube_r = float3(r.x, r.z, r.y);

    float3 f0 = lerp(float3(dielectric_specular, dielectric_specular, dielectric_specular), base_color.rgb, metallic);
    float3 lo = float3(0.0f, 0.0f, 0.0f);

    [loop]
    for (int light_index = 0; light_index < int(g_per_frame.point_light_num) && light_index < M_MAX_POINT_LIGHT_COUNT;
         ++light_index)
    {
        float3 point_light_position = g_per_frame.scene_point_lights[light_index].position;
        float point_light_radius = g_per_frame.scene_point_lights[light_index].radius;

        float3 l = normalize(point_light_position - input.world_position);
        float nol = min(dot(n, l), 1.0f);

        float distance = length(point_light_position - input.world_position);
        float distance_attenuation = 1.0f / (distance * distance + 1.0f);
        float radius_attenuation = 1.0f - ((distance * distance) / (point_light_radius * point_light_radius));
        float light_attenuation = radius_attenuation * distance_attenuation * nol;

        if (light_attenuation > 0.0f)
        {
            float3 position_view_space = input.world_position - point_light_position;
            float3 position_spherical_function_domain = normalize(position_view_space);
            float2 position_ndcxy =
                position_spherical_function_domain.xy / (abs(position_spherical_function_domain.z) + 1.0f);
            float2 uv = NdcXYToUV(position_ndcxy);
            float layer_index = (0.5f + 0.5f * sign(position_spherical_function_domain.z)) +
                                2.0f * float(light_index);

            float depth = g_point_lights_shadow.Sample(g_point_lights_shadow_sampler, float3(uv, layer_index)) +
                          0.000075f;
            float closest_length = depth * point_light_radius;
            float current_length = length(position_view_space);

            if (closest_length >= current_length)
            {
                float3 en = g_per_frame.scene_point_lights[light_index].intensity * light_attenuation;
                lo += BRDF(l, v, n, f0, base_color.rgb, metallic, roughness) * en;
            }
        }
    }

    float3 la = base_color.rgb * g_per_frame.ambient_light;

    float3 irradiance = g_irradiance_texture.Sample(g_irradiance_sampler, origin_samplecube_n).rgb;
    float3 diffuse = irradiance * base_color.rgb;

    float3 f = F_SchlickR(clamp(dot(n, v), 0.0f, 1.0f), f0, roughness);
    float2 brdf_lut = g_brdf_lut.Sample(g_brdf_lut_sampler, float2(clamp(dot(n, v), 0.0f, 1.0f), roughness)).rg;

    float lod = roughness * MAX_REFLECTION_LOD;
    float3 reflection = g_specular_texture.SampleLevel(g_specular_sampler, origin_samplecube_r, lod).rgb;
    float3 specular = reflection * (f * brdf_lut.x + brdf_lut.y);

    float3 kd = 1.0f - f;
    kd *= 1.0f - metallic;
    float3 libl = kd * diffuse + specular;

    {
        float3 l = normalize(g_per_frame.scene_directional_light.direction);
        float nol = min(dot(n, l), 1.0f);

        if (nol > 0.0f)
        {
            float4 position_clip = mul(g_per_frame.directional_light_proj_view, float4(input.world_position, 1.0f));
            float3 position_ndc = position_clip.xyz / position_clip.w;
            float2 uv = NdcXYToUV(position_ndc.xy);

            float closest_depth = g_directional_light_shadow.Sample(g_directional_light_shadow_sampler, uv) + 0.000075f;
            float current_depth = position_ndc.z;

            if (closest_depth >= current_depth)
            {
                float3 en = g_per_frame.scene_directional_light.color * nol;
                lo += BRDF(l, v, n, f0, base_color.rgb, metallic, roughness) * en;
            }
        }
    }

    return float4(lo + la + libl, 1.0f);
}
