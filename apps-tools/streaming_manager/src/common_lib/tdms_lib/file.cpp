#include "file.h"

using namespace TDMS;
// The public headers no longer pull std into scope; do it here instead of
// qualifying every name in this implementation file.

template <typename T, typename Key>
bool key_exists(const T& container, const Key& key) {
    return (container.find(key) != std::end(container));
}

File::File() : m_read_fs(), m_reader(nullptr) {}

File::~File() {
    Close();
}

auto File::Print(std::vector<std::shared_ptr<Metadata>>& data, bool PrintRaw, long limitData) -> void {
    for (auto& m : data) {
        std::cout << "Path: " << m->PathStr << std::endl;
        std::cout << "\tProperties:" << m->Properties.size() << std::endl;
        for (auto& p : m->Properties) {
            std::cout << "\t\tKey: " << p.first << "\tValue:" << p.second.ToString() << std::endl;
        }
        std::cout << "\tRaw Data:" << m->RawData.Size << std::endl;
        if (m->RawData.Size > 0) {
            std::cout << "\t\t- Type:" << m->RawData.DataType.ToTypeString() << std::endl;
            std::cout << "\t\t- Count:" << m->RawData.Count << std::endl;
            std::cout << "\t\t- IsInterleaved:" << m->RawData.IsInterleaved << std::endl;
            std::cout << "\t\t- Dimension:" << m->RawData.Dimension << std::endl;
            std::cout << "\t\t- InterleaveStride:" << m->RawData.InterleaveStride << std::endl;
            std::cout << "\t\t- Offset:" << m->RawData.Offset << std::endl;
            if (PrintRaw) {
                std::cout << "\t\t\tRAW DATA:" << std::endl;
                m->RawData.DataType.PrintVector(limitData);
            }
        }
    }
}

auto File::clearPrevMetadata() -> void {
    m_prevMetaDataLookup.clear();
}

auto File::ReadFile(std::string m_fileName) -> std::vector<std::shared_ptr<Metadata>> {
    std::fstream ifs;
    ifs.open(m_fileName, std::ios::binary | std::ifstream::in);
    if (ifs.fail()) {
        std::cout << "File " << m_fileName << " not exist" << std::endl;
        return std::vector<std::shared_ptr<Metadata>>();
    }
    ifs.seekg(0, std::ios::beg);
    std::streampos fsize = 0;
    fsize = ifs.tellg();
    ifs.seekg(0, std::ios::end);
    fsize = ifs.tellg() - fsize;
    ifs.seekg(0, std::ios::beg);
    Reader reader(ifs, fsize);
    auto metadata = LoadMetadata(reader);
    ifs.close();
    return metadata;
}

auto File::ReadFileWithoutClose(std::string m_fileName) -> std::vector<std::shared_ptr<Segment>> {
    if (m_read_fs.is_open())
        m_read_fs.close();
    // Reset before opening: the failure path below used to return with the old
    // Reader already deleted but still pointed at.
    m_reader.reset();
    m_read_fs.clear();
    m_prevMetaDataLookup.clear();
    m_read_fs.open(m_fileName, std::ios::binary | std::ifstream::in);
    if (m_read_fs.fail()) {
        std::cout << "File " << m_fileName << " not exist" << std::endl;
        return std::vector<std::shared_ptr<Segment>>();
    }

    m_read_fs.seekg(0, std::ios::beg);
    std::streampos fsize = 0;
    fsize = m_read_fs.tellg();
    m_read_fs.seekg(0, std::ios::end);
    fsize = m_read_fs.tellg() - fsize;
    m_read_fs.seekg(0, std::ios::beg);
    m_reader = std::make_unique<Reader>(m_read_fs, static_cast<std::uint64_t>(fsize), false);
    return GetSegments(*m_reader);
}

auto File::GetMetadata(std::shared_ptr<Segment> segment) -> std::vector<std::shared_ptr<Metadata>> {
    // m_reader is null before ReadFileWithoutClose, after Close, and when the
    // caller used ReadFile (which builds a local Reader); all three used to be a
    // null dereference here.
    if (m_reader == nullptr || segment == nullptr)
        return std::vector<std::shared_ptr<Metadata>>();
    return GetMetadataItem(*m_reader, segment, m_prevMetaDataLookup);
}

