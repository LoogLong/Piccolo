#include "common.hlsli"
#include "path_tracing_common.hlsli"

RaytracingAccelerationStructure g_scene_tlas : register(t0, space0);
RWTexture2D<float4> g_scene_output : register(u1, space0);
ConstantBuffer<PathTracingFrameData> g_frame_data : register(b2, space0);
RWTexture2D<float4> g_accumulation_output : register(u3, space0);
StructuredBuffer<PathTracingVertexData> g_vertices : register(t4, space0);
StructuredBuffer<uint> g_indices : register(t5, space0);
StructuredBuffer<PathTracingMaterialData> g_materials : register(t6, space0);
StructuredBuffer<PathTracingGeometryData> g_geometries : register(t7, space0);
StructuredBuffer<PathTracingInstanceData> g_instances : register(t8, space0);

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
    const uint safe_instance_count = max(g_frame_data.instance_count, 1u);
    const uint instance_index = min(InstanceIndex(), safe_instance_count - 1u);
    const PathTracingInstanceData instance_data = g_instances[instance_index];
    const PathTracingGeometryData geometry_data = g_geometries[instance_data.geometry_index];
    const PathTracingMaterialData material_data = g_materials[instance_data.material_index];

    const uint primitive_index = PrimitiveIndex();
    const uint index_base = geometry_data.index_offset + primitive_index * 3u;
    const uint i0 = g_indices[index_base + 0u] + geometry_data.vertex_offset;
    const uint i1 = g_indices[index_base + 1u] + geometry_data.vertex_offset;
    const uint i2 = g_indices[index_base + 2u] + geometry_data.vertex_offset;

    const PathTracingVertexData v0 = g_vertices[i0];
    const PathTracingVertexData v1 = g_vertices[i1];
    const PathTracingVertexData v2 = g_vertices[i2];

    const float3 barycentric = float3(1.0f - attributes.barycentrics.x - attributes.barycentrics.y,
                                     attributes.barycentrics.x,
                                     attributes.barycentrics.y);

    const float3 normal_os = normalize(v0.normal.xyz * barycentric.x +
                                       v1.normal.xyz * barycentric.y +
                                       v2.normal.xyz * barycentric.z);
    const float3 tangent_os = normalize(v0.tangent.xyz * barycentric.x +
                                        v1.tangent.xyz * barycentric.y +
                                        v2.tangent.xyz * barycentric.z);

    const float3x4 object_to_world = ObjectToWorld3x4();
    float3 normal_ws = normalize(mul((float3x3)object_to_world, normal_os));
    float3 tangent_ws = normalize(mul((float3x3)object_to_world, tangent_os));
    const float3 world_position = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    if (dot(normal_ws, -WorldRayDirection()) < 0.0f &&
        (material_data.flags & PICCOLO_PATH_TRACING_MATERIAL_FLAG_DOUBLE_SIDED) != 0u)
    {
        normal_ws = -normal_ws;
    }

    const float metallic = saturate(material_data.metallic_roughness_normal_occlusion.x);
    const float roughness = saturate(material_data.metallic_roughness_normal_occlusion.y);
    const float3 base_color = material_data.base_color_factor.rgb;
    const float3 emissive = material_data.emissive_factor.rgb;
    const float3 view_dir = normalize(g_frame_data.camera_position - world_position);
    const float3 light_dir = normalize(float3(0.4f, 0.8f, 0.2f));
    const float ndotl = saturate(dot(normal_ws, light_dir));
    const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), base_color, metallic);

    (void)tangent_ws;
    (void)view_dir;
    payload.radiance = base_color * (0.08f + 0.92f * ndotl) + emissive + f0 * (1.0f - roughness) * 0.02f;
    payload.hit = 1;
}
