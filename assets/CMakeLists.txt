cmake_minimum_required(VERSION 3.24)

# The library the port (port/) and the studio (studio/) share, portable C++
# for every platform: the game's data formats, the mod layer that resolves
# user overrides before the originals, the game filesystem, and their tests.
project(
    khdays_port
    VERSION 0.1.0
    DESCRIPTION "Native PC port of Kingdom Hearts 358/2 Days: data formats, mods and filesystem"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KHDAYS_ENABLE_SANITIZERS
    "Build khdays targets with AddressSanitizer + UndefinedBehaviorSanitizer (Clang/GCC only)"
    OFF)

if(KHDAYS_ENABLE_SANITIZERS AND MSVC)
    message(WARNING
        "KHDAYS_ENABLE_SANITIZERS is only wired for Clang/GCC; it will be ignored under MSVC.")
endif()

# ASan+UBSan on one of the project's own targets; fetched third-party code is
# left uninstrumented.
function(khdays_enable_sanitizers target)
    if(NOT KHDAYS_ENABLE_SANITIZERS OR MSVC)
        return()
    endif()

    target_compile_options(${target} PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all)
    target_link_options(${target} PRIVATE
        -fsanitize=address,undefined)
endfunction()

# The project's warning level on one of its own targets.
function(khdays_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

include(FetchContent)

# Optional PNG overrides via stb_image. When off, the build needs no network
# for stb and PNG overrides are unavailable (BMP still works).
option(KHDAYS_ENABLE_PNG "Support PNG overrides (fetches stb)" ON)
if(KHDAYS_ENABLE_PNG)
    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        # stb has no release tags: pin a commit (recorded in
        # THIRD_PARTY_NOTICES.md). A shallow clone cannot fetch a bare SHA.
        GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
    )
    FetchContent_MakeAvailable(stb)
endif()

# Optional glTF model overrides via cgltf (needs PNG for glTF textures).
option(KHDAYS_ENABLE_GLTF "Support glTF model overrides (fetches cgltf)" ON)
if(KHDAYS_ENABLE_GLTF AND KHDAYS_ENABLE_PNG)
    FetchContent_Declare(
        cgltf
        GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
        # Pinned commit, recorded in THIRD_PARTY_NOTICES.md.
        GIT_TAG 85cd62382dfea638278962690cf515023f33ed00
    )
    FetchContent_MakeAvailable(cgltf)
endif()

# Optional FBX model overrides via ufbx (needs PNG's stb for their textures;
# specs/014): its three files, pinned and checked (fetch_ufbx.cmake).
option(KHDAYS_ENABLE_FBX "Support FBX model overrides (fetches ufbx)" ON)
if(KHDAYS_ENABLE_FBX AND KHDAYS_ENABLE_PNG)
    include("${CMAKE_CURRENT_SOURCE_DIR}/fetch_ufbx.cmake")
    add_library(khdays-ufbx STATIC "${ufbx_SOURCE_DIR}/ufbx.c")
endif()

# Our own targets are UTF-8, source and execution: without this MSVC reads
# sources in the host's codepage and mangles non-ASCII literals. Set after
# FetchContent so third-party targets keep their own flags.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# The game's data formats, decoded to neutral forms (textures, 2D graphics,
# fonts, text, models and animation, sound, video containers) and the open
# formats mods bring (PNG, BMP, glTF).
add_library(
    khdays-assets
    STATIC
        src/animation.cpp
        src/audio.cpp
        src/bmp.cpp
        src/cakp.cpp
        src/cell.cpp
        src/font.cpp
        src/gltf_export.cpp
        src/graphics2d.cpp
        src/material_animation.cpp
        src/mdl0.cpp
        src/mesh.cpp
        src/message.cpp
        src/mobiclip.cpp
        src/mods.cpp
        src/mods_video.cpp
        ../external/khdays-decomp/libs/mobiclip/video/portable/mobiclip_reference.cpp
        src/screen.cpp
        src/sequence.cpp
        src/tex0.cpp
        src/ui_layout.cpp
)

target_include_directories(
    khdays-assets
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../external/khdays-decomp/libs/mobiclip/video/portable"
)

if(KHDAYS_ENABLE_PNG)
    target_sources(khdays-assets PRIVATE src/png.cpp src/image.cpp)
    target_include_directories(khdays-assets PRIVATE "${stb_SOURCE_DIR}")
    target_compile_definitions(khdays-assets PUBLIC KHDAYS_HAS_PNG)
endif()

if(KHDAYS_ENABLE_GLTF AND KHDAYS_ENABLE_PNG)
    target_sources(khdays-assets PRIVATE src/gltf.cpp)
    target_include_directories(khdays-assets PRIVATE "${cgltf_SOURCE_DIR}")
    target_compile_definitions(khdays-assets PUBLIC KHDAYS_HAS_GLTF)
endif()

if(KHDAYS_ENABLE_FBX AND KHDAYS_ENABLE_PNG)
    target_sources(khdays-assets PRIVATE src/fbx.cpp)
    target_include_directories(khdays-assets PRIVATE "${ufbx_SOURCE_DIR}")
    target_link_libraries(khdays-assets PRIVATE khdays-ufbx)
    target_compile_definitions(khdays-assets PUBLIC KHDAYS_HAS_FBX)
endif()

khdays_warnings(khdays-assets)
khdays_enable_sanitizers(khdays-assets)

# The mod layer (overrides resolved before the originals) and the game
# filesystem (NitroFS paths across mods, decompressed and raw data).
add_library(
    khdays-resource
    STATIC
        src/loader.cpp
        src/filesystem.cpp
)

target_link_libraries(khdays-resource PUBLIC khdays-assets)
khdays_warnings(khdays-resource)
khdays_enable_sanitizers(khdays-resource)

include(CTest)

if(BUILD_TESTING)
    # One executable per tests/test_<name>.cpp, run by ctest as
    # khdays-<name>. khdays-asset-tests builds them all: CI builds that target
    # instead of naming each test, so adding a test here is the only place it
    # has to be added.
    set(KHDAYS_TESTS
        audio cakp cell font graphics2d material_animation mdl0 mesh message
        mobiclip tex0 ui_layout vfs)
    if(KHDAYS_ENABLE_FBX AND KHDAYS_ENABLE_GLTF AND KHDAYS_ENABLE_PNG)
        list(APPEND KHDAYS_TESTS model_import)  # the mod models' two formats (specs/014)
    endif()
    set(KHDAYS_TEST_TARGETS)
    foreach(name IN LISTS KHDAYS_TESTS)
        string(REPLACE "_" "-" dashed "${name}")
        add_executable(khdays-${dashed}-test tests/test_${name}.cpp)
        target_link_libraries(khdays-${dashed}-test PRIVATE khdays-resource)
        khdays_warnings(khdays-${dashed}-test)
        khdays_enable_sanitizers(khdays-${dashed}-test)
        add_test(NAME khdays-${dashed} COMMAND khdays-${dashed}-test)
        list(APPEND KHDAYS_TEST_TARGETS khdays-${dashed}-test)
    endforeach()
    add_custom_target(khdays-asset-tests DEPENDS ${KHDAYS_TEST_TARGETS})
endif()