auto File::Close() -> bool {
    m_prevMetaDataLookup.clear();
    m_reader.reset();
    if (m_read_fs.is_open()) {
        m_read_fs.close();
        return true;
    }
    return false;
}

auto File::WriteFile(std::string m_fileName, WriterSegment& segment, bool Append) -> void {
    std::fstream ifs;
    ifs.open(m_fileName, std::ios::binary | std::ofstream::out | std::ofstream::in | (Append ? std::ofstream::binary : std::ofstream::trunc));
    if (ifs.fail()) {
        ifs.open(m_fileName, std::ios::binary | std::ofstream::out | std::ofstream::in | std::ofstream::trunc);
        if (ifs.fail()) {
            std::cout << "File " << m_fileName << " not exist" << std::endl;
            return;
        }
    }
    Writer writer(ifs, Append);
    writer.Write(segment);
    ifs.close();
}

auto File::WriteMemory(std::iostream& stream, WriterSegment& segment) -> void {
    Writer writer(stream, true);
    writer.Write(segment);
}

auto File::LoadMetadata(Reader& reader) -> std::vector<std::shared_ptr<Metadata>> {
    std::vector<std::shared_ptr<Segment>> segments = GetSegments(reader);
    std::vector<std::shared_ptr<Metadata>> metadataRet;
    std::map<std::string, std::map<std::string, std::shared_ptr<Metadata>>> prevMetaDataLookup;
    for (auto& segment : segments) {
        if (!(segment->TableOfContents.ContainsNewObjects || segment->TableOfContents.HasDaqMxData || segment->TableOfContents.HasMetaData ||
              segment->TableOfContents.HasRawData)) {
            continue;
        }
        auto metadata = GetMetadataItem(reader, segment, prevMetaDataLookup);
        metadataRet.insert(metadataRet.end(), metadata.begin(), metadata.end());
    }
    return metadataRet;
}

