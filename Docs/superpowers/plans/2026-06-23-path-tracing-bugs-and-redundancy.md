# Path Tracing Pipeline Bugs & Redundancy Issues

> Discovered 2026-06-23 during GPU skinning debugging with Nsight.

## Bug #3: Instance buffer / TLAS index mismatch

**Location:** `path_tracing_pass.cpp` — `buildTopLevelAS()`

**Root cause:** `updatePathTracingSceneBuffers()` creates `g_instances` at line 837 using ALL `collected_instances`. But later at lines 923-929, instances with null BLAS are removed via `erase(remove_if(...))`. The TLAS is built from the filtered list (lines 947-958), but `g_instances` was already populated with the unfiltered order.

When an instance is filtered out, the DXR `InstanceIndex()` (TLAS position) no longer matches the `g_instances` index:

```
Before filter:
  g_instances[0] = StaticA
  g_instances[1] = SkinnedB  (BLAS build failed → null)
  g_instances[2] = StaticC

After filter:
  collected_instances = [StaticA, StaticC]

TLAS:
  DXR instance 0 → StaticA  → reads g_instances[0] = StaticA ✓
  DXR instance 1 → StaticC  → reads g_instances[1] = SkinnedB ✗

Shader gets wrong geometry_index, material_index, flags for StaticC.
```

**Fix:** `updatePathTracingSceneBuffers()` must be called AFTER filtering, or with a filtered instance list.

## Bug #4: `shader_instance_index` — dead code

**Location:** `render_resource.h:33` (definition), `path_tracing_pass.cpp:942` (assignment only)

`shader_instance_index` is assigned but never read anywhere in the codebase. It is dead code and should be removed.

---

## Redundancy Issues (Nsight + code analysis)

### R1: 7 passes over `collected_instances` in `buildTopLevelAS()`

**Location:** `path_tracing_pass.cpp:798-958`

The same vector is iterated 7 separate times:

| Pass | Line | Description |
|------|------|-------------|
| 1 | 798 | `std::any_of()` — check if any skinned instance exists |
| 2 | 803 | Static BLAS loop — ensure BLAS for static meshes |
| 3 | 845 | Per-instance BLAS loop — build BLAS for skinned instances |
| 4 | 900 | Orphan cleanup — destroy BLAS for disappeared instances |
| 5 | 923 | `remove_if` + `erase` — filter null BLAS instances |
| 6 | 940 | `shader_instance_index` assignment — **DEAD CODE** (value never read) |
| 7 | 949 | Create TLAS instance descriptions |

**Impact:** O(7N) passes over instance list. Passes 1-2 can be combined. Pass 6 should be removed entirely. Passes 3-5 and 7 can potentially be merged.

### R2: Full descriptor rewrite every frame

**Location:** `path_tracing_pass.cpp:562-777`

`updateDescriptorSet()` writes all 15 descriptors every frame via `updateDescriptorSets()`. But many bindings are static across frames:

| Binding | Changes? | When |
|---------|----------|------|
| t0 (TLAS) | Per rebuild | When TLAS rebuilt |
| u1 (scene_output) | Never | — |
| b2 (frame_data) | Every frame | Camera, lights |
| u3 (accumulation) | Every frame | Ping-pong swap |
| t4 (vertices) | Per rebuild | When scene buffers updated |
| t5 (indices) | Per rebuild | When scene buffers updated |
| t6 (materials) | Rarely | Only on material change |
| t7 (geometries) | Per rebuild | When scene buffers updated |
| t8 (instances) | Per rebuild | When scene buffers updated |
| t9 (irradiance) | Never | — |
| t10 (specular) | Never | — |
| t11 (texture_array) | Rarely | Only on material change |
| s12 (sampler) | Never | — |
| t1036 (skinned_vert) | Every frame | If skinned meshes present |
| t1035 (accum_prev) | Every frame | Ping-pong swap |

Only ~6-7 of 15 bindings need per-frame updates. Unchanged descriptors are rewritten with identical values.

### R3: Scene buffer rebuild every frame when skinned instances exist

**Location:** `path_tracing_pass.cpp:825-841`

```cpp
const bool tlas_dirty = has_skinned || scene.isPathTracingTLASDirty() || ...;
if (!tlas_dirty) return true;
updatePathTracingSceneBuffers(...);
```

When `has_skinned` is true, `tlas_dirty` is always true → scene buffers rebuilt every frame. But static mesh data (vertex positions, normals, texcoords, indices, materials) does NOT change frame-to-frame. Only:
- Instance transforms change
- Skinned vertex data changes

The current code rebuilds ALL buffers (vertices, indices, materials, geometries, instances) even though only the instance buffer and skinned vertex buffer change.

### R4: Per-instance BLAS destroy+rebuild every frame

**Location:** `path_tracing_pass.cpp:872-893`

```cpp
if (pt_resources.blas != nullptr)
{
    m_rhi->destroyAccelerationStructure(pt_resources.blas);
    pt_resources.blas = nullptr;
}
pt_resources.blas = buildPathTracingBLASFromSkinned(...);
```

The per-instance BLAS is destroyed and rebuilt every frame unconditionally. This is necessary for animated meshes (positions change), but wasteful for non-animated skinned meshes. No check for whether the position buffer actually changed since last build.

### R5: Joint matrix buffer reallocated when joint count changes

**Location:** `gpu_skinning_pass.cpp:177-198`

```cpp
if (data_size > m_joint_matrix_buffer_capacity)
{
    destroyBuffer(m_joint_matrix_buffer);
    freeMemory(m_joint_matrix_memory);
    createBuffer(m_joint_matrix_buffer_capacity = data_size * 2, ...);
}
```

Minor: buffer doubles capacity each time it grows, but never shrinks. Fine for steady state, but wastes memory if a scene with many skinned meshes is replaced by a scene with few.

### R6: Flat vertex output buffer never shrinks

**Location:** `gpu_skinning_pass.cpp:439-452`

```cpp
if (required_size > m_skinned_vertex_output_capacity)
{
    destroyBuffer(...);
    createBuffer(m_skinned_vertex_output_capacity = required_size * 2, ...);
}
```

Same pattern — grows but never shrinks. Not a per-frame redundancy but a memory concern.

### R7: Redundant `if` checks in `updateDescriptorSet`

**Location:** `path_tracing_pass.cpp:580-601`

```cpp
if (m_specular_texture_view == nullptr || m_linear_sampler == nullptr)
{
    // ... populate texture views
}
if (m_specular_texture_view == nullptr || m_linear_sampler == nullptr)
{
    return false;  // This check can never fail after the block above
}
```

After the first `if` block populates the views/sampler, the second identical check at line 598 can never fail. The views/sampler are either populated successfully (non-null) or never populated (both null → return false).

### R8: `g_skinned_vertices` descriptor written even with no skinned meshes

**Location:** `path_tracing_pass.cpp:645-648`

`getSkinnedVertexBuffer()` may return nullptr (when no skinned meshes in scene). The descriptor still writes a null SRV binding every frame. Harmless but unnecessary.

