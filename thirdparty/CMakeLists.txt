cmake_policy(SET CMP0077 NEW)

set(MSDF_ATLAS_BUILD_STANDALONE OFF)
set(MSDF_ATLAS_USE_SKIA OFF)
set(MSDF_ATLAS_NO_ARTERY_FONT ON)
set(MSDFGEN_DISABLE_PNG ON)

set(SDL2MIXER_DEPS_SHARED OFF)
set(SDL2MIXER_VENDORED ON)
set(SDL2MIXER_FLAC OFF)
set(SDL2MIXER_MOD OFF)
set(SDL2MIXER_MP3 OFF)
set(SDL2MIXER_MIDI OFF)
set(SDL2MIXER_OPUS OFF)
set(SDL2MIXER_VORBIS "VORBISFILE")
set(SDL2MIXER_WAVPACK OFF)

set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)

set(UNLEASHED_RECOMP_IOS OFF)
if (CMAKE_SYSTEM_NAME STREQUAL "iOS" OR IOS)
    set(UNLEASHED_RECOMP_IOS ON)
endif()

if (CMAKE_SYSTEM_NAME MATCHES "Linux" OR ANDROID)
    set(SDL_VULKAN_ENABLED ON CACHE BOOL "")
endif()

# Vulkan validation on Android is opt-in only: flip this to ON *and* put
# libVkLayer_khronos_validation.so back into the APK's jniLibs when debugging.
# It must stay OFF for normal builds — the layer (plus forced sync validation in
# plume_vulkan.cpp) runs on every API call and costs a large amount of CPU time.
# NOTE: this is a CACHE variable; after changing it, reconfigure the existing
# build dir with -DVULKAN_VALIDATION_LAYER_ENABLED=<ON|OFF> or the old value sticks.
if (ANDROID)
    set(VULKAN_VALIDATION_LAYER_ENABLED OFF CACHE BOOL "")
endif()

if (WIN32)
    set(D3D12_AGILITY_SDK_ENABLED ON CACHE BOOL "")
endif()

add_subdirectory("${UNLEASHED_RECOMP_THIRDPARTY_ROOT}/msdf-atlas-gen")

# The GUI installer wizard (and its native file picker dependency) is not built for
# Android; game files are pushed manually instead. See ui/installer_wizard.cpp guards.
# iOS uses its own UIKit document picker.
if (NOT ANDROID AND NOT UNLEASHED_RECOMP_IOS)
    add_subdirectory("${UNLEASHED_RECOMP_THIRDPARTY_ROOT}/nativefiledialog-extended")
endif()

if (UNLEASHED_RECOMP_IOS)
    if (NOT TARGET xxHash::xxhash)
        set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "" FORCE)
        add_subdirectory("${UNLEASHED_RECOMP_TOOLS_ROOT}/XenonRecomp/thirdparty/xxHash/cmake_unofficial" "${CMAKE_BINARY_DIR}/thirdparty/xxHash")
    endif()
    if (NOT TARGET tomlplusplus::tomlplusplus)
        add_subdirectory("${UNLEASHED_RECOMP_TOOLS_ROOT}/XenonRecomp/thirdparty/tomlplusplus" "${CMAKE_BINARY_DIR}/thirdparty/tomlplusplus")
    endif()
    if (NOT TARGET libzstd_static)
        add_subdirectory("${UNLEASHED_RECOMP_TOOLS_ROOT}/XenosRecomp/thirdparty/zstd/build/cmake" "${CMAKE_BINARY_DIR}/thirdparty/zstd")
    endif()
endif()

add_subdirectory("${UNLEASHED_RECOMP_THIRDPARTY_ROOT}/o1heap")
if (UNLEASHED_RECOMP_IOS)
    add_subdirectory("${UNLEASHED_RECOMP_IOS_DEPS_ROOT}/SDL" "${CMAKE_BINARY_DIR}/thirdparty/SDL")
else()
    add_subdirectory("${UNLEASHED_RECOMP_THIRDPARTY_ROOT}/SDL")
endif()
add_subdirectory("${UNLEASHED_RECOMP_THIRDPARTY_ROOT}/SDL_mixer")
if (UNLEASHED_RECOMP_IOS)
    include("${CMAKE_SOURCE_DIR}/cmake/PrepareDependency.cmake")
    file(GLOB PLUME_PATCHES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/patches/dependencies/plume-*.patch")
    list(SORT PLUME_PATCHES)
    unleashed_prepare_dependency(PLUME_SOURCE plume "${UNLEASHED_RECOMP_IOS_DEPS_ROOT}/plume" PATCHES ${PLUME_PATCHES})
    add_subdirectory("${PLUME_SOURCE}" "${CMAKE_BINARY_DIR}/thirdparty/plume")
else()
    add_subdirectory("${UNLEASHED_RECOMP_THIRDPARTY_ROOT}/plume")
endif()

# Experiment: allows loading a custom Vulkan driver (Mesa Turnip) instead of the vendor
# proprietary blob, to work around a driver-side shader compiler bug seen with this
# engine's buffer_device_address-based bindless resource access. See vulkan_driver_android.cpp.
if (ANDROID)
    add_subdirectory("${UNLEASHED_RECOMP_THIRDPARTY_ROOT}/libadrenotools")
endif()

if (APPLE AND NOT UNLEASHED_RECOMP_IOS)
    add_subdirectory("${UNLEASHED_RECOMP_THIRDPARTY_ROOT}/MoltenVK")
endif()
