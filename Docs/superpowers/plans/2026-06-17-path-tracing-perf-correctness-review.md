# Review: Path Tracing Performance & Correctness Fix Plan

> Review of `docs/superpowers/plans/2026-06-17-path-tracing-perf-correctness.md`
> Date: 2026-06-17

## Verdict

The plan is well-structured with clear dependencies and a sensible dispatch strategy. However, **two correctness bugs and one critical omission** must be fixed before implementation. The omission (RNG re-seeding) would render the entire accumulation effort pointless.

---

## 🔴 Critical Issues

### Issue 1 — Task 2: Missing RNG re-seeding makes temporal accumulation useless

**File:** [engine/shader/hlsl/path_tracing.lib.hlsl:43](engine/shader/hlsl/path_tracing.lib.hlsl#L43)

```hlsl
payload.rng = InitRNG(pixel, extent, 0);  // hardcoded 0 — never changes per frame
```

`InitRNG` derives its state from three inputs: `pixel`, `extent`, and `sample_index`. Since `sample_index` is hardcoded to `0`, every frame generates **identical random seeds** for every pixel. Each frame traces exactly the same paths and produces exactly the same `payload.radiance`.

The blending formula `(prev * (N-1) + radiance) / N` then reduces to `radiance` because `prev = radiance` (same frame result). The image never converges beyond 1 spp quality.

**Required fix in Task 2 Step 3:**

```hlsl
payload.rng = InitRNG(pixel, extent, g_frame_data.sample_index);
```

Without this, the accumulation infrastructure built in Task 1 is dead code.

---

### Issue 2 — Task 3: RR weight uses different value than survival probability

**File:** [engine/shader/hlsl/path_tracing.lib.hlsl](engine/shader/hlsl/path_tracing.lib.hlsl) (indirect bounce section)

The plan computes the survival probability from `Lo + BRDF`:

```hlsl
// Survival probability is based on Lo + throughput
if (RussianRouletteContinue(payload.rng, Lo + throughput))
{
    // ...
    // But the divisor weight only uses throughput (no Lo!)
    const float rr_weight = 1.0f / clamp(
        max(throughput.r, max(throughput.g, throughput.b)), 0.05f, 0.95f);
```

For unbiased Monte Carlo, the re-weighting factor must be `1 / P(continue)`, i.e., `1 / survival_prob`. Using a different value introduces bias — survived paths will be systematically over- or under-weighted relative to their actual survival probability.

**Required fix:** Either:

**Option A** — Base `rr_weight` on the same value used for the decision:

```hlsl
const float rr_prob = clamp(max(Lo.r + throughput.r, max(Lo.g + throughput.g, Lo.b + throughput.b)), 0.05f, 0.95f);
if (Rand01(payload.rng) < rr_prob)
{
    // ...
    const float rr_weight = 1.0f / rr_prob;
```

**Option B** — Base both the decision and weight on `throughput` alone (simpler, standard for path throughput RR):

```hlsl
const float survival_prob = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05f, 0.95f);
if (Rand01(payload.rng) < survival_prob)
{
    // ...
    const float rr_weight = 1.0f / survival_prob;
```

---

## 🟡 Medium Issues

### Issue 3 — Task 1 Steps 6-7: Descriptor imageLayout mismatches actual image layout at draw time

In Step 6, `updateDescriptorSet()` writes:

```cpp
accumulation_prev_info.imageLayout = RHI_IMAGE_LAYOUT_GENERAL;
```

In Step 7 (which runs *after* `updateDescriptorSet()`), the image is transitioned to:

```cpp
RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
```

In D3D12, the layout in the descriptor at draw time must match the image's actual layout. At trace execution time, the image is in `SHADER_READ_ONLY_OPTIMAL`, but the descriptor states `GENERAL`. While D3D12 validation layers occasionally tolerate `GENERAL` (since it's a superset of allowed operations), this is technically a spec violation.

**Recommended fix:** Either:

**(a)** Move the `transitionImage` call for `m_accumulation_prev_image` before `updateDescriptorSet()`, and use `SHADER_READ_ONLY_OPTIMAL` in the descriptor info.

