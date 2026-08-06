/**
 * The writer's byte-level contract.
 *
 * Every expectation here is built with tests/support/spec_builder.h from the
 * TDMS 2.0 field layout, not captured from the writer, so these tests are an
 * oracle rather than a snapshot. They are also the byte-compatibility gate for
 * the whole refactoring: any change that moves a single byte of writer output
 * fails here.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "byte_utils.h"
#include "golden_bytes.h"
#include "spec_builder.h"
#include "writer.h"

using namespace tdms_test;

namespace {

// Builds the node list the way the external consumer does
// (writer_lib/file_helper.cpp: root with the two ToC bits, then a group, then
// channels), and returns the bytes Writer produces.
class SegmentFixture {
   public:
    explicit SegmentFixture(bool hasRawData = true) {
        m_root = m_segment.GenerateRoot();
        m_root->TableOfContents.HasMetaData = true;
        m_root->TableOfContents.HasRawData = hasRawData;
        m_nodes.push_back(m_root);
    }

    auto AddGroup(const std::string& name) -> std::shared_ptr<TDMS::Metadata> {
        auto group = m_segment.GenerateGroup(name);
        m_nodes.push_back(group);
        return group;
    }

    auto AddChannel(const std::string& group, const std::string& channel) -> std::shared_ptr<TDMS::Metadata> {
        auto node = m_segment.GenerateChannel(group, channel);
        m_nodes.push_back(node);
        return node;
    }

    // Keeps the sample buffer alive for the lifetime of the fixture.
    template <typename T>
    auto AddSamples(const std::shared_ptr<TDMS::Metadata>& node, TDMS::TDMSType type, const std::vector<T>& values) -> void {
        auto buffer = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[values.size() * sizeof(T)]);
        if (!values.empty()) {
            std::memcpy(buffer.get(), values.data(), values.size() * sizeof(T));
        }
        m_buffers.push_back(buffer);
        m_segment.AddRaw(node, type, static_cast<std::int64_t>(values.size()), buffer);
    }

    auto AddProperty(const std::shared_ptr<TDMS::Metadata>& node, const std::string& key, TDMS::DataType value) -> void {
        m_segment.AddProperties(node, key, value);
    }

    auto segment() -> TDMS::WriterSegment& { return m_segment; }
    auto root() -> std::shared_ptr<TDMS::Metadata> { return m_root; }

    auto Write() -> Bytes {
        m_segment.LoadMetadata(m_nodes);
        std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
        TDMS::Writer writer(stream, true);
        writer.Write(m_segment);
        const std::string text = stream.str();
        return ToBytes(text);
    }

   private:
    TDMS::WriterSegment m_segment;
    std::shared_ptr<TDMS::Metadata> m_root;
    std::vector<std::shared_ptr<TDMS::Metadata>> m_nodes;
    std::vector<std::shared_ptr<std::uint8_t[]>> m_buffers;
};

template <typename T>
auto ScalarProperty(TDMS::TDMSType type, T value) -> TDMS::DataType {
    TDMS::DataType property;
    property.InitDataType(type, TDMS::DataType::MakeData<T>(value));
    return property;
}

auto ReferenceFixtureBytes() -> Bytes {
    SegmentFixture fixture;
    auto group = fixture.AddGroup("Group");
    fixture.AddProperty(group, "osc_rate", ScalarProperty<std::uint64_t>(TDMS::TDMSType::UnsignedInteger64, golden::kReferenceOscRate));
    auto channel = fixture.AddChannel("Group", "CH1");
    fixture.AddSamples<float>(channel, TDMS::TDMSType::SingleFloat, {1.f, 2.f, 3.f, 4.f});
    return fixture.Write();
}

}  // namespace

TEST(WriterGolden, ReferenceSegmentMatchesTheHandDerivedBytes) {
    ExpectBytesEq(ReferenceFixtureBytes(), golden::Reference());
}

TEST(WriterGolden, ReferenceSegmentLeadInFieldsAreExact) {
    const Bytes actual = ReferenceFixtureBytes();
    ASSERT_EQ(actual.size(), golden::kReferenceSize);

    EXPECT_EQ(std::string(actual.begin(), actual.begin() + 4), "TDSm");

    std::int32_t toc = 0;
    std::int32_t version = 0;
    std::int64_t next = 0;
    std::int64_t raw = 0;
    std::memcpy(&toc, actual.data() + 4, sizeof(toc));
    std::memcpy(&version, actual.data() + 8, sizeof(version));
    std::memcpy(&next, actual.data() + 12, sizeof(next));
    std::memcpy(&raw, actual.data() + 20, sizeof(raw));

    EXPECT_EQ(toc, kTocMetaData | kTocRawData);
    EXPECT_EQ(version, kTdms2Version);
    EXPECT_EQ(next, golden::kReferenceMetadataLength + golden::kReferenceRawLength);
    EXPECT_EQ(raw, golden::kReferenceMetadataLength);
}

TEST(WriterGolden, RootObjectIsNotEmittedAndObjectCountExcludesIt) {
    const Bytes actual = ReferenceFixtureBytes();
    std::uint32_t objectCount = 0;
    std::memcpy(&objectCount, actual.data() + kLeadInSize, sizeof(objectCount));
    EXPECT_EQ(objectCount, 2u) << "group + channel; the root \"/\" object is not written";

    // The root path would appear as a 1-byte length-prefixed "/" record.
    const std::string text(actual.begin(), actual.end());
    const std::string rootRecord("\x01\x00\x00\x00/", 5);
    EXPECT_EQ(text.find(rootRecord), std::string::npos) << "no \"/\" object record may appear";
}

TEST(WriterGolden, ContainsNewObjectsBitIsNeverSet) {
    const Bytes actual = ReferenceFixtureBytes();
    std::int32_t toc = 0;
    std::memcpy(&toc, actual.data() + 4, sizeof(toc));
    EXPECT_EQ(toc & kTocNewObjList, 0);
    EXPECT_EQ(toc & kTocBigEndian, 0);
    EXPECT_EQ(toc & kTocDaqMx, 0);
    EXPECT_EQ(toc & kTocInterleaved, 0);
}

TEST(WriterGolden, GroupOnlySegmentLeavesTheRawDataOffsetAtZero) {
    // The writer only patches the raw-data-offset field when the root's
    // HasRawData bit is set. With no raw data the field stays 0, which is what
    // this pins - it is not spec-perfect, it is what the writer does.
    SegmentFixture fixture(/*hasRawData=*/false);
    fixture.AddGroup("Group");
    ExpectBytesEq(fixture.Write(), golden::GroupOnly());
}

