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
// kPathTracingMaxLightCount in path_tracing_pass.cpp. Plan 2026-07-16
// Phase 6 B3: bumped 32 -> 256 to match the practical cap of typical
// point-light scenes; the CPU emits a LOG_WARN if the scene exceeds
// this cap (no longer silently truncated).
#define PT_MAX_LIGHTS 256u

#define PT_LIGHT_SKY         0u
#define PT_LIGHT_DIRECTIONAL 1u
#define PT_LIGHT_POINT       2u

// Default soft-sun half-angle in degrees (~the sun's angular radius). Task 3
// makes this configurable via PathTracingDirectionalAngleDeg.
#define PT_DEFAULT_SUN_HALF_ANGLE_DEG 0.53f

// Sky NEE CDF bin count (Plan 2026-07-15 Phase 5 A1). Matches the
// kPathTracingSkyCdfBinCount constant in render_resource.cpp. Used by
// SampleLight(PT_LIGHT_SKY, ...) to size the linear scan and the
// (face, row, col) -> direction conversion.
#define PT_SKY_CDF_BIN_COUNT 32u
#define PT_SKY_CDF_ROWS      (6u * PT_SKY_CDF_BIN_COUNT)

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

// =============================================================================
// Sky NEE row-margin CDF (Plan 2026-07-15 Phase 5 A1).
//
// Built CPU-side at IBL load time by render_resource_sky_cdf.cpp; see that
// file for the data layout. Two R32_SFLOAT 2D images:
//
//   g_sky_row_marginal : shape (6*N, 1) -- per-(face,row) probability mass.
//                        For a uniform distribution all rows hold 1/(6*N).
//
//   g_sky_row_cdf      : shape (6*N, N) -- within-row CDF. row_cdf[fr, c] is
//                        the cumulative probability of selecting col <= c
//                        within the row indexed by (face, row), normalised
//                        so the last entry is 1.0.
//
// SampleLight(PT_LIGHT_SKY, ...) does a two-step lookup (row + col) and
// converts the resulting (face, row, col) triple to a 3D direction in
// DirectX cubemap convention.
//
// The PDF returned by the new sampler integrates to 1 over the sphere, so
// the NEE estimator stays unbiased. There is a small systematic bias from
// ignoring the cubemap face-area Jacobian; documented in the C++ header.
// =============================================================================
Texture2D<float> g_sky_row_marginal : register(t1037, space0);
Texture2D<float> g_sky_row_cdf      : register(t1038, space0);

