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

#if PICCOLO_ENABLE_VULKAN_BACKEND && defined(__has_include)
#if __has_include(<axis_frag.h>)
#include <axis_frag.h>
#define PICCOLO_VULKAN_AXIS_FRAG AXIS_FRAG
#else
#define PICCOLO_VULKAN_AXIS_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<axis_vert.h>)
#include <axis_vert.h>
#define PICCOLO_VULKAN_AXIS_VERT AXIS_VERT
#else
#define PICCOLO_VULKAN_AXIS_VERT ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<color_grading_frag.h>)
#include <color_grading_frag.h>
#define PICCOLO_VULKAN_COLOR_GRADING_FRAG COLOR_GRADING_FRAG
#else
#define PICCOLO_VULKAN_COLOR_GRADING_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<combine_ui_frag.h>)
#include <combine_ui_frag.h>
#define PICCOLO_VULKAN_COMBINE_UI_FRAG COMBINE_UI_FRAG
#else
#define PICCOLO_VULKAN_COMBINE_UI_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<debugdraw_frag.h>)
#include <debugdraw_frag.h>
#define PICCOLO_VULKAN_DEBUGDRAW_FRAG DEBUGDRAW_FRAG
#else
#define PICCOLO_VULKAN_DEBUGDRAW_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<debugdraw_vert.h>)
#include <debugdraw_vert.h>
#define PICCOLO_VULKAN_DEBUGDRAW_VERT DEBUGDRAW_VERT
#else
#define PICCOLO_VULKAN_DEBUGDRAW_VERT ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<deferred_lighting_frag.h>)
#include <deferred_lighting_frag.h>
#define PICCOLO_VULKAN_DEFERRED_LIGHTING_FRAG DEFERRED_LIGHTING_FRAG
#else
#define PICCOLO_VULKAN_DEFERRED_LIGHTING_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<deferred_lighting_vert.h>)
#include <deferred_lighting_vert.h>
#define PICCOLO_VULKAN_DEFERRED_LIGHTING_VERT DEFERRED_LIGHTING_VERT
#else
#define PICCOLO_VULKAN_DEFERRED_LIGHTING_VERT ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<fxaa_frag.h>)
#include <fxaa_frag.h>
#define PICCOLO_VULKAN_FXAA_FRAG FXAA_FRAG
#else
#define PICCOLO_VULKAN_FXAA_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<fxaa_vert.h>)
#include <fxaa_vert.h>
#define PICCOLO_VULKAN_FXAA_VERT FXAA_VERT
#else
#define PICCOLO_VULKAN_FXAA_VERT ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<mesh_directional_light_shadow_frag.h>)
#include <mesh_directional_light_shadow_frag.h>
#define PICCOLO_VULKAN_MESH_DIRECTIONAL_LIGHT_SHADOW_FRAG MESH_DIRECTIONAL_LIGHT_SHADOW_FRAG
#else
#define PICCOLO_VULKAN_MESH_DIRECTIONAL_LIGHT_SHADOW_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<mesh_directional_light_shadow_vert.h>)
#include <mesh_directional_light_shadow_vert.h>
#define PICCOLO_VULKAN_MESH_DIRECTIONAL_LIGHT_SHADOW_VERT MESH_DIRECTIONAL_LIGHT_SHADOW_VERT
#else
#define PICCOLO_VULKAN_MESH_DIRECTIONAL_LIGHT_SHADOW_VERT ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<mesh_frag.h>)
#include <mesh_frag.h>
#define PICCOLO_VULKAN_MESH_FRAG MESH_FRAG
#else
#define PICCOLO_VULKAN_MESH_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<mesh_gbuffer_frag.h>)
#include <mesh_gbuffer_frag.h>
#define PICCOLO_VULKAN_MESH_GBUFFER_FRAG MESH_GBUFFER_FRAG
#else
#define PICCOLO_VULKAN_MESH_GBUFFER_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<mesh_inefficient_pick_frag.h>)
#include <mesh_inefficient_pick_frag.h>
#define PICCOLO_VULKAN_MESH_INEFFICIENT_PICK_FRAG MESH_INEFFICIENT_PICK_FRAG
#else
#define PICCOLO_VULKAN_MESH_INEFFICIENT_PICK_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<mesh_inefficient_pick_vert.h>)
#include <mesh_inefficient_pick_vert.h>
#define PICCOLO_VULKAN_MESH_INEFFICIENT_PICK_VERT MESH_INEFFICIENT_PICK_VERT
#else
#define PICCOLO_VULKAN_MESH_INEFFICIENT_PICK_VERT ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<mesh_point_light_shadow_frag.h>)
#include <mesh_point_light_shadow_frag.h>
#define PICCOLO_VULKAN_MESH_POINT_LIGHT_SHADOW_FRAG MESH_POINT_LIGHT_SHADOW_FRAG
#else
#define PICCOLO_VULKAN_MESH_POINT_LIGHT_SHADOW_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<mesh_point_light_shadow_geom.h>)
#include <mesh_point_light_shadow_geom.h>
#define PICCOLO_VULKAN_MESH_POINT_LIGHT_SHADOW_GEOM MESH_POINT_LIGHT_SHADOW_GEOM
#else
#define PICCOLO_VULKAN_MESH_POINT_LIGHT_SHADOW_GEOM ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<mesh_point_light_shadow_vert.h>)
#include <mesh_point_light_shadow_vert.h>
#define PICCOLO_VULKAN_MESH_POINT_LIGHT_SHADOW_VERT MESH_POINT_LIGHT_SHADOW_VERT
#else
#define PICCOLO_VULKAN_MESH_POINT_LIGHT_SHADOW_VERT ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<mesh_vert.h>)
#include <mesh_vert.h>
#define PICCOLO_VULKAN_MESH_VERT MESH_VERT
#else
#define PICCOLO_VULKAN_MESH_VERT ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<particle_emit_comp.h>)
#include <particle_emit_comp.h>
#define PICCOLO_VULKAN_PARTICLE_EMIT_COMP PARTICLE_EMIT_COMP
#else
#define PICCOLO_VULKAN_PARTICLE_EMIT_COMP ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<particle_kickoff_comp.h>)
#include <particle_kickoff_comp.h>
#define PICCOLO_VULKAN_PARTICLE_KICKOFF_COMP PARTICLE_KICKOFF_COMP
#else
#define PICCOLO_VULKAN_PARTICLE_KICKOFF_COMP ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<particle_simulate_comp.h>)
#include <particle_simulate_comp.h>
#define PICCOLO_VULKAN_PARTICLE_SIMULATE_COMP PARTICLE_SIMULATE_COMP
#else
#define PICCOLO_VULKAN_PARTICLE_SIMULATE_COMP ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<gpu_skinning_comp.h>)
#include <gpu_skinning_comp.h>
#define PICCOLO_VULKAN_GPU_SKINNING_COMP GPU_SKINNING_COMP
#else
#define PICCOLO_VULKAN_GPU_SKINNING_COMP ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<particlebillboard_frag.h>)
#include <particlebillboard_frag.h>
#define PICCOLO_VULKAN_PARTICLEBILLBOARD_FRAG PARTICLEBILLBOARD_FRAG
#else
#define PICCOLO_VULKAN_PARTICLEBILLBOARD_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<particlebillboard_vert.h>)
#include <particlebillboard_vert.h>
#define PICCOLO_VULKAN_PARTICLEBILLBOARD_VERT PARTICLEBILLBOARD_VERT
#else
#define PICCOLO_VULKAN_PARTICLEBILLBOARD_VERT ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<post_process_vert.h>)
#include <post_process_vert.h>
#define PICCOLO_VULKAN_POST_PROCESS_VERT POST_PROCESS_VERT
#else
#define PICCOLO_VULKAN_POST_PROCESS_VERT ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<skybox_frag.h>)
#include <skybox_frag.h>
#define PICCOLO_VULKAN_SKYBOX_FRAG SKYBOX_FRAG
#else
#define PICCOLO_VULKAN_SKYBOX_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<skybox_vert.h>)
#include <skybox_vert.h>
#define PICCOLO_VULKAN_SKYBOX_VERT SKYBOX_VERT
#else
#define PICCOLO_VULKAN_SKYBOX_VERT ::Piccolo::emptyVulkanShaderBytecode()
#endif
#if __has_include(<tone_mapping_frag.h>)
#include <tone_mapping_frag.h>
#define PICCOLO_VULKAN_TONE_MAPPING_FRAG TONE_MAPPING_FRAG
#else
#define PICCOLO_VULKAN_TONE_MAPPING_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#endif
#else
#define PICCOLO_VULKAN_AXIS_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_AXIS_VERT ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_COLOR_GRADING_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_COMBINE_UI_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_DEBUGDRAW_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_DEBUGDRAW_VERT ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_DEFERRED_LIGHTING_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_DEFERRED_LIGHTING_VERT ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_FXAA_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_FXAA_VERT ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_MESH_DIRECTIONAL_LIGHT_SHADOW_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_MESH_DIRECTIONAL_LIGHT_SHADOW_VERT ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_MESH_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_MESH_GBUFFER_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_MESH_INEFFICIENT_PICK_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_MESH_INEFFICIENT_PICK_VERT ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_MESH_POINT_LIGHT_SHADOW_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_MESH_POINT_LIGHT_SHADOW_GEOM ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_MESH_POINT_LIGHT_SHADOW_VERT ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_MESH_VERT ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_PARTICLE_EMIT_COMP ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_PARTICLE_KICKOFF_COMP ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_PARTICLE_SIMULATE_COMP ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_PARTICLEBILLBOARD_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_PARTICLEBILLBOARD_VERT ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_POST_PROCESS_VERT ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_SKYBOX_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_SKYBOX_VERT ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_TONE_MAPPING_FRAG ::Piccolo::emptyVulkanShaderBytecode()
#define PICCOLO_VULKAN_GPU_SKINNING_COMP ::Piccolo::emptyVulkanShaderBytecode()
#endif

