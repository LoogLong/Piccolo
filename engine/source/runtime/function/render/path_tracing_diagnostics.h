#pragma once

#include "runtime/function/render/render_pipeline_base.h"
#include "runtime/function/render/interface/rhi.h"

namespace Piccolo
{
    void logPathTracingReadinessReport(const RHI& rhi, const RenderPipelineBase& pipeline);
} // namespace Piccolo
