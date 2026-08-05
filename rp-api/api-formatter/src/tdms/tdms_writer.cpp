#include "tdms_writer.h"

#include <algorithm>
#include <ios>
#include <stdexcept>
#include <utility>

#include "tdms_bytes.h"

using namespace rp_formatter_api::tdms;

namespace {

// TDMS 2.0.
constexpr std::int32_t kFormatVersion = 4713;

// Table-of-contents bits. Only these two are ever set: this writer always
// emits a metadata section, and sets the raw-data bit when the segment
// actually carries samples. Bit 2 (new object list), bit 5 (interleaved),
// bit 6 (big endian) and bit 7 (DAQmx) stay clear - matching what the previous
// implementation produced, byte for byte.
constexpr std::int32_t kTocHasMetaData = 1 << 1;
constexpr std::int32_t kTocHasRawData = 1 << 3;

constexpr std::size_t kLeadInSize = 28;
static_assert(kLeadInSize == 4 /*"TDSm"*/ + 4 /*ToC*/ + 4 /*version*/ + 8 /*next segment*/ + 8 /*raw data*/);

// "No raw data in this segment for this object."
constexpr std::uint32_t kNoRawDataIndex = 0xFFFFFFFFu;

// Length of the raw-data index that follows: type code + dimension + value
// count, plus the length field itself. Constant for every non-string type.
constexpr std::uint32_t kRawDataIndexLength = 20;

// For TDMS 2.0 the only valid array dimension is 1.
constexpr std::uint32_t kArrayDimension = 1;

}  // namespace

auto rp_formatter_api::tdms::GroupPath(std::string_view _group) -> std::string {
    return "/'" + std::string(_group) + "'";
}

auto rp_formatter_api::tdms::ChannelPath(std::string_view _group, std::string_view _channel) -> std::string {
    return GroupPath(_group) + "/'" + std::string(_channel) + "'";
}

auto Segment::find(std::string_view _path) const -> std::optional<ObjectId> {
    for (std::size_t i = 0; i < m_objects.size(); i++) {
        if (m_objects[i].path == _path) {
            return static_cast<ObjectId>(i);
        }
    }
    return std::nullopt;
}

auto Segment::AddGroup(std::string_view _group) -> ObjectId {
    const std::string path = GroupPath(_group);
    if (const auto existing = find(path)) {
        return *existing;
    }
    m_objects.push_back(Object{path, {}, std::nullopt});
    return static_cast<ObjectId>(m_objects.size() - 1);
}

auto Segment::AddChannel(std::string_view _group, std::string_view _channel, const RawView& _samples) -> ObjectId {
    if (_samples.type == Type::String) {
        throw std::invalid_argument("[ERROR] tdms: string raw data is not supported");
    }
    AddGroup(_group);

    const std::string path = ChannelPath(_group, _channel);
    if (const auto existing = find(path)) {
        m_objects[static_cast<std::size_t>(*existing)].samples = _samples;
        return *existing;
    }
    m_objects.push_back(Object{path, {}, _samples});
    return static_cast<ObjectId>(m_objects.size() - 1);
}

auto Segment::SetProperty(ObjectId _object, std::string _key, Value _value) -> void {
    m_objects.at(static_cast<std::size_t>(_object)).properties.insert_or_assign(std::move(_key), std::move(_value));
}

auto Segment::rawDataByteCount() const -> std::uint64_t {
    std::uint64_t total = 0;
    for (const auto& object : m_objects) {
        if (object.samples) {
            total += object.samples->sampleCount * TypeSize(object.samples->type);
        }
    }
    return total;
}

auto Segment::hasRawData() const -> bool {
    return std::any_of(m_objects.begin(), m_objects.end(), [](const Segment::Object& object) { return object.samples.has_value(); });
}

auto rp_formatter_api::tdms::EncodeMetadata(const Segment& _segment) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> out;
    AppendLE<std::uint32_t>(out, static_cast<std::uint32_t>(_segment.objects().size()));

    for (const auto& object : _segment.objects()) {
        AppendString(out, object.path);

        if (object.samples) {
            AppendLE<std::uint32_t>(out, kRawDataIndexLength);
            AppendLE<std::uint32_t>(out, static_cast<std::uint32_t>(object.samples->type));
            AppendLE<std::uint32_t>(out, kArrayDimension);
            AppendLE<std::uint64_t>(out, object.samples->sampleCount);
        } else {
            AppendLE<std::uint32_t>(out, kNoRawDataIndex);
        }

        AppendLE<std::uint32_t>(out, static_cast<std::uint32_t>(object.properties.size()));
        for (const auto& [key, value] : object.properties) {
            AppendString(out, key);
            AppendLE<std::uint32_t>(out, static_cast<std::uint32_t>(value.type()));
            AppendBytes(out, value.payload().data(), value.payload().size());
        }
    }
    return out;
}

auto rp_formatter_api::tdms::WriteSegment(std::ostream& _out, const Segment& _segment) -> void {
    const std::vector<std::uint8_t> metadata = EncodeMetadata(_segment);
    const std::uint64_t metadataLength = metadata.size();
    const std::uint64_t rawDataLength = _segment.rawDataByteCount();

    std::int32_t tableOfContents = kTocHasMetaData;
    if (_segment.hasRawData()) {
        tableOfContents |= kTocHasRawData;
    }

    _out.seekp(0, std::ios::end);

    // Lead-in, 28 bytes. Both offsets are relative to the end of the lead-in
    // and are final on the first write - no back-patching.
    WriteBytes(_out, "TDSm", 4);
    WriteLE<std::int32_t>(_out, tableOfContents);
    WriteLE<std::int32_t>(_out, kFormatVersion);
    WriteLE<std::uint64_t>(_out, metadataLength + rawDataLength);
    WriteLE<std::uint64_t>(_out, metadataLength);

    WriteBytes(_out, metadata.data(), metadata.size());

    // Raw data, in the same object order as the metadata section.
    for (const auto& object : _segment.objects()) {
        if (object.samples && object.samples->byteCount() > 0) {
            WriteBytes(_out, object.samples->data, object.samples->byteCount());
        }
    }
}
