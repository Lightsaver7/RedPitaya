##
## Unit tests for tdms_lib. No Python anywhere: the library has no independent
## third-party reference reader available here, so correctness is pinned two
## ways instead.
##
##  1. Golden byte arrays, built declaratively with tests/support/spec_builder.h
##     - an encoder that shares no code with TDMS::BinaryStream - and derived by
##     hand from the TDMS 2.0 specification. test_writer_golden.cpp asserts the
##     writer produces exactly those bytes.
##
##  2. test_reader_golden.cpp feeds the SAME hand-derived bytes to Reader and
##     never mentions Writer. That is what stops a bug shared by writer and
##     reader from passing: a round trip through a symmetric misunderstanding
##     succeeds, a pinned byte pattern does not.
##
## test_roundtrip.cpp sits on top of both. The remaining files are regression
## suites for the crashes, hangs and memory defects, plus a deterministic
## mutation sweep over the goldens for robustness.
##
## Build:  cmake -S . -B build -DBUILD_TESTS=ON [-DTDMS_ENABLE_SANITIZERS=ON]
##         cmake --build build && (cd build && ctest --output-on-failure)
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
else()
    message(STATUS "Using system GTest")
endif()

add_executable(tdms_tests
    test_writer_golden.cpp
    test_reader_golden.cpp
    test_roundtrip.cpp
    test_datatype.cpp
    test_binary_stream.cpp
    test_reader_interleaved.cpp
    test_file_lifecycle.cpp
    test_file_incremental_metadata.cpp
    test_malformed.cpp
    test_widths_and_abi.cpp
    test_print.cpp
    test_timestamp.cpp
    test_api_compat_no_using.cpp
    test_api_compat_std_using.cpp
)

target_include_directories(tdms_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(tdms_tests PRIVATE ${PROJECT_NAME} GTest::gtest_main)
target_compile_features(tdms_tests PRIVATE cxx_std_17)

if(TDMS_ENABLE_SANITIZERS)
    target_compile_options(tdms_tests PRIVATE ${_tdms_sanitizer_flags})
    target_link_options(tdms_tests PRIVATE ${_tdms_sanitizer_flags})
endif()

## gtest_discover_tests runs the test binary at build time, which cannot work
## when cross-compiling for the RedPitaya ARM target.
## Labelled so the post-build run can select exactly this library's tests when
## several libraries are configured into one build tree.
if(CMAKE_CROSSCOMPILING)
    add_test(NAME tdms_tests COMMAND tdms_tests)
    set_tests_properties(tdms_tests PROPERTIES LABELS tdms_lib)
else()
    include(GoogleTest)
    gtest_discover_tests(tdms_tests PROPERTIES LABELS tdms_lib)
endif()
