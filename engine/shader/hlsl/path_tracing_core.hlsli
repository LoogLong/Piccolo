#ifndef PICCOLO_PATH_TRACING_CORE_HLSLI
#define PICCOLO_PATH_TRACING_CORE_HLSLI

#include "path_tracing_rng.hlsli"

// =============================================================================
// Path tracing integrator core.
//
// Architecture (see Docs/plans/2026-07-10-path-tracing-correct-lighting.md):
//   raygen owns the iterative path loop + PathState; the closest-hit shader is
//   thin (it only records the hit); material fetch and shading happen in
//   raygen. This is an iterative throughput path integrator, mathematically
//   equivalent to the textbook recursive formulation but with the loop in
//   raygen so the pipeline recursion depth can drop to 1.
//
// DXR constraint that shapes the payload: ObjectToWorld3x4() / RayTCurrent()
// are CHS-only built-ins, so the world-space normal MUST be computed in CHS.
// Material lookup (g_materials + textures) stays in raygen ("material in
// raygen buffer" per the plan). Field order is chosen so the payload packs
// into exactly 32 bytes (two 16-byte registers), matching the D3D12 payload
// size budget (kRayTracingMaxPayloadSizeBytes = 32):
//   reg0: float3 world_normal + float hit_t
//   reg1: float2 texcoord    + uint instance_index + uint flags
// =============================================================================

// Payload flags.
#define PT_HIT_FLAG 1u   // bit0: the ray hit geometry (cleared on miss)

// Hit record written by the thin closest-hit shader and consumed by raygen.
// Raygen reconstructs the world hit position as origin + hit_t * direction.
struct PathTracingHitPayload
{
    float3 world_normal;    // geometry normal in world space, face-forward to the ray
    float  hit_t;           // RayTCurrent(); raygen reconstructs world position from it
    float2 texcoord;        // interpolated vertex texcoord (for material texture sampling)
    uint   instance_index;  // InstanceIndex() (for material lookup via g_instances)
    uint   flags;           // PT_HIT_FLAG on hit
};

// Independent, minimal shadow payload for NEE visibility rays (Task 2+).
// Init occluded = 1; the shadow miss shader clears it to 0 (light reached).
struct PathShadowPayload
{
    uint occluded;
};

// Per-path state. Lives only in raygen (never passed through a payload).
struct PathState
{
    float3 radiance;     // accumulated path radiance
    float3 throughput;   // product of f_r * |n.wi| / pdf and RR reweighting
    float3 origin;       // current ray origin
    float3 direction;    // current ray direction
    uint   bounce;       // current bounce index
    RNG    rng;          // per-pixel RNG
};

// Placeholder until Task 3 promotes this to a FrameData uniform and deletes
// the #define (see plan Task 3 Step 4).
#define PT_PLACEHOLDER_MAX_BOUNCES 4u

// Shading surface material + geometry derived from a hit.
struct PathTracingSurface
{
    float3 base_color;
    float3 emissive;
    float3 normal;       // world-space shading normal (face-forward)
    float3 position;     // world-space hit position
    float  metallic;
    float  roughness;
    float3 f0;
};

// Load material + textures for a hit. Geometry (normal/texcoord) comes from
// the payload (computed in CHS); the world position is reconstructed from the
// ray and hit_t. Only the material buffers/textures are read here.
PathTracingSurface LoadHitSurface(PathTracingHitPayload hit, float3 ray_origin, float3 ray_direction)
{
    PathTracingSurface surface;
    surface.position = ray_origin + hit.hit_t * ray_direction;
    surface.normal   = hit.world_normal;

    const uint safe_instance_count = max(g_frame_data.instance_count, 1u);
    const uint instance_index      = min(hit.instance_index, safe_instance_count - 1u);
    const PathTracingInstanceData instance_data = g_instances[instance_index];
    const PathTracingMaterialData material_data = g_materials[instance_data.material_index];

    float3 base_color = material_data.base_color_factor.rgb;
    if (material_data.base_color_texture_index < PICCOLO_PATH_TRACING_MAX_MATERIAL_TEXTURES)
    {
        base_color *= g_texture_array[material_data.base_color_texture_index].SampleLevel(
            g_linear_sampler, hit.texcoord, 0.0f).rgb;
    }

    float metallic   = material_data.metallic_roughness_normal_occlusion.x;
    float roughness  = material_data.metallic_roughness_normal_occlusion.y;
    if (material_data.metallic_roughness_texture_index < PICCOLO_PATH_TRACING_MAX_MATERIAL_TEXTURES)
    {
        const float4 mr = g_texture_array[material_data.metallic_roughness_texture_index].SampleLevel(
            g_linear_sampler, hit.texcoord, 0.0f);
        metallic  *= mr.b;
        roughness *= mr.g;
    }
    roughness = max(0.04f, roughness);

    float3 emissive = material_data.emissive_factor.rgb;
    if (material_data.emissive_texture_index < PICCOLO_PATH_TRACING_MAX_MATERIAL_TEXTURES)
    {
        emissive *= g_texture_array[material_data.emissive_texture_index].SampleLevel(
            g_linear_sampler, hit.texcoord, 0.0f).rgb;
    }

    surface.base_color = base_color;
    surface.metallic   = metallic;
    surface.roughness  = roughness;
    surface.emissive   = emissive;
    // Dielectric F0 is unified to 0.04 for the path tracer (matches mesh).
    surface.f0         = lerp(0.04f, base_color, metallic);
    return surface;
}

// Sample the environment/sky background radiance for a direction (miss path).
// Uses the un-convolved specular cubemap (g_specular_texture), the same
// resource the raster sky samples — NOT the convolved irradiance map.
float3 SampleSkyRadiance(float3 direction)
{
    // Engine cubemaps use an (x, z, y) component mapping (matches the old miss
    // shader and SampleEnvironmentLight).
    const float3 sample_dir = float3(direction.x, direction.z, direction.y);
    return g_specular_texture.SampleLevel(g_linear_sampler, sample_dir, 0.0f).rgb;
}

// One bounce of the path. Returns true to continue with another bounce, false
// to terminate. Task 1 placeholder: a single flat-shaded bounce (emissive +
// small albedo term) so the scene is visible with correct orientation. Task 2
// adds NEE, Task 3 adds BSDF sampling + MIS + RR.
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

    // Task 1 placeholder shading: emissive + a small constant albedo term so
    // geometry reads without a lighting model. Real direct/indirect lighting
    // arrives in Task 2 (NEE) and Task 3 (BSDF + MIS + RR).
    path.radiance += path.throughput * (surface.emissive + surface.base_color * 0.1f);
    return false;
}

#endif
