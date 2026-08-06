#ifndef TDMS_LIB_WRITER_H
#define TDMS_LIB_WRITER_H

#include <cstdint>
#include <memory>
#include "binary_stream.h"
#include "data_type.h"
#include "file_struct_types.h"

namespace TDMS {
class WriterSegment {
   public:
    auto AddProperties(std::shared_ptr<Metadata> metadata, std::string key, DataType value) -> void;
    auto AddRaw(std::shared_ptr<Metadata> metadata, TDMSType type, std::int64_t count, std::shared_ptr<std::uint8_t[]> rawData) -> void;
    auto LoadMetadata(std::vector<std::shared_ptr<Metadata>> data) -> void;
    auto IsRootNodePresent() -> bool;
    auto GenerateRoot() -> std::shared_ptr<Metadata>;
    auto GenerateGroup(std::string groupName) -> std::shared_ptr<Metadata>;
    auto GenerateChannel(std::string groupName, std::string channelName) -> std::shared_ptr<Metadata>;
    auto GetRoot() -> std::shared_ptr<Metadata>;
    auto GetNodes() -> std::vector<std::shared_ptr<Metadata>>;

   private:
    std::vector<std::shared_ptr<Metadata>> m_nodes;
};

class Writer {
   public:
    Writer(std::iostream& fileStream, bool append);
    auto Write(WriterSegment& segment) -> void;
    auto GetFileSize() -> std::uint64_t;

   private:
    auto WriteSegment(std::int64_t offset, std::shared_ptr<Metadata> leadin) -> void;
    auto WriteRawHeader(std::shared_ptr<Metadata> metadata) -> void;
    auto WriteNextSegmentAddress(std::int64_t offset, std::int64_t address) -> void;
    auto WriteRawSegmentAddress(std::int64_t offset, std::int64_t address) -> void;

    std::iostream* m_fileStream;
    bool m_is_append;
};
}  // namespace TDMS

#endif  //TDMS_LIB_WRITER_H
