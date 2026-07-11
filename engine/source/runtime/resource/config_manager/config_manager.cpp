#include "runtime/resource/config_manager/config_manager.h"

#include "runtime/engine.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <string>
#include <cctype>
#include <cstdlib>

namespace Piccolo
{
    namespace
    {
        bool toBoolWithDefault(const std::string& value, bool default_value)
        {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (lower == "1" || lower == "true" || lower == "yes" || lower == "on")
            {
                return true;
            }

            if (lower == "0" || lower == "false" || lower == "no" || lower == "off")
            {
                return false;
            }

            return default_value;
        }

        // Plan Task 3 Step 4: the config manager is a hardcoded if/else chain
        // with no generic KV parsing, so unknown keys are silently dropped and
        // there is no integer parser. Add toUint here (returns default on
        // malformed input) so PathTracingMaxBounces / MaxPathIntensity can be
        // added without losing the silent-drop behavior for unrelated keys.
        uint32_t toUint(const std::string& value, uint32_t default_value)
        {
            if (value.empty()) return default_value;
            // strtoul skips leading whitespace and accepts the integer until
            // any non-digit, which is what we want for a plain numeric value.
            char* end = nullptr;
            errno = 0;
            unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
            if (end == value.c_str() || errno != 0) return default_value;
            return static_cast<uint32_t>(parsed);
        }

        float toFloat(const std::string& value, float default_value)
        {
            if (value.empty()) return default_value;
            char* end = nullptr;
            errno = 0;
            float parsed = std::strtof(value.c_str(), &end);
            if (end == value.c_str() || errno != 0) return default_value;
            return parsed;
        }
    } // namespace

    void ConfigManager::initialize(const std::filesystem::path& config_file_path)
    {
        // read configs
        std::ifstream config_file(config_file_path);
        std::string   config_line;
        while (std::getline(config_file, config_line))
        {
            size_t seperate_pos = config_line.find_first_of('=');
            if (seperate_pos > 0 && seperate_pos < (config_line.length() - 1))
            {
                std::string name  = config_line.substr(0, seperate_pos);
                std::string value = config_line.substr(seperate_pos + 1, config_line.length() - seperate_pos - 1);
                if (name == "BinaryRootFolder")
                {
                    m_root_folder = config_file_path.parent_path() / value;
                }
                else if (name == "AssetFolder")
                {
                    m_asset_folder = m_root_folder / value;
                }
                else if (name == "SchemaFolder")
                {
                    m_schema_folder = m_root_folder / value;
                }
                else if (name == "DefaultWorld")
                {
                    m_default_world_url = value;
                }
                else if (name == "BigIconFile")
                {
                    m_editor_big_icon_path = m_root_folder / value;
                }
                else if (name == "SmallIconFile")
                {
                    m_editor_small_icon_path = m_root_folder / value;
                }
                else if (name == "FontFile")
                {
                    m_editor_font_path = m_root_folder / value;
                }
                else if (name == "GlobalRenderingRes")
                {
                    m_global_rendering_res_url = value;
                }
                else if (name == "GlobalParticleRes")
                {
                    m_global_particle_res_url = value;
                }
                else if (name == "RenderBackend")
                {
                    m_render_backend = value;
                }
                else if (name == "RenderSceneMode")
                {
                    m_render_scene_mode = value;
                }
                else if (name == "RenderBackendAllowFallback")
                {
                    m_render_backend_allow_fallback = toBoolWithDefault(value, true);
                }
                else if (name == "PathTracingMaxBounces")
                {
                    m_path_tracing_max_bounces = toUint(value, m_path_tracing_max_bounces);
                }
                else if (name == "PathTracingMaxPathIntensity")
                {
                    m_path_tracing_max_path_intensity = toUint(value, m_path_tracing_max_path_intensity);
                }
                else if (name == "PathTracingMaxSamplesPerFrame")
                {
                    m_path_tracing_max_samples_per_frame = toUint(value, m_path_tracing_max_samples_per_frame);
                }
                else if (name == "PathTracingDirectionalAngleDeg")
                {
                    m_path_tracing_directional_angle_deg = toFloat(value, m_path_tracing_directional_angle_deg);
                }
                else if (name == "PathTracingFpsBudget")
                {
                    // Plan §0 minimum FPS at the chosen tier's resolution.
                    m_path_tracing_fps_budget = toUint(value, m_path_tracing_fps_budget);
                }
                else if (name == "PathTracingVramBudgetMb")
                {
                    // Plan §0 peak VRAM budget (MB).
                    m_path_tracing_vram_budget_mb = toUint(value, m_path_tracing_vram_budget_mb);
                }
                else if (name == "PathTracingConvergenceBudgetS")
                {
                    // Plan §0 still-camera time-to-SSIM>=0.95 budget (seconds).
                    m_path_tracing_convergence_budget_s = toFloat(value, m_path_tracing_convergence_budget_s);
                }
#ifdef ENABLE_PHYSICS_DEBUG_RENDERER
                else if (name == "JoltAssetFolder")
                {
                    m_jolt_physics_asset_folder = m_root_folder / value;
                }
#endif
            }
        }
    }

    const std::filesystem::path& ConfigManager::getRootFolder() const { return m_root_folder; }

    const std::filesystem::path& ConfigManager::getAssetFolder() const { return m_asset_folder; }

    const std::filesystem::path& ConfigManager::getSchemaFolder() const { return m_schema_folder; }

    const std::filesystem::path& ConfigManager::getEditorBigIconPath() const { return m_editor_big_icon_path; }

    const std::filesystem::path& ConfigManager::getEditorSmallIconPath() const { return m_editor_small_icon_path; }

    const std::filesystem::path& ConfigManager::getEditorFontPath() const { return m_editor_font_path; }

    const std::string& ConfigManager::getDefaultWorldUrl() const { return m_default_world_url; }

    const std::string& ConfigManager::getGlobalRenderingResUrl() const { return m_global_rendering_res_url; }

    const std::string& ConfigManager::getGlobalParticleResUrl() const { return m_global_particle_res_url; }

    const std::string& ConfigManager::getRenderBackend() const { return m_render_backend; }

    const std::string& ConfigManager::getRenderSceneMode() const { return m_render_scene_mode; }

    bool ConfigManager::getRenderBackendAllowFallback() const { return m_render_backend_allow_fallback; }

#ifdef ENABLE_PHYSICS_DEBUG_RENDERER
    const std::filesystem::path& ConfigManager::getJoltPhysicsAssetFolder() const { return m_jolt_physics_asset_folder; }
#endif

} // namespace Piccolo
