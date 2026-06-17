# Path Tracing Performance & Correctness Fix

> **For agentic workers:** Use `superpowers:subagent-driven-development` to implement this plan.
> Each Task can be assigned to a separate subagent when its dependencies are met.
> Tasks marked `[PARALLEL]` have no mutual dependencies and can execute concurrently.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix path tracing to converge over time (temporal accumulation), terminate low-contribution paths (Russian Roulette), correct IBL coordinate systems, and reduce per-frame ray budgets — making it both visually correct and usable at interactive framerates.

**Architecture:** Three independent shader-side fixes (accumulation, Russian Roulette, IBL correction) all sit inside the same HLSL file. The C++ host-side changes provide the accumulation infrastructure. All shader changes read from the same FrameData struct, so that struct's additions are a shared dependency.

**Tech Stack:** D3D12 Ray Tracing (DXR 1.1), HLSL SM 6.6, C++17, Piccolo RHI abstraction layer

## Global Constraints

- No new files — all changes in existing `path_tracing.lib.hlsl`, `path_tracing_pass.h`, `path_tracing_pass.cpp`
- D3D12 backend only; `RHIRayTracingSupportLevel::Supported` guard remains
- Must not break raster fallback path
- After all Tasks, commit the working changes

---

## File Map

| File | Responsibility | Modified? |
|------|---------------|-----------|
| `engine/shader/hlsl/path_tracing.lib.hlsl` | RayGen, ClosestHit, Miss shaders — all GPU-side path tracing logic | ✅ Yes |
| `engine/shader/hlsl/path_tracing_common.hlsli` | HLSL structs matching C++ GPU data layouts | ✅ Yes |
| `engine/shader/hlsl/path_tracing_rng.hlsli` | Wang hash RNG for stochastic sampling — consumed by `InitRNG` call site, file itself unchanged | ❌ No |
| `engine/source/runtime/function/render/passes/path_tracing_pass.h` | C++ `FrameData` struct, member variables for accumulation ping-pong | ✅ Yes |
| `engine/source/runtime/function/render/passes/path_tracing_pass.cpp` | Host-side dispatch, descriptor writes, buffer/image management | ✅ Yes |

---

### Task 1: Add Accumulation Infrastructure (C++ Host Side)

**Files:**
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.h:32-46` (FrameData)
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.h:73-76` (add accumulation_prev members)
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp:400-444` (ensureAccumulationImage)
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp:510-508` (updateDescriptorSet)
- Modify: `engine/source/runtime/function/render/passes/path_tracing_pass.cpp:446-508` (updateFrameData)

**Interfaces:**
- Produces: `FrameData::reset_accumulation` (uint32_t, 0 = accumulate, 1 = reset), `FrameData::sample_index` already exists
- Produces: `m_accumulation_prev_image`, `m_accumulation_prev_image_view` — previous frame's accumulation for reading
- Produces: Updated descriptor set binding 3 to point to `m_accumulation_image` for STORAGE_IMAGE (write), and new binding for `m_accumulation_prev_image` as SAMPLED_IMAGE (read)

**This Task is the prerequisite for Tasks 2, 3, and 4.** They can all start once this is committed.

- [ ] **Step 1: Add accumulation ping-pong members to PathTracingPass.h**

In `PathTracingPass` class, after `m_accumulation_image_view` (line 75), add:

```cpp
// Ping-pong accumulation: even frames write here, odd frames read from here
RHIImage*        m_accumulation_prev_image {nullptr};
RHIDeviceMemory* m_accumulation_prev_memory {nullptr};
RHIImageView*    m_accumulation_prev_image_view {nullptr};
RHIImageLayout   m_accumulation_prev_image_layout {RHI_IMAGE_LAYOUT_UNDEFINED};
```

In `FrameData` struct (line 32-46), change `_padding` to `reset_accumulation`:

