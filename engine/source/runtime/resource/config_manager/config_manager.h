#pragma once

#include <filesystem>
#include <string>

namespace Piccolo
{
    struct EngineInitParams;

    class ConfigManager
    {
    public:
        void initialize(const std::filesystem::path& config_file_path);

        const std::filesystem::path& getRootFolder() const;
        const std::filesystem::path& getAssetFolder() const;
        const std::filesystem::path& getSchemaFolder() const;
        const std::filesystem::path& getEditorBigIconPath() const;
        const std::filesystem::path& getEditorSmallIconPath() const;
        const std::filesystem::path& getEditorFontPath() const;

#ifdef ENABLE_PHYSICS_DEBUG_RENDERER
        const std::filesystem::path& getJoltPhysicsAssetFolder() const;
#endif

        const std::string& getDefaultWorldUrl() const;
        const std::string& getGlobalRenderingResUrl() const;
        const std::string& getGlobalParticleResUrl() const;
        const std::string& getRenderBackend() const;
        const std::string& getRenderSceneMode() const;
        bool               getRenderBackendAllowFallback() const;

        // Path tracing tunables (plan Task 3 Step 4). Defaults are set so the
        // path tracer degrades gracefully when no config key is present.
        uint32_t            getPathTracingMaxBounces() const { return m_path_tracing_max_bounces; }
        uint32_t            getPathTracingMaxPathIntensity() const { return m_path_tracing_max_path_intensity; }
        // Paths traced per dispatch (plan Task 3 Step 4 / Phase 2.1 of the
        // optimization plan). Default 1 = one sample per frame. Set to >1
        // to compress more samples per second (FPS drops proportionally;
        // useful for offline-quality snapshots from a still viewpoint).
        uint32_t            getPathTracingMaxSamplesPerFrame() const { return m_path_tracing_max_samples_per_frame; }
        float               getPathTracingDirectionalAngleDeg() const { return m_path_tracing_directional_angle_deg; }

        // Tier-1 performance budget (plan 2026-07-12 §0). The hard numbers
        // gate tier acceptance: any regression beyond the threshold is a
        // blocker for merging. Stored as separate ini keys so the budget
        // travels with the project's `*.ini` and is comparable across runs.
        //   fps_budget          : minimum target FPS at the tier's chosen res.
        //   vram_budget_mb      : peak VRAM budget (incl. lights, accum, AOV).
        //   convergence_budget_s: still-camera SSIM >= 0.95 within this many
        //                         seconds. Default 1.0s aligns with RTX 4070.
        // Defaults match plan §0: RTX 3060 30 FPS / 1.5 GB / 1.5s;
        //                          RTX 4070 60 FPS / 2.0 GB / 1.0s.
        // 30/1500/1.5 is the lowest common denominator that both tiers pass.
        uint32_t getPathTracingFpsBudget() const { return m_path_tracing_fps_budget; }
        uint32_t getPathTracingVramBudgetMb() const { return m_path_tracing_vram_budget_mb; }
        float    getPathTracingConvergenceBudgetS() const { return m_path_tracing_convergence_budget_s; }

        // Tier-1 quality preset (plan 2026-07-12 §3). 0 = Performance (default;
        // 1 spp + strong denoiser, 60+ FPS), 1 = Balanced (2 spp + medium
        // denoiser, 30-60 FPS), 2 = Quality (4 spp + weak denoiser, 15-30
        // FPS), 3 = Interactive (1/2 spp + very strong denoiser, 60+ FPS for
        // first-frame / fast-pan).
        uint32_t getPathTracingQualityPreset() const { return m_path_tracing_quality_preset; }

        // Bug fix 2026-07-12 (plan "fix B"): raster pipeline's
        // directional_light.color is a unit-candela direction-light value
        // tuned for deferred shading (1.0 ~= noon sun at the unit used by
        // mesh.frag). PT at 1 spp/frame with a 0.53 deg soft-sun cone
        // samples the sun cone extremely rarely -- the cube of unit
        // contribution to a hit is rarely reached. Multiply the
        // directional-light color by this scale inside the PT light buffer
        // only (raster is untouched) so a sun that *is* sampled by NEE
        // contributes a physically meaningful amount. Default 5.0 is a
        // hand-tuned starting point; tune per scene by raising the value
        // and watching the sun-lit wall brightness in the editor.
        float getPathTracingSunIrradianceScale() const { return m_path_tracing_sun_irradiance_scale; }

    private:
        std::filesystem::path m_root_folder;
        std::filesystem::path m_asset_folder;
        std::filesystem::path m_schema_folder;
        std::filesystem::path m_editor_big_icon_path;
        std::filesystem::path m_editor_small_icon_path;
        std::filesystem::path m_editor_font_path;

#ifdef ENABLE_PHYSICS_DEBUG_RENDERER
        std::filesystem::path m_jolt_physics_asset_folder;
#endif

        std::string m_default_world_url;
        std::string m_global_rendering_res_url;
        std::string m_global_particle_res_url;
        std::string m_render_backend {"Auto"};
        std::string m_render_scene_mode {"Raster"};
        bool        m_render_backend_allow_fallback {true};

        // Defaults: 8 bounces is enough for most indoor/outdoor scenes to
        // converge; a 0.0 angle falls back to the shader's hard-coded 0.53 deg
        // soft-sun default in path_tracing_pass.cpp.
        uint32_t    m_path_tracing_max_bounces {8u};
        uint32_t    m_path_tracing_max_path_intensity {100u};
        // spp per dispatch (= per frame in current sequential driver).
        uint32_t    m_path_tracing_max_samples_per_frame {1u};
        float       m_path_tracing_directional_angle_deg {0.53f};

        // Tier-1 performance budget defaults (plan 2026-07-12 §0). 30 FPS
        // keeps the conservative RTX 3060 number; 1500 MB VRAM and 1.5s
        // convergence are the budget floor both 3060/4070 must pass.
        uint32_t    m_path_tracing_fps_budget {30u};
        uint32_t    m_path_tracing_vram_budget_mb {1500u};
        float       m_path_tracing_convergence_budget_s {1.5f};
        // Default Performance = 1 spp + strong denoiser; matches the "1
        // sample per frame" baseline that the rest of the renderer expects.
        uint32_t    m_path_tracing_quality_preset {0u};
        // PT sun irradiance scale (see getPathTracingSunIrradianceScale doc).
        float       m_path_tracing_sun_irradiance_scale {5.0f};
    };
} // namespace Piccolo
