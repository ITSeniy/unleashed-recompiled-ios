cmake_minimum_required(VERSION 3.20)
project(UnleashedIOSHost LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(BUILD_SHARED_LIBS OFF)
# Code generators for the iOS build. The paths below are compiled into them, so
# ios/generate.py runs them without arguments.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(IOS_OUTPUT_ROOT "${REPO_ROOT}/out/ios" CACHE PATH "Output directory of ios/prepare.py and ios/generate.py")
set(UNLEASHED_RECOMP_TOOLS_ROOT "${REPO_ROOT}/tools")
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "" FORCE)
add_subdirectory("${REPO_ROOT}/tools/XenonRecomp" "${CMAKE_BINARY_DIR}/xenon")
include("${REPO_ROOT}/cmake/PrepareDependency.cmake")
unleashed_prepare_dependency(XENOS_SOURCE xenos "${IOS_OUTPUT_ROOT}/deps/XenosRecomp"
    PATCHES "${REPO_ROOT}/patches/dependencies/xenos-air-target.patch" COPY_DIRS XenosRecomp)
add_subdirectory("${XENOS_SOURCE}" "${CMAKE_BINARY_DIR}/xenos")
target_compile_features(XenosRecomp PRIVATE cxx_std_20)
target_compile_features(XenonRecomp PRIVATE cxx_std_20)
target_compile_definitions(XenosRecomp PRIVATE
    XENOS_RECOMP_AIR_SDK="iphoneos"
    XENOS_RECOMP_AIR_TARGET="air64-apple-ios16.4"
    XENOS_RECOMP_INPUT="${REPO_ROOT}/UnleashedRecompLib/private"
    XENOS_RECOMP_OUTPUT="${IOS_OUTPUT_ROOT}/generated/shader_cache.cpp"
    XENOS_RECOMP_INCLUDE_INPUT="${XENOS_SOURCE}/XenosRecomp/shader_common.h"
    UNLEASHED_RECOMP)
target_compile_definitions(XenonRecomp PRIVATE
    XENON_RECOMP_CONFIG_FILE_PATH="${REPO_ROOT}/UnleashedRecompLib/config/SWA.toml"
    XENON_RECOMP_HEADER_FILE_PATH="${REPO_ROOT}/tools/XenonRecomp/XenonUtils/ppc_context.h")
add_subdirectory("${REPO_ROOT}/tools/file_to_c" "${CMAKE_BINARY_DIR}/file_to_c")
add_subdirectory("${REPO_ROOT}/tools/x_decompress" "${CMAKE_BINARY_DIR}/x_decompress")
