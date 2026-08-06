#ifndef TDMS_LIB_READER_H
#define TDMS_LIB_READER_H

#include <cstdint>

#include "binary_stream.h"
#include "data_type.h"
#include "file_struct_types.h"


namespace TDMS {
class Reader {
   public:
    Reader(std::iostream& fileStream, std::uint64_t fileSize, bool showLog = false);
    ~Reader();
    auto GetFileSize() -> std::uint64_t;
    auto ReadFirstSegment() -> std::shared_ptr<Segment>;
    auto ReadSegment(std::uint64_t offset) -> std::shared_ptr<Segment>;
    auto ReadMetadata(std::shared_ptr<Segment> segment) -> std::vector<std::shared_ptr<Metadata>>;
    auto ReadRawData(RawData& rawData) -> std::vector<std::shared_ptr<DataType::Raw>>;
    auto ReadRawFixed(std::int64_t offset, std::int64_t count, TDMSType dataType) -> std::vector<std::shared_ptr<DataType::Raw>>;
    auto ReadRawInterleaved(std::int64_t offset, std::int64_t count, TDMSType dataType, std::int64_t interleaveSkip) -> std::vector<std::shared_ptr<DataType::Raw>>;
    auto ReadRawStrings(std::int64_t offset, std::int64_t count) -> std::vector<std::shared_ptr<DataType::Raw>>;

   private:
    std::iostream* m_fileStream;
    std::uint64_t m_fileSize;
    BinaryStream m_bstream;
    bool m_showLog;
};
}  // namespace TDMS

#endif