**(b)** Keep `GENERAL` in both the descriptor and the layout — skip the Step 7 transition to `SHADER_READ_ONLY_OPTIMAL` entirely, leaving the prev image in `GENERAL`. This is a common pattern in real-time ray tracers.

---

### Issue 4 — Task 3: Unconventional throughput metric for Russian Roulette

The plan computes:

```hlsl
float3 throughput = BRDF(V, V, N, f0, base_color, metallic, roughness);
```

`BRDF(V, V, ...)` evaluates the BSDF in the view direction, i.e., the specular peak. For diffuse surfaces (roughness ≈ 1.0, low metallic), this evaluates to a low value, causing paths to terminate early on diffuse bounces even when they carry significant indirect energy. Conversely, highly specular surfaces will almost always continue.

A more standard approach is to use:

```hlsl
float3 throughput = base_color;  // surface albedo — simpler and more representative
```

Or ideally, track the accumulated path throughput `throughput *= BRDF(...) * NdotL / pdf` and use its luminance for RR decisions. See PBRT v4 §13.4.1 for reference.

**Severity:** Low for an initial implementation. The plan's approach is biased but functional. Consider adding a TODO comment for future improvement.

---

## 🟢 Minor Issues

### Issue 5 — Task 1 Step 5: Hardcoded binding count replaces `std::size`

The plan changes:

```cpp
create_info.bindingCount = 14; // was: std::size(bindings)
```

The existing code uses `std::size(bindings)` which automatically adapts when the array size changes. Using a hardcoded literal creates a maintenance hazard — if another binding is added later, the count must be updated in a second place.

**Recommendation:** Keep `std::size(bindings)` — it will correctly compute 14 when the array is `bindings[14]`.

---

### Issue 6 — Task 4 Step 1: IBL swizzle correct but unverifiable statically

The plan applies `float3(ray_dir.x, ray_dir.z, ray_dir.y)` to match `SampleEnvironmentLight`'s convention. Whether this is the correct fix depends on the cubemap's coordinate system:

- If the cubemap is authored with Y-up → the plan fixes an actual bug (currently wrong sky color)
- If the cubemap is authored with its own native convention that matches `WorldRayDirection()` → the plan *breaks* sky color

Given that `SampleEnvironmentLight` applies the same swizzle and presumably produces correct diffuse IBL, the fix is likely correct. **Verify with a visual test:** render a scene with only sky visible and check that the color is plausible (not black/inverted/garbled).

---

### Issue 7 — Plan file map lists unused file

The File Map table lists `path_tracing_rng.hlsli` but no task modifies it. If the RNG re-seeding fix (Issue 1 above) is accepted, that file's content would be indirectly consumed but not modified — the table should be updated to clarify this, or the `InitRNG` fix should be noted as not requiring file changes (only the caller changes).

---

## Issue Summary

| # | Severity | Task | Summary |
|---|----------|------|---------|
| 1 | 🔴 Critical | Task 2 | RNG seeded with hardcoded 0 — accumulation does nothing |
| 2 | 🔴 Critical | Task 3 | RR weight `1/P` uses different `P` than the termination decision |
| 3 | 🟡 Medium | Task 1 | Descriptor `imageLayout` vs actual layout mismatch |
| 4 | 🟡 Medium | Task 3 | Unconventional RR throughput metric may cause early termination on diffuse |
| 5 | 🟢 Minor | Task 1 | Hardcoded `14` replaces safer `std::size(bindings)` |
| 6 | 🟢 Minor | Task 4 | IBL swizzle correctness depends on cubemap convention |
| 7 | 🟢 Minor | — | File Map lists `path_tracing_rng.hlsli` but no task touches it |

---

## Recommendation

Fix Issues 1 and 2 before implementation. Both are one-line changes. Fix Issue 3 during implementation for D3D12 spec compliance. Issues 4–7 are nice-to-have and can be addressed as code review follow-ups.

**Approved for implementation after Issues 1 and 2 are resolved in the plan.**
