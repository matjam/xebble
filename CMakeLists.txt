cmake_minimum_required(VERSION 4.0)
project(xebble VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(freetype CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(minizip-ng CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

# shaderc for shader compilation
find_package(unofficial-shaderc CONFIG REQUIRED)

add_subdirectory(shaders)
add_subdirectory(src)
add_subdirectory(examples)

enable_testing()
add_subdirectory(tests)
