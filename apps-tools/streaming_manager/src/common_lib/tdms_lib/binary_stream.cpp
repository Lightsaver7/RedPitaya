#include "binary_stream.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

using namespace TDMS;

namespace {

auto BytesRemaining(std::iostream& stream) -> std::int64_t {
    if (!stream.good()) {
        return 0;
    }
    const std::streampos here = stream.tellg();
    if (here < 0) {
        return 0;
    }
    stream.seekg(0, std::ios::end);
    const std::streampos end = stream.tellg();
    stream.seekg(here, std::ios::beg);
    if (end < here) {
        return 0;
    }
    return static_cast<std::int64_t>(end - here);
}

}  // namespace

BinaryStream::BinaryStream() {}

BinaryStream::~BinaryStream() {}

auto BinaryStream::ReadLengthPrefixedString(std::iostream& reader) -> DataType {
    return BinaryStream::Read(reader, TDMSType::String);
}

auto BinaryStream::ReadString(std::iostream& reader, std::int64_t length) -> DataType {
    DataType datatype;
    if (length == 0) {
        datatype.InitBytes(TDMSType::String, nullptr, 0);
        return datatype;
    }
    // The length comes from the file, so it can be negative once it has been
    // through a signed conversion, or far larger than the file. Either used to
    // become `new uint8_t[length]` directly - a 4 GiB request on a 46-byte
    // stream. The `catch` this replaces was unreachable: istream::read does not
    // throw unless exceptions() has been armed, which it never is here, so a
    // short read silently handed out an uninitialised buffer.
    if (length < 0 || length > BytesRemaining(reader)) {
        std::cerr << "[ERROR] BinaryStream::ReadString: implausible length " << length << std::endl;
        reader.setstate(std::ios::failbit);
        return datatype;
    }
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(length));
    reader.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(length));
    if (reader.gcount() != static_cast<std::streamsize>(length)) {
        std::cerr << "[ERROR] BinaryStream::ReadString: short read of " << length << " bytes" << std::endl;
        return datatype;
    }
    datatype.InitBytes(TDMSType::String, buffer.data(), buffer.size());
    return datatype;
}

auto BinaryStream::Read(std::iostream& reader, TDMSType dataType) -> DataType {
    DataType data;
    switch (dataType) {
        case TDMSType::Empty:
            return data;
        case TDMSType::Void:
            reader.peek();
            return data;
        case TDMSType::Boolean:
        case TDMSType::Integer8:
        case TDMSType::Integer16:
        case TDMSType::Integer32:
        case TDMSType::Integer64:
        case TDMSType::UnsignedInteger8:
        case TDMSType::UnsignedInteger16:
        case TDMSType::UnsignedInteger32:
        case TDMSType::UnsignedInteger64:
        case TDMSType::SingleFloat:
        case TDMSType::SingleFloatWithUnit:
        case TDMSType::DoubleFloat:
        case TDMSType::DoubleFloatWithUnit:
        case TDMSType::TimeStamp: {
            const std::uint32_t length = DataType::GetLength(dataType);
            std::array<std::uint8_t, 16> buffer{};
            if (length > buffer.size()) {
                reader.setstate(std::ios::failbit);
                return data;
            }
            reader.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(length));
            if (reader.gcount() != static_cast<std::streamsize>(length)) {
                // A short read used to leave the buffer uninitialised and hand
                // it out as a valid value.
                std::cerr << "[ERROR] BinaryStream::Read: short read for type " << static_cast<std::uint32_t>(dataType) << std::endl;
                return data;
            }
            data.InitBytes(dataType, buffer.data(), length);
            return data;
        }
        case TDMSType::String: {
            DataType dataPrefix = BinaryStream::Read(reader, TDMSType::Integer32);
            if (!reader.good()) {
                return DataType();
            }
            const std::uint32_t prefix = dataPrefix.GetData<std::uint32_t>();
            return BinaryStream::ReadString(reader, static_cast<std::int64_t>(prefix));
        }
        default:
            // An undecodable type has an unknown width, so the stream cannot be
            // advanced past it and everything after this point would be parsed
            // at the wrong offset. Returning an empty value silently, as the
            // original did, desynchronised the whole metadata parse; mark the
            // stream so the caller can stop.
            std::cerr << "[ERROR] BinaryStream::Read: cannot determine the size of data type " << static_cast<std::uint32_t>(dataType) << std::endl;
            reader.setstate(std::ios::failbit);
            break;
    }
    return DataType();
}

