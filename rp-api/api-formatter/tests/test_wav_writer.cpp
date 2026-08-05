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

TEST(WavWriter, BigEndianModeByteSwapsTheFormatChunkFields) {
    // setEndiannes() only affects the integers written by buildHeader() (the
    // helpers addInt16ToFileData/addInt32ToFileData take the endianness), so
    // the format chunk is asserted directly on the raw bytes here instead of
    // through ParseWavHeader(), which always decodes little-endian.
    CFormatter formatter(RP_F_WAV, 44100);
    ASSERT_TRUE(formatter.setEndiannes(RP_F_BigEndian));
    std::vector<uint8_t> ch1 = {1, 2, 3, 4};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto bytes = mem.str();
    ASSERT_GE(bytes.size(), kWavHeaderSize);

    EXPECT_EQ(bytes.substr(0, 4), "RIFF");
    EXPECT_EQ(bytes.substr(8, 4), "WAVE");
    EXPECT_EQ(bytes.substr(12, 4), "fmt ");
    EXPECT_EQ(bytes.substr(36, 4), "data");

    // fmt chunk size 16, PCM tag 1, 1 channel, 44100 Hz, 8 bits per sample.
    EXPECT_EQ(bytes.substr(16, 4), std::string("\x00\x00\x00\x10", 4));
    EXPECT_EQ(bytes.substr(20, 2), std::string("\x00\x01", 2));
    EXPECT_EQ(bytes.substr(22, 2), std::string("\x00\x01", 2));
    EXPECT_EQ(bytes.substr(24, 4), std::string("\x00\x00\xAC\x44", 4));
    EXPECT_EQ(bytes.substr(34, 2), std::string("\x00\x08", 2));
}

TEST(WavWriter, LittleEndianIsTheDefaultWithoutCallingSetEndiannes) {
    CFormatter formatter(RP_F_WAV, 44100);
    std::vector<uint8_t> ch1 = {1};
    formatter.setChannel(RP_F_CH1, ch1.data(), 1);

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto bytes = mem.str();

    EXPECT_EQ(bytes.substr(24, 4), std::string("\x44\xAC\x00\x00", 4));
}

TEST(WavWriter, DoubleChannelIsNarrowedTo32BitFloatPayload) {
    // getBitsCount(RP_F_d64_Bit) is 64 but WAV output is capped at 32 bits
    // (maxSupportedBitDepth in CWaveWriter::Impl::write()), so doubles are
    // written as float32 samples.
    CFormatter formatter(RP_F_WAV, 44100);
    std::vector<double> ch1 = {0.5, -0.25, 1.0};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto bytes = mem.str();
    auto header = ParseWavHeader(bytes);

    EXPECT_EQ(header.bitsPerSample, 32u);
    EXPECT_EQ(header.audioFormat, 3u);
    ASSERT_EQ(bytes.size(), kWavHeaderSize + ch1.size() * sizeof(float));

    std::vector<float> written(ch1.size());
    std::memcpy(written.data(), bytes.data() + kWavHeaderSize, written.size() * sizeof(float));
    EXPECT_FLOAT_EQ(written[0], 0.5f);
    EXPECT_FLOAT_EQ(written[1], -0.25f);
    EXPECT_FLOAT_EQ(written[2], 1.0f);
}

TEST(WavWriter, IntegerChannelsAreNormalisedToPlusMinusOneWhenPromotedTo32Bit) {
    // A float channel forces 32-bit output; the 8/16-bit integer channels
    // alongside it are then reinterpreted as signed and divided by their
    // full-scale value (get32Bit() in CWaveWriter::Impl::write()).
    CFormatter formatter(RP_F_WAV, 44100);
    std::vector<float> ch1 = {0.f};
    std::vector<uint8_t> ch2 = {0x7F};
    std::vector<uint16_t> ch3 = {0x7FFF};
    formatter.setChannel(RP_F_CH1, ch1.data(), 1);
    formatter.setChannel(RP_F_CH2, ch2.data(), 1);
    formatter.setChannel(RP_F_CH3, ch3.data(), 1);

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto bytes = mem.str();
    auto header = ParseWavHeader(bytes);

    EXPECT_EQ(header.numChannels, 3u);
    EXPECT_EQ(header.bitsPerSample, 32u);
    ASSERT_EQ(bytes.size(), kWavHeaderSize + 3 * sizeof(float));

    std::vector<float> written(3);
    std::memcpy(written.data(), bytes.data() + kWavHeaderSize, written.size() * sizeof(float));
    EXPECT_FLOAT_EQ(written[0], 0.f);
    EXPECT_FLOAT_EQ(written[1], 1.f);
    EXPECT_FLOAT_EQ(written[2], 1.f);
}

