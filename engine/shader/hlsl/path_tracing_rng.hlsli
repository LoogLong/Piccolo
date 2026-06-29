#ifndef PICCOLO_PATH_TRACING_RNG_HLSLI
#define PICCOLO_PATH_TRACING_RNG_HLSLI

uint WangHash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

struct RNG
{
    uint state;
};

RNG InitRNG(uint2 pixel, uint2 extent, uint sample_index)
{
    RNG rng;
    rng.state = WangHash(pixel.x) ^ (WangHash(pixel.y) << 1) ^ (WangHash(sample_index) << 2);
    return rng;
}

float Rand01(inout RNG rng)
{
    rng.state = rng.state * 747796405u + 2891336453u;
    uint word = ((rng.state >> ((rng.state >> 28u) + 4u)) ^ rng.state) * 277803737u;
    return float((word >> 22u) ^ word) / 4294967296.0f;
}

float2 Rand2D(inout RNG rng)
{
    return float2(Rand01(rng), Rand01(rng));
}

#endif