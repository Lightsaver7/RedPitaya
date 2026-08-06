#include "reader.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <regex>

using namespace TDMS;
// The public headers no longer pull std into scope; do it here instead of
// qualifying every name in this implementation file.

Reader::~Reader() {}

Reader::Reader(std::iostream& fileStream, uint64_t fileSize, bool showLog) {
    m_fileStream = &fileStream;
    m_fileSize = fileSize;
    m_showLog = showLog;
}

std::uint64_t Reader::GetFileSize() {
    return m_fileSize;
}

auto Reader::ReadFirstSegment() -> std::shared_ptr<Segment> {
    return ReadSegment(0);
}

auto Reader::ReadSegment(std::uint64_t offset) -> std::shared_ptr<Segment> {
    if (offset >= m_fileSize)
        return nullptr;
    // A lead-in is 28 bytes; anything shorter cannot be a segment.
    if (m_fileSize - offset < static_cast<std::uint64_t>(Segment::Length))
        return nullptr;

    m_fileStream->clear();
    m_fileStream->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    auto leadin = std::make_shared<Segment>();
    leadin->Offset = static_cast<std::int64_t>(offset);
    leadin->MetadataOffset = static_cast<std::int64_t>(offset) + leadin->Length;
    DataType ident = BinaryStream::ReadString(*m_fileStream, 4);
    leadin->Identifier = ident.GetDataString();
    // The tag used to be stored and never compared, so any file at all was
    // parsed as a segment with whatever offsets its bytes happened to encode.
    if (leadin->Identifier != "TDSm") {
        if (m_showLog)
            std::cout << "Not a TDMS segment at offset " << offset << "\n";
        return nullptr;
    }
    uint32_t tableOfContentsMask = m_bstream.Read<uint32_t>(*m_fileStream, TDMSType::UnsignedInteger32);

    leadin->TableOfContents.ContainsNewObjects = ((tableOfContentsMask >> 2) & 1) == 1;
    leadin->TableOfContents.HasDaqMxData = ((tableOfContentsMask >> 7) & 1) == 1;
    leadin->TableOfContents.HasMetaData = ((tableOfContentsMask >> 1) & 1) == 1;
    leadin->TableOfContents.HasRawData = ((tableOfContentsMask >> 3) & 1) == 1;
    leadin->TableOfContents.NumbersAreBigEndian = ((tableOfContentsMask >> 6) & 1) == 1;
    leadin->TableOfContents.RawDataIsInterleaved = ((tableOfContentsMask >> 5) & 1) == 1;

    leadin->Version = BinaryStream::Read<int32_t>(*m_fileStream, TDMSType::Integer32);

    const std::int64_t segmentBody = static_cast<std::int64_t>(offset) + leadin->Length;
    std::int64_t nextsegment = BinaryStream::Read<std::int64_t>(*m_fileStream, TDMSType::Integer64);
    // A relative offset that would land outside the file means "read to the end"
    // (an incomplete segment); a negative one is malformed.
    if (nextsegment < 0 || nextsegment > static_cast<std::int64_t>(m_fileSize) - segmentBody)
        nextsegment = -1;
    if (nextsegment != -1)
        nextsegment += segmentBody;
    leadin->NextSegmentOffset = nextsegment;
    std::int64_t rawdataoffset = BinaryStream::Read<std::int64_t>(*m_fileStream, TDMSType::Integer64);
    if (rawdataoffset < 0)
        rawdataoffset = 0;
    if (rawdataoffset != 0)
        rawdataoffset += segmentBody;
    leadin->RawDataOffset = rawdataoffset;
    if (!m_fileStream->good())
        return nullptr;
    if (m_showLog)
        std::cout << "Segment offset :" << offset << "\n";
    return leadin;
}

