/**
 * Unit tests for the mode-independent part of the CFormatter facade
 * (src/rp_formatter.cpp): file lifecycle, buffer bookkeeping, sample-count
 * accounting and which operations are legal for which output mode.
 *
 * Format-specific byte layout lives in test_wav_writer.cpp /
 * test_csv_writer.cpp / test_tdms_writer.cpp; this file only pins down the
 * behaviour every mode shares. CSV is used as the vehicle wherever a
 * concrete mode has to be picked, because its output is the cheapest to
 * assert on.
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

TEST(Formatter, MaxSamplesIsZeroBeforeAnyChannelIsSet) {
    CFormatter formatter(RP_F_CSV, 1000);
    EXPECT_EQ(formatter.getMaxSamples(), 0u);
}

TEST(Formatter, MaxSamplesIsTheLongestChannelNotTheSum) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint8_t> shortCh = {1, 2};
    std::vector<uint8_t> longCh = {1, 2, 3, 4, 5};
    formatter.setChannel(RP_F_CH1, shortCh.data(), static_cast<int>(shortCh.size()));
    formatter.setChannel(RP_F_CH2, longCh.data(), static_cast<int>(longCh.size()));

    EXPECT_EQ(formatter.getMaxSamples(), 5u);
}

TEST(Formatter, ReassigningTheSameChannelReplacesItsSampleCount) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint8_t> samples = {1, 2, 3, 4, 5};
    formatter.setChannel(RP_F_CH1, samples.data(), 5);
    ASSERT_EQ(formatter.getMaxSamples(), 5u);

    formatter.setChannel(RP_F_CH1, samples.data(), 2);
    EXPECT_EQ(formatter.getMaxSamples(), 2u);
}

TEST(Formatter, ClearBufferForgetsEveryChannel) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint8_t> samples = {1, 2, 3};
    formatter.setChannel(RP_F_CH1, samples.data(), static_cast<int>(samples.size()));
    ASSERT_EQ(formatter.getMaxSamples(), 3u);

    formatter.clearBuffer();

    EXPECT_EQ(formatter.getMaxSamples(), 0u);
    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    EXPECT_EQ(mem.str(), "\r\n");
}

TEST(Formatter, EndiannessIsOnlyConfigurableInWavMode) {
    CFormatter wav(RP_F_WAV, 1000);
    CFormatter csv(RP_F_CSV, 1000);
    CFormatter tdms(RP_F_TDMS, 1000);

    EXPECT_TRUE(wav.setEndiannes(RP_F_BigEndian));
    EXPECT_TRUE(wav.setEndiannes(RP_F_LittleEndian));
    EXPECT_FALSE(csv.setEndiannes(RP_F_BigEndian));
    EXPECT_FALSE(tdms.setEndiannes(RP_F_BigEndian));
}

TEST(Formatter, IsOpenFileTracksTheOpenCloseCycle) {
    CFormatter formatter(RP_F_CSV, 1000);
    const std::string path = TestFixturePath("lifecycle.csv");

    EXPECT_FALSE(formatter.isOpenFile());
    ASSERT_TRUE(formatter.openFile(path));
    EXPECT_TRUE(formatter.isOpenFile());
    EXPECT_TRUE(formatter.closeFile());
    EXPECT_FALSE(formatter.isOpenFile());
}

TEST(Formatter, OpeningASecondFileWithoutClosingIsRefused) {
    CFormatter formatter(RP_F_CSV, 1000);
    ASSERT_TRUE(formatter.openFile(TestFixturePath("first_open.csv")));

    EXPECT_FALSE(formatter.openFile(TestFixturePath("second_open.csv")));

    EXPECT_TRUE(formatter.isOpenFile());
    EXPECT_TRUE(formatter.closeFile());
}

TEST(Formatter, ReopeningAfterCloseSucceeds) {
    CFormatter formatter(RP_F_CSV, 1000);
    const std::string path = TestFixturePath("reopen.csv");

    ASSERT_TRUE(formatter.openFile(path));
    ASSERT_TRUE(formatter.closeFile());
    EXPECT_TRUE(formatter.openFile(path));
    EXPECT_TRUE(formatter.closeFile());
}

TEST(Formatter, CloseFileWithoutAnOpenFileReturnsFalse) {
    CFormatter formatter(RP_F_CSV, 1000);
    EXPECT_FALSE(formatter.closeFile());
}

TEST(Formatter, ClosingTwiceReturnsFalseTheSecondTime) {
    CFormatter formatter(RP_F_CSV, 1000);
    ASSERT_TRUE(formatter.openFile(TestFixturePath("double_close.csv")));
    ASSERT_TRUE(formatter.closeFile());
    EXPECT_FALSE(formatter.closeFile());
}

TEST(Formatter, OpenFileFailsForAPathThatCannotBeCreated) {
    CFormatter formatter(RP_F_CSV, 1000);
    EXPECT_FALSE(formatter.openFile(TestFixturePath("no_such_directory/out.csv")));
    EXPECT_FALSE(formatter.isOpenFile());
}

TEST(Formatter, OpenFileTruncatesExistingContent) {
    const std::string path = TestFixturePath("truncate.csv");
    {
        std::ofstream seed(path, std::ios::binary);
        seed << "stale content that must not survive";
    }

    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint8_t> samples = {1};
    formatter.setChannel(RP_F_CH1, samples.data(), 1);
    ASSERT_TRUE(formatter.openFile(path));
    ASSERT_TRUE(formatter.writeToFile());
    ASSERT_TRUE(formatter.closeFile());

    std::ifstream check(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "CH1\r\n1");
}

TEST(Formatter, WriteToFileWithoutAnOpenFileFailsInEveryMode) {
    for (auto mode : {RP_F_WAV, RP_F_CSV, RP_F_TDMS}) {
        CFormatter formatter(mode, 1000);
        std::vector<float> samples = {1.f, 2.f};
        formatter.setChannel(RP_F_CH1, samples.data(), static_cast<int>(samples.size()));
        EXPECT_FALSE(formatter.writeToFile()) << "mode " << mode;
    }
}

TEST(Formatter, WriteToStreamSucceedsWithoutAnyOpenFileInEveryMode) {
    for (auto mode : {RP_F_WAV, RP_F_CSV, RP_F_TDMS}) {
        CFormatter formatter(mode, 1000);
        std::vector<float> samples = {1.f, 2.f};
        formatter.setChannel(RP_F_CH1, samples.data(), static_cast<int>(samples.size()));

        std::stringstream mem;
        EXPECT_TRUE(formatter.writeToStream(&mem)) << "mode " << mode;
        EXPECT_FALSE(mem.str().empty()) << "mode " << mode;
    }
}

TEST(Formatter, DestructorClosesAStillOpenFile) {
    const std::string path = TestFixturePath("closed_by_destructor.csv");
    {
        CFormatter formatter(RP_F_CSV, 1000);
        std::vector<uint8_t> samples = {1, 2};
        formatter.setChannel(RP_F_CH1, samples.data(), static_cast<int>(samples.size()));
        ASSERT_TRUE(formatter.openFile(path));
        ASSERT_TRUE(formatter.writeToFile());
    }

    std::ifstream check(path, std::ios::binary);
    ASSERT_TRUE(check.is_open());
    std::string content((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "CH1\r\n1\r\n2");
}

TEST(Formatter, NumpyChannelSettersBehaveLikeTheirPlainOverloads) {
    std::vector<uint8_t> ui8 = {1};
    std::vector<uint16_t> ui16 = {2};
    std::vector<int32_t> i32 = {-3};
    std::vector<uint32_t> ui32 = {4};
    std::vector<int64_t> i64 = {-5};
    std::vector<uint64_t> ui64 = {6};
    std::vector<float> f32 = {7.f};
    std::vector<double> d64 = {-8.0};

    CFormatter formatter(RP_F_CSV, 1000);
    formatter.setChannelUI8NP(RP_F_CH1, ui8.data(), 1);
    formatter.setChannelUI16NP(RP_F_CH2, ui16.data(), 1);
    formatter.setChannelI32NP(RP_F_CH3, i32.data(), 1);
    formatter.setChannelUI32NP(RP_F_CH4, ui32.data(), 1);
    formatter.setChannelI64NP(RP_F_CH5, i64.data(), 1);
    formatter.setChannelUI64NP(RP_F_CH6, ui64.data(), 1);
    formatter.setChannelFNP(RP_F_CH7, f32.data(), 1);
    formatter.setChannelDNP(RP_F_CH8, d64.data(), 1);

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    EXPECT_EQ(mem.str(), "CH1,CH2,CH3,CH4,CH5,CH6,CH7,CH8\r\n1,2,-3,4,-5,6,7.000000,-8.000000");
}

TEST(Formatter, NumpyChannelSettersHonourCustomNames) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<float> f32 = {1.f};
    formatter.setChannelFNP(RP_F_CH1, f32.data(), 1, "Voltage");

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    EXPECT_EQ(mem.str().substr(0, 7), "Voltage");
}

TEST(Formatter, ZeroSampleChannelStillProducesAHeader) {
    CFormatter formatter(RP_F_CSV, 1000);
    std::vector<uint8_t> empty;
    formatter.setChannel(RP_F_CH1, empty.data(), 0);

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    EXPECT_EQ(mem.str(), "CH1\r\n");
    EXPECT_EQ(formatter.getMaxSamples(), 0u);
}
