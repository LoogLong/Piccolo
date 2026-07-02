#pragma once

#include "runtime/function/render/interface/rhi.h"

#include <vector>

namespace Piccolo
{
#ifndef PICCOLO_ENABLE_VULKAN_BACKEND
#define PICCOLO_ENABLE_VULKAN_BACKEND 0
#endif

#ifndef PICCOLO_ENABLE_D3D12_BACKEND
#define PICCOLO_ENABLE_D3D12_BACKEND 0
#endif

#ifndef PICCOLO_D3D12_SHADER_BYTECODE_AVAILABLE
#define PICCOLO_D3D12_SHADER_BYTECODE_AVAILABLE 0
#endif

    struct RenderShaderBytecode
    {
        const std::vector<unsigned char>& spirv;
        const std::vector<unsigned char>& dxil;
    };

    inline bool d3d12ShaderBytecodeAvailable()
    {
        return PICCOLO_D3D12_SHADER_BYTECODE_AVAILABLE != 0;
    }

    inline const std::vector<unsigned char>& emptyVulkanShaderBytecode()
    {
        static const std::vector<unsigned char> empty;
        return empty;
    }

    inline const std::vector<unsigned char>& emptyD3D12ShaderBytecode()
    {
        static const std::vector<unsigned char> empty;
        return empty;
    }

    inline const std::vector<unsigned char>& selectRenderShaderBytecode(RHIBackendType backend,
                                                                        const RenderShaderBytecode& bytecode)
    {
        if (backend == RHIBackendType::D3D12)
        {
            return d3d12ShaderBytecodeAvailable() ? bytecode.dxil : emptyD3D12ShaderBytecode();
        }

        return bytecode.spirv;
    }

    inline const std::vector<unsigned char>& selectRenderShaderBytecode(RHIBackendType backend,
                                                                        const std::vector<unsigned char>& spirv,
                                                                        const std::vector<unsigned char>& dxil)
    {
        return selectRenderShaderBytecode(backend, RenderShaderBytecode {spirv, dxil});
    }

    inline const std::vector<unsigned char>& selectRenderShaderBytecode(const RHI& rhi,
                                                                        const RenderShaderBytecode& bytecode)
    {
        return selectRenderShaderBytecode(rhi.getBackendType(), bytecode);
    }
} // namespace Piccolo

#include <render_shader_bytecode_gen.h>

#define PICCOLO_RENDER_SHADER_BYTECODE_CONCAT_DETAIL(prefix, shader) prefix##shader
#define PICCOLO_RENDER_SHADER_BYTECODE_CONCAT(prefix, shader) \
    PICCOLO_RENDER_SHADER_BYTECODE_CONCAT_DETAIL(prefix, shader)
#define PICCOLO_RENDER_VULKAN_SHADER_BYTECODE(shader) PICCOLO_RENDER_SHADER_BYTECODE_CONCAT(PICCOLO_VULKAN_, shader)
#define PICCOLO_RENDER_D3D12_SHADER_BYTECODE(shader) PICCOLO_RENDER_SHADER_BYTECODE_CONCAT(PICCOLO_D3D12_, shader)
#define PICCOLO_RENDER_SHADER_BYTECODE(rhi, shader) \
    (::Piccolo::selectRenderShaderBytecode( \
        (rhi)->getBackendType(), PICCOLO_RENDER_VULKAN_SHADER_BYTECODE(shader), PICCOLO_RENDER_D3D12_SHADER_BYTECODE(shader)))

namespace Piccolo
{
    inline bool pathTracingBytecodeAvailable(const RHI& rhi)
    {
        return !PICCOLO_RENDER_SHADER_BYTECODE(&rhi, PATH_TRACING_LIB).empty();
    }

    inline bool supportsPathTracing(const RHI& rhi)
    {
        return rhi.supportsRayTracing() && pathTracingBytecodeAvailable(rhi);
    }
} // namespace Piccolo
