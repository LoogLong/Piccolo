set(imgui_SOURCE_DIR_ ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

if(NOT DEFINED PICCOLO_ENABLE_VULKAN_BACKEND)
    set(PICCOLO_ENABLE_VULKAN_BACKEND ON)
endif()

if(NOT DEFINED PICCOLO_ENABLE_D3D12_BACKEND)
    if(WIN32)
        set(PICCOLO_ENABLE_D3D12_BACKEND ON)
    else()
        set(PICCOLO_ENABLE_D3D12_BACKEND OFF)
    endif()
endif()

file(GLOB imgui_sources CONFIGURE_DEPENDS  "${imgui_SOURCE_DIR_}/*.cpp")
file(GLOB imgui_impl CONFIGURE_DEPENDS  
"${imgui_SOURCE_DIR_}/backends/imgui_impl_glfw.cpp" 
"${imgui_SOURCE_DIR_}/backends/imgui_impl_glfw.h")
if(PICCOLO_ENABLE_VULKAN_BACKEND)
    list(APPEND imgui_impl
        "${imgui_SOURCE_DIR_}/backends/imgui_impl_vulkan.cpp"
        "${imgui_SOURCE_DIR_}/backends/imgui_impl_vulkan.h")
endif()
if(WIN32 AND PICCOLO_ENABLE_D3D12_BACKEND)
    list(APPEND imgui_impl
        "${imgui_SOURCE_DIR_}/backends/imgui_impl_dx12.cpp"
        "${imgui_SOURCE_DIR_}/backends/imgui_impl_dx12.h")
endif()
add_library(imgui STATIC ${imgui_sources} ${imgui_impl})
target_include_directories(imgui PUBLIC $<BUILD_INTERFACE:${imgui_SOURCE_DIR_}>)
target_link_libraries(imgui PUBLIC glfw)
if(PICCOLO_ENABLE_VULKAN_BACKEND)
    target_include_directories(imgui PUBLIC $<BUILD_INTERFACE:${vulkan_include}>)
    target_link_libraries(imgui PUBLIC ${vulkan_lib})
endif()
if(WIN32 AND PICCOLO_ENABLE_D3D12_BACKEND)
    target_link_libraries(imgui PUBLIC d3d12 dxgi dxguid)
endif()
