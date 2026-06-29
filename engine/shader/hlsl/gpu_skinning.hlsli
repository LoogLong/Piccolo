#ifndef PICCOLO_GPU_SKINNING_HLSLI
#define PICCOLO_GPU_SKINNING_HLSLI

// Output vertex data from GPU skinning compute shader.
// Layout intentionally matches PathTracingVertexData so consumers
// can bind the same buffer with their own type.
struct SkinnedVertexData
{
    float4 position;
    float4 normal;
    float4 tangent;
    float4 texcoord;
};

struct SkinComputeConstants
{
    uint vertex_count;
    uint joint_matrix_offset;  // index into g_joint_matrices for this instance
    uint output_vertex_offset; // offset into flat g_skinned_vertices (u7 only)
    uint _padding;
};

#endif // PICCOLO_GPU_SKINNING_HLSLI
