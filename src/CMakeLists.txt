add_library(xebble STATIC
    log.cpp
    window.cpp
    $<$<PLATFORM_ID:Darwin>:window_macos.cpp>
    $<$<PLATFORM_ID:Windows>:window_windows.cpp>
    $<$<PLATFORM_ID:Linux>:window_linux.cpp>
    vulkan/vma_impl.cpp
    vulkan/context.cpp
    vulkan/swapchain.cpp
    vulkan/buffer.cpp
    vulkan/descriptor.cpp
    vulkan/command.cpp
    vulkan/pipeline.cpp
    stb_impl.cpp
    texture.cpp
    renderer.cpp
    spritesheet.cpp
    tilemap.cpp
    font.cpp
    asset_manager.cpp
    game.cpp
    input_map.cpp
    world.cpp
    builtin_systems.cpp
    embedded_font.cpp
    embedded_petme64.cpp
    embedded_petme642y.cpp
    embedded_berkelium64.cpp
    embedded_fonts_helpers.cpp
    ui.cpp
    debug.cpp
    rng.cpp
    grid.cpp
    fov.cpp
    pathfinding.cpp
    audio.cpp
    config.cpp
)

target_include_directories(xebble PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(xebble PUBLIC
    Vulkan::Vulkan
    glfw
    GPUOpen::VulkanMemoryAllocator
    freetype
    glm::glm
    tomlplusplus::tomlplusplus
    miniz::miniz
    miniaudio::miniaudio
    $<IF:$<TARGET_EXISTS:libxmp::xmp_shared>,libxmp::xmp_shared,libxmp::xmp_static>
)

# Threading required by miniaudio on Linux/macOS
if(UNIX)
    target_link_libraries(xebble PUBLIC pthread dl m)
endif()

# stb is header-only, add include path
target_include_directories(xebble PRIVATE ${Stb_INCLUDE_DIR})

target_compile_features(xebble PUBLIC cxx_std_23)

# Pass bundled MoltenVK ICD path if available
if(XEBBLE_MOLTENVK_ICD_PATH)
    target_compile_definitions(xebble PRIVATE
        XEBBLE_MOLTENVK_ICD="${XEBBLE_MOLTENVK_ICD_PATH}"
    )
endif()

# SID chiptune support via libsidplayfp (built from source via ExternalProject)
target_link_libraries(xebble PUBLIC sidplayfp)
