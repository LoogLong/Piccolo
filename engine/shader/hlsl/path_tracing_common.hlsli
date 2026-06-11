#ifndef PICCOLO_PATH_TRACING_COMMON_HLSLI
#define PICCOLO_PATH_TRACING_COMMON_HLSLI

struct PathTracingFrameData
{
    row_major float4x4 proj_view_matrix_inv;
    float3 camera_position;
    uint sample_index;
    uint2 extent;
    uint instance_count;
    uint _padding;
};

#endif
