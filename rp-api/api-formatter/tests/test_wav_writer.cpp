/**
 * Unit tests for CFormatter(RP_F_WAV, ...) — the WAV writer, exercised only
 * through the public CFormatter facade (per project convention, the
 * lower-level CWaveWriter is not touched directly).
 *
 * Each test builds a RIFF/WAVE file in memory via writeToStream() and
 * parses the header + PCM payload back out by hand (the WAV container is
 * simple enough that a hand parser here is trustworthy and keeps this
 * suite dependency-free).
 *
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rp_formatter.h"
#include "test_support.h"

using namespace rp_formatter_api;

namespace {

uint16_t ReadU16(const std::string& bytes, size_t offset) {
    return static_cast<uint16_t>(static_cast<uint8_t>(bytes[offset]) | (static_cast<uint8_t>(bytes[offset + 1]) << 8));
}

uint32_t ReadU32(const std::string& bytes, size_t offset) {
    return static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset])) | (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 3])) << 24);
}

// Fixed-size WAV/RIFF canonical header layout.
struct WavHeader {
    std::string riffId;
    uint32_t chunkSize;
    std::string waveId;
    std::string fmtId;
    uint32_t fmtSize;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    std::string dataId;
    uint32_t dataChunkSize;
};

WavHeader ParseWavHeader(const std::string& bytes) {
    WavHeader h;
    h.riffId = bytes.substr(0, 4);
    h.chunkSize = ReadU32(bytes, 4);
    h.waveId = bytes.substr(8, 4);
    h.fmtId = bytes.substr(12, 4);
    h.fmtSize = ReadU32(bytes, 16);
    h.audioFormat = ReadU16(bytes, 20);
    h.numChannels = ReadU16(bytes, 22);
    h.sampleRate = ReadU32(bytes, 24);
    h.byteRate = ReadU32(bytes, 28);
    h.blockAlign = ReadU16(bytes, 32);
    h.bitsPerSample = ReadU16(bytes, 34);
    h.dataId = bytes.substr(36, 4);
    h.dataChunkSize = ReadU32(bytes, 40);
    return h;
}

constexpr uint32_t kWavHeaderSize = 44;

}  // namespace

TEST(WavWriter, RiffContainerMagicAndStructuralFields) {
    CFormatter formatter(RP_F_WAV, 48000);
    std::vector<float> samples = {0.f, 0.25f, -0.5f, 1.f, -1.f, 0.125f, 0.75f, -0.75f};
    formatter.setChannel(RP_F_CH1, samples.data(), static_cast<int>(samples.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));

    auto bytes = mem.str();
    ASSERT_GE(bytes.size(), kWavHeaderSize);
    auto header = ParseWavHeader(bytes);

    EXPECT_EQ(header.riffId, "RIFF");
    EXPECT_EQ(header.waveId, "WAVE");
    EXPECT_EQ(header.fmtId, "fmt ");
    EXPECT_EQ(header.fmtSize, 16u);
    EXPECT_EQ(header.dataId, "data");
    EXPECT_EQ(header.numChannels, 1u);
    EXPECT_EQ(header.sampleRate, 48000u);
}

TEST(WavWriter, SingleFloatChannelPcmPayloadMatchesInput) {
    CFormatter formatter(RP_F_WAV, 44100);
    std::vector<float> samples = {0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f};
    formatter.setChannel(RP_F_CH1, samples.data(), static_cast<int>(samples.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto bytes = mem.str();
    auto header = ParseWavHeader(bytes);

    // IEEE float, 32 bits per sample -> audio format tag WAVE_FORMAT_IEEE_FLOAT (3).
    EXPECT_EQ(header.bitsPerSample, 32u);
    EXPECT_EQ(header.audioFormat, 3u);
    EXPECT_EQ(header.blockAlign, 4u);

    const uint32_t expectedDataSize = static_cast<uint32_t>(samples.size() * sizeof(float));
    EXPECT_EQ(header.dataChunkSize, expectedDataSize);
    EXPECT_EQ(header.chunkSize, 36u + expectedDataSize);
    ASSERT_EQ(bytes.size(), kWavHeaderSize + expectedDataSize);

    std::vector<float> writtenSamples(samples.size());
    std::memcpy(writtenSamples.data(), bytes.data() + kWavHeaderSize, expectedDataSize);
    EXPECT_EQ(writtenSamples, samples);
}

TEST(WavWriter, ByteRateFollowsRequestedOscRateNot44100) {
    const uint32_t oscRate = 100000;
    CFormatter formatter(RP_F_WAV, oscRate);
    std::vector<uint8_t> samples = {1, 2, 3, 4};
    formatter.setChannel(RP_F_CH1, samples.data(), static_cast<int>(samples.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto header = ParseWavHeader(mem.str());

    EXPECT_EQ(header.sampleRate, oscRate);
    // ByteRate = numChannels * sampleRate * bitsPerSample / 8.
    const uint32_t expectedByteRate = 1u * oscRate * header.bitsPerSample / 8u;
    EXPECT_EQ(header.byteRate, expectedByteRate);
}

TEST(WavWriter, SingleUint8ChannelPassesThroughUnscaled) {
    CFormatter formatter(RP_F_WAV, 44100);
    std::vector<uint8_t> samples = {0, 10, 20, 30, 255};
    formatter.setChannel(RP_F_CH1, samples.data(), static_cast<int>(samples.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto bytes = mem.str();
    auto header = ParseWavHeader(bytes);

    EXPECT_EQ(header.bitsPerSample, 8u);
    ASSERT_EQ(bytes.size(), kWavHeaderSize + samples.size());

    std::string payload = bytes.substr(kWavHeaderSize, samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        EXPECT_EQ(static_cast<uint8_t>(payload[i]), samples[i]) << "sample #" << i;
    }
}

TEST(WavWriter, MixedUint8AndUint16ChannelsAreInterleavedAt16Bit) {
    // When channels differ in bit depth, CWaveWriter promotes everything to
    // the widest depth present. Mixing an 8-bit and a 16-bit channel means
    // the 8-bit channel gets left-shifted into the high byte (see
    // CWaveWriter::Impl::write()'s get16Bit lambda) and the result is
    // interleaved sample-by-sample: [ch1[0], ch2[0], ch1[1], ch2[1], ...].
    CFormatter formatter(RP_F_WAV, 44100);
    std::vector<uint8_t> ch1 = {0x10, 0x20, 0x30};
    std::vector<uint16_t> ch2 = {0x1234, 0x5678, 0x9ABC};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));
    formatter.setChannel(RP_F_CH2, ch2.data(), static_cast<int>(ch2.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto bytes = mem.str();
    auto header = ParseWavHeader(bytes);

    EXPECT_EQ(header.numChannels, 2u);
    EXPECT_EQ(header.bitsPerSample, 16u);

    const size_t expectedDataSize = ch1.size() * 2u /*channels*/ * sizeof(uint16_t);
    ASSERT_EQ(bytes.size(), kWavHeaderSize + expectedDataSize);

    std::vector<uint16_t> expectedInterleaved;
    for (size_t i = 0; i < ch1.size(); i++) {
        expectedInterleaved.push_back(static_cast<uint16_t>(ch1[i]) << 8);
        expectedInterleaved.push_back(ch2[i]);
    }
    std::vector<uint16_t> actualInterleaved(expectedInterleaved.size());
    std::memcpy(actualInterleaved.data(), bytes.data() + kWavHeaderSize, expectedDataSize);
    EXPECT_EQ(actualInterleaved, expectedInterleaved);
}

