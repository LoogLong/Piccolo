// Plan 2026-07-12 §2.2: self-built 5x5 spatial-bilateral + temporal-blend
// fallback denoiser for vendor-SDK-less hosts (software rasterizer, Linux
// without NVIDIA/AMD SDK, debug builds).
//
// Plan 2026-07-16 Phase 6 B4 upgrade: the spatial filter now consumes
// albedo + normal/depth AOVs as additional range-weight features (the
// standard A-SVGF / kernel-prediction denoise setup). Three inputs are
// AOV-driven range weights:
//   - albedo (RGB) -- chrominance range weight; samples with similar
//     albedo to the centre are weighted higher. This is the dominant
//     edge-preservation term and replaces the RGB-distance weight used
//     in the previous bilateral pass.
//   - normal (XYZ in RGB) -- normal range weight; samples with normal
//     close to the centre are weighted higher (cuts across geometry
//     edges).
//   - depth (A of normal/depth) -- depth range weight; samples with
//     similar view-space z are weighted higher (cuts across depth
//     discontinuities).
//
// The luminance range weight is kept as a tie-breaker for very smooth
// regions where AOV differences are small.
//
// Inputs:
//   t0 g_current_accumulation    : per-pixel PT accum written this frame
//   t1 g_history_accumulation    : previous denoised output (ping-pong)
//   t4 g_aov_albedo              : primary-hit albedo (RGBA16F)
//   t5 g_aov_normal_depth        : primary-hit normal/depth (RGBA16F,
//                                 normal.xyz in RGB packed to [0,1])
//
// Output:
//   u2 g_denoised_output         : present to swapchain
//
// Push constants:
//   b3 DenoiseConstants { float strength; uint frame_index; uint2 extent; }
//
// Phase-2 design (Plan §2.2): 5x5 spatial bilateral + temporal blend only.
// The 5x5 kernel uses 4 range-weight terms (albedo + normal + depth +
// luminance) and a Gaussian spatial kernel. Strength drives the temporal
// alpha: 0.0 keeps raw accumulation (Quality preset), 1.0 trusts history
// fully (Interactive preset).

#include "common.hlsli"

Texture2D<float4>  g_current_accumulation    : register(t0, space0);
Texture2D<float4>  g_history_accumulation    : register(t1, space0);
RWTexture2D<float4> g_denoised_output        : register(u2, space0);

// Plan 2026-07-16 Phase 6 B4: AOV inputs from the raygen. Both are RGBA16F
// sampled with a linear sampler (filtering helps the bilateral kernel
// smooth sub-pixel misalignments between the AOV write position and the
// per-pixel denoise tap).
Texture2D<float4>  g_aov_albedo             : register(t4, space0);
Texture2D<float4>  g_aov_normal_depth       : register(t5, space0);

cbuffer DenoiseConstants : register(b3, space0)
{
    float  g_denoise_strength;  // 0..1; 0 = passthrough, 1 = full bilateral
    uint   g_frame_index;       // sample_index; 0 = first frame (no history)
    uint2  g_denoise_extent;
};

// Plan 2026-07-16 Phase 6 B4: per-channel sigma values for the new
// AOV-driven range weights. The previous bilateral used two scalar
// sigmas (kRangeSigmaLum, kRangeSigmaRGB); the new kernel has 3
// chrominance/normal/depth sigmas plus a small luminance tie-breaker.
// Tuning: lower sigma -> sharper edges (more filtering rejected), higher
// sigma -> smoother. Defaults match PBRT/ReSTIR-style A-SVGF.
static const float kSpatialSigma   = 0.05f;
static const float kRangeSigmaLum = 0.10f;  // luminance tie-breaker
static const float kRangeSigmaRGB = 0.06f;  // albedo chrominance
static const float kRangeSigmaN   = 0.10f;  // normal cosine distance
static const float kRangeSigmaZ   = 0.05f;  // depth relative distance

static const float kSpatialWeight[25] =
{
    1.0/273, 4.0/273, 7.0/273, 4.0/273, 1.0/273,
    4.0/273,16.0/273,26.0/273,16.0/273, 4.0/273,
    7.0/273,26.0/273,41.0/273,26.0/273, 7.0/273,
    4.0/273,16.0/273,26.0/273,16.0/273, 4.0/273,
    1.0/273, 4.0/273, 7.0/273, 4.0/273, 1.0/273,
};

