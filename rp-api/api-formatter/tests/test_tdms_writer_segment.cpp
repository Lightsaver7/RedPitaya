/**
 * Unit tests for src/tdms/tdms_writer.{h,cpp} - TDMS segment construction and
 * the on-disk byte layout.
 *
 * The file format is pinned down two ways here. GoldenSegmentBytes asserts a
 * complete hand-checked segment byte for byte, which is what makes a change to
 * the writer reviewable at all; the remaining tests assert individual fields
 * so that a failure says which field moved rather than just "the bytes
 * differ".
 *
 * Channel *values* as a reader sees them stay the job of verify_tdms.py, which
 * uses the third-party npTDMS library - an independent parser cannot share a
 * misunderstanding of the format with the code under test.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "tdms_types.h"
#include "tdms_writer.h"

using namespace rp_formatter_api::tdms;

namespace {

constexpr std::size_t kLeadInSize = 28;

struct LeadIn {
    std::string tag;
    std::int32_t toc;
    std::int32_t version;
    std::uint64_t nextSegmentOffset;
    std::uint64_t rawDataOffset;
};

LeadIn ParseLeadIn(const std::string& bytes, std::size_t at = 0) {
    LeadIn leadIn;
    leadIn.tag = bytes.substr(at, 4);
    std::memcpy(&leadIn.toc, bytes.data() + at + 4, sizeof(leadIn.toc));
    std::memcpy(&leadIn.version, bytes.data() + at + 8, sizeof(leadIn.version));
    std::memcpy(&leadIn.nextSegmentOffset, bytes.data() + at + 12, sizeof(leadIn.nextSegmentOffset));
    std::memcpy(&leadIn.rawDataOffset, bytes.data() + at + 20, sizeof(leadIn.rawDataOffset));
    return leadIn;
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + at, sizeof(value));
    return value;
}

std::uint64_t ReadU64(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    std::uint64_t value = 0;
    std::memcpy(&value, bytes.data() + at, sizeof(value));
    return value;
}

std::string Written(const Segment& segment) {
    std::stringstream stream;
    WriteSegment(stream, segment);
    return stream.str();
}

// The same shape CTDMSWriter builds: one group carrying properties, one float
// channel.
struct SampleSegment {
    Segment segment;
    std::vector<float> samples = {1.f, 2.f, 3.f, 4.f};

    explicit SampleSegment(const std::string& channelName = "CH1") {
        const auto group = segment.AddGroup("Group");
        segment.SetProperty(group, "osc_rate", Value::UInt64(125000000));
        segment.AddChannel("Group", channelName, view());
    }

    auto view() const -> RawView {
        return RawView{Type::Float, reinterpret_cast<const std::uint8_t*>(samples.data()), samples.size()};
    }
};

std::string HexDump(const std::string& bytes) {
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 0x0F]);
    }
    return out;
}

}  // namespace

// --- Object paths ----------------------------------------------------------

TEST(TdmsPaths, GroupAndChannelPathsAreSingleQuoteDelimited) {
    EXPECT_EQ(GroupPath("Group"), "/'Group'");
    EXPECT_EQ(ChannelPath("Group", "CH1"), "/'Group'/'CH1'");
    EXPECT_EQ(ChannelPath("Group", "Voltage, V"), "/'Group'/'Voltage, V'");
}

// --- Segment construction --------------------------------------------------

TEST(TdmsSegment, AddGroupTwiceReturnsTheSameObject) {
    Segment segment;
    const auto first = segment.AddGroup("Group");
    const auto second = segment.AddGroup("Group");

    EXPECT_EQ(first, second);
    ASSERT_EQ(segment.objects().size(), 1u);
    EXPECT_EQ(segment.objects()[0].path, "/'Group'");
}

TEST(TdmsSegment, AddChannelDeclaresTheGroupBeforeTheChannel) {
    std::vector<float> samples = {1.f};
    Segment segment;
    segment.AddChannel("Group", "CH1", RawView{Type::Float, reinterpret_cast<const std::uint8_t*>(samples.data()), 1});

    ASSERT_EQ(segment.objects().size(), 2u);
    EXPECT_EQ(segment.objects()[0].path, "/'Group'");
    EXPECT_EQ(segment.objects()[1].path, "/'Group'/'CH1'");
}

TEST(TdmsSegment, GroupsHaveNoSamplesAndChannelsDo) {
    SampleSegment sample;
    ASSERT_EQ(sample.segment.objects().size(), 2u);
    EXPECT_FALSE(sample.segment.objects()[0].samples.has_value());
    ASSERT_TRUE(sample.segment.objects()[1].samples.has_value());
    EXPECT_EQ(sample.segment.objects()[1].samples->type, Type::Float);
    EXPECT_EQ(sample.segment.objects()[1].samples->sampleCount, 4u);
}

TEST(TdmsSegment, SegmentDoesNotCopyTheSampleBuffer) {
    SampleSegment sample;
    ASSERT_TRUE(sample.segment.objects()[1].samples.has_value());
    EXPECT_EQ(sample.segment.objects()[1].samples->data, reinterpret_cast<const std::uint8_t*>(sample.samples.data()));
}

TEST(TdmsSegment, PropertiesAreStoredUnderTheirKey) {
    Segment segment;
    const auto group = segment.AddGroup("Group");
    segment.SetProperty(group, "osc_rate", Value::UInt64(125000000));

    const auto& properties = segment.objects()[0].properties;
    ASSERT_EQ(properties.count("osc_rate"), 1u);
    EXPECT_EQ(properties.at("osc_rate").type(), Type::UInt64);
    EXPECT_EQ(properties.at("osc_rate").payload(), Value::UInt64(125000000).payload());
}

TEST(TdmsSegment, SetPropertyOverwritesAnExistingKey) {
    Segment segment;
    const auto group = segment.AddGroup("Group");
    segment.SetProperty(group, "osc_rate", Value::UInt64(1));
    segment.SetProperty(group, "osc_rate", Value::UInt64(2));

    ASSERT_EQ(segment.objects()[0].properties.size(), 1u);
    EXPECT_EQ(segment.objects()[0].properties.at("osc_rate").payload(), Value::UInt64(2).payload());
}

TEST(TdmsSegment, StringRawDataIsRejected) {
    Segment segment;
    const std::uint8_t bytes[] = {'a', 'b'};
    EXPECT_THROW(segment.AddChannel("Group", "CH1", RawView{Type::String, bytes, 2}), std::invalid_argument);
}

TEST(TdmsSegment, RawDataByteCountSumsEveryChannel) {
    std::vector<float> f32 = {1.f, 2.f};
    std::vector<double> d64 = {1.0, 2.0, 3.0};
    Segment segment;
    EXPECT_EQ(segment.rawDataByteCount(), 0u);
    EXPECT_FALSE(segment.hasRawData());

    segment.AddChannel("Group", "CH1", RawView{Type::Float, reinterpret_cast<const std::uint8_t*>(f32.data()), 2});
    segment.AddChannel("Group", "CH2", RawView{Type::Double, reinterpret_cast<const std::uint8_t*>(d64.data()), 3});

    EXPECT_EQ(segment.rawDataByteCount(), 2u * 4u + 3u * 8u);
    EXPECT_TRUE(segment.hasRawData());
}

// --- Metadata encoding -----------------------------------------------------

TEST(TdmsMetadata, ObjectCountCoversEveryDeclaredObject) {
    SampleSegment sample;
    const auto metadata = EncodeMetadata(sample.segment);
    ASSERT_GE(metadata.size(), 4u);
    EXPECT_EQ(ReadU32(metadata, 0), 2u);
}

TEST(TdmsMetadata, ObjectPathsAppearInTheMetadataSection) {
    SampleSegment sample("Voltage");
    const auto metadata = EncodeMetadata(sample.segment);
    const std::string text(metadata.begin(), metadata.end());

    EXPECT_NE(text.find("/'Group'"), std::string::npos);
    EXPECT_NE(text.find("/'Group'/'Voltage'"), std::string::npos);
}

TEST(TdmsMetadata, AnObjectWithoutSamplesGetsTheNoRawDataIndex) {
    Segment segment;
    segment.AddGroup("Group");
    const auto metadata = EncodeMetadata(segment);

    // objectCount(4) + pathLength(4) + path(8) -> raw data index
    const std::size_t rawIndexAt = 4 + 4 + 8;
    ASSERT_GE(metadata.size(), rawIndexAt + 4);
    EXPECT_EQ(ReadU32(metadata, rawIndexAt), 0xFFFFFFFFu);
}

TEST(TdmsMetadata, AChannelGetsAFullRawDataIndex) {
    std::vector<float> samples = {1.f, 2.f, 3.f, 4.f};
    Segment segment;
    segment.AddChannel("Group", "CH1", RawView{Type::Float, reinterpret_cast<const std::uint8_t*>(samples.data()), 4});
    const auto metadata = EncodeMetadata(segment);

    // Skip objectCount, the group record, and the channel path.
    const std::size_t groupRecord = 4 + 8 /*path*/ + 4 /*index*/ + 4 /*propertyCount*/;
    const std::size_t rawIndexAt = 4 + groupRecord + 4 + std::string("/'Group'/'CH1'").size();
    ASSERT_GE(metadata.size(), rawIndexAt + 20);

    EXPECT_EQ(ReadU32(metadata, rawIndexAt), 20u);                                              // index length
    EXPECT_EQ(ReadU32(metadata, rawIndexAt + 4), static_cast<std::uint32_t>(Type::Float));      // data type
    EXPECT_EQ(ReadU32(metadata, rawIndexAt + 8), 1u);                                           // dimension
    EXPECT_EQ(ReadU64(metadata, rawIndexAt + 12), 4u);                                          // value count
}

