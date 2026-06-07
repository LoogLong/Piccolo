#include "common.hlsli"

uint main(nointerpolation uint node_id : TEXCOORD0) : SV_Target0
{
    return node_id;
}