```cpp
struct FrameData
{
    Matrix4x4 proj_view_matrix_inv {Matrix4x4::IDENTITY};
    Vector3   camera_position {Vector3::ZERO};
    uint32_t  sample_index {0};
    uint32_t  extent[2] {0, 0};
    uint32_t  instance_count {0};
    uint32_t  reset_accumulation {0};   // was: _padding
    Vector4   ambient_light {0.02f, 0.02f, 0.02f, 0.0f};
    RenderScenePointLight scene_point_lights[s_max_point_light_count] {};
    RenderSceneDirectionalLight scene_directional_light {};
    Matrix4x4 directional_light_proj_view {Matrix4x4::IDENTITY};
    uint32_t point_light_count {0};
    uint32_t _padding_light[3] {0, 0, 0};
};
```

- [ ] **Step 2: Add `resetting` logic to `updateFrameData`**

In `updateFrameData()`, at the location where `m_sample_index = 0` is set (lines 466-470), also set `resetting = true`. Then populate it into `frame_data`:

```cpp
FrameData frame_data {};
// ... existing fields ...
frame_data.reset_accumulation = resetting ? 1u : 0u;
```

Track `resetting` at function scope:

```cpp
bool PathTracingPass::updateFrameData(uint32_t instance_count)
{
    // ... existing nullptr checks ...

    bool resetting = false;

    const bool camera_changed =
        !m_has_last_camera_state ||
        !matrixEquals(m_last_proj_view_matrix_inv, current_proj_view_inv) ||
        !vectorEquals(m_last_camera_position, current_camera_position) ||
        m_extent.width != extent.width ||
        m_extent.height != extent.height;
    if (camera_changed)
    {
        m_sample_index = 0;
        resetting = true;
        // ... existing camera state save ...
    }

    if (auto render_scene = m_render_resource_impl->getCurrentRenderScene())
    {
        if (render_scene->isPathTracingAccumulationDirty())
        {
            m_sample_index = 0;
            resetting = true;
        }
    }

    // ... build frame_data ...
    frame_data.reset_accumulation = resetting ? 1u : 0u;
    // ... memcpy and map/unmap as before ...
}
```

- [ ] **Step 3: Update primary accumulation image usage + create prev image in `ensureAccumulationImage`**

In the existing `ensureAccumulationImage()`, update the primary accumulation image creation (line 420-429) to add `RHI_IMAGE_USAGE_SAMPLED_BIT` — needed because it will serve as the read-source on alternating frames after ping-pong swap:

```cpp
// Change line 424 from:
//     RHI_IMAGE_USAGE_STORAGE_BIT,
// To:
//     RHI_IMAGE_USAGE_STORAGE_BIT | RHI_IMAGE_USAGE_SAMPLED_BIT,
m_rhi->createImage(extent.width,
                   extent.height,
                   RHI_FORMAT_R32G32B32A32_SFLOAT,
                   RHI_IMAGE_TILING_OPTIMAL,
                   RHI_IMAGE_USAGE_STORAGE_BIT | RHI_IMAGE_USAGE_SAMPLED_BIT,  // <-- updated
                   RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                   m_accumulation_image,
                   m_accumulation_memory,
                   0,
                   1,
                   1);
```

Then, after creating the primary `m_accumulation_image_view`, create the prev image with identical specs:

```cpp
// Create prev accumulation image (for reading previous frame)
m_rhi->createImage(extent.width,
                   extent.height,
                   RHI_FORMAT_R32G32B32A32_SFLOAT,
                   RHI_IMAGE_TILING_OPTIMAL,
                   RHI_IMAGE_USAGE_STORAGE_BIT | RHI_IMAGE_USAGE_SAMPLED_BIT,
                   RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                   m_accumulation_prev_image,
                   m_accumulation_prev_memory,
                   0,
                   1,
                   1);
m_rhi->createImageView(m_accumulation_prev_image,
                       RHI_FORMAT_R32G32B32A32_SFLOAT,
                       RHI_IMAGE_ASPECT_COLOR_BIT,
                       RHI_IMAGE_VIEW_TYPE_2D,
                       1,
                       1,
                       m_accumulation_prev_image_view);
m_accumulation_prev_image_layout = RHI_IMAGE_LAYOUT_UNDEFINED;
```

- [ ] **Step 4: Update `destroyAccumulationImage` to clean up prev image**

Add destruction of the prev image before resetting `m_extent`:

