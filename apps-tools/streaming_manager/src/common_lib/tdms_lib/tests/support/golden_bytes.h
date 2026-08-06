/**
 * Frozen golden byte arrays for the TDMS segment layouts the tests use.
 *
 * Every golden here was derived BY HAND from the TDMS 2.0 specification using
 * tests/support/spec_builder.h, and cross-checked once against the output of
 * the library as received before any refactoring began. Both agreed, which is
 * what makes them usable as an oracle in a suite with no third-party reader.
 *
 * The layout of a segment, for reference while reading the builders below:
 *
 *   lead-in, 28 bytes
 *     +0   "TDSm"
 *     +4   int32  table-of-contents mask
 *     +8   int32  version (4713 = TDMS 2.0)
 *     +12  int64  next segment offset, relative to the end of the lead-in
 *     +20  int64  raw data offset,     relative to the end of the lead-in
 *   metadata section, starting at +28
 *     uint32 objectCount
 *     per object:
 *       uint32 pathLength, path bytes
 *       uint32 rawDataIndex : 0xFFFFFFFF none, 20 fixed-width, 28 string
 *         if 20: uint32 dataType, uint32 dimension, uint64 valueCount
 *         if 28: uint32 dataType, uint32 dimension, uint64 valueCount, uint64 totalBytes
 *       uint32 propertyCount
 *       per property: uint32 keyLength, key bytes, uint32 typeCode, payload
 *   raw data, immediately after the metadata section, in object order
 *
 * NOTE on what these goldens deliberately encode about the CURRENT writer,
 * because byte compatibility is a hard requirement:
 *   - the root object "/" is NOT emitted (objectCount is nodes.size() - 1 and
 *     every loop skips the root), so a group + one channel gives objectCount 2;
 *   - the ContainsNewObjects bit (1 << 2) is never set;
 *   - properties are emitted in ascending key order (std::map);
 *   - group and channel names are not quote-escaped.
 * None of those is spec-perfect; all of them are what the writer does today.
 */

#pragma once

#include <cstdint>
#include <string>

#include "spec_builder.h"

