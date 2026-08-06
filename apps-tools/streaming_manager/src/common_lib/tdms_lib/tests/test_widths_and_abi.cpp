/**
 * Field widths that must not depend on the host's pointer size.
 *
 * The RedPitaya target is 32-bit ARM, where `long` is 4 bytes. Every field
 * below carries a 64-bit TDMS file-format quantity - a sample count, a byte
 * size or a file offset - and used to be declared `long`, so it truncated
 * silently on the target while looking correct on an x86-64 developer machine.
 * The static_asserts hold on every target, including the one that matters.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <sstream>
#include <type_traits>

#include "byte_utils.h"
#include "file_struct_types.h"
#include "reader.h"
#include "spec_builder.h"

using namespace tdms_test;

static_assert(sizeof(TDMS::Segment::Offset) == 8, "segment file offset is a 64-bit quantity");
static_assert(sizeof(TDMS::Segment::MetadataOffset) == 8);
static_assert(sizeof(TDMS::Segment::RawDataOffset) == 8, "lead-in raw data offset is int64 on disk");
static_assert(sizeof(TDMS::Segment::NextSegmentOffset) == 8, "lead-in next segment offset is int64 on disk");
static_assert(sizeof(TDMS::RawData::Offset) == 8);
static_assert(sizeof(TDMS::RawData::Count) == 8, "raw data index value count is uint64 on disk");
static_assert(sizeof(TDMS::RawData::Size) == 8);
static_assert(sizeof(TDMS::RawData::InterleaveStride) == 8);

TEST(Widths, SegmentLeadInLengthIsTwentyEight) {
    TDMS::Segment segment;
    EXPECT_EQ(segment.Length, 28);
}

TEST(Widths, TableOfContentsFlagsDefaultToFalse) {
    // The six flags had no default member initialisers, so a Metadata created
    // anywhere other than GenerateRoot carried indeterminate values.
    TDMS::TableOfContents toc;
    EXPECT_FALSE(toc.HasMetaData);
    EXPECT_FALSE(toc.HasRawData);
    EXPECT_FALSE(toc.HasDaqMxData);
    EXPECT_FALSE(toc.RawDataIsInterleaved);
    EXPECT_FALSE(toc.NumbersAreBigEndian);
    EXPECT_FALSE(toc.ContainsNewObjects);

    TDMS::Metadata metadata;
    EXPECT_FALSE(metadata.TableOfContents.HasMetaData);
    EXPECT_FALSE(metadata.TableOfContents.HasRawData);
    EXPECT_FALSE(metadata.TableOfContents.HasDaqMxData);
    EXPECT_FALSE(metadata.TableOfContents.RawDataIsInterleaved);
    EXPECT_FALSE(metadata.TableOfContents.NumbersAreBigEndian);
    EXPECT_FALSE(metadata.TableOfContents.ContainsNewObjects);
}

TEST(Widths, ASegmentOffsetAboveFourGigabytesIsNotTruncated) {
    // Reader takes the file size as a constructor argument, so a 64-bit offset
    // can be exercised without producing a 4 GiB file: build a lead-in whose
    // next-segment offset is past 2^32 and tell the Reader the file is large.
    const std::int64_t nextRelative = 0x1'0000'0004LL;
    Builder file;
    file.LeadIn(kTocMetaData, nextRelative, 0);
    file.U32(0);   // an empty metadata section, never reached

    const Bytes bytes = file.bytes();
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, 0x2'0000'0000ULL);

    auto segment = reader.ReadFirstSegment();
    ASSERT_NE(segment, nullptr);
    EXPECT_EQ(segment->NextSegmentOffset, nextRelative + static_cast<std::int64_t>(kLeadInSize))
        << "the offset must survive at full 64-bit width";
}

TEST(Widths, ARawDataCountAboveFourGigabytesIsNotTruncated) {
    Builder meta;
    meta.U32(1);
    meta.PString("/'G'/'A'");
    meta.U32(kFixedRawDataIndexLength);
    meta.U32(kTypeI8);
    meta.U32(1);
    meta.U64(0x1'0000'0004ULL);   // more samples than fit in 32 bits
    meta.U32(0);

    Builder file;
    file.LeadIn(kTocMetaData, static_cast<std::int64_t>(meta.size()), 0);
    file.Raw(meta.bytes());

    const Bytes bytes = file.bytes();
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, bytes.size());

    auto segment = reader.ReadFirstSegment();
    ASSERT_NE(segment, nullptr);
    auto metadata = reader.ReadMetadata(segment);
    if (metadata.empty()) {
        // Rejecting a count that cannot possibly be backed by this file is also
        // an acceptable answer; what must not happen is a silent truncation.
        SUCCEED() << "count rejected as implausible for the file size";
        return;
    }
    EXPECT_EQ(metadata[0]->RawData.Count, 0x1'0000'0004LL) << "no 32-bit truncation of the sample count";
}
