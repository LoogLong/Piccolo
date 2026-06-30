#include "common.hlsli"

struct DebugDrawPerFrameData
{
    row_major float4x4 proj_view_matrix;
};

struct DebugDrawData
{
    row_major float4x4 model;
    float4             color;
};

ConstantBuffer<DebugDrawPerFrameData> g_debug_per_frame : register(b0, space0);
ConstantBuffer<DebugDrawData>         g_debug_draw : register(b1, space0);

struct DebugDrawVSInput
{
    float3 position : POSITION;
    float4 color    : NORMAL;
    float2 texcoord : TANGENT;
};

struct DebugDrawVSOutput
{
    float4 position : SV_Position;
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;
};

DebugDrawVSOutput main(DebugDrawVSInput input)
{
    float4 position = float4(input.position, 1.0f);
    if (input.texcoord.x < 0.0f)
    {
        position = mul(g_debug_per_frame.proj_view_matrix, mul(g_debug_draw.model, position));
    }

    DebugDrawVSOutput output;
    output.position = position;
    output.color    = g_debug_draw.color.a > 0.000001f ? g_debug_draw.color : input.color;
    output.texcoord = input.texcoord;
    return output;
}