```cpp
if (m_accumulation_prev_image_view != nullptr)
{
    m_rhi->destroyImageView(m_accumulation_prev_image_view);
    m_accumulation_prev_image_view = nullptr;
}
if (m_accumulation_prev_image != nullptr)
{
    m_rhi->destroyImage(m_accumulation_prev_image);
    m_accumulation_prev_image = nullptr;
}
if (m_accumulation_prev_memory != nullptr)
{
    m_rhi->freeMemory(m_accumulation_prev_memory);
    m_accumulation_prev_memory = nullptr;
}
m_accumulation_prev_image_layout = RHI_IMAGE_LAYOUT_UNDEFINED;
```

- [ ] **Step 5: Update descriptor set layout to add accumulation input image**

In `setupDescriptorSetLayout()`, add a 14th binding (binding 13) for the previous accumulation as a sampled image that the RayGen shader can read:

```cpp
// After bindings[12] ...
bindings[13].binding         = 1035; // must be >1034 to avoid overlap with g_texture_array (t11-t1034)
bindings[13].descriptorType  = RHI_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
bindings[13].descriptorCount = 1;
bindings[13].stageFlags      = RHI_SHADER_STAGE_RAYGEN_BIT_KHR;

RHIDescriptorSetLayoutCreateInfo create_info {};
create_info.sType        = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
create_info.bindingCount = static_cast<uint32_t>(std::size(bindings));
create_info.pBindings    = bindings;
```

- [ ] **Step 6: Update descriptor set writes for accumulation prev image**

In `updateDescriptorSet()`, add the prev accumulation image info and a write for binding 13. Extend the `writes` array from `[13]` to `[14]`:

```cpp
RHIDescriptorImageInfo accumulation_prev_info {};
accumulation_prev_info.imageView   = m_accumulation_prev_image_view;
accumulation_prev_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

// Extend writes array to 14 entries
RHIWriteDescriptorSet writes[14] {};
// ... existing writes[0] through writes[12] unchanged ...

writes[13].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
writes[13].dstSet          = m_descriptor_set;
writes[13].dstBinding      = 1035;
writes[13].descriptorType  = RHI_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
writes[13].descriptorCount = 1;
writes[13].pImageInfo      = &accumulation_prev_info;

m_rhi->updateDescriptorSets(static_cast<uint32_t>(std::size(writes)), writes, 0, nullptr);
```

- [ ] **Step 7: Add barrier transitions for accumulation prev image in `dispatch()`**

After transitioning `m_accumulation_image` to GENERAL (lines 138-145), also transition `m_accumulation_prev_image`:

```cpp
transitionImage(m_accumulation_prev_image,
                m_accumulation_prev_image_layout,
                RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                0,
                RHI_ACCESS_SHADER_READ_BIT,
                RHI_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                RHI_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
m_accumulation_prev_image_layout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
```

And after `cmdTraceRays`, keep `m_accumulation_prev_image` in GENERAL for the next frame:

```cpp
// After existing accumulation transition (line 175-181):
// Keep prev accumulation image in GENERAL layout for next frame's read
```

- [ ] **Step 8: At end of `dispatch()`, swap accumulation images**

After the dispatch and transitions, swap the current and prev accumulation images so next frame reads what this frame wrote:

```cpp
// Swap accumulation ping-pong
std::swap(m_accumulation_image, m_accumulation_prev_image);
std::swap(m_accumulation_image_view, m_accumulation_prev_image_view);
std::swap(m_accumulation_memory, m_accumulation_prev_memory);
std::swap(m_accumulation_image_layout, m_accumulation_prev_image_layout);
```

Note: the descriptor set was already bound and the trace already executed — the swap only affects the next frame's `ensureAccumulationImage` early-out check. The descriptor set must be re-written each frame to point to the correct current/prev images. Since `updateDescriptorSet()` is called every frame (because `m_descriptor_set_dirty` is set true by various paths), this is already satisfied.

- [ ] **Step 9: Commit**

```bash
git add engine/source/runtime/function/render/passes/path_tracing_pass.h \
        engine/source/runtime/function/render/passes/path_tracing_pass.cpp
git commit -m "feat: add accumulation ping-pong infrastructure for path tracing

- Add m_accumulation_prev_image/view/memory for reading previous frame
- Add FrameData::reset_accumulation flag (replaces _padding)
- Add descriptor binding 13 for prev accumulation image (read)
- Swap accumulation images each frame after dispatch
- Clean up prev image in destroyAccumulationImage"
```

