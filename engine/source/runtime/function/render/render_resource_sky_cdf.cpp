#include "runtime/function/render/render_resource_sky_cdf.h"

#include "runtime/function/render/render_type.h"

#include <algorithm>
#include <cmath>

namespace Piccolo
{
    namespace
    {
        // Rec. 709 luminance weights -- matches the denoise shader and the
        // engine's existing PT luminance reads.
        constexpr float kLumR = 0.2126f;
        constexpr float kLumG = 0.7152f;
        constexpr float kLumB = 0.0722f;
    } // namespace

    void buildSkyCdfFromSpecularMaps(
        const std::array<std::shared_ptr<TextureData>, 6>& specular_maps,
        uint32_t bin_count,
        SkyCdfData& out)
    {
        out = SkyCdfData {};
        if (bin_count == 0)
        {
            return;
        }
        out.bin_count = bin_count;

        const uint32_t N         = bin_count;
        const uint32_t rows_count = 6u * N;
        out.marginal.assign(rows_count, 0.0f);
        out.row_cdf.assign(rows_count * N, 0.0f);

        // Per-face pixel aggregation: for each (face, row, col) bin, sum the
        // luminance of all pixels in the original cubemap that fall in that
        // bin. The aggregation is uniform in the (i, j) parameterisation,
        // so face-edge pixels (which subtend a larger solid angle) are
        // under-represented relative to face-centre pixels; this is the
        // documented small bias of the row-margin approximation.
        std::vector<float> bin_lum(static_cast<size_t>(rows_count) * N, 0.0f);

        for (uint32_t face = 0; face < 6u; ++face)
        {
            const auto& tex = specular_maps[face];
            if (tex == nullptr || tex->m_pixels == nullptr || tex->m_width == 0 || tex->m_height == 0)
            {
                continue;
            }
            // loadTextureHDR uses RGBA32F (4-channel float). Other formats
            // are not produced by the current asset pipeline for HDR skies;
            // if a future pipeline adds one, the cubemap will fall back to
            // uniform-sphere NEE (the GPU side detects a zero marginal and
            // synthesises a default).
            if (tex->m_format != RHI_FORMAT_R32G32B32A32_SFLOAT)
            {
                continue;
            }
            const float*   pixels = static_cast<const float*>(tex->m_pixels);
            const uint32_t W      = tex->m_width;
            const uint32_t H      = tex->m_height;
            for (uint32_t j = 0; j < H; ++j)
            {
                const uint32_t row = std::min(j * N / H, N - 1u);
                for (uint32_t i = 0; i < W; ++i)
                {
                    const uint32_t col = std::min(i * N / W, N - 1u);
                    const float*   p   = &pixels[(static_cast<size_t>(j) * W + i) * 4u];
                    const float   lum = kLumR * p[0] + kLumG * p[1] + kLumB * p[2];
                    if (lum > 0.0f)
                    {
                        bin_lum[(static_cast<size_t>(face) * N + row) * N + col] += lum;
                    }
                }
            }
        }

        // Row marginal + within-row CDF. For an empty row (zero luminance),
        // fall back to a uniform step so the GPU sample still picks a valid
        // col; the contribution Li will be near zero so the path is
        // effectively a no-op for variance.
        float total = 0.0f;
        for (uint32_t fr = 0; fr < rows_count; ++fr)
        {
            float row_sum = 0.0f;
            for (uint32_t c = 0; c < N; ++c)
            {
                row_sum += bin_lum[static_cast<size_t>(fr) * N + c];
            }
            out.marginal[fr] = row_sum;
            total += row_sum;

            if (row_sum > 0.0f)
            {
                float cum = 0.0f;
                for (uint32_t c = 0; c < N; ++c)
                {
                    cum += bin_lum[static_cast<size_t>(fr) * N + c];
                    out.row_cdf[static_cast<size_t>(fr) * N + c] = cum / row_sum;
                }
                // Force the last CDF entry to exactly 1.0 so float drift
                // during the GPU linear scan cannot fall off the end.
                out.row_cdf[static_cast<size_t>(fr) * N + (N - 1u)] = 1.0f;
            }
            else
            {
                for (uint32_t c = 0; c < N; ++c)
                {
                    out.row_cdf[static_cast<size_t>(fr) * N + c] =
                        static_cast<float>(c + 1u) / static_cast<float>(N);
                }
            }
        }
        out.total_lum = total;
    }
} // namespace Piccolo
