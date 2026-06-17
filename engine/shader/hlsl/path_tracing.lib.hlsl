#include "common.hlsli"
#include "path_tracing_common.hlsli"
#include "path_tracing_rng.hlsli"

RaytracingAccelerationStructure g_scene_tlas : register(t0, space0);
RWTexture2D<float4> g_scene_output : register(u1, space0);
ConstantBuffer<PathTracingFrameData> g_frame_data : register(b2, space0);
RWTexture2D<float4> g_accumulation_output : register(u3, space0);
Texture2D<float4> g_accumulation_prev : register(t1035, space0); // previous frame accumulation (read)
StructuredBuffer<PathTracingVertexData> g_vertices : register(t4, space0);
StructuredBuffer<uint> g_indices : register(t5, space0);
StructuredBuffer<PathTracingMaterialData> g_materials : register(t6, space0);
StructuredBuffer<PathTracingGeometryData> g_geometries : register(t7, space0);
StructuredBuffer<PathTracingInstanceData> g_instances : register(t8, space0);
TextureCube<float4> g_irradiance_texture : register(t9, space0);
TextureCube<float4> g_specular_texture : register(t10, space0);
Texture2D<float4> g_texture_array[PICCOLO_PATH_TRACING_MAX_MATERIAL_TEXTURES] : register(t11, space0);
SamplerState g_linear_sampler : register(s12, space0);

struct PathTracingRayPayload
{
    float3 radiance;
    uint   hit;            // 0=miss, 1=hit; shadow rays reuse as: 0=occluded, 1=unoccluded
    RNG    rng;
    bool   is_shadow_ray;  // true = shadow ray, false = normal ray
    uint   bounce_depth;   // 0=primary ray, 1=first indirect bounce, ...
};

[shader("raygeneration")]
void PathTracingRayGen()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 extent = DispatchRaysDimensions().xy;
    const float2 uv = (float2(pixel) + 0.5f) / float2(max(extent, uint2(1, 1)));

    const float2 ndc_uv = float2(uv.x, 1.0f - uv.y);
    const float4 ndc = float4(UVToNdcXY(ndc_uv), 1.0f, 1.0f);
    const float4 world = mul(g_frame_data.proj_view_matrix_inv, ndc);
    const float3 world_position = world.xyz / max(world.w, 0.00001f);

    PathTracingRayPayload payload;
    payload.radiance      = float3(0.0f, 0.0f, 0.0f);
    payload.hit           = 0u;
    payload.rng           = InitRNG(pixel, extent, g_frame_data.sample_index);
    payload.is_shadow_ray = false;
    payload.bounce_depth  = 0u;

    if (g_frame_data.instance_count > 0)
    {
        RayDesc ray;
        ray.Origin    = g_frame_data.camera_position;
        ray.Direction = normalize(world_position - g_frame_data.camera_position);
        ray.TMin      = 0.001f;
        ray.TMax      = 100000.0f;

        TraceRay(g_scene_tlas, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
    }

    // Temporal accumulation blend
    float3 prev_accum = float3(0.0f, 0.0f, 0.0f);
    float sample_count = 1.0f;

    if (g_frame_data.reset_accumulation == 0)
    {
        prev_accum = g_accumulation_prev.Load(int3(pixel, 0)).rgb;
        sample_count = float(g_frame_data.sample_index) + 1.0f;
    }

    const float3 blended = (prev_accum * (sample_count - 1.0f) + payload.radiance) / sample_count;

    // Accumulation at trace resolution
    g_accumulation_output[pixel] = float4(blended, 1.0f);

    // Nearest-neighbor upscale to full-resolution scene output
    const uint2 full_pixel = pixel * g_frame_data.trace_scale;
    g_scene_output[full_pixel] = float4(blended, 1.0f);
    g_scene_output[full_pixel + uint2(1, 0)] = float4(blended, 1.0f);
    g_scene_output[full_pixel + uint2(0, 1)] = float4(blended, 1.0f);
    g_scene_output[full_pixel + uint2(1, 1)] = float4(blended, 1.0f);
}

[shader("miss")]
void PathTracingMiss(inout PathTracingRayPayload payload)
{
    if (payload.is_shadow_ray)
    {
        payload.hit = 0u;
        payload.radiance = float3(1.0f, 1.0f, 1.0f);
    }
    else
    {
        const float3 ray_dir = WorldRayDirection();
        // Match the coordinate convention used by SampleEnvironmentLight:
        // the engine cubemaps use (x, z, y) mapping
        const float3 sample_dir = float3(ray_dir.x, ray_dir.z, ray_dir.y);
        const float3 sky_color = g_specular_texture.SampleLevel(g_linear_sampler, sample_dir, 0.0f).rgb;
        payload.radiance = sky_color;
        payload.hit      = 0u;
    }
}

