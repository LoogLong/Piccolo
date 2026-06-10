#include "common.hlsli"

Texture2D<float4> g_input_color : register(t0, space0);
SamplerState      g_input_sampler : register(s0, space0);

#define UP_LEFT      0
#define UP           1
#define UP_RIGHT     2
#define LEFT         3
#define CENTER       4
#define RIGHT        5
#define DOWN_LEFT    6
#define DOWN         7
#define DOWN_RIGHT   8

#define STEP_COUNT_MAX   12
#define EDGE_THRESHOLD_MIN  0.0312f
#define EDGE_THRESHOLD_MAX  0.125f
#define SUBPIXEL_QUALITY    0.75f
#define GRADIENT_SCALE      0.25f

float Luma(float3 color)
{
    return dot(color, float3(0.299f, 0.578f, 0.114f));
}

float Quality(int i)
{
    if (i < 5)
    {
        return 1.0f;
    }
    if (i == 5)
    {
        return 1.5f;
    }
    if (i < 10)
    {
        return 2.0f;
    }
    if (i == 10)
    {
        return 4.0f;
    }
    return 8.0f;
}

float4 main(FullscreenVSOutput input) : SV_Target0
{
    uint width = 0;
    uint height = 0;
    g_input_color.GetDimensions(width, height);

    float2 uv_step = rcp(max(float2(float(width), float(height)), float2(1.0f, 1.0f)));
    float2 kernel_step_mat[9] = {
        float2(-1.0f, 1.0f), float2(0.0f, 1.0f), float2(1.0f, 1.0f),
        float2(-1.0f, 0.0f), float2(0.0f, 0.0f), float2(1.0f, 0.0f),
        float2(-1.0f, -1.0f), float2(0.0f, -1.0f), float2(1.0f, -1.0f)
    };

    float luma_mat[9];
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        luma_mat[i] = Luma(g_input_color.Sample(g_input_sampler, input.uv + uv_step * kernel_step_mat[i]).rgb);
    }

    float luma_min = min(luma_mat[CENTER],
                         min(min(luma_mat[UP], luma_mat[DOWN]), min(luma_mat[LEFT], luma_mat[RIGHT])));
    float luma_max = max(luma_mat[CENTER],
                         max(max(luma_mat[UP], luma_mat[DOWN]), max(luma_mat[LEFT], luma_mat[RIGHT])));
    float luma_range = luma_max - luma_min;

    if (luma_range < max(EDGE_THRESHOLD_MIN, luma_max * EDGE_THRESHOLD_MAX))
    {
        return g_input_color.Sample(g_input_sampler, input.uv);
    }

    float luma_horizontal =
        abs(luma_mat[UP_LEFT] + luma_mat[DOWN_LEFT] - 2.0f * luma_mat[LEFT]) +
        2.0f * abs(luma_mat[UP] + luma_mat[DOWN] - 2.0f * luma_mat[CENTER]) +
        abs(luma_mat[UP_RIGHT] + luma_mat[DOWN_RIGHT] - 2.0f * luma_mat[RIGHT]);
    float luma_vertical =
        abs(luma_mat[UP_LEFT] + luma_mat[UP_RIGHT] - 2.0f * luma_mat[UP]) +
        2.0f * abs(luma_mat[LEFT] + luma_mat[RIGHT] - 2.0f * luma_mat[CENTER]) +
        abs(luma_mat[DOWN_LEFT] + luma_mat[DOWN_RIGHT] - 2.0f * luma_mat[DOWN]);
    bool is_horizontal = luma_horizontal > luma_vertical;

    float gradient_down_left = (is_horizontal ? luma_mat[DOWN] : luma_mat[LEFT]) - luma_mat[CENTER];
    float gradient_up_right = (is_horizontal ? luma_mat[UP] : luma_mat[RIGHT]) - luma_mat[CENTER];
    bool is_down_left = abs(gradient_down_left) > abs(gradient_up_right);

    float2 step_tangent = (is_horizontal ? float2(1.0f, 0.0f) : float2(0.0f, 1.0f)) * uv_step;
    float2 step_normal =
        (is_down_left ? -1.0f : 1.0f) *
        (is_horizontal ? float2(0.0f, 1.0f) : float2(1.0f, 0.0f)) *
        uv_step;

    float gradient = is_down_left ? gradient_down_left : gradient_up_right;

    float2 uv_start = input.uv + 0.5f * step_normal;
    float luma_average_start = luma_mat[CENTER] + 0.5f * gradient;

    float2 uv_pos = uv_start + step_tangent;
    float2 uv_neg = uv_start - step_tangent;

    float delta_luma_pos = Luma(g_input_color.Sample(g_input_sampler, uv_pos).rgb) - luma_average_start;
    float delta_luma_neg = Luma(g_input_color.Sample(g_input_sampler, uv_neg).rgb) - luma_average_start;

    bool reached_pos = abs(delta_luma_pos) > GRADIENT_SCALE * abs(gradient);
    bool reached_neg = abs(delta_luma_neg) > GRADIENT_SCALE * abs(gradient);
    bool reached_both = reached_pos && reached_neg;

    if (!reached_pos)
    {
        uv_pos += step_tangent;
    }
    if (!reached_neg)
    {
        uv_neg -= step_tangent;
    }

    if (!reached_both)
    {
        [loop]
        for (int i = 2; i < STEP_COUNT_MAX; ++i)
        {
            if (!reached_pos)
            {
                delta_luma_pos = Luma(g_input_color.Sample(g_input_sampler, uv_pos).rgb) - luma_average_start;
            }
            if (!reached_neg)
            {
                delta_luma_neg = Luma(g_input_color.Sample(g_input_sampler, uv_neg).rgb) - luma_average_start;
            }

            reached_pos = abs(delta_luma_pos) > GRADIENT_SCALE * abs(gradient);
            reached_neg = abs(delta_luma_neg) > GRADIENT_SCALE * abs(gradient);
            reached_both = reached_pos && reached_neg;

            if (!reached_pos)
            {
                uv_pos += Quality(i) * step_tangent;
            }
            if (!reached_neg)
            {
                uv_neg -= Quality(i) * step_tangent;
            }

            if (reached_both)
            {
                break;
            }
        }
    }

    float2 delta_pos = abs(uv_pos - uv_start);
    float2 delta_neg = abs(uv_neg - uv_start);
    float length_pos = max(delta_pos.x, delta_pos.y);
    float length_neg = max(delta_neg.x, delta_neg.y);
    bool is_pos_near = length_pos < length_neg;

    float pixel_offset = -1.0f * (is_pos_near ? length_pos : length_neg) / (length_pos + length_neg) + 0.5f;

    if (((is_pos_near ? delta_luma_pos : delta_luma_neg) < 0.0f) ==
        (luma_mat[CENTER] < luma_average_start))
    {
        pixel_offset = 0.0f;
    }

    float average_weight_mat[9] = {
        1.0f, 2.0f, 1.0f,
        2.0f, 0.0f, 2.0f,
        1.0f, 2.0f, 1.0f
    };
    float luma_average_center = 0.0f;
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        luma_average_center += average_weight_mat[i] * luma_mat[i];
    }
    luma_average_center /= 12.0f;

    float subpixel_luma_range = saturate(abs(luma_average_center - luma_mat[CENTER]) / luma_range);
    float subpixel_offset = (-2.0f * subpixel_luma_range + 3.0f) * subpixel_luma_range * subpixel_luma_range;
    subpixel_offset = subpixel_offset * subpixel_offset * SUBPIXEL_QUALITY;

    pixel_offset = max(pixel_offset, subpixel_offset);

    return g_input_color.Sample(g_input_sampler, input.uv + pixel_offset * step_normal);
}
