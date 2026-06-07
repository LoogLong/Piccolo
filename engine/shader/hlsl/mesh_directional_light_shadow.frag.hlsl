#include "common.hlsli"

float main(float4 position : SV_Position) : SV_Target0
{
    return position.z;
}
