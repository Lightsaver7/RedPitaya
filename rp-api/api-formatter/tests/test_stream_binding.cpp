/**
 * Unit tests for the output-stream binding enforced by SStreamGuard
 * (src/writers/common.h) and wired into all three writers.
 *
 * Contract under test: the first writeToStream()/writeToFile() call binds a
 * CFormatter to that one stream. Every later write must target the same
 * stream until resetWriter() clears the binding; anything else is refused
 * with a false return instead of producing a malformed file. Mode-specific
 * consequences of the binding live in the per-writer test files - this file
 * covers the contract itself, uniformly across WAV, CSV and TDMS.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rp_formatter.h"
#include "test_support.h"

using namespace rp_formatter_api;

namespace {

const rp_mode_t kAllModes[] = {RP_F_WAV, RP_F_CSV, RP_F_TDMS};

std::string ModeName(rp_mode_t mode) {
    switch (mode) {
        case RP_F_WAV:
            return "RP_F_WAV";
        case RP_F_CSV:
            return "RP_F_CSV";
        case RP_F_TDMS:
            return "RP_F_TDMS";
    }
    return "unknown";
}

std::string FileExtension(rp_mode_t mode) {
    switch (mode) {
        case RP_F_WAV:
            return ".wav";
        case RP_F_CSV:
            return ".csv";
        case RP_F_TDMS:
            return ".tdms";
    }
    return ".bin";
}

std::streamoff FileSize(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        return -1;
    }
    return f.tellg();
}

}  // namespace

TEST(StreamBinding, WritingTwiceToTheSameStreamIsAllowedInEveryMode) {
    for (auto mode : kAllModes) {
        CFormatter formatter(mode, 44100);
        std::vector<float> ch1 = {1.f, 2.f};
        formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

        std::stringstream mem;
        EXPECT_TRUE(formatter.writeToStream(&mem)) << ModeName(mode);
        EXPECT_TRUE(formatter.writeToStream(&mem)) << ModeName(mode);
    }
}

TEST(StreamBinding, SwappingTheStreamIsRejectedInEveryMode) {
    for (auto mode : kAllModes) {
        CFormatter formatter(mode, 44100);
        std::vector<float> ch1 = {1.f, 2.f};
        formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

        std::stringstream first;
        ASSERT_TRUE(formatter.writeToStream(&first)) << ModeName(mode);
        std::stringstream second;

        EXPECT_FALSE(formatter.writeToStream(&second)) << ModeName(mode);
        EXPECT_TRUE(second.str().empty()) << ModeName(mode);
    }
}

TEST(StreamBinding, RejectedWriteLeavesTheBoundStreamUntouched) {
    for (auto mode : kAllModes) {
        CFormatter formatter(mode, 44100);
        std::vector<float> ch1 = {1.f, 2.f};
        formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

        std::stringstream first;
        ASSERT_TRUE(formatter.writeToStream(&first)) << ModeName(mode);
        const std::string before = first.str();

        std::stringstream second;
        ASSERT_FALSE(formatter.writeToStream(&second)) << ModeName(mode);

        EXPECT_EQ(first.str(), before) << ModeName(mode);
    }
}

TEST(StreamBinding, ResetWriterClearsTheBindingInEveryMode) {
    for (auto mode : kAllModes) {
        CFormatter formatter(mode, 44100);
        std::vector<float> ch1 = {1.f, 2.f};
        formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

        std::stringstream first;
        ASSERT_TRUE(formatter.writeToStream(&first)) << ModeName(mode);
        std::stringstream second;
        ASSERT_FALSE(formatter.writeToStream(&second)) << ModeName(mode);

        formatter.resetWriter();

        EXPECT_TRUE(formatter.writeToStream(&second)) << ModeName(mode);
        EXPECT_FALSE(second.str().empty()) << ModeName(mode);
    }
}

TEST(StreamBinding, RepeatedWriteToFileStaysBoundToTheOpenFile) {
    for (auto mode : kAllModes) {
        CFormatter formatter(mode, 44100);
        std::vector<float> ch1 = {1.f, 2.f};
        formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

        const std::string path = TestFixturePath("binding_same_file" + FileExtension(mode));
        ASSERT_TRUE(formatter.openFile(path)) << ModeName(mode);
        EXPECT_TRUE(formatter.writeToFile()) << ModeName(mode);
        EXPECT_TRUE(formatter.writeToFile()) << ModeName(mode);
        ASSERT_TRUE(formatter.closeFile()) << ModeName(mode);
    }
}

TEST(StreamBinding, ReopeningAFileWithoutResetWriterIsRejected) {
    // openFile() allocates a fresh std::fstream, so the second file is a
    // different stream even when the path is identical. Without
    // resetWriter() the writer refuses to write into it, and the new file
    // stays empty rather than receiving headerless or mis-patched data.
    for (auto mode : kAllModes) {
        CFormatter formatter(mode, 44100);
        std::vector<float> ch1 = {1.f, 2.f};
        formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

        const std::string first = TestFixturePath("binding_first" + FileExtension(mode));
        ASSERT_TRUE(formatter.openFile(first)) << ModeName(mode);
        ASSERT_TRUE(formatter.writeToFile()) << ModeName(mode);
        ASSERT_TRUE(formatter.closeFile()) << ModeName(mode);

        const std::string second = TestFixturePath("binding_second" + FileExtension(mode));
        ASSERT_TRUE(formatter.openFile(second)) << ModeName(mode);
        EXPECT_FALSE(formatter.writeToFile()) << ModeName(mode);
        ASSERT_TRUE(formatter.closeFile()) << ModeName(mode);

        EXPECT_GT(FileSize(first), 0) << ModeName(mode);
        EXPECT_EQ(FileSize(second), 0) << ModeName(mode);
    }
}

TEST(StreamBinding, ReopeningAFileAfterResetWriterProducesAnEquivalentFile) {
    for (auto mode : kAllModes) {
        CFormatter formatter(mode, 44100);
        std::vector<float> ch1 = {1.f, 2.f};
        formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

        const std::string first = TestFixturePath("rebind_first" + FileExtension(mode));
        ASSERT_TRUE(formatter.openFile(first)) << ModeName(mode);
        ASSERT_TRUE(formatter.writeToFile()) << ModeName(mode);
        ASSERT_TRUE(formatter.closeFile()) << ModeName(mode);

        formatter.resetWriter();

        const std::string second = TestFixturePath("rebind_second" + FileExtension(mode));
        ASSERT_TRUE(formatter.openFile(second)) << ModeName(mode);
        EXPECT_TRUE(formatter.writeToFile()) << ModeName(mode);
        ASSERT_TRUE(formatter.closeFile()) << ModeName(mode);

        EXPECT_GT(FileSize(second), 0) << ModeName(mode);
        // TDMS embeds a timestamp property, so only the size is comparable.
        EXPECT_EQ(FileSize(second), FileSize(first)) << ModeName(mode);
    }
}

TEST(StreamBinding, AStreamCannotBeMixedWithAnOpenFile) {
    for (auto mode : kAllModes) {
        CFormatter formatter(mode, 44100);
        std::vector<float> ch1 = {1.f, 2.f};
        formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

        std::stringstream mem;
        ASSERT_TRUE(formatter.writeToStream(&mem)) << ModeName(mode);

        const std::string path = TestFixturePath("binding_mixed" + FileExtension(mode));
        ASSERT_TRUE(formatter.openFile(path)) << ModeName(mode);
        EXPECT_FALSE(formatter.writeToFile()) << ModeName(mode);
        ASSERT_TRUE(formatter.closeFile()) << ModeName(mode);

        EXPECT_EQ(FileSize(path), 0) << ModeName(mode);
    }
}

TEST(StreamBinding, BindingSurvivesCloseFileSoAReusedFstreamAddressIsNotAccepted) {
    // closeFile() destroys the std::fstream; the next openFile() may well
    // get the same heap address back. The binding must not silently match on
    // that recycled pointer.
    for (auto mode : kAllModes) {
        CFormatter formatter(mode, 44100);
        std::vector<float> ch1 = {1.f, 2.f};
        formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

        const std::string path = TestFixturePath("binding_recycled" + FileExtension(mode));
        ASSERT_TRUE(formatter.openFile(path)) << ModeName(mode);
        ASSERT_TRUE(formatter.writeToFile()) << ModeName(mode);
        ASSERT_TRUE(formatter.closeFile()) << ModeName(mode);

        ASSERT_TRUE(formatter.openFile(path)) << ModeName(mode);
        EXPECT_FALSE(formatter.writeToFile()) << ModeName(mode);
        ASSERT_TRUE(formatter.closeFile()) << ModeName(mode);

        EXPECT_EQ(FileSize(path), 0) << ModeName(mode);
    }
}

TEST(StreamBinding, ResetWriterBeforeTheFirstWriteIsHarmless) {
    for (auto mode : kAllModes) {
        CFormatter formatter(mode, 44100);
        std::vector<float> ch1 = {1.f, 2.f};
        formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

        formatter.resetWriter();
        formatter.resetWriter();

        std::stringstream mem;
        EXPECT_TRUE(formatter.writeToStream(&mem)) << ModeName(mode);
        EXPECT_FALSE(mem.str().empty()) << ModeName(mode);
    }
}

TEST(StreamBinding, ClearBufferDoesNotClearTheStreamBinding) {
    // clearBuffer() only drops channel data; it must not be a back door for
    // switching streams mid-file.
    for (auto mode : kAllModes) {
        CFormatter formatter(mode, 44100);
        std::vector<float> ch1 = {1.f, 2.f};
        formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

        std::stringstream first;
        ASSERT_TRUE(formatter.writeToStream(&first)) << ModeName(mode);

        formatter.clearBuffer();
        formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

        std::stringstream second;
        EXPECT_FALSE(formatter.writeToStream(&second)) << ModeName(mode);
    }
}
