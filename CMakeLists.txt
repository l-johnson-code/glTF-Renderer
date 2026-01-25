cmake_minimum_required(VERSION 3.25.0)

project(glTF)

add_executable(glTF WIN32)

set_property(TARGET glTF PROPERTY CXX_STANDARD 20)

set_target_properties(glTF PROPERTIES 
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
)

target_compile_features(glTF PUBLIC cxx_std_20)

target_include_directories(glTF PRIVATE 
    "${CMAKE_SOURCE_DIR}/External/imgui"
    "${CMAKE_SOURCE_DIR}/External"
    "${CMAKE_SOURCE_DIR}/External/DirectX-Headers/include"
)

# DirectX Headers.
add_subdirectory("External/DirectX-Headers")
target_link_libraries(glTF PRIVATE "DirectX-Headers")

# SDL.
add_subdirectory("External/SDL")
target_link_libraries(glTF PRIVATE "SDL3::SDL3")
add_custom_command(
    TARGET glTF POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:glTF>
    VERBATIM
)

# GLM
target_include_directories(glTF PRIVATE "External/glm")
target_compile_definitions(glTF PRIVATE "GLM_ENABLE_EXPERIMENTAL" "GLM_FORCE_QUAT_DATA_XYZW")

# spdlog
add_subdirectory("External/spdlog")
target_link_libraries(glTF PRIVATE spdlog::spdlog_header_only)
target_include_directories(glTF PRIVATE "External/spdlog/include")

target_link_libraries(glTF PRIVATE 
    "D3d12.lib" 
    "DXGI.lib"
    "dxguid.lib"
)

target_compile_definitions(glTF PRIVATE $<$<CONFIG:Debug>:DEBUG> NOMINMAX)

# ImGui.
target_compile_definitions(glTF PRIVATE IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_sources(glTF PRIVATE
    "External/imgui/imgui_demo.cpp"
    "External/imgui/imgui_draw.cpp"
    "External/imgui/imgui_tables.cpp"
    "External/imgui/imgui_widgets.cpp"
    "External/imgui/imgui.cpp"
    "External/imgui/backends/imgui_impl_dx12.cpp"
    "External/imgui/backends/imgui_impl_sdl3.cpp"
)
target_compile_definitions(glTF PRIVATE IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

# TinyEXR.
target_sources(glTF PRIVATE
    "External/tinyexr/tinyexr.cc"
    "External/tinyexr/deps/miniz/miniz.c"
)
set_source_files_properties("External/tinyexr/tinyexr.cc" PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/External/tinyexr/deps/miniz")

# TinyGLtf.
target_sources(glTF PRIVATE
    "External/tinygltf/tiny_gltf.cc"
)

target_sources(glTF PRIVATE
    "Source/Animation.cpp"
    "Source/AnimationPlayer.cpp"
    "Source/BufferAllocator.cpp"
    "Source/Config.cpp"
    "Source/DescriptorAllocator.cpp"
    "Source/EnvironmentMap.cpp"
    "Source/File.cpp"
    "Source/ForwardPass.cpp"
    "Source/Gltf.cpp"
    "Source/GpuResources.cpp"
    "Source/GpuSkin.cpp"
    "Source/Main.cpp"
    "Source/Mesh.cpp"
    "Source/Pathtracer.cpp"
    "Source/RaytracingAccelerationStructure.cpp"
    "Source/Renderer.cpp"
    "Source/Swapchain.cpp"
    "Source/Timer.cpp"
    "Source/ToneMapper.cpp"
    "Source/UploadBuffer.cpp"
)

# Shaders
set(SHADER_SOURCE_FILES
    "Source/Shaders/Background.vs.hlsl"
    "Source/Shaders/Background.ps.hlsl"
    "Source/Shaders/ConvertEquirectangularToCubemap.cs.hlsl"
    "Source/Shaders/FilterEnvironmentCubeMap.cs.hlsl"
    "Source/Shaders/Forward.vs.hlsl"
    "Source/Shaders/Forward.ps.hlsl"
    "Source/Shaders/FullscreenTriangle.vs.hlsl"
    "Source/Shaders/GenerateEnvironmentImportanceMap.cs.hlsl"
    "Source/Shaders/GenerateEnvironmentImportanceMapLevel.cs.hlsl"
    "Source/Shaders/GenerateMipLevel.cs.hlsl"
    "Source/Shaders/GenerateMipLevelArray.cs.hlsl"
    "Source/Shaders/PathTracer.lib.hlsl"
    "Source/Shaders/Skin.cs.hlsl"
    "Source/Shaders/ToneMapper.ps.hlsl"
    "Source/Shaders/TransmissionDownsample.cs.hlsl"
)
    
set(SHADER_HEADER_FILES
    "Source/Shaders/Bsdf.hlsli"
    "Source/Shaders/Color.hlsli"
    "Source/Shaders/Common.hlsli"
    "Source/Shaders/Random.hlsli"
    "Source/Shaders/Lights.hlsli"
    "Source/Shaders/Material.hlsli"
    "Source/Shaders/Sampling.hlsli"
    "Source/Shaders/Transforms.hlsli"
)

set(SHADER_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Shaders)

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

add_custom_command(
    TARGET glTF POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/Resources/Sheen_E.exr" $<TARGET_FILE_DIR:glTF>
    VERBATIM
)

add_custom_command(
    TARGET glTF POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/Resources/ProggyVector-Regular.ttf" $<TARGET_FILE_DIR:glTF>
    VERBATIM
)



