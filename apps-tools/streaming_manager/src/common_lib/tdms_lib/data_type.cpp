#include "data_type.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>

using namespace TDMS;

namespace {

// Seconds between the TDMS epoch (1904-01-01T00:00:00 UTC) and the Unix epoch,
// ignoring leap seconds: 66 years (1904..1969) is 66 * 365 days plus 17 leap
// days (1904, 1908, ... 1968) = 24107 days.
//
// This was previously derived with mktime() from a LOCAL 1904-01-01, which made
// every recorded timestamp depend on the machine's timezone even though a TDMS
// timestamp is UTC - measured as 2082844800 under UTC but 2082853817 under
// Europe/Moscow, whose 1904 offset was UTC+02:30:17.
constexpr std::int64_t kSecondsFrom1904To1970 = 24107LL * 86400LL;
static_assert(kSecondsFrom1904To1970 == 2082844800LL, "TDMS epoch offset");

// Large enough for "%f" of DBL_MAX, which needs over 300 characters. The
// original code formatted into char[22].
constexpr std::size_t kFormatBufferSize = 352;

// Frees a buffer produced by any `new T[n]` with a trivially destructible T.
// Every pointer the adopting entry points can receive comes from `new char[n]`,
// `new std::uint8_t[n]` or `new std::uint64_t[2]`, all of which are plain
// operator new[] allocations, so this is the exact matching deallocation and it
// avoids the element-type mismatch the old typed `delete[]` expressions had.
auto FreeAdopted(void* pointer) noexcept -> void {
    if (pointer != nullptr) {
        ::operator delete[](pointer);
    }
}

}  // namespace

auto DataType::InitDataType(TDMSType dataType, void* rawData) -> void {
    m_dataType = dataType;
    if (rawData == nullptr) {
        m_data.clear();
        if (dataType == TDMSType::String) {
            m_dataStringLenght = 0;
        }
        return;
    }
    if (dataType == TDMSType::String) {
        // The byte length of a string cannot be recovered from this signature;
        // free the buffer rather than leak it and report the misuse.
        FreeAdopted(rawData);
        m_data.clear();
        m_dataStringLenght = 0;
        std::cerr << "[ERROR] DataType::InitDataType: use InitStringType for TDMSType::String" << std::endl;
        return;
    }
    const std::uint32_t length = DataType::GetLength(dataType);
    const auto* source = static_cast<const std::uint8_t*>(rawData);
    m_data.assign(source, source + length);
    FreeAdopted(rawData);
}

auto DataType::InitDataType(TDMSType dataType, std::vector<std::shared_ptr<DataType::Raw>> vec) -> void {
    m_dataType = dataType;
    m_vectorData = std::move(vec);
}

auto DataType::InitStringType(std::uint32_t length, void* rawData) -> void {
    m_dataType = TDMSType::String;
    if (rawData == nullptr) {
        m_data.clear();
        m_dataStringLenght = 0;
        return;
    }
    const auto* source = static_cast<const std::uint8_t*>(rawData);
    m_data.assign(source, source + length);
    m_dataStringLenght = length;
    FreeAdopted(rawData);
}

auto DataType::InitBytes(TDMSType dataType, const void* data, std::size_t size) -> void {
    m_dataType = dataType;
    if (data == nullptr || size == 0) {
        m_data.clear();
    } else {
        const auto* source = static_cast<const std::uint8_t*>(data);
        m_data.assign(source, source + size);
    }
    if (dataType == TDMSType::String) {
        m_dataStringLenght = static_cast<std::uint32_t>(size);
    }
}

auto DataType::InitRaw(TDMSType dataType, std::uint64_t count, std::shared_ptr<std::uint8_t[]> rawData) -> void {
    m_dataType = dataType;
    std::shared_ptr<Raw> raw = std::make_shared<Raw>();
    raw->data = std::move(rawData);
    raw->size = count * GetLength();
    raw->dataType = dataType;
    m_vectorData.push_back(raw);
}

