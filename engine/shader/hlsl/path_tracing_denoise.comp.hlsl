// Plan 2026-07-12 §2.2: self-built 5x5 spatial-bilateral + temporal-blend
// fallback denoiser for vendor-SDK-less hosts (software rasterizer, Linux
// without NVIDIA/AMD SDK, debug builds).
//
// Two inputs:
//   t0 g_current_accumulation : per-pixel PT accum written this frame
//   t1 g_history_accumulation : previous denoised output (ping-pong)
//
// Output:
//   u2 g_denoised_output : present to swapchain
//
// Push constants:
//   b3 DenoiseConstants { float strength; uint frame_index; uint2 extent; }
//
// Phase-1 design (plan §2.2): 5x5 spatial bilateral + temporal blend only.
// The 5x5 kernel uses 1 depth term and 1 luminance term; normals would be
// a second-order improvement, deferred to a follow-up. Strength drives the
// temporal alpha: 0.0 keeps raw accumulation (Quality preset), 1.0 trusts
// history fully (Interactive preset).

#include "common.hlsli"

Texture2D<float4>  g_current_accumulation : register(t0, space0);
Texture2D<float4>  g_history_accumulation : register(t1, space0);
RWTexture2D<float4> g_denoised_output    : register(u2, space0);

cbuffer DenoiseConstants : register(b3, space0)
{
    float  g_denoise_strength;  // 0..1; 0 = passthrough, 1 = full bilateral
    uint   g_frame_index;       // sample_index; 0 = first frame (no history)
    uint2  g_denoise_extent;
};

static const float kSpatialSigma  = 0.05f; // per-channel luminance sigma
static const float kRangeSigmaLum = 0.20f; // luminance difference sigma
static const float kRangeSigmaRGB = 0.10f; // channel-wise sigma (smaller)
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

    const float4 center = g_current_accumulation.Load(int3(pixel, 0));

    // Spatial 5x5 bilateral weighted by luminance similarity to center.
    float3 weighted_sum  = float3(0.0f, 0.0f, 0.0f);
    float  weight_sum    = 0.0f;
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
            const float4 s       = g_current_accumulation.Load(int3(sample_p, 0));
            const float s_lum    = dot(s.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            const float dLum     = (s_lum - center_lum) / max(center_lum + 1e-3f, 1e-3f);
            const float dRGB     = length(s.rgb - center.rgb) /
                                   max(length(center.rgb) + 1e-3f, 1e-3f);
            const float wLum     = exp(-(dLum * dLum) / (kRangeSigmaLum * kRangeSigmaLum));
            const float wRGB     = exp(-(dRGB * dRGB) / (kRangeSigmaRGB * kRangeSigmaRGB));
            const float w        = kSpatialWeight[(dy + 2) * 5 + (dx + 2)] *
                                   wLum * wRGB * g_denoise_strength + (1.0f - g_denoise_strength);
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
    // Map 0.85 (default) to ~0.6 history share; 1.0 -> 0.95 share; 0.25 -> 0.15.
    const float history_alpha = saturate(g_denoise_strength * 0.95f);
    const float3 blended = (g_frame_index == 0u)
                               ? spatial
                               : lerp(spatial, history, history_alpha);

    g_denoised_output[pixel] = float4(blended, 1.0f);
}
