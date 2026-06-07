
set(ENGINE_ROOT_DIR "${PICCOLO_ROOT_DIR}/engine")
set(THIRD_PARTY_DIR "${ENGINE_ROOT_DIR}/3rdparty")
set(ENGINE_ASSET_DIR "/asset")

set(DEPLOY_CONFIG_DIR "configs/deployment")
set(DEVELOP_CONFIG_DIR "configs/development")

option(ENABLE_PHYSICS_DEBUG_RENDERER "Enable Physics Debug Renderer" OFF)

# only support physics debug render at windows platform
if(NOT WIN32)
  if(ENABLE_PHYSICS_DEBUG_RENDERER)
    message(WARNING "Disable Physics Debug Renderer")
    set(ENABLE_PHYSICS_DEBUG_RENDERER OFF CACHE BOOL "" FORCE)
  endif()
endif()

if(ENABLE_PHYSICS_DEBUG_RENDERER)
  set(JOLT_ASSET_DIR "/jolt-asset")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options("/MP")
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT PiccoloEditor)
endif()

set(vulkan_include ${THIRD_PARTY_DIR}/VulkanSDK/include)

if(WIN32)
    set(vulkan_lib ${THIRD_PARTY_DIR}/VulkanSDK/lib/Win32/vulkan-1.lib)
    set(glslangValidator_executable ${THIRD_PARTY_DIR}/VulkanSDK/bin/Win32/glslangValidator.exe)
    add_compile_definitions("PICCOLO_VK_LAYER_PATH=${THIRD_PARTY_DIR}/VulkanSDK/bin/Win32")

    set(_dxc_search_paths
        "${THIRD_PARTY_DIR}/VulkanSDK/bin/Win32"
        "${THIRD_PARTY_DIR}/DirectXShaderCompiler/bin/x64"
        "${THIRD_PARTY_DIR}/DXC/bin/x64")

    if(DEFINED ENV{VULKAN_SDK})
        list(APPEND _dxc_search_paths "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/Bin32")
    endif()

    if(DEFINED ENV{WindowsSdkDir})
        file(GLOB _windows_sdk_dxc_paths "$ENV{WindowsSdkDir}/bin/*/x64" "$ENV{WindowsSdkDir}/bin/x64")
        list(APPEND _dxc_search_paths ${_windows_sdk_dxc_paths})
    endif()

    find_program(dxc_executable NAMES dxc.exe dxc HINTS ${_dxc_search_paths})

    if(dxc_executable)
        message(STATUS "Found dxc: ${dxc_executable}")
        add_compile_definitions(PICCOLO_D3D12_SHADER_BYTECODE_AVAILABLE=1)
    else()
        message(WARNING "dxc.exe not found; D3D12 HLSL shaders will be skipped. Set dxc_executable to enable DXIL generation.")
        add_compile_definitions(PICCOLO_D3D12_SHADER_BYTECODE_AVAILABLE=0)
    endif()
elseif(UNIX)
    if(APPLE)
      set(vulkan_lib ${THIRD_PARTY_DIR}/VulkanSDK/lib/MacOS/libvulkan.1.dylib)
      set(glslangValidator_executable ${THIRD_PARTY_DIR}/VulkanSDK/bin/MacOS/glslangValidator)
      add_compile_definitions("PICCOLO_VK_LAYER_PATH=${THIRD_PARTY_DIR}/VulkanSDK/bin/MacOS")
      add_compile_definitions("PICCOLO_VK_ICD_FILENAMES=${THIRD_PARTY_DIR}/VulkanSDK/bin/MacOS/MoltenVK_icd.json")
    else()
      set(vulkan_lib ${THIRD_PARTY_DIR}/VulkanSDK/lib/Linux/libvulkan.so.1)
      set(glslangValidator_executable ${THIRD_PARTY_DIR}/VulkanSDK/bin/Linux/glslangValidator)
      add_compile_definitions("PICCOLO_VK_LAYER_PATH=${THIRD_PARTY_DIR}/VulkanSDK/bin/Linux")
    endif()
else()
  message(FATAL_ERROR "Unknown Platform")
endif()


set(SHADER_COMPILE_TARGET PiccoloShaderCompile)
add_subdirectory(shader)

add_subdirectory(3rdparty)

add_subdirectory(source/runtime)
add_subdirectory(source/editor)
add_subdirectory(source/meta_parser)
#add_subdirectory(source/test)

set(CODEGEN_TARGET "PiccoloPreCompile")
include(source/precompile/precompile.cmake)
set_target_properties("${CODEGEN_TARGET}" PROPERTIES FOLDER "Engine" )

add_dependencies(PiccoloRuntime "${CODEGEN_TARGET}")
add_dependencies("${CODEGEN_TARGET}" "PiccoloParser")