auto DataType::MakeString(const std::string& text) -> DataType {
    DataType result;
    result.InitBytes(TDMSType::String, text.data(), text.size());
    return result;
}

auto DataType::MakeTimeStamp(std::int64_t secondsSinceUnixEpoch, std::uint64_t fraction) -> DataType {
    std::uint8_t payload[16];
    const std::int64_t seconds = secondsSinceUnixEpoch + kSecondsFrom1904To1970;
    std::memcpy(payload, &fraction, sizeof(fraction));
    std::memcpy(payload + sizeof(fraction), &seconds, sizeof(seconds));
    DataType result;
    result.InitBytes(TDMSType::TimeStamp, payload, sizeof(payload));
    return result;
}

auto DataType::GetDataType() const -> TDMSType {
    return m_dataType;
}

auto DataType::GetRawVector() const -> std::vector<std::shared_ptr<DataType::Raw>> {
    return m_vectorData;
}

auto DataType::GetRawData() const -> void* {
    // Null when there is no payload, which is the signal callers already relied
    // on. Const-casting away constness here matches the original signature.
    return m_data.empty() ? nullptr : const_cast<void*>(static_cast<const void*>(m_data.data()));
}

auto DataType::GetLength() const -> std::uint32_t {
    if (m_dataType == TDMSType::String)
        return this->m_dataStringLenght;
    return DataType::GetLength(this->m_dataType);
}

auto DataType::GetLength(TDMSType dataType) -> std::uint32_t {
    switch (dataType) {
        case TDMSType::Empty:
            return 0;
        case TDMSType::Void:
            return 1;
        case TDMSType::Integer8:
            return 1;
        case TDMSType::Integer16:
            return 2;
        case TDMSType::Integer32:
            return 4;
        case TDMSType::Integer64:
            return 8;
        case TDMSType::UnsignedInteger8:
            return 1;
        case TDMSType::UnsignedInteger16:
            return 2;
        case TDMSType::UnsignedInteger32:
            return 4;
        case TDMSType::UnsignedInteger64:
            return 8;
        case TDMSType::SingleFloat:
        case TDMSType::SingleFloatWithUnit:
            return 4;
        case TDMSType::DoubleFloat:
        case TDMSType::DoubleFloatWithUnit:
            return 8;
        case TDMSType::Boolean:
            return 1;
        case TDMSType::TimeStamp:
            return 16;
        case TDMSType::String:
            // A string is length-prefixed and has no fixed width. This used to
            // return (uint32_t)-1, which made GetArrayLength(String, n) come out
            // as 0xFFFFFFFF * n and the reader attempt a multi-gigabyte read.
            return 0;
        default:
            std::cerr << "[ERROR] DataType: cannot determine the size of data type "
                      << std::to_string(static_cast<std::uint32_t>(dataType)) << std::endl;
            break;
    }
    return 0;
}

auto DataType::GetArrayLength(TDMSType dataType, std::uint64_t size) -> std::uint64_t {
    return static_cast<std::uint64_t>(GetLength(dataType)) * size;
}

