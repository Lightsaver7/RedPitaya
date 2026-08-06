#ifndef TDMS_LIB_BINARYSTREAM_H
#define TDMS_LIB_BINARYSTREAM_H

#include <cstdint>
#include <iostream>
#include "data_type.h"



namespace TDMS {
class BinaryStream {
   public:
    BinaryStream();
    ~BinaryStream();

    static auto ReadLengthPrefixedString(std::iostream& reader) -> DataType;
    static auto ReadString(std::iostream& reader, std::int64_t length) -> DataType;
    static auto Read(std::iostream& reader, TDMSType dataType) -> DataType;
    static auto ReadArray(std::iostream& reader, std::int64_t size, std::int64_t offset) -> std::shared_ptr<std::uint8_t[]>;
    static auto ReadArray(std::iostream& reader, std::int64_t dataSize, std::int64_t count, std::int64_t offset, std::int64_t interleaveSkip)
        -> std::shared_ptr<std::uint8_t[]>;
    static auto Write(std::iostream& writer, const DataType& data) -> void;

    template <typename T>
    static auto Read(std::iostream& reader, TDMSType dataType) -> T {
        DataType data = BinaryStream::Read(reader, dataType);
        return data.GetData<T>();
    }
};
}  // namespace TDMS

#endif
