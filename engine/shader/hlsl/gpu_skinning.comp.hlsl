#include "common.hlsli"
#include "gpu_skinning.hlsli"

// Mesh vertex buffers are tightly packed on CPU/GPU (12B position, 24B normal+tangent, 8B texcoord).
// StructuredBuffer<float3> uses 16B element stride and misreads packed data for vertex_id > 0.
// Reinterpret as uint words and load by explicit byte offsets.
static const uint kRestPositionStrideBytes = 12u;
static const uint kRestNormalTangentStrideBytes = 24u;
static const uint kRestTexcoordStrideBytes = 8u;

float3 LoadFloat3Packed(StructuredBuffer<uint> packed, uint byte_offset)
{
    uint base = byte_offset / 4u;
    return float3(asfloat(packed[base + 0u]), asfloat(packed[base + 1u]), asfloat(packed[base + 2u]));
}

float2 LoadFloat2Packed(StructuredBuffer<uint> packed, uint byte_offset)
{
    uint base = byte_offset / 4u;
    return float2(asfloat(packed[base + 0u]), asfloat(packed[base + 1u]));
}

// Set 0 — mesh static (space0)
StructuredBuffer<uint>                        g_rest_positions_packed      : register(t0, space0);
StructuredBuffer<MeshVertexJointBindingData>  g_joint_bindings             : register(t1, space0);
StructuredBuffer<uint>                        g_rest_normal_tangent_packed : register(t2, space0);
StructuredBuffer<uint>                        g_rest_texcoords_packed      : register(t3, space0);

// Set 1 — frame shared (space1)
StructuredBuffer<JointMatrixData>          g_joint_matrices      : register(t0, space1);
ConstantBuffer<SkinComputeConstants>       g_constants           : register(b1, space1);
RWStructuredBuffer<SkinnedVertexData>      g_skinned_vertices    : register(u2, space1);

// Set 2 — instance dynamic (space2)
// Note: RWStructuredBuffer<float3> uses 16-byte stride; BLAS must match kGpuSkinnedPositionStorageStrideBytes.
RWStructuredBuffer<float3>                 g_skinned_positions   : register(u0, space2);

[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint vertex_id = dispatch_id.x;
    if (vertex_id >= g_constants.vertex_count)
    {
        return;
    }

    float3 rest_position = LoadFloat3Packed(g_rest_positions_packed, vertex_id * kRestPositionStrideBytes);
    float3 rest_normal =
        LoadFloat3Packed(g_rest_normal_tangent_packed, vertex_id * kRestNormalTangentStrideBytes);
    float3 rest_tangent = LoadFloat3Packed(g_rest_normal_tangent_packed,
                                           vertex_id * kRestNormalTangentStrideBytes + 12u);
    float2 texcoord = LoadFloat2Packed(g_rest_texcoords_packed, vertex_id * kRestTexcoordStrideBytes);

    MeshVertexJointBindingData binding = g_joint_bindings[vertex_id];
    float4x4 skinning_matrix = (float4x4)0.0f;

    if (binding.weights.x > 0.0f && binding.indices.x > 0)
    {
        uint joint_idx = g_constants.joint_matrix_offset + uint(binding.indices.x);
        skinning_matrix += g_joint_matrices[joint_idx].joint_matrix * binding.weights.x;
    }
    if (binding.weights.y > 0.0f && binding.indices.y > 0)
    {
        uint joint_idx = g_constants.joint_matrix_offset + uint(binding.indices.y);
        skinning_matrix += g_joint_matrices[joint_idx].joint_matrix * binding.weights.y;
    }
    if (binding.weights.z > 0.0f && binding.indices.z > 0)
    {
        uint joint_idx = g_constants.joint_matrix_offset + uint(binding.indices.z);
        skinning_matrix += g_joint_matrices[joint_idx].joint_matrix * binding.weights.z;
    }
    if (binding.weights.w > 0.0f && binding.indices.w > 0)
    {
        uint joint_idx = g_constants.joint_matrix_offset + uint(binding.indices.w);
        skinning_matrix += g_joint_matrices[joint_idx].joint_matrix * binding.weights.w;
    }

    float3 skinned_position = mul(skinning_matrix, float4(rest_position, 1.0f)).xyz;
    float3 skinned_normal   = normalize(mul((float3x3)skinning_matrix, rest_normal));
    float3 skinned_tangent  = normalize(mul((float3x3)skinning_matrix, rest_tangent));

    g_skinned_positions[vertex_id] = skinned_position;

    uint out_idx = g_constants.output_vertex_offset + vertex_id;
    SkinnedVertexData v;
    v.position = float4(skinned_position, 1.0f);
    v.normal   = float4(skinned_normal, 0.0f);
    v.tangent  = float4(skinned_tangent, 0.0f);
    v.texcoord = float4(texcoord, 0.0f, 0.0f);
    g_skinned_vertices[out_idx] = v;
}
