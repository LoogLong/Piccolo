#pragma once

#include <array>
#include <string>

#include "runtime/function/render/interface/rhi.h"

namespace Piccolo
{
    RHIBackendType parseRenderBackend(const std::string& backend);
    const char*    renderBackendToString(RHIBackendType backend);
    RHIBackendType getPlatformDefaultRenderBackend();
} // namespace Piccolo
