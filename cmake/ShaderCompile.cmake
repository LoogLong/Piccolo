function(compile_shader SHADERS TARGET_NAME SHADER_INCLUDE_FOLDER GENERATED_DIR GLSLANG_BIN)
    if(NOT GLSLANG_BIN)
        message(FATAL_ERROR "glslangValidator is required to compile Vulkan GLSL shaders")
    endif()

    set(working_dir "${CMAKE_CURRENT_SOURCE_DIR}")

    set(ALL_GENERATED_SPV_FILES "")
    set(ALL_GENERATED_CPP_FILES "")

    if(UNIX)
        execute_process(COMMAND chmod a+x ${GLSLANG_BIN})
    endif()

    foreach(SHADER ${SHADERS})
    # Prepare a header name and a global variable for this shader
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        string(REPLACE "." "_" HEADER_NAME ${SHADER_NAME})
        string(TOUPPER ${HEADER_NAME} GLOBAL_SHADER_VAR)

        set(SPV_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_DIR}/spv/${SHADER_NAME}.spv")
        set(CPP_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_DIR}/cpp/${HEADER_NAME}.h")

        add_custom_command(
            OUTPUT ${SPV_FILE}
            COMMAND "${GLSLANG_BIN}" "-I${SHADER_INCLUDE_FOLDER}" -V100 -o "${SPV_FILE}" "${SHADER}"
            DEPENDS ${SHADER}
            WORKING_DIRECTORY "${working_dir}"
            VERBATIM)

        list(APPEND ALL_GENERATED_SPV_FILES ${SPV_FILE})

        add_custom_command(
            OUTPUT ${CPP_FILE}
            COMMAND "${CMAKE_COMMAND}" "-DPATH=${SPV_FILE}" "-DHEADER=${CPP_FILE}"
                "-DGLOBAL=${GLOBAL_SHADER_VAR}" -P "${PICCOLO_ROOT_DIR}/cmake/GenerateShaderCPPFile.cmake"
            DEPENDS ${SPV_FILE}
            WORKING_DIRECTORY "${working_dir}"
            VERBATIM)

        list(APPEND ALL_GENERATED_CPP_FILES ${CPP_FILE})

    endforeach()

    add_custom_target(${TARGET_NAME}
        DEPENDS ${ALL_GENERATED_SPV_FILES} ${ALL_GENERATED_CPP_FILES}
        SOURCES ${SHADERS})

endfunction()

function(get_hlsl_shader_profile SHADER OUT_PROFILE)
    get_filename_component(SHADER_NAME "${SHADER}" NAME)

    if(SHADER_NAME MATCHES "\\.vert\\.hlsl$")
        set(SHADER_PROFILE "vs_6_0")
    elseif(SHADER_NAME MATCHES "\\.frag\\.hlsl$")
        set(SHADER_PROFILE "ps_6_0")
    elseif(SHADER_NAME MATCHES "\\.comp\\.hlsl$")
        set(SHADER_PROFILE "cs_6_0")
    elseif(SHADER_NAME MATCHES "\\.geom\\.hlsl$")
        set(SHADER_PROFILE "gs_6_0")
    elseif(SHADER_NAME MATCHES "\\.lib\\.hlsl$")
        set(SHADER_PROFILE "lib_6_3")
    else()
        message(FATAL_ERROR "Unknown HLSL shader stage for ${SHADER_NAME}")
    endif()

    set(${OUT_PROFILE} "${SHADER_PROFILE}" PARENT_SCOPE)
endfunction()

function(compile_hlsl_shader SHADERS TARGET_NAME SHADER_INCLUDE_FOLDER GENERATED_DIR DXC_BIN)
    if(NOT WIN32)
        return()
    endif()

    if(NOT SHADERS)
        return()
    endif()

    set_source_files_properties(${SHADERS} PROPERTIES HEADER_FILE_ONLY TRUE)
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        set_source_files_properties(${SHADERS} PROPERTIES VS_TOOL_OVERRIDE "None")
    endif()

    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "Target ${TARGET_NAME} must exist before adding HLSL shader compilation")
    endif()

    if(NOT DXC_BIN)
        message(FATAL_ERROR "dxc.exe is required to compile D3D12 HLSL shaders")
    endif()

    set(working_dir "${CMAKE_CURRENT_SOURCE_DIR}")
    file(GLOB_RECURSE HLSL_INCLUDE_FILES CONFIGURE_DEPENDS "${SHADER_INCLUDE_FOLDER}/*.hlsli")

    set(ALL_GENERATED_DXIL_FILES "")
    set(ALL_GENERATED_DXIL_CPP_FILES "")

    foreach(SHADER ${SHADERS})
        get_filename_component(SHADER_NAME "${SHADER}" NAME)
        string(REGEX REPLACE "\\.hlsl$" "" SHADER_BASE_NAME "${SHADER_NAME}")
        string(REPLACE "." "_" HEADER_NAME "${SHADER_BASE_NAME}")
        string(TOUPPER "${HEADER_NAME}" GLOBAL_SHADER_VAR)
        set(GLOBAL_SHADER_VAR "D3D12_${GLOBAL_SHADER_VAR}")

        get_hlsl_shader_profile("${SHADER}" SHADER_PROFILE)
        set(DXC_ENTRY_ARGS -E main)
        if(SHADER_PROFILE MATCHES "^lib_")
            set(DXC_ENTRY_ARGS "")
        endif()

        set(DXIL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_DIR}/dxil/${SHADER_BASE_NAME}.dxil")
        set(CPP_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_DIR}/dxil_cpp/${HEADER_NAME}.h")

        add_custom_command(
            OUTPUT "${DXIL_FILE}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_DIR}/dxil"
            COMMAND "${DXC_BIN}" -nologo ${DXC_ENTRY_ARGS} -T "${SHADER_PROFILE}" -I "${SHADER_INCLUDE_FOLDER}" -Fo "${DXIL_FILE}" "${SHADER}"
            DEPENDS "${SHADER}" ${HLSL_INCLUDE_FILES}
            WORKING_DIRECTORY "${working_dir}"
            VERBATIM)

        list(APPEND ALL_GENERATED_DXIL_FILES "${DXIL_FILE}")

        add_custom_command(
            OUTPUT "${CPP_FILE}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/${GENERATED_DIR}/dxil_cpp"
            COMMAND "${CMAKE_COMMAND}" "-DPATH=${DXIL_FILE}" "-DHEADER=${CPP_FILE}"
                "-DGLOBAL=${GLOBAL_SHADER_VAR}" -P "${PICCOLO_ROOT_DIR}/cmake/GenerateShaderCPPFile.cmake"
            DEPENDS "${DXIL_FILE}" "${PICCOLO_ROOT_DIR}/cmake/GenerateShaderCPPFile.cmake"
            WORKING_DIRECTORY "${working_dir}"
            VERBATIM)

        list(APPEND ALL_GENERATED_DXIL_CPP_FILES "${CPP_FILE}")
    endforeach()

    set(HLSL_TARGET_NAME "${TARGET_NAME}D3D12")
    add_custom_target(${HLSL_TARGET_NAME}
        DEPENDS ${ALL_GENERATED_DXIL_FILES} ${ALL_GENERATED_DXIL_CPP_FILES}
        SOURCES ${SHADERS} ${HLSL_INCLUDE_FILES})
    add_dependencies(${TARGET_NAME} ${HLSL_TARGET_NAME})
    set_target_properties("${HLSL_TARGET_NAME}" PROPERTIES FOLDER "Engine")
endfunction()