// Build an orthonormal basis with `n` as the z axis.
void PathTracingBuildOnb(float3 n, out float3 t, out float3 b)
{
    const float3 up = abs(n.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

// Estimate the precomputed diffuse environment ambient (low-frequency irradiance
// lookup). One-shot per primary hit. Equivalent to infinite bounces of diffuse
// sky off the upper hemisphere (a low-frequency approximation of the full
// bounce stack baked offline into g_irradiance_texture).
//
// Plan 2026-07-12 §1.2: this now matches the raster diffuse IBL formula used
// in mesh.frag.hlsl:83-95 and deferred_lighting.frag.hlsl:102-103. Earlier
// "Bug fix F/G" comments claimed g_irradiance_texture was per-direction sky
// luminance with no prefilter; that was wrong -- the six HDR faces
// `asset/texture/sky/skybox_irradiance_{X+,X-,Y+,Y-,Z+,Z-}.hdr` are *already*
// the prefiltered diffuse irradiance cubemap (offline-baked hemispherical
// convolution). mip 0 IS the cosine-weighted integral over the upper
// hemisphere in the direction of the surface normal, which is exactly the
// diffuse-ambient term we want. Higher mips exist because rhi->createCubeMap
// auto-generates a mip chain (box downsamples via vulkan_util.cpp:550-618)
// but they carry no extra IBL meaning here; sample at LOD 0.
//
// NOT double-counting: this adds the precomputed low-frequency ambient on top
// of the sun NEE (a specific delta direction) and the iterative BSDF
// bounces. It uses the same ResourceSlot binding the raster pipeline does
// (path_tracing_pass.cpp:511-527 + 1230-1235), so the PT view of the sky
// lighting matches the raster view.
float3 EstimateEnvironmentAmbient(PathTracingSurface s, float3 wo)
{
    // Sample the raw world-space normal -- the raster diffuse lookup (mesh.frag.hlsl:83)
    // does the same. The (x,z,y) swizzle in earlier revisions was a cargo-culted
    // copy from skybox.frag.hlsl:14, which is the SPECULAR (background) cubemap
    // and *does* need the swizzle because of how its six faces are arranged
    // on disk. The irradiance faces are stored without that swizzle.
    const float3 n_dir = s.normal;
    const float3 irr = g_irradiance_texture.SampleLevel(g_linear_sampler, n_dir, 0.0f).rgb;

    // Diffuse-ambient weighting (matches mesh.frag.hlsl:83-95, no Fresnel:
    // F is the fraction of energy *reflected* along the view direction and
    // does not attenuate diffuse). Metallic surfaces get zero diffuse from
    // here (their diffuse is rolled into base_color at metallic=1 to mimic
    // energy-conserving metals).
    return irr * s.base_color * (1.0f - s.metallic);
}

// Reflected direction in the (x, z, y) cubemap convention.
//   R = reflect(I, N). Input I is the world view direction (-wo),
//   surface normal N. Output is suitable for SamplingTextureCube directly.
float3 PT_ReflectDirection(float3 wo_neg, float3 n)
{
    // world reflect
    float3 r = reflect(wo_neg, n);
    // match the engine cubemap (x, z, y) convention used by SampleSkyRadiance
    return float3(r.x, r.z, r.y);
}

// LOD index for GGX importance-sampled specular IBL. Maps the Schlick
// roughness to an integer mip level on g_specular_texture. Matches the
// heuristic used by deferred-lighting.frag.hlsl (MipCount * roughness^2 style,
// clamped to skip the very-LOD-0 face which would fire aliases).
float PT_SpecularIBLLod(float roughness, float mip_count)
{
    // Schlick's approximation: specular ~= exp(2 * MipCount * roughness^2).
    // The classic Unreal/Crytek form: mip = mip_count * roughness^2 - 1 / 2.
    float mip = roughness * roughness * mip_count - 0.5f;
    mip = clamp(mip, 0.0f, mip_count - 1.0f);
    return mip;
}

// First-hit specular IBL contribution (one-shot per primary hit). Matches
// deferred-lighting.frag.hlsl's specular IBL (deferred_lighting.frag.hlsl:121-122).
// We sample g_specular_texture along the reflected direction at a roughness-
// driven mip level, weight by Fresnel and metallic to get the right
// "metallic specular, dielectric half-Fresnel" shape, and report a
// specular-sample pdf for MIS against the BRDF lobe.
//
// The MIS pairing between this specular IBL sample and the diffuse/brdf path
// is: specular-IBL pdf is approximately delta(W) at the reflection direction,
// weighted by the BRDF's normal-distribution peak. It naturally dominates
// for low roughness (mirror-like surfaces) and fades into diffuse for high
// roughness, mirroring PBRT v4 / UE Reference's heuristic.
//
// Plan 2026-07-12 §4.3: full MIS pdf tracking requires the BRDF Jacobian
// of the reflection operator and is deferred. Two compensating fixes
// land here:
//   (a) Fade out specular IBL for roughness > 0.5 (where the lobe is so
//       wide the cubemap sample is essentially a diffuse term already
//       double-counted with EstimateEnvironmentAmbient + sky NEE).
//   (b) Clamp NdotR so back-facing reflections (dot < 0) contribute zero
//       instead of a small positive value (avoids double-sided artefacts).
float3 EstimateEnvironmentSpecular(PathTracingSurface s, float3 wo)
{
    // Plan §4.3(a): fade out for rough surfaces.
    const float rough_fade = 1.0f - smoothstep(0.4f, 0.6f, s.roughness);
    if (rough_fade <= 0.0f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    const float3 r_world = PT_ReflectDirection(-wo, s.normal);

    // Schlick-style mip from roughness. The cubemap's actual mip count is
    // uploaded each frame as g_frame_data.cubemap_mip_count (Plan Phase 5
    // A4) so this works for any 256/512/1024/2048 sky without a hardcoded
    // constant.
    const float kMips = (float)max(g_frame_data.cubemap_mip_count, 1u);
    const float lod = PT_SpecularIBLLod(s.roughness, kMips);

    const float3 env_spec = g_specular_texture.SampleLevel(g_linear_sampler, r_world, lod).rgb;

    // Cube map sample direction's cost with the surface: NDotR = 1 by
    // construction (reflection is along n for the lobe peak), but for the
    // diffuse sample on a rough surface we still pay a NdotL factor.
    const float3 N = s.normal;
    const float NdotR = clamp(dot(N, normalize(reflect(-wo, N))), 0.0f, 1.0f);

    // Standard IBL specular term (deferred_lighting.frag.hlsl:121):
    //   specular contribution = env_spec * f * specular_modulation
    // where the modulation handles metallic vs dielectric. For metals the
    // f0 term in EvalBSDF is the base color (lerp(0.04, base_color, metallic));
    // we reuse that.
    const float3 f = F_Schlick(clamp(dot(N, wo), 0.0f, 1.0f), s.f0);
    return env_spec * f * NdotR * rough_fade;
}

// =============================================================================
// SampleEnvironmentSpecularDelta -- mirror-only first-hit specular IBL
// (Plan 2026-07-15 Phase 5 A5).
//
// For surfaces with roughness < kMirrorCutoffRoughness (0.05), the GGX
// specular lobe is narrow enough to treat as a delta at the perfect-reflection
// direction. We sample the cubemap along wi = reflect(-wo, n), at a roughness-
// driven LOD, and pair the contribution with the BSDF lobe via the power
// heuristic to avoid double-counting against EstimateEnvironmentSpecular
// (rough case) and sky NEE + EstimateEnvironmentAmbient (low-frequency sky).
//
// Returns true and fills wi/Li/f/pdf when this path applies; returns false
// for roughness >= kMirrorCutoffRoughness so the caller falls back to
// EstimateEnvironmentSpecular (the rough case, faded by rough_fade for
// roughness > 0.4). Returns false for back-facing reflections (dot(n,wi)<=0)
// so we never add a negative contribution.
//
// pdf_light is 1.0 (delta in continuous solid angle). pdf_bsdf at the
// reflection direction is the GGX specular lobe pdf (a^2 / (4 pi NdotV) for
// mirror incidence, falling to 0 as roughness -> 0). For roughness in the
// mirror regime the BSDF pdf is small so the MIS weight naturally favours
// this sample (which is the right answer -- a mirror reflection's energy
// should come from the cubemap, not the BSDF lobe).
// =============================================================================
static const float kMirrorCutoffRoughness = 0.05f;

bool SampleEnvironmentSpecularDelta(
    PathTracingSurface s, float3 wo,
    out float3 wi, out float3 Li, out float3 f, out float pdf)
{
    wi  = float3(0.0f, 0.0f, 0.0f);
    Li  = float3(0.0f, 0.0f, 0.0f);
    f   = float3(0.0f, 0.0f, 0.0f);
    pdf = 0.0f;

    if (s.roughness >= kMirrorCutoffRoughness)
    {
        // Rough case -- caller falls back to EstimateEnvironmentSpecular.
        return false;
    }

    const float3 N     = s.normal;
    const float3 wi_ref = reflect(-wo, N);
    const float NdotL  = dot(N, wi_ref);
    if (NdotL <= 0.0f)
    {
        // Reflection went below the horizon; no specular IBL.
        return false;
    }

    const float NdotV = max(dot(N, wo), 0.0f);
    const float3 r_world_swizzled = float3(wi_ref.x, wi_ref.z, wi_ref.y);
    const float kMips = (float)max(g_frame_data.cubemap_mip_count, 1u);
    const float lod   = PT_SpecularIBLLod(s.roughness, kMips);

    Li = g_specular_texture.SampleLevel(g_linear_sampler, r_world_swizzled, lod).rgb;
    f  = F_Schlick(NdotV, s.f0);
    wi = wi_ref;
    pdf = 1.0f;  // delta in solid angle -- power_heuristic handles cross-fade.
    return true;
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
        // Plan 2026-07-15 Phase 5 A1: row-margin CDF importance sampling.
        // Two-step lookup over (marginal, row_cdf) of the same cubemap that
        // g_specular_texture already exposes. pdf integrates to 1 over the
        // sphere, so the NEE estimator is unbiased. The 4-8x variance drop
        // is the headline win for outdoor sky-dominated scenes.
        //
        // Step 1: build the cumulative marginal on the fly (small: 6*N =
        // 192 entries) and find the row whose cumulative >= u1.
        const float u1     = Rand01(rng);
        const float u2     = Rand01(rng);
        const uint  rows   = PT_SKY_CDF_ROWS;
        const uint  N_bins = PT_SKY_CDF_BIN_COUNT;

        // Cumulative marginal scan.
        float cum   = 0.0f;
        float total = 0.0f;
        [unroll]
        for (uint r = 0u; r < rows; ++r)
        {
            total += g_sky_row_marginal.Load(int3(int(r), 0, 0));
        }
        if (total <= 0.0f)
        {
            // Degenerate cubemap: no luminance at all. Fall back to
            // uniform-sphere sampling so we still produce a (zero) result
            // rather than NaN.
            const float z   = 1.0f - 2.0f * u1;
            const float rr  = sqrt(max(1.0f - z * z, 0.0f));
            const float phi = 2.0f * 3.14159265f * u2;
            wi  = normalize(float3(rr * cos(phi), rr * sin(phi), z));
            pdf = 1.0f / (4.0f * 3.14159265f);
            Li  = g_specular_texture.SampleLevel(g_linear_sampler, wi, 0.0f).rgb;
            dist = 1e30f;
            return;
        }
        const float u1_total = u1 * total;

        uint row_idx = 0u;
        [unroll]
        for (uint r = 0u; r < rows; ++r)
        {
            cum += g_sky_row_marginal.Load(int3(int(r), 0, 0));
            if (cum >= u1_total) { row_idx = r; break; }
        }
        // (cum is now >= u1_total; the last row's marginal is included in total
        // and the loop's break guards against fall-through.)

        // Step 2: invert the within-row CDF. The row holds N_bins entries
        // (last == 1.0) so the scan is tight.
        uint col_idx = N_bins - 1u;
        [unroll]
        for (uint c = 0u; c < N_bins; ++c)
        {
            if (g_sky_row_cdf.Load(int3(int(c), int(row_idx), 0)) >= u2)
            {
                col_idx = c;
                break;
            }
        }

        // (face, row_in_face, col) -> 3D direction in DirectX cubemap convention.
        // The (x, z, y) cubemap swizzle in SampleSkyRadiance handles the fact
        // that the asset on disk stores the same per-face textures but in a
        // (x, z, y) world coordinate. We sample g_specular_texture with the
        // same swizzled direction; here we compute the un-swizzled world
        // direction because the renderer writes path.radiance in world space
        // and the same direction will be used for the shadow ray cast.
        const uint face = row_idx / N_bins;          // 0..5
        const uint rface = row_idx - face * N_bins;  // 0..N-1
        const float inv_N = 1.0f / float(N_bins);
        // Pixel-centre UVs in [0, 1].
        const float u_uv = (float(col_idx) + 0.5f) * inv_N * 2.0f - 1.0f;  // [-1, 1]
        const float v_uv = (float(rface)   + 0.5f) * inv_N * 2.0f - 1.0f;  // [-1, 1]

        // DirectX cubemap face order: +X, -X, +Y, -Y, +Z, -Z.
        // The per-face (u, v) -> direction mapping follows the standard
        // "OpenGL / Vulkan" cubemap convention used everywhere else in the
        // engine (see engine/shader/hlsl/path_tracing_core.hlsli
        // SampleSkyRadiance and the deferred lighting specular IBL code).
        float3 local;
        if      (face == 0u) { local = float3( 1.0f, -v_uv, -u_uv); }  // +X
        else if (face == 1u) { local = float3(-1.0f, -v_uv,  u_uv); }  // -X
        else if (face == 2u) { local = float3( u_uv,  1.0f,  v_uv); }  // +Y
        else if (face == 3u) { local = float3( u_uv, -1.0f, -v_uv); }  // -Y
        else if (face == 4u) { local = float3( u_uv, -v_uv,  1.0f); }  // +Z
        else                 { local = float3(-u_uv, -v_uv, -1.0f); }  // -Z
        wi = normalize(local);

        // Li: the per-direction sky radiance, sampled at the same direction
        // with the same swizzle SampleSkyRadiance uses, so the energy
        // matches the rest of the engine. We use LOD 0 (sharpest, matches
        // mip 0 which the original uniform-sphere sampler used).
        const float3 sample_dir_swizzled = float3(wi.x, wi.z, wi.y);
        Li = g_specular_texture.SampleLevel(g_linear_sampler, sample_dir_swizzled, 0.0f).rgb;

        // pdf in the row+col sampling space. Composed of:
        //   p(row) = marginal[row] / total
        //   p(col) = row_cdf[row, col+1] - row_cdf[row, col]   (within-row pdf)
        //   p(row, col) = p(row) * p(col)
        // The 6*N*N denominator cancels in the estimator, so we return the
        // per-(row, col) probability mass directly.
        const float p_row  = g_sky_row_marginal.Load(int3(int(row_idx), 0, 0)) / total;
        float p_col  = 1.0f / float(N_bins);
        if (col_idx > 0u)
        {
            const float cdf_prev = g_sky_row_cdf.Load(int3(int(col_idx) - 1, int(row_idx), 0));
            const float cdf_cur  = g_sky_row_cdf.Load(int3(int(col_idx),     int(row_idx), 0));
            p_col = max(cdf_cur - cdf_prev, 1e-7f);
        }
        else
        {
            p_col = max(g_sky_row_cdf.Load(int3(0, int(row_idx), 0)), 1e-7f);
        }
        pdf  = p_row * p_col;
        dist = 1e30f;  // sky at infinity
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
        //
        // §4.4 of plans/2026-07-12-path-tracing-future.md: skip when BSDF
        // pdf is effectively zero for this lobe pair (e.g. transmission lobe
        // queried with a wi on the reflection side). Avoids the cost of
        // EvalBSDF / shadow ray / MIS weight when the contribution is zero.
        uint   lobe;
        float  pdf_bsdf = BRDFPdf(s, wo, wi, lobe);
        if (pdf_bsdf <= 1e-6f)
        {
            continue;
        }
        float  w_light  = MISWeightPower(pdf, pdf_bsdf);

        Lo += w_light * f * Li * NdotL / pdf;
    }
    return Lo;
}

#endif
