/**
 * Unit tests for rp_formatter_api::TDMS::BinaryStream
 * (src/tdms/binary_stream.cpp).
 *
 * BinaryStream::Write() is what actually serialises every TDMS property
 * value written by Writer::Write(), so the exact byte layout it produces is
 * part of the file format. Read()/ReadLengthPrefixedString() are the
 * matching public deserialisers; they are covered here by round-tripping
 * through a std::stringstream, which also pins down the length-prefixed
 * string encoding (4-byte little-endian count, no NUL terminator).
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

#include "binary_stream.h"
#include "data_type.h"

using namespace rp_formatter_api::TDMS;

namespace {

template <typename T>
DataType MakeScalar(TDMSType type, T value) {
    DataType data;
    data.InitDataType(type, DataType::MakeData<T>(value));
    return data;
}

template <typename T>
std::string Serialize(TDMSType type, T value) {
    DataType data = MakeScalar<T>(type, value);
    std::stringstream stream;
    BinaryStream::Write(stream, data);
    return stream.str();
}

// Writes `value`, rewinds, reads it back through the public Read() entry
// point and returns the decoded scalar.
template <typename T>
T RoundTrip(TDMSType type, T value) {
    DataType data = MakeScalar<T>(type, value);
    std::stringstream stream;
    BinaryStream::Write(stream, data);
    stream.seekg(0, std::ios::beg);
    return BinaryStream::Read<T>(stream, type);
}

}  // namespace

TEST(TdmsBinaryStream, ScalarWriteEmitsExactlyTheTypeWidth) {
    EXPECT_EQ(Serialize<int8_t>(TDMSType::Integer8, 1).size(), 1u);
    EXPECT_EQ(Serialize<int16_t>(TDMSType::Integer16, 1).size(), 2u);
    EXPECT_EQ(Serialize<int32_t>(TDMSType::Integer32, 1).size(), 4u);
    EXPECT_EQ(Serialize<int64_t>(TDMSType::Integer64, 1).size(), 8u);
    EXPECT_EQ(Serialize<uint8_t>(TDMSType::UnsignedInteger8, 1).size(), 1u);
    EXPECT_EQ(Serialize<uint16_t>(TDMSType::UnsignedInteger16, 1).size(), 2u);
    EXPECT_EQ(Serialize<uint32_t>(TDMSType::UnsignedInteger32, 1).size(), 4u);
    EXPECT_EQ(Serialize<uint64_t>(TDMSType::UnsignedInteger64, 1).size(), 8u);
    EXPECT_EQ(Serialize<float>(TDMSType::SingleFloat, 1.f).size(), 4u);
    EXPECT_EQ(Serialize<double>(TDMSType::DoubleFloat, 1.0).size(), 8u);
    EXPECT_EQ(Serialize<uint8_t>(TDMSType::Boolean, 1).size(), 1u);
}

TEST(TdmsBinaryStream, Uint32IsWrittenLittleEndian) {
    const std::string bytes = Serialize<uint32_t>(TDMSType::UnsignedInteger32, 0x12345678u);
    ASSERT_EQ(bytes.size(), 4u);
    EXPECT_EQ(static_cast<uint8_t>(bytes[0]), 0x78u);
    EXPECT_EQ(static_cast<uint8_t>(bytes[1]), 0x56u);
    EXPECT_EQ(static_cast<uint8_t>(bytes[2]), 0x34u);
    EXPECT_EQ(static_cast<uint8_t>(bytes[3]), 0x12u);
}

TEST(TdmsBinaryStream, EmptyTypeWritesNothing) {
    DataType data;
    std::stringstream stream;
    BinaryStream::Write(stream, data);
    EXPECT_TRUE(stream.str().empty());
}

TEST(TdmsBinaryStream, VoidTypeWritesASingleZeroByte) {
    DataType data;
    data.InitDataType(TDMSType::Void, nullptr);
    std::stringstream stream;
    BinaryStream::Write(stream, data);
    EXPECT_EQ(stream.str(), std::string(1, '\0'));
}

TEST(TdmsBinaryStream, UnsupportedTypeThrowsInvalidArgument) {
    DataType data;
    data.InitDataType(TDMSType::ExtendedFloat, nullptr);
    std::stringstream stream;
    EXPECT_THROW(BinaryStream::Write(stream, data), std::invalid_argument);
}

TEST(TdmsBinaryStream, TimeStampWritesBothSixtyFourBitHalves) {
    auto* value = new uint64_t[2];
    value[0] = 0x1111111111111111ull;
    value[1] = 0x2222222222222222ull;
    DataType data;
    data.InitDataType(TDMSType::TimeStamp, value);

    std::stringstream stream;
    BinaryStream::Write(stream, data);

    const std::string bytes = stream.str();
    ASSERT_EQ(bytes.size(), 16u);
    uint64_t subseconds = 0;
    uint64_t seconds = 0;
    std::memcpy(&subseconds, bytes.data(), sizeof(subseconds));
    std::memcpy(&seconds, bytes.data() + sizeof(subseconds), sizeof(seconds));
    EXPECT_EQ(subseconds, 0x1111111111111111ull);
    EXPECT_EQ(seconds, 0x2222222222222222ull);
}

TEST(TdmsBinaryStream, ScalarValuesSurviveAWriteReadRoundTrip) {
    EXPECT_EQ(RoundTrip<int8_t>(TDMSType::Integer8, -100), -100);
    EXPECT_EQ(RoundTrip<int16_t>(TDMSType::Integer16, -30000), -30000);
    EXPECT_EQ(RoundTrip<int32_t>(TDMSType::Integer32, -2000000000), -2000000000);
    EXPECT_EQ(RoundTrip<int64_t>(TDMSType::Integer64, -9000000000000000000ll), -9000000000000000000ll);
    EXPECT_EQ(RoundTrip<uint8_t>(TDMSType::UnsignedInteger8, 255), 255u);
    EXPECT_EQ(RoundTrip<uint16_t>(TDMSType::UnsignedInteger16, 65535), 65535u);
    EXPECT_EQ(RoundTrip<uint32_t>(TDMSType::UnsignedInteger32, 4294967295u), 4294967295u);
    EXPECT_EQ(RoundTrip<uint64_t>(TDMSType::UnsignedInteger64, 18446744073709551615ull), 18446744073709551615ull);
    EXPECT_FLOAT_EQ(RoundTrip<float>(TDMSType::SingleFloat, -1.25f), -1.25f);
    EXPECT_DOUBLE_EQ(RoundTrip<double>(TDMSType::DoubleFloat, 1e-9), 1e-9);
}

TEST(TdmsBinaryStream, StringIsWrittenAsFourByteLittleEndianLengthThenRawBytes) {
    const std::string text = "osc_rate";
    char* buffer = new char[text.size()];
    std::memcpy(buffer, text.data(), text.size());
    DataType data;
    data.InitStringType(static_cast<uint32_t>(text.size()), buffer);

    std::stringstream stream;
    BinaryStream::Write(stream, data);

    const std::string bytes = stream.str();
    ASSERT_EQ(bytes.size(), 4u + text.size());
    uint32_t prefix = 0;
    std::memcpy(&prefix, bytes.data(), sizeof(prefix));
    EXPECT_EQ(prefix, static_cast<uint32_t>(text.size()));
    EXPECT_EQ(bytes.substr(4), text);
}

TEST(TdmsBinaryStream, LengthPrefixedStringSurvivesARoundTrip) {
    const std::string text = "Group";
    char* buffer = new char[text.size()];
    std::memcpy(buffer, text.data(), text.size());
    DataType data;
    data.InitStringType(static_cast<uint32_t>(text.size()), buffer);

    std::stringstream stream;
    BinaryStream::Write(stream, data);
    stream.seekg(0, std::ios::beg);

    DataType decoded = BinaryStream::ReadLengthPrefixedString(stream);
    EXPECT_EQ(decoded.GetDataType(), TDMSType::String);
    EXPECT_EQ(decoded.GetLength(), static_cast<uint32_t>(text.size()));
    EXPECT_EQ(decoded.GetDataString(), text);
}

TEST(TdmsBinaryStream, ReadStringTakesAnExplicitLengthWithoutALengthPrefix) {
    std::stringstream stream;
    stream << "CH1CH2";
    stream.seekg(3, std::ios::beg);

    DataType decoded = BinaryStream::ReadString(stream, 3);
    EXPECT_EQ(decoded.GetDataString(), "CH2");
}

TEST(TdmsBinaryStream, ReadOfEmptyTypeConsumesNothingAndYieldsAnEmptyValue) {
    std::stringstream stream;
    stream << "abc";
    stream.seekg(0, std::ios::beg);

    DataType decoded = BinaryStream::Read(stream, TDMSType::Empty);
    EXPECT_EQ(decoded.GetDataType(), TDMSType::Empty);
    EXPECT_EQ(stream.tellg(), std::streampos(0));
}

TEST(TdmsBinaryStream, ConsecutiveScalarsAreReadBackInWriteOrder) {
    DataType first = MakeScalar<int32_t>(TDMSType::Integer32, 11);
    DataType second = MakeScalar<int32_t>(TDMSType::Integer32, 22);

    std::stringstream stream;
    BinaryStream::Write(stream, first);
    BinaryStream::Write(stream, second);
    stream.seekg(0, std::ios::beg);

    EXPECT_EQ(BinaryStream::Read<int32_t>(stream, TDMSType::Integer32), 11);
    EXPECT_EQ(BinaryStream::Read<int32_t>(stream, TDMSType::Integer32), 22);
}

TEST(TdmsBinaryStream, ReadArrayReadsFromAnAbsoluteOffsetAndRestoresGetPosition) {
    std::stringstream stream;
    const std::string payload("\x01\x02\x03\x04\x05\x06", 6);
    stream.write(payload.data(), payload.size());
    stream.seekg(1, std::ios::beg);

    auto buffer = BinaryStream::ReadArray(stream, 3, 2);

    ASSERT_NE(buffer.get(), nullptr);
    EXPECT_EQ(buffer[0], 0x03u);
    EXPECT_EQ(buffer[1], 0x04u);
    EXPECT_EQ(buffer[2], 0x05u);
    EXPECT_EQ(stream.tellg(), std::streampos(1));
}
