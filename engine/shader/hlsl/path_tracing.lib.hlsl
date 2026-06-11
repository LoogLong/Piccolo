#include "common.hlsli"
#include "path_tracing_common.hlsli"

RaytracingAccelerationStructure g_scene_tlas : register(t0, space0);
RWTexture2D<float4> g_scene_output : register(u1, space0);
ConstantBuffer<PathTracingFrameData> g_frame_data : register(b2, space0);
RWTexture2D<float4> g_accumulation_output : register(u3, space0);

struct PathTracingRayPayload
{
    float3 radiance;
    uint hit;
};

[shader("raygeneration")]
void PathTracingRayGen()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 extent = DispatchRaysDimensions().xy;
    const float2 uv = (float2(pixel) + 0.5f) / float2(max(extent, uint2(1, 1)));

    PathTracingRayPayload payload;
    payload.radiance = float3(0.02f + uv.x * 0.3f, 0.03f + uv.y * 0.3f, 0.08f);
    payload.hit = 0;

    if (g_frame_data.instance_count > 0)
    {
        const float4 ndc = float4(UVToNdcXY(uv), 1.0f, 1.0f);
        const float4 world = mul(g_frame_data.proj_view_matrix_inv, ndc);
        const float3 world_position = world.xyz / max(world.w, 0.00001f);

        RayDesc ray;
        ray.Origin = g_frame_data.camera_position;
        ray.Direction = normalize(world_position - g_frame_data.camera_position);
        ray.TMin = 0.001f;
        ray.TMax = 100000.0f;

        TraceRay(g_scene_tlas, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
    }
    const float4 current_sample = float4(payload.radiance, 1.0f);
    const float4 previous_accumulation =
        g_frame_data.sample_index == 0 ? float4(0.0f, 0.0f, 0.0f, 0.0f) : g_accumulation_output[pixel];
    const float sample_count = (float)g_frame_data.sample_index + 1.0f;
    const float4 accumulated = (previous_accumulation * (float)g_frame_data.sample_index + current_sample) / sample_count;
    g_accumulation_output[pixel] = accumulated;
    g_scene_output[pixel] = accumulated;
}

[shader("miss")]
void PathTracingMiss(inout PathTracingRayPayload payload)
{
    payload.radiance = float3(0.02f, 0.03f, 0.08f);
    payload.hit = 0;
}

[shader("closesthit")]
void PathTracingClosestHit(inout PathTracingRayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    const float3 barycentric = float3(1.0f - attributes.barycentrics.x - attributes.barycentrics.y,
                                     attributes.barycentrics.x,
                                     attributes.barycentrics.y);
    payload.radiance = 0.2f + 0.6f * barycentric;
    payload.hit = 1;
}