float3 CosineSampleHemisphere(float2 u, out float pdf)
{
    const float r   = sqrt(u.x);
    const float phi = 2.0f * 3.14159265f * u.y;
    pdf = sqrt(max(1.0f - u.x, 0.0f)) / 3.14159265f;
    return float3(r * cos(phi), r * sin(phi), sqrt(max(1.0 - u.x, 0.0)));
}

float3 TangentToWorld(float3 v_local, float3 n)
{
    const float3 up = abs(n.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 t = normalize(cross(up, n));
    const float3 b = cross(n, t);
    return v_local.x * t + v_local.y * b + v_local.z * n;
}

#define MAX_BOUNCES 4

// Next-event estimation: sample environment light (IBL) as direct lighting
// This is a simplified version matching the deferred pipeline's IBL contribution
float3 SampleEnvironmentLight(float3 n, float3 v, float3 base_color, float metallic, float roughness, float3 f0)
{
    float3 origin_samplecube_n = float3(n.x, n.z, n.y);

    float3 irradiance = g_irradiance_texture.SampleLevel(g_linear_sampler, origin_samplecube_n, 0.0f).rgb;
    float3 diffuse = irradiance * base_color;

    float3 f = F_Schlick(clamp(dot(n, v), 0.0f, 1.0f), f0);
    float3 kd = (1.0f - f) * (1.0f - metallic);

    return kd * diffuse;
}

[shader("closesthit")]
void PathTracingClosestHit(inout PathTracingRayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    if (payload.is_shadow_ray)
    {
        payload.hit = 1u;
        return;
    }

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
    const float2 texcoord = v0.texcoord.xy * barycentric.x +
                            v1.texcoord.xy * barycentric.y +
                            v2.texcoord.xy * barycentric.z;

    const float3x4 object_to_world = ObjectToWorld3x4();
    float3 N = normalize(mul((float3x3)object_to_world, normal_os));
    const float3 world_position = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    float3 V = -WorldRayDirection();
    if (dot(N, V) < 0.0f)
    {
        N = -N;
    }

    float3 base_color = material_data.base_color_factor.rgb;
    if (material_data.base_color_texture_index < PICCOLO_PATH_TRACING_MAX_MATERIAL_TEXTURES)
    {
        base_color *= g_texture_array[material_data.base_color_texture_index].SampleLevel(
            g_linear_sampler, texcoord, 0.0f).rgb;
    }

    float metallic = material_data.metallic_roughness_normal_occlusion.x;
    float roughness = material_data.metallic_roughness_normal_occlusion.y;
    if (material_data.metallic_roughness_texture_index < PICCOLO_PATH_TRACING_MAX_MATERIAL_TEXTURES)
    {
        const float4 mr = g_texture_array[material_data.metallic_roughness_texture_index].SampleLevel(
            g_linear_sampler, texcoord, 0.0f);
        metallic *= mr.b;
        roughness *= mr.g;
    }
    roughness = max(0.04f, roughness);

    float3 emissive = material_data.emissive_factor.rgb;
    if (material_data.emissive_texture_index < PICCOLO_PATH_TRACING_MAX_MATERIAL_TEXTURES)
    {
        emissive *= g_texture_array[material_data.emissive_texture_index].SampleLevel(
            g_linear_sampler, texcoord, 0.0f).rgb;
    }

    const float3 f0 = lerp(0.04f, base_color, metallic);
    float3 Lo = float3(0.0f, 0.0f, 0.0f);

    // --- Directional light (shadow ray only on primary bounce) ---
    {
        const float3 L = normalize(g_frame_data.scene_directional_light.direction);
        const float NdotL = dot(N, L);

        if (NdotL > 0.0f)
        {
            if (payload.bounce_depth == 0u)
            {
                PathTracingRayPayload shadow_payload;
                shadow_payload.radiance      = float3(0.0f, 0.0f, 0.0f);
                shadow_payload.hit           = 0u;
                shadow_payload.rng           = payload.rng;
                shadow_payload.is_shadow_ray = true;
                shadow_payload.bounce_depth  = 0u;

                RayDesc shadow_ray;
                shadow_ray.Origin    = world_position + N * 0.01f;
                shadow_ray.Direction = L;
                shadow_ray.TMin      = 0.001f;
                shadow_ray.TMax      = 100000.0f;

                TraceRay(g_scene_tlas, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
                         0xFF, 0, 1, 0, shadow_ray, shadow_payload);
                payload.rng = shadow_payload.rng;

                if (shadow_payload.hit == 0u)
                {
                    Lo += BRDF(L, V, N, f0, base_color, metallic, roughness) *
                          g_frame_data.scene_directional_light.color * NdotL;
                }
            }
            else
            {
                // Indirect bounce: unshadowed directional light
                Lo += BRDF(L, V, N, f0, base_color, metallic, roughness) *
                      g_frame_data.scene_directional_light.color * NdotL;
            }
        }
    }

    // --- Point lights (shadow rays only on primary bounce) ---
    if (payload.bounce_depth == 0u)
    {
        const uint point_light_count = min(g_frame_data.point_light_count, M_MAX_POINT_LIGHT_COUNT);
        for (uint li = 0; li < point_light_count; li++)
        {
            const PointLight light = g_frame_data.scene_point_lights[li];
            const float3 light_vec = light.position - world_position;
            const float  light_dist = length(light_vec);
            const float3 L = light_vec / light_dist;
            const float  NdotL = dot(N, L);

            if (NdotL > 0.0f)
            {
                PathTracingRayPayload shadow_payload;
                shadow_payload.radiance      = float3(0.0f, 0.0f, 0.0f);
                shadow_payload.hit           = 0u;
                shadow_payload.rng           = payload.rng;
                shadow_payload.is_shadow_ray = true;
                shadow_payload.bounce_depth  = 0u;

                RayDesc shadow_ray;
                shadow_ray.Origin    = world_position + N * 0.01f;
                shadow_ray.Direction = L;
                shadow_ray.TMin      = 0.001f;
                shadow_ray.TMax      = light_dist - 0.001f;

                TraceRay(g_scene_tlas, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
                         0xFF, 0, 1, 0, shadow_ray, shadow_payload);
                payload.rng = shadow_payload.rng;

                if (shadow_payload.hit == 0u)
                {
                    const float attenuation = 1.0f / max(light_dist * light_dist, 0.0001f);
                    Lo += BRDF(L, V, N, f0, base_color, metallic, roughness) *
                          light.intensity * attenuation * NdotL;
                }
            }
        }
    }
    else
    {
        // On indirect bounces: approximate point light contribution without shadow rays
        const uint point_light_count = min(g_frame_data.point_light_count, M_MAX_POINT_LIGHT_COUNT);
        for (uint li = 0; li < point_light_count; li++)
        {
            const PointLight light = g_frame_data.scene_point_lights[li];
            const float3 light_vec = light.position - world_position;
            const float  light_dist = length(light_vec);
            const float3 L = light_vec / light_dist;
            const float  NdotL = max(dot(N, L), 0.0f);

            if (NdotL > 0.0f)
            {
                const float attenuation = 1.0f / max(light_dist * light_dist, 0.0001f);
                Lo += BRDF(L, V, N, f0, base_color, metallic, roughness) *
                      light.intensity * attenuation * NdotL;
            }
        }
    }

    // --- Indirect bounce with Russian Roulette ---
    if (payload.bounce_depth < MAX_BOUNCES)
    {
        // PBRT v4: terminate paths proportional to (1 - base_color luminance)
        const float rr_lum = max(base_color.r, max(base_color.g, base_color.b));
        const float rr_prob = clamp(rr_lum, 0.05f, 0.95f);
        if (Rand01(payload.rng) < rr_prob) // survived
        {
            float pdf;
            const float3 sample_dir = CosineSampleHemisphere(Rand2D(payload.rng), pdf);
            const float3 L = TangentToWorld(sample_dir, N);
            const float  NdotL = max(dot(N, L), 0.0f);

            if (pdf > 0.0f && NdotL > 0.0f)
            {
                PathTracingRayPayload indirect_payload;
                indirect_payload.radiance      = float3(0.0f, 0.0f, 0.0f);
                indirect_payload.hit           = 0u;
                indirect_payload.rng           = payload.rng;
                indirect_payload.is_shadow_ray = false;
                indirect_payload.bounce_depth  = payload.bounce_depth + 1u;

                RayDesc indirect_ray;
                indirect_ray.Origin    = world_position + N * 0.01f;
                indirect_ray.Direction = L;
                indirect_ray.TMin      = 0.001f;
                indirect_ray.TMax      = 100000.0f;

                TraceRay(g_scene_tlas, RAY_FLAG_NONE, 0xFF, 0, 1, 0, indirect_ray, indirect_payload);
                payload.rng = indirect_payload.rng;

                if (indirect_payload.hit != 0u)
                {
                    // re-weight by 1/P for unbiased estimator
                    Lo += BRDF(L, V, N, f0, base_color, metallic, roughness) *
                          indirect_payload.radiance * NdotL / pdf / rr_prob;
                }
            }
        }
    }

    float3 libl = SampleEnvironmentLight(N, V, base_color, metallic, roughness, f0);
    float3 la = base_color * g_frame_data.ambient_light.rgb;

    payload.radiance = emissive + Lo + libl + la;
    payload.hit = 1u;
}