TEST(TdmsMetadata, ZeroSampleChannelStillGetsAFullRawDataIndex) {
    // A channel that happens to carry no samples this segment must keep its
    // index with a count of 0; collapsing it to 0xFFFFFFFF would change the
    // meaning from "empty this time" to "no data here at all".
    Segment segment;
    segment.AddChannel("Group", "CH1", RawView{Type::Float, nullptr, 0});
    const auto metadata = EncodeMetadata(segment);

    const std::size_t groupRecord = 4 + 8 + 4 + 4;
    const std::size_t rawIndexAt = 4 + groupRecord + 4 + std::string("/'Group'/'CH1'").size();
    ASSERT_GE(metadata.size(), rawIndexAt + 20);
    EXPECT_EQ(ReadU32(metadata, rawIndexAt), 20u);
    EXPECT_EQ(ReadU64(metadata, rawIndexAt + 12), 0u);
    EXPECT_TRUE(segment.hasRawData());
    EXPECT_EQ(segment.rawDataByteCount(), 0u);
}

TEST(TdmsMetadata, SampleCountAboveTwoGigabytesIsWrittenAsAFull64BitField) {
    // The old writer routed the count through `long`, 32-bit on the ARM target.
    // No buffer is allocated here - only the metadata section is encoded.
    Segment segment;
    segment.AddChannel("Group", "CH1", RawView{Type::Int8, nullptr, 0x1'0000'0000ull});
    const auto metadata = EncodeMetadata(segment);

    const std::size_t groupRecord = 4 + 8 + 4 + 4;
    const std::size_t rawIndexAt = 4 + groupRecord + 4 + std::string("/'Group'/'CH1'").size();
    ASSERT_GE(metadata.size(), rawIndexAt + 20);
    EXPECT_EQ(ReadU64(metadata, rawIndexAt + 12), 0x1'0000'0000ull);
}

