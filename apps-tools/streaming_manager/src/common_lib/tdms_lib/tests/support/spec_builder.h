/**
 * A little-endian byte-array builder used to state TDMS byte expectations
 * declaratively.
 *
 * This deliberately shares NO code with TDMS::BinaryStream. The library has no
 * independent reference implementation to check against (there is no Python in
 * this test suite), so the goldens are derived by hand from the TDMS 2.0
 * specification using this builder, and the reader is then validated against
 * those same hand-derived bytes rather than against the library's own writer.
 * A round trip through a bug shared by writer and reader would pass; a pinned
 * byte pattern will not.
 */

#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace tdms_test {

using Bytes = std::vector<std::uint8_t>;

// TDMS type codes, spelled out here independently of the library's enum so a
// change to the enum cannot silently move a golden.
enum : std::uint32_t {
    kTypeVoid = 0x00,
    kTypeI8 = 0x01,
    kTypeI16 = 0x02,
    kTypeI32 = 0x03,
    kTypeI64 = 0x04,
    kTypeU8 = 0x05,
    kTypeU16 = 0x06,
    kTypeU32 = 0x07,
    kTypeU64 = 0x08,
    kTypeF32 = 0x09,
    kTypeF64 = 0x0A,
    kTypeString = 0x20,
    kTypeBool = 0x21,
    kTypeTimeStamp = 0x44,
};

// Table-of-contents bits.
enum : std::int32_t {
    kTocMetaData = 1 << 1,
    kTocNewObjList = 1 << 2,
    kTocRawData = 1 << 3,
    kTocInterleaved = 1 << 5,
    kTocBigEndian = 1 << 6,
    kTocDaqMx = 1 << 7,
};

constexpr std::int32_t kTdms2Version = 4713;
constexpr std::size_t kLeadInSize = 28;
constexpr std::uint32_t kNoRawDataIndex = 0xFFFFFFFFu;
constexpr std::uint32_t kFixedRawDataIndexLength = 20;
constexpr std::uint32_t kStringRawDataIndexLength = 28;

class Builder {
   public:
    template <typename T>
    auto LE(T value) -> Builder& {
        static_assert(std::is_trivially_copyable<T>::value, "LE requires a trivially copyable type");
        std::uint8_t raw[sizeof(T)];
        std::memcpy(raw, &value, sizeof(T));
        // The host is little-endian on every target this library builds for;
        // assert rather than silently produce the wrong expectation.
        const std::uint16_t probe = 0x0201;
        std::uint8_t probeBytes[2];
        std::memcpy(probeBytes, &probe, 2);
        if (probeBytes[0] != 0x01) {
            std::abort();
        }
        // Appended byte by byte rather than with insert(): gcc 13 emits a
        // spurious -Wstringop-overflow for the inlined memmove of a small
        // fixed-size array into a vector.
        for (std::size_t i = 0; i < sizeof(T); i++) {
            m_bytes.push_back(raw[i]);
        }
        return *this;
    }

    auto U8(std::uint8_t v) -> Builder& { return LE<std::uint8_t>(v); }
    auto I8(std::int8_t v) -> Builder& { return LE<std::int8_t>(v); }
    auto U16(std::uint16_t v) -> Builder& { return LE<std::uint16_t>(v); }
    auto I16(std::int16_t v) -> Builder& { return LE<std::int16_t>(v); }
    auto U32(std::uint32_t v) -> Builder& { return LE<std::uint32_t>(v); }
    auto I32(std::int32_t v) -> Builder& { return LE<std::int32_t>(v); }
    auto U64(std::uint64_t v) -> Builder& { return LE<std::uint64_t>(v); }
    auto I64(std::int64_t v) -> Builder& { return LE<std::int64_t>(v); }
    auto F32(float v) -> Builder& { return LE<float>(v); }
    auto F64(double v) -> Builder& { return LE<double>(v); }

    // Raw characters, no length prefix and no NUL.
    auto Raw(const std::string& text) -> Builder& {
        m_bytes.insert(m_bytes.end(), text.begin(), text.end());
        return *this;
    }

    auto Raw(const Bytes& bytes) -> Builder& {
        m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
        return *this;
    }

    // A TDMS string: 4-byte little-endian length, then the characters.
    auto PString(const std::string& text) -> Builder& {
        U32(static_cast<std::uint32_t>(text.size()));
        return Raw(text);
    }

    // The 28-byte segment lead-in. Both offsets are relative to the end of the
    // lead-in, as in the file format.
    auto LeadIn(std::int32_t tocMask, std::int64_t nextSegmentOffset, std::int64_t rawDataOffset,
                std::int32_t version = kTdms2Version) -> Builder& {
        Raw(std::string("TDSm"));
        I32(tocMask);
        I32(version);
        I64(nextSegmentOffset);
        I64(rawDataOffset);
        return *this;
    }

    // A metadata record for an object that carries no raw data in this segment.
    auto ObjectNoRaw(const std::string& path, std::uint32_t propertyCount) -> Builder& {
        PString(path);
        U32(kNoRawDataIndex);
        U32(propertyCount);
        return *this;
    }

    // A metadata record for a channel with a fixed-width raw data index.
    auto ObjectWithRaw(const std::string& path, std::uint32_t dataType, std::uint64_t valueCount,
                       std::uint32_t propertyCount, std::uint32_t dimension = 1) -> Builder& {
        PString(path);
        U32(kFixedRawDataIndexLength);
        U32(dataType);
        U32(dimension);
        U64(valueCount);
        U32(propertyCount);
        return *this;
    }

    // A metadata record for a String channel, whose index carries an extra
    // 8-byte total size field (index length 28).
    auto ObjectWithStringRaw(const std::string& path, std::uint64_t valueCount, std::uint64_t totalBytes,
                             std::uint32_t propertyCount, std::uint32_t dimension = 1) -> Builder& {
        PString(path);
        U32(kStringRawDataIndexLength);
        U32(kTypeString);
        U32(dimension);
        U64(valueCount);
        U64(totalBytes);
        U32(propertyCount);
        return *this;
    }

    // key, type code, then the payload the caller appends via the returned ref.
    auto PropertyHeader(const std::string& key, std::uint32_t typeCode) -> Builder& {
        PString(key);
        U32(typeCode);
        return *this;
    }

    auto size() const -> std::size_t { return m_bytes.size(); }
    auto bytes() const -> const Bytes& { return m_bytes; }
    auto str() const -> std::string { return std::string(m_bytes.begin(), m_bytes.end()); }

   private:
    Bytes m_bytes;
};

}  // namespace tdms_test
