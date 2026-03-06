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

    # Bundle shaders into Resources/shaders/
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

    # Bundle assets into Resources/assets/
    file(GLOB ASSET_FILES "${CMAKE_CURRENT_SOURCE_DIR}/basic_tilemap/assets/*")
    set_source_files_properties(${ASSET_FILES} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/assets"
    )
    target_sources(basic_tilemap PRIVATE ${ASSET_FILES})

    # Bundle MoltenVK into Frameworks/
    if(MOLTENVK_LIB)
        get_filename_component(MOLTENVK_REAL "${MOLTENVK_LIB}" REALPATH)
        set_source_files_properties("${MOLTENVK_REAL}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Frameworks"
        )
        target_sources(basic_tilemap PRIVATE "${MOLTENVK_REAL}")

        # Generate ICD JSON with path relative to its location in the bundle:
        # JSON at: Contents/Resources/vulkan/icd.d/MoltenVK_icd.json
        # dylib at: Contents/Frameworks/libMoltenVK.dylib
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
