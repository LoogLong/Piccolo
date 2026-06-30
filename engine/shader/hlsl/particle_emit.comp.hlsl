#include "common.hlsli"

#define POINT_TYPE_EMITTER 0
#define MESH_TYPE_EMITTER 1

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

struct EmitterInfo
{
    float4 pos;
    row_major float4x4 rotation;
    float4 vel;
    float4 acc;
    float3 size;
    int emitter_type;
    float4 life;
    float4 color;
};

ConstantBuffer<ParticleDispatchData> g_particle_dispatch : register(b0, space0);
RWStructuredBuffer<Particle> g_particles : register(u1, space0);
RWStructuredBuffer<ParticleCountBuffer> g_counter : register(u2, space0);
RWStructuredBuffer<ParticleArgumentBuffer> g_argument : register(u3, space0);
RWStructuredBuffer<int4> g_alive_list : register(u4, space0);
RWStructuredBuffer<int4> g_dead_buffer : register(u5, space0);
RWStructuredBuffer<int4> g_alive_list_next : register(u6, space0);
RWStructuredBuffer<EmitterInfo> g_emitter_info : register(u7, space0);
Texture2D<float4> g_emitter_texture : register(t10, space0);
SamplerState g_emitter_sampler : register(s10, space0);

static const float PHI = 1.618033988749895f;
static const float PI = 3.141592653589793f;

float GoldNoise(float2 xy, float seed)
{
    return frac(tan(distance(xy * PHI, xy) * seed) * xy.x);
}

[numthreads(256, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint thread_id = dispatch_thread_id.x;

    if (thread_id == 0)
    {
        g_emitter_info[0].life.z += 1.0f;
    }

    DeviceMemoryBarrierWithGroupSync();

    if (g_emitter_info[0].life.z <= g_particle_dispatch.emit_delta || thread_id >= (uint)max(g_counter[0].emit_count, 0))
    {
        return;
    }

    if (thread_id == 0)
    {
        g_emitter_info[0].life.z = 1.0f;
    }

    EmitterInfo emitter = g_emitter_info[0];
    float rnd0 = GoldNoise(float2(thread_id * g_particle_dispatch.random0, thread_id * g_particle_dispatch.random1), g_particle_dispatch.random2);
    float rnd1 = GoldNoise(float2(thread_id * g_particle_dispatch.random0, thread_id * g_particle_dispatch.random1), g_particle_dispatch.random2 + 0.2f);
    float rnd2 = GoldNoise(float2(thread_id * g_particle_dispatch.random0, thread_id * g_particle_dispatch.random1), g_particle_dispatch.random2 + 0.4f);

    bool fixed_particle = false;
    Particle particle;

    if (emitter.emitter_type == POINT_TYPE_EMITTER)
    {
        float theta = 0.15f * PI;
        float phi = (2.0f * rnd0 - 1.0f) * PI;
        float radius = 1.0f + rnd1;
        float x = radius * sin(theta) * cos(phi);
        float y = radius * sin(theta) * sin(phi);
        float z = radius * cos(theta);

        particle.pos = float3(0.1f * (2.0f * rnd0 - 1.0f), 0.1f * (2.0f * rnd1 - 1.0f), 0.1f * (2.0f * rnd2 - 1.0f)) * emitter.pos.w + emitter.pos.xyz;
        particle.vel = float3(x, y, z) * emitter.vel.w + emitter.vel.xyz;
        particle.color = emitter.color;
    }
    else
    {
        float4 rotated_pos = mul(emitter.rotation, float4(0.0f, rnd0, rnd1, 0.0f));
        particle.pos = emitter.pos.xyz + rotated_pos.xyz;

        float4 color = g_emitter_texture.SampleLevel(g_emitter_sampler, float2(1.0f - rnd0, 1.0f - rnd1), 0.0f);
        if (color.a > 0.9f)
        {
            particle.color = color;
            fixed_particle = true;
        }
        else
        {
            particle.color = float4(1.0f - rnd0, 1.0f - rnd1, 1.0f - rnd2, 0.0f);
            float4 rotated_vel = mul(emitter.rotation,
                                     float4((rnd0 * 2.0f - 1.0f) * emitter.vel.w + emitter.vel.x,
                                            (rnd1 * 2.0f - 1.0f) * emitter.vel.w + emitter.vel.y,
                                            (rnd2 * 2.0f - 1.0f) * emitter.vel.w + emitter.vel.z,
                                            1.0f));
            particle.vel = rotated_vel.xyz;
        }
    }

    particle.acc = fixed_particle ? float3(0.0f, 0.0f, 0.0f) : emitter.acc.xyz + g_particle_dispatch.gravity.xyz;
    particle.life = rnd0 * emitter.life.y + emitter.life.x;
    particle.size_x = emitter.size.x;
    particle.size_y = emitter.size.y;

    int dead_count;
    InterlockedAdd(g_counter[0].dead_count, -1, dead_count);
    if (dead_count <= 0)
    {
        InterlockedAdd(g_counter[0].dead_count, 1);
        return;
    }

    int particle_index = g_dead_buffer[dead_count - 1].x;
    g_particles[particle_index] = particle;

    int alive_index;
    InterlockedAdd(g_counter[0].alive_count, 1, alive_index);
    if (g_argument[0].alive_flap_bit == 0)
    {
        g_alive_list[alive_index].x = particle_index;
    }
    else
    {
        g_alive_list_next[alive_index].x = particle_index;
    }
}
