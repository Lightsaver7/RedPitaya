#ifndef TDMS_LIB_DATA_TYPE_H
#define TDMS_LIB_DATA_TYPE_H

#include <cstdint>
#include <cstring>
#include <ctime>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace TDMS {
enum class TDMSType {
    Empty = 0x0000000F,
    Void = 0x00000000,
    Integer8 = 0x00000001,
    Integer16 = 0x00000002,
    Integer32 = 0x00000003,
    Integer64 = 0x00000004,
    UnsignedInteger8 = 0x00000005,
    UnsignedInteger16 = 0x00000006,
    UnsignedInteger32 = 0x00000007,
    UnsignedInteger64 = 0x00000008,
    SingleFloat = 0x00000009,
    DoubleFloat = 0x0000000A,
    ExtendedFloat = 0x0000000B,
    SingleFloatWithUnit = 0x00000019,
    DoubleFloatWithUnit = 0x0000001A,
    ExtendedFloatWithUnit = 0x0000001B,
    String = 0x00000020,
    Boolean = 0x00000021,
    TimeStamp = 0x00000044
};

/**
 * A TDMS value: either a single scalar/string property payload, or a list of
 * raw sample blocks belonging to a channel.
 *
 * The scalar payload lives in a std::vector<std::uint8_t> rather than behind a
 * void*, which is what makes this class obey the rule of zero: copying, moving
 * and destruction are all compiler-generated and correct. The previous design
 * owned a void* whose deallocation switched on a runtime type tag, and that
 * produced four separate defects - a destructor that threw (so destroying a
 * DataType inside the map<string, DataType> of Metadata during unwinding
 * terminated the process), a copy-assignment that dropped the old buffer
 * without freeing it, a self-assignment that silently discarded the payload,
 * and a delete[] through an element type that did not match the new[].
 *
 * INVARIANT: m_data is either empty ("no payload") or exactly GetLength()
 * bytes. For TDMSType::String, m_data.size() == m_dataStringLenght always.
 *
 * The pointer-adopting entry points (InitDataType(TDMSType, void*),
 * InitStringType, and the static MakeData/GetRawTimeValue that feed them) keep
 * their original signatures and their original "the callee takes ownership"
 * contract, because external code builds properties with them. They now copy
 * the bytes in and free the incoming allocation immediately, so the caller must
 * still not free it - and must no longer read through it afterwards.
 */
class DataType {
   public:
    // One contiguous block of raw channel samples. `size` is always a BYTE
    // count, never an element count.
    struct Raw {
        std::shared_ptr<std::uint8_t[]> data;
        std::uint64_t size;
        TDMSType dataType;
        Raw() : data(nullptr), size(0), dataType(TDMSType::Empty) {}
    };

   protected:
    TDMSType m_dataType = TDMSType::Empty;
    // Byte length of a String payload; meaningless for other types. Named as in
    // the original header (typo included) because it is protected API.
    std::uint32_t m_dataStringLenght = 0;
    std::vector<std::uint8_t> m_data;
    std::vector<std::shared_ptr<DataType::Raw>> m_vectorData;

   public:
    DataType() = default;

    // Adopts rawData: copies GetLength(dataType) bytes out of it and frees it.
    // A null pointer records the type with no payload, which is how the reader
    // remembers a channel's element type before its samples are available.
    auto InitDataType(TDMSType dataType, void* rawData) -> void;
    auto InitDataType(TDMSType dataType, std::vector<std::shared_ptr<DataType::Raw>> vec) -> void;
    // Adopts rawData: copies `length` bytes out of it and frees it.
    auto InitStringType(std::uint32_t length, void* rawData) -> void;
    auto InitRaw(TDMSType dataType, std::uint64_t count, std::shared_ptr<std::uint8_t[]> rawData) -> void;

    // Copies `size` bytes without taking ownership of anything. Preferred over
    // the adopting overloads for new code.
    auto InitBytes(TDMSType dataType, const void* data, std::size_t size) -> void;

    auto GetDataType() const -> TDMSType;
    auto ToString() const -> std::string;
    auto ToTypeString() const -> std::string;
    auto GetRawVector() const -> std::vector<std::shared_ptr<DataType::Raw>>;
    // Points at the payload, or null when there is none. Valid until the next
    // mutating call on this object.
    auto GetRawData() const -> void*;
    auto GetDataString() const -> std::string;
    auto GetLength() const -> std::uint32_t;
    auto PrintVector(int limitDataSize) const -> void;

    template <typename T>
    auto GetData() const -> T {
        static_assert(std::is_trivially_copyable<T>::value, "GetData<T> requires a trivially copyable T");
        T value{};
        if (m_data.size() >= sizeof(T)) {
            std::memcpy(&value, m_data.data(), sizeof(T));
        }
        return value;
    }

    // Allocates a byte buffer holding `value`, for handing to InitDataType.
    // Kept for source compatibility; MakeScalar below is the safe equivalent.
    template <typename T>
    static auto MakeData(T value) -> void* {
        static_assert(std::is_trivially_copyable<T>::value, "MakeData<T> requires a trivially copyable T");
        std::uint8_t* buff = new std::uint8_t[sizeof(T)];
        std::memcpy(buff, &value, sizeof(T));
        return buff;
    }

    // Builds a value without any raw pointer changing hands. The payload width
    // is checked against the type, so a mismatched pair cannot be constructed.
    template <typename T>
    static auto MakeScalar(TDMSType dataType, T value) -> DataType {
        static_assert(std::is_trivially_copyable<T>::value, "MakeScalar<T> requires a trivially copyable T");
        DataType result;
        if (sizeof(T) != DataType::GetLength(dataType)) {
            throw std::invalid_argument("[ERROR] MakeScalar: payload width does not match the TDMS type");
        }
        result.InitBytes(dataType, &value, sizeof(T));
        return result;
    }

    static auto MakeString(const std::string& text) -> DataType;
    // secondsSinceUnixEpoch is converted to the TDMS 1904 epoch.
    static auto MakeTimeStamp(std::int64_t secondsSinceUnixEpoch, std::uint64_t fraction = 0) -> DataType;

    static auto GetLength(TDMSType dataType) -> std::uint32_t;
    static auto GetArrayLength(TDMSType dataType, std::uint64_t size) -> std::uint64_t;
    // Returns a new[]-allocated pair {fraction, secondsSince1904} for handing to
    // InitDataType(TimeStamp, ...). Kept for source compatibility;
    // MakeTimeStamp is the safe equivalent.
    static auto GetRawTimeValue(std::time_t time_val) -> std::uint64_t*;
};
}  // namespace TDMS

#endif
