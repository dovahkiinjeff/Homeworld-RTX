cmake_minimum_required(VERSION 3.25)

project(HomeworldModern
    VERSION 0.91.3
    DESCRIPTION "Native Windows modernization of the Homeworld engine"
    LANGUAGES C CXX)

include(CTest)

if(NOT WIN32)
    message(FATAL_ERROR "The modern CMake target currently supports 64-bit Windows only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "HomeworldModern requires an x64 generator/toolchain.")
endif()

option(HW_ENABLE_MOVIES "Enable FFmpeg-backed Bink cutscene playback" ON)
option(HW_ENABLE_NETWORK "Enable SDL_net-backed LAN networking" OFF)
option(HW_ENABLE_D3D12_DIAGNOSTICS "Probe the active GPU for D3D12/DXR support at startup" ON)
option(HW_ENABLE_D3D12_BACKEND "Use the D3D12 DXGI visible presentation bridge" ON)
option(HW_ENABLE_D3D12_NATIVE_RASTER "Replace the legacy OpenGL context with the native D3D12 fixed-function compatibility rasterizer" ON)
option(HW_ENABLE_ASAN "Enable MSVC AddressSanitizer for native crash diagnosis" OFF)

if(HW_ENABLE_ASAN AND NOT MSVC)
    message(FATAL_ERROR "HW_ENABLE_ASAN currently requires the MSVC toolchain.")
endif()

find_package(SDL2 CONFIG REQUIRED)
if(HW_ENABLE_D3D12_NATIVE_RASTER)
    if(NOT HW_ENABLE_D3D12_BACKEND)
        message(FATAL_ERROR "HW_ENABLE_D3D12_NATIVE_RASTER requires HW_ENABLE_D3D12_BACKEND.")
    endif()
else()
    find_package(OpenGL REQUIRED)
endif()

if(HW_ENABLE_MOVIES)
    find_package(FFMPEG REQUIRED)
endif()

if(HW_ENABLE_NETWORK)
    find_package(SDL2_net CONFIG REQUIRED)
endif()

# The vcpkg winflexbison port installs host tools outside the ordinary PATH.
# Help CMake's standard FindBISON/FindFLEX modules locate them.
set(_hw_tool_hints)
if(DEFINED VCPKG_INSTALLED_DIR)
    if(NOT DEFINED VCPKG_HOST_TRIPLET)
        set(VCPKG_HOST_TRIPLET "x64-windows")
    endif()
    list(APPEND _hw_tool_hints
        "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/winflexbison"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/winflexbison")
endif()

find_program(_hw_bison NAMES win_bison bison HINTS ${_hw_tool_hints})
find_program(_hw_flex NAMES win_flex flex HINTS ${_hw_tool_hints})
if(_hw_bison)
    set(BISON_EXECUTABLE "${_hw_bison}" CACHE FILEPATH "Bison executable" FORCE)
endif()
if(_hw_flex)
    set(FLEX_EXECUTABLE "${_hw_flex}" CACHE FILEPATH "Flex executable" FORCE)
endif()

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

set(HW_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(HW_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${HW_GENERATED_DIR}/kas2c")
file(MAKE_DIRECTORY "${HW_GENERATED_DIR}/Game")
file(MAKE_DIRECTORY "${HW_GENERATED_DIR}/Missions")

BISON_TARGET(KasParser
    "${HW_SOURCE_DIR}/tools/kas2c/parser.y"
    "${HW_GENERATED_DIR}/kas2c/parser.c"
    DEFINES_FILE "${HW_GENERATED_DIR}/kas2c/parser.h")
FLEX_TARGET(KasLexer
    "${HW_SOURCE_DIR}/tools/kas2c/lexer.l"
    "${HW_GENERATED_DIR}/kas2c/lexer.c"
    COMPILE_FLAGS "-i --wincompat")
ADD_FLEX_BISON_DEPENDENCY(KasLexer KasParser)

add_executable(kas2c
    "${HW_SOURCE_DIR}/tools/kas2c/kas2c.c"
    ${BISON_KasParser_OUTPUTS}
    ${FLEX_KasLexer_OUTPUTS})
target_include_directories(kas2c PRIVATE
    "${HW_SOURCE_DIR}/tools/kas2c"
    "${HW_SOURCE_DIR}/src/Game"
    "${HW_GENERATED_DIR}/kas2c")
target_link_libraries(kas2c PRIVATE SDL2::SDL2)
target_compile_definitions(kas2c PRIVATE
    _CRT_NONSTDC_NO_WARNINGS
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMINMAX)

if(MSVC)
    set(HW_PREPROCESSOR_MODE MSVC)
    target_compile_options(kas2c PRIVATE /W3 /wd4996)
else()
    set(HW_PREPROCESSOR_MODE GNU)
endif()

set(HW_PREPROCESS_SCRIPT "${HW_SOURCE_DIR}/cmake/Preprocess.cmake")

function(hw_add_kas output_name input_file)
    set(_kp "${HW_GENERATED_DIR}/Missions/${output_name}.kp")
    set(_c "${HW_GENERATED_DIR}/Missions/${output_name}.c")
    set(_h "${HW_GENERATED_DIR}/Missions/${output_name}.h")
    set(_func "${HW_GENERATED_DIR}/Missions/${output_name}.func.c")

    add_custom_command(
        OUTPUT "${_c}" "${_h}" "${_func}"
        COMMAND "${CMAKE_COMMAND}"
            "-DHW_COMPILER=${CMAKE_C_COMPILER}"
            "-DHW_MODE=${HW_PREPROCESSOR_MODE}"
            "-DHW_INPUT=${input_file}"
            "-DHW_OUTPUT=${_kp}"
            -P "${HW_PREPROCESS_SCRIPT}"
        COMMAND $<TARGET_FILE:kas2c> "${_kp}" "${_c}" "${_h}" "${_func}"
        DEPENDS
            kas2c
            "${input_file}"
            "${HW_SOURCE_DIR}/src/Missions/SinglePlayer/Swarmer.kash"
            "${HW_SOURCE_DIR}/src/Missions/SinglePlayer/Utilities.kash"
            "${HW_PREPROCESS_SCRIPT}"
        COMMENT "Generating ${output_name} mission sources"
        VERBATIM)

    set(HW_GENERATED_MISSION_SOURCES
        ${HW_GENERATED_MISSION_SOURCES} "${_c}" "${_h}" "${_func}"
        PARENT_SCOPE)
endfunction()

set(HW_SINGLE_PLAYER_MISSIONS
    Mission01 Mission02 Mission03 Mission04 Mission05 Mission05_OEM Mission06
    Mission07 Mission08 Mission09 Mission10 Mission11 Mission12 Mission13
    Mission14 Mission15 Mission16)

foreach(_mission IN LISTS HW_SINGLE_PLAYER_MISSIONS)
    hw_add_kas("${_mission}"
        "${HW_SOURCE_DIR}/src/Missions/SinglePlayer/${_mission}.kas")
endforeach()
hw_add_kas("Tutorial1"
    "${HW_SOURCE_DIR}/src/Missions/Tutorials/Tutorial1.kas")

set(HW_WRAPPED_FUNCTIONS "${HW_GENERATED_DIR}/Game/wrapped_functions.h")
add_custom_command(
    OUTPUT "${HW_WRAPPED_FUNCTIONS}"
    COMMAND "${CMAKE_COMMAND}"
        "-DHW_COMPILER=${CMAKE_C_COMPILER}"
        "-DHW_MODE=${HW_PREPROCESSOR_MODE}"
        "-DHW_INPUT=${HW_SOURCE_DIR}/src/Game/functions.h"
        "-DHW_OUTPUT=${HW_WRAPPED_FUNCTIONS}"
        -P "${HW_PREPROCESS_SCRIPT}"
    DEPENDS
        "${HW_SOURCE_DIR}/src/Game/functions.h"
        "${HW_SOURCE_DIR}/src/Game/wrap.h"
        "${HW_SOURCE_DIR}/src/Game/wrap_types.h"
        "${HW_PREPROCESS_SCRIPT}"
    COMMENT "Generating wrapped effect functions"
    VERBATIM)

file(GLOB HW_GAME_SOURCES CONFIGURE_DEPENDS "${HW_SOURCE_DIR}/src/Game/*.c")
set(HW_MESH_SOURCE "${HW_SOURCE_DIR}/src/Game/Mesh.c")
if(NOT EXISTS "${HW_MESH_SOURCE}")
    message(FATAL_ERROR "Required legacy mesh implementation is missing: ${HW_MESH_SOURCE}")
endif()
# Mesh.c owns the legacy mesh renderer/loader and several renderer globals.
# Keep it out of the wildcard list and add it explicitly to the target below.
# This avoids Visual Studio project regeneration ever dropping Mesh.obj while
# other Game sources still reference meshLoad/meshRender/etc.
list(REMOVE_ITEM HW_GAME_SOURCES "${HW_MESH_SOURCE}")
# Retire the modern harvestable DustCloud replacement. Legacy Clouds.c owns
# harvestable DustCloud/GasCloud rendering again.
list(REMOVE_ITEM HW_GAME_SOURCES "${HW_SOURCE_DIR}/src/Game/ModernDustCloud.c")

file(GLOB HW_SDL_SOURCES CONFIGURE_DEPENDS "${HW_SOURCE_DIR}/src/SDL/*.c")

file(GLOB HW_SHIP_SOURCES CONFIGURE_DEPENDS "${HW_SOURCE_DIR}/src/Ships/*.c")

set(HW_THIRD_PARTY_SOURCES
    "${HW_SOURCE_DIR}/src/ThirdParty/CRC/CRC32.c"
    "${HW_SOURCE_DIR}/src/ThirdParty/LZSS/BitIO.c"
    "${HW_SOURCE_DIR}/src/ThirdParty/LZSS/LZSS.c")

set(HW_MODERN_SOURCES
    "${HW_SOURCE_DIR}/src/Modern/WindowsDiagnostics.cpp"
    "${HW_SOURCE_DIR}/src/Modern/WindowsInstall.cpp"
    "${HW_SOURCE_DIR}/src/Modern/WindowsSplash.cpp")
if(HW_ENABLE_D3D12_DIAGNOSTICS OR HW_ENABLE_D3D12_BACKEND)
    list(APPEND HW_MODERN_SOURCES
        "${HW_SOURCE_DIR}/src/Modern/ModernGraphics.cpp")
endif()

if(HW_ENABLE_D3D12_BACKEND)
    list(APPEND HW_MODERN_SOURCES
        "${HW_SOURCE_DIR}/src/Modern/ModernRaytracing.cpp"
        "${HW_SOURCE_DIR}/src/Modern/ModernDlaa.cpp"
        "${HW_SOURCE_DIR}/src/Modern/ModernUpscaler.cpp")
    if(HW_ENABLE_D3D12_NATIVE_RASTER)
        list(APPEND HW_MODERN_SOURCES
            "${HW_SOURCE_DIR}/src/Modern/LegacyD3D12GL.cpp")
    endif()

    set(HW_STREAMLINE_ROOT "${HW_SOURCE_DIR}/.deps/streamline" CACHE PATH
        "NVIDIA Streamline SDK root")
    find_path(HW_STREAMLINE_INCLUDE_DIR sl.h
        HINTS "${HW_STREAMLINE_ROOT}/include" NO_DEFAULT_PATH)
    find_library(HW_STREAMLINE_INTERPOSER_LIBRARY sl.interposer
        HINTS "${HW_STREAMLINE_ROOT}/lib/x64" NO_DEFAULT_PATH)
    set(HW_STREAMLINE_RUNTIME_DIR "${HW_STREAMLINE_ROOT}/bin/x64")
    foreach(_hw_streamline_file IN ITEMS
            sl.interposer.dll sl.common.dll sl.dlss.dll nvngx_dlss.dll)
        if(NOT EXISTS "${HW_STREAMLINE_RUNTIME_DIR}/${_hw_streamline_file}")
            message(FATAL_ERROR
                "NVIDIA Streamline runtime '${_hw_streamline_file}' is missing. "
                "Run Windows/bootstrap.ps1 to install the signed v2.12.0 SDK.")
        endif()
    endforeach()
    # DLSS Ray Reconstruction is a separate Streamline feature/runtime.
    # Require the signed production payload instead of producing a build that
    # advertises RR but silently reports RUNTIME MISSING at launch.
    set(HW_STREAMLINE_RR_RUNTIME_NAMES sl.dlss_d.dll nvngx_dlssd.dll)
    set(HW_STREAMLINE_RR_RUNTIME_PATHS)
    foreach(_hw_streamline_rr_file IN LISTS HW_STREAMLINE_RR_RUNTIME_NAMES)
        set(_hw_rr_path "${HW_STREAMLINE_RUNTIME_DIR}/${_hw_streamline_rr_file}")
        if(NOT EXISTS "${_hw_rr_path}")
            message(FATAL_ERROR
                "NVIDIA DLSS Ray Reconstruction runtime '${_hw_streamline_rr_file}' is missing from the normalized Streamline runtime. "
                "Run Windows/bootstrap.ps1 to install the production RR runtime.")
        endif()
        list(APPEND HW_STREAMLINE_RR_RUNTIME_PATHS "${_hw_rr_path}")
    endforeach()
    if(NOT HW_STREAMLINE_INCLUDE_DIR OR NOT HW_STREAMLINE_INTERPOSER_LIBRARY)
        message(FATAL_ERROR
            "NVIDIA Streamline SDK v2.12.0 is missing. Run "
            "Windows/bootstrap.ps1 before configuring the build.")
    endif()
    set(HW_FIDELITYFX_ROOT "${HW_SOURCE_DIR}/.deps/fidelityfx" CACHE PATH "AMD FidelityFX SDK root")
    set(HW_XESS_ROOT "${HW_SOURCE_DIR}/.deps/xess" CACHE PATH "Intel XeSS SDK root")
    set(HW_FIDELITYFX_LOADER_LIBRARY "${HW_FIDELITYFX_ROOT}/lib/amd_fidelityfx_loader_dx12.lib")
    set(HW_XESS_LIBRARY "${HW_XESS_ROOT}/lib/libxess.lib")
    foreach(_hw_vendor_file IN ITEMS
            "${HW_FIDELITYFX_ROOT}/Kits/FidelityFX/api/include/ffx_api.hpp"
            "${HW_FIDELITYFX_ROOT}/Kits/FidelityFX/upscalers/include/ffx_upscale.hpp"
            "${HW_FIDELITYFX_LOADER_LIBRARY}"
            "${HW_XESS_ROOT}/include/xess/xess_d3d12.h"
            "${HW_XESS_LIBRARY}")
        if(NOT EXISTS "${_hw_vendor_file}")
            message(FATAL_ERROR "Cross-vendor reconstruction dependency missing: ${_hw_vendor_file}. Run Windows/bootstrap.ps1.")
        endif()
    endforeach()
    # The official vcpkg package exports the compiler path. Keep a direct
    # tool-directory search as a fallback for older package revisions.
    find_package(directx-dxc CONFIG QUIET)
    if(DIRECTX_DXC_TOOL)
        set(HW_DXC_EXECUTABLE "${DIRECTX_DXC_TOOL}")
    endif()
    set(_hw_dxc_hints)
    if(DEFINED VCPKG_INSTALLED_DIR)
        list(APPEND _hw_dxc_hints
            "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/directx-dxc"
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc")
    endif()
    find_program(HW_DXC_EXECUTABLE NAMES dxc dxc.exe
        HINTS ${_hw_dxc_hints})
    if(NOT HW_DXC_EXECUTABLE)
        message(FATAL_ERROR
            "The DirectX Shader Compiler (dxc) was not found. Re-run "
            "Windows/bootstrap.ps1 so vcpkg can install directx-dxc.")
    endif()

    file(MAKE_DIRECTORY "${HW_GENERATED_DIR}/Modern")
    set(HW_DXR_SHADER_HEADER
        "${HW_GENERATED_DIR}/Modern/ModernRaytracingShader.h")
    add_custom_command(
        OUTPUT "${HW_DXR_SHADER_HEADER}"
        COMMAND "${HW_DXC_EXECUTABLE}"
            -T lib_6_3
            -HV 2021
            -O3
            -Fh "${HW_DXR_SHADER_HEADER}"
            -Vn g_hwModernRaytracingLibrary
            "${HW_SOURCE_DIR}/src/Modern/ModernRaytracing.hlsl"
        DEPENDS "${HW_SOURCE_DIR}/src/Modern/ModernRaytracing.hlsl"
        COMMENT "Compiling the DXR path-tracing shader library"
        VERBATIM)
    list(APPEND HW_MODERN_SOURCES "${HW_DXR_SHADER_HEADER}")
    set_source_files_properties(
        "${HW_SOURCE_DIR}/src/Modern/ModernRaytracing.cpp"
        PROPERTIES OBJECT_DEPENDS "${HW_DXR_SHADER_HEADER}")
endif()

set_source_files_properties("${HW_SOURCE_DIR}/src/Game/ETG.c"
    PROPERTIES OBJECT_DEPENDS "${HW_WRAPPED_FUNCTIONS}")

# Keep a console subsystem for the first native revisions: the original engine
# writes critical diagnostics to stdout/stderr.  SDL_MAIN_HANDLED preserves the
# ordinary C main() entry point.
add_executable(homeworld
    ${HW_GAME_SOURCES}
    "${HW_MESH_SOURCE}"
    ${HW_SDL_SOURCES}
    ${HW_SHIP_SOURCES}
    ${HW_THIRD_PARTY_SOURCES}
    ${HW_MODERN_SOURCES}
    ${HW_GENERATED_MISSION_SOURCES}
    "${HW_WRAPPED_FUNCTIONS}")
set_source_files_properties("${HW_MESH_SOURCE}" PROPERTIES LANGUAGE C)
get_target_property(_hw_homeworld_sources homeworld SOURCES)
list(FIND _hw_homeworld_sources "${HW_MESH_SOURCE}" _hw_mesh_source_index)
if(_hw_mesh_source_index EQUAL -1)
    message(FATAL_ERROR "Mesh.c was not attached to the homeworld target")
endif()

target_include_directories(homeworld PRIVATE
    "${HW_SOURCE_DIR}/src/Game"
    "${HW_SOURCE_DIR}/src/SDL"
    "${HW_SOURCE_DIR}/src/Ships"
    "${HW_SOURCE_DIR}/src/Missions/SinglePlayer"
    "${HW_SOURCE_DIR}/src/Missions/Tutorials"
    "${HW_SOURCE_DIR}/src/ThirdParty/CRC"
    "${HW_SOURCE_DIR}/src/ThirdParty/LZSS"
    "${HW_SOURCE_DIR}/src/ThirdParty/Titan"
    "${HW_SOURCE_DIR}/src/ThirdParty/stb"
    "${HW_SOURCE_DIR}/src/Modern"
    "${HW_GENERATED_DIR}/Modern"
    "${HW_GENERATED_DIR}/Game"
    "${HW_GENERATED_DIR}/Missions")

if(HW_ENABLE_D3D12_BACKEND)
    target_include_directories(homeworld PRIVATE "${HW_STREAMLINE_INCLUDE_DIR}")
endif()
if(HW_ENABLE_MOVIES)
    target_include_directories(homeworld PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_directories(homeworld PRIVATE ${FFMPEG_LIBRARY_DIRS})
endif()

target_compile_features(homeworld PRIVATE c_std_11 cxx_std_17)
target_compile_definitions(homeworld PRIVATE
    SDL_MAIN_HANDLED
    HW_GAME_HOMEWORLD
    GENERIC_ETGCALLFUNCTION
    _X86_64
    _X86_64_FIX_ME
    _LINUX_FIX_ME
    _GNU_SOURCE
    WINDOWS
    _WINDOWS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_NONSTDC_NO_WARNINGS
    _CRT_SECURE_NO_WARNINGS
    HW_MODERN_FORCE_LOD0
    HW_MODERN_DISABLE_NLIPS
    HW_MODERN_INFINITE_DRAW_DISTANCE
    "$<$<BOOL:${HW_ENABLE_D3D12_DIAGNOSTICS}>:HW_ENABLE_D3D12_DIAGNOSTICS>"
    "$<$<BOOL:${HW_ENABLE_D3D12_BACKEND}>:HW_ENABLE_D3D12_BACKEND>"
    "$<$<BOOL:${HW_ENABLE_D3D12_NATIVE_RASTER}>:HW_ENABLE_D3D12_NATIVE_RASTER>"
    "$<$<BOOL:${HW_ENABLE_NETWORK}>:HW_ENABLE_NETWORK>"
    "$<$<BOOL:${HW_ENABLE_MOVIES}>:HW_ENABLE_MOVIES>"
    "$<$<CONFIG:Debug>:HW_BUILD_FOR_DEBUGGING>"
    "$<$<NOT:$<CONFIG:Debug>>:HW_BUILD_FOR_DISTRIBUTION>")

if(MSVC)
    target_compile_options(homeworld PRIVATE
        /W3
        /wd4013 /wd4047 /wd4090 /wd4113 /wd4133 /wd4244 /wd4267 /wd4996)
    if(HW_ENABLE_ASAN)
        target_compile_options(homeworld PRIVATE /fsanitize=address /Zi)
    endif()
    target_link_options(homeworld PRIVATE
        /DEBUG:FULL
        /MANIFEST:EMBED
        "/MANIFESTINPUT:${HW_SOURCE_DIR}/Windows/HomeworldModern.manifest")
    if(HW_ENABLE_ASAN)
        target_link_options(homeworld PRIVATE /INCREMENTAL:NO)
    endif()
endif()

target_link_libraries(homeworld PRIVATE
    SDL2::SDL2
    advapi32
    dbghelp
    comdlg32
    gdi32
    ole32
    shell32
    user32
    winmm)

if(NOT HW_ENABLE_D3D12_NATIVE_RASTER)
    target_link_libraries(homeworld PRIVATE OpenGL::GL)
endif()

if(HW_ENABLE_MOVIES)
    target_link_libraries(homeworld PRIVATE ${FFMPEG_LIBRARIES})
endif()

if(HW_ENABLE_D3D12_DIAGNOSTICS OR HW_ENABLE_D3D12_BACKEND)
    target_link_libraries(homeworld PRIVATE d3dcompiler dxguid)
    if(HW_ENABLE_D3D12_BACKEND)
        # Streamline's import library supplies the D3D12/DXGI entry points and
        # installs NVIDIA's signed interposer before device creation.
        target_link_libraries(homeworld PRIVATE
            "${HW_STREAMLINE_INTERPOSER_LIBRARY}"
            "${HW_FIDELITYFX_LOADER_LIBRARY}"
            "${HW_XESS_LIBRARY}")
        target_include_directories(homeworld PRIVATE
            "${HW_FIDELITYFX_ROOT}/Kits/FidelityFX/api/include"
            "${HW_FIDELITYFX_ROOT}/Kits/FidelityFX/upscalers/include"
            "${HW_XESS_ROOT}/include")
    else()
        target_link_libraries(homeworld PRIVATE d3d12 dxgi)
    endif()
endif()

if(HW_ENABLE_NETWORK)
    if(TARGET SDL2_net::SDL2_net)
        target_link_libraries(homeworld PRIVATE SDL2_net::SDL2_net)
    elseif(TARGET SDL2_net::SDL2_net-static)
        target_link_libraries(homeworld PRIVATE SDL2_net::SDL2_net-static)
    else()
        message(FATAL_ERROR "SDL2_net was found, but it did not provide a supported CMake target.")
    endif()
endif()

if(TARGET SDL2::SDL2)
    add_custom_command(TARGET homeworld POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            $<TARGET_FILE:SDL2::SDL2>
            $<TARGET_FILE_DIR:homeworld>
        COMMENT "Copying the SDL2 runtime")
endif()

# The modern interface owns its presentation assets instead of depending on
# FEMan decorative LIFs embedded in Homeworld.big.  Copy them beside the EXE so
# SDL_GetBasePath() can resolve them regardless of the launch working folder.
add_custom_command(TARGET homeworld POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${HW_SOURCE_DIR}/assets/UI"
        "$<TARGET_FILE_DIR:homeworld>/UI"
    COMMENT "Copying Homeworld Modern interface artwork")

add_custom_command(TARGET homeworld POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${HW_SOURCE_DIR}/assets/Missions"
        "$<TARGET_FILE_DIR:homeworld>/Missions"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${HW_SOURCE_DIR}/assets/Planets"
        "$<TARGET_FILE_DIR:homeworld>/Planets"
    COMMENT "Copying Homeworld Modern mission and planet artwork")

add_custom_command(TARGET homeworld POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${HW_SOURCE_DIR}/assets/Cursors"
        "$<TARGET_FILE_DIR:homeworld>/Cursors"
    COMMENT "Copying faithful high-definition Homeworld cursors")

add_custom_command(TARGET homeworld POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${HW_SOURCE_DIR}/assets/Asteroids"
        "$<TARGET_FILE_DIR:homeworld>/Asteroids"
    COMMENT "Copying subdivided Homeworld Modern asteroid artwork")

# Loose highest-detail ship textures.  Homeworld's resource layer gives these
# files precedence over the same paths inside Homeworld.big.  Only R1
# Mothership LOD0 is supplied because HW_MODERN_FORCE_LOD0 keeps that mesh at
# every distance in the modern build.
add_custom_command(TARGET homeworld POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${HW_SOURCE_DIR}/assets/R1"
        "$<TARGET_FILE_DIR:homeworld>/R1"
    COMMENT "Copying Homeworld Modern LOD0 ship texture overrides")

if(HW_ENABLE_D3D12_BACKEND)
    foreach(_hw_streamline_file IN ITEMS
            sl.interposer.dll sl.common.dll sl.dlss.dll nvngx_dlss.dll)
        add_custom_command(TARGET homeworld POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${HW_STREAMLINE_RUNTIME_DIR}/${_hw_streamline_file}"
                $<TARGET_FILE_DIR:homeworld>
            COMMENT "Copying NVIDIA Streamline runtime: ${_hw_streamline_file}")
    endforeach()
    foreach(_hw_streamline_rr_path IN LISTS HW_STREAMLINE_RR_RUNTIME_PATHS)
        get_filename_component(_hw_streamline_rr_file "${_hw_streamline_rr_path}" NAME)
        add_custom_command(TARGET homeworld POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_hw_streamline_rr_path}"
                $<TARGET_FILE_DIR:homeworld>
            COMMENT "Copying NVIDIA DLSS Ray Reconstruction runtime: ${_hw_streamline_rr_file}")
    endforeach()
    foreach(_hw_vendor_runtime IN ITEMS
            "${HW_FIDELITYFX_ROOT}/bin/amd_fidelityfx_loader_dx12.dll"
            "${HW_FIDELITYFX_ROOT}/bin/amd_fidelityfx_upscaler_dx12.dll"
            "${HW_XESS_ROOT}/bin/libxess.dll")
        add_custom_command(TARGET homeworld POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_hw_vendor_runtime}" $<TARGET_FILE_DIR:homeworld>
            COMMENT "Copying cross-vendor reconstruction runtime: ${_hw_vendor_runtime}")
    endforeach()
endif()

set_target_properties(homeworld PROPERTIES
    OUTPUT_NAME "HomeworldModern"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:homeworld>")

install(TARGETS homeworld RUNTIME DESTINATION .)
install(DIRECTORY "${HW_SOURCE_DIR}/assets/UI/" DESTINATION UI)
install(DIRECTORY "${HW_SOURCE_DIR}/assets/Missions/" DESTINATION Missions)
install(DIRECTORY "${HW_SOURCE_DIR}/assets/Planets/" DESTINATION Planets)
install(DIRECTORY "${HW_SOURCE_DIR}/assets/Cursors/" DESTINATION Cursors)
install(DIRECTORY "${HW_SOURCE_DIR}/assets/Asteroids/" DESTINATION Asteroids)
install(DIRECTORY "${HW_SOURCE_DIR}/assets/R1/" DESTINATION R1)

if(BUILD_TESTING)
    add_executable(ui_scale_tests
        "${HW_SOURCE_DIR}/tests/UIScaleTests.c"
        "${HW_SOURCE_DIR}/src/SDL/uiscale.c")
    target_include_directories(ui_scale_tests PRIVATE
        "${HW_SOURCE_DIR}/src/Game"
        "${HW_SOURCE_DIR}/src/SDL")
    target_link_libraries(ui_scale_tests PRIVATE SDL2::SDL2)
    target_compile_features(ui_scale_tests PRIVATE c_std_11)
    add_test(NAME ui_scale_policy COMMAND ui_scale_tests)
endif()