---

### Task 2: Temporal Accumulation in HLSL RayGen [PARALLEL]

**Dependencies:** Task 1 complete (FrameData fields, descriptor bindings exist)

**Files:**
- Modify: `engine/shader/hlsl/path_tracing.lib.hlsl:6,28-60` (declare new binding, modify RayGen)
- Modify: `engine/shader/hlsl/path_tracing_common.hlsli:15` (add `reset_accumulation` to HLSL struct)

**Interfaces:**
- Consumes: `g_accumulation_prev : register(t1035, space0)` — sampled image for previous frame's accumulated radiance (binding 1035 avoids overlap with g_texture_array at t11-t1034)
- Consumes: `g_frame_data.reset_accumulation` — uint, 1 = discard history
- Consumes: `g_frame_data.sample_index` — already existed, now used for blending weight
- Produces: `g_scene_output[pixel]` now contains blended result instead of raw single-sample radiance

- [ ] **Step 1: Update `PathTracingFrameData` in common.hlsli**

In `engine/shader/hlsl/path_tracing_common.hlsli:15`, change `uint _padding;` to `uint reset_accumulation;`:

```hlsl
struct PathTracingFrameData
{
    row_major float4x4 proj_view_matrix_inv;
    float3 camera_position;
    uint sample_index;
    uint2 extent;
    uint instance_count;
    uint reset_accumulation;  // was: uint _padding;
    float4 ambient_light;
    PointLight scene_point_lights[M_MAX_POINT_LIGHT_COUNT];
    DirectionalLight scene_directional_light;
    row_major float4x4 directional_light_proj_view;
    uint point_light_count;
    uint3 _padding_light;
};
```

- [ ] **Step 2: Add prev accumulation binding declaration**

In `path_tracing.lib.hlsl`, after line 6 (`g_scene_output` declaration), add:

```hlsl
RaytracingAccelerationStructure g_scene_tlas : register(t0, space0);
RWTexture2D<float4> g_scene_output : register(u1, space0);
ConstantBuffer<PathTracingFrameData> g_frame_data : register(b2, space0);
RWTexture2D<float4> g_accumulation_output : register(u3, space0);
Texture2D<float4> g_accumulation_prev : register(t1035, space0); // previous frame accumulation (read)
```

Note: `g_accumulation_prev` is declared as `Texture2D<float4>` (SRV, not UAV) because HLSL DXR shaders cannot cleanly read from `RWTexture2D`. The C++ side binds it as `RHI_DESCRIPTOR_TYPE_SAMPLED_IMAGE` on binding 13, and we use `.Load(int3(pixel, 0))` for exact pixel access.

- [ ] **Step 3: Replace RayGen write with accumulation blending AND fix RNG seeding**

Replace lines 57-59 (`g_scene_output[pixel] = ...; g_accumulation_output[pixel] = ...;`) in `PathTracingRayGen()` with the accumulation blend. **Also fix line 43** — change `InitRNG(pixel, extent, 0)` to `InitRNG(pixel, extent, g_frame_data.sample_index)` — the hardcoded `0` caused every frame to trace identical paths, making accumulation useless:

```hlsl
// In PathTracingRayGen(), replace the InitRNG call (line 43):
// OLD: payload.rng = InitRNG(pixel, extent, 0);
payload.rng = InitRNG(pixel, extent, g_frame_data.sample_index);

// ... (rest of existing code: uv, ndc, world_position setup) ...

// Replace the final writes (lines 57-59):
// Temporal accumulation blend
float3 prev_accum = float3(0.0f, 0.0f, 0.0f);
float sample_count = 1.0f;

if (g_frame_data.reset_accumulation == 0)
{
    prev_accum = g_accumulation_prev.Load(int3(pixel, 0)).rgb;
    sample_count = float(g_frame_data.sample_index) + 1.0f;
}

const float3 blended = (prev_accum * (sample_count - 1.0f) + payload.radiance) / sample_count;

g_scene_output[pixel] = float4(blended, 1.0f);
g_accumulation_output[pixel] = float4(blended, 1.0f);
```

