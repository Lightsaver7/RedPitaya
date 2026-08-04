#pragma once

#include <filesystem>
#include <string>

// TEST_FIXTURES_DIR is injected by tests/CMakeLists.txt via
// target_compile_definitions() and points at a scratch directory shared by
// every writer test that needs to round-trip through a real file on disk.
//
// It is only guaranteed to exist right after CMake configures the build
// (see the one-off file(MAKE_DIRECTORY ...) there); CTest's
// cleanup_test_fixtures test removes it again once the whole tests/
// suite finishes. Since ctest can be invoked many times against one
// configured build, any test that writes into TEST_FIXTURES_DIR must
// (re)create it itself first rather than assume it is still there.
inline auto EnsureTestFixturesDir() -> std::string {
    std::filesystem::create_directories(TEST_FIXTURES_DIR);
    return TEST_FIXTURES_DIR;
}

inline auto TestFixturePath(const std::string& fileName) -> std::string {
    return EnsureTestFixturesDir() + "/" + fileName;
}
