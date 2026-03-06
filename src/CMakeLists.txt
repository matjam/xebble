add_library(xebble STATIC
    log.cpp
    window.cpp
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
    world.cpp
    builtin_systems.cpp
)

target_include_directories(xebble PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(xebble PUBLIC
    Vulkan::Vulkan
    glfw
    GPUOpen::VulkanMemoryAllocator
    freetype
    glm::glm
    tomlplusplus::tomlplusplus
    MINIZIP::minizip-ng
)

# stb is header-only, add include path
target_include_directories(xebble PRIVATE ${Stb_INCLUDE_DIR})

target_compile_features(xebble PUBLIC cxx_std_23)

# Pass bundled MoltenVK ICD path if available
if(XEBBLE_MOLTENVK_ICD_PATH)
    target_compile_definitions(xebble PRIVATE
        XEBBLE_MOLTENVK_ICD="${XEBBLE_MOLTENVK_ICD_PATH}"
    )
endif()