- [ ] **Step 4: Commit**

```bash
git add engine/shader/hlsl/path_tracing.lib.hlsl \
        engine/shader/hlsl/path_tracing_common.hlsli
git commit -m "feat: add temporal accumulation blending in path tracing RayGen

- Fix InitRNG hardcoded sample_index=0 → use g_frame_data.sample_index
  (every frame traced identical paths, making accumulation a no-op)
- Read previous frame's accumulation from binding 13 (Texture2D via SRV),
  blend with new sample using running-average weight
- Respect FrameData::reset_accumulation flag to discard history on camera
  movement or scene changes

Before: 1 spp forever, no convergence. After: N spp after N frames."
```

---

### Task 3: Russian Roulette Path Termination [PARALLEL]

**Dependencies:** Task 1 complete (but can develop in parallel with Task 2)

**Files:**
- Modify: `engine/shader/hlsl/path_tracing.lib.hlsl:95,256-288` (MAX_BOUNCES, closest-hit indirect bounce section)

**Interfaces:**
- Consumes: `RNG` state from `payload.rng` (defined in `path_tracing_rng.hlsli`)
- Produces: Paths terminate probabilistically instead of always reaching `MAX_BOUNCES`

- [ ] **Step 1: Implement Russian Roulette helper function**

Add this function before the closest-hit shader (near line 95, after `CosineSampleHemisphere`):

```hlsl
// Russian Roulette: returns (survived: bool, survival_probability: float)
// Uses surface base_color luminance as the throughput proxy — a simple,
// representative metric for whether the path carries energy worth tracing.
float2 RussianRoulette(inout RNG rng, float3 base_color)
{
    // PBRT v4 §13.4.1: terminate paths proportional to (1 - reflectance)
    const float lum = max(base_color.r, max(base_color.g, base_color.b));
    const float survival_prob = clamp(lum, 0.05f, 0.95f);
    const float survived = Rand01(rng) < survival_prob ? 1.0f : 0.0f;
    return float2(survived, survival_prob);
}
```

Note: `survival_prob` is returned so the caller can re-weight surviving paths by `1/survival_prob` for an unbiased estimator.

- [ ] **Step 2: Replace indirect bounce section with Russian Roulette**

Replace the indirect bounce block (lines 256-288 in `PathTracingClosestHit`) with:

```hlsl
    // --- Indirect bounce with Russian Roulette ---
    if (payload.bounce_depth < MAX_BOUNCES)
    {
        // PBRT v4 §13.4.1: Russian roulette based on surface reflectance
        float2 rr = RussianRoulette(payload.rng, base_color);
        if (rr.x > 0.0f) // survived
        {
            float pdf;
            const float3 sample_dir = CosineSampleHemisphere(Rand2D(payload.rng), pdf);
            const float3 L = TangentToWorld(sample_dir, N);
            const float  NdotL = max(dot(N, L), 0.0f);

            if (pdf > 0.0f && NdotL > 0.0f)
            {
                PathTracingRayPayload indirect_payload;
                indirect_payload.radiance      = float3(0.0f, 0.0f, 0.0f);
                indirect_payload.hit           = 0u;
                indirect_payload.rng           = payload.rng;
                indirect_payload.is_shadow_ray = false;
                indirect_payload.bounce_depth  = payload.bounce_depth + 1u;

                RayDesc indirect_ray;
                indirect_ray.Origin    = world_position + N * 0.01f;
                indirect_ray.Direction = L;
                indirect_ray.TMin      = 0.001f;
                indirect_ray.TMax      = 100000.0f;

                TraceRay(g_scene_tlas, RAY_FLAG_NONE, 0xFF, 0, 1, 0, indirect_ray, indirect_payload);
                payload.rng = indirect_payload.rng;

                if (indirect_payload.hit != 0u)
                {
                    // rr.y = survival_prob — re-weight by 1/P for unbiased estimator
                    Lo += BRDF(L, V, N, f0, base_color, metallic, roughness) *
                          indirect_payload.radiance * NdotL / pdf / rr.y;
                }
            }
        }
    }
```

