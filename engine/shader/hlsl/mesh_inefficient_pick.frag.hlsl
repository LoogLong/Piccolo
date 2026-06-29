#include "common.hlsli"

struct PickPSInput
{
    float4 position : SV_Position;
    nointerpolation uint node_id : TEXCOORD0;
};

uint main(PickPSInput input) : SV_Target0
{
    return input.node_id;
}
