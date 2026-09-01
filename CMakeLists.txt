cmake_minimum_required(VERSION 3.22)
project(tefui_plugin C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TEFKERNEL_DIR "" CACHE PATH "Path to an extracted TEFKernel-main source tree")
if (NOT EXISTS "${TEFKERNEL_DIR}/include/tef_api.h")
    message(FATAL_ERROR "TEFKERNEL_DIR must point to an extracted TEFKernel-main directory")
endif()

add_library(tefui SHARED
        src/tef_api_imp.c
        src/tefui_plugin.cpp
)

target_include_directories(tefui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${TEFKERNEL_DIR}/include
)

target_compile_definitions(tefui PRIVATE TEFUI_PLUGIN_BUILD)

if (ANDROID)
    target_link_libraries(tefui PRIVATE log dl)
    set(TEFUI_PLATFORM "android")
    set(TEFUI_ARCH "${ANDROID_ABI}")
elseif (WIN32)
    set(TEFUI_PLATFORM "windows")
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(TEFUI_ARCH "x64")
    else()
        set(TEFUI_ARCH "x86")
    endif()
elseif (APPLE)
    set(TEFUI_PLATFORM "mac")
    set(TEFUI_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
else()
    set(TEFUI_PLATFORM "linux")
    set(TEFUI_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()

set_target_properties(tefui PROPERTIES
        OUTPUT_NAME "tefui.${TEFUI_PLATFORM}.${TEFUI_ARCH}"
        PREFIX ""
)

