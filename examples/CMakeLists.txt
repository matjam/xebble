add_executable(basic_tilemap basic_tilemap/main.cpp)
target_link_libraries(basic_tilemap PRIVATE xebble)
target_include_directories(basic_tilemap PRIVATE ${Stb_INCLUDE_DIR})
add_dependencies(basic_tilemap xebble_shaders)
