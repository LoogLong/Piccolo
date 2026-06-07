#include "common.hlsli"

ConstantBuffer<MeshPerDrawcallData>          g_mesh_per_drawcall : register(b1, space0);
StructuredBuffer<JointMatrixData>            g_joint_matrices : register(t2, space0);
StructuredBuffer<MeshVertexJointBindingData> g_joint_bindings : register(t0, space1);

struct PointShadowVSInput
{
    float3 position    : POSITION;
    uint   vertex_id   : SV_VertexID;
    uint   instance_id : SV_InstanceID;
};

struct PointShadowVSOutput
{
    float3 world_position : TEXCOORD0;
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

PointShadowVSOutput main(PointShadowVSInput input)
{
    MeshInstanceData instance_data = g_mesh_per_drawcall.mesh_instances[input.instance_id];

    float3 model_position = input.position;
    if (instance_data.enable_vertex_blending > 0.0f)
    {
        model_position = mul(LoadSkinningMatrix(input.vertex_id, input.instance_id), float4(input.position, 1.0f)).xyz;
    }

    PointShadowVSOutput output;
    output.world_position = mul(instance_data.model_matrix, float4(model_position, 1.0f)).xyz;
    return output;
}
