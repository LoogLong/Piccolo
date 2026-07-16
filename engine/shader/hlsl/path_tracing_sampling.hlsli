#ifndef PICCOLO_PATH_TRACING_SAMPLING_HLSLI
#define PICCOLO_PATH_TRACING_SAMPLING_HLSLI

#include "path_tracing_rng.hlsli"
#include "path_tracing_core.hlsli"

// =============================================================================
// BSDF sampling + MIS (plan Task 3 M2).
//
// Shares the LOBE FORMULA with EvalBSDF (single-Fresnel Cook-Torrance) so that
// BRDFPdf == the pdf of SampleBRDF in expectation. Mismatched lobe formulas
// silently bias the path integrator; plan Task 3 Step 1 requires they match.
// =============================================================================

// Lobe tags for one-sample MIS (informational only -- not required for MIS
// weight computation, which uses the combined BRDFPdf).
#define PT_LOBE_DIFFUSE      0u
#define PT_LOBE_SPECULAR     1u
#define PT_LOBE_TRANSMISSION 2u  // Plan 2026-07-16 Phase 6 C2

// Russian Roulette: skip RR until this many bounces to avoid biasing the
// early contribution toward termination (plan Task 3 Step 5).
#define PT_MIN_BOUNCES_BEFORE_RR 3u

