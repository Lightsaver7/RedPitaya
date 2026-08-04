##
## Unit tests for rp-formatter.
##
## C++ side (GoogleTest): exercises the public CFormatter facade for the
## WAV, CSV and TDMS writers. Split across one file per writer:
##   - test_wav_writer.cpp
##   - test_csv_writer.cpp
##   - test_tdms_writer.cpp
## all compiled into a single "rp_formatter_tests" binary and registered
## individually with CTest via gtest_discover_tests().
##
## TDMS content correctness is NOT re-checked with a hand-written C++
## parser: verify_tdms.py reads the fixture files the C++ tests produce
## with the independent, third-party npTDMS library instead. CTest
## fixtures (FIXTURES_SETUP/FIXTURES_REQUIRED/FIXTURES_CLEANUP) guarantee
## it runs after the C++ suite and that the fixture directory is cleaned
## up afterwards either way.
##
## Python side: test_rp_formatter_swig.py exercises the SWIG-generated
## `rp_formatter` Python module directly (only built/run when
## BUILD_PYTHON_MODULE is ON).
##


include(FetchContent)

find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(STATUS "System GTest not found - fetching GoogleTest via FetchContent")

    get_property(_rp_formatter_cached_cc CACHE CMAKE_C_COMPILER PROPERTY VALUE)
    get_property(_rp_formatter_cached_cxx CACHE CMAKE_CXX_COMPILER PROPERTY VALUE)
    if(_rp_formatter_cached_cc)
        set(CMAKE_C_COMPILER "${_rp_formatter_cached_cc}")
    endif()
    if(_rp_formatter_cached_cxx)
        set(CMAKE_CXX_COMPILER "${_rp_formatter_cached_cxx}")
    endif()

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
else()
    message(STATUS "Using system GTest ${GTest_VERSION}")
endif()

add_custom_target(rp_formatter_run_tests ALL
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS rp_formatter_tests
    COMMENT "Running rp-formatter unit tests (ctest) - build fails if any test fails"
    VERBATIM
)
if(BUILD_PYTHON_MODULE AND TARGET rp_formatter_py)
    add_dependencies(rp_formatter_run_tests rp_formatter_py)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Scratch directory shared by all writer tests for anything that has to
# round-trip through an actual file on disk (as opposed to writeToStream()
# into an in-memory buffer).
set(TEST_FIXTURES_DIR ${CMAKE_CURRENT_BINARY_DIR}/fixtures)
file(MAKE_DIRECTORY ${TEST_FIXTURES_DIR})

add_executable(rp_formatter_tests
    test_wav_writer.cpp
    test_csv_writer.cpp
    test_tdms_writer.cpp
)

target_include_directories(rp_formatter_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/writers
    ${PROJECT_SOURCE_DIR}/src/tdms
)

target_compile_definitions(rp_formatter_tests PRIVATE
    TEST_FIXTURES_DIR="${TEST_FIXTURES_DIR}"
)

target_link_libraries(rp_formatter_tests PRIVATE
    ${PROJECT_NAME}-obj
    GTest::gtest_main
    -lm -lpthread
)

include(GoogleTest)
# Individually registers every TEST()/TEST_F() in the binary with CTest, so
# a WAV or CSV regression is reported on its own line instead of hiding
# inside one big "rp_formatter_tests" pass/fail. Deliberately NOT given a
# FIXTURES_SETUP here: CTest only runs FIXTURES_REQUIRED tests once EVERY
# FIXTURES_SETUP test for that fixture has passed, and a failing WAV/CSV
# test (see the BUG-WAV-*/BUG-CSV-1 tests, which are expected to fail on
# today's code) would then also block npTDMS verification and the SWIG
# suite below for a completely unrelated reason. The fixture is instead
# produced by the dedicated re-run further down, filtered to only the
# TDMS tests.
gtest_discover_tests(rp_formatter_tests)

add_test(
    NAME tdms_fixture_setup
    COMMAND rp_formatter_tests --gtest_filter=TdmsWriter.*
)
set_tests_properties(tdms_fixture_setup PROPERTIES
    FIXTURES_SETUP tdms_fixture
)

add_test(
    NAME tdms_verify_with_nptdms
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/verify_tdms.py ${TEST_FIXTURES_DIR}
)
set_tests_properties(tdms_verify_with_nptdms PROPERTIES
    FIXTURES_REQUIRED tdms_fixture
)

add_test(
    NAME cleanup_test_fixtures
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${TEST_FIXTURES_DIR}
)
set_tests_properties(cleanup_test_fixtures PROPERTIES
    FIXTURES_CLEANUP tdms_fixture
)

## SWIG / Python wrapper tests -------------------------------------------
## Only registered when the python module is actually being built, since
## they import the compiled `rp_formatter` extension.
if(BUILD_PYTHON_MODULE AND TARGET rp_formatter_py)
    add_test(
        NAME swig_wrapper_tests
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR} -p "test_rp_formatter_swig.py" -v
    )
    # SWIG_ADD_LIBRARY(rp_formatter_py ...) generates two artifacts that both
    # need to be importable: the compiled extension (named after the CMake
    # target, _rp_formatter_py.*, under CMAKE_LIBRARY_OUTPUT_DIRECTORY) and
    # the pure-Python wrapper module (named after the %module directive in
    # rp_formatter.i, rp_formatter.py, generated into the top-level binary
    # dir). Both directories go on PYTHONPATH.
    set_tests_properties(swig_wrapper_tests PROPERTIES
        ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:rp_formatter_py>:${CMAKE_BINARY_DIR}"
        WORKING_DIRECTORY ${TEST_FIXTURES_DIR}
        # Reuses the same scratch directory as the C++ suite; require the
        # fixture so CTest never runs cleanup_test_fixtures out from under
        # this test (matters when running with e.g. `ctest -j`).
        FIXTURES_REQUIRED tdms_fixture
    )
endif()
