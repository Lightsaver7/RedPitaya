#pragma once
#include <cstdint>
#include <fstream>
#include <memory>
#include <map>
#include <string>
#include <vector>

#include "file_struct_types.h"
#include "reader.h"
#include "writer.h"


namespace TDMS {
class File {
   public:
    File();
    ~File();

    auto ReadFile(std::string m_fileName) -> std::vector<std::shared_ptr<Metadata>>;
    auto ReadFileWithoutClose(std::string m_fileName) -> std::vector<std::shared_ptr<Segment>>;
    auto Close() -> bool;
    auto GetMetadata(std::shared_ptr<Segment>) -> std::vector<std::shared_ptr<Metadata>>;
    auto WriteFile(std::string m_fileName, WriterSegment& segment, bool Append) -> void;
    auto WriteMemory(std::iostream& stream, WriterSegment& segment) -> void;
    auto Print(std::vector<std::shared_ptr<Metadata>>& data, bool PrintRaw, long limitData) -> void;
    auto clearPrevMetadata() -> void;

   private:
    auto LoadMetadata(Reader& reader) -> std::vector<std::shared_ptr<Metadata>>;
    auto GetSegments(Reader& reader) -> std::vector<std::shared_ptr<Segment>>;
    auto GetMetadataItem(Reader& reader, std::shared_ptr<Segment> segment,
                         std::map<std::string, std::map<std::string, std::shared_ptr<Metadata>>>& prevMetaDataLookup) -> std::vector<std::shared_ptr<Metadata>>;

    std::fstream m_read_fs;
    // unique_ptr, not a raw pointer: Close() used to `delete m_reader` without
    // nulling it and ~File calls Close() again, so an explicit Close() followed
    // by destruction was a double free.
    std::unique_ptr<Reader> m_reader;
    std::map<std::string, std::map<std::string, std::shared_ptr<Metadata>>> m_prevMetaDataLookup;
};
}  // namespace TDMS