TEST(TdmsMetadata, PropertiesAreWrittenInAscendingKeyOrder) {
    Segment segment;
    const auto group = segment.AddGroup("G");
    segment.SetProperty(group, "zulu", Value::UInt8(1));
    segment.SetProperty(group, "alpha", Value::UInt8(2));
    segment.SetProperty(group, "mike", Value::UInt8(3));

    const auto metadata = EncodeMetadata(segment);
    const std::string text(metadata.begin(), metadata.end());

    const auto alpha = text.find("alpha");
    const auto mike = text.find("mike");
    const auto zulu = text.find("zulu");
    ASSERT_NE(alpha, std::string::npos);
    ASSERT_NE(mike, std::string::npos);
    ASSERT_NE(zulu, std::string::npos);
    EXPECT_LT(alpha, mike);
    EXPECT_LT(mike, zulu);
}

TEST(TdmsMetadata, PropertyRecordIsKeyThenTypeCodeThenPayload) {
    Segment segment;
    const auto group = segment.AddGroup("G");
    segment.SetProperty(group, "k", Value::UInt32(0x12345678u));

    const auto metadata = EncodeMetadata(segment);
    // objectCount(4) + pathLength(4) + "/'G'"(4) + index(4) + propertyCount(4)
    std::size_t at = 4 + 4 + 4 + 4 + 4;
    EXPECT_EQ(ReadU32(metadata, at), 1u);  // key length
    at += 4;
    EXPECT_EQ(metadata[at], 'k');
    at += 1;
    EXPECT_EQ(ReadU32(metadata, at), static_cast<std::uint32_t>(Type::UInt32));
    at += 4;
    EXPECT_EQ(ReadU32(metadata, at), 0x12345678u);
}