TEST(WavWriter, PackWithOnlyUnsupportedChannelsYieldsAHeaderAndNoPayload) {
    CFormatter formatter(RP_F_WAV, 44100);
    std::vector<int32_t> ch1 = {1, 2, 3};
    std::vector<uint64_t> ch2 = {1, 2, 3};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));
    formatter.setChannel(RP_F_CH2, ch2.data(), static_cast<int>(ch2.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto bytes = mem.str();
    auto header = ParseWavHeader(bytes);

    EXPECT_EQ(header.numChannels, 0u);
    EXPECT_EQ(header.dataChunkSize, 0u);
    EXPECT_EQ(bytes.size(), kWavHeaderSize);
}

TEST(WavWriter, EmptyPackYieldsAHeaderAndNoPayload) {
    CFormatter formatter(RP_F_WAV, 44100);

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto bytes = mem.str();
    auto header = ParseWavHeader(bytes);

    EXPECT_EQ(header.riffId, "RIFF");
    EXPECT_EQ(header.numChannels, 0u);
    EXPECT_EQ(header.dataChunkSize, 0u);
    EXPECT_EQ(bytes.size(), kWavHeaderSize);
}

TEST(WavWriter, ResetWriterReEmitsTheHeaderOnTheNextStream) {
    CFormatter formatter(RP_F_WAV, 44100);
    std::vector<float> ch1 = {1.f, 2.f};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

    std::stringstream first;
    ASSERT_TRUE(formatter.writeToStream(&first));
    formatter.resetWriter();
    std::stringstream second;
    ASSERT_TRUE(formatter.writeToStream(&second));

    ASSERT_EQ(first.str().size(), kWavHeaderSize + ch1.size() * sizeof(float));
    EXPECT_EQ(second.str(), first.str());
}

TEST(WavWriter, SwappingTheStreamWithoutResetWriterIsRejected) {
    // The writer stays bound to the stream it emitted the RIFF header into:
    // updateSize() keeps patching the chunk sizes at offsets 4 and 40 of
    // that stream on every write. Accepting a different stream would mean
    // writing a headerless file whose first sample bytes then get
    // overwritten by that size patching, so the write is refused instead
    // and the second stream is left untouched.
    CFormatter formatter(RP_F_WAV, 44100);
    std::vector<float> ch1 = {1.f, 2.f};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

    std::stringstream first;
    ASSERT_TRUE(formatter.writeToStream(&first));
    std::stringstream second;

    EXPECT_FALSE(formatter.writeToStream(&second));
    EXPECT_TRUE(second.str().empty());
    EXPECT_EQ(first.str().size(), kWavHeaderSize + ch1.size() * sizeof(float));
}

TEST(WavWriter, ResetWriterRebindsTheWriterToTheNextStream) {
    CFormatter formatter(RP_F_WAV, 44100);
    std::vector<float> ch1 = {1.f, 2.f};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

    std::stringstream first;
    ASSERT_TRUE(formatter.writeToStream(&first));
    std::stringstream second;
    ASSERT_FALSE(formatter.writeToStream(&second));

    formatter.resetWriter();

    EXPECT_TRUE(formatter.writeToStream(&second));
    EXPECT_EQ(second.str(), first.str());
}

TEST(WavWriter, PayloadOfARebindStreamIsNotCorruptedBySizePatching) {
    CFormatter formatter(RP_F_WAV, 44100);
    std::vector<float> ch1 = {1.f, 2.f};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

    std::stringstream first;
    ASSERT_TRUE(formatter.writeToStream(&first));
    formatter.resetWriter();
    std::stringstream second;
    ASSERT_TRUE(formatter.writeToStream(&second));

    const std::string bytes = second.str();
    ASSERT_EQ(bytes.size(), kWavHeaderSize + ch1.size() * sizeof(float));
    std::vector<float> written(ch1.size());
    std::memcpy(written.data(), bytes.data() + kWavHeaderSize, written.size() * sizeof(float));
    EXPECT_EQ(written, ch1);
}

TEST(WavWriter, BlockAlignAndByteRateScaleWithChannelCount) {
    CFormatter formatter(RP_F_WAV, 48000);
    std::vector<float> ch1 = {1.f, 2.f};
    std::vector<float> ch2 = {3.f, 4.f};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));
    formatter.setChannel(RP_F_CH2, ch2.data(), static_cast<int>(ch2.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto header = ParseWavHeader(mem.str());

    EXPECT_EQ(header.numChannels, 2u);
    EXPECT_EQ(header.bitsPerSample, 32u);
    EXPECT_EQ(header.blockAlign, 8u);
    EXPECT_EQ(header.byteRate, 2u * 48000u * 4u);
}

TEST(WavWriter, TimeAndIndexChannelsAreNotPartOfWavOutput) {
    // Unlike the TDMS writer, CWaveWriter only iterates RP_F_CH1..RP_F_CH10.
    CFormatter formatter(RP_F_WAV, 44100);
    std::vector<float> ch1 = {1.f, 2.f};
    std::vector<double> time = {0.0, 0.1};
    std::vector<uint32_t> index = {0, 1};
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));
    formatter.setChannel(RP_F_TIME, time.data(), static_cast<int>(time.size()));
    formatter.setChannel(RP_F_INDEX, index.data(), static_cast<int>(index.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto bytes = mem.str();
    auto header = ParseWavHeader(bytes);

    EXPECT_EQ(header.numChannels, 1u);
    EXPECT_EQ(bytes.size(), kWavHeaderSize + ch1.size() * sizeof(float));
}

TEST(WavWriter, InterleavedPayloadOrderFollowsAscendingChannelNumber) {
    CFormatter formatter(RP_F_WAV, 44100);
    std::vector<float> ch3 = {30.f, 31.f};
    std::vector<float> ch1 = {10.f, 11.f};
    formatter.setChannel(RP_F_CH3, ch3.data(), static_cast<int>(ch3.size()));
    formatter.setChannel(RP_F_CH1, ch1.data(), static_cast<int>(ch1.size()));

    std::stringstream mem;
    ASSERT_TRUE(formatter.writeToStream(&mem));
    auto bytes = mem.str();

    std::vector<float> written(4);
    ASSERT_EQ(bytes.size(), kWavHeaderSize + written.size() * sizeof(float));
    std::memcpy(written.data(), bytes.data() + kWavHeaderSize, written.size() * sizeof(float));

    const std::vector<float> expected = {10.f, 30.f, 11.f, 31.f};
    EXPECT_EQ(written, expected);
}
