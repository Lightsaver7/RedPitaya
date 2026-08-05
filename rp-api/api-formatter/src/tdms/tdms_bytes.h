/**
 * Little-endian byte emission helpers for the TDMS writer.
 *
 * TDMS stores every numeric field little-endian unless the segment's
 * table-of-contents says otherwise (this writer never sets that bit). These
 * helpers make that explicit: the old code wrote integers with
 * write(reinterpret_cast<char*>(&x), sizeof x), which happens to be correct
 * on x86 and ARM little-endian but is silently wrong anywhere else and hides
 * the format decision inside a cast.
 *
 * The byte-order branch below is resolved at compile time and collapses to a
 * plain store on every target this library is built for.
 */

#ifndef __RP_TDMS_BYTES_H__
#define __RP_TDMS_BYTES_H__

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <string_view>
#include <type_traits>
#include <vector>

namespace rp_formatter_api::tdms {

// Object representation of _value in little-endian order.
template <typename T>
auto ToLE(T _value) -> std::array<std::uint8_t, sizeof(T)> {
    static_assert(std::is_trivially_copyable_v<T>, "only trivially copyable scalars are serialisable");
    std::array<std::uint8_t, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &_value, sizeof(T));
    if constexpr (std::endian::native == std::endian::big) {
        std::reverse(bytes.begin(), bytes.end());
    }
    return bytes;
}

template <typename T>
auto AppendLE(std::vector<std::uint8_t>& _out, T _value) -> void {
    const auto bytes = ToLE(_value);
    _out.insert(_out.end(), bytes.begin(), bytes.end());
}

auto inline AppendBytes(std::vector<std::uint8_t>& _out, const void* _data, std::size_t _size) -> void {
    const auto* first = static_cast<const std::uint8_t*>(_data);
    _out.insert(_out.end(), first, first + _size);
}

// A TDMS string: 4-byte little-endian length, then the raw bytes. Never
// NUL-terminated, and embedded NULs are preserved.
auto inline AppendString(std::vector<std::uint8_t>& _out, std::string_view _text) -> void {
    AppendLE<std::uint32_t>(_out, static_cast<std::uint32_t>(_text.size()));
    AppendBytes(_out, _text.data(), _text.size());
}

template <typename T>
auto WriteLE(std::ostream& _out, T _value) -> void {
    const auto bytes = ToLE(_value);
    _out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

auto inline WriteBytes(std::ostream& _out, const void* _data, std::size_t _size) -> void {
    _out.write(static_cast<const char*>(_data), static_cast<std::streamsize>(_size));
}

}  // namespace rp_formatter_api::tdms

#endif