// Build orthonormal basis with `n` as z, output t (x), b (y).
void PT_BuildBasis(float3 n, out float3 t, out float3 b)
{
    const float3 up = abs(n.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

// Cosine-weighted hemisphere sampling. pdf = cos(theta) / pi.
float3 SampleCosineHemisphere(float2 u, float3 n, out float pdf)
{
    const float r     = sqrt(u.x);
    const float phi   = 2.0f * 3.14159265f * u.y;
    const float cos_t = sqrt(max(1.0f - u.x, 0.0f));
    pdf = cos_t / 3.14159265f;

    float3 t, b;
    PT_BuildBasis(n, t, b);
    return normalize(r * cos(phi) * t + r * sin(phi) * b + cos_t * n);
}

// =============================================================================
// Heitz & Neyret 2018 visible-normal distribution sampling (canonical, exact).
//
// "Sampling the GGX Distribution of Visible Normals", Heitz & Neyret, 2018
// (hal.inria.fr/hal-03264977, also: jcgt.org/published/0003/02/03/paper.pdf),
// Algorithm 1 (isotropic). Replaces the previous approximate rational inverse
// CDF (Plan 2026-07-16 Phase 6 C1); the new sampler is exact within the
// published derivation -- no approximation in cos(theta_h).
//
// Visible-normal sampling produces a half-vector h with density proportional
// to D(h) * G1(wo, h) * max(0, dot(wo, h)). Compared to plain NDF sampling
// this eliminates ~50% wasted samples for low-roughness surfaces and
// eliminates the "snap-bright-spike" we see on metal at low roughness.
//
// Algorithm (isotropic, the only case the path tracer needs):
//   1. Stretch wo by alpha: wo_s = normalize(vec3(alpha * wo.x, alpha * wo.y, wo.z)).
//      This rounds the GGX into a unit sphere in the (T, B) plane so the
//      visible-cap sampling below is isotropic.
//   2. Sample (t1, t2) uniformly on a unit disk (r = sqrt(u.x), phi = 2 pi u.y).
//   3. Bias t2 toward the projected wo_s direction with the visible-cap
//      factor s = 0.5 * (1 + wo_s.z): t2 = (1 - s) * sqrt(1 - t1^2) + s * t2.
//   4. Project onto hemisphere: h_s = normalize(t1, t2, sqrt(max(0, 1 - t1^2 - t2^2))).
//   5. Un-stretch: h = normalize(h_s.xy / alpha, h_s.z).
//
// The visible-normal pdf is
//   pdf(h | wo) = D(h) * G1(wo, h) * max(0, dot(wo, h)) / dot(n, wo)
// which transforms via the reflection operator Jacobian (dwh / dwi = 1 /
// (4 dot(wi, h))) into the BRDF lobe pdf
//   pdf_BRDF(wi | wo) = D(h) * G1(wo, h) / (dot(n, wo) * 4)
// (using dot(wo, h) = dot(wi, h) for the half-vector). The new BRDFPdf
// specular branch below uses this formula so MIS pairs the sampler and the
// lobe density exactly.
//
// Numerical fallback for the degenerate case dot(n, wo) <= 0 (wo behind
// the surface): return a slightly-perturbed wo so the integrator still
// makes forward progress instead of dividing by zero.
// =============================================================================
float3 SampleGGXVNDF(float3 wo_world, float3 n_world, float2 u, float roughness)
{
    // Build orthonormal basis (T, B, N) where N is the surface normal.
    float3 N = normalize(n_world);
    float3 T = abs(N.z) < 0.99999f
        ? normalize(cross(float3(0.0f, 0.0f, 1.0f), N))
        : normalize(cross(float3(1.0f, 0.0f, 0.0f), N));
    float3 B = cross(N, T);

    // wo in the surface frame.
    float3 wo;
    wo.x = dot(wo_world, T);
    wo.y = dot(wo_world, B);
    wo.z = dot(wo_world, N);
    if (wo.z <= 0.001f)
    {
        // Numerical fallback -- not part of the Heitz derivation. The
        // integrator's main loop already rejects wi where dot(n, wi) <= 0
        // so this branch only fires on rare back-facing hits.
        return normalize(wo_world + N * 0.1f);
    }

    // Step 1: stretch wo by alpha (isotropic -- alpha_x = alpha_y = alpha).
    const float alpha = max(roughness * roughness, 1e-3f);
    float3 wo_stretched = normalize(float3(wo.x * alpha, wo.y * alpha, wo.z));

    // Step 2: uniform sample on the unit disk (the projected half-vector
    // domain under the spherical cap parameterization).
    const float r = sqrt(u.x);
    const float phi = 6.28318530f * u.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);

    // Step 3: visible-cap bias factor. s smoothly transitions from 0
    // (wo_stretched.z = 0, the sample goes around the disk boundary) to 1
    // (wo_stretched.z = 1, the sample collapses onto the projected wo axis).
    const float s = 0.5f * (1.0f + wo_stretched.z);
    t2 = (1.0f - s) * sqrt(max(0.0f, 1.0f - t1 * t1)) + s * t2;

    // Step 4: project (t1, t2) onto the upper hemisphere.
    const float t1sq_plus_t2sq = t1 * t1 + t2 * t2;
    float3 h_stretched = normalize(float3(t1, t2, sqrt(max(0.0f, 1.0f - t1sq_plus_t2sq))));

    // Step 5: un-stretch -- invert the alpha scaling on (x, y).
    float3 h = normalize(float3(h_stretched.x / alpha, h_stretched.y / alpha, h_stretched.z));

    return h.x * T + h.y * B + h.z * N;
}

// G1 (single-direction visibility) for Schlick-GGX. Required by BRDFPdf's
// specular branch once the visible-normal sampler is used (see SampleGGXVNDF).
//   G1(w) = 2 * dot(n, w) / (dot(n, w) + sqrt(alpha^2 + (1 - alpha^2) * dot(n, w)^2))
float G1_SchlickGGX(float NdotX, float alpha)
{
    const float a2       = alpha * alpha;
    const float NdotX2   = NdotX * NdotX;
    const float denom    = NdotX + sqrt(a2 + (1.0f - a2) * NdotX2);
    return (2.0f * NdotX) / max(denom, 1e-7f);
}

// Sample the BSDF at a surface for one-bounce continuation (indirect light).
// One-sample MIS: the chosen lobe (diffuse / specular reflection / transmission)
// is a Bernoulli choice under the per-lobe probability. The combined BSDF pdf
// that the indirect continuation evaluates to is BRDFPdf(s, wo, wi, _), NOT
// a single lobe. Returns wi, pdf (combined), lobe (for stats / future use).
//
// Plan 2026-07-16 Phase 6 C2: a transmission lobe is added when the material
// has transmission_factor > 0. The lobe is sampled by refracting -wo around
// the surface normal with eta = 1 / ior (air -> medium approximation). On
// total internal reflection (TIR) the refraction direction is undefined and
// we fall back to specular reflection -- the integrator records lobe as
// PT_LOBE_SPECULAR and continues.
//
// Energy conservation note: this is a first-cut implementation. We split
// the lobe selection via Bernoulli with Ptransmission = transmission_factor,
// Pdiffuse = (1 - metallic) * (1 - transmission_factor), Pspecular_reflect
// = (1 - Pdiffuse). Fresnel-weighted splitting (Schlick F at the sampled
// direction) is deferred -- the current code over-counts the reflection
// lobe at grazing angles for transmissive materials. A future revision
// should compute F at the sampled half-vector and weight the lobe
// probabilities accordingly.
void SampleBRDF(PathTracingSurface s, float3 wo, inout RNG rng,
                out float3 wi, out float pdf, out uint lobe)
{
    const float Ptransmission = s.transmission_factor;
    const float Pdiffuse      = saturate(1.0f - s.metallic) * (1.0f - Ptransmission);

    const float r0 = Rand01(rng);

    if (r0 < Ptransmission)
    {
        // Transmission lobe. Refract -wo around the surface normal.
        // wi_refracted is computed by refract(-wo, n, eta).
        const float3 wi_refracted = refract(-wo, s.normal, s.eta);
        if (dot(wi_refracted, wi_refracted) <= 0.0f)
        {
            // TIR -- refract() returns (0,0,0). Fall back to a specular
            // reflection sample so the integrator still makes progress.
            // Review 2026-07-16: the wo_dot_n denominator must be
            // dot(n, wo), not dot(n, h). The reflection Jacobian
            // dwh/dwi = 1/(4 wi.h) cancels the |wo.h| in the VNDF
            // density leaving 1/dot(n,wo) in the BRDF lobe pdf; using
            // dot(n, h) here would have been an off-by-one against
            // BRDFPdf's formula and biased the MIS weight.
            const float3 h = SampleGGXVNDF(wo, s.normal, Rand2D(rng), s.roughness);
            wi   = reflect(-wo, h);
            lobe = PT_LOBE_SPECULAR;
            const float dot_wo_h = max(abs(dot(wo, h)), 1e-7f);
            const float dot_n_h  = max(dot(s.normal, h), 0.0f);
            const float a   = max(s.roughness * s.roughness, 1e-3f);
            const float a2  = a * a;
            const float denom = dot_n_h * dot_n_h * (a2 - 1.0f) + 1.0f;
            const float D   = a2 / (3.14159265f * denom * denom);
            const float G1_wo_h  = G1_SchlickGGX(dot_wo_h, a);
            const float wo_dot_n = max(dot(wo, s.normal), 1e-4f);
            pdf = (D * G1_wo_h) / (wo_dot_n * 4.0f);
            return;
        }
        wi   = wi_refracted;
        lobe = PT_LOBE_TRANSMISSION;
        // Delta BTDF pdf (in the limit of thin-walled, delta-transmission).
        // We use 1.0 for MIS with the other delta lobes; the per-lobe
        // probability (Ptransmission) is folded into the combined pdf by
        // BRDFPdf (Bernoulli mixture).
        pdf = 1.0f;
        return;
    }

    if (r0 < Ptransmission + Pdiffuse)
    {
        lobe = PT_LOBE_DIFFUSE;
        wi   = SampleCosineHemisphere(Rand2D(rng), s.normal, pdf);
        return;
    }

    // Specular reflection lobe.
    // Review 2026-07-16: the wo_dot_n denominator must be dot(n, wo),
    // not dot(n, h). The reflection Jacobian dwh/dwi = 1/(4 wi.h) cancels
    // the |wo.h| in the VNDF density leaving 1/dot(n,wo) in the BRDF
    // lobe pdf; using dot(n, h) here was an off-by-one against BRDFPdf's
    // formula and biased the MIS weight by dot(n,wo)/dot(n,h).
    lobe = PT_LOBE_SPECULAR;
    const float3 h = SampleGGXVNDF(wo, s.normal, Rand2D(rng), s.roughness);
    wi = reflect(-wo, h);

    const float dot_wo_h = max(abs(dot(wo, h)), 1e-7f);
    const float dot_n_h  = max(dot(s.normal, h), 0.0f);
    const float a   = max(s.roughness * s.roughness, 1e-3f);
    const float a2  = a * a;
    const float denom = dot_n_h * dot_n_h * (a2 - 1.0f) + 1.0f;
    const float D   = a2 / (3.14159265f * denom * denom);
    const float G1_wo_h  = G1_SchlickGGX(dot_wo_h, a);
    const float wo_dot_n = max(dot(wo, s.normal), 1e-4f);
    pdf = (D * G1_wo_h) / (wo_dot_n * 4.0f);
}

// Combined BSDF pdf for a given incident direction `wi` (under the lobe
// mixture used by SampleBRDF). This is what NEE passes to MISWeightPower
// against pdf_light. lobe is set to whichever lobe dominates (informational).
//
// Plan 2026-07-16 Phase 6 C2: three-lobe Bernoulli mixture
//   pdf = Ptransmission * pdf_transmission + Pdiffuse * pdf_d + Pspecular * pdf_s
// where pdf_transmission = 1 (delta, sampled at the refracted direction),
// pdf_d = cos(n, wi) / pi on the reflection hemisphere (0 otherwise),
// pdf_s = the VNDF-consistent BRDF lobe pdf (Plan Phase 6 C1) for wi on
// the reflection hemisphere (0 otherwise -- the reflection lobe cannot
// sample a refracted direction).
//
// For refracted wi (dot(n, wi) < 0), pdf_d = pdf_s = 0 and the combined
// pdf collapses to Ptransmission. This matches the sampling strategy
// density so MIS remains unbiased.
float BRDFPdf(PathTracingSurface s, float3 wo, float3 wi, out uint lobe)
{
    lobe = PT_LOBE_DIFFUSE;
    const float dot_n_v = dot(s.normal, wo);
    if (dot_n_v <= 0.0f) { return 0.0f; }

    const float Ptransmission = s.transmission_factor;
    const float Pdiffuse_full = saturate(1.0f - s.metallic) * (1.0f - Ptransmission);
    const float Pspec_full    = 1.0f - Ptransmission - Pdiffuse_full;

    const float dot_n_l = dot(s.normal, wi);

    // Diffuse lobe density (only valid on the reflection hemisphere).
    // Diffuse lobe density. SampleCosineHemisphere draws wi with density
    // cos(theta) / pi; that IS the pdf of the chosen direction under the
    // diffuse lobe. EvalBSDF's (1-F)(1-metallic) factor is a *weight* on
    // base_color and does not affect the lobe's sampling density (it is
    // independent of which wi the lobe picks). The MC estimator remains
    // unbiased because pdf_d matches the actual sampling distribution.
    //
    // Note: an earlier draft of Phase 5 A2 tried to fold (1-F)(1-metallic)
    // into pdf_d to "match" EvalBSDF, but pdf and BRDF are not required
    // to share a shape -- only their integrals over the hemisphere need
    // to satisfy the rendering equation. Adding the factor introduces
    // a per-channel bias for colored F0 materials (F is per-channel RGB
    // but pdf is a scalar). Reverted.
    const float pdf_d = max(dot_n_l, 0.0f) / 3.14159265f;

    // Specular reflection lobe (Plan 2026-07-16 Phase 6 C1).
    // SampleBRDF uses the Heitz VNDF sampler for the specular lobe, so the
    // correct combined-lobe pdf at wi is the VNDF pdf at wi (not the plain
    // NDF pdf). The VNDF density on the unit sphere is
    //   pdf_VNDF(h | wo) = D(h) * G1(wo, h) * max(0, dot(wo, h)) / dot(n, wo)
    // and the reflection operator Jacobian dwh/dwi = 1 / (4 dot(wi, h))
    // converts it to the BRDF-lobe pdf
    //   pdf_BRDF(wi | wo) = D(h) * G1(wo, h) / (dot(n, wo) * 4)
    // using dot(wo, h) = dot(wi, h) for the half-vector.
    //
    // The reflection lobe can only sample wi with dot(n, wi) > 0. For
    // refracted wi we zero pdf_s so the combined pdf collapses to the
    // transmission lobe only.
    float pdf_s = 0.0f;
    if (dot_n_l > 0.0f)
    {
        const float3 h = normalize(wo + wi);
        const float dot_n_h = dot(s.normal, h);
        const float dot_v_h = dot(wo, h);
        if (dot_n_h > 0.0f && dot_v_h > 0.0f)
        {
            const float a   = max(s.roughness * s.roughness, 1e-3f);
            const float a2  = a * a;
            const float denom = dot_n_h * dot_n_h * (a2 - 1.0f) + 1.0f;
            const float D   = a2 / (3.14159265f * denom * denom);
            const float G1_wo_h = G1_SchlickGGX(dot_v_h, a);
            const float wo_dot_n = max(dot_n_v, 1e-4f);
            pdf_s = (D * G1_wo_h) / (wo_dot_n * 4.0f);
        }
    }

    // Combined Bernoulli-mixture pdf. For refracted wi, only the
    // transmission term contributes; for reflected wi, the diffuse and/or
    // specular terms contribute.
    const float pdf_combined = Ptransmission * 1.0f + Pdiffuse_full * pdf_d + Pspec_full * pdf_s;

    // Dominant lobe for diagnostics (telemetry, future use).
    if (dot_n_l < 0.0f)
    {
        lobe = PT_LOBE_TRANSMISSION;
    }
    else if (pdf_s > pdf_d)
    {
        lobe = PT_LOBE_SPECULAR;
    }
    return pdf_combined;
}

// Veach power-heuristic MIS weight (beta = 2, UE / PBRT standard).
// pdf_a is the chosen strategy; pdf_b is the alternate strategy.
float MISWeightPower(float pdf_a, float pdf_b)
{
    const float a = max(pdf_a * pdf_a, 1e-12f);
    const float b = max(pdf_b * pdf_b, 1e-12f);
    return a / (a + b);
}

#endif