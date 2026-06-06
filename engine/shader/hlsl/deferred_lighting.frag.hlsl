#include "common.hlsli"

ConstantBuffer<PerFrameData> g_per_frame : register(b0, space0);

Texture2D<float4> g_gbuffer_a : register(t0, space1);
Texture2D<float4> g_gbuffer_b : register(t1, space1);
Texture2D<float4> g_gbuffer_c : register(t2, space1);
Texture2D<float>  g_scene_depth : register(t3, space1);
TextureCube<float4> g_skybox_texture : register(t1, space2);
SamplerState g_skybox_sampler : register(s1, space2);

float4 main(FullscreenVSOutput input) : SV_Target0
{
    int2 pixel_coord = int2(input.position.xy);
    float4 gbuffer_a = g_gbuffer_a.Load(int3(pixel_coord, 0));
    float4 gbuffer_b = g_gbuffer_b.Load(int3(pixel_coord, 0));
    float4 gbuffer_c = g_gbuffer_c.Load(int3(pixel_coord, 0));

    GBufferData gbuffer = DecodeGBufferData(gbuffer_a, gbuffer_b, gbuffer_c);

    if (gbuffer.shading_model_id == SHADINGMODELID_UNLIT)
    {
        float3 sky_direction = normalize(float3(input.uv * 2.0f - 1.0f, 1.0f));
        float3 sky_sample_uvw = float3(sky_direction.x, sky_direction.z, sky_direction.y);
        return float4(g_skybox_texture.Sample(g_skybox_sampler, sky_sample_uvw).rgb, 1.0f);
    }

    float3 light_direction = normalize(-g_per_frame.scene_directional_light.direction);
    float3 light_color = max(g_per_frame.scene_directional_light.color, float3(0.2f, 0.2f, 0.2f));
    float lighting = saturate(dot(gbuffer.world_normal, light_direction));
    float3 ambient = max(g_per_frame.ambient_light, float3(0.05f, 0.05f, 0.05f));

    return float4(gbuffer.base_color * (ambient + lighting * light_color), 1.0f);
}
