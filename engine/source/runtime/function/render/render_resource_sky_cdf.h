#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Piccolo
{
    class TextureData;

    // =============================================================================
    // Sky row-margin CDF for path-tracer sky NEE importance sampling.
    // (Docs/plans/2026-07-15-path-tracing-accuracy-v4.md §3 Task 5.1 / Phase 5
    // A1.)
    //
    // Computed on the CPU at IBL creation time from the same HDR pixels
    // uploaded to g_specular_texture (mip 0, the pre-blurred mip chain
    // generated on the GPU supplies the LODs sampled by the path tracer).
    //
    // Two textures are uploaded (both R32_SFLOAT 2D images, used as 1D /
    // (6*N x N) respectively on the GPU):
    //
    //   _sky_row_marginal_image  -- shape (6*N, 1)
    //     Per-(face,row) probability. Sampling step 1: pick (face, row) from
    //     the cumulative marginal via linear scan.
    //
    //   _sky_row_cdf_image       -- shape (6*N, N)
    //     Within-row CDF. Sampling step 2: pick the column in the row by
    //     inverting the row's CDF via linear scan.
    //
    // The combined pdf returned from path_tracing_light.hlsli's
    // SampleLight(PT_LIGHT_SKY, ...) is the product of the two densities
    // (so the integral over the sphere is 1, as required for an unbiased
    // estimator). A small systematic bias is introduced by ignoring the
    // cubemap face-area Jacobian (the per-bin solid angle varies with
    // position on a face), but the bias is bounded by the relative area
    // variation across a single face which is < 20% even at the
    // face-corners of an N=32 grid; we accept this for the first cut in
    // exchange for skipping a per-bin precomputed Jacobian texture.
    // =============================================================================

    struct SkyCdfData
    {
        uint32_t bin_count {0};
        std::vector<float> marginal;  // size = 6 * N
        std::vector<float> row_cdf;   // size = 6 * N * N
        float total_lum {0.0f};
    };

    // Build a sky row-margin CDF from the 6-face HDR specular cubemap pixels
    // already on the CPU (as loaded by loadTextureHDR, RGBA32F, 4 channels).
    //
    // bin_count: number of row/col bins per face. Typical 32 or 64. The
    //            CPU-side work is O(W * H * 6) per cubemap; GPU storage is
    //            O(6 * N^2) floats -- both are cheap for any reasonable N.
    // out:       populated with marginal, row_cdf, bin_count, total_lum.
    //
    // Computes luminance per pixel (Rec. 709 weights matching the denoise
    // shader and the engine's existing PT luminance reads), aggregates into
    // an N x N grid per face, then normalises each row into a CDF and
    // aggregates rows into a marginal.
    void buildSkyCdfFromSpecularMaps(
        const std::array<std::shared_ptr<TextureData>, 6>& specular_maps,
        uint32_t bin_count,
        SkyCdfData& out);

} // namespace Piccolo
