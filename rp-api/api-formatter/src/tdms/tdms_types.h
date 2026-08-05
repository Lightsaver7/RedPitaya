/**
 * TDMS data types, property values and channel sample views.
 *
 * This replaces the old TDMS::DataType, which used one class for three
 * unrelated jobs - an owned scalar behind a void*, an owned string, and a
 * borrowed array of samples - and freed the void* through a switch on a
 * runtime type tag. That design is where the throwing destructor, the leaking
 * copy-assignment and the mismatched delete[] all came from.
 *
 * Here the two jobs are separate types with opposite ownership:
 *
 *   Value   owns its bytes in a std::vector. Copy, move and destruction are
 *           all implicit and correct (rule of zero); there is nothing to get
 *           wrong. It carries the payload already encoded, so serialising a
 *           property needs no switch on its type at all.
 *
 *   RawView borrows the caller's sample buffer and is trivially copyable.
 *           Borrowing is deliberate, not an oversight: CTDMSWriter points it
 *           straight at the acquisition buffer, which can be megabytes.
 */

#ifndef __RP_TDMS_TYPES_H__
#define __RP_TDMS_TYPES_H__

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "tdms_bytes.h"

namespace rp_formatter_api::tdms {

// TDMS type codes. Only the codes this writer can emit are listed; the old
// enum also carried Empty, Void, the Extended* and the *WithUnit variants,
// none of which were ever produced.
enum class Type : std::uint32_t {
    Int8 = 0x01,
    Int16 = 0x02,
    Int32 = 0x03,
    Int64 = 0x04,
    UInt8 = 0x05,
    UInt16 = 0x06,
    UInt32 = 0x07,
    UInt64 = 0x08,
    Float = 0x09,
    Double = 0x0A,
    String = 0x20,
    Boolean = 0x21,
    Timestamp = 0x44,
};

// On-disk width of a fixed-width type. Type::String has none - it is
// length-prefixed - and yields 0.
constexpr auto TypeSize(Type _type) -> std::size_t {
    switch (_type) {
        case Type::Int8:
        case Type::UInt8:
        case Type::Boolean:
            return 1;
        case Type::Int16:
        case Type::UInt16:
            return 2;
        case Type::Int32:
        case Type::UInt32:
        case Type::Float:
            return 4;
        case Type::Int64:
        case Type::UInt64:
        case Type::Double:
            return 8;
        case Type::Timestamp:
            return 16;
        case Type::String:
            return 0;
    }
    return 0;
}

// Seconds between the TDMS epoch (1904-01-01T00:00:00 UTC) and the Unix
// epoch, ignoring leap seconds: 66 years (1904..1969) is 66 * 365 days plus
// 17 leap days (1904, 1908, ... 1968) = 24107 days.
//
// The old code derived this three separate ways, one of them via mktime(),
// which is local-time and therefore made the recorded timestamp depend on the
// machine's timezone even though a TDMS timestamp is UTC.
constexpr std::int64_t kSecondsFrom1904To1970 = 24107LL * 86400LL;
static_assert(kSecondsFrom1904To1970 == 2082844800LL);

// An owned TDMS property value.
//
// payload() holds exactly the bytes that follow the 4-byte type code on disk;
// for Type::String that already includes the 4-byte length prefix. Writing a
// property is therefore "type code, then payload" with no per-type logic.
//
// There is no default constructor and no generic Scalar<T>(Type, T) factory:
// every Value comes from a factory that fixes the type code and the payload
// width together, so a value whose tag disagrees with its width cannot be
// constructed.
class Value {
   public:
    static auto Int8(std::int8_t _v) -> Value { return Scalar(Type::Int8, _v); }
    static auto Int16(std::int16_t _v) -> Value { return Scalar(Type::Int16, _v); }
    static auto Int32(std::int32_t _v) -> Value { return Scalar(Type::Int32, _v); }
    static auto Int64(std::int64_t _v) -> Value { return Scalar(Type::Int64, _v); }
    static auto UInt8(std::uint8_t _v) -> Value { return Scalar(Type::UInt8, _v); }
    static auto UInt16(std::uint16_t _v) -> Value { return Scalar(Type::UInt16, _v); }
    static auto UInt32(std::uint32_t _v) -> Value { return Scalar(Type::UInt32, _v); }
    static auto UInt64(std::uint64_t _v) -> Value { return Scalar(Type::UInt64, _v); }
    static auto Float(float _v) -> Value { return Scalar(Type::Float, _v); }
    static auto Double(double _v) -> Value { return Scalar(Type::Double, _v); }
    static auto Boolean(bool _v) -> Value { return Scalar(Type::Boolean, static_cast<std::uint8_t>(_v ? 1 : 0)); }

    static auto String(std::string_view _text) -> Value {
        std::vector<std::uint8_t> payload;
        payload.reserve(sizeof(std::uint32_t) + _text.size());
        AppendString(payload, _text);
        return Value(Type::String, std::move(payload));
    }

    // A TDMS timestamp is a 64-bit unsigned count of 2^-64 fractions of a
    // second followed by 64-bit signed seconds since the 1904 epoch, in that
    // order.
    static auto Timestamp(std::int64_t _secondsSince1904, std::uint64_t _fractions = 0) -> Value {
        std::vector<std::uint8_t> payload;
        payload.reserve(TypeSize(Type::Timestamp));
        AppendLE<std::uint64_t>(payload, _fractions);
        AppendLE<std::int64_t>(payload, _secondsSince1904);
        return Value(Type::Timestamp, std::move(payload));
    }

    static auto TimestampFromUnixSeconds(std::int64_t _unixSeconds) -> Value {
        return Timestamp(_unixSeconds + kSecondsFrom1904To1970);
    }

    auto type() const -> Type { return m_type; }
    auto payload() const -> const std::vector<std::uint8_t>& { return m_payload; }

   private:
    Value(Type _type, std::vector<std::uint8_t> _payload) : m_type(_type), m_payload(std::move(_payload)) {}

    template <typename T>
    static auto Scalar(Type _type, T _v) -> Value {
        std::vector<std::uint8_t> payload;
        payload.reserve(sizeof(T));
        AppendLE<T>(payload, _v);
        return Value(_type, std::move(payload));
    }

    Type m_type;
    std::vector<std::uint8_t> m_payload;
};

// A non-owning view of one channel's contiguous samples.
//
// LIFETIME: the referenced buffer must outlive every Segment this view is
// added to, and must not be modified while that segment is being written.
// Trivially copyable and trivially destructible on purpose.
struct RawView {
    Type type;
    const std::uint8_t* data;
    // Goes into the 64-bit "number of values" field verbatim, so it is
    // uint64_t rather than size_t: on the 32-bit ARM target size_t would
    // silently narrow it, which is what the old `long RawData::Count` did.
    std::uint64_t sampleCount;

    // Fits in size_t by construction - the samples are an object in this
    // process's address space.
    auto byteCount() const -> std::size_t { return static_cast<std::size_t>(sampleCount) * TypeSize(type); }
};

static_assert(std::is_trivially_copyable_v<RawView>);
static_assert(std::is_trivially_destructible_v<RawView>);
// Checked on every target, including the 32-bit ARM one where size_t is 4
// bytes: the sample count is a 64-bit field of the file format and must not
// depend on the host's pointer width, which is exactly what the old
// `long RawData::Count` did.
static_assert(sizeof(RawView::sampleCount) == 8);

}  // namespace rp_formatter_api::tdms

#endif
