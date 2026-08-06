#ifndef TDMS_LIB_FILESTRUCTTYPES_H
#define TDMS_LIB_FILESTRUCTTYPES_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "data_type.h"


namespace TDMS {

// Default-initialised on purpose: WriterSegment::GenerateGroup and
// GenerateChannel do not touch these flags, so without the initialisers every
// group and channel Metadata carried six indeterminate bools.
struct TableOfContents {
    bool HasMetaData = false;
    bool HasRawData = false;
    bool HasDaqMxData = false;
    bool RawDataIsInterleaved = false;
    bool NumbersAreBigEndian = false;
    bool ContainsNewObjects = false;
};

// Offsets are std::int64_t, not long: they hold the lead-in's 64-bit next-segment
// and raw-data offsets, and `long` is 4 bytes on the 32-bit ARM target, so the
// original declarations truncated any file past 2 GiB while looking correct on a
// 64-bit developer machine. Length is static constexpr so Segment stays
// copy-assignable (a const member deletes operator=).
struct Segment {
    static constexpr std::int64_t Length = 28;
    std::int64_t Offset = 0;
    std::int64_t MetadataOffset = 0;
    std::int64_t RawDataOffset = 0;
    std::int64_t NextSegmentOffset = 0;
    std::string Identifier;
    TDMS::TableOfContents TableOfContents;
    int Version = 0;
};

// Count is the on-disk uint64 value count and Size the byte size of the block;
// both were `long`, i.e. 32-bit on the ARM target.
struct RawData {
    std::int64_t Offset = 0;
    TDMS::DataType DataType;
    int Dimension = 0;
    std::int64_t Count = 0;
    std::int64_t Size = 0;
    bool IsInterleaved = false;
    std::int64_t InterleaveStride = 0;
};

struct Metadata {
    TDMS::TableOfContents TableOfContents;
    int Version = 0;
    std::string PathStr = "";
    std::vector<std::string> Path;
    TDMS::RawData RawData;
    std::map<std::string, DataType> Properties;
};

}  // namespace TDMS

#endif
