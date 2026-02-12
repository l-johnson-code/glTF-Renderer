cmake_minimum_required(VERSION 3.25.0)

file(WRITE "${CMAKE_BINARY_DIR}/.gitignore" "*")

project(glTF)

add_executable(glTF WIN32)

set_target_properties(glTF PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

set_target_properties(glTF PROPERTIES 
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:glTF>"
)

target_compile_features(glTF PRIVATE cxx_std_20)

target_include_directories(glTF PRIVATE 
    "External"
    "External/winpixeventruntime.1.0.240308001/Include"
)

target_link_libraries(glTF PRIVATE 
    "D3d12.lib" 
    "DXGI.lib"
    "dxguid.lib"
)

target_compile_definitions(glTF PRIVATE
    NOMINMAX
    $<$<CONFIG:Debug>:DEBUG>
)

# DirectX Headers.
add_subdirectory("External/DirectX-Headers")
target_link_libraries(glTF PRIVATE "DirectX-Headers")

# SDL.
add_subdirectory("External/SDL")
target_link_libraries(glTF PRIVATE SDL3::SDL3)
add_custom_command(
    TARGET glTF POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:glTF>
    VERBATIM
)

# GLM
add_subdirectory("External/glm")
target_compile_definitions(glTF PUBLIC GLM_ENABLE_EXPERIMENTAL GLM_FORCE_QUAT_DATA_XYZW)
target_link_libraries(glTF PRIVATE glm::glm-header-only)

# spdlog
add_subdirectory("External/spdlog")
target_link_libraries(glTF PRIVATE spdlog::spdlog_header_only)
target_include_directories(glTF PRIVATE "External/spdlog/include")

# ImGui.
add_library(ImGui STATIC)
target_compile_definitions(ImGui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_include_directories(ImGui PUBLIC "External/imgui")
target_include_directories(ImGui PUBLIC "External/imgui")
target_sources(ImGui PRIVATE
    "External/imgui/imgui_demo.cpp"
    "External/imgui/imgui_draw.cpp"
    "External/imgui/imgui_tables.cpp"
    "External/imgui/imgui_widgets.cpp"
    "External/imgui/imgui.cpp"
    "External/imgui/backends/imgui_impl_dx12.cpp"
    "External/imgui/backends/imgui_impl_sdl3.cpp"
)
target_link_libraries(ImGui PRIVATE SDL3::SDL3)
target_link_libraries(glTF PRIVATE ImGui)

# TinyEXR.
option(TINYEXR_BUILD_SAMPLE OFF)
add_subdirectory(External/tinyexr)
target_link_libraries(glTF PRIVATE tinyexr)

# TinyGLtf.
option(TINYGLTF_BUILD_LOADER_EXAMPLE OFF)
option(TINYGLTF_INSTALL OFF)
option(TINYGLTF_INSTALL_VENDOR OFF)
add_subdirectory(External/tinygltf)
target_link_libraries(glTF PRIVATE tinygltf)

# Tracy.
option(TRACY_ENABLE "Enable profiling using tracy." OFF)
if(TRACY_ENABLE)
    option(OVERLOAD_NEW_AND_DELETE "Overload new and delete operators. Increases profiler cpu and memory usage." ON)
    option(TRACY_ON_DEMAND "Only profile when a connection is established with the tracy server." OFF)
    option(TRACY_NO_SAMPLING "Disable call stack sampling." ON)
    option(TRACY_NO_SYSTEM_TRACING "Disable systrace sampling." ON)
    add_subdirectory(External/tracy)
    if(OVERLOAD_NEW_AND_DELETE)
        target_compile_definitions(glTF PRIVATE "OVERLOAD_NEW_AND_DELETE")
    endif()
    target_link_libraries(glTF PRIVATE Tracy::TracyClient)
endif()

# PIX.
option(USE_PIX "Enable PIX GPU event markers." ON)
if(USE_PIX)
    target_compile_definitions(glTF PRIVATE USE_PIX)
    add_library(WinPixEventRuntime SHARED IMPORTED)
    set_target_properties(WinPixEventRuntime PROPERTIES 
        IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/External/winpixeventruntime.1.0.240308001/bin/x64/WinPixEventRuntime.lib" 
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/External/winpixeventruntime.1.0.240308001/bin/x64/WinPixEventRuntime.dll"
    )
    target_link_libraries(glTF PRIVATE WinPixEventRuntime)
endif()

target_sources(glTF PRIVATE
    "Source/Animation.cpp"
    "Source/AnimationPlayer.cpp"
    "Source/Bloom.cpp"
    "Source/BufferAllocator.cpp"
    "Source/CommandContext.cpp"
    "Source/Config.cpp"
    "Source/EnvironmentMap.cpp"
    "Source/File.cpp"
    "Source/ForwardPass.cpp"
    "Source/Gltf.cpp"
    "Source/GpuResources.cpp"
    "Source/GpuSkin.cpp"
    "Source/Main.cpp"
    "Source/Memory.cpp"
    "Source/Mesh.cpp"
    "Source/Pathtracer.cpp"
    "Source/Profiling.cpp"
    "Source/Rasterizer.cpp"
    "Source/RaytracingAccelerationStructure.cpp"
    "Source/Renderer.cpp"
    "Source/ShaderTableBuilder.cpp"
    "Source/Swapchain.cpp"
    "Source/Timer.cpp"
    "Source/ToneMapper.cpp"
    "Source/UploadBuffer.cpp"
)

# Shaders
set(SHADER_SOURCE_FILES
    "Source/Shaders/Background.ps.hlsl"
    "Source/Shaders/Background.vs.hlsl"
    "Source/Shaders/BloomDownsample.cs.hlsl"
    "Source/Shaders/BloomUpsample.cs.hlsl"
    "Source/Shaders/Bsdf.hlsli"
    "Source/Shaders/Color.hlsli"
    "Source/Shaders/Common.hlsli"
    "Source/Shaders/ConvertEquirectangularToCubemap.cs.hlsl"
    "Source/Shaders/FilterEnvironmentCubeMap.cs.hlsl"
    "Source/Shaders/Forward.ps.hlsl"
    "Source/Shaders/Forward.vs.hlsl"
    "Source/Shaders/FullscreenTriangle.vs.hlsl"
    "Source/Shaders/GenerateEnvironmentImportanceMap.cs.hlsl"
    "Source/Shaders/GenerateEnvironmentImportanceMapLevel.cs.hlsl"
    "Source/Shaders/GenerateMipLevel.cs.hlsl"
    "Source/Shaders/GenerateMipLevelArray.cs.hlsl"
    "Source/Shaders/Lights.hlsli"
    "Source/Shaders/Material.hlsli"
    "Source/Shaders/PathTracer.lib.hlsl"
    "Source/Shaders/Random.hlsli"
    "Source/Shaders/Sampling.hlsli"
    "Source/Shaders/Skin.cs.hlsl"
    "Source/Shaders/ToneMapper.ps.hlsl"
    "Source/Shaders/Transforms.hlsli"
    "Source/Shaders/TransmissionDownsample.cs.hlsl"
)

set(SHADER_HEADER_FILES ${SHADER_SOURCE_FILES})
list(FILTER SHADER_HEADER_FILES INCLUDE REGEX "^.*(\.hlsli)$")

# Workaround for TARGET_FILE_DIR generator expression not being usable in OUTPUT custom commands.
get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(IS_MULTI_CONFIG)
  set(SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/$<CONFIG>/Shaders")
else()
  set(SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/Shaders")
endif()

foreach(FILE ${SHADER_SOURCE_FILES})
    cmake_path(GET FILE STEM SHADER_NAME)
    cmake_path(GET FILE EXTENSION SHADER_EXTENSION)
    if (${SHADER_EXTENSION} STREQUAL .ps.hlsl)
        set(SHADER_OUTPUT_EXTENSION .ps.bin)
        set(SHADER_PROFILE ps_6_6)
    elseif (${SHADER_EXTENSION} STREQUAL .vs.hlsl)
        set(SHADER_OUTPUT_EXTENSION .vs.bin)
        set(SHADER_PROFILE vs_6_6)
    elseif (${SHADER_EXTENSION} STREQUAL .lib.hlsl)
        set(SHADER_OUTPUT_EXTENSION .lib.bin)
        set(SHADER_PROFILE lib_6_6)
    elseif (${SHADER_EXTENSION} STREQUAL .cs.hlsl)
        set(SHADER_OUTPUT_EXTENSION .cs.bin)
        set(SHADER_PROFILE cs_6_6)
    else()
        continue()
    endif()
    set(SHADER_INPUT_FILE ${FILE})
    set(SHADER_OUTPUT_FILE ${SHADER_BIN_DIR}/${SHADER_NAME}${SHADER_OUTPUT_EXTENSION})
    # Fun fact! If an input file to dxc has a path starting with C:/ includes don't work!
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT_FILE}
        COMMAND dxc ARGS -HV 2021 -T ${SHADER_PROFILE} -Fo ${SHADER_OUTPUT_FILE} -Zi $<$<CONFIG:Debug>:-Od> -Qembed_debug ${SHADER_INPUT_FILE}
        MAIN_DEPENDENCY ${SHADER_INPUT_FILE}
        DEPENDS ${SHADER_HEADER_FILES}
        # This allows us to use relative paths so we don't break includes.
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT_FILE})
endforeach()

add_custom_target(Shaders DEPENDS ${SHADER_OUTPUTS})

add_dependencies(glTF Shaders)

# Copy DLLs to build directory.
add_custom_command(
    TARGET glTF POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:glTF> $<TARGET_RUNTIME_DLLS:glTF>
    VERBATIM COMMAND_EXPAND_LISTS
)

# Copy resources to build directory.
set(RESOURCES ${CMAKE_SOURCE_DIR}/Resources/Sheen_E.exr ${CMAKE_SOURCE_DIR}/Resources/ProggyVector-Regular.ttf)
add_custom_command(
    TARGET glTF POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:glTF> ${RESOURCES}
    VERBATIM COMMAND_EXPAND_LISTS
)