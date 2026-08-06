/**
 * BinaryStream - the byte-level read/write primitives.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

#include "binary_stream.h"
#include "byte_utils.h"
#include "spec_builder.h"

using namespace tdms_test;
using TDMS::BinaryStream;
using TDMS::DataType;
using TDMS::TDMSType;

TEST(BinaryStreamTest, ScalarWriteEmitsExactlyTheTypeWidthLittleEndian) {
    DataType value;
    value.InitDataType(TDMSType::UnsignedInteger32, DataType::MakeData<std::uint32_t>(0x12345678u));
    std::stringstream stream = EmptyStream();
    BinaryStream::Write(stream, value);
    EXPECT_EQ(stream.str(), std::string("\x78\x56\x34\x12", 4));
}

TEST(BinaryStreamTest, StringWriteIsALengthPrefixFollowedByTheBytes) {
    const std::string text = "osc_rate";
    auto* buffer = new std::uint8_t[text.size()];
    std::memcpy(buffer, text.data(), text.size());
    DataType value;
    value.InitStringType(static_cast<std::uint32_t>(text.size()), buffer);

    std::stringstream stream = EmptyStream();
    BinaryStream::Write(stream, value);

    const std::string written = stream.str();
    ASSERT_EQ(written.size(), 4u + text.size());
    std::uint32_t prefix = 0;
    std::memcpy(&prefix, written.data(), sizeof(prefix));
    EXPECT_EQ(prefix, text.size());
    EXPECT_EQ(written.substr(4), text);
}

TEST(BinaryStreamTest, EveryScalarTypeSurvivesAWriteReadRoundTrip) {
    struct Case {
        TDMSType type;
        std::uint64_t bits;
        std::uint32_t width;
    };
    const Case cases[] = {
        {TDMSType::Integer8, 0xFFull, 1},
        {TDMSType::Integer16, 0xFEFFull, 2},
        {TDMSType::Integer32, 0x12345678ull, 4},
        {TDMSType::Integer64, 0x0123456789ABCDEFull, 8},
        {TDMSType::UnsignedInteger8, 0xC8ull, 1},
        {TDMSType::UnsignedInteger16, 0x1234ull, 2},
        {TDMSType::UnsignedInteger32, 0xAABBCCDDull, 4},
        {TDMSType::UnsignedInteger64, 0xFEDCBA9876543210ull, 8},
        {TDMSType::SingleFloat, 0x3F800000ull, 4},
        {TDMSType::DoubleFloat, 0x3FF0000000000000ull, 8},
        {TDMSType::Boolean, 0x01ull, 1},
    };

    for (const Case& c : cases) {
        SCOPED_TRACE("type " + std::to_string(static_cast<std::uint32_t>(c.type)));
        auto* buffer = new std::uint8_t[c.width];
        std::memcpy(buffer, &c.bits, c.width);
        DataType value;
        value.InitDataType(c.type, buffer);

        std::stringstream stream = EmptyStream();
        BinaryStream::Write(stream, value);
        ASSERT_EQ(stream.str().size(), c.width);
        stream.seekg(0, std::ios::beg);

        DataType decoded = BinaryStream::Read(stream, c.type);
        EXPECT_EQ(decoded.GetDataType(), c.type);
        ASSERT_NE(decoded.GetRawData(), nullptr);
        EXPECT_EQ(std::memcmp(decoded.GetRawData(), &c.bits, c.width), 0);
    }
}

TEST(BinaryStreamTest, LengthPrefixedStringSurvivesARoundTrip) {
    Builder builder;
    builder.PString("Group");
    const Bytes bytes = builder.bytes();
    std::stringstream stream = MakeStream(bytes);

    DataType decoded = BinaryStream::ReadLengthPrefixedString(stream);
    EXPECT_EQ(decoded.GetDataType(), TDMSType::String);
    EXPECT_EQ(decoded.GetLength(), 5u);
    EXPECT_EQ(decoded.GetDataString(), "Group");
}

TEST(BinaryStreamTest, ReadStringTakesAnExplicitLength) {
    std::stringstream stream = MakeStream(ToBytes("CH1CH2"));
    stream.seekg(3, std::ios::beg);
    DataType decoded = BinaryStream::ReadString(stream, 3);
    EXPECT_EQ(decoded.GetDataString(), "CH2");
}

TEST(BinaryStreamTest, ReadArrayReadsFromAnAbsoluteOffsetAndRestoresThePosition) {
    const Bytes payload = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    std::stringstream stream = MakeStream(payload);
    stream.seekg(1, std::ios::beg);

    auto buffer = BinaryStream::ReadArray(stream, 3, 2);
    ASSERT_NE(buffer.get(), nullptr);
    EXPECT_EQ(buffer[0], 0x03u);
    EXPECT_EQ(buffer[1], 0x04u);
    EXPECT_EQ(buffer[2], 0x05u);
    EXPECT_EQ(stream.tellg(), std::streampos(1));
}

TEST(BinaryStreamTest, InterleavedReadArrayAdvancesItsDestination) {
    // The loop used to read every element into the START of the destination
    // buffer, so only one sample survived.
    Builder builder;
    builder.I32(10).I32(99);    // channel A sample 0, channel B sample 0
    builder.I32(11).I32(99);
    builder.I32(12).I32(99);
    const Bytes bytes = builder.bytes();
    std::stringstream stream = MakeStream(bytes);

    // dataSize 4, count 3, offset 0, skip 4 (one foreign int32 between samples)
    auto buffer = BinaryStream::ReadArray(stream, 4, 3, 0, 4);
    ASSERT_NE(buffer.get(), nullptr);

    std::int32_t values[3] = {0, 0, 0};
    std::memcpy(values, buffer.get(), sizeof(values));
    EXPECT_EQ(values[0], 10);
    EXPECT_EQ(values[1], 11);
    EXPECT_EQ(values[2], 12);
}

TEST(BinaryStreamTest, AnUnknownTypeDoesNotSilentlyLeaveTheStreamInPlace) {
    // Returning an empty value without consuming anything desynchronises the
    // whole metadata parse; the reader must be able to tell that it failed.
    std::stringstream stream = MakeStream(Bytes{0x11, 0x22, 0x33, 0x44});
    DataType decoded = BinaryStream::Read(stream, TDMSType::ExtendedFloat);
    EXPECT_EQ(decoded.GetDataType(), TDMSType::Empty);
    EXPECT_TRUE(stream.fail() || stream.eof()) << "an undecodable type must mark the stream, not look successful";
}

TEST(BinaryStreamTest, AShortReadIsReportedRatherThanReturningStaleBytes) {
    std::stringstream stream = MakeStream(Bytes{0x01, 0x02});   // only 2 of 8 bytes
    DataType decoded = BinaryStream::Read(stream, TDMSType::Integer64);
    EXPECT_TRUE(stream.fail() || stream.eof());
    (void)decoded;
}

TEST(BinaryStreamTest, AHugeStringLengthPrefixDoesNotAllocateWildly) {
    Builder builder;
    builder.U32(0xFFFFFFF0u);   // length prefix far beyond the stream
    const Bytes bytes = builder.bytes();
    std::stringstream stream = MakeStream(bytes);
    EXPECT_NO_THROW({
        DataType decoded = BinaryStream::Read(stream, TDMSType::String);
        EXPECT_TRUE(decoded.GetDataString().empty());
    });
}

TEST(BinaryStreamTest, EmptyAndVoidWritesMatchTheFormat) {
    {
        DataType value;   // Empty
        std::stringstream stream = EmptyStream();
        BinaryStream::Write(stream, value);
        EXPECT_TRUE(stream.str().empty());
    }
    {
        DataType value;
        value.InitDataType(TDMSType::Void, nullptr);
        std::stringstream stream = EmptyStream();
        BinaryStream::Write(stream, value);
        EXPECT_EQ(stream.str(), std::string(1, '\0'));
    }
}
