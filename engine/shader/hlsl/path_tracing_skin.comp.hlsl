#include "common.hlsli"
#include "path_tracing_common.hlsli"

// Input: rest-pose vertex data from mesh GPU buffers
StructuredBuffer<float3>                      g_rest_positions      : register(t0, space0);
StructuredBuffer<MeshVertexJointBindingData>  g_joint_bindings      : register(t1, space0);
// Interleaved normals and tangents: each vertex occupies 2 consecutive float3 elements
// Element [vertex_id * 2 + 0] = normal, [vertex_id * 2 + 1] = tangent
StructuredBuffer<float3>                      g_rest_normal_tangent : register(t2, space0);
StructuredBuffer<float2>                      g_rest_texcoords      : register(t3, space0);

// Input: per-instance joint matrices
StructuredBuffer<JointMatrixData>             g_joint_matrices      : register(t4, space0);

// Constants
struct SkinComputeConstants
{
    uint vertex_count;
    uint joint_matrix_offset;  // index into g_joint_matrices for this instance's first matrix
    uint output_vertex_offset; // offset into output buffers
    uint _padding;
};
ConstantBuffer<SkinComputeConstants> g_constants : register(b0, space0);

// Output
RWStructuredBuffer<float3>                 g_skinned_positions : register(u0, space0);
RWStructuredBuffer<PathTracingVertexData>  g_skinned_vertices  : register(u1, space0);

[numthreads(64, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint vertex_id = dispatch_id.x;
    if (vertex_id >= g_constants.vertex_count)
    {
        return;
    }

    // Load rest-pose data
    float3 rest_position = g_rest_positions[vertex_id];
    // Normals and tangents are interleaved in the same buffer (stride=2)
    float3 rest_normal   = g_rest_normal_tangent[vertex_id * 2 + 0];
    float3 rest_tangent  = g_rest_normal_tangent[vertex_id * 2 + 1];
    float2 texcoord      = g_rest_texcoords[vertex_id];

    // Compute skinning matrix (same algorithm as mesh.vert.hlsl:LoadSkinningMatrix)
    MeshVertexJointBindingData binding = g_joint_bindings[vertex_id];
    float4x4 skinning_matrix = (float4x4)0.0f;

    // Weighted accumulation of up to 4 joint matrices
    // NOTE: indices > 0 check mirrors mesh.vert.hlsl (index 0 = invalid joint)
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

    // Apply skinning
    float3 skinned_position = mul(skinning_matrix, float4(rest_position, 1.0f)).xyz;
    float3 skinned_normal   = normalize(mul((float3x3)skinning_matrix, rest_normal));
    float3 skinned_tangent  = normalize(mul((float3x3)skinning_matrix, rest_tangent));

    // Write outputs
    uint out_idx = g_constants.output_vertex_offset + vertex_id;
    g_skinned_positions[out_idx] = skinned_position;

    PathTracingVertexData v;
    v.position = float4(skinned_position, 1.0f);
    v.normal   = float4(skinned_normal, 0.0f);
    v.tangent  = float4(skinned_tangent, 0.0f);
    v.texcoord = float4(texcoord, 0.0f, 0.0f);
    g_skinned_vertices[out_idx] = v;
}
