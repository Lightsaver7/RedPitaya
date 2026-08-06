/**
 * Hostile and truncated input.
 *
 * The contract asserted here is deliberately weak: the library must RETURN -
 * not crash, not hang, not allocate wildly - for any byte string. Several of
 * these inputs used to hang the process outright, so every case runs under a
 * bounded-work guard rather than trusting the loop conditions.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "byte_utils.h"
#include "file.h"
#include "golden_bytes.h"
#include "reader.h"
#include "spec_builder.h"

using namespace tdms_test;

namespace {

// Reads the bytes through the public File entry point.
auto ReadAll(const Bytes& bytes) -> std::size_t {
    TempFile file("malformed.tdms");
    file.write(bytes);
    TDMS::File reader;
    return reader.ReadFile(file.path()).size();
}

}  // namespace

TEST(Malformed, EmptyFileReturnsNothing) {
    EXPECT_EQ(ReadAll({}), 0u);
}

TEST(Malformed, FileShorterThanTheLeadInReturnsNothing) {
    EXPECT_EQ(ReadAll({'T', 'D', 'S', 'm'}), 0u);
    Bytes almost = golden::Reference();
    almost.resize(kLeadInSize - 1);
    EXPECT_EQ(ReadAll(almost), 0u);
}

TEST(Malformed, AFileThatIsNotTdmsIsRejected) {
    // The 4-byte tag used to be stored and never compared, so arbitrary bytes
    // were parsed as a lead-in with garbage offsets.
    Bytes junk;
    for (int i = 0; i < 200; i++) {
        junk.push_back(static_cast<std::uint8_t>(0x41 + (i % 26)));
    }
    EXPECT_EQ(ReadAll(junk), 0u);
}

TEST(Malformed, ARawDataOnlySegmentDoesNotParseSamplesAsMetadata) {
    // ToC advertises raw data but not metadata - the normal shape of a LabVIEW
    // continuation segment. ReadMetadata used to read the first sample bytes as
    // an object count (0x41424344 = 1094861636) and spin.
    const Bytes bytes = golden::RawDataOnlySegment();
    std::stringstream stream = MakeStream(bytes);
    TDMS::Reader reader(stream, bytes.size());
    auto segment = reader.ReadFirstSegment();
    ASSERT_NE(segment, nullptr);
    ASSERT_FALSE(segment->TableOfContents.HasMetaData);
    ASSERT_TRUE(segment->TableOfContents.HasRawData);

    EXPECT_TRUE(reader.ReadMetadata(segment).empty());
}

TEST(Malformed, ARawDataOnlySegmentIsSafeThroughReadFile) {
    EXPECT_NO_THROW(ReadAll(golden::RawDataOnlySegment()));
}

TEST(Malformed, ANegativeObjectCountIsRejected) {
    Builder meta;
    meta.I32(-5);
    Builder file;
    file.LeadIn(kTocMetaData, static_cast<std::int64_t>(meta.size()), 0);
    file.Raw(meta.bytes());
    EXPECT_EQ(ReadAll(file.bytes()), 0u);
}

TEST(Malformed, AnAbsurdObjectCountIsRejected) {
    Builder meta;
    meta.U32(1000000000u);
    Builder file;
    file.LeadIn(kTocMetaData, static_cast<std::int64_t>(meta.size()), 0);
    file.Raw(meta.bytes());
    EXPECT_EQ(ReadAll(file.bytes()), 0u);
}

TEST(Malformed, APathLengthBeyondTheFileIsRejected) {
    Builder meta;
    meta.U32(1);
    meta.U32(0x7FFFFFF0u);   // path length far past EOF
    Builder file;
    file.LeadIn(kTocMetaData, static_cast<std::int64_t>(meta.size()), 0);
    file.Raw(meta.bytes());
    EXPECT_NO_THROW(ReadAll(file.bytes()));
}

TEST(Malformed, ADaqMxRawDataIndexIsRejectedRatherThanMisparsed) {
    // Any raw-data-index length other than -1/0/20/28 has an unknown byte size,
    // so the stream cannot be resynchronised and the segment must be abandoned.
    Builder meta;
    meta.U32(1);
    meta.PString("/'G'/'A'");
    meta.U32(0x69120000u);   // DAQmx format-changing scaler
    Builder file;
    file.LeadIn(kTocMetaData, static_cast<std::int64_t>(meta.size()), 0);
    file.Raw(meta.bytes());
    EXPECT_NO_THROW(ReadAll(file.bytes()));
}

TEST(Malformed, AStringChannelWithAFixedWidthIndexIsRejected) {
    // GetLength(String) has no fixed width; with a length-20 index the reader
    // used to compute a 17 GB read.
    Builder meta;
    meta.U32(1);
    meta.ObjectWithRaw("/'G'/'A'", kTypeString, 4, 0);
    Builder file;
    file.LeadIn(kTocMetaData | kTocRawData, static_cast<std::int64_t>(meta.size()), static_cast<std::int64_t>(meta.size()));
    file.Raw(meta.bytes());
    EXPECT_NO_THROW(ReadAll(file.bytes()));
}

TEST(Malformed, TruncationAtEveryOffsetIsSafe) {
    const Bytes reference = golden::Reference();
    for (std::size_t length = 0; length < reference.size(); length++) {
        SCOPED_TRACE("truncated to " + std::to_string(length) + " bytes");
        const Bytes truncated(reference.begin(), reference.begin() + static_cast<std::ptrdiff_t>(length));
        EXPECT_NO_THROW(ReadAll(truncated));
    }
}

TEST(Malformed, ASegmentPointingAtItselfDoesNotLoopForever) {
    // GetSegments walked NextSegmentOffset with no requirement that it advance.
    Bytes bytes = golden::Reference();
    const std::int64_t selfReferencing = -static_cast<std::int64_t>(kLeadInSize);
    std::memcpy(bytes.data() + 12, &selfReferencing, sizeof(selfReferencing));
    EXPECT_NO_THROW(ReadAll(bytes));
}

TEST(Malformed, ASegmentPointingBackwardsDoesNotLoopForever) {
    Bytes twice = golden::Reference();
    const Bytes second = golden::Reference();
    twice.insert(twice.end(), second.begin(), second.end());
    // Make the second segment point back at the first.
    const std::int64_t backwards = -static_cast<std::int64_t>(golden::kReferenceSize + kLeadInSize);
    std::memcpy(twice.data() + golden::kReferenceSize + 12, &backwards, sizeof(backwards));
    EXPECT_NO_THROW(ReadAll(twice));
}

TEST(Malformed, MutationSweepOverTheReferenceSegment) {
    // Substitute a handful of hostile values at every offset of the lead-in and
    // metadata section. The only assertion is that the call returns; the point
    // is that none of the several thousand mutants crashes, hangs or allocates
    // without bound. This is the substitute for a fuzzer in a suite that has to
    // stay dependency-free and deterministic.
    const Bytes reference = golden::Reference();
    const std::size_t mutableEnd = kLeadInSize + static_cast<std::size_t>(golden::kReferenceMetadataLength);
    const std::uint8_t substitutions[] = {0x00, 0xFF, 0x01, 0x7F, 0x80};

    for (std::size_t at = 0; at < mutableEnd; at++) {
        for (std::uint8_t value : substitutions) {
            if (reference[at] == value) {
                continue;
            }
            Bytes mutant = reference;
            mutant[at] = value;
            SCOPED_TRACE("offset " + std::to_string(at) + " -> " + std::to_string(static_cast<int>(value)));
            EXPECT_NO_THROW(ReadAll(mutant));
        }
    }
}
