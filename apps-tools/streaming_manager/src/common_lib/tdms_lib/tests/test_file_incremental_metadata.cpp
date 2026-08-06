/**
 * Incremental (implicit) metadata - a raw region holding more than one chunk of
 * samples under a single metadata section, per NI's white paper.
 *
 * File::GetMetadataItem synthesises one Metadata record per extra chunk. That
 * code created its record as a default-constructed (null) shared_ptr and
 * dereferenced it immediately, so reaching this branch was a guaranteed
 * segfault; two nearby loops also had no progress guarantee and could spin
 * forever.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "byte_utils.h"
#include "file.h"
#include "golden_bytes.h"
#include "spec_builder.h"

using namespace tdms_test;

namespace {

auto ReadAll(const Bytes& bytes) -> std::vector<std::shared_ptr<TDMS::Metadata>> {
    TempFile file("incremental.tdms");
    file.write(bytes);
    TDMS::File reader;
    return reader.ReadFile(file.path());
}

}  // namespace

TEST(IncrementalMetadata, TwoChunksUnderOneMetadataSectionDoNotCrash) {
    // The headline regression: this input used to segfault.
    auto metadata = ReadAll(golden::IncrementalMetadata());
    ASSERT_GE(metadata.size(), 2u) << "one declared record plus one implicit record";

    for (const auto& m : metadata) {
        ASSERT_NE(m, nullptr) << "no null record may be handed out";
    }
}

TEST(IncrementalMetadata, ImplicitRecordsCarryTheFieldsTheyUsedToLose) {
    auto metadata = ReadAll(golden::IncrementalMetadata());
    ASSERT_GE(metadata.size(), 2u);

    const auto& declared = metadata[0];
    const auto& implicit = metadata[1];

    EXPECT_EQ(implicit->Path, declared->Path);
    EXPECT_EQ(implicit->RawData.Count, declared->RawData.Count);
    EXPECT_EQ(implicit->RawData.Size, declared->RawData.Size);
    EXPECT_EQ(implicit->RawData.Dimension, declared->RawData.Dimension);
    EXPECT_EQ(implicit->RawData.DataType.GetDataType(), declared->RawData.DataType.GetDataType());
    EXPECT_EQ(implicit->RawData.IsInterleaved, declared->RawData.IsInterleaved);
    // These four were dropped entirely by the original implementation, which
    // made every implicit record print with an empty path.
    EXPECT_EQ(implicit->PathStr, declared->PathStr);
    EXPECT_EQ(implicit->Version, declared->Version);
    EXPECT_EQ(implicit->TableOfContents.HasRawData, declared->TableOfContents.HasRawData);
    EXPECT_EQ(implicit->RawData.InterleaveStride, declared->RawData.InterleaveStride);
    // The second chunk starts one chunk further into the raw region.
    EXPECT_EQ(implicit->RawData.Offset, declared->RawData.Offset + declared->RawData.Size);
}

TEST(IncrementalMetadata, AZeroObjectCountWithRawDataDoesNotHang) {
    // With no objects the "all objects have raw data" predicate is vacuously
    // true, so the chunk loop ran with nothing to advance its offset.
    Builder meta;
    meta.U32(0);
    Builder raw;
    for (int i = 0; i < 8; i++) {
        raw.F32(static_cast<float>(i));
    }
    Builder file;
    file.LeadIn(kTocMetaData | kTocRawData, static_cast<std::int64_t>(meta.size() + raw.size()),
                static_cast<std::int64_t>(meta.size()));
    file.Raw(meta.bytes()).Raw(raw.bytes());

    EXPECT_NO_THROW(ReadAll(file.bytes()));
}

TEST(IncrementalMetadata, ZeroSizedChunksDoNotHang) {
    // Every channel declares zero samples, so a chunk contributes no bytes.
    Builder meta;
    meta.U32(1);
    meta.ObjectWithRaw("/'G'/'A'", kTypeF32, 0, 0);
    Builder raw;
    for (int i = 0; i < 8; i++) {
        raw.F32(static_cast<float>(i));
    }
    Builder file;
    file.LeadIn(kTocMetaData | kTocRawData, static_cast<std::int64_t>(meta.size() + raw.size()),
                static_cast<std::int64_t>(meta.size()));
    file.Raw(meta.bytes()).Raw(raw.bytes());

    EXPECT_NO_THROW(ReadAll(file.bytes()));
}

TEST(IncrementalMetadata, AnAbsurdRawExtentProducesABoundedNumberOfRecords) {
    // The lead-in claims a raw region far larger than the file. The number of
    // synthesised records must be bounded by what is actually there.
    Bytes bytes = golden::IncrementalMetadata();
    const std::int64_t huge = 1LL << 40;
    std::memcpy(bytes.data() + 12, &huge, sizeof(huge));

    std::vector<std::shared_ptr<TDMS::Metadata>> metadata;
    EXPECT_NO_THROW(metadata = ReadAll(bytes));
    EXPECT_LT(metadata.size(), 10000u) << "implicit records must be bounded by the real file size";
}

TEST(IncrementalMetadata, AGroupObjectSuppressesImplicitRecords) {
    // Documented current behaviour, deliberately preserved: the predicate that
    // decides whether to synthesise implicit records looks at every object,
    // including the group (which never has raw data), so an ordinary
    // group+channel segment produces no implicit records even when its raw
    // region holds several chunks. Correcting this to consider only channel
    // objects is spec-correct but changes what ReadFile returns for ordinary
    // files, so it is a separate, explicitly-approved change.
    Builder meta;
    meta.U32(2);
    meta.ObjectNoRaw("/'G'", 0);
    meta.ObjectWithRaw("/'G'/'A'", kTypeF32, 4, 0);
    Builder raw;
    for (int i = 0; i < 8; i++) {
        raw.F32(static_cast<float>(i));
    }
    Builder file;
    file.LeadIn(kTocMetaData | kTocRawData, static_cast<std::int64_t>(meta.size() + raw.size()),
                static_cast<std::int64_t>(meta.size()));
    file.Raw(meta.bytes()).Raw(raw.bytes());

    auto metadata = ReadAll(file.bytes());
    EXPECT_EQ(metadata.size(), 2u) << "group + channel, no implicit records";
}
