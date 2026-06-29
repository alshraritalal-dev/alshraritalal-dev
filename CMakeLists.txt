cmake_minimum_required(VERSION 3.28 FATAL_ERROR)

project(TalalOpenWorldEngine
    VERSION 0.1.0
    DESCRIPTION "Independent open-world engine build foundation for DEMO_WORKSTATION"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ProjectLayout)
include(CompilerOptions)
include(DependencyManifest)

talal_reject_in_source_build()
talal_configure_output_directories()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TALAL_ENABLE_GRAPHICS "Enable DX12/Vulkan graphics dependency discovery" ON)
option(TALAL_ENABLE_AUDIO "Enable audio dependency discovery" ON)
option(TALAL_ENABLE_NETWORK "Enable network dependency discovery" ON)
option(TALAL_ENABLE_PHYSICS "Enable physics dependency discovery" ON)
option(TALAL_ENABLE_TOOLS "Enable tools and profiler dependency discovery" ON)
option(TALAL_ENABLE_TESTS "Enable test harness discovery" ON)
option(TALAL_REQUIRE_DEPENDENCIES "Fail configure when a declared dependency is missing" OFF)
option(TALAL_ENABLE_VENDOR_SDKS "Enable locally installed commercial SDK integration" OFF)

set(TALAL_TARGET_MACHINE "DEMO_WORKSTATION" CACHE STRING "Local target machine profile")
set(TALAL_CPU_BASELINE "AVX2" CACHE STRING "CPU instruction baseline for compiler flags")
set(TALAL_LOCAL_COMPILE_JOBS "16" CACHE STRING "Default local compile parallelism")
set(TALAL_LOCAL_SHADER_JOBS "12" CACHE STRING "Default local shader compile parallelism")
set(TALAL_LOCAL_ASSET_JOBS "8" CACHE STRING "Default local asset processing parallelism")
set(TALAL_VENDOR_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor_sdks" CACHE PATH "Root for vendor SDK drops")

add_library(talal_build_config INTERFACE)
target_compile_features(talal_build_config INTERFACE cxx_std_23)
target_compile_definitions(talal_build_config INTERFACE
    TALAL_TARGET_MACHINE="${TALAL_TARGET_MACHINE}"
    TALAL_CPU_BASELINE="${TALAL_CPU_BASELINE}"
    TALAL_LOCAL_COMPILE_JOBS=${TALAL_LOCAL_COMPILE_JOBS}
    TALAL_LOCAL_SHADER_JOBS=${TALAL_LOCAL_SHADER_JOBS}
    TALAL_LOCAL_ASSET_JOBS=${TALAL_LOCAL_ASSET_JOBS})

talal_apply_compiler_options(talal_build_config)
talal_discover_manifest_dependencies(talal_build_config)

if(NOT TARGET Microsoft::DirectXShaderCompiler)
    find_package(directx-dxc CONFIG REQUIRED)
endif()

add_library(D3D12MemAlloc STATIC
    third_party/d3d12ma/D3D12MemAlloc.cpp)

target_include_directories(D3D12MemAlloc PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/d3d12ma")

target_link_libraries(D3D12MemAlloc PUBLIC
    d3d12
    dxgi)

set_target_properties(D3D12MemAlloc PROPERTIES
    FOLDER "third_party")

add_library(talal_core
    src/core/memory/arena_allocator.cpp
    src/core/memory/pool_allocator.cpp
    src/core/memory/tlsf_heap.cpp
    src/core/platform/cpu_topology.cpp
    src/core/platform/high_res_timer.cpp
    src/core/threading/job_system.cpp)

target_include_directories(talal_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(talal_core PUBLIC
    talal_build_config)

target_compile_definitions(talal_core PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN)

set_target_properties(talal_core PROPERTIES
    FOLDER "core")

add_executable(GameEngine WIN32
    apps/game_engine/BudgetAllocator.cpp
    apps/game_engine/DashboardController.cpp
    apps/game_engine/DisplayEnumerator.cpp
    apps/game_engine/ScalabilitySerializer.cpp
    apps/game_engine/UIRenderer.cpp
    apps/game_engine/main.cpp
    src/renderer/backend/descriptor_heap.cpp
    src/renderer/backend/dx12_command_list.cpp
    src/renderer/backend/dx12_command_queue.cpp
    src/renderer/backend/dx12_common.cpp
    src/renderer/backend/dx12_device.cpp
    src/renderer/backend/dx12_swapchain.cpp
    src/renderer/backend/gpu_resource_manager.cpp
    src/renderer/backend/upload_ring_buffer.cpp
    src/renderer/camera.cpp
    src/renderer/frame_graph.cpp
    src/renderer/pipeline/pbr_material.cpp
    src/renderer/render_passes/gbuffer_pass.cpp
    src/renderer/render_passes/lighting_pass.cpp
    src/renderer/render_passes/tonemapping_pass.cpp
    src/renderer/renderer_mvp.cpp
    tools/shader_compiler.cpp)

target_link_libraries(GameEngine PRIVATE
    talal_build_config
    talal_core
    Microsoft::DirectXShaderCompiler
    D3D12MemAlloc
    d3d12
    dxgi
    dxguid
    d3dcompiler
    bcrypt
    user32
    gdi32
    comctl32
    shcore)

add_custom_command(TARGET GameEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:Microsoft::DirectXShaderCompiler>
        $<TARGET_FILE_DIR:GameEngine>)

target_include_directories(GameEngine PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(GameEngine PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    "TALAL_SOURCE_DIR=L\"${CMAKE_CURRENT_SOURCE_DIR}\"")

set_target_properties(GameEngine PROPERTIES
    OUTPUT_NAME "GameEngine"
    FOLDER "apps")

if(TALAL_ENABLE_TESTS)
    enable_testing()
    find_package(Catch2 CONFIG REQUIRED)

    add_executable(talal_core_tests
        tests/core_systems_tests.cpp)

    target_link_libraries(talal_core_tests PRIVATE
        talal_core
        Catch2::Catch2WithMain)

    set_target_properties(talal_core_tests PROPERTIES
        FOLDER "tests")

    add_test(NAME talal_core_tests COMMAND talal_core_tests)
endif()

talal_print_phase0_summary()
