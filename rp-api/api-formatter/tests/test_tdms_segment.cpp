/**
 * Unit tests for rp_formatter_api::TDMS::WriterSegment / Writer / File
 * (src/tdms/writer.cpp, src/tdms/file.cpp).
 *
 * These sit one level below CTDMSWriter and cover the parts of the TDMS
 * container that npTDMS cannot report on precisely: the object-path
 * spelling, the 28-byte lead-in (tag, table-of-contents bitmask, version,
 * next-segment offset, raw-data offset) and the guard clauses that reject
 * the not-implemented cases (interleaved raw data, DAQmx data, string raw
 * data).
 *
 * Byte-level channel *values* are deliberately left to verify_tdms.py and
 * the third-party npTDMS reader; what is asserted here is only the framing
 * that the reader relies on to find those values at all.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "data_type.h"
#include "file.h"
#include "file_struct_types.h"
#include "test_support.h"
#include "writer.h"

using namespace rp_formatter_api::TDMS;

namespace {

constexpr size_t kLeadInSize = 28;
constexpr int32_t kTocHasMetaData = 1 << 1;
constexpr int32_t kTocContainsNewObjects = 1 << 2;
constexpr int32_t kTocHasRawData = 1 << 3;
constexpr int32_t kTocRawDataIsInterleaved = 1 << 5;
constexpr int32_t kTocNumbersAreBigEndian = 1 << 6;

struct LeadIn {
    std::string tag;
    int32_t toc;
    int32_t version;
    int64_t nextSegmentOffset;
    int64_t rawDataOffset;
};

LeadIn ParseLeadIn(const std::string& bytes, size_t at = 0) {
    LeadIn leadIn;
    leadIn.tag = bytes.substr(at, 4);
    std::memcpy(&leadIn.toc, bytes.data() + at + 4, sizeof(leadIn.toc));
    std::memcpy(&leadIn.version, bytes.data() + at + 8, sizeof(leadIn.version));
    std::memcpy(&leadIn.nextSegmentOffset, bytes.data() + at + 12, sizeof(leadIn.nextSegmentOffset));
    std::memcpy(&leadIn.rawDataOffset, bytes.data() + at + 20, sizeof(leadIn.rawDataOffset));
    return leadIn;
}

// Minimal but complete segment: root + one group + one float channel, i.e.
// the same shape CTDMSWriter::Impl::write() builds.
struct SampleSegment {
    WriterSegment segment;
    std::vector<std::shared_ptr<Metadata>> nodes;
    std::vector<float> samples = {1.f, 2.f, 3.f, 4.f};

    explicit SampleSegment(const std::string& channelName = "CH1") {
        auto root = segment.GenerateRoot();
        root->TableOfContents.HasMetaData = true;
        root->TableOfContents.HasRawData = true;
        nodes.push_back(root);

        auto group = segment.GenerateGroup("Group");
        nodes.push_back(group);

        auto channel = segment.GenerateChannel("Group", channelName);
        nodes.push_back(channel);
        segment.AddRaw(channel, TDMSType::SingleFloat, static_cast<int64_t>(samples.size()), reinterpret_cast<uint8_t*>(samples.data()));

        segment.LoadMetadata(nodes);
    }
};

}  // namespace

TEST(TdmsWriterSegment, RootUsesTdms2VersionAndAnEmptyPath) {
    WriterSegment segment;
    auto root = segment.GenerateRoot();

    EXPECT_EQ(root->Version, 4713);
    EXPECT_EQ(root->PathStr, "/");
    EXPECT_TRUE(root->Path.empty());
    EXPECT_FALSE(root->TableOfContents.HasMetaData);
    EXPECT_FALSE(root->TableOfContents.HasRawData);
    EXPECT_FALSE(root->TableOfContents.HasDaqMxData);
    EXPECT_FALSE(root->TableOfContents.RawDataIsInterleaved);
    EXPECT_FALSE(root->TableOfContents.NumbersAreBigEndian);
    EXPECT_FALSE(root->TableOfContents.ContainsNewObjects);
}

TEST(TdmsWriterSegment, GroupAndChannelPathsAreSingleQuoteDelimited) {
    WriterSegment segment;
    EXPECT_EQ(segment.GenerateGroup("Group")->PathStr, "/'Group'");
    EXPECT_EQ(segment.GenerateChannel("Group", "CH1")->PathStr, "/'Group'/'CH1'");
}

TEST(TdmsWriterSegment, ChannelPathRecordsGroupAndChannelSeparately) {
    WriterSegment segment;
    auto channel = segment.GenerateChannel("Group", "Voltage");
    ASSERT_EQ(channel->Path.size(), 2u);
    EXPECT_EQ(channel->Path[0], "Group");
    EXPECT_EQ(channel->Path[1], "Voltage");
}

TEST(TdmsWriterSegment, RootIsOnlyFoundOnceLoadMetadataHasSeenIt) {
    WriterSegment segment;
    EXPECT_FALSE(segment.IsRootNodePresent());
    EXPECT_EQ(segment.GetRoot(), nullptr);

    std::vector<std::shared_ptr<Metadata>> groupOnly = {segment.GenerateGroup("Group")};
    segment.LoadMetadata(groupOnly);
    EXPECT_FALSE(segment.IsRootNodePresent());
    EXPECT_EQ(segment.GetRoot(), nullptr);

    auto root = segment.GenerateRoot();
    std::vector<std::shared_ptr<Metadata>> withRoot = {root, segment.GenerateGroup("Group")};
    segment.LoadMetadata(withRoot);
    EXPECT_TRUE(segment.IsRootNodePresent());
    EXPECT_EQ(segment.GetRoot(), root);
    EXPECT_EQ(segment.GetNodes().size(), 2u);
}

TEST(TdmsWriterSegment, AddPropertiesStoresValuesUnderTheirKey) {
    WriterSegment segment;
    auto group = segment.GenerateGroup("Group");

    DataType rate;
    rate.InitDataType(TDMSType::UnsignedInteger64, DataType::MakeData<uint64_t>(125000000ull));
    segment.AddProperties(group, "osc_rate", rate);

    ASSERT_EQ(group->Properties.count("osc_rate"), 1u);
    EXPECT_EQ(group->Properties["osc_rate"].GetDataType(), TDMSType::UnsignedInteger64);
    EXPECT_EQ(group->Properties["osc_rate"].GetData<uint64_t>(), 125000000ull);
}

TEST(TdmsWriterSegment, AddPropertiesOverwritesAnExistingKey) {
    // Overwriting a key goes through DataType::operator=(const DataType&),
    // which sets m_rawData = nullptr before allocating the new buffer and so
    // leaks the previous one (8 bytes per overwrite here; visible under
    // -fsanitize=address). The observable behaviour asserted below is
    // correct, so this is noted rather than expressed as a failing test.
    WriterSegment segment;
    auto group = segment.GenerateGroup("Group");

    DataType first;
    first.InitDataType(TDMSType::UnsignedInteger64, DataType::MakeData<uint64_t>(1ull));
    DataType second;
    second.InitDataType(TDMSType::UnsignedInteger64, DataType::MakeData<uint64_t>(2ull));
    segment.AddProperties(group, "osc_rate", first);
    segment.AddProperties(group, "osc_rate", second);

    EXPECT_EQ(group->Properties.size(), 1u);
    EXPECT_EQ(group->Properties["osc_rate"].GetData<uint64_t>(), 2ull);
}

TEST(TdmsWriterSegment, AddRawFillsInCountDimensionAndByteSize) {
    WriterSegment segment;
    auto channel = segment.GenerateChannel("Group", "CH1");
    std::vector<double> samples = {1.0, 2.0, 3.0};

    segment.AddRaw(channel, TDMSType::DoubleFloat, static_cast<int64_t>(samples.size()), reinterpret_cast<uint8_t*>(samples.data()));

    EXPECT_EQ(channel->RawData.Count, 3);
    EXPECT_EQ(channel->RawData.Dimension, 1);
    EXPECT_EQ(channel->RawData.Size, static_cast<long>(3 * sizeof(double)));
    EXPECT_EQ(channel->RawData.Offset, 0);
    EXPECT_FALSE(channel->RawData.IsInterleaved);
    EXPECT_EQ(channel->RawData.DataType.GetDataType(), TDMSType::DoubleFloat);
}

TEST(TdmsWriterSegment, AddRawRejectsStringRawData) {
    WriterSegment segment;
    auto channel = segment.GenerateChannel("Group", "CH1");
    std::vector<uint8_t> bytes = {'a', 'b'};

    EXPECT_THROW(segment.AddRaw(channel, TDMSType::String, 2, bytes.data()), std::invalid_argument);
}

TEST(TdmsWriter, LeadInStartsWithTheTdsmTagAndTdms2Version) {
    SampleSegment sample;
    std::stringstream stream;
    Writer writer(stream, true);
    writer.Write(sample.segment);

    const std::string bytes = stream.str();
    ASSERT_GE(bytes.size(), kLeadInSize);
    auto leadIn = ParseLeadIn(bytes);

    EXPECT_EQ(leadIn.tag, "TDSm");
    EXPECT_EQ(leadIn.version, 4713);
}

TEST(TdmsWriter, TableOfContentsMaskReflectsExactlyTheFlagsThatWereSet) {
    SampleSegment sample;
    std::stringstream stream;
    Writer writer(stream, true);
    writer.Write(sample.segment);

    auto leadIn = ParseLeadIn(stream.str());
    EXPECT_EQ(leadIn.toc, kTocHasMetaData | kTocHasRawData);
    EXPECT_EQ(leadIn.toc & kTocContainsNewObjects, 0);
    EXPECT_EQ(leadIn.toc & kTocNumbersAreBigEndian, 0);
    EXPECT_EQ(leadIn.toc & kTocRawDataIsInterleaved, 0);
}

TEST(TdmsWriter, TableOfContentsMaskPicksUpNewObjectsAndBigEndianFlags) {
    SampleSegment sample;
    auto root = sample.segment.GetRoot();
    root->TableOfContents.ContainsNewObjects = true;
    root->TableOfContents.NumbersAreBigEndian = true;

    std::stringstream stream;
    Writer writer(stream, true);
    writer.Write(sample.segment);

    auto leadIn = ParseLeadIn(stream.str());
    EXPECT_EQ(leadIn.toc, kTocHasMetaData | kTocHasRawData | kTocContainsNewObjects | kTocNumbersAreBigEndian);
}

TEST(TdmsWriter, NextSegmentOffsetCoversEverythingWrittenAfterTheLeadIn) {
    SampleSegment sample;
    std::stringstream stream;
    Writer writer(stream, true);
    writer.Write(sample.segment);

    const std::string bytes = stream.str();
    auto leadIn = ParseLeadIn(bytes);
    EXPECT_EQ(leadIn.nextSegmentOffset, static_cast<int64_t>(bytes.size() - kLeadInSize));
}

TEST(TdmsWriter, RawDataOffsetPointsAtTheStartOfTheRawDataBlock) {
    SampleSegment sample;
    std::stringstream stream;
    Writer writer(stream, true);
    writer.Write(sample.segment);

    const std::string bytes = stream.str();
    auto leadIn = ParseLeadIn(bytes);
    const int64_t rawBytes = static_cast<int64_t>(sample.samples.size() * sizeof(float));

    EXPECT_GT(leadIn.rawDataOffset, 0);
    EXPECT_EQ(leadIn.rawDataOffset, leadIn.nextSegmentOffset - rawBytes);
}

TEST(TdmsWriter, RawDataBlockIsTheChannelSamplesVerbatim) {
    SampleSegment sample;
    std::stringstream stream;
    Writer writer(stream, true);
    writer.Write(sample.segment);

    const std::string bytes = stream.str();
    auto leadIn = ParseLeadIn(bytes);
    const size_t rawBytes = sample.samples.size() * sizeof(float);
    ASSERT_EQ(bytes.size(), kLeadInSize + static_cast<size_t>(leadIn.nextSegmentOffset));

    std::vector<float> written(sample.samples.size());
    std::memcpy(written.data(), bytes.data() + kLeadInSize + leadIn.rawDataOffset, rawBytes);
    EXPECT_EQ(written, sample.samples);
}

TEST(TdmsWriter, MetadataSectionCountsEveryNodeExceptTheRoot) {
    SampleSegment sample;
    std::stringstream stream;
    Writer writer(stream, true);
    writer.Write(sample.segment);

    const std::string bytes = stream.str();
    int32_t objectCount = 0;
    std::memcpy(&objectCount, bytes.data() + kLeadInSize, sizeof(objectCount));
    EXPECT_EQ(objectCount, 2);
}

TEST(TdmsWriter, ObjectPathsAppearInTheMetadataSection) {
    SampleSegment sample("Voltage");
    std::stringstream stream;
    Writer writer(stream, true);
    writer.Write(sample.segment);

    const std::string bytes = stream.str();
    EXPECT_NE(bytes.find("/'Group'"), std::string::npos);
    EXPECT_NE(bytes.find("/'Group'/'Voltage'"), std::string::npos);
}

TEST(TdmsWriter, SecondWriteAppendsAWholeNewSegment) {
    SampleSegment sample;
    std::stringstream stream;
    Writer writer(stream, true);
    writer.Write(sample.segment);
    const size_t sizeAfterFirst = stream.str().size();
    writer.Write(sample.segment);

    const std::string bytes = stream.str();
    ASSERT_EQ(bytes.size(), sizeAfterFirst * 2);
    EXPECT_EQ(ParseLeadIn(bytes, 0).tag, "TDSm");
    EXPECT_EQ(ParseLeadIn(bytes, sizeAfterFirst).tag, "TDSm");
    EXPECT_EQ(ParseLeadIn(bytes, sizeAfterFirst).nextSegmentOffset, static_cast<int64_t>(sizeAfterFirst - kLeadInSize));
}

TEST(TdmsWriter, ChannelWithoutRawDataGetsTheNoRawDataIndex) {
    WriterSegment segment;
    auto root = segment.GenerateRoot();
    root->TableOfContents.HasMetaData = true;
    auto group = segment.GenerateGroup("Group");
    std::vector<std::shared_ptr<Metadata>> nodes = {root, group};
    segment.LoadMetadata(nodes);

    std::stringstream stream;
    Writer writer(stream, true);
    writer.Write(segment);

    const std::string bytes = stream.str();
    const size_t rawIndexOffset = kLeadInSize + 4 /*object count*/ + 4 /*path length*/ + group->PathStr.size();
    ASSERT_GE(bytes.size(), rawIndexOffset + 4);
    int32_t rawIndex = 0;
    std::memcpy(&rawIndex, bytes.data() + rawIndexOffset, sizeof(rawIndex));
    EXPECT_EQ(rawIndex, -1);
}