auto Reader::ReadMetadata(std::shared_ptr<Segment> segment) -> std::vector<std::shared_ptr<Metadata>> {
    std::vector<std::shared_ptr<Metadata>> metadatas;
    if (segment == nullptr)
        return metadatas;

    // A segment that does not advertise metadata carries only samples - the
    // normal shape of a LabVIEW continuation segment. Parsing it anyway read the
    // first sample bytes as an object count (0x41424344 = 1094861636 objects on
    // one real file) and spun until the process was killed.
    if (!segment->TableOfContents.HasMetaData) {
        if (m_showLog)
            std::cout << "Segment at " << segment->Offset << " carries no metadata\n";
        return metadatas;
    }

    if (m_showLog) {
        std::cout << "Metadata offset: " << segment->MetadataOffset << "\n";
        std::cout << "Raw offset: " << segment->RawDataOffset << "\n";
    }
    m_fileStream->clear();
    m_fileStream->seekg(static_cast<std::streamoff>(segment->MetadataOffset), std::ios::beg);
    const int32_t objectCount = BinaryStream::Read<int32_t>(*m_fileStream, TDMSType::Integer32);

    // The smallest possible object record is a 4-byte path length, an empty
    // path, a 4-byte raw-data index and a 4-byte property count, so an object
    // count beyond that bound cannot be backed by this file.
    const std::int64_t metadataBytes =
        (segment->RawDataOffset > segment->MetadataOffset ? segment->RawDataOffset : static_cast<std::int64_t>(m_fileSize)) - segment->MetadataOffset - 4;
    if (objectCount < 0 || (metadataBytes > 0 && objectCount > metadataBytes / 12)) {
        std::cerr << "[ERROR] Reader::ReadMetadata: implausible object count " << objectCount << std::endl;
        return metadatas;
    }

    // --- Phase 1: parse the object records only. No sample data is read here,
    // because for interleaved segments the stride is not known until every
    // object has been seen - reading during this loop is what made interleaved
    // channels come back with one sample each.
    std::int64_t rawDataOffset = segment->RawDataOffset;
    const bool isInterleaved = segment->TableOfContents.RawDataIsInterleaved;
    std::int64_t interleaveStride = 0;
    for (int32_t x = 0; x < objectCount; x++) {
        if (!m_fileStream->good()) {
            std::cerr << "[ERROR] Reader::ReadMetadata: stream failed after " << x << " of " << objectCount << " objects" << std::endl;
            return metadatas;
        }
        if (m_showLog)
            std::cout << "Metadata offset position: " << m_fileStream->tellg() << "\n";
        auto metadata = std::make_shared<Metadata>();
        metadata->TableOfContents = segment->TableOfContents;
        metadata->Version = segment->Version;
        metadata->PathStr = BinaryStream::ReadLengthPrefixedString(*m_fileStream).GetDataString();
        if (!m_fileStream->good())
            return metadatas;

        // NOTE: match.str() is the whole match, so Path entries keep their
        // surrounding quotes ("'Group'") whereas WriterSegment stores bare
        // names. Preserved deliberately - external readers already see this
        // form, and PathStr carries the full path either way.
        std::regex r("'(.*?)'");
        std::sregex_iterator next(metadata->PathStr.begin(), metadata->PathStr.end(), r);
        std::sregex_iterator end;
        while (next != end) {
            std::smatch match = *next;
            metadata->Path.push_back(match.str());
            next++;
        }

        const auto rawDataIndexLength = BinaryStream::Read<int32_t>(*m_fileStream, TDMSType::Integer32);
        // Only these four index forms have a known byte width. Anything else -
        // notably the DAQmx forms - cannot be skipped, so the stream can no
        // longer be resynchronised and the rest of the segment is unparseable.
        if (rawDataIndexLength != -1 && rawDataIndexLength != 0 && rawDataIndexLength != 20 && rawDataIndexLength != 28) {
            std::cerr << "[ERROR] Reader::ReadMetadata: unsupported raw data index length " << rawDataIndexLength << std::endl;
            return metadatas;
        }
        if (rawDataIndexLength > 0) {
            metadata->RawData.Offset = rawDataOffset;
            if (m_showLog)
                std::cout << "RawData.Offset " << rawDataOffset << std::endl;
            metadata->RawData.IsInterleaved = isInterleaved;

            const TDMSType dataType = BinaryStream::Read<TDMSType>(*m_fileStream, TDMSType::Integer32);
            if (rawDataIndexLength == 20 && dataType == TDMSType::String) {
                // A string channel needs the 28-byte form; with the 20-byte one
                // there is no total size and GetLength(String) has no fixed
                // width, which used to produce a multi-gigabyte read.
                std::cerr << "[ERROR] Reader::ReadMetadata: string channel with a fixed-width raw data index" << std::endl;
                return metadatas;
            }
            metadata->RawData.DataType.InitDataType(dataType, nullptr);

            metadata->RawData.Dimension = BinaryStream::Read<int32_t>(*m_fileStream, TDMSType::Integer32);
            metadata->RawData.Count = BinaryStream::Read<std::int64_t>(*m_fileStream, TDMSType::Integer64);
            if (metadata->RawData.Count < 0) {
                std::cerr << "[ERROR] Reader::ReadMetadata: negative value count" << std::endl;
                return metadatas;
            }

            metadata->RawData.Size = rawDataIndexLength == 28 ? BinaryStream::Read<std::int64_t>(*m_fileStream, TDMSType::Integer64)
                                                              : static_cast<std::int64_t>(DataType::GetArrayLength(dataType, static_cast<std::uint64_t>(metadata->RawData.Count)));
            if (metadata->RawData.Size < 0) {
                std::cerr << "[ERROR] Reader::ReadMetadata: negative raw data size" << std::endl;
                return metadatas;
            }
            if (m_showLog)
                std::cout << "RawData.Size " << metadata->RawData.Size << std::endl;

            if (isInterleaved) {
                // For an interleaved segment each channel's offset is its
                // position WITHIN one stride, and the stride is the sum of all
                // channel widths.
                const std::int64_t width = DataType::GetLength(dataType);
                rawDataOffset += width;
                interleaveStride += width;
            } else {
                rawDataOffset += metadata->RawData.Size;
            }
        }
        if (m_showLog)
            std::cout << "Property offset position: " << m_fileStream->tellg() << "\n";
        const auto propertyCount = BinaryStream::Read<int32_t>(*m_fileStream, TDMSType::Integer32);
        if (propertyCount < 0) {
            std::cerr << "[ERROR] Reader::ReadMetadata: negative property count" << std::endl;
            return metadatas;
        }
        for (auto y = 0; y < propertyCount; y++) {
            auto key = BinaryStream::ReadLengthPrefixedString(*m_fileStream).GetDataString();
            const auto propertyType = BinaryStream::Read<TDMSType>(*m_fileStream, TDMSType::Integer32);
            auto value = BinaryStream::Read(*m_fileStream, propertyType);
            if (!m_fileStream->good()) {
                // An undecodable property leaves the stream at an unknown
                // offset; everything after it would be parsed as garbage.
                std::cerr << "[ERROR] Reader::ReadMetadata: undecodable property \"" << key << "\"" << std::endl;
                return metadatas;
            }
            metadata->Properties.insert(std::pair<std::string, DataType>(key, value));
        }
        metadatas.push_back(metadata);
    }

    // --- Phase 2: now that every object is known, derive the interleave
    // geometry once, instead of per channel.
    if (isInterleaved && interleaveStride > 0) {
        const std::int64_t rawEnd = segment->NextSegmentOffset > 0 ? segment->NextSegmentOffset : static_cast<std::int64_t>(m_fileSize);
        const std::int64_t rawBytes = rawEnd - segment->RawDataOffset;
        const std::int64_t sampleCount = rawBytes > 0 ? rawBytes / interleaveStride : 0;
        for (auto& metadata : metadatas) {
            if (metadata->RawData.Size == 0 && metadata->RawData.Count == 0)
                continue;
            metadata->RawData.InterleaveStride = interleaveStride;
            metadata->RawData.Count = sampleCount;
            metadata->RawData.Size = sampleCount * static_cast<std::int64_t>(DataType::GetLength(metadata->RawData.DataType.GetDataType()));
        }
    }

    // --- Phase 3: read the sample blocks, with a correct stride in hand.
    for (auto& metadata : metadatas) {
        if (metadata->RawData.Count == 0 && metadata->RawData.Size == 0)
            continue;
        const TDMSType dataType = metadata->RawData.DataType.GetDataType();
        std::vector<std::shared_ptr<DataType::Raw>> raw = ReadRawData(metadata->RawData);
        metadata->RawData.DataType.InitDataType(dataType, raw);
    }
    return metadatas;
}

