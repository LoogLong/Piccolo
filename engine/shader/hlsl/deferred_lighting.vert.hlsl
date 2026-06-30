#include "common.hlsli"

FullscreenVSOutput main(uint vertex_id : SV_VertexID)
{
    const float3 positions[3] =
    {
        float3(3.0f, 1.0f, 0.5f),
        float3(-1.0f, 1.0f, 0.5f),
        float3(-1.0f, -3.0f, 0.5f)
    };

    const float2 uvs[3] =
    {
        float2(2.0f, 1.0f),
        float2(0.0f, 1.0f),
        float2(0.0f, -1.0f)
    };

    FullscreenVSOutput output;
    output.position = float4(positions[vertex_id], 1.0f);
    output.uv       = uvs[vertex_id];
    return output;
}
