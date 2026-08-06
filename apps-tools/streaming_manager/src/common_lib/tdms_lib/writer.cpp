#include "writer.h"
#include <iostream>

#define OFFSET_NEXT_SEGMENT 12
#define OFFSET_RAW_DATA OFFSET_NEXT_SEGMENT + 8

using namespace TDMS;
// The public headers no longer pull std into scope; do it here instead of
// qualifying every name in this implementation file.

auto WriterSegment::LoadMetadata(std::vector<std::shared_ptr<Metadata>> data) -> void {
    m_nodes = data;
}

auto WriterSegment::IsRootNodePresent() -> bool {
    return GetRoot() != nullptr;
}

auto WriterSegment::GetRoot() -> std::shared_ptr<Metadata> {
    for (auto& n : m_nodes) {
        if (n->Path.size() == 0)
            return n;
    }
    return nullptr;
}

auto WriterSegment::GetNodes() -> std::vector<std::shared_ptr<Metadata>> {
    return m_nodes;
}

auto WriterSegment::GenerateRoot() -> std::shared_ptr<Metadata> {
    auto metadata = std::make_shared<Metadata>();
    metadata->Version = 4713;  // This version of TDMS2.0
    metadata->PathStr = "/";
    metadata->TableOfContents.ContainsNewObjects = false;
    metadata->TableOfContents.HasDaqMxData = false;
    metadata->TableOfContents.HasMetaData = false;
    metadata->TableOfContents.HasRawData = false;
    metadata->TableOfContents.NumbersAreBigEndian = false;
    metadata->TableOfContents.RawDataIsInterleaved = false;
    return metadata;
}

auto WriterSegment::GenerateGroup(std::string groupName) -> std::shared_ptr<Metadata> {
    auto metadata = std::make_shared<Metadata>();
    metadata->PathStr = "/'" + groupName + "'";
    metadata->Path.push_back(groupName);
    return metadata;
}

auto WriterSegment::GenerateChannel(std::string groupName, std::string channelName) -> std::shared_ptr<Metadata> {
    auto metadata = std::make_shared<Metadata>();
    metadata->PathStr = "/'" + groupName + "'/'" + channelName + "'";
    metadata->Path.push_back(groupName);
    metadata->Path.push_back(channelName);
    return metadata;
}

auto WriterSegment::AddProperties(std::shared_ptr<Metadata> metadata, std::string key, DataType value) -> void {
    metadata->Properties[key] = value;
}

auto WriterSegment::AddRaw(std::shared_ptr<Metadata> metadata, TDMSType type, std::int64_t count, std::shared_ptr<std::uint8_t[]> rawData) -> void {
    if (type == TDMSType::String)
        throw std::invalid_argument("[ERROR] Set string raw data not implemented!");

    metadata->RawData.DataType.InitRaw(type, count, rawData);
    metadata->RawData.Count = count;
    metadata->RawData.Dimension = 1;
    metadata->RawData.IsInterleaved = false;
    metadata->RawData.Size = count * static_cast<std::int64_t>(metadata->RawData.DataType.GetLength());
    metadata->RawData.Offset = 0;
}

auto Writer::GetFileSize() -> std::uint64_t {
    m_fileStream->seekg(0, std::ios::beg);
    std::streampos fsize = 0;
    fsize = m_fileStream->tellg();
    m_fileStream->seekg(0, std::ios::end);
    fsize = m_fileStream->tellg() - fsize;
    m_fileStream->seekg(0, std::ios::beg);
    return fsize;
}

auto Writer::Write(WriterSegment& segment) -> void {
    auto root = segment.GetRoot();
    if (root == nullptr) {
        std::cerr << "[Error] No root metadata\n";
        return;
    }
    m_fileStream->seekp(0, std::ios::end);
    auto posSegmentBegin = m_fileStream->tellp();
    WriteSegment(static_cast<std::int64_t>(posSegmentBegin), root);
    auto nodes = segment.GetNodes();
    int32_t metadatacount = static_cast<int32_t>(nodes.size()) - 1;
    m_fileStream->write(reinterpret_cast<char*>(&metadatacount), sizeof(metadatacount));

    for (auto& n : nodes) {
        if (n != root) {
            int32_t path_len = static_cast<int32_t>(n->PathStr.size());
            m_fileStream->write(reinterpret_cast<char*>(&path_len), sizeof(path_len));
            m_fileStream->write(n->PathStr.data(), n->PathStr.size());
            WriteRawHeader(n);
            int32_t property_count = static_cast<int32_t>(n->Properties.size());
            m_fileStream->write(reinterpret_cast<char*>(&property_count), sizeof(property_count));
            for (const auto& kv : n->Properties) {
                int32_t key_len = static_cast<int32_t>(kv.first.size());
                m_fileStream->write(reinterpret_cast<char*>(&key_len), sizeof(key_len));
                m_fileStream->write(kv.first.data(), kv.first.size());
                const DataType& value = kv.second;
                uint32_t typeV = static_cast<uint32_t>(value.GetDataType());
                m_fileStream->write(reinterpret_cast<char*>(&typeV), sizeof(typeV));
                BinaryStream::Write(*m_fileStream, value);
            }
        }
    }

    if (root->TableOfContents.HasRawData) {
        auto posMetadataEnd = m_fileStream->tellp();
        WriteRawSegmentAddress(posSegmentBegin, posMetadataEnd - posSegmentBegin - 28);
    }

    for (auto& n : nodes) {
        if (n != root) {
            auto rawVector = n->RawData.DataType.GetRawVector();
            for (auto& r : rawVector) {
                m_fileStream->write((const char*)r->data.get(), r->size);
            }
        }
    }

    auto posSegmentEnd = m_fileStream->tellp();
    WriteNextSegmentAddress(posSegmentBegin, posSegmentEnd - posSegmentBegin - 28);
}

