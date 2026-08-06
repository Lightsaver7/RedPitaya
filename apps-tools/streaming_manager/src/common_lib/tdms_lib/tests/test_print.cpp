/**
 * File::Print / DataType::PrintVector.
 *
 * These are diagnostics, but they are where the sprintf-into-char[22] overflow
 * and the uninitialised return buffer lived, so they get exercised for every
 * type rather than left untested.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "byte_utils.h"
#include "file.h"
#include "golden_bytes.h"

using namespace tdms_test;

namespace {

// Swaps std::cout's buffer so the diagnostics can be inspected.
class CoutCapture {
   public:
    CoutCapture() : m_previous(std::cout.rdbuf(m_buffer.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(m_previous); }
    auto text() const -> std::string { return m_buffer.str(); }

   private:
    std::stringstream m_buffer;
    std::streambuf* m_previous;
};

auto MetadataWithSamples(TDMS::TDMSType type, const Bytes& raw, std::int64_t count) -> std::shared_ptr<TDMS::Metadata> {
    auto metadata = std::make_shared<TDMS::Metadata>();
    metadata->PathStr = "/'G'/'A'";
    auto buffer = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[raw.size() ? raw.size() : 1]);
    if (!raw.empty()) {
        std::memcpy(buffer.get(), raw.data(), raw.size());
    }
    metadata->RawData.DataType.InitRaw(type, static_cast<std::uint64_t>(count), buffer);
    metadata->RawData.Count = count;
    metadata->RawData.Size = static_cast<std::int64_t>(raw.size());
    metadata->RawData.Dimension = 1;
    return metadata;
}

}  // namespace

TEST(Print, PrintsPathAndPropertiesWithoutCrashing) {
    TempFile file("reference.tdms");
    file.write(golden::Reference());
    TDMS::File reader;
    auto metadata = reader.ReadFile(file.path());
    ASSERT_EQ(metadata.size(), 2u);

    std::string text;
    {
        CoutCapture capture;
        reader.Print(metadata, /*PrintRaw=*/true, /*limitData=*/-1);
        text = capture.text();
    }
    EXPECT_NE(text.find("/'Group'"), std::string::npos);
    EXPECT_NE(text.find("osc_rate"), std::string::npos);
}

TEST(Print, EveryLimitValueIsAccepted) {
    TempFile file("reference.tdms");
    file.write(golden::Reference());
    TDMS::File reader;
    auto metadata = reader.ReadFile(file.path());

    for (long limit : {-1L, 0L, 1L, 1000000L}) {
        SCOPED_TRACE("limitData " + std::to_string(limit));
        CoutCapture capture;
        EXPECT_NO_THROW(reader.Print(metadata, true, limit));
    }
}

TEST(Print, LargeDoublesDoNotOverflowTheFormattingBuffer) {
    Bytes raw;
    const double values[] = {1.7976931348623157e308, -1.7976931348623157e308, 0.0};
    for (double v : values) {
        Bytes bytes(sizeof(double));
        std::memcpy(bytes.data(), &v, sizeof(double));
        raw.insert(raw.end(), bytes.begin(), bytes.end());
    }

    std::vector<std::shared_ptr<TDMS::Metadata>> metadata = {
        MetadataWithSamples(TDMS::TDMSType::DoubleFloat, raw, 3)};

    TDMS::File printer;
    CoutCapture capture;
    EXPECT_NO_THROW(printer.Print(metadata, true, -1));
}

TEST(Print, TimeStampVectorsArePrintedWithoutReadingPastTheEnd) {
    // The TimeStamp arm consumes two uint64 per printed value; with the wrong
    // element count that reads past the buffer.
    for (int stamps : {1, 3}) {
        SCOPED_TRACE("timestamps " + std::to_string(stamps));
        Bytes raw;
        for (int i = 0; i < stamps; i++) {
            Builder one;
            one.U64(0).I64(3868000000LL + i);
            const Bytes bytes = one.bytes();
            raw.insert(raw.end(), bytes.begin(), bytes.end());
        }
        std::vector<std::shared_ptr<TDMS::Metadata>> metadata = {
            MetadataWithSamples(TDMS::TDMSType::TimeStamp, raw, stamps)};

        TDMS::File printer;
        CoutCapture capture;
        EXPECT_NO_THROW(printer.Print(metadata, true, -1));
    }
}

TEST(Print, EveryScalarTypePrints) {
    const struct {
        TDMS::TDMSType type;
        std::size_t width;
    } cases[] = {
        {TDMS::TDMSType::Integer8, 1},          {TDMS::TDMSType::Integer16, 2},
        {TDMS::TDMSType::Integer32, 4},         {TDMS::TDMSType::Integer64, 8},
        {TDMS::TDMSType::UnsignedInteger8, 1},  {TDMS::TDMSType::UnsignedInteger16, 2},
        {TDMS::TDMSType::UnsignedInteger32, 4}, {TDMS::TDMSType::UnsignedInteger64, 8},
        {TDMS::TDMSType::SingleFloat, 4},       {TDMS::TDMSType::DoubleFloat, 8},
        {TDMS::TDMSType::Boolean, 1},
    };

    for (const auto& c : cases) {
        SCOPED_TRACE("type " + std::to_string(static_cast<std::uint32_t>(c.type)));
        Bytes raw(c.width * 2, 0xA5);
        std::vector<std::shared_ptr<TDMS::Metadata>> metadata = {MetadataWithSamples(c.type, raw, 2)};
        TDMS::File printer;
        CoutCapture capture;
        EXPECT_NO_THROW(printer.Print(metadata, true, -1));
    }
}
