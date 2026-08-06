/**
 * Write then read, on top of the golden suites.
 *
 * These are the weakest tests in the suite on their own - a defect shared by
 * writer and reader passes a round trip - so they exist only to catch
 * integration mistakes that the byte-level goldens would not notice, such as
 * offsets that are self-consistent but wrong relative to the segment.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "byte_utils.h"
#include "file.h"
#include "golden_bytes.h"

using namespace tdms_test;

namespace {

struct Written {
    Bytes bytes;
    std::vector<std::shared_ptr<std::uint8_t[]>> buffers;
};

template <typename T>
auto WriteOneChannel(TDMS::TDMSType type, const std::vector<T>& samples, int segments = 1) -> Written {
    TDMS::WriterSegment segment;
    std::vector<std::shared_ptr<TDMS::Metadata>> nodes;
    auto root = segment.GenerateRoot();
    root->TableOfContents.HasMetaData = true;
    root->TableOfContents.HasRawData = true;
    nodes.push_back(root);
    auto group = segment.GenerateGroup("Group");
    nodes.push_back(group);
    auto channel = segment.GenerateChannel("Group", "CH1");
    nodes.push_back(channel);

    Written result;
    auto buffer = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[samples.size() * sizeof(T)]);
    if (!samples.empty()) {
        std::memcpy(buffer.get(), samples.data(), samples.size() * sizeof(T));
    }
    result.buffers.push_back(buffer);
    segment.AddRaw(channel, type, static_cast<std::int64_t>(samples.size()), buffer);
    segment.LoadMetadata(nodes);

    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    TDMS::Writer writer(stream, true);
    for (int i = 0; i < segments; i++) {
        writer.Write(segment);
    }
    result.bytes = ToBytes(stream.str());
    return result;
}

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

TEST(RoundTrip, FloatChannelSurvivesWriteThenRead) {
    const std::vector<float> samples = {1.f, -2.5f, 3.25f, 4.f};
    const Written written = WriteOneChannel<float>(TDMS::TDMSType::SingleFloat, samples);

    std::stringstream stream = MakeStream(written.bytes);
    TDMS::Reader reader(stream, written.bytes.size());
    auto metadata = reader.ReadMetadata(reader.ReadFirstSegment());
    ASSERT_EQ(metadata.size(), 2u);
    EXPECT_EQ(SamplesOf<float>(metadata[1]), samples);
}

TEST(RoundTrip, EveryScalarTypeSurvivesWriteThenRead) {
    {
        const std::vector<std::int8_t> v = {-1, 2, -3};
        const Written w = WriteOneChannel<std::int8_t>(TDMS::TDMSType::Integer8, v);
        std::stringstream s = MakeStream(w.bytes);
        TDMS::Reader r(s, w.bytes.size());
        auto md = r.ReadMetadata(r.ReadFirstSegment());
        ASSERT_EQ(md.size(), 2u);
        EXPECT_EQ(SamplesOf<std::int8_t>(md[1]), v);
    }
    {
        const std::vector<std::int64_t> v = {-1, 1LL << 40, 3};
        const Written w = WriteOneChannel<std::int64_t>(TDMS::TDMSType::Integer64, v);
        std::stringstream s = MakeStream(w.bytes);
        TDMS::Reader r(s, w.bytes.size());
        auto md = r.ReadMetadata(r.ReadFirstSegment());
        ASSERT_EQ(md.size(), 2u);
        EXPECT_EQ(SamplesOf<std::int64_t>(md[1]), v);
    }
    {
        const std::vector<double> v = {1.5, -2.25, 1e300};
        const Written w = WriteOneChannel<double>(TDMS::TDMSType::DoubleFloat, v);
        std::stringstream s = MakeStream(w.bytes);
        TDMS::Reader r(s, w.bytes.size());
        auto md = r.ReadMetadata(r.ReadFirstSegment());
        ASSERT_EQ(md.size(), 2u);
        EXPECT_EQ(SamplesOf<double>(md[1]), v);
    }
}

TEST(RoundTrip, TwoWrittenSegmentsAreBothFound) {
    const std::vector<float> samples = {1.f, 2.f};
    const Written written = WriteOneChannel<float>(TDMS::TDMSType::SingleFloat, samples, 2);

    TempFile file("two.tdms");
    file.write(written.bytes);
    TDMS::File reader;
    auto segments = reader.ReadFileWithoutClose(file.path());
    EXPECT_EQ(segments.size(), 2u);
    reader.Close();
}

TEST(RoundTrip, PropertiesSurviveWriteThenRead) {
    TDMS::WriterSegment segment;
    std::vector<std::shared_ptr<TDMS::Metadata>> nodes;
    auto root = segment.GenerateRoot();
    root->TableOfContents.HasMetaData = true;
    nodes.push_back(root);
    auto group = segment.GenerateGroup("Group");
    nodes.push_back(group);

    TDMS::DataType rate;
    rate.InitDataType(TDMS::TDMSType::UnsignedInteger64, TDMS::DataType::MakeData<std::uint64_t>(golden::kReferenceOscRate));
    segment.AddProperties(group, "osc_rate", rate);
    TDMS::DataType scale;
    scale.InitDataType(TDMS::TDMSType::DoubleFloat, TDMS::DataType::MakeData<double>(-0.125));
    segment.AddProperties(group, "scale", scale);
    segment.LoadMetadata(nodes);

    std::stringstream out(std::ios::in | std::ios::out | std::ios::binary);
    TDMS::Writer writer(out, true);
    writer.Write(segment);

    const Bytes bytes = ToBytes(out.str());
    std::stringstream in = MakeStream(bytes);
    TDMS::Reader reader(in, bytes.size());
    auto metadata = reader.ReadMetadata(reader.ReadFirstSegment());
    ASSERT_EQ(metadata.size(), 1u);
    ASSERT_EQ(metadata[0]->Properties.size(), 2u);
    EXPECT_EQ(metadata[0]->Properties.at("osc_rate").GetData<std::uint64_t>(), golden::kReferenceOscRate);
    EXPECT_DOUBLE_EQ(metadata[0]->Properties.at("scale").GetData<double>(), -0.125);
}

TEST(RoundTrip, WriteFileThenReadFileAgree) {
    TempFile file("roundtrip.tdms");
    const std::vector<float> samples = {5.f, 6.f, 7.f};

    {
        TDMS::WriterSegment segment;
        std::vector<std::shared_ptr<TDMS::Metadata>> nodes;
        auto root = segment.GenerateRoot();
        root->TableOfContents.HasMetaData = true;
        root->TableOfContents.HasRawData = true;
        nodes.push_back(root);
        auto group = segment.GenerateGroup("Group");
        nodes.push_back(group);
        auto channel = segment.GenerateChannel("Group", "CH1");
        nodes.push_back(channel);
        auto buffer = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[samples.size() * sizeof(float)]);
        std::memcpy(buffer.get(), samples.data(), samples.size() * sizeof(float));
        segment.AddRaw(channel, TDMS::TDMSType::SingleFloat, 3, buffer);
        segment.LoadMetadata(nodes);

        TDMS::File writer;
        writer.WriteFile(file.path(), segment, false);
    }

    TDMS::File reader;
    auto metadata = reader.ReadFile(file.path());
    ASSERT_EQ(metadata.size(), 2u);
    EXPECT_EQ(SamplesOf<float>(metadata[1]), samples);
}
