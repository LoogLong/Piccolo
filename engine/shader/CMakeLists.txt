set(TARGET_NAME ${SHADER_COMPILE_TARGET})

file(GLOB_RECURSE SHADER_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.vert"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.frag"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.comp"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.geom"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.tesc"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.tese"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.mesh"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.task")

file(GLOB_RECURSE HLSL_SHADER_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/*.vert.hlsl"
                                                       "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/*.frag.hlsl"
                                                       "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/*.comp.hlsl"
                                                       "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/*.geom.hlsl"
                                                       "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/*.lib.hlsl")
file(GLOB_RECURSE HLSL_SHADER_INCLUDE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/*.hlsli")

SOURCE_GROUP("shaders" FILES ${SHADER_FILES})
SOURCE_GROUP("hlsl" FILES ${HLSL_SHADER_FILES} ${HLSL_SHADER_INCLUDE_FILES})

set(SHADER_INCLUDE_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(HLSL_SHADER_INCLUDE_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/hlsl)

set(GENERATED_SHADER_FOLDER "generated")
if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER})
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER})
endif()

if (PICCOLO_ENABLE_VULKAN_BACKEND AND NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER}/spv)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER}/spv)
endif()

if (PICCOLO_ENABLE_VULKAN_BACKEND AND NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER}/cpp)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER}/cpp)
endif()

include(${PICCOLO_ROOT_DIR}/cmake/ShaderCompile.cmake)
include(${PICCOLO_ROOT_DIR}/cmake/GenerateRenderShaderBytecodeHeader.cmake)

set(HLSL_RT_SHADER_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/path_tracing.lib.hlsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/path_tracing_denoise.comp.hlsl"
  "${CMAKE_CURRENT_SOURCE_DIR}/hlsl/gpu_skinning.comp.hlsl")

set(RENDER_SHADER_BYTECODE_GEN_H
  "${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER}/render_shader_bytecode_gen.h")
generate_render_shader_bytecode_header(
  OUTPUT_HEADER "${RENDER_SHADER_BYTECODE_GEN_H}"
  GLSL_SHADERS "${SHADER_FILES}"
  HLSL_SHADERS "${HLSL_SHADER_FILES}"
  HLSL_RT_SHADERS "${HLSL_RT_SHADER_FILES}")

if(PICCOLO_ENABLE_VULKAN_BACKEND)
  compile_shader(
    "${SHADER_FILES}"
    "${TARGET_NAME}"
    "${SHADER_INCLUDE_FOLDER}"
    "${GENERATED_SHADER_FOLDER}"
    "${glslangValidator_executable}")

  # Vulkan ray tracing: same HLSL sources as D3D12, compiled to SPIR-V via SPIR-V-capable dxc.
  if(HLSL_RT_SHADER_FILES AND dxc_spirv_executable)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER}/vulkan_cpp)
      file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER}/vulkan_cpp)
    endif()
    compile_hlsl_to_spirv(
      "${HLSL_RT_SHADER_FILES}"
      "${TARGET_NAME}"
      "${HLSL_SHADER_INCLUDE_FOLDER}"
      "${GENERATED_SHADER_FOLDER}"
      "${dxc_spirv_executable}")
  elseif(HLSL_RT_SHADER_FILES)
    message(STATUS "SPIR-V-capable dxc not found; Vulkan ray-tracing SPIR-V will not be built. "
                   "Install the Vulkan SDK or set -DPICCOLO_DXC_SPIRV_EXECUTABLE=... to enable Vulkan path tracing.")
  endif()
else()
  add_custom_target(${TARGET_NAME})
endif()

if(WIN32 AND PICCOLO_ENABLE_D3D12_BACKEND)
  if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER}/dxil)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER}/dxil)
  endif()

  if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER}/dxil_cpp)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER}/dxil_cpp)
  endif()

  compile_hlsl_shader(
    "${HLSL_SHADER_FILES}"
    "${TARGET_NAME}"
    "${HLSL_SHADER_INCLUDE_FOLDER}"
    "${GENERATED_SHADER_FOLDER}"
    "${dxc_executable}")
endif()

set_target_properties("${TARGET_NAME}" PROPERTIES FOLDER "Engine" )

