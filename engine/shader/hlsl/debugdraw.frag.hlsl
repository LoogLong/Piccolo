Texture2D<float4> g_debug_texture : register(t2, space0);
SamplerState      g_debug_sampler : register(s2, space0);

struct DebugDrawVSOutput
{
    float4 position : SV_Position;
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;
};

float4 main(DebugDrawVSOutput input) : SV_Target0
{
    if (input.texcoord.x >= 0.0f && input.texcoord.y >= 0.0f)
    {
        float xi = g_debug_texture.Sample(g_debug_sampler, input.texcoord).r;
        return input.color * xi;
    }

    return input.color;
}