auto Reader::ReadRawData(RawData& rawData) -> std::vector<std::shared_ptr<DataType::Raw>> {
    if (rawData.IsInterleaved) {
        // Computed in int64_t and clamped: `int - uint32_t` made this expression
        // unsigned, so a stride smaller than the element width wrapped to a huge
        // value that was then narrowed into an int parameter.
        const std::int64_t width = DataType::GetLength(rawData.DataType.GetDataType());
        const std::int64_t skip = std::max<std::int64_t>(0, rawData.InterleaveStride - width);
        return ReadRawInterleaved(rawData.Offset, rawData.Count, rawData.DataType.GetDataType(), skip);
    }
    return rawData.DataType.GetDataType() == TDMSType::String ? ReadRawStrings(rawData.Offset, rawData.Count)
                                                              : ReadRawFixed(rawData.Offset, rawData.Count, rawData.DataType.GetDataType());
}

auto Reader::ReadRawFixed(std::int64_t offset, std::int64_t count, TDMSType dataType) -> std::vector<std::shared_ptr<DataType::Raw>> {
    std::vector<std::shared_ptr<DataType::Raw>> vec;
    const std::int64_t sizeread = static_cast<std::int64_t>(DataType::GetLength(dataType)) * count;
    if (sizeread <= 0 || offset < 0 || offset + sizeread > static_cast<std::int64_t>(m_fileSize)) {
        if (sizeread > 0)
            std::cerr << "[ERROR] Reader::ReadRawFixed: block at " << offset << " of " << sizeread << " bytes is outside the file" << std::endl;
        return vec;
    }
    auto buff = BinaryStream::ReadArray(*m_fileStream, sizeread, offset);
    if (buff == nullptr)
        return vec;
    auto raw = std::make_shared<DataType::Raw>();
    raw->data = buff;
    raw->size = static_cast<std::uint64_t>(sizeread);
    raw->dataType = dataType;
    vec.push_back(raw);
    return vec;
}

