#include "common.hlsli"

struct ParticleBillboardFrameData
{
    row_major float4x4 proj_view_matrix;
    float3 right_direction;
    float right_padding;
    float3 up_direction;
    float up_padding;
    float3 forward_direction;
    float forward_padding;
};

struct Particle
{
    float3 pos;
    float life;
    float3 vel;
    float size_x;
    float3 acc;
    float size_y;
    float4 color;
};

struct ParticleBillboardVSOutput
{
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

ConstantBuffer<ParticleBillboardFrameData> g_particle_frame : register(b0, space0);
StructuredBuffer<Particle> g_particles : register(t1, space0);

ParticleBillboardVSOutput main(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    const float2 vertex_buffer[4] =
    {
        float2(-0.5f, 0.5f),
        float2(0.5f, 0.5f),
        float2(-0.5f, -0.5f),
        float2(0.5f, -0.5f)
    };

    const float2 uv_buffer[4] =
    {
        float2(0.0f, 1.0f),
        float2(1.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f)
    };

    Particle particle = g_particles[instance_id];
    float2 model_position = vertex_buffer[vertex_id & 3u];
    float project_vel_x = dot(particle.vel, g_particle_frame.right_direction);
    float project_vel_y = dot(particle.vel, g_particle_frame.up_direction);

    float3 world_position;
    if (abs(project_vel_x) < particle.size_x || abs(project_vel_y) < particle.size_y)
    {
        world_position = particle.size_x * g_particle_frame.right_direction * model_position.x +
                         particle.size_y * g_particle_frame.up_direction * model_position.y +
                         particle.pos;
    }
    else
    {
        float3 project_dir = normalize(project_vel_x * g_particle_frame.right_direction +
                                       project_vel_y * g_particle_frame.up_direction);
        float3 side_dir = normalize(cross(g_particle_frame.forward_direction, project_dir));
        world_position = particle.size_x * side_dir * model_position.x +
                         particle.size_y * project_dir * model_position.y +
                         particle.pos;
    }

    ParticleBillboardVSOutput output;
    output.position = mul(g_particle_frame.proj_view_matrix, float4(world_position, 1.0f));
    output.color = particle.color;
    output.uv = uv_buffer[vertex_id & 3u];
    return output;
}
