# TNA iMPACT! - PS5 port (ReXGlue runtime on its PS5 platform layer).
#
#   cmake -S ps5 -B ps5/out/ps5 -G Ninja -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=ps5/cmake/ps5-toolchain.cmake \
#     -DPS5_PAYLOAD_SDK=<ps5-payload-sdk> \
#     -DREXSDK_DIR=<rexglue-sdk, the PS5 runtime branch> \
#     -DREXGLUE_USE_VULKAN=ON
#   cmake --build ps5/out/ps5 -j2
#   ps5/tools/ps5_package.sh
#
# Code: run 'rexglue codegen tnaimpact_manifest.toml' in the repository first
# (it reads private/extract/default.xex - your own disc's, never in this
# repository). generated/ is shared with the macOS build through the
# ps5/generated/default symlink; ps5_patch_generated.py applies the two PS5
# changes to generated code (see its docstring).
#
# No native renderer: TNA iMPACT! has no PC port renderer to reuse. The Xenos
# plugin's Vulkan backend is the only graphics backend, and the app's own ImGui
# overlay comes from the ReXGlue runtime.
cmake_minimum_required(VERSION 3.25)

project(tnaimpact VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TNA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# AMD FidelityFX headers (only), when fetched: the SDK's rexui then defines
# REX_HAS_FIDELITYFX_SDK, which turns on the prebuilt FSR 1 (EASU+RCAS) and
# CAS guest-output shaders and the present_effect values "fsr" / "cas". The
# FidelityFX runtime stays off (no amd_fidelityfx_* target): FSR 2/3 are not
# available, as on the SDK's own PS5 layer.
if(NOT REXGLUE_FIDELITYFX_SOURCE_DIR AND
   EXISTS "$ENV{HOME}/ps5-deps/FidelityFX-SDK/sdk/include")
    set(REXGLUE_FIDELITYFX_SOURCE_DIR "$ENV{HOME}/ps5-deps/FidelityFX-SDK" CACHE PATH
        "AMD FidelityFX SDK headers (sdk/include, ffx-api/include)")
endif()

# The PS5: no profiler, and none of SDL's desktop backends (the runtime's PS5
# platform layer has the display, audio and the DualSense).
if(REX_PS5)
    set(REXGLUE_ENABLE_TRACY OFF CACHE BOOL "" FORCE)
    foreach(opt SDL_DBUS SDL_HIDAPI SDL_IBUS SDL_JACK SDL_KMSDRM SDL_LIBUDEV SDL_OFFSCREEN SDL_OSS SDL_SNDIO)
        set(${opt} OFF CACHE BOOL "" FORCE)
    endforeach()
endif()

# ps5/generated/rexglue.cmake is a copy of the project's generated file with
# ps5_patch_generated.py applied (no PS5 codegen executable in the build).
include(generated/rexglue.cmake)

# The app: the shared src/tnaimpact_app.h (unchanged, so the macOS build and
# its TNA_DUMP_IMAGE hook keep working) plus the PS5 path setup and crash
# reports. The hub (Home / Settings / About) and its sound are based on
# elripalda's SvR2008-Recomp-PS5 (GPL-3.0); its fonts are built into the app
# (fonts/, SIL Open Font License).
add_executable(tnaimpact
    src/main.cpp
    src/crash_report_posix.cpp
    src/fps_counter.cpp
    src/hub/hub_install.cpp
    src/hub/hub_sfx.cpp
    src/hub/hub_ui.cpp
    src/hub/ps5_hub.cpp
    src/ps5/sound.cpp)

set_source_files_properties(src/hub/ps5_hub.cpp PROPERTIES
    COMPILE_DEFINITIONS "TNA_FONTS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/fonts\""
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fonts/oswald-latin-700-normal.ttf")

target_include_directories(tnaimpact PRIVATE src ${TNA_ROOT}/src)

rexglue_setup_target(tnaimpact GPU_PLUGINS xenos)

if(REXSDK_DIR AND TARGET imgui)
    # SDK source tree: ImGui (built into the runtime) is not on the public
    # include path the way the installed SDK bundles it.
    target_include_directories(tnaimpact PRIVATE $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>
                                                ${REXSDK_DIR}/thirdparty/stb)
endif()

target_compile_definitions(tnaimpact PRIVATE TNAIMPACT_PS5_VERSION="${PROJECT_VERSION}")
