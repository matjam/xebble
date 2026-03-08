# Helper macro to add a self-contained example with macOS .app bundle support.
# Usage: xebble_example(TARGET dir [NEEDS_STB])
macro(xebble_example TARGET DIR)
    add_executable(${TARGET} ${DIR}/main.cpp)
    target_link_libraries(${TARGET} PRIVATE xebble)
    target_include_directories(${TARGET} PRIVATE
        ${Stb_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}   # lets examples reach shared/
    )
    add_dependencies(${TARGET} xebble_shaders)

    if(APPLE)
        # Convert "ex01_hello_world" → "Ex01HelloWorld" for bundle name.
        string(REPLACE "_" ";" _parts ${TARGET})
        set(_bname "")
        foreach(_p ${_parts})
            string(SUBSTRING ${_p} 0 1 _first)
            string(SUBSTRING ${_p} 1 -1 _rest)
            string(TOUPPER ${_first} _first)
            string(APPEND _bname "${_first}${_rest}")
        endforeach()

        # Bundle ID: replace underscores with hyphens.
        string(REPLACE "_" "-" _bid ${TARGET})

        set_target_properties(${TARGET} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME        "${_bname}"
            MACOSX_BUNDLE_GUI_IDENTIFIER     "com.xebble.${_bid}"
            MACOSX_BUNDLE_BUNDLE_VERSION     "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_INFO_PLIST         "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        )

        set(SHADER_DIR "${CMAKE_BINARY_DIR}/shaders")
        set(SHADER_FILES
            "${SHADER_DIR}/sprite.vert.spv"
            "${SHADER_DIR}/sprite.frag.spv"
            "${SHADER_DIR}/blit.vert.spv"
            "${SHADER_DIR}/blit.frag.spv"
        )
        set_source_files_properties(${SHADER_FILES} PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources/shaders"
            GENERATED TRUE
        )
        target_sources(${TARGET} PRIVATE ${SHADER_FILES})

        if(MOLTENVK_LIB)
            get_filename_component(MOLTENVK_REAL "${MOLTENVK_LIB}" REALPATH)
            set_source_files_properties("${MOLTENVK_REAL}" PROPERTIES
                MACOSX_PACKAGE_LOCATION "Frameworks"
            )
            target_sources(${TARGET} PRIVATE "${MOLTENVK_REAL}")
            set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/MoltenVK_icd.json" PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources/vulkan/icd.d"
            )
            target_sources(${TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/MoltenVK_icd.json")
        endif()
    endif()
endmacro()

# ---------------------------------------------------------------------------
# basic_tilemap — original reference example (keep as-is)
# ---------------------------------------------------------------------------
add_executable(basic_tilemap basic_tilemap/main.cpp)
target_link_libraries(basic_tilemap PRIVATE xebble)
target_include_directories(basic_tilemap PRIVATE ${Stb_INCLUDE_DIR})
add_dependencies(basic_tilemap xebble_shaders)

if(APPLE)
    set_target_properties(basic_tilemap PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "BasicTilemap"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.xebble.basic-tilemap"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    )

    set(SHADER_DIR "${CMAKE_BINARY_DIR}/shaders")
    set(SHADER_FILES
        "${SHADER_DIR}/sprite.vert.spv"
        "${SHADER_DIR}/sprite.frag.spv"
        "${SHADER_DIR}/blit.vert.spv"
        "${SHADER_DIR}/blit.frag.spv"
    )
    set_source_files_properties(${SHADER_FILES} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/shaders"
        GENERATED TRUE
    )
    target_sources(basic_tilemap PRIVATE ${SHADER_FILES})

    file(GLOB ASSET_FILES "${CMAKE_CURRENT_SOURCE_DIR}/basic_tilemap/assets/*")
    set_source_files_properties(${ASSET_FILES} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/assets"
    )
    target_sources(basic_tilemap PRIVATE ${ASSET_FILES})

    if(MOLTENVK_LIB)
        get_filename_component(MOLTENVK_REAL "${MOLTENVK_LIB}" REALPATH)
        set_source_files_properties("${MOLTENVK_REAL}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Frameworks"
        )
        target_sources(basic_tilemap PRIVATE "${MOLTENVK_REAL}")

        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/MoltenVK_icd.json"
            "{\n"
            "    \"file_format_version\": \"1.0.0\",\n"
            "    \"ICD\": {\n"
            "        \"library_path\": \"../../../Frameworks/libMoltenVK.dylib\",\n"
            "        \"api_version\": \"1.2.0\",\n"
            "        \"is_portability_driver\": true\n"
            "    }\n"
            "}\n"
        )
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/MoltenVK_icd.json" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources/vulkan/icd.d"
        )
        target_sources(basic_tilemap PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/MoltenVK_icd.json")
    endif()
endif()

# Generate the ICD JSON once (used by the macro-registered examples too).
if(APPLE AND MOLTENVK_LIB)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/MoltenVK_icd.json"
        "{\n"
        "    \"file_format_version\": \"1.0.0\",\n"
        "    \"ICD\": {\n"
        "        \"library_path\": \"../../../Frameworks/libMoltenVK.dylib\",\n"
        "        \"api_version\": \"1.2.0\",\n"
        "        \"is_portability_driver\": true\n"
        "    }\n"
        "}\n"
    )
endif()

# ---------------------------------------------------------------------------
# Examples ex01 – ex13
# ---------------------------------------------------------------------------
xebble_example(ex01_hello_world     ex01_hello_world)
xebble_example(ex02_tilemap         ex02_tilemap)
xebble_example(ex03_sprites         ex03_sprites)
xebble_example(ex04_input           ex04_input)
xebble_example(ex05_ui              ex05_ui)
xebble_example(ex06_ecs             ex06_ecs)
if(XEBBLE_PROFILE)
    find_library(GPERFTOOLS_PROFILER profiler HINTS /opt/homebrew/lib)
    if(GPERFTOOLS_PROFILER)
        target_compile_definitions(ex06_ecs PRIVATE XEBBLE_PROFILE)
        target_include_directories(ex06_ecs PRIVATE /opt/homebrew/include)
        target_link_libraries(ex06_ecs PRIVATE ${GPERFTOOLS_PROFILER})
    else()
        message(FATAL_ERROR "XEBBLE_PROFILE enabled but gperftools not found. Install via: brew install gperftools")
    endif()
endif()
xebble_example(ex07_scene           ex07_scene)
xebble_example(ex08_grid_fov_path   ex08_grid_fov_path)
xebble_example(ex09_procgen         ex09_procgen)
xebble_example(ex10_rng             ex10_rng)
xebble_example(ex11_turn            ex11_turn)
xebble_example(ex12_msglog          ex12_msglog)
xebble_example(ex13_serial          ex13_serial)
xebble_example(ex14_fonts           ex14_fonts)
xebble_example(ex15_atlas_debug     ex15_atlas_debug)
xebble_example(ex16_graphics_settings ex16_graphics_settings)
xebble_example(ex17_audio           ex17_audio)

# Copy bundled audio assets next to the ex17_audio binary so they are found
# at runtime regardless of the working directory.
add_custom_command(TARGET ex17_audio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ex17_audio/test.sid"
        "$<TARGET_FILE_DIR:ex17_audio>/test.sid"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ex17_audio/coin.wav"
        "$<TARGET_FILE_DIR:ex17_audio>/coin.wav"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ex17_audio/test.mp3"
        "$<TARGET_FILE_DIR:ex17_audio>/test.mp3"
    COMMENT "Copying audio assets next to ex17_audio"
)