// --- Segment serialisation -------------------------------------------------

TEST(TdmsWriteSegment, LeadInCarriesTheTdsmTagAndTdms2Version) {
    SampleSegment sample;
    const std::string bytes = Written(sample.segment);

    ASSERT_GE(bytes.size(), kLeadInSize);
    const auto leadIn = ParseLeadIn(bytes);
    EXPECT_EQ(leadIn.tag, "TDSm");
    EXPECT_EQ(leadIn.version, 4713);
}

TEST(TdmsWriteSegment, TableOfContentsIsDerivedFromTheSegmentContents) {
    Segment metadataOnly;
    metadataOnly.AddGroup("Group");
    EXPECT_EQ(ParseLeadIn(Written(metadataOnly)).toc, 0x02);

    SampleSegment withSamples;
    EXPECT_EQ(ParseLeadIn(Written(withSamples.segment)).toc, 0x0A);
}

TEST(TdmsWriteSegment, InterleavedBigEndianAndDaqMxBitsAreNeverSet) {
    SampleSegment sample;
    const auto leadIn = ParseLeadIn(Written(sample.segment));

    EXPECT_EQ(leadIn.toc & (1 << 2), 0) << "new object list";
    EXPECT_EQ(leadIn.toc & (1 << 5), 0) << "interleaved";
    EXPECT_EQ(leadIn.toc & (1 << 6), 0) << "big endian";
    EXPECT_EQ(leadIn.toc & (1 << 7), 0) << "DAQmx";
}

TEST(TdmsWriteSegment, OffsetsAreFinalOnTheFirstWriteAndNeverASentinel) {
    SampleSegment sample;
    const std::string bytes = Written(sample.segment);
    const auto leadIn = ParseLeadIn(bytes);
    const std::uint64_t rawBytes = sample.samples.size() * sizeof(float);

    EXPECT_EQ(leadIn.nextSegmentOffset, bytes.size() - kLeadInSize);
    EXPECT_EQ(leadIn.rawDataOffset, leadIn.nextSegmentOffset - rawBytes);
    EXPECT_EQ(leadIn.rawDataOffset, EncodeMetadata(sample.segment).size());
}

TEST(TdmsWriteSegment, MetadataOnlySegmentStillGetsAValidRawDataOffset) {
    Segment segment;
    const std::string bytes = Written(segment);
    const auto leadIn = ParseLeadIn(bytes);

    // Only the object count is written, so the metadata section is 4 bytes and
    // the (empty) raw data block starts right after it.
    EXPECT_EQ(bytes.size(), kLeadInSize + 4);
    EXPECT_EQ(leadIn.nextSegmentOffset, 4u);
    EXPECT_EQ(leadIn.rawDataOffset, 4u);
    EXPECT_EQ(leadIn.toc, 0x02);
}

TEST(TdmsWriteSegment, RawDataBlockIsTheChannelSamplesVerbatim) {
    SampleSegment sample;
    const std::string bytes = Written(sample.segment);
    const auto leadIn = ParseLeadIn(bytes);

    std::vector<float> written(sample.samples.size());
    ASSERT_EQ(bytes.size(), kLeadInSize + leadIn.nextSegmentOffset);
    std::memcpy(written.data(), bytes.data() + kLeadInSize + leadIn.rawDataOffset, written.size() * sizeof(float));
    EXPECT_EQ(written, sample.samples);
}

TEST(TdmsWriteSegment, ObjectOrderInTheRawDataBlockMatchesTheMetadataOrder) {
    // A reader pairs metadata objects with raw data by position, so the two
    // orders must agree. Two channels of different widths make a swap visible.
    std::vector<std::uint8_t> ch1 = {0x11, 0x22};
    std::vector<std::uint32_t> ch2 = {0xAABBCCDDu};
    Segment segment;
    segment.AddChannel("Group", "CH1", RawView{Type::Int8, ch1.data(), ch1.size()});
    segment.AddChannel("Group", "CH2", RawView{Type::UInt32, reinterpret_cast<const std::uint8_t*>(ch2.data()), ch2.size()});

    const std::string bytes = Written(segment);
    const auto leadIn = ParseLeadIn(bytes);
    const std::string raw = bytes.substr(kLeadInSize + leadIn.rawDataOffset);

    ASSERT_EQ(raw.size(), 2u + 4u);
    EXPECT_EQ(raw, std::string("\x11\x22\xDD\xCC\xBB\xAA", 6));
}

