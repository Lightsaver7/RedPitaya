##
## Unit tests for settings_lib.
##
## Built only when BUILD_TESTS is ON, and then run automatically right after the
## library builds - see the rp_settings_lib_run_tests target in ../CMakeLists.txt.
##
## The suite is split by layer: the ENUM macro and the Channels bitmask are
## pure header behaviour, while the CStreamSettings files exercise the accessor
## contract, the string key/value interface and the JSON layer separately, so a
## failure names the layer that broke.
##

find_package(GTest QUIET)
if(NOT TARGET GTest::gtest_main)
    message(STATUS "System GTest not found - fetching GoogleTest via FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(settings_lib_tests
    test_enums.cpp
    test_channels.cpp
    test_stream_settings.cpp
    test_stream_settings_keys.cpp
    test_stream_settings_json.cpp
)

target_include_directories(settings_lib_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
    ${COMMON_LIB_DIR}
)

target_link_directories(settings_lib_tests
    PRIVATE
    ${CMAKE_BINARY_DIR}/bin/
    ${CMAKE_BINARY_DIR}/lib/
    )


## stream_settings.h pulls in data_lib/network_header.h for sizeHeader(), and
## the library itself logs through logger_lib; neither reaches the test binary
## through settings_lib's own link interface.
target_link_libraries(settings_lib_tests PRIVATE
    ${PROJECT_NAME}
    data_lib
    logger_lib
    GTest::gtest_main
    pthread)
target_compile_features(settings_lib_tests PRIVATE cxx_std_20)

if(SETTINGS_ENABLE_SANITIZERS)
    target_compile_options(settings_lib_tests PRIVATE ${_settings_sanitizer_flags})
    target_link_options(settings_lib_tests PRIVATE ${_settings_sanitizer_flags})
endif()

## Labelled so the post-build run can select exactly this library's tests when
## several libraries are configured into one build tree.
if(CMAKE_CROSSCOMPILING)
    ## gtest_discover_tests runs the test binary on the host at build time,
    ## which cannot work for an ARM binary.
    add_test(NAME settings_lib_tests COMMAND settings_lib_tests)
    set_tests_properties(settings_lib_tests PROPERTIES LABELS settings_lib)
else()
    include(GoogleTest)
    gtest_discover_tests(settings_lib_tests PROPERTIES LABELS settings_lib)
endif()
