#ifndef PICCOLO_PATH_TRACING_LIGHT_HLSLI
#define PICCOLO_PATH_TRACING_LIGHT_HLSLI

#include "path_tracing_rng.hlsli"
// sampling.hlsli only depends on path_tracing_core.hlsli (already included by
// lib.hlsl before us), so we can pull it in here to make BRDFPdf /
// MISWeightPower visible inside EstimateDirectLight without re-architecting.
#include "path_tracing_sampling.hlsli"

// Unified light buffer + next-event estimation. Lights are uploaded by the CPU
// (PathTracingPass::buildLightBuffer) as a single StructuredBuffer<PathTracingLight>,
// replacing the scattered scene_point_lights[] / scene_directional_light fields
// that used to live in PathTracingFrameData (plan Task 2 Step 1/4).

// Maximum lights in the unified buffer. Must match the CPU constant
// kPathTracingMaxLightCount in path_tracing_pass.cpp.
#define PT_MAX_LIGHTS 32u

#define PT_LIGHT_SKY         0u
#define PT_LIGHT_DIRECTIONAL 1u
#define PT_LIGHT_POINT       2u

// Default soft-sun half-angle in degrees (~the sun's angular radius). Task 3
// makes this configurable via PathTracingDirectionalAngleDeg.
#define PT_DEFAULT_SUN_HALF_ANGLE_DEG 0.53f

struct PathTracingLight
{
    float3 position;   // point light position
    float  param0;     // directional: sin(halfAngle); point: radius (0 = delta)
    float3 direction;  // directional: direction FROM surface TO light (at infinity)
    float  param1;     // reserved
    float3 color;      // directional: irradiance color; point: radiant intensity
    uint   type;
};

// Unified light buffer, populated by PathTracingPass::buildLightBuffer each frame.
// t1035 sits between the material texture array (t11..t1034) and the skinned
// vertex buffer (t1036) -- the texture array's 1024 descriptors occupy that
// whole D3D12 register range, so low slots like t13 are not free there.
StructuredBuffer<PathTracingLight> g_lights : register(t1035, space0);