auto Writer::WriteRawHeader(std::shared_ptr<Metadata> metadata) -> void {
    if (metadata->RawData.DataType.GetDataType() == TDMSType::String)
        throw std::invalid_argument("[ERROR] Save string raw data not implemented!");
    if (metadata->RawData.IsInterleaved)
        throw std::invalid_argument("[ERROR] Interleaved raw data not implemented!");

    auto rawVector = metadata->RawData.DataType.GetRawVector();
    if (rawVector.size() == 0) {
        // Write INDEX of raw header
        int32_t raw_index = -1;
        m_fileStream->write(reinterpret_cast<char*>(&raw_index), sizeof(raw_index));
        return;
    } else {
        // Write INDEX of raw header
        int32_t raw_index = 20;
        m_fileStream->write(reinterpret_cast<char*>(&raw_index), sizeof(raw_index));

        // Write data type of raw header
        TDMSType raw_datatype = metadata->RawData.DataType.GetDataType();
        uint32_t typeV = static_cast<uint32_t>(raw_datatype);
        m_fileStream->write(reinterpret_cast<char*>(&typeV), sizeof(typeV));

        // Write demension type of raw header (for ver 2.0, 1 is the only valid value)
        int32_t raw_demension = 1;
        m_fileStream->write(reinterpret_cast<char*>(&raw_demension), sizeof(raw_demension));

        // Write count values
        uint64_t raw_count_values = static_cast<uint64_t>(metadata->RawData.Count);
        m_fileStream->write(reinterpret_cast<char*>(&raw_count_values), sizeof(raw_count_values));
    }
}

Writer::Writer(std::iostream& fileStream, bool append) {
    m_fileStream = &fileStream;
    m_is_append = append;
}

// Full lenght 28 bytes;

auto Writer::WriteSegment(std::int64_t offset, std::shared_ptr<Metadata> leadin) -> void {
    if (leadin->TableOfContents.RawDataIsInterleaved)
        throw std::invalid_argument("[ERROR] Interleaved data not implemented!");
    if (leadin->TableOfContents.HasDaqMxData)
        throw std::invalid_argument("[ERROR] DaqMx data not implemented!");

    m_fileStream->seekp(offset, std::ios::beg);
    m_fileStream->write("TDSm", 4);
    int32_t tableOfContentsMask = 0;
    if (leadin->TableOfContents.ContainsNewObjects)
        tableOfContentsMask |= 1 << 2;
    if (leadin->TableOfContents.HasDaqMxData)
        tableOfContentsMask |= 1 << 7;
    if (leadin->TableOfContents.HasMetaData)
        tableOfContentsMask |= 1 << 1;
    if (leadin->TableOfContents.HasRawData)
        tableOfContentsMask |= 1 << 3;
    if (leadin->TableOfContents.NumbersAreBigEndian)
        tableOfContentsMask |= 1 << 6;
    if (leadin->TableOfContents.RawDataIsInterleaved)
        tableOfContentsMask |= 1 << 5;

    m_fileStream->write(reinterpret_cast<char*>(&tableOfContentsMask), sizeof(tableOfContentsMask));
    int32_t version = leadin->Version;
    m_fileStream->write(reinterpret_cast<char*>(&version), sizeof(version));
    int64_t nextsegment = -1;
    m_fileStream->write(reinterpret_cast<char*>(&nextsegment), sizeof(nextsegment));
    int64_t data_offset = 0;
    m_fileStream->write(reinterpret_cast<char*>(&data_offset), sizeof(data_offset));
}

// Both back-patches are strictly scoped: save the put position, seek, write one
// field, seek back. The original kept two std::vector<pos_type> stacks and four
// push/pop methods for this, of which the "g" pair was never called at all.
auto Writer::WriteNextSegmentAddress(std::int64_t offset, std::int64_t address) -> void {
    const std::ios::pos_type resume = m_fileStream->tellp();
    m_fileStream->seekp(offset + OFFSET_NEXT_SEGMENT, std::ios::beg);
    m_fileStream->write(reinterpret_cast<const char*>(&address), sizeof(address));
    m_fileStream->seekp(resume);
}

auto Writer::WriteRawSegmentAddress(std::int64_t offset, std::int64_t address) -> void {
    const std::ios::pos_type resume = m_fileStream->tellp();
    m_fileStream->seekp(offset + OFFSET_RAW_DATA, std::ios::beg);
    m_fileStream->write(reinterpret_cast<const char*>(&address), sizeof(address));
    m_fileStream->seekp(resume);
}
