#include "runtime/function/render/path_tracing_diagnostics.h"

#include "runtime/core/base/macro.h"
#include "runtime/function/render/render_backend.h"
#include "runtime/function/render/render_shader_bytecode.h"

namespace Piccolo
{
    namespace
    {
        const char* sceneModeLabel(RenderSceneRenderMode mode)
        {
            return mode == RenderSceneRenderMode::PathTracing ? "PathTracing" : "Raster";
        }

        bool gpuSkinningBytecodeAvailable(const RHI& rhi)
        {
            return !PICCOLO_RENDER_SHADER_BYTECODE(&rhi, GPU_SKINNING_COMP).empty();
        }

        void logCheck(const char* name, bool ok, const char* fail_detail)
        {
            if (ok)
            {
                LOG_INFO("  [ok]   {}", name);
            }
            else
            {
                LOG_WARN("  [fail] {} — {}", name, fail_detail);
            }
        }
    } // namespace

    void logPathTracingReadinessReport(const RHI& rhi, const RenderPipelineBase& pipeline)
    {
        const bool ray_tracing_supported     = rhi.supportsRayTracing();
        const bool path_tracing_lib          = pathTracingBytecodeAvailable(rhi);
        const bool gpu_skinning_lib          = gpuSkinningBytecodeAvailable(rhi);
        const bool path_tracing_ready        = supportsPathTracing(rhi);
        const RenderSceneRenderMode requested = pipeline.getRequestedSceneRenderMode();
        const RenderSceneRenderMode effective = pipeline.getEffectiveSceneRenderMode();

        LOG_INFO("Path tracing readiness report:");
        LOG_INFO("  backend: {}", renderBackendToString(rhi.getBackendType()));
        LOG_INFO("  requested_mode: {}", sceneModeLabel(requested));

        if (ray_tracing_supported)
        {
            logCheck("ray_tracing_extensions", true, nullptr);
        }
        else if (rhi.getBackendType() == RHIBackendType::Vulkan)
        {
            logCheck("ray_tracing_extensions",
                     false,
                     "VK_KHR_ray_tracing_pipeline or related extensions unavailable on this device");
        }
        else
        {
            logCheck("ray_tracing_extensions", false, "backend reports ray tracing unsupported");
        }

        if (path_tracing_lib)
        {
            logCheck("shader_bytecode.path_tracing_lib", true, nullptr);
        }
        else if (rhi.getBackendType() == RHIBackendType::Vulkan)
        {
            logCheck("shader_bytecode.path_tracing_lib",
                     false,
                     "SPIR-V missing — rebuild with PICCOLO_DXC_EXECUTABLE set; "
                     "cmake --build build --target PiccoloShaderCompile");
        }
        else
        {
            logCheck("shader_bytecode.path_tracing_lib",
                     false,
                     "DXIL missing — install dxc and rebuild PiccoloShaderCompile");
        }

        if (gpu_skinning_lib)
        {
            logCheck("shader_bytecode.gpu_skinning", true, nullptr);
        }
        else
        {
            logCheck("shader_bytecode.gpu_skinning",
                     false,
                     "compute shader bytecode unavailable for active backend");
        }

        LOG_INFO("  path_tracing_ready: {}", path_tracing_ready);
        LOG_INFO("  effective_mode: {}", sceneModeLabel(effective));

        if (requested == RenderSceneRenderMode::PathTracing && effective == RenderSceneRenderMode::Raster)
        {
            if (!ray_tracing_supported)
            {
                LOG_WARN("  action: ray tracing unavailable — PathTracing falls back to Raster");
            }
            else if (!path_tracing_lib)
            {
                LOG_WARN("  action: rebuild shaders (PiccoloShaderCompile) then restart the editor");
            }
            else if (pipeline.hasPathTracingRuntimeFallback())
            {
                LOG_WARN("  action: runtime dispatch failed earlier — check prior PATH_TRACING warnings");
            }
        }
    }
} // namespace Piccolo
