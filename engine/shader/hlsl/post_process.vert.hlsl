#include "common.hlsli"

FullscreenVSOutput main(uint vertex_id : SV_VertexID)
{
    const float3 positions[3] =
    {
        float3(3.0f, 1.0f, 0.5f),
        float3(-1.0f, 1.0f, 0.5f),
        float3(-1.0f, -3.0f, 0.5f)
    };

    FullscreenVSOutput output;
    output.position = float4(positions[vertex_id], 1.0f);
    output.uv       = 0.5f * (positions[vertex_id].xy + float2(1.0f, 1.0f));
    return output;
}
