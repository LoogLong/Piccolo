# PiccoloFindDXC.cmake — DirectX Shader Compiler (dxc) discovery for DXIL and Vulkan SPIR-V.
#
# Windows SDK ships a dxc that compiles DXIL but not SPIR-V. Vulkan path tracing needs a SPIR-V-capable
# dxc (Vulkan SDK or DirectXShaderCompiler with ENABLE_SPIRV_CODEGEN).
#
# Usage:
#   include(${PICCOLO_ROOT_DIR}/cmake/PiccoloFindDXC.cmake)
#   piccolo_find_dxc(
#     OUT_EXECUTABLE PICCOLO_DXC_EXECUTABLE
#     OUT_SPIRV_EXECUTABLE PICCOLO_DXC_SPIRV_EXECUTABLE
#     REQUIRED_FOR_D3D12 ON|OFF
#   )
#
# Optional cache overrides:
#   -DPICCOLO_DXC_EXECUTABLE=/path/to/dxc.exe
#   -DPICCOLO_DXC_SPIRV_EXECUTABLE=/path/to/spirv-capable/dxc.exe

function(_piccolo_dxc_supports_spirv dxc_exe out_var)
    if(NOT dxc_exe OR NOT EXISTS "${dxc_exe}")
        set(${out_var} FALSE PARENT_SCOPE)
        return()
    endif()

    set(_test_hlsl "${CMAKE_BINARY_DIR}/CMakeFiles/piccolo_dxc_spirv_test.hlsl")
    set(_test_spv "${CMAKE_BINARY_DIR}/CMakeFiles/piccolo_dxc_spirv_test.spv")
    file(WRITE "${_test_hlsl}" "[numthreads(1,1,1)] void main() {}")

    execute_process(
        COMMAND "${dxc_exe}" -nologo -spirv -T cs_6_0 -E main -Fo "${_test_spv}" "${_test_hlsl}"
        RESULT_VARIABLE _result
        OUTPUT_QUIET
        ERROR_QUIET)

    if(_result EQUAL 0 AND EXISTS "${_test_spv}")
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()

    file(REMOVE "${_test_hlsl}" "${_test_spv}")
endfunction()

function(_piccolo_collect_dxc_candidates out_list)
    set(_candidates)

    # Bundled DirectXShaderCompiler redistributable (preferred; supports DXIL + SPIR-V).
    file(GLOB _bundled_dxc_paths "${THIRD_PARTY_DIR}/dxc_*/bin/x64/dxc.exe")
    if(_bundled_dxc_paths)
        list(APPEND _candidates ${_bundled_dxc_paths})
    endif()

    set(_dxc_search_paths
        "${THIRD_PARTY_DIR}/dxc_2026_02_20/bin/x64"
        "${THIRD_PARTY_DIR}/VulkanSDK/bin/Win32"
        "${THIRD_PARTY_DIR}/DirectXShaderCompiler/bin/x64"
        "${THIRD_PARTY_DIR}/DXC/bin/x64")

    if(DEFINED ENV{VULKAN_SDK})
        list(APPEND _dxc_search_paths "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/Bin32")
    endif()

    foreach(_search_path IN LISTS _dxc_search_paths)
        if(EXISTS "${_search_path}/dxc.exe")
            list(APPEND _candidates "${_search_path}/dxc.exe")
        endif()
    endforeach()

    set(_windows_sdk_roots)
    if(DEFINED ENV{WindowsSdkDir})
        list(APPEND _windows_sdk_roots "$ENV{WindowsSdkDir}")
    endif()
    if(DEFINED ENV{ProgramFiles\(x86\)})
        list(APPEND _windows_sdk_roots "$ENV{ProgramFiles\(x86\)}/Windows Kits/10")
    endif()
    if(DEFINED ENV{ProgramFiles})
        list(APPEND _windows_sdk_roots "$ENV{ProgramFiles}/Windows Kits/10")
    endif()
    list(APPEND _windows_sdk_roots
        "C:/Program Files (x86)/Windows Kits/10"
        "C:/Program Files/Windows Kits/10")

    foreach(_windows_sdk_root IN LISTS _windows_sdk_roots)
        file(GLOB _windows_sdk_dxc_paths
            "${_windows_sdk_root}/bin/*/x64/dxc.exe"
            "${_windows_sdk_root}/bin/x64/dxc.exe")
        list(APPEND _candidates ${_windows_sdk_dxc_paths})
    endforeach()

    find_program(_dxc_on_path NAMES dxc.exe dxc)
    if(_dxc_on_path)
        list(APPEND _candidates "${_dxc_on_path}")
    endif()

    list(REMOVE_DUPLICATES _candidates)
    set(${out_list} "${_candidates}" PARENT_SCOPE)
