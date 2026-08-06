##
## Unit tests for data_lib.
##
## Built only when BUILD_TESTS is ON, and then run automatically right after the
## library builds - see the rp_data_lib_run_tests target in ../CMakeLists.txt.
##
## tests/support.h stands in for the DMA mapping: the library is normally handed
## uio_lib::MemoryRegionT descriptors pointing at device memory, and the Arena
## helper produces descriptors of the same shape over an ordinary heap block, so
## every buffer path can be driven without hardware.
##
## test_buffers_cached.cpp deliberately spends about two seconds in two tests:
## the ring buffer's full and empty cases only show up by waiting out the one
## second timeout in writeBuffer(true) and readBuffer().
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

add_executable(data_lib_tests
    test_buffer.cpp
    test_buffers_pack.cpp
    test_buffers_cached.cpp
    test_network_header.cpp
    test_neon.cpp
    test_signal.cpp
)

target_include_directories(data_lib_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
    ${COMMON_LIB_DIR}
)

target_link_directories(data_lib_tests
    PRIVATE
    ${CMAKE_BINARY_DIR}/bin/
    ${CMAKE_BINARY_DIR}/lib/
    )

## buffers_cached.h pulls in settings_lib/channels.hpp and
## uio_lib/memory_manager.h, and the library logs through logger_lib; none of
## those reach the test binary through data_lib's own link interface.
target_link_libraries(data_lib_tests PRIVATE
    ${PROJECT_NAME}
    settings_lib
    logger_lib
    GTest::gtest_main
    pthread)
target_compile_features(data_lib_tests PRIVATE cxx_std_20)

if(DATA_ENABLE_SANITIZERS)
    target_compile_options(data_lib_tests PRIVATE ${_data_sanitizer_flags})
    target_link_options(data_lib_tests PRIVATE ${_data_sanitizer_flags})
endif()

## Labelled so the post-build run can select exactly this library's tests when
## several libraries are configured into one build tree.
if(CMAKE_CROSSCOMPILING)
    ## gtest_discover_tests runs the test binary on the host at build time,
    ## which cannot work for an ARM binary.
    add_test(NAME data_lib_tests COMMAND data_lib_tests)
    set_tests_properties(data_lib_tests PROPERTIES LABELS data_lib)
else()
    include(GoogleTest)
    gtest_discover_tests(data_lib_tests PROPERTIES LABELS data_lib)
endif()
