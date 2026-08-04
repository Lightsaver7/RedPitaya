/**
 * Unit tests for CFormatter(RP_F_TDMS, ...) — the TDMS writer, exercised
 * only through the public CFormatter facade.
 *
 * These C++ tests are deliberately shallow: they only check that
 * CFormatter reports success and produces a plausible, non-empty file for
 * each scenario ("did writing to the file succeed"). Byte-level
 * correctness of the TDMS container itself ("is what we wrote actually a
 * valid TDMS file with the right groups/channels/properties/values") is
 * checked independently in verify_tdms.py using the third-party npTDMS
 * reader — an independent parser is a much stronger correctness check
 * than a hand-rolled TDMS parser living next to the code under test,
 * which could easily share the same misunderstanding of the format as a
 * bug in the writer.
 *
 * Fixture files are written under TEST_FIXTURES_DIR (configured by
 * tests/CMakeLists.txt) and are intentionally NOT deleted by this binary;
 * CTest fixtures (see tests/CMakeLists.txt) guarantee verify_tdms.py runs
 * after this executable and a cleanup step removes the directory
 * afterwards.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "rp_formatter.h"
#include "test_support.h"

using namespace rp_formatter_api;

namespace {
std::string FixturePath(const std::string& name) {
    return TestFixturePath(name);
}

// TDMS lead-in is 28 bytes; anything smaller cannot possibly be valid.
constexpr std::streamsize kMinTdmsSegmentSize = 28;
}  // namespace

TEST(TdmsWriter, SingleFloatChannelWithCustomNameWritesNonEmptyFile) {
    CFormatter formatter(RP_F_TDMS, 125000000);
    std::vector<float> ch1 = {0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()), "MyChan1");

    const std::string path = FixturePath("single_float_channel.tdms");
    ASSERT_TRUE(formatter.openFile(path));
    ASSERT_TRUE(formatter.writeToFile());
    ASSERT_TRUE(formatter.closeFile());

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(f.is_open());
    EXPECT_GE(f.tellg(), kMinTdmsSegmentSize);
}

TEST(TdmsWriter, MixedTypeMultiChannelWritesNonEmptyFile) {
    CFormatter formatter(RP_F_TDMS, 1000000);
    std::vector<uint8_t> ch1 = {10, 20, 30, 40};
    std::vector<uint16_t> ch2 = {100, 200, 300, 400};
    std::vector<int32_t> ch3 = {-1, -2, -3, -4};
    std::vector<double> ch4 = {1.5, 2.5, 3.5, 4.5};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));
    formatter.setChannel(RP_F_CH2, ch2.data(), static_cast<int>(ch2.size()));
    formatter.setChannel(RP_F_CH3, ch3.data(), static_cast<int>(ch3.size()));
    formatter.setChannel(RP_F_CH4, ch4.data(), static_cast<int>(ch4.size()));

    const std::string path = FixturePath("mixed_type_multi_channel.tdms");
    ASSERT_TRUE(formatter.openFile(path));
    ASSERT_TRUE(formatter.writeToFile());
    ASSERT_TRUE(formatter.closeFile());

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(f.is_open());
    EXPECT_GE(f.tellg(), kMinTdmsSegmentSize);
}

TEST(TdmsWriter, TimeAndIndexChannelsAreIncludedInOutput) {
    // Unlike the CSV/WAV writers, CTDMSWriter::Impl::write() loops
    // RP_F_CH1..RP_F_INDEX inclusive, so TIME/INDEX channels are written
    // out as regular TDMS channels if a buffer is set for them.
    CFormatter formatter(RP_F_TDMS, 1000000);
    std::vector<uint32_t> index = {0, 1, 2, 3};
    std::vector<double> time = {0.0, 0.001, 0.002, 0.003};
    std::vector<float> ch1 = {1.f, 2.f, 3.f, 4.f};
    formatter.setChannel(RP_F_INDEX, index.data(), static_cast<int>(index.size()));
    formatter.setChannel(RP_F_TIME, time.data(), static_cast<int>(time.size()));
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

    const std::string path = FixturePath("time_and_index_channels.tdms");
    ASSERT_TRUE(formatter.openFile(path));
    ASSERT_TRUE(formatter.writeToFile());
    ASSERT_TRUE(formatter.closeFile());

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(f.is_open());
    EXPECT_GE(f.tellg(), kMinTdmsSegmentSize);
}

TEST(TdmsWriter, RepeatedWriteToFileAppendsAFurtherTdmsSegment) {
    // TDMS files stream as a sequence of independently-addressable
    // segments; calling writeToFile() twice on the same open file must
    // grow it by a second full segment rather than overwriting the first.
    CFormatter formatter(RP_F_TDMS, 1000000);
    std::vector<float> ch1 = {1.f, 2.f, 3.f, 4.f};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

    const std::string path = FixturePath("two_segments.tdms");
    ASSERT_TRUE(formatter.openFile(path));
    ASSERT_TRUE(formatter.writeToFile());
    const auto sizeAfterFirstWrite = [&] {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        return f.tellg();
    }();
    ASSERT_TRUE(formatter.writeToFile());
    ASSERT_TRUE(formatter.closeFile());

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(f.is_open());
    EXPECT_GT(f.tellg(), sizeAfterFirstWrite);
}

TEST(TdmsWriter, WriteToFileFailsGracefullyWhenNoFileIsOpen) {
    CFormatter formatter(RP_F_TDMS, 1000000);
    std::vector<float> ch1 = {1.f};
    formatter.setChannel(RP_F_CH1, ch1.data(), 1);

    EXPECT_FALSE(formatter.writeToFile());
}
