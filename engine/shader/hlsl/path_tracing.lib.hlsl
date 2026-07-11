#include "common.hlsli"
#include "path_tracing_common.hlsli"
#include "path_tracing_rng.hlsli"

RaytracingAccelerationStructure g_scene_tlas : register(t0, space0);
RWTexture2D<float4> g_scene_output : register(u1, space0);
ConstantBuffer<PathTracingFrameData> g_frame_data : register(b2, space0);
RWTexture2D<float4> g_accumulation : register(u3, space0);
StructuredBuffer<PathTracingVertexData> g_vertices : register(t4, space0);
StructuredBuffer<uint> g_indices : register(t5, space0);
StructuredBuffer<PathTracingMaterialData> g_materials : register(t6, space0);
StructuredBuffer<PathTracingGeometryData> g_geometries : register(t7, space0);
StructuredBuffer<PathTracingInstanceData> g_instances : register(t8, space0);
// Per-instance skinned vertex data for animated meshes.
// Indexed as [geometry_data.vertex_offset + local_vertex_index].
StructuredBuffer<PathTracingVertexData> g_skinned_vertices : register(t1036, space0);
TextureCube<float4> g_irradiance_texture : register(t9, space0);
TextureCube<float4> g_specular_texture : register(t10, space0);
Texture2D<float4> g_texture_array[PICCOLO_PATH_TRACING_MAX_MATERIAL_TEXTURES] : register(t11, space0);
SamplerState g_linear_sampler : register(s12, space0);

// Core integrator (PathState, payloads, surface load, path step). Included
// after the buffer declarations above so its helper functions can reference
// them (e.g. g_frame_data, g_instances, g_materials, g_texture_array).
#include "path_tracing_core.hlsli"

[shader("raygeneration")]
void PathTracingRayGen()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 extent = DispatchRaysDimensions().xy;
    const float2 uv = (float2(pixel) + 0.5f) / float2(max(extent, uint2(1, 1)));

    // Pixel (0,0) is top-left for both backends. Map UV->NDC without a Y flip;
    // ClipSpaceConvention is already encoded in proj_view_matrix_inv (Y-up
    // backends adjust the inverse on the CPU -- see PathTracingPass::updateFrameData).
    // INVARIANT (plan Task 1 Step 5): do not re-derive the inverse or re-introduce
    // a UV Y-flip here; the CPU-side proj_view_inv * y_flip is the load-bearing wall.
    const float4 ndc = float4(UVToNdcXY(uv), 1.0f, 1.0f);
    const float4 world = mul(g_frame_data.proj_view_matrix_inv, ndc);
    const float3 world_position = world.xyz / max(world.w, 0.00001f);

    PathState path;
    path.radiance   = float3(0.0f, 0.0f, 0.0f);
    path.throughput = float3(1.0f, 1.0f, 1.0f);
    path.origin     = g_frame_data.camera_position;
    path.direction  = normalize(world_position - g_frame_data.camera_position);
    path.bounce     = 0u;
    path.rng        = InitRNG(pixel, extent, g_frame_data.sample_index);

    if (g_frame_data.instance_count > 0)
    {
        // Iterative path loop. CHS no longer recurses, so all TraceRay calls
        // originate from raygen (depth 1); the loop bound is a placeholder
        // constant until Task 3 promotes it to g_frame_data.max_bounces.
        for (uint bounce = 0; bounce < PT_PLACEHOLDER_MAX_BOUNCES; ++bounce)
        {
            path.bounce = bounce;
            if (!PathTracingStep(path))
            {
                break;
            }
        }
    }

    // Temporal accumulation blend.
    float3 prev_accum = float3(0.0f, 0.0f, 0.0f);
    float sample_count = 1.0f;

    if (g_frame_data.reset_accumulation == 0)
    {
        prev_accum = g_accumulation[pixel].rgb;
        sample_count = float(g_frame_data.sample_index) + 1.0f;
    }

    const float3 blended = (prev_accum * (sample_count - 1.0f) + path.radiance) / sample_count;

    g_scene_output[pixel] = float4(blended, 1.0f);
    g_accumulation[pixel] = float4(blended, 1.0f);
}

[shader("miss")]
void PathTracingMiss(inout PathTracingHitPayload payload)
{
    // Thin miss: just mark "no hit". The sky background radiance is added by
    // PathTracingStep via SampleSkyRadiance (using the path direction, which
    // equals WorldRayDirection() here).
    payload.flags       = 0u;
    payload.hit_t       = 0.0f;
    payload.world_normal = float3(0.0f, 0.0f, 0.0f);
}

[shader("closesthit")]
void PathTracingClosestHit(inout PathTracingHitPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    // Thin closest-hit: record the hit only. NO lighting, NO shadow rays, NO
    // recursive TraceRay (the old recursive IBL / ambient / no-shadow point
    // light / indirect-TraceRay paths are removed -- plan Task 1 Step 4).
    // Material fetch + shading happen in raygen (LoadHitSurface).
    //
    // ObjectToWorld3x4() / RayTCurrent() are CHS-only built-ins, so the
    // world-space normal is computed here and packed into the payload.
    const uint safe_instance_count = max(g_frame_data.instance_count, 1u);
    const uint instance_index = min(InstanceIndex(), safe_instance_count - 1u);
    const PathTracingInstanceData instance_data = g_instances[instance_index];
    const PathTracingGeometryData geometry_data = g_geometries[instance_data.geometry_index];

    const uint primitive_index = PrimitiveIndex();
    const uint index_base = geometry_data.index_offset + primitive_index * 3u;
    const uint local_i0 = g_indices[index_base + 0u];
    const uint local_i1 = g_indices[index_base + 1u];
    const uint local_i2 = g_indices[index_base + 2u];

    PathTracingVertexData v0, v1, v2;

    if (instance_data.flags & 1u) // enable_vertex_blending
    {
        // Skinned instance: read from per-instance g_skinned_vertices buffer.
        v0 = g_skinned_vertices[geometry_data.vertex_offset + local_i0];
        v1 = g_skinned_vertices[geometry_data.vertex_offset + local_i1];
        v2 = g_skinned_vertices[geometry_data.vertex_offset + local_i2];
    }
    else
    {
        // Static mesh: read from flat g_vertices buffer.
        v0 = g_vertices[local_i0 + geometry_data.vertex_offset];
        v1 = g_vertices[local_i1 + geometry_data.vertex_offset];
        v2 = g_vertices[local_i2 + geometry_data.vertex_offset];
    }

    const float3 barycentric = float3(1.0f - attributes.barycentrics.x - attributes.barycentrics.y,
                                     attributes.barycentrics.x,
                                     attributes.barycentrics.y);

    const float3 normal_os = normalize(v0.normal.xyz * barycentric.x +
                                       v1.normal.xyz * barycentric.y +
                                       v2.normal.xyz * barycentric.z);
    const float2 texcoord = v0.texcoord.xy * barycentric.x +
                            v1.texcoord.xy * barycentric.y +
                            v2.texcoord.xy * barycentric.z;

    const float3x4 object_to_world = ObjectToWorld3x4();
    float3 N = normalize(mul((float3x3)object_to_world, normal_os));

    // Face-forward the geometric normal toward the incoming ray.
    float3 V = -WorldRayDirection();
    if (dot(N, V) < 0.0f)
    {
        N = -N;
    }

    payload.world_normal   = N;
    payload.hit_t          = RayTCurrent();
    payload.texcoord       = texcoord;
    payload.instance_index = instance_index;
    payload.flags          = PT_HIT_FLAG;
}
