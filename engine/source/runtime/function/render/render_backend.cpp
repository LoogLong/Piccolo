#include "runtime/function/render/render_backend.h"

#include <algorithm>
#include <cctype>

namespace Piccolo
{
    namespace
    {
        std::string toLowerCopy(const std::string& value)
        {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return lower;
        }
    } // namespace

    RHIBackendType parseRenderBackend(const std::string& backend)
    {
        const std::string lower = toLowerCopy(backend);
        if (lower == "vulkan")
        {
            return RHIBackendType::Vulkan;
        }
        if (lower == "d3d12" || lower == "dx12")
        {
            return RHIBackendType::D3D12;
        }
        return RHIBackendType::Auto;
    }

    const char* renderBackendToString(RHIBackendType backend)
    {
        switch (backend)
        {
            case RHIBackendType::Vulkan:
                return "Vulkan";
            case RHIBackendType::D3D12:
                return "D3D12";
            case RHIBackendType::Auto:
            default:
                return "Auto";
        }
    }

    RHIBackendType getPlatformDefaultRenderBackend()
    {
#ifdef _WIN32
        return RHIBackendType::D3D12;
#else
        return RHIBackendType::Vulkan;
#endif
    }
} // namespace Piccolo
