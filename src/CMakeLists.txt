find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_executable(VibeRendering
    main.cpp
    App.cpp
    VulkanContext.cpp
    Swapchain.cpp
    Renderer.cpp
    Pipeline.cpp
    GltfLoader.cpp
)

target_link_libraries(VibeRendering PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    spdlog::spdlog
    vk-bootstrap::vk-bootstrap
    GPUOpen::VulkanMemoryAllocator
    fastgltf::fastgltf
    imgui::imgui
)

target_compile_definitions(VibeRendering PRIVATE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    $<$<CONFIG:Debug>:VIBE_VALIDATION_LAYERS>
    $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
    $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
)

target_include_directories(VibeRendering PRIVATE ${Stb_INCLUDE_DIR})

target_compile_options(VibeRendering PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

# ── Shader compilation ────────────────────────────────────────────────────────
find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found. Is the Vulkan SDK installed and VULKAN_SDK set?")
endif()

set(SHADER_SRC_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_BIN_DIR ${CMAKE_SOURCE_DIR}/assets/shaders)
file(MAKE_DIRECTORY ${SHADER_BIN_DIR})

set(SHADERS
    ${SHADER_SRC_DIR}/mesh.vert
    ${SHADER_SRC_DIR}/mesh.frag
    ${SHADER_SRC_DIR}/shadow_cube.vert
    ${SHADER_SRC_DIR}/shadow_cube.frag
)

foreach(SHADER IN LISTS SHADERS)
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV ${SHADER_BIN_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT  ${SPV}
        COMMAND ${GLSLC} ${SHADER} -o ${SPV}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader: ${SHADER_NAME}"
        VERBATIM
    )
    list(APPEND SPV_FILES ${SPV})
endforeach()

add_custom_target(CompileShaders ALL DEPENDS ${SPV_FILES})
add_dependencies(VibeRendering CompileShaders)

# ── Copy assets to exe directory after build ──────────────────────────────────
add_custom_command(TARGET VibeRendering POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:VibeRendering>/assets"
    COMMENT "Copying assets to output directory"
)