TEST(WavWriter, ShorterChannelIsZeroPaddedToMaxSampleCount) {
    CFormatter formatter(RP_F_WAV, 44100);
    std::vector<uint8_t> ch1 = {1, 2, 3, 4, 5};
    std::vector<uint8_t> ch2 = {9, 8};  // shorter than ch1
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));
    formatter.setChannel(RP_F_CH2, ch2.data(), static_cast<int>(ch2.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto bytes = mem.str();

    const size_t expectedDataSize = ch1.size() * 2u;
    ASSERT_EQ(bytes.size(), kWavHeaderSize + expectedDataSize);

    std::string payload = bytes.substr(kWavHeaderSize, expectedDataSize);
    std::vector<uint8_t> expected = {1, 9, 2, 8, 3, 0, 4, 0, 5, 0};
    for (size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(static_cast<uint8_t>(payload[i]), expected[i]) << "byte #" << i;
    }
}

TEST(WavWriter, ChannelTypesUnsupportedByWavAreSilentlyExcluded) {
    // getWavSupport() in writers/common.h excludes ui32/i32/ui64/i64 from
    // WAV output; only ui8/ui16/f32/d64 channels count towards NumChannels.
    CFormatter formatter(RP_F_WAV, 44100);
    std::vector<float> supported = {1.f, 2.f, 3.f};
    std::vector<uint32_t> unsupported = {1, 2, 3};
    formatter.setChannel(RP_F_CH1, supported.data(), static_cast<int>(supported.size()));
    formatter.setChannel(RP_F_CH2, unsupported.data(), static_cast<int>(unsupported.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto header = ParseWavHeader(mem.str());

    EXPECT_EQ(header.numChannels, 1u);
}

TEST(WavWriter, RepeatedWriteToFileAppendsDataWithoutDuplicatingHeader) {
    // Mirrors the streaming use pattern in tests/rp_formatter_test.py:
    // open once, call writeToFile() repeatedly, close once. The header
    // must only be emitted on the first call.
    CFormatter formatter(RP_F_WAV, 44100);
    std::vector<float> ch1 = {1.f, 2.f, 3.f, 4.f};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

    const std::string path = TestFixturePath("repeat_write.wav");
    ASSERT_TRUE(formatter.openFile(path));
    ASSERT_TRUE(formatter.writeToFile());
    ASSERT_TRUE(formatter.writeToFile());
    ASSERT_TRUE(formatter.closeFile());

    std::ifstream check(path, std::ios::binary);
    ASSERT_TRUE(check.is_open());
    std::string bytes((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());

    // Only one 44-byte canonical header may appear, at offset 0.
    ASSERT_GE(bytes.size(), kWavHeaderSize);
    EXPECT_EQ(bytes.substr(0, 4), "RIFF");
    // No second "RIFF" marker should appear anywhere after the header.
    EXPECT_EQ(bytes.find("RIFF", 4), std::string::npos);

    auto header = ParseWavHeader(bytes);
    const uint32_t expectedDataSize = static_cast<uint32_t>(ch1.size() * sizeof(float) * 2 /* two writes */);
    EXPECT_EQ(header.dataChunkSize, expectedDataSize);
    EXPECT_EQ(bytes.size(), kWavHeaderSize + expectedDataSize);
}