namespace tdms_test {
namespace golden {

// The reference segment: group "Group" carrying one UInt64 property, plus
// channel "CH1" carrying four float32 samples.
//
//   metadata = 4 (objectCount)
//            + 4 + 8  (path "/'Group'")        + 4 (no raw index) + 4 (propCount)
//            + 4 + 8  (key "osc_rate")         + 4 (type U64)     + 8 (value)
//            + 4 + 14 (path "/'Group'/'CH1'")  + 4 (index len 20) + 4 (type F32)
//                                              + 4 (dimension)    + 8 (count)
//            + 4 (propCount)
//            = 90
//   raw      = 4 * 4 = 16
//   next segment offset = 90 + 16 = 106,  raw data offset = 90
//   total file size     = 28 + 106 = 134
inline auto ReferenceMetadata() -> Bytes {
    Builder meta;
    meta.U32(2);
    meta.ObjectNoRaw("/'Group'", 1);
    meta.PropertyHeader("osc_rate", kTypeU64);
    meta.U64(125000000ull);
    meta.ObjectWithRaw("/'Group'/'CH1'", kTypeF32, 4, 0);
    return meta.bytes();
}

inline auto ReferenceRawData() -> Bytes {
    Builder raw;
    raw.F32(1.f).F32(2.f).F32(3.f).F32(4.f);
    return raw.bytes();
}

inline auto Reference() -> Bytes {
    const Bytes meta = ReferenceMetadata();
    const Bytes raw = ReferenceRawData();
    Builder file;
    file.LeadIn(kTocMetaData | kTocRawData, static_cast<std::int64_t>(meta.size() + raw.size()),
                static_cast<std::int64_t>(meta.size()));
    file.Raw(meta).Raw(raw);
    return file.bytes();
}

constexpr std::size_t kReferenceSize = 134;
constexpr std::int64_t kReferenceMetadataLength = 90;
constexpr std::int64_t kReferenceRawLength = 16;
constexpr std::uint64_t kReferenceOscRate = 125000000ull;

// Root + group only, no channel and no raw data. Pins that the writer leaves
// the raw-data-offset field at 0 when HasRawData is clear (it only patches that
// field under `if (root->TableOfContents.HasRawData)`).
inline auto GroupOnly() -> Bytes {
    Builder meta;
    meta.U32(1);
    meta.ObjectNoRaw("/'Group'", 0);

    Builder file;
    file.LeadIn(kTocMetaData, static_cast<std::int64_t>(meta.size()), 0);
    file.Raw(meta.bytes());
    return file.bytes();
}

// Two contiguous int32 channels, 3 samples each. The raw block is channel A's
// three samples followed by channel B's three - not interleaved.
inline auto TwoChannelsContiguous() -> Bytes {
    Builder meta;
    meta.U32(3);
    meta.ObjectNoRaw("/'G'", 0);
    meta.ObjectWithRaw("/'G'/'A'", kTypeI32, 3, 0);
    meta.ObjectWithRaw("/'G'/'B'", kTypeI32, 3, 0);

    Builder raw;
    raw.I32(10).I32(11).I32(12);
    raw.I32(20).I32(21).I32(22);

    Builder file;
    file.LeadIn(kTocMetaData | kTocRawData, static_cast<std::int64_t>(meta.size() + raw.size()),
                static_cast<std::int64_t>(meta.size()));
    file.Raw(meta.bytes()).Raw(raw.bytes());
    return file.bytes();
}

// Two interleaved int32 channels, 3 samples each: A0 B0 A1 B1 A2 B2.
// Stride is 8 bytes; channel A sits at intra-stride offset 0, channel B at 4.
inline auto TwoChannelsInterleaved() -> Bytes {
    Builder meta;
    meta.U32(2);
    meta.ObjectWithRaw("/'G'/'A'", kTypeI32, 3, 0);
    meta.ObjectWithRaw("/'G'/'B'", kTypeI32, 3, 0);

    Builder raw;
    raw.I32(10).I32(20);
    raw.I32(11).I32(21);
    raw.I32(12).I32(22);

    Builder file;
    file.LeadIn(kTocMetaData | kTocRawData | kTocInterleaved, static_cast<std::int64_t>(meta.size() + raw.size()),
                static_cast<std::int64_t>(meta.size()));
    file.Raw(meta.bytes()).Raw(raw.bytes());
    return file.bytes();
}

// A segment whose objects are ALL channels (no group object), with a raw region
// holding TWO chunks of samples under one metadata section. This is the
// incremental-metadata shape from NI's white paper, and it is the exact input
// that makes File::GetMetadataItem take its implicit-metadata branch.
inline auto IncrementalMetadata() -> Bytes {
    Builder meta;
    meta.U32(1);
    meta.ObjectWithRaw("/'G'/'A'", kTypeF32, 4, 0);

    Builder raw;
    raw.F32(1.f).F32(2.f).F32(3.f).F32(4.f);
    raw.F32(5.f).F32(6.f).F32(7.f).F32(8.f);

    Builder file;
    file.LeadIn(kTocMetaData | kTocRawData, static_cast<std::int64_t>(meta.size() + raw.size()),
                static_cast<std::int64_t>(meta.size()));
    file.Raw(meta.bytes()).Raw(raw.bytes());
    return file.bytes();
}

// A segment that advertises raw data but NOT metadata - the normal shape of a
// LabVIEW continuation segment. Its sample bytes must never be parsed as an
// object count.
inline auto RawDataOnlySegment() -> Bytes {
    Builder raw;
    for (int i = 0; i < 8; i++) {
        raw.U32(0x41424344u);
    }

    Builder file;
    file.LeadIn(kTocRawData, static_cast<std::int64_t>(raw.size()), 0);
    file.Raw(raw.bytes());
    return file.bytes();
}

}  // namespace golden
}  // namespace tdms_test
