add_executable(xebble_tests
    test_types.cpp
    test_event.cpp
)

target_link_libraries(xebble_tests PRIVATE
    xebble
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(xebble_tests)