endfunction()

function(piccolo_find_dxc)
    set(_options REQUIRED_FOR_D3D12)
    set(_one_value_args OUT_EXECUTABLE OUT_SPIRV_EXECUTABLE)
    cmake_parse_arguments(ARG "${_options}" "${_one_value_args}" "" ${ARGN})

    if(NOT ARG_OUT_EXECUTABLE)
        message(FATAL_ERROR "piccolo_find_dxc requires OUT_EXECUTABLE")
    endif()

    set(_dxil_dxc "")
    set(_spirv_dxc "")

    if(PICCOLO_DXC_SPIRV_EXECUTABLE AND EXISTS "${PICCOLO_DXC_SPIRV_EXECUTABLE}")
        set(_spirv_dxc "${PICCOLO_DXC_SPIRV_EXECUTABLE}")
    endif()
    if(PICCOLO_DXC_EXECUTABLE AND EXISTS "${PICCOLO_DXC_EXECUTABLE}")
        set(_dxil_dxc "${PICCOLO_DXC_EXECUTABLE}")
        if(NOT _spirv_dxc)
            _piccolo_dxc_supports_spirv("${_dxil_dxc}" _cached_has_spirv)
            if(_cached_has_spirv)
                set(_spirv_dxc "${_dxil_dxc}")
            endif()
        endif()
    endif()

    if(NOT _dxil_dxc OR (ARG_OUT_SPIRV_EXECUTABLE AND NOT _spirv_dxc))
        _piccolo_collect_dxc_candidates(_candidates)

        foreach(_candidate IN LISTS _candidates)
            _piccolo_dxc_supports_spirv("${_candidate}" _has_spirv)

            if(_has_spirv)
                if(NOT _spirv_dxc)
                    set(_spirv_dxc "${_candidate}")
                endif()
                if(NOT _dxil_dxc)
                    set(_dxil_dxc "${_candidate}")
                endif()
            elseif(NOT _dxil_dxc)
                set(_dxil_dxc "${_candidate}")
            endif()

            if(_dxil_dxc AND (_spirv_dxc OR NOT ARG_OUT_SPIRV_EXECUTABLE))
                break()
            endif()
        endforeach()
    endif()

    if(_spirv_dxc)
        _piccolo_dxc_supports_spirv("${_dxil_dxc}" _dxil_has_spirv)
        if(NOT _dxil_has_spirv)
            set(_dxil_dxc "${_spirv_dxc}")
        endif()
    endif()

    if(_dxil_dxc)
        set(${ARG_OUT_EXECUTABLE} "${_dxil_dxc}" PARENT_SCOPE)
        set(PICCOLO_DXC_EXECUTABLE "${_dxil_dxc}" CACHE FILEPATH "DirectX Shader Compiler for DXIL" FORCE)
        message(STATUS "Found PICCOLO_DXC_EXECUTABLE: ${_dxil_dxc}")
    else()
        set(${ARG_OUT_EXECUTABLE} "" PARENT_SCOPE)
    endif()

    if(ARG_OUT_SPIRV_EXECUTABLE)
        if(_spirv_dxc)
            set(${ARG_OUT_SPIRV_EXECUTABLE} "${_spirv_dxc}" PARENT_SCOPE)
            set(PICCOLO_DXC_SPIRV_EXECUTABLE "${_spirv_dxc}" CACHE FILEPATH "SPIR-V-capable dxc for Vulkan ray tracing" FORCE)
            message(STATUS "Found PICCOLO_DXC_SPIRV_EXECUTABLE: ${_spirv_dxc}")
        else()
            set(${ARG_OUT_SPIRV_EXECUTABLE} "" PARENT_SCOPE)
            if(_dxil_dxc)
                message(STATUS
                    "No SPIR-V-capable dxc found (Windows SDK dxc compiles DXIL only). "
                    "Install the Vulkan SDK or DirectXShaderCompiler to build Vulkan path-tracing shaders.")
            endif()
        endif()
    endif()

    if(NOT _dxil_dxc AND ARG_REQUIRED_FOR_D3D12)
        message(FATAL_ERROR
            "dxc.exe is required to build D3D12 backend shaders. "
            "Install the Windows SDK or Vulkan SDK, add dxc to PATH, or set -DPICCOLO_DXC_EXECUTABLE=...")
    elseif(NOT _dxil_dxc)
        message(STATUS
            "PICCOLO_DXC_EXECUTABLE not found; D3D12 HLSL shaders will not be built.")
    endif()
endfunction()
