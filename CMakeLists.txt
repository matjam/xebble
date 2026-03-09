cmake_minimum_required(VERSION 4.0)
project(xebble VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# On Linux the vcpkg-built vulkan-loader would end up in each binary's RUNPATH,
# shadowing the system libvulkan.so.1 that GLFW dlopen()s internally to detect
# Vulkan support.  The system loader already lives in the ldconfig cache
# (/usr/lib), so no RPATH is needed at all for it.
if(UNIX AND NOT APPLE)
    set(CMAKE_SKIP_RPATH TRUE)
endif()

# Suppress "ignoring duplicate libraries" warning on macOS.
# vcpkg's freetype port lists zlib twice in its link interface: once as
# the ZLIB::ZLIB imported target and again as a hard-coded libz.a path
# (a transitive dependency from libpng). Both resolve to the same archive,
# so the duplicate is harmless.
if(APPLE)
    add_link_options("LINKER:-no_warn_duplicate_libraries")
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(freetype CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(miniz CONFIG REQUIRED)
find_package(libxmp CONFIG REQUIRED)

# miniaudio is header-only; vcpkg does not install a CMake config file for it.
# Locate the header manually and create a lightweight imported target.
find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h
    HINTS
        "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/include"
    REQUIRED
)
if(NOT TARGET miniaudio::miniaudio)
    add_library(miniaudio::miniaudio INTERFACE IMPORTED)
    target_include_directories(miniaudio::miniaudio INTERFACE "${MINIAUDIO_INCLUDE_DIR}")
endif()
message(STATUS "Found miniaudio: ${MINIAUDIO_INCLUDE_DIR}/miniaudio.h")

# shaderc for shader compilation
find_package(unofficial-shaderc CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Core library targets
# ---------------------------------------------------------------------------
add_subdirectory(shaders)
add_subdirectory(src/sidplay)
add_subdirectory(src)

# ---------------------------------------------------------------------------
# Examples, tests, MoltenVK bundling — only when building xebble directly
# ---------------------------------------------------------------------------
if(PROJECT_IS_TOP_LEVEL)
    option(XEBBLE_PROFILE "Enable gperftools CPU profiling in ex06_ecs" OFF)

    find_package(GTest CONFIG REQUIRED)

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
            message(FATAL_ERROR
                "MoltenVK not found. Install via: brew install molten-vk\n"
                "Or set MOLTENVK_LIB to the path of libMoltenVK.dylib")
        endif()
    endif()

    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install rules — allow consumers to find_package(xebble)
# ---------------------------------------------------------------------------

# Install public headers
install(DIRECTORY include/xebble
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

# Install compiled SPIR-V shaders
set(XEBBLE_SHADER_INSTALL_DIR ${CMAKE_INSTALL_DATADIR}/xebble/shaders)
install(FILES
    ${CMAKE_BINARY_DIR}/shaders/sprite.vert.spv
    ${CMAKE_BINARY_DIR}/shaders/sprite.frag.spv
    ${CMAKE_BINARY_DIR}/shaders/blit.vert.spv
    ${CMAKE_BINARY_DIR}/shaders/blit.frag.spv
    DESTINATION ${XEBBLE_SHADER_INSTALL_DIR}
)

# Install library targets and generate export set
install(TARGETS xebble sidplayfp
    EXPORT xebbleTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Install the export set
install(EXPORT xebbleTargets
    FILE xebbleTargets.cmake
    NAMESPACE xebble::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xebble
)

# Generate and install the package config file
configure_package_config_file(
    cmake/xebbleConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/xebbleConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xebble
    PATH_VARS XEBBLE_SHADER_INSTALL_DIR
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/xebbleConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/xebbleConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/xebbleConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xebble
)