#if PICCOLO_ENABLE_D3D12_BACKEND && PICCOLO_D3D12_SHADER_BYTECODE_AVAILABLE != 0 && defined(__has_include)
#if __has_include(<dxil_cpp/axis_frag.h>)
#include <dxil_cpp/axis_frag.h>
#define PICCOLO_D3D12_AXIS_FRAG D3D12_AXIS_FRAG
#else
#define PICCOLO_D3D12_AXIS_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/axis_vert.h>)
#include <dxil_cpp/axis_vert.h>
#define PICCOLO_D3D12_AXIS_VERT D3D12_AXIS_VERT
#else
#define PICCOLO_D3D12_AXIS_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/color_grading_frag.h>)
#include <dxil_cpp/color_grading_frag.h>
#define PICCOLO_D3D12_COLOR_GRADING_FRAG D3D12_COLOR_GRADING_FRAG
#else
#define PICCOLO_D3D12_COLOR_GRADING_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/combine_ui_frag.h>)
#include <dxil_cpp/combine_ui_frag.h>
#define PICCOLO_D3D12_COMBINE_UI_FRAG D3D12_COMBINE_UI_FRAG
#else
#define PICCOLO_D3D12_COMBINE_UI_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/debugdraw_frag.h>)
#include <dxil_cpp/debugdraw_frag.h>
#define PICCOLO_D3D12_DEBUGDRAW_FRAG D3D12_DEBUGDRAW_FRAG
#else
#define PICCOLO_D3D12_DEBUGDRAW_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/debugdraw_vert.h>)
#include <dxil_cpp/debugdraw_vert.h>
#define PICCOLO_D3D12_DEBUGDRAW_VERT D3D12_DEBUGDRAW_VERT
#else
#define PICCOLO_D3D12_DEBUGDRAW_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/deferred_lighting_frag.h>)
#include <dxil_cpp/deferred_lighting_frag.h>
#define PICCOLO_D3D12_DEFERRED_LIGHTING_FRAG D3D12_DEFERRED_LIGHTING_FRAG
#else
#define PICCOLO_D3D12_DEFERRED_LIGHTING_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/deferred_lighting_vert.h>)
#include <dxil_cpp/deferred_lighting_vert.h>
#define PICCOLO_D3D12_DEFERRED_LIGHTING_VERT D3D12_DEFERRED_LIGHTING_VERT
#else
#define PICCOLO_D3D12_DEFERRED_LIGHTING_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/fxaa_frag.h>)
#include <dxil_cpp/fxaa_frag.h>
#define PICCOLO_D3D12_FXAA_FRAG D3D12_FXAA_FRAG
#else
#define PICCOLO_D3D12_FXAA_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/fxaa_vert.h>)
#include <dxil_cpp/fxaa_vert.h>
#define PICCOLO_D3D12_FXAA_VERT D3D12_FXAA_VERT
#else
#define PICCOLO_D3D12_FXAA_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/mesh_directional_light_shadow_frag.h>)
#include <dxil_cpp/mesh_directional_light_shadow_frag.h>
#define PICCOLO_D3D12_MESH_DIRECTIONAL_LIGHT_SHADOW_FRAG D3D12_MESH_DIRECTIONAL_LIGHT_SHADOW_FRAG
#else
#define PICCOLO_D3D12_MESH_DIRECTIONAL_LIGHT_SHADOW_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/mesh_directional_light_shadow_vert.h>)
#include <dxil_cpp/mesh_directional_light_shadow_vert.h>
#define PICCOLO_D3D12_MESH_DIRECTIONAL_LIGHT_SHADOW_VERT D3D12_MESH_DIRECTIONAL_LIGHT_SHADOW_VERT
#else
#define PICCOLO_D3D12_MESH_DIRECTIONAL_LIGHT_SHADOW_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/mesh_frag.h>)
#include <dxil_cpp/mesh_frag.h>
#define PICCOLO_D3D12_MESH_FRAG D3D12_MESH_FRAG
#else
#define PICCOLO_D3D12_MESH_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/mesh_gbuffer_frag.h>)
#include <dxil_cpp/mesh_gbuffer_frag.h>
#define PICCOLO_D3D12_MESH_GBUFFER_FRAG D3D12_MESH_GBUFFER_FRAG
#else
#define PICCOLO_D3D12_MESH_GBUFFER_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/mesh_inefficient_pick_frag.h>)
#include <dxil_cpp/mesh_inefficient_pick_frag.h>
#define PICCOLO_D3D12_MESH_INEFFICIENT_PICK_FRAG D3D12_MESH_INEFFICIENT_PICK_FRAG
#else
#define PICCOLO_D3D12_MESH_INEFFICIENT_PICK_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/mesh_inefficient_pick_vert.h>)
#include <dxil_cpp/mesh_inefficient_pick_vert.h>
#define PICCOLO_D3D12_MESH_INEFFICIENT_PICK_VERT D3D12_MESH_INEFFICIENT_PICK_VERT
#else
#define PICCOLO_D3D12_MESH_INEFFICIENT_PICK_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/mesh_point_light_shadow_frag.h>)
#include <dxil_cpp/mesh_point_light_shadow_frag.h>
#define PICCOLO_D3D12_MESH_POINT_LIGHT_SHADOW_FRAG D3D12_MESH_POINT_LIGHT_SHADOW_FRAG
#else
#define PICCOLO_D3D12_MESH_POINT_LIGHT_SHADOW_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/mesh_point_light_shadow_geom.h>)
#include <dxil_cpp/mesh_point_light_shadow_geom.h>
#define PICCOLO_D3D12_MESH_POINT_LIGHT_SHADOW_GEOM D3D12_MESH_POINT_LIGHT_SHADOW_GEOM
#else
#define PICCOLO_D3D12_MESH_POINT_LIGHT_SHADOW_GEOM ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/mesh_point_light_shadow_vert.h>)
#include <dxil_cpp/mesh_point_light_shadow_vert.h>
#define PICCOLO_D3D12_MESH_POINT_LIGHT_SHADOW_VERT D3D12_MESH_POINT_LIGHT_SHADOW_VERT
#else
#define PICCOLO_D3D12_MESH_POINT_LIGHT_SHADOW_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/mesh_vert.h>)
#include <dxil_cpp/mesh_vert.h>
#define PICCOLO_D3D12_MESH_VERT D3D12_MESH_VERT
#else
#define PICCOLO_D3D12_MESH_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/particle_emit_comp.h>)
#include <dxil_cpp/particle_emit_comp.h>
#define PICCOLO_D3D12_PARTICLE_EMIT_COMP D3D12_PARTICLE_EMIT_COMP
#else
#define PICCOLO_D3D12_PARTICLE_EMIT_COMP ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/particle_kickoff_comp.h>)
#include <dxil_cpp/particle_kickoff_comp.h>
#define PICCOLO_D3D12_PARTICLE_KICKOFF_COMP D3D12_PARTICLE_KICKOFF_COMP
#else
#define PICCOLO_D3D12_PARTICLE_KICKOFF_COMP ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/particle_simulate_comp.h>)
#include <dxil_cpp/particle_simulate_comp.h>
#define PICCOLO_D3D12_PARTICLE_SIMULATE_COMP D3D12_PARTICLE_SIMULATE_COMP
#else
#define PICCOLO_D3D12_PARTICLE_SIMULATE_COMP ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/particlebillboard_frag.h>)
#include <dxil_cpp/particlebillboard_frag.h>
#define PICCOLO_D3D12_PARTICLEBILLBOARD_FRAG D3D12_PARTICLEBILLBOARD_FRAG
#else
#define PICCOLO_D3D12_PARTICLEBILLBOARD_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/particlebillboard_vert.h>)
#include <dxil_cpp/particlebillboard_vert.h>
#define PICCOLO_D3D12_PARTICLEBILLBOARD_VERT D3D12_PARTICLEBILLBOARD_VERT
#else
#define PICCOLO_D3D12_PARTICLEBILLBOARD_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/gpu_skinning_comp.h>)
#include <dxil_cpp/gpu_skinning_comp.h>
#define PICCOLO_D3D12_GPU_SKINNING_COMP D3D12_GPU_SKINNING_COMP
#else
#define PICCOLO_D3D12_GPU_SKINNING_COMP ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/path_tracing_lib.h>)
#include <dxil_cpp/path_tracing_lib.h>
#define PICCOLO_D3D12_PATH_TRACING_LIB D3D12_PATH_TRACING_LIB
#else
#define PICCOLO_D3D12_PATH_TRACING_LIB ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/post_process_vert.h>)
#include <dxil_cpp/post_process_vert.h>
#define PICCOLO_D3D12_POST_PROCESS_VERT D3D12_POST_PROCESS_VERT
#else
#define PICCOLO_D3D12_POST_PROCESS_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/skybox_frag.h>)
#include <dxil_cpp/skybox_frag.h>
#define PICCOLO_D3D12_SKYBOX_FRAG D3D12_SKYBOX_FRAG
#else
#define PICCOLO_D3D12_SKYBOX_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/skybox_vert.h>)
#include <dxil_cpp/skybox_vert.h>
#define PICCOLO_D3D12_SKYBOX_VERT D3D12_SKYBOX_VERT
#else
#define PICCOLO_D3D12_SKYBOX_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#if __has_include(<dxil_cpp/tone_mapping_frag.h>)
#include <dxil_cpp/tone_mapping_frag.h>
#define PICCOLO_D3D12_TONE_MAPPING_FRAG D3D12_TONE_MAPPING_FRAG
#else
#define PICCOLO_D3D12_TONE_MAPPING_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#endif
#else
#define PICCOLO_D3D12_AXIS_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_AXIS_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_COLOR_GRADING_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_COMBINE_UI_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_DEBUGDRAW_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_DEBUGDRAW_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_DEFERRED_LIGHTING_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_DEFERRED_LIGHTING_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_FXAA_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_FXAA_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_MESH_DIRECTIONAL_LIGHT_SHADOW_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_MESH_DIRECTIONAL_LIGHT_SHADOW_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_MESH_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_MESH_GBUFFER_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_MESH_INEFFICIENT_PICK_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_MESH_INEFFICIENT_PICK_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_MESH_POINT_LIGHT_SHADOW_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_MESH_POINT_LIGHT_SHADOW_GEOM ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_MESH_POINT_LIGHT_SHADOW_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_MESH_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_PARTICLE_EMIT_COMP ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_PARTICLE_KICKOFF_COMP ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_PARTICLE_SIMULATE_COMP ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_PARTICLEBILLBOARD_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_PARTICLEBILLBOARD_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_PATH_TRACING_LIB ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_GPU_SKINNING_COMP ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_POST_PROCESS_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_SKYBOX_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_SKYBOX_VERT ::Piccolo::emptyD3D12ShaderBytecode()
#define PICCOLO_D3D12_TONE_MAPPING_FRAG ::Piccolo::emptyD3D12ShaderBytecode()
#endif

#define PICCOLO_RENDER_SHADER_BYTECODE_CONCAT_DETAIL(prefix, shader) prefix##shader
#define PICCOLO_RENDER_SHADER_BYTECODE_CONCAT(prefix, shader) \
    PICCOLO_RENDER_SHADER_BYTECODE_CONCAT_DETAIL(prefix, shader)
#define PICCOLO_RENDER_VULKAN_SHADER_BYTECODE(shader) PICCOLO_RENDER_SHADER_BYTECODE_CONCAT(PICCOLO_VULKAN_, shader)
#define PICCOLO_RENDER_D3D12_SHADER_BYTECODE(shader) PICCOLO_RENDER_SHADER_BYTECODE_CONCAT(PICCOLO_D3D12_, shader)
#define PICCOLO_RENDER_SHADER_BYTECODE(rhi, shader) \
    (::Piccolo::selectRenderShaderBytecode( \
        (rhi)->getBackendType(), PICCOLO_RENDER_VULKAN_SHADER_BYTECODE(shader), PICCOLO_RENDER_D3D12_SHADER_BYTECODE(shader)))
