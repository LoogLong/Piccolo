set(TARGET_NAME ${SHADER_COMPILE_TARGET})

file(GLOB_RECURSE SHADER_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.vert"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.frag"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.comp"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.geom"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.tesc"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.tese"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.mesh"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.task"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.rgen"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.rchit"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.rmiss"
                                            "${CMAKE_CURRENT_SOURCE_DIR}/glsl/*.rcall")

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

if(PICCOLO_ENABLE_VULKAN_BACKEND)
  compile_shader(
    "${SHADER_FILES}"
    "${TARGET_NAME}"
    "${SHADER_INCLUDE_FOLDER}"
    "${GENERATED_SHADER_FOLDER}"
    "${glslangValidator_executable}")
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

# For each shader, we create a header file
# foreach(SHADER ${shaders})
#     # Prepare a header name and a global variable for this shader
#     get_filename_component(SHADER_NAME ${SHADER} NAME)
#     string(REPLACE "." "_" HEADER_NAME ${SHADER_NAME})
#     string(TOUPPER ${HEADER_NAME} GLOBAL_SHADER_VAR)

#     set(SHADER_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER}/${HEADER_NAME}.h")

#     add_custom_target(
#       ${HEADER_NAME}
#       # Compile any GLSL shader into SPIR-V shader
#       # -g -Od should only be used in debug version
#       COMMAND ${glslangValidator_executable} -I${SHADER_INCLUDE_FOLDER} -V100 -o ${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER}/${SHADER_NAME}.spv ${SHADER}
#       # Make a C header file with the SPIR-V shader
#       COMMAND ${CMAKE_COMMAND} -DPATH="${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER}/${SHADER_NAME}.spv" -DHEADER="${SHADER_HEADER}" -DGLOBAL="${GLOBAL_SHADER_VAR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_resource.cmake"
#       # Rebuild the header file if the shader is updated
#       DEPENDS ${SHADER}
#       COMMENT "Building ${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_SHADER_FOLDER}/${SHADER_NAME}.spv and embedding it into ${SHADER_HEADER}"
#     )

#     set_target_properties(${HEADER_NAME} PROPERTIES FOLDER "Shaders/generated" )
#     # Add the custom target like a dependencies of the project
#     add_dependencies(${PICCOLO_NAME} ${HEADER_NAME})

#     message(STATUS "Generating build commands for ${SHADER}")
# endforeach()

