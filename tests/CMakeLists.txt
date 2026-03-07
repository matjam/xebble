add_executable(xebble_tests
    test_types.cpp
    test_event.cpp
    test_spritesheet.cpp
    test_tilemap.cpp
    test_font.cpp
    test_asset_manager.cpp
    test_ecs.cpp
    test_embedded_font.cpp
    test_ui.cpp
    test_grid.cpp
    test_rng.cpp
    test_scene.cpp
    test_fov.cpp
    test_pathfinding.cpp
    test_turn.cpp
    test_msglog.cpp
    test_procgen.cpp
    test_serial.cpp
)

target_link_libraries(xebble_tests PRIVATE
    xebble
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(xebble_tests)
