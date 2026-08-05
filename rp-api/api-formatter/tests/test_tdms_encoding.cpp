/**
 * Unit tests for src/tdms/tdms_bytes.h - the little-endian byte emission
 * layer underneath the TDMS writer.
 *
 * These assert fixed bit patterns rather than round-tripping through a
 * matching reader. A round trip passes even when writer and reader share the
 * same misunderstanding of the format; a pinned byte pattern does not. The
 * expectations below are the same ones npTDMS applies when reading (struct
 * format '<' for every field).
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "tdms_bytes.h"

using namespace rp_formatter_api::tdms;

namespace {

template <typename T>
std::vector<std::uint8_t> Appended(T value) {
    std::vector<std::uint8_t> out;
    AppendLE<T>(out, value);
    return out;
}

std::vector<std::uint8_t> Bytes(std::initializer_list<int> values) {
    std::vector<std::uint8_t> out;
    for (int v : values) {
        out.push_back(static_cast<std::uint8_t>(v));
    }
    return out;
}

}  // namespace

TEST(TdmsBytes, AppendLeEmitsLittleEndianForEveryIntegerWidth) {
    EXPECT_EQ(Appended<std::uint8_t>(0x12), Bytes({0x12}));
    EXPECT_EQ(Appended<std::int8_t>(-1), Bytes({0xFF}));
    EXPECT_EQ(Appended<std::uint16_t>(0x1234), Bytes({0x34, 0x12}));
    EXPECT_EQ(Appended<std::int16_t>(-2), Bytes({0xFE, 0xFF}));
    EXPECT_EQ(Appended<std::uint32_t>(0x12345678u), Bytes({0x78, 0x56, 0x34, 0x12}));
    EXPECT_EQ(Appended<std::int32_t>(-2), Bytes({0xFE, 0xFF, 0xFF, 0xFF}));
    EXPECT_EQ(Appended<std::uint64_t>(0x0123456789ABCDEFull), Bytes({0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01}));
    EXPECT_EQ(Appended<std::int64_t>(-2), Bytes({0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
}

TEST(TdmsBytes, AppendLeEmitsIeee754LittleEndianForFloats) {
    EXPECT_EQ(Appended<float>(1.0f), Bytes({0x00, 0x00, 0x80, 0x3F}));
    EXPECT_EQ(Appended<float>(-2.0f), Bytes({0x00, 0x00, 0x00, 0xC0}));
    EXPECT_EQ(Appended<double>(1.0), Bytes({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F}));
    EXPECT_EQ(Appended<double>(-2.0), Bytes({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0}));
}

TEST(TdmsBytes, AppendLeOfA64BitValueAboveTwoGigabytesKeepsAllEightBytes) {
    // The old writer stored sample counts in a `long`, which is 32-bit on the
    // ARM target this library ships on; anything past 2^31 was truncated.
    EXPECT_EQ(Appended<std::uint64_t>(0x1'0000'0000ull), Bytes({0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}));
    EXPECT_EQ(Appended<std::uint64_t>(0xFFFF'FFFF'FFFF'FFFFull), Bytes({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
}

TEST(TdmsBytes, AppendLeAppendsRatherThanReplacing) {
    std::vector<std::uint8_t> out;
    AppendLE<std::uint16_t>(out, 0x1234);
    AppendLE<std::uint16_t>(out, 0x5678);
    EXPECT_EQ(out, Bytes({0x34, 0x12, 0x78, 0x56}));
}

TEST(TdmsBytes, AppendStringWritesA32BitLengthPrefixThenRawBytes) {
    std::vector<std::uint8_t> out;
    AppendString(out, "osc_rate");

    ASSERT_EQ(out.size(), 4u + 8u);
    EXPECT_EQ(std::vector<std::uint8_t>(out.begin(), out.begin() + 4), Bytes({0x08, 0x00, 0x00, 0x00}));
    EXPECT_EQ(std::string(out.begin() + 4, out.end()), "osc_rate");
}

TEST(TdmsBytes, AppendStringEncodesTheEmptyStringAsAZeroLength) {
    std::vector<std::uint8_t> out;
    AppendString(out, "");
    EXPECT_EQ(out, Bytes({0x00, 0x00, 0x00, 0x00}));
}

TEST(TdmsBytes, AppendStringIsNotNulTerminatedAndKeepsEmbeddedNuls) {
    const std::string text("a\0b", 3);
    std::vector<std::uint8_t> out;
    AppendString(out, text);

    ASSERT_EQ(out.size(), 4u + 3u);
    EXPECT_EQ(out[4], 'a');
    EXPECT_EQ(out[5], 0u);
    EXPECT_EQ(out[6], 'b');
}

TEST(TdmsBytes, AppendBytesCopiesTheBufferVerbatim) {
    const std::uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    std::vector<std::uint8_t> out;
    AppendBytes(out, payload, sizeof(payload));
    EXPECT_EQ(out, Bytes({0xDE, 0xAD, 0xBE, 0xEF}));
}

TEST(TdmsBytes, WriteLeProducesTheSameBytesAsAppendLe) {
    std::stringstream stream;
    WriteLE<std::uint32_t>(stream, 0x12345678u);
    WriteLE<double>(stream, 1.0);

    std::vector<std::uint8_t> expected;
    AppendLE<std::uint32_t>(expected, 0x12345678u);
    AppendLE<double>(expected, 1.0);

    const std::string written = stream.str();
    ASSERT_EQ(written.size(), expected.size());
    EXPECT_EQ(std::vector<std::uint8_t>(written.begin(), written.end()), expected);
}

TEST(TdmsBytes, WriteBytesCopiesTheBufferVerbatim) {
    const std::uint8_t payload[] = {0x01, 0x02, 0x03};
    std::stringstream stream;
    WriteBytes(stream, payload, sizeof(payload));
    EXPECT_EQ(stream.str(), std::string("\x01\x02\x03", 3));
}
