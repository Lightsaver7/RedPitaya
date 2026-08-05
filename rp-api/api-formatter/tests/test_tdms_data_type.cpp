/**
 * Unit tests for rp_formatter_api::TDMS::DataType (src/tdms/data_type.cpp).
 *
 * DataType is the value/ownership primitive the whole TDMS writer is built
 * on: it decides how many bytes a TDMS type occupies, owns the raw buffer
 * that later gets written out verbatim, and is copied by value into
 * Metadata::Properties (see WriterSegment::AddProperties), so its copy and
 * move semantics are load-bearing rather than incidental.
 *
 * Note on ownership: InitDataType()/InitStringType() take ownership of the
 * pointer handed to them and the destructor frees it with delete[], so
 * every buffer in these tests is allocated with new[].
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <ctime>
#include <string>

#include "data_type.h"

using namespace rp_formatter_api::TDMS;

namespace {

template <typename T>
DataType MakeScalar(TDMSType type, T value) {
    DataType data;
    data.InitDataType(type, DataType::MakeData<T>(value));
    return data;
}

DataType MakeString(const std::string& text) {
    DataType data;
    char* buffer = new char[text.size()];
    memcpy(buffer, text.data(), text.size());
    data.InitStringType(static_cast<uint32_t>(text.size()), buffer);
    return data;
}

}  // namespace

TEST(TdmsDataType, StaticLengthMatchesTheTdmsSpecForEveryScalarType) {
    EXPECT_EQ(DataType::GetLength(TDMSType::Empty), 0u);
    EXPECT_EQ(DataType::GetLength(TDMSType::Void), 1u);
    EXPECT_EQ(DataType::GetLength(TDMSType::Integer8), 1u);
    EXPECT_EQ(DataType::GetLength(TDMSType::Integer16), 2u);
    EXPECT_EQ(DataType::GetLength(TDMSType::Integer32), 4u);
    EXPECT_EQ(DataType::GetLength(TDMSType::Integer64), 8u);
    EXPECT_EQ(DataType::GetLength(TDMSType::UnsignedInteger8), 1u);
    EXPECT_EQ(DataType::GetLength(TDMSType::UnsignedInteger16), 2u);
    EXPECT_EQ(DataType::GetLength(TDMSType::UnsignedInteger32), 4u);
    EXPECT_EQ(DataType::GetLength(TDMSType::UnsignedInteger64), 8u);
    EXPECT_EQ(DataType::GetLength(TDMSType::SingleFloat), 4u);
    EXPECT_EQ(DataType::GetLength(TDMSType::SingleFloatWithUnit), 4u);
    EXPECT_EQ(DataType::GetLength(TDMSType::DoubleFloat), 8u);
    EXPECT_EQ(DataType::GetLength(TDMSType::DoubleFloatWithUnit), 8u);
    EXPECT_EQ(DataType::GetLength(TDMSType::Boolean), 1u);
    EXPECT_EQ(DataType::GetLength(TDMSType::TimeStamp), 16u);
}

TEST(TdmsDataType, StaticLengthIsZeroForATypeItCannotSize) {
    EXPECT_EQ(DataType::GetLength(TDMSType::ExtendedFloat), 0u);
}

TEST(TdmsDataType, ArrayLengthIsElementSizeTimesCount) {
    EXPECT_EQ(DataType::GetArrayLength(TDMSType::SingleFloat, 0u), 0u);
    EXPECT_EQ(DataType::GetArrayLength(TDMSType::SingleFloat, 10u), 40u);
    EXPECT_EQ(DataType::GetArrayLength(TDMSType::DoubleFloat, 10u), 80u);
    EXPECT_EQ(DataType::GetArrayLength(TDMSType::Integer8, 1000u), 1000u);
    EXPECT_EQ(DataType::GetArrayLength(TDMSType::TimeStamp, 4u), 64u);
}

TEST(TdmsDataType, DefaultConstructedIsEmptyWithNoRawData) {
    DataType data;
    EXPECT_EQ(data.GetDataType(), TDMSType::Empty);
    EXPECT_EQ(data.GetRawData(), nullptr);
    EXPECT_EQ(data.ToTypeString(), "Empty");
    EXPECT_EQ(data.ToString(), "Empty");
}

TEST(TdmsDataType, ScalarRoundTripsThroughMakeDataAndGetData) {
    EXPECT_EQ(MakeScalar<int8_t>(TDMSType::Integer8, -8).GetData<int8_t>(), -8);
    EXPECT_EQ(MakeScalar<int16_t>(TDMSType::Integer16, -300).GetData<int16_t>(), -300);
    EXPECT_EQ(MakeScalar<int32_t>(TDMSType::Integer32, -70000).GetData<int32_t>(), -70000);
    EXPECT_EQ(MakeScalar<int64_t>(TDMSType::Integer64, -5000000000ll).GetData<int64_t>(), -5000000000ll);
    EXPECT_EQ(MakeScalar<uint8_t>(TDMSType::UnsignedInteger8, 200).GetData<uint8_t>(), 200u);
    EXPECT_EQ(MakeScalar<uint16_t>(TDMSType::UnsignedInteger16, 60000).GetData<uint16_t>(), 60000u);
    EXPECT_EQ(MakeScalar<uint32_t>(TDMSType::UnsignedInteger32, 4000000000u).GetData<uint32_t>(), 4000000000u);
    EXPECT_EQ(MakeScalar<uint64_t>(TDMSType::UnsignedInteger64, 18000000000000000000ull).GetData<uint64_t>(), 18000000000000000000ull);
    EXPECT_FLOAT_EQ(MakeScalar<float>(TDMSType::SingleFloat, 1.5f).GetData<float>(), 1.5f);
    EXPECT_DOUBLE_EQ(MakeScalar<double>(TDMSType::DoubleFloat, -2.25).GetData<double>(), -2.25);
}

TEST(TdmsDataType, InstanceLengthFollowsTheTypeForScalars) {
    EXPECT_EQ(MakeScalar<int32_t>(TDMSType::Integer32, 1).GetLength(), 4u);
    EXPECT_EQ(MakeScalar<double>(TDMSType::DoubleFloat, 1.0).GetLength(), 8u);
}

TEST(TdmsDataType, TypeStringIsStableForEveryNamedType) {
    EXPECT_EQ(MakeScalar<int8_t>(TDMSType::Integer8, 1).ToTypeString(), "Integer8");
    EXPECT_EQ(MakeScalar<int16_t>(TDMSType::Integer16, 1).ToTypeString(), "Integer16");
    EXPECT_EQ(MakeScalar<int32_t>(TDMSType::Integer32, 1).ToTypeString(), "Integer32");
    EXPECT_EQ(MakeScalar<int64_t>(TDMSType::Integer64, 1).ToTypeString(), "Integer64");
    EXPECT_EQ(MakeScalar<uint8_t>(TDMSType::UnsignedInteger8, 1).ToTypeString(), "UInteger8");
    EXPECT_EQ(MakeScalar<uint16_t>(TDMSType::UnsignedInteger16, 1).ToTypeString(), "UInteger16");
    EXPECT_EQ(MakeScalar<uint32_t>(TDMSType::UnsignedInteger32, 1).ToTypeString(), "UInteger32");
    EXPECT_EQ(MakeScalar<uint64_t>(TDMSType::UnsignedInteger64, 1).ToTypeString(), "UInteger64");
    EXPECT_EQ(MakeScalar<float>(TDMSType::SingleFloat, 1.f).ToTypeString(), "SingleFloat");
    EXPECT_EQ(MakeScalar<double>(TDMSType::DoubleFloat, 1.0).ToTypeString(), "DoubleFloat");
    EXPECT_EQ(MakeScalar<uint8_t>(TDMSType::Boolean, 1).ToTypeString(), "Boolean");
    EXPECT_EQ(MakeString("x").ToTypeString(), "String");
}

TEST(TdmsDataType, ToStringRendersScalarsInTheirNativeFormat) {
    EXPECT_EQ(MakeScalar<int32_t>(TDMSType::Integer32, -12345).ToString(), "-12345");
    EXPECT_EQ(MakeScalar<uint16_t>(TDMSType::UnsignedInteger16, 65535).ToString(), "65535");
    EXPECT_EQ(MakeScalar<float>(TDMSType::SingleFloat, 2.5f).ToString(), "2.500000");
    EXPECT_EQ(MakeScalar<double>(TDMSType::DoubleFloat, -0.125).ToString(), "-0.125000");
}

TEST(TdmsDataType, ToStringRendersBooleanAsTrueOrFalse) {
    EXPECT_EQ(MakeScalar<uint8_t>(TDMSType::Boolean, 0).ToString(), "false");
    EXPECT_EQ(MakeScalar<uint8_t>(TDMSType::Boolean, 1).ToString(), "true");
}

TEST(TdmsDataType, StringTypeKeepsItsExplicitLengthAndContent) {
    DataType data = MakeString("Voltage");
    EXPECT_EQ(data.GetDataType(), TDMSType::String);
    EXPECT_EQ(data.GetLength(), 7u);
    EXPECT_EQ(data.GetDataString(), "Voltage");
    EXPECT_EQ(data.ToString(), "Voltage");
}

TEST(TdmsDataType, StringTypeIsNotNulTerminatedAndMayContainEmbeddedNuls) {
    const std::string raw("a\0b", 3);
    DataType data = MakeString(raw);
    EXPECT_EQ(data.GetLength(), 3u);
    EXPECT_EQ(data.GetDataString(), raw);
}

TEST(TdmsDataType, CopyConstructorDeepCopiesTheRawBuffer) {
    DataType original = MakeScalar<int32_t>(TDMSType::Integer32, 42);
    DataType copy(original);

    ASSERT_NE(copy.GetRawData(), nullptr);
    EXPECT_NE(copy.GetRawData(), original.GetRawData());
    EXPECT_EQ(copy.GetData<int32_t>(), 42);

    static_cast<int32_t*>(original.GetRawData())[0] = 7;
    EXPECT_EQ(copy.GetData<int32_t>(), 42);
}

TEST(TdmsDataType, CopyAssignmentDeepCopiesTheRawBuffer) {
    DataType original = MakeScalar<double>(TDMSType::DoubleFloat, 3.5);
    DataType copy;
    copy = original;

    ASSERT_NE(copy.GetRawData(), nullptr);
    EXPECT_NE(copy.GetRawData(), original.GetRawData());
    EXPECT_DOUBLE_EQ(copy.GetData<double>(), 3.5);
}

TEST(TdmsDataType, CopyOfAStringKeepsLengthAndContentIndependently) {
    DataType original = MakeString("Voltage");
    DataType copy(original);

    EXPECT_NE(copy.GetRawData(), original.GetRawData());
    EXPECT_EQ(copy.GetLength(), 7u);
    EXPECT_EQ(copy.GetDataString(), "Voltage");
}

TEST(TdmsDataType, MoveConstructorTakesOverTheBufferAndEmptiesTheSource) {
    DataType original = MakeScalar<int32_t>(TDMSType::Integer32, 99);
    void* rawBefore = original.GetRawData();

    DataType moved(std::move(original));

    EXPECT_EQ(moved.GetRawData(), rawBefore);
    EXPECT_EQ(moved.GetData<int32_t>(), 99);
    EXPECT_EQ(original.GetRawData(), nullptr);
    EXPECT_EQ(original.GetDataType(), TDMSType::Empty);
}

TEST(TdmsDataType, CopyOfADefaultConstructedValueStaysEmpty) {
    DataType original;
    DataType copy(original);
    EXPECT_EQ(copy.GetDataType(), TDMSType::Empty);
    EXPECT_EQ(copy.GetRawData(), nullptr);
}

TEST(TdmsDataType, InitRawRecordsTypeAndTotalByteSizeOfTheArray) {
    DataType data;
    auto* samples = new uint8_t[4 * sizeof(float)]();
    data.InitRaw(TDMSType::SingleFloat, 4, samples);

    EXPECT_EQ(data.GetDataType(), TDMSType::SingleFloat);
    auto vec = data.GetRawVector();
    ASSERT_EQ(vec.size(), 1u);
    EXPECT_EQ(vec[0]->dataType, TDMSType::SingleFloat);
    EXPECT_EQ(vec[0]->size, 4u * sizeof(float));
    EXPECT_EQ(vec[0]->data, samples);

    delete[] samples;
}

TEST(TdmsDataType, InitRawAppendsRatherThanReplacingPreviousArrays) {
    DataType data;
    auto* first = new uint8_t[2 * sizeof(float)]();
    auto* second = new uint8_t[3 * sizeof(float)]();
    data.InitRaw(TDMSType::SingleFloat, 2, first);
    data.InitRaw(TDMSType::SingleFloat, 3, second);

    auto vec = data.GetRawVector();
    ASSERT_EQ(vec.size(), 2u);
    EXPECT_EQ(vec[0]->size, 2u * sizeof(float));
    EXPECT_EQ(vec[1]->size, 3u * sizeof(float));

    delete[] first;
    delete[] second;
}

TEST(TdmsDataType, InitRawDoesNotTakeOwnershipOfTheCallersBuffer) {
    auto* samples = new uint8_t[sizeof(double)]();
    {
        DataType data;
        data.InitRaw(TDMSType::DoubleFloat, 1, samples);
    }
    samples[0] = 1;
    EXPECT_EQ(samples[0], 1u);
    delete[] samples;
}

TEST(TdmsDataType, RawTimeValueHasZeroSubsecondsAndAdvancesOneForOneWithTimeT) {
    const time_t base = 1000000000;
    uint64_t* a = DataType::GetRawTimeValue(base);
    uint64_t* b = DataType::GetRawTimeValue(base + 60);

    EXPECT_EQ(a[0], 0u);
    EXPECT_EQ(b[0], 0u);
    EXPECT_EQ(b[1] - a[1], 60u);
    EXPECT_GT(a[1], static_cast<uint64_t>(base));

    delete[] a;
    delete[] b;
}

TEST(TdmsDataType, TimeStampToStringRendersTheEncodedCalendarDate) {
    auto* value = DataType::GetRawTimeValue(0);
    DataType data;
    data.InitDataType(TDMSType::TimeStamp, value);

    const std::string text = data.ToString();
    EXPECT_NE(text.find("year: 1970"), std::string::npos) << text;
    EXPECT_NE(text.find("month: 01"), std::string::npos) << text;
}
