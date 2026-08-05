/**
 * Unit tests for CFormatter(RP_F_CSV, ...) — the CSV writer, exercised
 * only through the public CFormatter facade.
 *
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

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t next = text.find("\r\n", pos);
        if (next == std::string::npos) {
            lines.push_back(text.substr(pos));
            break;
        }
        lines.push_back(text.substr(pos, next - pos));
        pos = next + 2;
    }
    return lines;
}

}  // namespace

TEST(CsvWriter, HeaderUsesDefaultChannelNamesAndCommaSeparator) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<float> ch1 = {0.f, 1.f};
    std::vector<uint8_t> ch2 = {10, 11};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));
    formatter.setChannel(RP_F_CH2, ch2.data(), static_cast<int>(ch2.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto lines = SplitLines(mem.str());

    ASSERT_GE(lines.size(), 1u);
    EXPECT_EQ(lines[0], "CH1,CH2");
}

TEST(CsvWriter, HeaderUsesCustomChannelNameWhenProvided) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<float> ch1 = {0.f, 1.f};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()), "Voltage");

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto lines = SplitLines(mem.str());

    EXPECT_EQ(lines[0], "Voltage");
}

TEST(CsvWriter, RowsFormatEachSupportedTypeWithSixDecimalPlacesForFloats) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint8_t> ui8 = {200};
    std::vector<uint16_t> ui16 = {60000};
    std::vector<uint32_t> ui32 = {4000000000u};
    std::vector<int32_t> i32 = {-123456};
    std::vector<uint64_t> ui64 = {18000000000000000000ull};
    std::vector<int64_t> i64 = {-9000000000000000000ll};
    std::vector<float> f32 = {3.5f};
    std::vector<double> d64 = {-2.25};

    formatter.setChannel(RP_F_CH1, ui8.data(), 1);
    formatter.setChannel(RP_F_CH2, ui16.data(), 1);
    formatter.setChannel(RP_F_CH3, ui32.data(), 1);
    formatter.setChannel(RP_F_CH4, i32.data(), 1);
    formatter.setChannel(RP_F_CH5, ui64.data(), 1);
    formatter.setChannel(RP_F_CH6, i64.data(), 1);
    formatter.setChannel(RP_F_CH7, f32.data(), 1);
    formatter.setChannel(RP_F_CH8, d64.data(), 1);

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto lines = SplitLines(mem.str());

    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "CH1,CH2,CH3,CH4,CH5,CH6,CH7,CH8");
    EXPECT_EQ(lines[1], "200,60000,4000000000,-123456,18000000000000000000,-9000000000000000000,3.500000,-2.250000");
}

TEST(CsvWriter, RowsAreSeparatedByCrLfWithNoTrailingTerminator) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint8_t> ch1 = {1, 2, 3};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    std::string text = mem.str();

    EXPECT_EQ(text, "CH1\r\n1\r\n2\r\n3");
}

TEST(CsvWriter, ShorterChannelPadsMissingSamplesWithLiteralZero) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint8_t> ch1 = {1, 2, 3};
    std::vector<uint8_t> ch2 = {9};  // shorter than ch1
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));
    formatter.setChannel(RP_F_CH2, ch2.data(), static_cast<int>(ch2.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto lines = SplitLines(mem.str());

    ASSERT_EQ(lines.size(), 4u);
    EXPECT_EQ(lines[0], "CH1,CH2");
    EXPECT_EQ(lines[1], "1,9");
    EXPECT_EQ(lines[2], "2,0");
    EXPECT_EQ(lines[3], "3,0");
}

TEST(CsvWriter, IndexAndTimeColumnsComeBeforeChannelColumnsInThatOrder) {
    // CCSVWriter::Impl::write() special-cases RP_F_INDEX and RP_F_TIME as
    // leading columns, in that exact order, ahead of CH1..CH10.
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint32_t> index = {0, 1};
    std::vector<double> time = {0.0, 0.1};
    std::vector<float> ch1 = {5.f, 6.f};
    formatter.setChannel(RP_F_INDEX, index.data(), static_cast<int>(index.size()));
    formatter.setChannel(RP_F_TIME, time.data(), static_cast<int>(time.size()));
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto lines = SplitLines(mem.str());

    EXPECT_EQ(lines[0], "INDEX,TIME,CH1");
    EXPECT_EQ(lines[1], "0,0.000000,5.000000");
    EXPECT_EQ(lines[2], "1,0.100000,6.000000");
}

TEST(CsvWriter, HeaderRowIsWrittenExactlyOnceAcrossRepeatedWritesToOneFile) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint8_t> ch1 = {1, 2};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

    const std::string path = TestFixturePath("repeat_write.csv");
    ASSERT_TRUE(formatter.openFile(path));
    ASSERT_TRUE(formatter.writeToFile());
    ASSERT_TRUE(formatter.writeToFile());
    ASSERT_TRUE(formatter.closeFile());

    std::ifstream check(path, std::ios::binary);
    ASSERT_TRUE(check.is_open());
    std::string content((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());

    size_t headerCount = 0;
    size_t pos = 0;
    while ((pos = content.find("CH1", pos)) != std::string::npos) {
        headerCount++;
        pos += 3;
    }
    EXPECT_EQ(headerCount, 1u);
}

TEST(CsvWriter, ResetWriterAfterCloseAllowsHeaderOnNextFile) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint8_t> ch1 = {1, 2};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

    const std::string path1 = TestFixturePath("first.csv");
    ASSERT_TRUE(formatter.openFile(path1));
    ASSERT_TRUE(formatter.writeToFile());
    ASSERT_TRUE(formatter.closeFile());

    formatter.resetWriter();
    formatter.clearBuffer();
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

    const std::string path2 = TestFixturePath("second.csv");
    ASSERT_TRUE(formatter.openFile(path2));
    ASSERT_TRUE(formatter.writeToFile());
    ASSERT_TRUE(formatter.closeFile());

    std::ifstream check(path2, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content.substr(0, 3), "CH1");
}

TEST(CsvWriter, EmptyPackStillEmitsTheHeaderTerminator) {
    // No channel at all: the header line degenerates to a bare CRLF and no
    // data rows follow.
    CFormatter formatter(RP_F_CSV, 1000);

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    EXPECT_EQ(mem.str(), "\r\n");
}

TEST(CsvWriter, IndexAndTimeColumnsHonourCustomNames) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint32_t> index = {0};
    std::vector<double> time = {0.0};
    formatter.setChannel(RP_F_INDEX, index.data(), 1, "n");
    formatter.setChannel(RP_F_TIME, time.data(), 1, "t, s");

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto lines = SplitLines(mem.str());

    EXPECT_EQ(lines[0], "n,t, s");
}

TEST(CsvWriter, ChannelsAreEmittedInAscendingOrderRegardlessOfAssignmentOrder) {
    // Columns follow the RP_F_CH* enum order, not the order setChannel() was
    // called in, and gaps in the channel numbering are simply skipped.
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint8_t> ch5 = {50};
    std::vector<uint8_t> ch2 = {20};
    std::vector<uint8_t> ch10 = {100};
    formatter.setChannel(RP_F_CH5, ch5.data(), 1);
    formatter.setChannel(RP_F_CH10, ch10.data(), 1);
    formatter.setChannel(RP_F_CH2, ch2.data(), 1);

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto lines = SplitLines(mem.str());

    EXPECT_EQ(lines[0], "CH2,CH5,CH10");
    EXPECT_EQ(lines[1], "20,50,100");
}

TEST(CsvWriter, SingleSampleProducesExactlyOneUnterminatedRow) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint8_t> ch1 = {42};
    formatter.setChannel(RP_F_CH1, ch1.data(), 1);

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));

    EXPECT_EQ(mem.str(), "CH1\r\n42");
}

TEST(CsvWriter, SecondWriteToTheSameStreamAppendsRowsWithoutARepeatedHeader) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint8_t> ch1 = {1, 2};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    ASSERT_TRUE(formatter.writeToStream(&mem));

    // Note the missing separator between the two blocks: the writer omits the
    // terminator after the last row of every block (see
    // CCSVWriter::Impl::write()), so consecutive blocks butt up against each
    // other. This documents the current streaming behaviour.
    EXPECT_EQ(mem.str(), "CH1\r\n1\r\n21\r\n2");
}

TEST(CsvWriter, ResetWriterReEmitsTheHeaderOnTheNextStream) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint8_t> ch1 = {1};
    formatter.setChannel(RP_F_CH1, ch1.data(), 1);

    std::stringstream first;
    ASSERT_TRUE(formatter.writeToStream(&first));
    formatter.resetWriter();
    std::stringstream second;
    ASSERT_TRUE(formatter.writeToStream(&second));

    EXPECT_EQ(first.str(), "CH1\r\n1");
    EXPECT_EQ(second.str(), "CH1\r\n1");
}

TEST(CsvWriter, TypesThatWavRejectsAreStillWrittenAsPlainDecimalColumns) {
    // getWavSupport() excludes the 32/64-bit integer types, but CSV has no
    // such restriction - all eight types must produce a column.
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint32_t> ui32 = {1, 2};
    std::vector<int32_t> i32 = {-1, -2};
    std::vector<uint64_t> ui64 = {3, 4};
    std::vector<int64_t> i64 = {-3, -4};
    formatter.setChannel(RP_F_CH1, ui32.data(), 2);
    formatter.setChannel(RP_F_CH2, i32.data(), 2);
    formatter.setChannel(RP_F_CH3, ui64.data(), 2);
    formatter.setChannel(RP_F_CH4, i64.data(), 2);

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto lines = SplitLines(mem.str());

    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "CH1,CH2,CH3,CH4");
    EXPECT_EQ(lines[1], "1,-1,3,-3");
    EXPECT_EQ(lines[2], "2,-2,4,-4");
}

TEST(CsvWriter, ShorterIndexColumnIsPaddedLikeAnyOtherColumn) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint32_t> index = {7};
    std::vector<uint8_t> ch1 = {1, 2};
    formatter.setChannel(RP_F_INDEX, index.data(), 1);
    formatter.setChannel(RP_F_CH1, ch1.data(), 2);

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto lines = SplitLines(mem.str());

    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[1], "7,1");
    EXPECT_EQ(lines[2], "0,2");
}
