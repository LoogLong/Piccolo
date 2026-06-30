#include "common.hlsli"

ConstantBuffer<PerFrameData> g_per_frame : register(b0, space0);

struct AxisData
{
    row_major float4x4 model_matrix;
    uint               selected_axis;
};

StructuredBuffer<AxisData> g_axis : register(t1, space0);

struct AxisVSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float3 tangent  : TANGENT;
    float2 texcoord : TEXCOORD0;
};

struct AxisVSOutput
{
    float4 position : SV_Position;
    float3 color    : COLOR0;
};

float3 AxisColor(float selector, uint selected_axis)
{
    if (selector < 0.01f)
    {
        return selected_axis == 0u ? float3(1.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    }
    if (selector < 1.01f)
    {
        return selected_axis == 1u ? float3(1.0f, 1.0f, 0.0f) : float3(0.0f, 1.0f, 0.0f);
    }
    if (selector < 2.01f)
    {
        return selected_axis == 2u ? float3(1.0f, 1.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f);
    }
    return float3(1.0f, 1.0f, 1.0f);
}

AxisVSOutput main(AxisVSInput input)
{
    AxisData axis_data = g_axis[0];
    float3 world_position = mul(axis_data.model_matrix, float4(input.position, 1.0f)).xyz;
    float4 clip_position  = mul(g_per_frame.proj_view_matrix, float4(world_position, 1.0f));
    clip_position.z       = clip_position.z * 0.0001f;

    AxisVSOutput output;
    output.position = clip_position;
    output.color    = AxisColor(input.texcoord.x, axis_data.selected_axis);
    return output;
}
