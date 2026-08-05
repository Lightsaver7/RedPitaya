/**
 * $Id$
 *
 * @brief Red Pitaya data formatter.
 *
 * (c) Red Pitaya  http://www.redpitaya.com
 *
 */

#include <chrono>
#include <cstdint>
#include <string>

#include "rp_tdms_writer.h"
#include "tdms/tdms_writer.h"

using namespace rp_formatter_api;

struct CTDMSWriter::Impl {
    uint32_t m_OSCRate;
    SStreamGuard m_guard;
    auto write(SBufferPack* _pack, std::iostream* _memory) -> bool;
};

CTDMSWriter::CTDMSWriter(uint32_t _oscRate) {
    m_pimpl = new Impl();
    m_pimpl->m_OSCRate = _oscRate;
}

CTDMSWriter::~CTDMSWriter() {
    delete m_pimpl;
}

auto CTDMSWriter::resetHeaderInit() -> void {
    m_pimpl->m_guard.reset();
}

auto CTDMSWriter::notifyStreamClosed(std::iostream* _memory) -> void {
    m_pimpl->m_guard.invalidate(_memory);
}

namespace {

// NOTE: RP_F_ui8_Bit and RP_F_ui16_Bit deliberately map onto the *signed* TDMS
// types, which is what this writer has always emitted; RP_F_ui32_Bit and
// RP_F_ui64_Bit do not. Values above 127 / 32767 therefore read back negative.
// See the comment in tests/verify_tdms.py - changing the mapping would silently
// alter the meaning of existing files.
auto tdmsTypeFor(rp_bits_t _bits) -> tdms::Type {
    switch (_bits) {
        case RP_F_ui8_Bit:
            return tdms::Type::Int8;
        case RP_F_ui16_Bit:
            return tdms::Type::Int16;
        case RP_F_ui32_Bit:
            return tdms::Type::UInt32;
        case RP_F_i32_Bit:
            return tdms::Type::Int32;
        case RP_F_ui64_Bit:
            return tdms::Type::UInt64;
        case RP_F_i64_Bit:
            return tdms::Type::Int64;
        case RP_F_f32_Bit:
            return tdms::Type::Float;
        case RP_F_d64_Bit:
            return tdms::Type::Double;
    }
    return tdms::Type::Int8;
}

auto unixSecondsNow() -> std::int64_t {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

}  // namespace

auto CTDMSWriter::Impl::write(SBufferPack* _pack, std::iostream* _memory) -> bool {
    if (!m_guard.accept(_memory, "CTDMSWriter")) {
        return false;
    }

    // Holds non-owning views of _pack's sample buffers, which outlive this
    // call; it must not escape the function.
    tdms::Segment segment;

    const auto group = segment.AddGroup("Group");
    segment.SetProperty(group, "time", tdms::Value::TimestampFromUnixSeconds(unixSecondsNow()));
    segment.SetProperty(group, "osc_rate", tdms::Value::UInt64(m_OSCRate));

    for (auto ch = RP_F_CH1; ch <= RP_F_INDEX; ch = rp_channel_t(ch + 1)) {
        if (!_pack->m_buffer.count(ch)) {
            continue;
        }
        const auto& name = _pack->m_name[ch];
        const std::string channelName = name.empty() ? SBufferPack::getChannelName(ch) : name;

        segment.AddChannel("Group", channelName,
                           tdms::RawView{tdmsTypeFor(_pack->m_bits[ch]), static_cast<const std::uint8_t*>(_pack->m_buffer[ch]),
                                         _pack->m_samplesCount[ch]});
    }

    tdms::WriteSegment(*_memory, segment);
    return true;
}

auto CTDMSWriter::writeToStream(SBufferPack* _pack, std::iostream* _memory) -> bool {
    return m_pimpl->write(_pack, _memory);
}