TEST(WriterGolden, TwoContiguousChannelsConcatenateTheirRawBlocksInNodeOrder) {
    SegmentFixture fixture;
    fixture.AddGroup("G");
    auto a = fixture.AddChannel("G", "A");
    auto b = fixture.AddChannel("G", "B");
    fixture.AddSamples<std::int32_t>(a, TDMS::TDMSType::Integer32, {10, 11, 12});
    fixture.AddSamples<std::int32_t>(b, TDMS::TDMSType::Integer32, {20, 21, 22});
    ExpectBytesEq(fixture.Write(), golden::TwoChannelsContiguous());
}

TEST(WriterGolden, PropertiesAreEmittedInAscendingKeyOrder) {
    // Metadata::Properties is a std::map, so insertion order does not matter.
    // Pinned because switching it to an unordered container would silently
    // change every file this library writes.
    SegmentFixture fixture(/*hasRawData=*/false);
    auto group = fixture.AddGroup("G");
    fixture.AddProperty(group, "zulu", ScalarProperty<std::uint8_t>(TDMS::TDMSType::UnsignedInteger8, 1));
    fixture.AddProperty(group, "alpha", ScalarProperty<std::uint8_t>(TDMS::TDMSType::UnsignedInteger8, 2));
    fixture.AddProperty(group, "mike", ScalarProperty<std::uint8_t>(TDMS::TDMSType::UnsignedInteger8, 3));

    Builder meta;
    meta.U32(1);
    meta.PString("/'G'");
    meta.U32(kNoRawDataIndex);
    meta.U32(3);
    meta.PropertyHeader("alpha", kTypeU8).U8(2);
    meta.PropertyHeader("mike", kTypeU8).U8(3);
    meta.PropertyHeader("zulu", kTypeU8).U8(1);

    Builder expected;
    expected.LeadIn(kTocMetaData, static_cast<std::int64_t>(meta.size()), 0);
    expected.Raw(meta.bytes());

    ExpectBytesEq(fixture.Write(), expected.bytes());
}