// Build an orthonormal basis with `n` as the z axis.
void PathTracingBuildOnb(float3 n, out float3 t, out float3 b)
{
    const float3 up = abs(n.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

// Uniform cone sampling around `axis` (half-angle from cos_half). Returns the
// sampled direction and the solid-angle pdf (1 / Omega_cone).
float3 SampleCone(float2 u, float3 axis, float cos_half, out float pdf)
{
    const float cos_theta = lerp(cos_half, 1.0f, u.x);
    const float sin_theta = sqrt(max(1.0f - cos_theta * cos_theta, 0.0f));
    const float phi       = 2.0f * 3.14159265f * u.y;
    const float3 local    = float3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);

    float3 t, b;
    PathTracingBuildOnb(axis, t, b);
    const float3 wi = normalize(local.x * t + local.y * b + local.z * axis);

    const float Omega = 2.0f * 3.14159265f * (1.0f - cos_half);
    pdf = 1.0f / max(Omega, 1e-8f);
    return wi;
}

// Sample one light for NEE. Outputs wi (world dir toward light), Li (radiance),
// pdf (solid-angle pdf; 1 for delta point lights), dist (distance to light, or
// 1e30 for directional). Li/pdf together reduce to the light's "color" so the
// contribution matches the old BRDF*color*NdotL form (plan Task 2 Step 2):
//   directional: f * Li * cos / pdf = f * (color/Omega) * cos * Omega = f * color * cos
//   point (delta): f * Li * cos / pdf = f * (intensity/r^2) * cos * 1
//   sky          : f * Li * cos / pdf = f * env(dir) * cos * 4*pi
//                 (uniform sphere sampling, 1/(4*pi) pdf, Li in radiance)
void SampleLight(PathTracingLight light,
                 float3           shading_pos,
                 inout RNG        rng,
                 out float3       wi,
                 out float3       Li,
                 out float        pdf,
                 out float        dist)
{
    wi   = float3(0.0f, 0.0f, 0.0f);
    Li   = float3(0.0f, 0.0f, 0.0f);
    pdf  = 0.0f;
    dist = 0.0f;

    if (light.type == PT_LIGHT_DIRECTIONAL)
    {
        const float sin_half = light.param0;
        const float cos_half = sqrt(max(1.0f - sin_half * sin_half, 0.0f));
        wi = SampleCone(Rand2D(rng), light.direction, cos_half, pdf);
        const float Omega = 2.0f * 3.14159265f * (1.0f - cos_half);
        Li   = light.color / max(Omega, 1e-8f);
        dist = 1e30f; // light at infinity
    }
    else if (light.type == PT_LIGHT_POINT)
    {
        const float3 to_light = light.position - shading_pos;
        dist = length(to_light);
        if (dist <= 1e-6f)
        {
            return;
        }
        wi  = to_light / dist;
        // Delta point light: pdf = 1, Li = intensity / r^2.
        pdf = 1.0f;
        Li  = light.color / max(dist * dist, 1e-8f);
    }
    else if (light.type == PT_LIGHT_SKY)
    {
        // Uniform sphere sampling. pdf = 1 / (4 * pi). We sample the diffuse
        // irradiance cubemap directly so Li is the per-direction env radiance
        // (which has been convolved for diffuse in IBL bake). The result:
        // contribution = f * Li * cos / pdf = f * env(dir) * cos * 4*pi
        // which equals the diffuse sky integral exactly when the env is mostly
        // diffuse. light.color from the CPU is unused here but kept in the
        // struct for diagnostics / future importance-sampled sky NEE.
        const float u1 = Rand01(rng);
        const float u2 = Rand01(rng);
        const float z   = 1.0f - 2.0f * u1;
        const float rr  = sqrt(max(1.0f - z * z, 0.0f));
        const float phi = 2.0f * 3.14159265f * u2;
        wi = normalize(float3(rr * cos(phi), rr * sin(phi), z));
        pdf = 1.0f / (4.0f * 3.14159265f);
        Li = g_irradiance_texture.SampleLevel(g_linear_sampler, wi, 0.0f).rgb;
        dist = 1e30f; // sky at infinity
    }
}

// Trace a visibility (shadow) ray with the independent minimal payload.
// Returns true if occluded. occluded is initialized to 1; the shadow miss
// shader (SBT miss index 1) clears it to 0 when the light is reached.
// SKIP_CLOSEST_HIT_SHADER + ACCEPT_FIRST_HIT_AND_END_SEARCH means any geometry
// hit ends the ray without running a closest-hit shader, leaving occluded = 1.
bool TraceShadowRay(float3 origin, float3 wi, float max_dist)
{
    PathShadowPayload shadow;
    shadow.occluded = 1u;

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = wi;
    ray.TMin      = 0.001f;
    ray.TMax      = max_dist;

    TraceRay(g_scene_tlas,
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
             0xFF, 0, 1, 1, ray, shadow); // missShaderIndex = 1 (shadow miss)
    return shadow.occluded != 0u;
}

// Next-event estimation: sum direct lighting from every light at the surface.
// Loops all lights; for each visible sample weights by power-heuristic MIS
// (plan Task 3 Step 3). Use the BSDF pdf that matches the LO lobe that would
// have produced the same wi (BRDFPdf from path_tracing_sampling.hlsli).
float3 EstimateDirectLight(PathTracingSurface s, float3 wo, inout RNG rng)
{
    float3 Lo = float3(0.0f, 0.0f, 0.0f);
    const uint light_count = min(g_frame_data.light_count, PT_MAX_LIGHTS);

    [loop]
    for (uint i = 0u; i < light_count; ++i)
    {
        const PathTracingLight light = g_lights[i];

        float3 wi;
        float3 Li;
        float  pdf;
        float  dist;
        SampleLight(light, s.position, rng, wi, Li, pdf, dist);

        if (pdf <= 0.0f || dot(Li, Li) <= 0.0f)
        {
            continue;
        }

        const float NdotL = dot(s.normal, wi);
        if (NdotL <= 0.0f)
        {
            continue;
        }

        const float max_dist = (light.type == PT_LIGHT_POINT) ? (dist - 0.001f) : 1e30f;
        if (TraceShadowRay(s.position + s.normal * 0.001f, wi, max_dist))
        {
            continue; // occluded
        }

        const float3 f = EvalBSDF(s, wo, wi);
        if (dot(f, float3(1.0f, 1.0f, 1.0f)) <= 0.0f)
        {
            continue;
        }

        // MIS weight: light strategy vs BSDF strategy (power heuristic, beta=2).
        // Delta lights (point, directional in the limit) get pdf_light = 1 or
        // 1/Omega, while the BSDF pdf for the same wi is bounded by shape
        // factors -- the MIS naturally upweights the dominant strategy.
        uint   lobe;
        float  pdf_bsdf = BRDFPdf(s, wo, wi, lobe);
        float  w_light  = MISWeightPower(pdf, pdf_bsdf);

        Lo += w_light * f * Li * NdotL / pdf;
    }
    return Lo;
}

#endif
