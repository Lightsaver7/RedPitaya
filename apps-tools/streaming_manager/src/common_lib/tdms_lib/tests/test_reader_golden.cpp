/**
 * The reader, validated against the SAME hand-derived golden bytes the writer
 * tests use.
 *
 * This file deliberately never mentions TDMS::Writer. Checking the reader
 * against the writer's output would pass even if both shared the same
 * misunderstanding of the format; checking it against bytes derived from the
 * specification will not.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "byte_utils.h"
#include "golden_bytes.h"
#include "reader.h"
#include "spec_builder.h"

using namespace tdms_test;

namespace {

template <typename T>
auto SamplesOf(const std::shared_ptr<TDMS::Metadata>& metadata) -> std::vector<T> {
    std::vector<T> out;
    for (const auto& raw : metadata->RawData.DataType.GetRawVector()) {
        const std::size_t count = static_cast<std::size_t>(raw->size) / sizeof(T);
        for (std::size_t i = 0; i < count; i++) {
            T value{};
            std::memcpy(&value, raw->data.get() + i * sizeof(T), sizeof(T));
            out.push_back(value);
        }
    }
    return out;
}

}  // namespace

TEST(ReaderGolden, ReferenceSegmentLeadInIsDecodedExactly) {
    const Bytes bytes = golden::Reference();
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, bytes.size());

    auto segment = reader.ReadFirstSegment();
    ASSERT_NE(segment, nullptr);
    EXPECT_EQ(segment->Identifier, "TDSm");
    EXPECT_EQ(segment->Version, kTdms2Version);
    EXPECT_TRUE(segment->TableOfContents.HasMetaData);
    EXPECT_TRUE(segment->TableOfContents.HasRawData);
    EXPECT_FALSE(segment->TableOfContents.RawDataIsInterleaved);
    EXPECT_FALSE(segment->TableOfContents.NumbersAreBigEndian);
    EXPECT_FALSE(segment->TableOfContents.HasDaqMxData);
    EXPECT_FALSE(segment->TableOfContents.ContainsNewObjects);
    // Both offsets are reported absolute by the reader (it adds offset + 28).
    EXPECT_EQ(segment->MetadataOffset, 28);
    EXPECT_EQ(segment->NextSegmentOffset, static_cast<long>(golden::kReferenceSize));
    EXPECT_EQ(segment->RawDataOffset, 28 + golden::kReferenceMetadataLength);
}

TEST(ReaderGolden, ReferenceSegmentMetadataIsDecodedExactly) {
    const Bytes bytes = golden::Reference();
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, bytes.size());

    auto segment = reader.ReadFirstSegment();
    ASSERT_NE(segment, nullptr);
    auto metadata = reader.ReadMetadata(segment);
    ASSERT_EQ(metadata.size(), 2u);

    // Group object.
    EXPECT_EQ(metadata[0]->PathStr, "/'Group'");
    EXPECT_EQ(metadata[0]->RawData.Count, 0);
    EXPECT_EQ(metadata[0]->RawData.Size, 0);
    ASSERT_EQ(metadata[0]->Properties.size(), 1u);
    ASSERT_EQ(metadata[0]->Properties.count("osc_rate"), 1u);
    EXPECT_EQ(metadata[0]->Properties.at("osc_rate").GetDataType(), TDMS::TDMSType::UnsignedInteger64);
    EXPECT_EQ(metadata[0]->Properties.at("osc_rate").GetData<std::uint64_t>(), golden::kReferenceOscRate);

    // Channel object.
    EXPECT_EQ(metadata[1]->PathStr, "/'Group'/'CH1'");
    EXPECT_EQ(metadata[1]->RawData.DataType.GetDataType(), TDMS::TDMSType::SingleFloat);
    EXPECT_EQ(metadata[1]->RawData.Count, 4);
    EXPECT_EQ(metadata[1]->RawData.Size, 16);
    EXPECT_EQ(metadata[1]->RawData.Dimension, 1);
    EXPECT_FALSE(metadata[1]->RawData.IsInterleaved);
    EXPECT_EQ(metadata[1]->RawData.Offset, 28 + golden::kReferenceMetadataLength);
    EXPECT_TRUE(metadata[1]->Properties.empty());

    const std::vector<float> expected = {1.f, 2.f, 3.f, 4.f};
    EXPECT_EQ(SamplesOf<float>(metadata[1]), expected);
}

TEST(ReaderGolden, PathVectorKeepsTheQuoteCharacters) {
    // Documented inconsistency, deliberately preserved: the reader fills Path
    // from the regex full match, so entries include the surrounding quotes,
    // while WriterSegment::GenerateGroup/GenerateChannel store bare names. The
    // full unquoted-vs-quoted path is available either way via PathStr.
    // Changing this would break external code that already reads Path.
    const Bytes bytes = golden::Reference();
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, bytes.size());
    auto metadata = reader.ReadMetadata(reader.ReadFirstSegment());
    ASSERT_EQ(metadata.size(), 2u);

    ASSERT_EQ(metadata[0]->Path.size(), 1u);
    EXPECT_EQ(metadata[0]->Path[0], "'Group'");
    ASSERT_EQ(metadata[1]->Path.size(), 2u);
    EXPECT_EQ(metadata[1]->Path[0], "'Group'");
    EXPECT_EQ(metadata[1]->Path[1], "'CH1'");
}

TEST(ReaderGolden, GroupOnlySegmentHasNoRawData) {
    const Bytes bytes = golden::GroupOnly();
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, bytes.size());

    auto segment = reader.ReadFirstSegment();
    ASSERT_NE(segment, nullptr);
    EXPECT_TRUE(segment->TableOfContents.HasMetaData);
    EXPECT_FALSE(segment->TableOfContents.HasRawData);

    auto metadata = reader.ReadMetadata(segment);
    ASSERT_EQ(metadata.size(), 1u);
    EXPECT_EQ(metadata[0]->PathStr, "/'Group'");
    EXPECT_EQ(metadata[0]->RawData.Count, 0);
    EXPECT_TRUE(metadata[0]->Properties.empty());
}

TEST(ReaderGolden, TwoContiguousChannelsGetSeparateRawRegions) {
    const Bytes bytes = golden::TwoChannelsContiguous();
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, bytes.size());

    auto metadata = reader.ReadMetadata(reader.ReadFirstSegment());
    ASSERT_EQ(metadata.size(), 3u);
    EXPECT_EQ(metadata[0]->PathStr, "/'G'");

    const std::vector<std::int32_t> expectedA = {10, 11, 12};
    const std::vector<std::int32_t> expectedB = {20, 21, 22};
    EXPECT_EQ(SamplesOf<std::int32_t>(metadata[1]), expectedA);
    EXPECT_EQ(SamplesOf<std::int32_t>(metadata[2]), expectedB);
    // Channel B's region starts right after channel A's 12 bytes.
    EXPECT_EQ(metadata[2]->RawData.Offset - metadata[1]->RawData.Offset, 12);
}

TEST(ReaderGolden, EveryScalarTypeRoundTripsFromGoldenBytes) {
    struct Case {
        std::uint32_t code;
        TDMS::TDMSType type;
        std::uint32_t width;
    };
    const Case cases[] = {
        {kTypeI8, TDMS::TDMSType::Integer8, 1},   {kTypeI16, TDMS::TDMSType::Integer16, 2},
        {kTypeI32, TDMS::TDMSType::Integer32, 4}, {kTypeI64, TDMS::TDMSType::Integer64, 8},
        {kTypeU8, TDMS::TDMSType::UnsignedInteger8, 1}, {kTypeU16, TDMS::TDMSType::UnsignedInteger16, 2},
        {kTypeU32, TDMS::TDMSType::UnsignedInteger32, 4}, {kTypeU64, TDMS::TDMSType::UnsignedInteger64, 8},
        {kTypeF32, TDMS::TDMSType::SingleFloat, 4}, {kTypeF64, TDMS::TDMSType::DoubleFloat, 8},
        {kTypeBool, TDMS::TDMSType::Boolean, 1},
    };

    for (const Case& c : cases) {
        SCOPED_TRACE("type code " + std::to_string(c.code));
        Builder meta;
        meta.U32(1);
        meta.ObjectWithRaw("/'G'/'A'", c.code, 2, 0);
        Bytes raw;
        for (std::uint32_t i = 0; i < 2 * c.width; i++) {
            raw.push_back(static_cast<std::uint8_t>(0xA0 + i));
        }
        Builder file;
        file.LeadIn(kTocMetaData | kTocRawData, static_cast<std::int64_t>(meta.size() + raw.size()),
                    static_cast<std::int64_t>(meta.size()));
        file.Raw(meta.bytes()).Raw(raw);

        const Bytes bytes = file.bytes();
        std::stringstream stream = MakeStream(bytes);
        TDMS::Reader reader(stream, bytes.size());
        auto metadata = reader.ReadMetadata(reader.ReadFirstSegment());
        ASSERT_EQ(metadata.size(), 1u);
        EXPECT_EQ(metadata[0]->RawData.DataType.GetDataType(), c.type);
        EXPECT_EQ(metadata[0]->RawData.Count, 2);
        EXPECT_EQ(metadata[0]->RawData.Size, static_cast<long>(2 * c.width));

        auto vec = metadata[0]->RawData.DataType.GetRawVector();
        ASSERT_EQ(vec.size(), 1u);
        ASSERT_EQ(vec[0]->size, 2u * c.width);
        for (std::uint32_t i = 0; i < 2 * c.width; i++) {
            EXPECT_EQ(vec[0]->data.get()[i], raw[i]) << "byte " << i;
        }
    }
}

TEST(ReaderGolden, EveryPropertyTypeIsDecodedFromGoldenBytes) {
    Builder meta;
    meta.U32(1);
    meta.PString("/'G'");
    meta.U32(kNoRawDataIndex);
    meta.U32(6);
    // Keys chosen so ascending order matches the emission order below.
    meta.PropertyHeader("a_i32", kTypeI32).I32(-123456);
    meta.PropertyHeader("b_u64", kTypeU64).U64(18000000000000000000ull);
    meta.PropertyHeader("c_f64", kTypeF64).F64(-2.25);
    meta.PropertyHeader("d_bool", kTypeBool).U8(1);
    meta.PropertyHeader("e_str", kTypeString).PString("Voltage");
    meta.PropertyHeader("f_time", kTypeTimeStamp).U64(0).I64(3868000000LL);

    Builder file;
    file.LeadIn(kTocMetaData, static_cast<std::int64_t>(meta.size()), 0);
    file.Raw(meta.bytes());

    const Bytes bytes = file.bytes();
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, bytes.size());
    auto metadata = reader.ReadMetadata(reader.ReadFirstSegment());
    ASSERT_EQ(metadata.size(), 1u);
    auto& props = metadata[0]->Properties;
    ASSERT_EQ(props.size(), 6u);

    EXPECT_EQ(props.at("a_i32").GetData<std::int32_t>(), -123456);
    EXPECT_EQ(props.at("b_u64").GetData<std::uint64_t>(), 18000000000000000000ull);
    EXPECT_DOUBLE_EQ(props.at("c_f64").GetData<double>(), -2.25);
    EXPECT_EQ(props.at("d_bool").GetData<std::uint8_t>(), 1u);
    EXPECT_EQ(props.at("e_str").GetDataType(), TDMS::TDMSType::String);
    EXPECT_EQ(props.at("e_str").GetDataString(), "Voltage");
    EXPECT_EQ(props.at("f_time").GetDataType(), TDMS::TDMSType::TimeStamp);
    const auto* stamp = static_cast<const std::uint64_t*>(props.at("f_time").GetRawData());
    ASSERT_NE(stamp, nullptr);
    EXPECT_EQ(stamp[0], 0u);
    EXPECT_EQ(stamp[1], 3868000000ull);
}

TEST(ReaderGolden, ReadSegmentPastTheEndOfFileReturnsNull) {
    const Bytes bytes = golden::Reference();
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, bytes.size());
    EXPECT_EQ(reader.ReadSegment(bytes.size()), nullptr);
    EXPECT_EQ(reader.ReadSegment(bytes.size() + 1000), nullptr);
}

TEST(ReaderGolden, GetFileSizeReportsWhatItWasConstructedWith) {
    const Bytes bytes = golden::Reference();
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, bytes.size());
    EXPECT_EQ(reader.GetFileSize(), bytes.size());
}