auto DataType::ToString() const -> std::string {
    char cstr[kFormatBufferSize] = {0};
    switch (m_dataType) {
        case TDMSType::Empty:
            return "Empty";
        case TDMSType::Void:
            return "Void";
        case TDMSType::Integer8:
            snprintf(cstr, sizeof(cstr), "%i", this->GetData<int8_t>());
            break;
        case TDMSType::Integer16:
            snprintf(cstr, sizeof(cstr), "%i", this->GetData<int16_t>());
            break;
        case TDMSType::Integer32:
            snprintf(cstr, sizeof(cstr), "%i", this->GetData<int32_t>());
            break;
        case TDMSType::Integer64:
            snprintf(cstr, sizeof(cstr), "%ld", (long int)this->GetData<int64_t>());
            break;
        case TDMSType::UnsignedInteger8:
            snprintf(cstr, sizeof(cstr), "%u", this->GetData<uint8_t>());
            break;
        case TDMSType::UnsignedInteger16:
            snprintf(cstr, sizeof(cstr), "%u", this->GetData<uint16_t>());
            break;
        case TDMSType::UnsignedInteger32:
            snprintf(cstr, sizeof(cstr), "%u", this->GetData<uint32_t>());
            break;
        case TDMSType::UnsignedInteger64:
            snprintf(cstr, sizeof(cstr), "%lu", (long unsigned int)this->GetData<uint64_t>());
            break;
        case TDMSType::SingleFloat:
        case TDMSType::SingleFloatWithUnit:
            snprintf(cstr, sizeof(cstr), "%f", this->GetData<float>());
            break;
        case TDMSType::DoubleFloat:
        case TDMSType::DoubleFloatWithUnit:
            snprintf(cstr, sizeof(cstr), "%lf", this->GetData<double>());
            break;
        case TDMSType::Boolean:
            snprintf(cstr, sizeof(cstr), "%s", this->GetData<uint8_t>() ? "true" : "false");
            break;
        case TDMSType::TimeStamp: {
            uint64_t* t = (uint64_t*)GetRawData();
            double v1 = (double)t[0] / std::pow(2., 64.);
            std::tm time_point;
            std::memset(&time_point, 0, sizeof(std::tm));
            std::time_t time = static_cast<std::time_t>(t[1]) - kSecondsFrom1904To1970;
#ifdef _WIN32
            gmtime_s(&time_point, &time);
#else
            gmtime_r(&time, &time_point);
#endif  // _WIN32
            std::stringstream stream;
            stream.imbue(std::locale::classic());
            stream << std::put_time(&time_point, "day: %d month: %m year: %Y time:%T ");
            snprintf(cstr, sizeof(cstr), "%lf", v1);
            return stream.str() + std::string(cstr);
        }
        case TDMSType::String: {
            return GetDataString();
        }
        default:
            // Used to fall through and return std::string(cstr) with cstr
            // uninitialised.
            return "Error";
    }
    return std::string(cstr);
}

auto DataType::ToTypeString() const -> std::string {
    switch (m_dataType) {
        case TDMSType::Empty:
            return "Empty";
        case TDMSType::Void:
            return "Void";
        case TDMSType::Integer8:
            return "Integer8";
        case TDMSType::Integer16:
            return "Integer16";
        case TDMSType::Integer32:
            return "Integer32";
        case TDMSType::Integer64:
            return "Integer64";
        case TDMSType::UnsignedInteger8:
            return "UInteger8";
        case TDMSType::UnsignedInteger16:
            return "UInteger16";
        case TDMSType::UnsignedInteger32:
            return "UInteger32";
        case TDMSType::UnsignedInteger64:
            return "UInteger64";
        case TDMSType::SingleFloat:
            return "SingleFloat";
        case TDMSType::SingleFloatWithUnit:
            return "SingleFloatWithUnit";
        case TDMSType::DoubleFloat:
            return "DoubleFloat";
        case TDMSType::DoubleFloatWithUnit:
            return "DoubleFloatWithUnit";
        case TDMSType::Boolean:
            return "Boolean";
        case TDMSType::TimeStamp:
            return "TimeStamp";
        case TDMSType::String:
            return "String";
        default:;
    }
    return "Error";
}