- [ ] **Step 3: Commit**

```bash
git add engine/shader/hlsl/path_tracing.lib.hlsl
git commit -m "feat: add Russian Roulette path termination for indirect bounces

- RussianRoulette() uses base_color luminance as throughput proxy (PBRT
  v4 §13.4.1) — simpler and more representative than BRDF(V,V)
- Survival probability clamped to [0.05, 0.95]
- Helper returns (survived, prob) tuple; caller re-weights by 1/prob using
  the exact same survival probability that made the decision — keeps the
  Monte Carlo estimator unbiased

This reduces total rays/pixel from ~N*MAX_BOUNCES to ~N*2 on average."
```

---

### Task 4: Fix IBL Coordinate System & Reduce Shadow Rays [PARALLEL]

**Dependencies:** Task 1 complete (can develop in parallel with Tasks 2 and 3)

**Files:**
- Modify: `engine/shader/hlsl/path_tracing.lib.hlsl:62-77` (miss shader)
- Modify: `engine/shader/hlsl/path_tracing.lib.hlsl:186-253` (shadow ray sections)

**Interfaces:**
- Consumes: `WorldRayDirection()` built-in, `g_specular_texture`, `g_irradiance_texture`
- Produces: Correct sky color in miss shader, reduced shadow rays per frame

- [ ] **Step 1: Fix miss shader IBL coordinate mapping**

Replace the miss shader (lines 62-77) with:

```hlsl
[shader("miss")]
void PathTracingMiss(inout PathTracingRayPayload payload)
{
    if (payload.is_shadow_ray)
    {
        payload.hit = 0u;
        payload.radiance = float3(1.0f, 1.0f, 1.0f);
    }
    else
    {
        const float3 ray_dir = WorldRayDirection();
        // Match the coordinate convention used by SampleEnvironmentLight:
        // the engine cubemaps use (x, z, y) mapping
        const float3 sample_dir = float3(ray_dir.x, ray_dir.z, ray_dir.y);
        const float3 sky_color = g_specular_texture.SampleLevel(g_linear_sampler, sample_dir, 0.0f).rgb;
        payload.radiance = sky_color;
        payload.hit      = 0u;
    }
}
```

- [ ] **Step 2: Reduce per-frame shadow rays — skip point-light shadows on indirect bounces**

In the point light loop (lines 218-253), only cast shadow rays for bounce depth 0 (primary hit):

```hlsl
    // --- Point lights (shadow rays only on primary bounce) ---
    if (payload.bounce_depth == 0u)
    {
        const uint point_light_count = min(g_frame_data.point_light_count, M_MAX_POINT_LIGHT_COUNT);
        for (uint li = 0; li < point_light_count; li++)
        {
            const PointLight light = g_frame_data.scene_point_lights[li];
            const float3 light_vec = light.position - world_position;
            const float  light_dist = length(light_vec);
            const float3 L = light_vec / light_dist;
            const float  NdotL = dot(N, L);

            if (NdotL > 0.0f)
            {
                PathTracingRayPayload shadow_payload;
                shadow_payload.radiance      = float3(0.0f, 0.0f, 0.0f);
                shadow_payload.hit           = 0u;
                shadow_payload.rng           = payload.rng;
                shadow_payload.is_shadow_ray = true;
                shadow_payload.bounce_depth  = 0u;

                RayDesc shadow_ray;
                shadow_ray.Origin    = world_position + N * 0.01f;
                shadow_ray.Direction = L;
                shadow_ray.TMin      = 0.001f;
                shadow_ray.TMax      = light_dist - 0.001f;

                TraceRay(g_scene_tlas, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
                         0xFF, 0, 1, 0, shadow_ray, shadow_payload);
                payload.rng = shadow_payload.rng;

                if (shadow_payload.hit == 0u)
                {
                    const float attenuation = 1.0f / max(light_dist * light_dist, 0.0001f);
                    Lo += BRDF(L, V, N, f0, base_color, metallic, roughness) *
                          light.intensity * attenuation * NdotL;
                }
            }
        }
    }
    else
    {
        // On indirect bounces: approximate point light contribution without shadow rays
        const uint point_light_count = min(g_frame_data.point_light_count, M_MAX_POINT_LIGHT_COUNT);
        for (uint li = 0; li < point_light_count; li++)
        {
            const PointLight light = g_frame_data.scene_point_lights[li];
            const float3 light_vec = light.position - world_position;
            const float  light_dist = length(light_vec);
            const float3 L = light_vec / light_dist;
            const float  NdotL = max(dot(N, L), 0.0f);

            if (NdotL > 0.0f)
            {
                const float attenuation = 1.0f / max(light_dist * light_dist, 0.0001f);
                Lo += BRDF(L, V, N, f0, base_color, metallic, roughness) *
                      light.intensity * attenuation * NdotL;
            }
        }
    }
```

