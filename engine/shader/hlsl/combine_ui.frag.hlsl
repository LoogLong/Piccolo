#include "common.hlsli"

Texture2D<float4> g_scene_color : register(t0, space0);
Texture2D<float4> g_ui_color : register(t1, space0);

float4 main(FullscreenVSOutput input) : SV_Target0
{
    int2 pixel_coord = int2(input.position.xy);
    float4 scene_color = g_scene_color.Load(int3(pixel_coord, 0));
    float4 ui_color    = g_ui_color.Load(int3(pixel_coord, 0));
    float4 gamma_ui    = float4(ApplyGamma(ui_color.rgb), pow(saturate(ui_color.a), 1.0f / 2.2f));

    if (ui_color.r < 1e-6f && ui_color.g < 1e-6f && ui_color.a < 1e-6f)
    {
        return scene_color;
    }

    return gamma_ui;
}
