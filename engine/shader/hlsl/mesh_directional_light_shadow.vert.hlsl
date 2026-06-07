#include "common.hlsli"

cbuffer DirectionalShadowPerFrame : register(b0, space0)
{
    row_major float4x4 g_light_proj_view;
};

ConstantBuffer<MeshPerDrawcallData>          g_mesh_per_drawcall : register(b1, space0);
StructuredBuffer<JointMatrixData>            g_joint_matrices : register(t2, space0);
StructuredBuffer<MeshVertexJointBindingData> g_joint_bindings : register(t0, space1);

struct ShadowVSInput
{
    float3 position    : POSITION;
    uint   vertex_id   : SV_VertexID;
    uint   instance_id : SV_InstanceID;
};

float4x4 LoadSkinningMatrix(uint vertex_id, uint instance_id)
{
    MeshVertexJointBindingData binding = g_joint_bindings[vertex_id];
    uint joint_base = M_MESH_VERTEX_BLENDING_MAX_JOINT_COUNT * instance_id;

    float4x4 skinning_matrix = (float4x4)0.0f;

    if (binding.weights.x > 0.0f && binding.indices.x > 0)
    {
        skinning_matrix += g_joint_matrices[joint_base + uint(binding.indices.x)].joint_matrix * binding.weights.x;
    }
    if (binding.weights.y > 0.0f && binding.indices.y > 0)
    {
        skinning_matrix += g_joint_matrices[joint_base + uint(binding.indices.y)].joint_matrix * binding.weights.y;
    }
    if (binding.weights.z > 0.0f && binding.indices.z > 0)
    {
        skinning_matrix += g_joint_matrices[joint_base + uint(binding.indices.z)].joint_matrix * binding.weights.z;
    }
    if (binding.weights.w > 0.0f && binding.indices.w > 0)
    {
        skinning_matrix += g_joint_matrices[joint_base + uint(binding.indices.w)].joint_matrix * binding.weights.w;
    }

    return skinning_matrix;
}

float4 main(ShadowVSInput input) : SV_Position
{
    MeshInstanceData instance_data = g_mesh_per_drawcall.mesh_instances[input.instance_id];

    float3 model_position = input.position;
    if (instance_data.enable_vertex_blending > 0.0f)
    {
        model_position = mul(LoadSkinningMatrix(input.vertex_id, input.instance_id), float4(input.position, 1.0f)).xyz;
    }

    float4 world_position = mul(instance_data.model_matrix, float4(model_position, 1.0f));
    return mul(g_light_proj_view, world_position);
}
