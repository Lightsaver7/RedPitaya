/**
 * Byte-comparison helpers and small RAII utilities for the TDMS tests.
 */

#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "spec_builder.h"

namespace tdms_test {

// getpid() is POSIX and absent from the MinGW runtime, which builds this tree
// for Windows; _getpid() in <process.h> is the equivalent there. The id only has
// to make the scratch directory unique between concurrent runs of the binary.
inline auto CurrentProcessId() -> long {
#if defined(_WIN32)
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

inline auto HexDump(const Bytes& bytes) -> std::string {
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 3);
    for (std::size_t i = 0; i < bytes.size(); i++) {
        if (i && i % 16 == 0) {
            out.push_back('\n');
        } else if (i) {
            out.push_back(' ');
        }
        out.push_back(digits[bytes[i] >> 4]);
        out.push_back(digits[bytes[i] & 0x0F]);
    }
    return out;
}

inline auto ToBytes(const std::string& text) -> Bytes {
    return Bytes(text.begin(), text.end());
}

// Full-buffer comparison that reports the first differing offset and a hex dump
// of both sides, which is what makes a golden failure diagnosable.
inline auto ExpectBytesEq(const Bytes& actual, const Bytes& expected) -> void {
    if (actual == expected) {
        return;
    }
    std::size_t at = 0;
    while (at < actual.size() && at < expected.size() && actual[at] == expected[at]) {
        at++;
    }
    ADD_FAILURE() << "byte mismatch\n"
                  << "  sizes: actual " << actual.size() << ", expected " << expected.size() << "\n"
                  << "  first difference at offset " << at << "\n"
                  << "--- actual ---\n"
                  << HexDump(actual) << "\n--- expected ---\n"
                  << HexDump(expected);
}

inline auto MakeStream(const Bytes& bytes) -> std::stringstream {
    std::stringstream stream(std::string(bytes.begin(), bytes.end()), std::ios::in | std::ios::out | std::ios::binary);
    return stream;
}

inline auto EmptyStream() -> std::stringstream {
    return std::stringstream(std::ios::in | std::ios::out | std::ios::binary);
}

// A uniquely named file in a temporary directory, removed on destruction, so
// the File tests never collide or leave anything behind.
class TempFile {
   public:
    explicit TempFile(const std::string& name) {
        m_dir = std::filesystem::temp_directory_path() / ("tdms_test_" + std::to_string(++s_counter) + "_" + std::to_string(CurrentProcessId()));
        std::filesystem::create_directories(m_dir);
        m_path = (m_dir / name).string();
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    ~TempFile() {
        std::error_code ignored;
        std::filesystem::remove_all(m_dir, ignored);
    }

    auto path() const -> const std::string& { return m_path; }

    auto write(const Bytes& bytes) const -> void {
        std::ofstream out(m_path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    auto read() const -> Bytes {
        std::ifstream in(m_path, std::ios::binary);
        return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

   private:
    static int s_counter;
    std::filesystem::path m_dir;
    std::string m_path;
};

inline int TempFile::s_counter = 0;

}  // namespace tdms_test