[numthreads(8, 8, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    const uint2 pixel = dispatch_thread_id.xy;
    if (pixel.x >= g_denoise_extent.x || pixel.y >= g_denoise_extent.y)
    {
        return;
    }

    const float4 center       = g_current_accumulation.Load(int3(pixel, 0));
    const float4 center_albedo = g_aov_albedo.Load(int3(pixel, 0));
    const float4 center_nd    = g_aov_normal_depth.Load(int3(pixel, 0));
    const float3 center_normal = center_nd.xyz * 2.0f - 1.0f;
    const float  center_depth = center_nd.w;

    // Spatial 5x5 weighted by AOV features + small luminance tie-breaker.
    float3 weighted_sum = float3(0.0f, 0.0f, 0.0f);
    float  weight_sum   = 0.0f;
    const float center_lum = dot(center.rgb, float3(0.2126f, 0.7152f, 0.0722f));

    [unroll]
    for (int dy = -2; dy <= 2; ++dy)
    {
        [unroll]
        for (int dx = -2; dx <= 2; ++dx)
        {
            const int2 ofs       = int2(dx, dy);
            const int2 sample_p  = clamp(int2(pixel) + ofs, int2(0, 0),
                                          int2(g_denoise_extent) - 1);
            const float4 s        = g_current_accumulation.Load(int3(sample_p, 0));
            const float4 s_albedo = g_aov_albedo.Load(int3(sample_p, 0));
            const float4 s_nd     = g_aov_normal_depth.Load(int3(sample_p, 0));
            const float3 s_normal = s_nd.xyz * 2.0f - 1.0f;
            const float  s_depth = s_nd.w;

            // Range weights. Each is in [0, 1]; high value means the
            // neighbour is "similar" to the centre and should be weighted
            // higher in the bilateral kernel.
            const float s_lum = dot(s.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            const float dLum = (s_lum - center_lum) / max(center_lum + 1e-3f, 1e-3f);
            const float wLum = exp(-(dLum * dLum) / (kRangeSigmaLum * kRangeSigmaLum));

            const float3 dRGB = s_albedo.rgb - center_albedo.rgb;
            const float  dRGB_len = length(dRGB);
            const float wRGB = exp(-(dRGB_len * dRGB_len) / (kRangeSigmaRGB * kRangeSigmaRGB));

            // Normal range weight: cosine similarity (1 at parallel,
            // 0 at perpendicular). Stable for grazing angles.
            const float wN = pow(max(0.0f, dot(s_normal, center_normal)), 1.0f / max(kRangeSigmaN * kRangeSigmaN, 1e-4f));

            // Depth range weight: relative depth difference. Skips
            // across depth discontinuities (foreground / background edge).
            float wZ = 1.0f;
            if (center_depth > 1e-3f)
            {
                const float dZ = abs(s_depth - center_depth) / max(center_depth, 1e-3f);
                wZ = exp(-(dZ * dZ) / (kRangeSigmaZ * kRangeSigmaZ));
            }

            // Combined weight. The 1.0 - g_denoise_strength at the end is
            // the passthrough-floor (so a strength of 0 = identity, no
            // bilateral filtering at all).
            const float spatial_w = kSpatialWeight[(dy + 2) * 5 + (dx + 2)];
            const float w = spatial_w * wLum * wRGB * wN * wZ
                          * g_denoise_strength + (1.0f - g_denoise_strength);

            weighted_sum += w * s.rgb;
            weight_sum   += w;
        }
    }

    const float3 spatial = weighted_sum / max(weight_sum, 1e-8f);

    // Temporal blend. On the very first frame history is undefined; treat
    // as zero so we just pass the spatial result.
    const float3 history = (g_frame_index == 0u)
                               ? float3(0.0f, 0.0f, 0.0f)
                               : g_history_accumulation.Load(int3(pixel, 0)).rgb;
    const float history_alpha = saturate(g_denoise_strength * 0.95f);
    const float3 blended = (g_frame_index == 0u)
                               ? spatial
                               : lerp(spatial, history, history_alpha);

    g_denoised_output[pixel] = float4(blended, 1.0f);
}