auto DataType::PrintVector(int limitDataSize) const -> void {
    int i = 0;
    for (auto& r : m_vectorData) {
        if (m_dataType == TDMSType::String) {
            printf("\t\t\t Raw val[%i]:", i++);
            for (auto j = 0u; j < r->size; j++) {
                printf("%c", r->data.get()[j]);
            }
            printf("\n");
        } else {
            printf("\t\t\t Raw val[%i]:\n", i++);
            bool Trunc = false;
            long DataSize = this->GetLength();
            if (m_dataType == TDMSType::TimeStamp)
                DataSize /= 2;
            long count = r->size / DataSize;
            if (limitDataSize != -1) {
                if (count > limitDataSize) {
                    count = limitDataSize;
                    Trunc = true;
                }
            }
            for (int j = 0; j < count; j++) {
                char cstr[22];
                printf("\t");
                switch (m_dataType) {
                    case TDMSType::Empty:
                        printf("\t\t\t- Empty\n");
                        break;
                    case TDMSType::Void:
                        printf("\t\t\t- Void\n");
                        break;
                    case TDMSType::Integer8:
                        printf("\t\t\t- %i\n", ((int8_t*)r->data.get())[j]);
                        break;
                    case TDMSType::Integer16:
                        printf("\t\t\t- %i\n", ((int16_t*)r->data.get())[j]);
                        break;
                    case TDMSType::Integer32:
                        printf("\t\t\t- %i\n", ((int32_t*)r->data.get())[j]);
                        break;
                    case TDMSType::Integer64:
                        printf("\t\t\t- %ld\n", (long int)((int64_t*)r->data.get())[j]);
                        break;
                    case TDMSType::UnsignedInteger8:
                        printf("\t\t\t- %u\n", ((uint8_t*)r->data.get())[j]);
                        break;
                    case TDMSType::UnsignedInteger16:
                        printf("\t\t\t- %u\n", ((uint16_t*)r->data.get())[j]);
                        break;
                    case TDMSType::UnsignedInteger32:
                        printf("\t\t\t- %u\n", ((uint32_t*)r->data.get())[j]);
                        break;
                    case TDMSType::UnsignedInteger64:
                        printf("\t\t\t- %lu\n", (long unsigned int)((uint64_t*)r->data.get())[j]);
                        break;
                    case TDMSType::SingleFloat:
                    case TDMSType::SingleFloatWithUnit:
                        printf("\t\t\t- %f\n", ((float_t*)r->data.get())[j]);
                        break;
                    case TDMSType::DoubleFloat:
                    case TDMSType::DoubleFloatWithUnit:
                        printf("\t\t\t- %lf\n", ((double_t*)r->data.get())[j]);
                        break;
                    case TDMSType::Boolean:
                        printf("\t\t\t- %s\n", (((uint8_t*)r->data.get())[j] ? "true" : "false"));
                        break;
                    case TDMSType::TimeStamp: {
                        uint64_t t1 = ((uint64_t*)r->data.get())[j++];
                        uint64_t v2 = ((uint64_t*)r->data.get())[j];
                        double v1 = (double)t1 / std::pow(2., 64.);

                        std::tm time_point;
                        std::memset(&time_point, 0, sizeof(std::tm));
                        std::time_t time = static_cast<std::time_t>(v2) - kSecondsFrom1904To1970;
#ifdef _WIN32
                        gmtime_s(&time_point, &time);
#else
                        gmtime_r(&time, &time_point);
#endif  // _WIN32
                        std::stringstream stream;
                        stream.imbue(std::locale::classic());
                        stream << std::put_time(&time_point, "day: %d month: %m year: %Y time:%T ");
                        snprintf(cstr, sizeof(cstr), "%lf", v1);
                        printf("\t\t\t- %s\n", (stream.str() + std::string(cstr)).c_str());
                        break;
                    }
                    default:
                        break;
                }
            }
            if (Trunc) {
                printf("\t\t\t- ........\n");
            }
        }
    }
}

auto DataType::GetRawTimeValue(std::time_t time_val) -> std::uint64_t* {
    std::uint64_t* val = new std::uint64_t[2];
    val[0] = 0;  // Sub-second fraction, in units of 2^-64 s.
    val[1] = static_cast<std::uint64_t>(static_cast<std::int64_t>(time_val) + kSecondsFrom1904To1970);
    return val;
}

auto DataType::GetDataString() const -> std::string {
    return std::string(m_data.begin(), m_data.end());
}
