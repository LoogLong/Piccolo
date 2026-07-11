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

// Core integrator (PathState, payloads, surface load, EvalBSDF). Included
// after the buffer declarations above so its helper functions can reference
// them (e.g. g_frame_data, g_instances, g_materials, g_texture_array).
#include "path_tracing_core.hlsli"

// Unified lights + next-event estimation (declares g_lights at t1035).
// Note: light.hlsli itself includes sampling.hlsli, so BRDFPdf /
// MISWeightPower are available here without a second include.
#include "path_tracing_light.hlsli"

// One bounce of the path. Returns true to continue with another bounce, false
// to terminate. Task 3 adds indirect (BSDF sampling + MIS) and Russian roulette
// (plan Task 3 Step 5/6) on top of the Task 2 NEE kernel.
bool PathTracingStep(inout PathState path)
{
    PathTracingHitPayload payload;
    payload.world_normal   = float3(0.0f, 0.0f, 0.0f);
    payload.hit_t          = 0.0f;
    payload.texcoord       = float2(0.0f, 0.0f);
    payload.instance_index = 0u;
    payload.flags          = 0u;

    RayDesc ray;
    ray.Origin    = path.origin;
    ray.Direction = path.direction;
    ray.TMin      = 0.001f;
    ray.TMax      = 100000.0f;

    TraceRay(g_scene_tlas, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);

    if ((payload.flags & PT_HIT_FLAG) == 0u)
    {
        // Miss: contribute the sky background and stop.
        path.radiance += path.throughput * SampleSkyRadiance(path.direction);
        return false;
    }

    const PathTracingSurface surface = LoadHitSurface(payload, path.origin, path.direction);
    const float3 wo = -path.direction;

    // Emissive + direct lighting (NEE w/ shadow rays + precomputed env
    // ambient at first hit). The precomputed ambient is a low-frequency proxy
    // for infinite diffuse sky bounces; the sun NEE handles specific delta
    // contributions without double-counting. Phase 1.3 adds a specular IBL
    // term along the reflection direction so glossy materials see
    // surrounding sky as well -- diffuse env covers the diffuse term, sky
    // NEE could in principle cover specular via MIS, but per-pixel CDF
    // importance sampling of the sky cubemap is queued in Phase 1.2 (deferred).
    path.radiance += path.throughput * (surface.emissive
        + EstimateDirectLight(surface, wo, path.rng)
        + EstimateEnvironmentAmbient(surface, wo)
        + EstimateEnvironmentSpecular(surface, wo));

    // Stop here if we are already at the last permitted bounce (direct-only cap).
    if ((path.bounce + 1u) >= g_frame_data.max_bounces)
    {
        return false;
    }

    // Indirect: sample BSDF (cosine lobe for diffuse / GGX NDF for specular),
    // accumulate throughput, and continue the path.
    float3 wi;
    float  pdf_bsdf;
    uint   lobe;
    SampleBRDF(surface, wo, path.rng, wi, pdf_bsdf, lobe);

    if (pdf_bsdf <= 0.0f)
    {
        return false;
    }
    const float NdotL = dot(surface.normal, wi);
    if (NdotL <= 0.0f)
    {
        return false;
    }
    const float3 f = EvalBSDF(surface, wo, wi);
    if (dot(f, float3(1.0f, 1.0f, 1.0f)) <= 0.0f)
    {
        return false;
    }

    // Firefly clamp (plan Task 3 Step 6): cap the throughput before RR so
    // that a bright spike is attenuated to MaxPathIntensity-weighted. The
    // cap is config-driven via PathTracingMaxPathIntensity (default 100,
    // clamped to >= 1); slightly biased but practical.
    const float max_pi = max(1.0f, (float)g_frame_data.max_path_intensity);
    path.throughput = min(path.throughput, float3(max_pi, max_pi, max_pi));

    // Russian roulette (plan Task 3 Step 5): only after the minimum bounces so
    // early contributions are not biased toward termination. RR is unbiased by
    // construction -- we always divide by the survival probability.
    if (path.bounce >= PT_MIN_BOUNCES_BEFORE_RR)
    {
        const float max_th = max(path.throughput.r, max(path.throughput.g, path.throughput.b));
        const float p      = sqrt(saturate(max_th * 0.0625f)); // ~1 at th=16, mild tail
        if (Rand01(path.rng) >= p)
        {
            return false;
        }
        path.throughput *= 1.0f / max(p, 1e-6f);
    }

    path.throughput *= f * NdotL / pdf_bsdf;
    path.origin       = surface.position + surface.normal * 0.001f;
    path.direction    = normalize(wi);
    path.bounce      += 1u;
    return true;
}

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
        // Iterative path loop. Bound is the configurable max_bounces uniform
        // in g_frame_data (plan Task 3 Step 4). Default falls back to 4 if
        // the CPU has not uploaded a sane value.
        const uint kMaxBounces = max(g_frame_data.max_bounces, 1u);
        for (uint bounce = 0; bounce < kMaxBounces; ++bounce)
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
    // Material fetch for the normal map happens here (Phase 4.1) because
    // raygen can't run ddx/ddy, so the tangent-space transform must be
    // computed in this stage. Diffuse / spec / emissive textures still
    // load in the raygen via LoadHitSurface.
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
    // Vertex tangent (object-space; .w holds glTF handedness: +1/-1).
    // We blend it exactly as glTF vertex tangents are defined.
    const float4 tangent_os = normalize(v0.tangent * barycentric.x +
                                        v1.tangent * barycentric.y +
                                        v2.tangent * barycentric.z);
    const float2 texcoord = v0.texcoord.xy * barycentric.x +
                            v1.texcoord.xy * barycentric.y +
                            v2.texcoord.xy * barycentric.z;

    const float3x4 object_to_world = ObjectToWorld3x4();
    const float3x3 O2W = (float3x3)object_to_world;
    float3 N = normalize(mul(O2W, normal_os));

    // Phase 4.1: normal-map perturb (TBN-based).
    // Material lookup: instance_data.material_index gives us the material struct.
    const PathTracingMaterialData material_data = g_materials[instance_data.material_index];
    const float normal_scale = material_data.metallic_roughness_normal_occlusion.z;

    // Sample the normal texture if bound. UBYTE channel decoded to [-1, 1].
    float3 nmap = float3(0.0f, 0.0f, 1.0f);
    if (material_data.normal_texture_index < PICCOLO_PATH_TRACING_MAX_MATERIAL_TEXTURES)
    {
        const float3 texel = g_texture_array[material_data.normal_texture_index].SampleLevel(
            g_linear_sampler, texcoord, 0.0f).rgb;
        nmap = texel * 2.0f - 1.0f;
    }
    // Tangent-space normal scaled into tangent components only (height is Z).
    nmap = normalize(float3(nmap.xy * normal_scale, nmap.z));

    // Object-space -> world-space tangent and bitangent.
    // tangent_os.w encodes glTF handedness: w > 0 means handedness matches
    // (cross(N, T, w) == B), w < 0 flips the bitangent.
    const float3 T_world = normalize(mul(O2W, tangent_os.xyz));
    const float3 B_world = cross(N, T_world) * tangent_os.w;

    // World-space normal from tangent-space normal map + tangent frame.
    float3 N_perturbed = nmap.x * T_world
                       + nmap.y * B_world
                       + nmap.z * N;
    N_perturbed = normalize(N_perturbed);

    // Face-forward toward the incoming ray (use the perturbed normal).
    float3 V = -WorldRayDirection();
    if (dot(N_perturbed, V) < 0.0f)
    {
        N_perturbed = -N_perturbed;
    }

    payload.world_normal   = N_perturbed;
    payload.hit_t          = RayTCurrent();
    payload.texcoord       = texcoord;
    payload.instance_index = instance_index;
    payload.flags          = PT_HIT_FLAG;
}

[shader("miss")]
void PathTracingShadowMiss(inout PathShadowPayload payload)
{
    // Shadow visibility ray reached the light (no occluder). The payload was
    // initialized to occluded = 1 in TraceShadowRay; clear it here. On a hit
    // the closest-hit shader is skipped (SKIP_CLOSEST_HIT_SHADER) so this miss
    // never runs and occluded stays 1. SBT miss index 1.
    payload.occluded = 0u;
}
