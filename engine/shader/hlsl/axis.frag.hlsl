struct AxisVSOutput
{
    float4 position : SV_Position;
    float3 color    : COLOR0;
};

float4 main(AxisVSOutput input) : SV_Target0
{
    return float4(input.color, 1.0f);
}