TEST(TdmsWriter, WriteWithoutARootNodeProducesNoOutput) {
    WriterSegment segment;
    std::vector<std::shared_ptr<Metadata>> nodes = {segment.GenerateGroup("Group")};
    segment.LoadMetadata(nodes);

    std::stringstream stream;
    Writer writer(stream, true);
    writer.Write(segment);

    EXPECT_TRUE(stream.str().empty());
}

TEST(TdmsWriter, InterleavedRawDataIsRejected) {
    SampleSegment sample;
    sample.segment.GetRoot()->TableOfContents.RawDataIsInterleaved = true;

    std::stringstream stream;
    Writer writer(stream, true);
    EXPECT_THROW(writer.Write(sample.segment), std::invalid_argument);
}

TEST(TdmsWriter, DaqMxDataIsRejected) {
    SampleSegment sample;
    sample.segment.GetRoot()->TableOfContents.HasDaqMxData = true;

    std::stringstream stream;
    Writer writer(stream, true);
    EXPECT_THROW(writer.Write(sample.segment), std::invalid_argument);
}

TEST(TdmsWriter, GetFileSizeReportsTheWholeStreamAndRewindsIt) {
    SampleSegment sample;
    std::stringstream stream;
    Writer writer(stream, true);
    writer.Write(sample.segment);

    const size_t expected = stream.str().size();
    EXPECT_EQ(writer.GetFileSize(), expected);
    EXPECT_EQ(stream.tellg(), std::streampos(0));
}

