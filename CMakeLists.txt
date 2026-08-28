cmake_minimum_required(VERSION 3.22.1)
project("TEFUI" C)
set(CMAKE_C_STANDARD 11)
include_directories(${CMAKE_SOURCE_DIR}/mod-api)

macro(add_terraria_mod MOD_NAME)
    add_library(${MOD_NAME} SHARED
        ${CMAKE_SOURCE_DIR}/mod-api/tefkernel/tef_api_imp.c
        ${ARGN})
    target_include_directories(${MOD_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/mod-api
        ${CMAKE_SOURCE_DIR}/TEFUI)
    target_link_libraries(${MOD_NAME} m)

    if(CMAKE_SYSTEM_NAME STREQUAL "Android")
        set(LOCAL_SYSTEM "android")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(LOCAL_SYSTEM "linux")
    else()
        string(TOLOWER "${CMAKE_SYSTEM_NAME}" LOCAL_SYSTEM)
    endif()

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        set(LOCAL_ARCH "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        set(LOCAL_ARCH "x64")
    else()
        set(LOCAL_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    set_target_properties(${MOD_NAME} PROPERTIES
        OUTPUT_NAME "${MOD_NAME}.${LOCAL_SYSTEM}.${LOCAL_ARCH}"
        SUFFIX ".so")

    if(CMAKE_BUILD_TYPE STREQUAL "Release" AND (ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
        add_custom_command(TARGET ${MOD_NAME} POST_BUILD
            COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:${MOD_NAME}>)
    endif()
endmacro()

add_subdirectory(TEFUI)
