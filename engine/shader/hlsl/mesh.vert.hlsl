#include "common.hlsli"

ConstantBuffer<PerFrameData> g_per_frame : register(b0, space0);

ConstantBuffer<MeshPerDrawcallData>            g_mesh_per_drawcall : register(b1, space0);
StructuredBuffer<JointMatrixData>              g_joint_matrices : register(t2, space0);
StructuredBuffer<MeshVertexJointBindingData>   g_joint_bindings : register(t0, space1);

struct MeshVSInput
{
    float3 position    : POSITION;
    float3 normal      : NORMAL;
    float3 tangent     : TANGENT;
    float2 texcoord    : TEXCOORD0;
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
        skinning_matrix += g_joint_matrices[joint_base + uint(binding.indices.x)].matrix * binding.weights.x;
    }
    if (binding.weights.y > 0.0f && binding.indices.y > 0)
    {
        skinning_matrix += g_joint_matrices[joint_base + uint(binding.indices.y)].matrix * binding.weights.y;
    }
    if (binding.weights.z > 0.0f && binding.indices.z > 0)
    {
        skinning_matrix += g_joint_matrices[joint_base + uint(binding.indices.z)].matrix * binding.weights.z;
    }
    if (binding.weights.w > 0.0f && binding.indices.w > 0)
    {
        skinning_matrix += g_joint_matrices[joint_base + uint(binding.indices.w)].matrix * binding.weights.w;
    }

    return skinning_matrix;
}

MeshVSOutput main(MeshVSInput input)
{
    MeshInstanceData instance_data = g_mesh_per_drawcall.mesh_instances[input.instance_id];

    float3 model_position = input.position;
    float3 model_normal   = input.normal;
    float3 model_tangent  = input.tangent;

    if (instance_data.enable_vertex_blending > 0.0f)
    {
        float4x4 skinning_matrix = LoadSkinningMatrix(input.vertex_id, input.instance_id);
        model_position = mul(skinning_matrix, float4(input.position, 1.0f)).xyz;
        model_normal   = normalize(mul((float3x3)skinning_matrix, input.normal));
        model_tangent  = normalize(mul((float3x3)skinning_matrix, input.tangent));
    }

    float4 world_position = mul(instance_data.model_matrix, float4(model_position, 1.0f));

    MeshVSOutput output;
    output.position       = mul(g_per_frame.proj_view_matrix, world_position);
    output.world_position = world_position.xyz;
    output.normal         = normalize(mul((float3x3)instance_data.model_matrix, model_normal));
    output.tangent        = normalize(mul((float3x3)instance_data.model_matrix, model_tangent));
    output.texcoord       = input.texcoord;
    return output;
}
