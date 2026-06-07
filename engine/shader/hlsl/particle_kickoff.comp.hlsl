#include "common.hlsli"

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

ConstantBuffer<ParticleDispatchData> g_particle_dispatch : register(b0, space0);
RWStructuredBuffer<ParticleCountBuffer> g_counter : register(u2, space0);
RWStructuredBuffer<ParticleArgumentBuffer> g_argument : register(u3, space0);

[numthreads(1, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    int emit_count = min(g_counter[0].dead_count, g_particle_dispatch.xemit_count);
    uint emit_groups = (uint)ceil(float(max(emit_count, 0)) / 256.0f);
    uint simulate_groups = (uint)ceil(float(max(emit_count + g_counter[0].alive_count_after_sim, 0)) / 256.0f);

    g_argument[0].emit_count = uint4(emit_groups, 1u, 1u, 0u);
    g_argument[0].simulate_count = uint4(simulate_groups, 1u, 1u, 0u);
    g_counter[0].alive_count = g_counter[0].alive_count_after_sim;
    g_counter[0].alive_count_after_sim = 0;
    g_counter[0].emit_count = emit_count;
    g_argument[0].alive_flap_bit = 1 - g_argument[0].alive_flap_bit;
}
