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

# On macOS, find and bundle MoltenVK so binaries are self-contained
if(APPLE)
    find_library(MOLTENVK_LIB MoltenVK HINTS /opt/homebrew/lib /usr/local/lib)
    if(MOLTENVK_LIB)
        message(STATUS "Found MoltenVK: ${MOLTENVK_LIB}")

        # Resolve symlinks to get the real dylib path
        get_filename_component(MOLTENVK_REAL "${MOLTENVK_LIB}" REALPATH)

        # Copy dylib to build output
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
        file(COPY_FILE "${MOLTENVK_REAL}" "${CMAKE_BINARY_DIR}/lib/libMoltenVK.dylib"
             ONLY_IF_DIFFERENT)

        # Generate ICD JSON pointing to the bundled dylib
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/vulkan/icd.d")
        file(WRITE "${CMAKE_BINARY_DIR}/vulkan/icd.d/MoltenVK_icd.json"
            "{\n"
            "    \"file_format_version\": \"1.0.0\",\n"
            "    \"ICD\": {\n"
            "        \"library_path\": \"${CMAKE_BINARY_DIR}/lib/libMoltenVK.dylib\",\n"
            "        \"api_version\": \"1.2.0\",\n"
            "        \"is_portability_driver\": true\n"
            "    }\n"
            "}\n"
        )

        set(XEBBLE_MOLTENVK_ICD_PATH "${CMAKE_BINARY_DIR}/vulkan/icd.d/MoltenVK_icd.json"
            CACHE INTERNAL "Path to bundled MoltenVK ICD JSON")

        message(STATUS "MoltenVK bundled to: ${CMAKE_BINARY_DIR}/lib/libMoltenVK.dylib")
    else()
        message(WARNING "MoltenVK not found — Vulkan may not work on macOS without it")
    endif()
endif()

enable_testing()
add_subdirectory(tests)