TEST(TdmsFile, WriteMemoryProducesTheSameBytesAsWriterWrite) {
    SampleSegment direct;
    std::stringstream expected;
    Writer writer(expected, true);
    writer.Write(direct.segment);

    SampleSegment viaFile;
    std::stringstream actual;
    File file;
    file.WriteMemory(actual, viaFile.segment);

    EXPECT_EQ(actual.str(), expected.str());
}

TEST(TdmsFile, WriteFileTruncatesWhenNotAppending) {
    SampleSegment sample;
    const std::string path = TestFixturePath("writer_write_file.tdms");

    File file;
    file.WriteFile(path, sample.segment, false);
    const std::streamoff sizeAfterFirst = std::ifstream(path, std::ios::binary | std::ios::ate).tellg();
    file.WriteFile(path, sample.segment, false);
    const std::streamoff sizeAfterSecond = std::ifstream(path, std::ios::binary | std::ios::ate).tellg();

    EXPECT_GT(sizeAfterFirst, 0);
    EXPECT_EQ(sizeAfterSecond, sizeAfterFirst);
}

TEST(TdmsFile, WriteFileAppendsASecondSegmentWhenAppending) {
    SampleSegment sample;
    const std::string path = TestFixturePath("writer_append_file.tdms");

    File file;
    file.WriteFile(path, sample.segment, false);
    const std::streamoff sizeAfterFirst = std::ifstream(path, std::ios::binary | std::ios::ate).tellg();
    file.WriteFile(path, sample.segment, true);
    const std::streamoff sizeAfterSecond = std::ifstream(path, std::ios::binary | std::ios::ate).tellg();

    EXPECT_EQ(sizeAfterSecond, sizeAfterFirst * 2);
}
