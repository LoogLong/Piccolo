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
#define PT_LOBE_DIFFUSE   0u
#define PT_LOBE_SPECULAR  1u

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

// GGX NDF sampling (sample half-vector h from D_GGX; ignores visibility).
// pdf(wh) = D(h) * max(dot(n, h), 0). Standard Smith 1967 inversion.
// For very low roughness this is not as good as Heitz VNDF but is unbiased
// (plan Task 3 Step 1: "GGX VNDF or NDF at minimum"). Cos^2 theta_h
// inversion follows "Sampling Transformations Z" / Estier 2004.
float3 SampleGGX(float2 u, float3 n, float roughness)
{
    float a  = max(roughness * roughness, 1e-3f);
    float a2 = a * a;
    float cos_h_sq = (1.0f - u.x) / max(1.0f + u.x * (a2 - 1.0f), 1e-10f);
    float cos_h = sqrt(max(cos_h_sq, 0.0f));
    float sin_h = sqrt(max(1.0f - cos_h * cos_h, 0.0f));
    float phi   = 2.0f * 3.14159265f * u.y;

    float3 t, b;
    PT_BuildBasis(n, t, b);
    return normalize(sin_h * cos(phi) * t + sin_h * sin(phi) * b + cos_h * n);
}

// Sample the BSDF at a surface for one-bounce continuation (indirect light).
// One-sample MIS: the chosen lobe (diffuse vs specular) is a Bernoulli choice
// under probability Pdiffuse. So the combined BRDF pdf that the indirect
// continuation evaluates to is BRDFPdf(s, wo, wi, _), NOT a single lobe.
// Returns wi, pdf (combined), lobe (for stats / future use).
void SampleBRDF(PathTracingSurface s, float3 wo, inout RNG rng,
                out float3 wi, out float pdf, out uint lobe)
{
    // Dielectric -> mostly diffuse; metal -> mostly specular. Simple split.
    const float Pdiffuse = saturate(1.0f - s.metallic);

    if (Rand01(rng) < Pdiffuse)
    {
        lobe = PT_LOBE_DIFFUSE;
        wi   = SampleCosineHemisphere(Rand2D(rng), s.normal, pdf);
    }
    else
    {
        lobe = PT_LOBE_SPECULAR;
        float3 h = SampleGGX(Rand2D(rng), s.normal, s.roughness);
        wi = reflect(-wo, h);

        const float dot_wo_h = max(abs(dot(wo, h)), 1e-7f);
        const float dot_n_h  = max(dot(s.normal, h), 0.0f);
        const float a   = max(s.roughness * s.roughness, 1e-3f);
        const float a2  = a * a;
        const float denom = dot_n_h * dot_n_h * (a2 - 1.0f) + 1.0f;
        const float D   = a2 / (3.14159265f * denom * denom);
        const float pdf_h = D * dot_n_h;
        pdf = pdf_h / (4.0f * dot_wo_h);
    }
}

// Combined BSDF pdf for a given incident direction `wi` (under the lobe
// mixture used by SampleBRDF). This is what NEE passes to MISWeightPower
// against pdf_light. lobe is set to whichever lobe dominates (informational).
float BRDFPdf(PathTracingSurface s, float3 wo, float3 wi, out uint lobe)
{
    lobe = PT_LOBE_DIFFUSE;
    const float dot_n_l = dot(s.normal, wi);
    const float dot_n_v = dot(s.normal, wo);
    if (dot_n_l <= 0.0f || dot_n_v <= 0.0f) { return 0.0f; }

    const float3 h = normalize(wo + wi);
    const float dot_n_h = dot(s.normal, h);
    const float dot_v_h = dot(wo, h);
    if (dot_n_h <= 0.0f) { return 0.0f; }

    const float Pdiffuse = saturate(1.0f - s.metallic);

    // Diffuse lobe (cosine-weighted hemisphere).
    const float pdf_d = dot_n_l / 3.14159265f;

    // Specular lobe (NDF h, then reflect).
    const float a   = max(s.roughness * s.roughness, 1e-3f);
    const float a2  = a * a;
    const float denom = dot_n_h * dot_n_h * (a2 - 1.0f) + 1.0f;
    const float D   = a2 / (3.14159265f * denom * denom);
    const float pdf_h = D * dot_n_h;
    const float pdf_s = pdf_h / max(4.0f * max(dot_v_h, 1e-7f), 1e-7f);

    if (pdf_s > pdf_d) lobe = PT_LOBE_SPECULAR;
    return Pdiffuse * pdf_d + (1.0f - Pdiffuse) * pdf_s;
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