TEST(TdmsWriteSegment, SecondWriteAppendsAWholeNewSegment) {
    SampleSegment sample;
    std::stringstream stream;
    WriteSegment(stream, sample.segment);
    const std::size_t sizeAfterFirst = stream.str().size();
    WriteSegment(stream, sample.segment);

    const std::string bytes = stream.str();
    ASSERT_EQ(bytes.size(), sizeAfterFirst * 2);
    EXPECT_EQ(ParseLeadIn(bytes, 0).tag, "TDSm");
    EXPECT_EQ(ParseLeadIn(bytes, sizeAfterFirst).tag, "TDSm");
    EXPECT_EQ(ParseLeadIn(bytes, sizeAfterFirst).nextSegmentOffset, sizeAfterFirst - kLeadInSize);
}

TEST(TdmsWriteSegment, WritingAppendsAtTheEndOfAStreamThatAlreadyHasContent) {
    SampleSegment sample;
    std::stringstream stream;
    stream << "PREFIX";
    WriteSegment(stream, sample.segment);

    const std::string bytes = stream.str();
    EXPECT_EQ(bytes.substr(0, 6), "PREFIX");
    EXPECT_EQ(bytes.substr(6, 4), "TDSm");
}

// --- The golden segment ----------------------------------------------------

TEST(TdmsWriteSegment, GoldenSegmentBytes) {
    // A complete, hand-checked segment. Short names are used so the whole file
    // fits in one readable literal. If anything at all about the layout moves,
    // this is the test that says so.
    //
    //   lead-in  "TDSm" | ToC 0x0A | version 4713 | next 89 | rawData 73
    //   metadata objectCount 2
    //            "/'G'"     index 0xFFFFFFFF, 1 property "r" = UInt64
    //            "/'G'/'C'" index 20, Float, dimension 1, 4 values, 0 properties
    //   raw      1.0f 2.0f 3.0f 4.0f
    //
    //   metadata length = 4 + 16 (group) + 17 (property) + 36 (channel) = 73
    //   raw data length = 4 * sizeof(float)                              = 16
    std::vector<float> samples = {1.f, 2.f, 3.f, 4.f};
    Segment segment;
    const auto group = segment.AddGroup("G");
    segment.SetProperty(group, "r", Value::UInt64(0x0102030405060708ull));
    segment.AddChannel("G", "C", RawView{Type::Float, reinterpret_cast<const std::uint8_t*>(samples.data()), samples.size()});

    const std::string golden =
        // -- lead-in, 28 bytes
        "5444536D"              // "TDSm"
        "0A000000"              // ToC: has metadata (1<<1) | has raw data (1<<3)
        "69120000"              // version 4713
        "5900000000000000"      // next segment offset  = 73 + 16
        "4900000000000000"      // raw data offset      = 73
        // -- metadata section
        "02000000"              // object count
        "04000000" "2F274727"   // path length 4, "/'G'"
        "FFFFFFFF"              // raw data index: none
        "01000000"              // property count
        "01000000" "72"         // key length 1, "r"
        "08000000"              // property type: UInt64
        "0807060504030201"      // 0x0102030405060708, little-endian
        "08000000" "2F2747272F274327"  // path length 8, "/'G'/'C'"
        "14000000"              // raw data index length 20
        "09000000"              // data type: Float
        "01000000"              // array dimension
        "0400000000000000"      // number of values
        "00000000"              // property count
        // -- raw data
        "0000803F"              // 1.0f
        "00000040"              // 2.0f
        "00004040"              // 3.0f
        "00008040";             // 4.0f

    EXPECT_EQ(HexDump(Written(segment)), golden);
}

TEST(TdmsWriteSegment, GoldenSegmentLengthsAgreeWithTheLeadIn) {
    // Cross-check of the golden fixture: the two lead-in offsets must match
    // what the metadata encoder and the sample buffer actually produce.
    std::vector<float> samples = {1.f, 2.f, 3.f, 4.f};
    Segment segment;
    const auto group = segment.AddGroup("G");
    segment.SetProperty(group, "r", Value::UInt64(0x0102030405060708ull));
    segment.AddChannel("G", "C", RawView{Type::Float, reinterpret_cast<const std::uint8_t*>(samples.data()), samples.size()});

    EXPECT_EQ(EncodeMetadata(segment).size(), 73u);
    EXPECT_EQ(segment.rawDataByteCount(), 16u);
    EXPECT_EQ(Written(segment).size(), kLeadInSize + 73u + 16u);
}