auto BinaryStream::ReadArray(std::iostream& reader, std::int64_t size, std::int64_t offset) -> std::shared_ptr<std::uint8_t[]> {
    if (size <= 0 || offset < 0) {
        return nullptr;
    }
    // Capture the position before any read: tellg() returns -1 once the stream
    // has failed, and restoring that poisons every later seek.
    const std::ios::pos_type resume = reader.tellg();
    auto buff = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[static_cast<std::size_t>(size)]());
    reader.clear();
    reader.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    reader.read(reinterpret_cast<char*>(buff.get()), static_cast<std::streamsize>(size));
    if (reader.gcount() != static_cast<std::streamsize>(size)) {
        std::cerr << "[ERROR] BinaryStream::ReadArray: short read of " << size << " bytes at offset " << offset << std::endl;
    }
    reader.clear();
    if (resume >= 0) {
        reader.seekg(resume);
    }
    return buff;
}

auto BinaryStream::ReadArray(std::iostream& reader, std::int64_t dataSize, std::int64_t count, std::int64_t offset,
                             std::int64_t interleaveSkip) -> std::shared_ptr<std::uint8_t[]> {
    if (dataSize <= 0 || count <= 0 || offset < 0 || interleaveSkip < 0) {
        return nullptr;
    }
    const std::ios::pos_type resume = reader.tellg();
    auto buff = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[static_cast<std::size_t>(dataSize * count)]());
    reader.clear();
    reader.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    for (std::int64_t x = 0; x < count; x++) {
        // The destination must advance: the original read every element into the
        // START of the buffer, so only one sample per channel ever survived.
        reader.read(reinterpret_cast<char*>(buff.get() + x * dataSize), static_cast<std::streamsize>(dataSize));
        if (reader.gcount() != static_cast<std::streamsize>(dataSize)) {
            std::cerr << "[ERROR] BinaryStream::ReadArray: interleaved block truncated after " << x << " of " << count << " samples" << std::endl;
            break;
        }
        if (interleaveSkip > 0) {
            reader.seekg(static_cast<std::streamoff>(interleaveSkip), std::ios::cur);
        }
    }
    reader.clear();
    if (resume >= 0) {
        reader.seekg(resume);
    }
    return buff;
}

auto BinaryStream::Write(std::iostream& writer, const DataType& data) -> void {
    switch (data.GetDataType()) {
        case TDMSType::Empty:
            break;
        case TDMSType::Void: {
            uint8_t buf = 0;
            writer.write((char*)&buf, sizeof(buf));
            break;
        }
        case TDMSType::Boolean:
        case TDMSType::Integer8:
        case TDMSType::Integer16:
        case TDMSType::Integer32:
        case TDMSType::Integer64:
        case TDMSType::UnsignedInteger8:
        case TDMSType::UnsignedInteger16:
        case TDMSType::UnsignedInteger32:
        case TDMSType::UnsignedInteger64:
        case TDMSType::SingleFloat:
        case TDMSType::SingleFloatWithUnit:
        case TDMSType::DoubleFloat:
        case TDMSType::DoubleFloatWithUnit:
        case TDMSType::TimeStamp: {
            if (data.GetRawData() == nullptr && data.GetLength() > 0) {
                throw std::logic_error("[ERROR] BinaryStream::Write: value has no payload to write");
            }
            writer.write(static_cast<const char*>(data.GetRawData()), data.GetLength());
            break;
        }
        case TDMSType::String: {
            const std::uint32_t strLen = data.GetLength();
            writer.write(reinterpret_cast<const char*>(&strLen), sizeof(strLen));
            if (strLen > 0) {
                writer.write(static_cast<const char*>(data.GetRawData()), strLen);
            }
            break;
        }
        default:
            throw std::invalid_argument("[ERROR] BinaryStream::Write: cannot determine the size of data type " + std::to_string(static_cast<std::uint32_t>(data.GetDataType())));
    }
}
