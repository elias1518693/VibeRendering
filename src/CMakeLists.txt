find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

add_executable(VibeRendering
    main.cpp
    App.cpp
    VulkanContext.cpp
    Swapchain.cpp
    Renderer.cpp
)

target_link_libraries(VibeRendering PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    spdlog::spdlog
    vk-bootstrap::vk-bootstrap
    GPUOpen::VulkanMemoryAllocator
)

target_compile_definitions(VibeRendering PRIVATE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    $<$<CONFIG:Debug>:VIBE_VALIDATION_LAYERS>
    $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
    $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
)

target_compile_options(VibeRendering PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)
