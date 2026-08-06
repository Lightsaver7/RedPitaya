/**
 * Interleaved raw data.
 *
 * This was broken three ways at once: the array reader overwrote the start of
 * its destination buffer on every iteration, the interleave stride was computed
 * only after the samples had already been read (so the stride was 0 at read
 * time and `stride - width` underflowed through unsigned arithmetic), and
 * Raw::size recorded one element's width instead of the whole block. The
 * observable result was one sample per channel instead of N.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
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

TEST(ReaderInterleaved, TwoInt32ChannelsYieldAllThreeSamplesEach) {
    const Bytes bytes = golden::TwoChannelsInterleaved();
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, bytes.size());

    auto segment = reader.ReadFirstSegment();
    ASSERT_NE(segment, nullptr);
    ASSERT_TRUE(segment->TableOfContents.RawDataIsInterleaved);

    auto metadata = reader.ReadMetadata(segment);
    ASSERT_EQ(metadata.size(), 2u);

    EXPECT_EQ(metadata[0]->RawData.InterleaveStride, 8) << "int32 + int32";
    EXPECT_EQ(metadata[1]->RawData.InterleaveStride, 8);
    EXPECT_EQ(metadata[0]->RawData.Count, 3);
    EXPECT_EQ(metadata[1]->RawData.Count, 3);
    EXPECT_EQ(metadata[0]->RawData.Size, 12);
    EXPECT_EQ(metadata[1]->RawData.Size, 12);

    const std::vector<std::int32_t> expectedA = {10, 11, 12};
    const std::vector<std::int32_t> expectedB = {20, 21, 22};
    EXPECT_EQ(SamplesOf<std::int32_t>(metadata[0]), expectedA);
    EXPECT_EQ(SamplesOf<std::int32_t>(metadata[1]), expectedB);
}

TEST(ReaderInterleaved, ChannelOffsetsAreIntraStride) {
    const Bytes bytes = golden::TwoChannelsInterleaved();
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, bytes.size());
    auto segment = reader.ReadFirstSegment();
    auto metadata = reader.ReadMetadata(segment);
    ASSERT_EQ(metadata.size(), 2u);

    EXPECT_EQ(metadata[0]->RawData.Offset, segment->RawDataOffset);
    EXPECT_EQ(metadata[1]->RawData.Offset, segment->RawDataOffset + 4) << "second int32 within the stride";
}

TEST(ReaderInterleaved, ThreeChannelsOfMixedWidthsUseTheSummedStride) {
    // int8 + int16 + float32 -> stride 7, deliberately not a power of two.
    Builder meta;
    meta.U32(3);
    meta.ObjectWithRaw("/'G'/'A'", kTypeI8, 2, 0);
    meta.ObjectWithRaw("/'G'/'B'", kTypeI16, 2, 0);
    meta.ObjectWithRaw("/'G'/'C'", kTypeF32, 2, 0);

    Builder raw;
    raw.I8(1).I16(100).F32(1.5f);
    raw.I8(2).I16(200).F32(2.5f);

    Builder file;
    file.LeadIn(kTocMetaData | kTocRawData | kTocInterleaved, static_cast<std::int64_t>(meta.size() + raw.size()),
                static_cast<std::int64_t>(meta.size()));
    file.Raw(meta.bytes()).Raw(raw.bytes());

    const Bytes bytes = file.bytes();
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, bytes.size());
    auto metadata = reader.ReadMetadata(reader.ReadFirstSegment());
    ASSERT_EQ(metadata.size(), 3u);

    for (const auto& m : metadata) {
        EXPECT_EQ(m->RawData.InterleaveStride, 7);
        EXPECT_EQ(m->RawData.Count, 2);
    }

    const std::vector<std::int8_t> expectedA = {1, 2};
    const std::vector<std::int16_t> expectedB = {100, 200};
    const std::vector<float> expectedC = {1.5f, 2.5f};
    EXPECT_EQ(SamplesOf<std::int8_t>(metadata[0]), expectedA);
    EXPECT_EQ(SamplesOf<std::int16_t>(metadata[1]), expectedB);
    EXPECT_EQ(SamplesOf<float>(metadata[2]), expectedC);
}

TEST(ReaderInterleaved, ASingleInterleavedChannelBehavesLikeContiguous) {
    Builder meta;
    meta.U32(1);
    meta.ObjectWithRaw("/'G'/'A'", kTypeI32, 3, 0);
    Builder raw;
    raw.I32(7).I32(8).I32(9);
    Builder file;
    file.LeadIn(kTocMetaData | kTocRawData | kTocInterleaved, static_cast<std::int64_t>(meta.size() + raw.size()),
                static_cast<std::int64_t>(meta.size()));
    file.Raw(meta.bytes()).Raw(raw.bytes());

    const Bytes bytes = file.bytes();
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, bytes.size());
    auto metadata = reader.ReadMetadata(reader.ReadFirstSegment());
    ASSERT_EQ(metadata.size(), 1u);

    const std::vector<std::int32_t> expected = {7, 8, 9};
    EXPECT_EQ(SamplesOf<std::int32_t>(metadata[0]), expected);
}

TEST(ReaderInterleaved, AnEmptyRawRegionYieldsZeroSamples) {
    Builder meta;
    meta.U32(2);
    meta.ObjectWithRaw("/'G'/'A'", kTypeI32, 0, 0);
    meta.ObjectWithRaw("/'G'/'B'", kTypeI32, 0, 0);
    Builder file;
    file.LeadIn(kTocMetaData | kTocRawData | kTocInterleaved, static_cast<std::int64_t>(meta.size()),
                static_cast<std::int64_t>(meta.size()));
    file.Raw(meta.bytes());

    const Bytes bytes = file.bytes();
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, bytes.size());
    auto metadata = reader.ReadMetadata(reader.ReadFirstSegment());
    ASSERT_EQ(metadata.size(), 2u);
    for (const auto& m : metadata) {
        EXPECT_EQ(m->RawData.Count, 0);
    }
}

TEST(ReaderInterleaved, ATruncatedInterleavedBlockDoesNotReadPastTheEnd) {
    Bytes bytes = golden::TwoChannelsInterleaved();
    bytes.resize(bytes.size() - 6);   // cut into the last stride
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, bytes.size());
    auto segment = reader.ReadFirstSegment();
    ASSERT_NE(segment, nullptr);
    EXPECT_NO_THROW(reader.ReadMetadata(segment));
}
