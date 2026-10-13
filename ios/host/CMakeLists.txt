cmake_minimum_required(VERSION 3.20)
project(UnleashedIOSHost LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(BUILD_SHARED_LIBS OFF)
if(NOT IOS_SOURCE_ROOT)
    message(FATAL_ERROR "Pass -DIOS_SOURCE_ROOT=<prepared iOS source directory>")
endif()
set(UNLEASHED_RECOMP_TOOLS_ROOT "${IOS_SOURCE_ROOT}/tools")
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "" FORCE)
add_subdirectory("${IOS_SOURCE_ROOT}/tools/XenonRecomp" "${CMAKE_BINARY_DIR}/xenon")
include("${IOS_SOURCE_ROOT}/cmake/PrepareDependency.cmake")
unleashed_prepare_dependency(XENOS_SOURCE xenos "${IOS_SOURCE_ROOT}/tools/XenosRecomp"
    PATCHES "${IOS_SOURCE_ROOT}/patches/dependencies/xenos-air-target.patch" COPY_DIRS XenosRecomp)
add_subdirectory("${XENOS_SOURCE}" "${CMAKE_BINARY_DIR}/xenos")
target_compile_features(XenosRecomp PRIVATE cxx_std_20)
target_compile_features(XenonRecomp PRIVATE cxx_std_20)
target_compile_definitions(XenosRecomp PRIVATE
    XENOS_RECOMP_AIR_SDK="iphoneos"
    XENOS_RECOMP_AIR_TARGET="air64-apple-ios16.4"
    XENOS_RECOMP_INPUT="${IOS_SOURCE_ROOT}/UnleashedRecompLib/private"
    XENOS_RECOMP_OUTPUT="${IOS_SOURCE_ROOT}/UnleashedRecompLib/shader/shader_cache.cpp"
    XENOS_RECOMP_INCLUDE_INPUT="${XENOS_SOURCE}/XenosRecomp/shader_common.h"
    UNLEASHED_RECOMP)
target_compile_definitions(XenonRecomp PRIVATE
    XENON_RECOMP_CONFIG_FILE_PATH="${IOS_SOURCE_ROOT}/UnleashedRecompLib/config/SWA.toml"
    XENON_RECOMP_HEADER_FILE_PATH="${IOS_SOURCE_ROOT}/tools/XenonRecomp/XenonUtils/ppc_context.h")
add_subdirectory("${IOS_SOURCE_ROOT}/tools/file_to_c" "${CMAKE_BINARY_DIR}/file_to_c")
add_subdirectory("${IOS_SOURCE_ROOT}/tools/x_decompress" "${CMAKE_BINARY_DIR}/x_decompress")