- [ ] **Step 3: Commit**

```bash
git add engine/shader/hlsl/path_tracing.lib.hlsl
git commit -m "fix: correct IBL coordinate mapping in miss shader and reduce shadow rays

IBL fix:
- Miss shader now swaps Y/Z when sampling specular cubemap, matching the
  convention used by SampleEnvironmentLight() in the closest-hit shader.

Shadow ray reduction:
- Point light shadow rays now only cast on primary bounce (depth 0).
- Indirect bounces use unshadowed point light contribution as approximation.
- Directional light shadow ray remains for all bounce depths.

Per-pixel ray count reduced from (1 + (N+2)*4) to (1 + (N+2) + 3*2),
roughly halving total TraceRay calls for typical scenes with 4 point lights."
```

---

### Task 5: Integration Verification

**Dependencies:** Tasks 1, 2, 3, 4 complete

**Files:**
- None (verification only)

**Interfaces:**
- Consumes: All prior Task deliverables
- Produces: Confirmation that the full pipeline works correctly

- [ ] **Step 1: Build the project**

```bash
# Run the Piccolo build system (adjust command to project's build system)
cd d:/program/Piccolo
# Use whatever build command the project uses — e.g.:
cmake --build build --config Release 2>&1 | tail -40
```

**Expected:** Build succeeds with no shader compilation errors and no C++ link errors.

- [ ] **Step 2: Verify HLSL shader compilation**

Check the build output for any DXC shader compilation warnings related to `path_tracing.lib.hlsl`. There should be zero warnings.

- [ ] **Step 3: Visual inspection checklist**

Run the engine in PathTracing mode and verify:

1. **Convergence:** The image starts noisy but converges to a clean result after ~50-100 frames. Stationary camera = improving quality.
2. **Reset on move:** Moving the camera resets accumulation (brief noise spike, then re-converges).
3. **Sky color:** Miss rays show plausible environment color, not black or inverted.
4. **Framerate:** Measurably higher FPS than before due to reduced shadow rays and Russian Roulette.
5. **No artifacts:** No black pixels, no NaN/inf in accumulation, no flickering beyond the expected Monte Carlo noise.

- [ ] **Step 4: Commit any final tweaks and the plan document**

```bash
git add docs/superpowers/plans/2026-06-17-path-tracing-perf-correctness.md
git commit -m "docs: add path tracing performance and correctness fix plan"
```

---

## Dependency Graph

```
Task 1 (C++ Accumulation Infra) ──┬── Task 2 (HLSL Accumulation) ──┬── Task 5 (Verify)
                                  ├── Task 3 (Russian Roulette)   ──┤
                                  └── Task 4 (IBL Fix + Ray Opt)  ──┘
```

- Task 1 is the **blocker** — must finish first
- Tasks 2, 3, 4 are `[PARALLEL]` — dispatch all three simultaneously after Task 1 commits
- Task 5 runs after 2, 3, 4 are all committed

## Multi-Agent Dispatch Strategy

After Task 1 is done, launch three subagents simultaneously:

```
subagent-1: "Implement Task 2 from docs/superpowers/plans/2026-06-17-path-tracing-perf-correctness.md"
subagent-2: "Implement Task 3 from docs/superpowers/plans/2026-06-17-path-tracing-perf-correctness.md"
subagent-3: "Implement Task 4 from docs/superpowers/plans/2026-06-17-path-tracing-perf-correctness.md"
```

Each works on different sections of the same HLSL file. The subagents should be told to expect potential merge conflicts and to coordinate via git.