auto Reader::ReadRawInterleaved(std::int64_t offset, std::int64_t count, TDMSType dataType, std::int64_t interleaveSkip) -> std::vector<std::shared_ptr<DataType::Raw>> {
    std::vector<std::shared_ptr<DataType::Raw>> vec;
    const std::int64_t width = DataType::GetLength(dataType);
    if (width <= 0 || count <= 0 || offset < 0)
        return vec;
    auto buff = BinaryStream::ReadArray(*m_fileStream, width, count, offset, interleaveSkip);
    if (buff == nullptr)
        return vec;
    auto raw = std::make_shared<DataType::Raw>();
    raw->data = buff;
    // Bytes for the WHOLE block. This used to record one element's width, so a
    // consumer saw a single sample no matter how many were read.
    raw->size = static_cast<std::uint64_t>(width * count);
    raw->dataType = dataType;
    vec.push_back(raw);
    return vec;
}

auto Reader::ReadRawStrings(std::int64_t offset, std::int64_t count) -> std::vector<std::shared_ptr<DataType::Raw>> {
    std::vector<std::shared_ptr<DataType::Raw>> vec;
    if (count <= 0 || offset < 0)
        return vec;
    // Every length below comes from the file, so each one is range-checked
    // before it becomes an allocation or a read: the original cast the
    // difference to int and could allocate a negative size.
    const std::int64_t fileSize = static_cast<std::int64_t>(m_fileSize);
    const std::int64_t dataOffset = offset + count * 4;
    if (dataOffset > fileSize)
        return vec;

    const std::ios::pos_type resume = m_fileStream->tellg();
    m_fileStream->clear();
    m_fileStream->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    std::int64_t dataPosition = dataOffset;
    for (std::int64_t x = 0; x < count; x++) {
        const auto endOfString = BinaryStream::Read<uint32_t>(*m_fileStream, TDMSType::UnsignedInteger32);
        if (!m_fileStream->good())
            break;
        const std::ios::pos_type indexPosition = m_fileStream->tellg();
        const std::int64_t stringEnd = dataOffset + static_cast<std::int64_t>(endOfString);
        const std::int64_t length = stringEnd - dataPosition;
        if (length < 0 || stringEnd > fileSize) {
            std::cerr << "[ERROR] Reader::ReadRawStrings: string " << x << " has an out-of-range extent" << std::endl;
            break;
        }
        auto buff = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[static_cast<std::size_t>(length > 0 ? length : 1)]());
        if (length > 0) {
            m_fileStream->seekg(static_cast<std::streamoff>(dataPosition), std::ios::beg);
            m_fileStream->read(reinterpret_cast<char*>(buff.get()), static_cast<std::streamsize>(length));
            if (m_fileStream->gcount() != static_cast<std::streamsize>(length)) {
                std::cerr << "[ERROR] Reader::ReadRawStrings: short read for string " << x << std::endl;
                break;
            }
        }
        auto raw = std::make_shared<DataType::Raw>();
        raw->data = buff;
        raw->size = static_cast<std::uint64_t>(length);
        raw->dataType = TDMSType::String;
        vec.push_back(raw);
        dataPosition = stringEnd;
        m_fileStream->clear();
        m_fileStream->seekg(indexPosition);
    }
    m_fileStream->clear();
    if (resume >= 0)
        m_fileStream->seekg(resume);
    return vec;
}
