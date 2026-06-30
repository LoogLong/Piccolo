#include "common.hlsli"

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

struct ParticleCountBuffer
{
    int dead_count;
    int alive_count;
    int alive_count_after_sim;
    int emit_count;
};

struct ParticleArgumentBuffer
{
    uint4 emit_count;
    uint4 simulate_count;
    int alive_flap_bit;
    int3 padding;
};

struct ParticleDispatchData
{
    float emit_delta;
    int xemit_count;
    float max_life;
    float fixed_time_step;
    float random0;
    float random1;
    float random2;
    uint frame_index;
    float4 gravity;
    uint4 viewport;
    float4 extent;
};

struct ParticleSimulationFrameData
{
    row_major float4x4 view_matrix;
    row_major float4x4 proj_view_matrix;
    row_major float4x4 proj_inv_matrix;
};

ConstantBuffer<ParticleDispatchData> g_particle_dispatch : register(b0, space0);
RWStructuredBuffer<Particle> g_particles : register(u1, space0);
RWStructuredBuffer<ParticleCountBuffer> g_counter : register(u2, space0);
RWStructuredBuffer<ParticleArgumentBuffer> g_argument : register(u3, space0);
RWStructuredBuffer<int4> g_alive_list : register(u4, space0);
RWStructuredBuffer<int4> g_dead_buffer : register(u5, space0);
RWStructuredBuffer<int4> g_alive_list_next : register(u6, space0);
ConstantBuffer<ParticleSimulationFrameData> g_simulation_frame : register(b8, space0);
RWStructuredBuffer<Particle> g_render_particles : register(u9, space0);
RWTexture2D<float4> g_scene_normal : register(u0, space1);
Texture2D<float> g_scene_depth : register(t1, space1);
SamplerState g_scene_sampler : register(s1, space1);

[numthreads(256, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint thread_id = dispatch_thread_id.x;
    if (thread_id >= (uint)max(g_counter[0].alive_count, 0))
    {
        return;
    }

    int particle_id = g_argument[0].alive_flap_bit == 0 ? g_alive_list[thread_id].x : g_alive_list_next[thread_id].x;
    Particle particle = g_particles[particle_id];
    float dt = g_particle_dispatch.fixed_time_step;

    if (particle.life > 0.0f)
    {
        particle.vel += particle.acc * dt;
        particle.pos += particle.vel * dt;

        float4 clip_position = mul(g_simulation_frame.proj_view_matrix, float4(particle.pos, 1.0f));
        float3 ndc_position = clip_position.xyz / max(clip_position.w, 0.0001f);

        if (ndc_position.x > -1.0f && ndc_position.x < 1.0f && ndc_position.y > -1.0f && ndc_position.y < 1.0f)
        {
            float2 uv = ndc_position.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
            float depth = g_scene_depth.SampleLevel(g_scene_sampler, uv, 0.0f);

            float4 view_particle = mul(g_simulation_frame.view_matrix, float4(particle.pos, 1.0f));
            float4 view_depth = mul(g_simulation_frame.proj_inv_matrix, float4(ndc_position.xy, depth, 1.0f));
            view_depth.xyz /= max(view_depth.w, 0.0001f);

            float collider_thickness = 0.5f;
            if (view_particle.z < view_depth.z && view_particle.z + collider_thickness > view_depth.z)
            {
                uint normal_width = 0;
                uint normal_height = 0;
                g_scene_normal.GetDimensions(normal_width, normal_height);
                uint2 normal_coord = min(uint2(uv * float2(normal_width, normal_height)),
                                         uint2(normal_width - 1u, normal_height - 1u));
                float3 world_normal = g_scene_normal[normal_coord].xyz * 2.0f - 1.0f;
                if (dot(particle.vel, world_normal) < 0.0f)
                {
                    float3 previous_direction = normalize(particle.vel);
                    float distance_to_surface = abs(dot(previous_direction, view_depth.xyz - view_particle.xyz));
                    float3 reflected_velocity = 0.4f * reflect(particle.vel, world_normal);
                    particle.vel = length(reflected_velocity) < 0.3f ? float3(0.0f, 0.0f, 0.0f) : reflected_velocity;
                    particle.pos += -distance_to_surface * previous_direction + particle.vel * dt;
                }
            }
        }
    }

    if (particle.life < 0.0f)
    {
        int dead_index;
        InterlockedAdd(g_counter[0].dead_count, 1, dead_index);
        g_dead_buffer[dead_index].x = particle_id;
        particle.pos = float3(0.0f, 0.0f, 0.0f);
        particle.life = 0.0f;
        particle.vel = float3(0.0f, 0.0f, 0.0f);
        particle.size_x = 0.0f;
        particle.acc = float3(0.0f, 0.0f, 0.0f);
        particle.size_y = 0.0f;
        particle.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    else
    {
        int next_alive_index;
        InterlockedAdd(g_counter[0].alive_count_after_sim, 1, next_alive_index);
        if (g_argument[0].alive_flap_bit == 0)
        {
            g_alive_list_next[next_alive_index].x = particle_id;
        }
        else
        {
            g_alive_list[next_alive_index].x = particle_id;
        }

        particle.life -= dt;
        g_render_particles[next_alive_index] = particle;
    }

    g_particles[particle_id] = particle;
}