TEST(WriterGolden, EveryScalarRawTypeIsWrittenWithItsOwnTypeCodeAndWidth) {
    struct Case {
        TDMS::TDMSType type;
        std::uint32_t code;
        std::size_t width;
    };
    const Case cases[] = {
        {TDMS::TDMSType::Integer8, kTypeI8, 1},   {TDMS::TDMSType::Integer16, kTypeI16, 2},
        {TDMS::TDMSType::Integer32, kTypeI32, 4}, {TDMS::TDMSType::Integer64, kTypeI64, 8},
        {TDMS::TDMSType::UnsignedInteger8, kTypeU8, 1}, {TDMS::TDMSType::UnsignedInteger16, kTypeU16, 2},
        {TDMS::TDMSType::UnsignedInteger32, kTypeU32, 4}, {TDMS::TDMSType::UnsignedInteger64, kTypeU64, 8},
        {TDMS::TDMSType::SingleFloat, kTypeF32, 4}, {TDMS::TDMSType::DoubleFloat, kTypeF64, 8},
        {TDMS::TDMSType::Boolean, kTypeBool, 1},
    };

    for (const Case& c : cases) {
        SegmentFixture fixture;
        fixture.AddGroup("G");
        auto channel = fixture.AddChannel("G", "A");
        // Two samples of whatever width, filled with a recognisable pattern.
        std::vector<std::uint8_t> raw(2 * c.width);
        for (std::size_t i = 0; i < raw.size(); i++) {
            raw[i] = static_cast<std::uint8_t>(0xA0 + i);
        }
        auto buffer = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[raw.size()]);
        std::memcpy(buffer.get(), raw.data(), raw.size());
        fixture.segment().AddRaw(channel, c.type, 2, buffer);

        Builder meta;
        meta.U32(2);
        meta.ObjectNoRaw("/'G'", 0);
        meta.ObjectWithRaw("/'G'/'A'", c.code, 2, 0);

        Builder expected;
        expected.LeadIn(kTocMetaData | kTocRawData, static_cast<std::int64_t>(meta.size() + raw.size()),
                        static_cast<std::int64_t>(meta.size()));
        expected.Raw(meta.bytes()).Raw(Bytes(raw.begin(), raw.end()));

        SCOPED_TRACE("type code " + std::to_string(c.code));
        ExpectBytesEq(fixture.Write(), expected.bytes());
    }
}

TEST(WriterGolden, ZeroSampleChannelStillGetsAFullRawDataIndex) {
    SegmentFixture fixture;
    fixture.AddGroup("G");
    auto channel = fixture.AddChannel("G", "A");
    fixture.AddSamples<float>(channel, TDMS::TDMSType::SingleFloat, {});

    Builder meta;
    meta.U32(2);
    meta.ObjectNoRaw("/'G'", 0);
    meta.ObjectWithRaw("/'G'/'A'", kTypeF32, 0, 0);

    Builder expected;
    expected.LeadIn(kTocMetaData | kTocRawData, static_cast<std::int64_t>(meta.size()), static_cast<std::int64_t>(meta.size()));
    expected.Raw(meta.bytes());

    ExpectBytesEq(fixture.Write(), expected.bytes());
}

TEST(WriterGolden, TwoWritesAppendTwoCompleteSegments) {
    TDMS::WriterSegment segment;
    std::vector<std::shared_ptr<TDMS::Metadata>> nodes;
    auto root = segment.GenerateRoot();
    root->TableOfContents.HasMetaData = true;
    root->TableOfContents.HasRawData = true;
    nodes.push_back(root);
    auto group = segment.GenerateGroup("Group");
    nodes.push_back(group);
    TDMS::DataType rate = ScalarProperty<std::uint64_t>(TDMS::TDMSType::UnsignedInteger64, golden::kReferenceOscRate);
    segment.AddProperties(group, "osc_rate", rate);
    auto channel = segment.GenerateChannel("Group", "CH1");
    nodes.push_back(channel);
    auto buffer = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[4 * sizeof(float)]);
    const float values[4] = {1.f, 2.f, 3.f, 4.f};
    std::memcpy(buffer.get(), values, sizeof(values));
    segment.AddRaw(channel, TDMS::TDMSType::SingleFloat, 4, buffer);
    segment.LoadMetadata(nodes);

    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    TDMS::Writer writer(stream, true);
    writer.Write(segment);
    writer.Write(segment);

    const Bytes actual = ToBytes(stream.str());
    Bytes expected = golden::Reference();
    const Bytes second = golden::Reference();
    expected.insert(expected.end(), second.begin(), second.end());
    ExpectBytesEq(actual, expected);
}

TEST(WriterGolden, WriteWithoutARootNodeProducesNothing) {
    TDMS::WriterSegment segment;
    std::vector<std::shared_ptr<TDMS::Metadata>> nodes = {segment.GenerateGroup("Group")};
    segment.LoadMetadata(nodes);

    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    TDMS::Writer writer(stream, true);
    writer.Write(segment);

    EXPECT_TRUE(stream.str().empty());
}

TEST(WriterGolden, InterleavedDaqMxAndStringRawAreRejected) {
    {
        SegmentFixture fixture;
        fixture.root()->TableOfContents.RawDataIsInterleaved = true;
        fixture.AddGroup("G");
        fixture.segment().LoadMetadata({fixture.root()});
        std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
        TDMS::Writer writer(stream, true);
        EXPECT_THROW(writer.Write(fixture.segment()), std::invalid_argument);
    }
    {
        SegmentFixture fixture;
        fixture.root()->TableOfContents.HasDaqMxData = true;
        fixture.segment().LoadMetadata({fixture.root()});
        std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
        TDMS::Writer writer(stream, true);
        EXPECT_THROW(writer.Write(fixture.segment()), std::invalid_argument);
    }
    {
        TDMS::WriterSegment segment;
        auto channel = segment.GenerateChannel("G", "A");
        auto buffer = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[2]);
        EXPECT_THROW(segment.AddRaw(channel, TDMS::TDMSType::String, 2, buffer), std::invalid_argument);
    }
}