auto File::GetMetadataItem(Reader& reader, std::shared_ptr<Segment> segment,
                           std::map<std::string, std::map<std::string, std::shared_ptr<Metadata>>>& prevMetaDataLookup) -> std::vector<std::shared_ptr<Metadata>> {
    std::vector<std::shared_ptr<Metadata>> metadataRet;
    std::vector<std::shared_ptr<Metadata>> metadatas = reader.ReadMetadata(segment);
    std::int64_t rawDataSize = 0;
    std::int64_t nextOffset = segment->RawDataOffset;
    for (auto& metadata : metadatas) {
        if (metadata->RawData.Count == 0 && metadata->Path.size() > 1) {
            // apply previous metadata if available
            auto prevMetadataPair = prevMetaDataLookup.find(metadata->Path[0]);
            if (prevMetadataPair != prevMetaDataLookup.end()) {
                auto prevMetadataMap = prevMetadataPair->second;
                auto prevMetaDataPair2 = prevMetadataMap.find(metadata->Path[1]);
                if (prevMetaDataPair2 != prevMetadataMap.end()) {
                    auto prevMetaData = prevMetaDataPair2->second;

                    metadata->RawData.Count = segment->TableOfContents.HasRawData ? prevMetaData->RawData.Count : 0;
                    metadata->RawData.DataType = prevMetaData->RawData.DataType;
                    metadata->RawData.Offset = segment->RawDataOffset + rawDataSize;
                    metadata->RawData.IsInterleaved = prevMetaData->RawData.IsInterleaved;
                    metadata->RawData.InterleaveStride = prevMetaData->RawData.InterleaveStride;
                    metadata->RawData.Size = prevMetaData->RawData.Size;
                    metadata->RawData.Dimension = prevMetaData->RawData.Dimension;
                }
            }
        }
        if (metadata->RawData.IsInterleaved && segment->NextSegmentOffset <= 0) {
            metadata->RawData.Count = segment->NextSegmentOffset > 0
                                          ? (segment->NextSegmentOffset - metadata->RawData.Offset + metadata->RawData.InterleaveStride - 1) / metadata->RawData.InterleaveStride
                                          : (reader.GetFileSize() - metadata->RawData.Offset + metadata->RawData.InterleaveStride - 1) / metadata->RawData.InterleaveStride;
        }
        if (metadata->Path.size() > 1) {
            rawDataSize += metadata->RawData.Size;
            nextOffset += metadata->RawData.Size;
        }
    }
    std::vector<std::shared_ptr<Metadata>> implicitMetadatas;
    // NOTE: this predicate looks at EVERY object, including a group (which never
    // has raw data), so an ordinary group+channel segment produces no implicit
    // records. That is the current behaviour and it is preserved deliberately;
    // restricting the predicate to channel objects is spec-correct but changes
    // what ReadFile returns for ordinary files.
    bool Check = !metadatas.empty();
    for (auto& metadata : metadatas) {
        if (!(!metadata->RawData.IsInterleaved && metadata->RawData.Size > 0))
            Check = false;
    }
    if (Check && segment->TableOfContents.HasRawData) {
        // Bytes one full chunk of every channel consumes. If this is zero the
        // loop below cannot advance, which is how a segment with no channels - or
        // with zero-sample channels - used to spin forever.
        std::int64_t chunkSize = 0;
        for (auto& metadata : metadatas) {
            if (metadata->Path.size() > 1)
                chunkSize += metadata->RawData.Size;
        }
        const std::int64_t rawEnd = segment->NextSegmentOffset > 0 ? segment->NextSegmentOffset : static_cast<std::int64_t>(reader.GetFileSize());
        if (chunkSize > 0) {
            while (nextOffset + chunkSize <= rawEnd) {
                // Incremental Meta Data see http://www.ni.com/white-paper/5696/en/#toc1
                for (auto& metadata : metadatas) {
                    if (metadata->Path.size() > 1) {
                        // make_shared, not a default-constructed shared_ptr: the
                        // original created a NULL pointer here and dereferenced
                        // it on the next line, so reaching this branch was a
                        // guaranteed segfault.
                        auto implicitMetadata = std::make_shared<Metadata>();
                        implicitMetadata->Path = metadata->Path;
                        // These four were dropped entirely, which left every
                        // implicit record with an empty path when printed.
                        implicitMetadata->PathStr = metadata->PathStr;
                        implicitMetadata->TableOfContents = metadata->TableOfContents;
                        implicitMetadata->Version = metadata->Version;
                        implicitMetadata->RawData.InterleaveStride = metadata->RawData.InterleaveStride;
                        implicitMetadata->RawData.Count = metadata->RawData.Count;
                        implicitMetadata->RawData.DataType = metadata->RawData.DataType;
                        implicitMetadata->RawData.Offset = nextOffset;
                        implicitMetadata->RawData.IsInterleaved = metadata->RawData.IsInterleaved;
                        implicitMetadata->RawData.Size = metadata->RawData.Size;
                        implicitMetadata->RawData.Dimension = metadata->RawData.Dimension;
                        implicitMetadata->Properties = metadata->Properties;
                        implicitMetadatas.push_back(implicitMetadata);
                        nextOffset += implicitMetadata->RawData.Size;
                    }
                }
            }
        }
    }

    std::vector<std::shared_ptr<Metadata>> metadataWithImplicit;

    metadataWithImplicit.insert(std::end(metadataWithImplicit), std::begin(metadatas), std::end(metadatas));
    metadataWithImplicit.insert(std::end(metadataWithImplicit), std::begin(implicitMetadatas), std::end(implicitMetadatas));

    for (auto& metadata : metadataWithImplicit) {
        if (metadata->Path.size() == 2) {
            if (!key_exists<std::map<std::string, std::map<std::string, std::shared_ptr<Metadata>>>, std::string>(prevMetaDataLookup, metadata->Path[0])) {
                auto pair_data = std::pair<std::string, std::map<std::string, std::shared_ptr<Metadata>>>(metadata->Path[0], std::map<std::string, std::shared_ptr<Metadata>>());
                prevMetaDataLookup.insert(pair_data);
            }
            prevMetaDataLookup[metadata->Path[0]][metadata->Path[1]] = metadata;
        }
        metadataRet.push_back(metadata);
    }
    return metadataRet;
}

std::vector<std::shared_ptr<Segment>> File::GetSegments(Reader& reader) {
    std::vector<std::shared_ptr<Segment>> list;
    auto segment = reader.ReadFirstSegment();
    while (segment != nullptr) {
        list.push_back(segment);
        const std::int64_t next = segment->NextSegmentOffset;
        // The offset must move forward. It came from the file, so a malformed or
        // hostile one could point at this segment or before it, and this loop had
        // no requirement that it advance at all.
        if (next <= segment->Offset)
            break;
        segment = reader.ReadSegment(static_cast<std::uint64_t>(next));
    }
    return list;
}
