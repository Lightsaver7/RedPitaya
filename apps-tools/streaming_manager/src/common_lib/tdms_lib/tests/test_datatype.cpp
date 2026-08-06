/**
 * DataType memory semantics.
 *
 * This is the regression suite for the ownership defects: a throwing
 * destructor, a leaking copy-assignment, self-assignment that dropped the
 * payload, and delete[] through the wrong element type. Several of the
 * guarantees are static_asserts so they hold at compile time rather than only
 * when a test happens to exercise the path. Run under
 * -DTDMS_ENABLE_SANITIZERS=ON for the leak and double-free coverage.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include "data_type.h"

using TDMS::DataType;
using TDMS::TDMSType;

// The destructor used to be noexcept(false) and threw for several
// type/pointer combinations, so destroying a DataType while unwinding
// terminated the process - and DataType lives inside map<string, DataType>.
static_assert(std::is_nothrow_destructible<DataType>::value, "~DataType must not throw");
static_assert(std::is_copy_constructible<DataType>::value);
static_assert(std::is_copy_assignable<DataType>::value);
static_assert(std::is_move_constructible<DataType>::value);
static_assert(std::is_move_assignable<DataType>::value);

TEST(DataTypeMemory, StaticLengthMatchesTheOnDiskWidth) {
    EXPECT_EQ(DataType::GetLength(TDMSType::Integer8), 1u);
    EXPECT_EQ(DataType::GetLength(TDMSType::Integer16), 2u);
    EXPECT_EQ(DataType::GetLength(TDMSType::Integer32), 4u);
    EXPECT_EQ(DataType::GetLength(TDMSType::Integer64), 8u);
    EXPECT_EQ(DataType::GetLength(TDMSType::UnsignedInteger8), 1u);
    EXPECT_EQ(DataType::GetLength(TDMSType::UnsignedInteger16), 2u);
    EXPECT_EQ(DataType::GetLength(TDMSType::UnsignedInteger32), 4u);
    EXPECT_EQ(DataType::GetLength(TDMSType::UnsignedInteger64), 8u);
    EXPECT_EQ(DataType::GetLength(TDMSType::SingleFloat), 4u);
    EXPECT_EQ(DataType::GetLength(TDMSType::DoubleFloat), 8u);
    EXPECT_EQ(DataType::GetLength(TDMSType::Boolean), 1u);
    EXPECT_EQ(DataType::GetLength(TDMSType::TimeStamp), 16u);
}

TEST(DataTypeMemory, StringHasNoFixedWidthAndArrayLengthIsZero) {
    // Used to return (uint32_t)-1, which made GetArrayLength(String, 4) come
    // out as 17179869180 - the reader then tried to read 17 GB for a String
    // channel carrying a fixed-width raw data index.
    EXPECT_EQ(DataType::GetLength(TDMSType::String), 0u);
    EXPECT_EQ(DataType::GetArrayLength(TDMSType::String, 4), 0u);
}

TEST(DataTypeMemory, ArrayLengthIsWidthTimesCount) {
    EXPECT_EQ(DataType::GetArrayLength(TDMSType::SingleFloat, 0), 0u);
    EXPECT_EQ(DataType::GetArrayLength(TDMSType::SingleFloat, 10), 40u);
    EXPECT_EQ(DataType::GetArrayLength(TDMSType::DoubleFloat, 10), 80u);
    EXPECT_EQ(DataType::GetArrayLength(TDMSType::TimeStamp, 4), 64u);
}

TEST(DataTypeMemory, DefaultConstructedIsEmptyWithNoPayload) {
    DataType value;
    EXPECT_EQ(value.GetDataType(), TDMSType::Empty);
    EXPECT_EQ(value.GetRawData(), nullptr);
}

TEST(DataTypeMemory, AdoptedScalarIsReadableAndFreedExactlyOnce) {
    DataType value;
    value.InitDataType(TDMSType::Integer32, DataType::MakeData<std::int32_t>(-123456));
    EXPECT_EQ(value.GetDataType(), TDMSType::Integer32);
    EXPECT_EQ(value.GetLength(), 4u);
    EXPECT_EQ(value.GetData<std::int32_t>(), -123456);
}

TEST(DataTypeMemory, AdoptingANullPointerIsSafe) {
    // Reader::ReadMetadata does exactly this to record a channel's element type
    // before its samples are available.
    DataType value;
    value.InitDataType(TDMSType::Integer32, nullptr);
    EXPECT_EQ(value.GetDataType(), TDMSType::Integer32);
    EXPECT_EQ(value.GetRawData(), nullptr);
    EXPECT_EQ(value.GetData<std::int32_t>(), 0) << "reading an absent payload must be defined";
}

TEST(DataTypeMemory, SelfAssignmentPreservesThePayload) {
    // The old operator= nulled m_rawData before testing the source, so a = a
    // silently dropped the data (and leaked the buffer).
    DataType value;
    value.InitDataType(TDMSType::Integer32, DataType::MakeData<std::int32_t>(0x11223344));

    const DataType& alias = value;
    value = alias;

    EXPECT_EQ(value.GetDataType(), TDMSType::Integer32);
    EXPECT_EQ(value.GetData<std::int32_t>(), 0x11223344);
}

TEST(DataTypeMemory, CopyIsDeepAndIndependent) {
    DataType original;
    original.InitDataType(TDMSType::Integer32, DataType::MakeData<std::int32_t>(42));
    DataType copy = original;

    ASSERT_NE(copy.GetRawData(), nullptr);
    EXPECT_NE(copy.GetRawData(), original.GetRawData());
    EXPECT_EQ(copy.GetData<std::int32_t>(), 42);

    static_cast<std::int32_t*>(original.GetRawData())[0] = 7;
    EXPECT_EQ(copy.GetData<std::int32_t>(), 42);
    EXPECT_EQ(original.GetData<std::int32_t>(), 7);
}

TEST(DataTypeMemory, RepeatedCopyAssignmentDoesNotLeak) {
    // Regression for the missing delete in operator=. Meaningful under ASan;
    // here it also pins the resulting value.
    DataType target;
    for (std::int32_t i = 0; i < 1000; i++) {
        DataType source;
        source.InitDataType(TDMSType::Integer32, DataType::MakeData<std::int32_t>(i));
        target = source;
    }
    EXPECT_EQ(target.GetData<std::int32_t>(), 999);
}

TEST(DataTypeMemory, DestroyingAnEmptyTypedValueWithAPayloadDoesNotThrow) {
    // The old destructor threw std::runtime_error for Empty and Void with a
    // non-null buffer.
    EXPECT_NO_THROW({
        DataType value;
        value.InitDataType(TDMSType::Empty, DataType::MakeData<std::int32_t>(1));
    });
    EXPECT_NO_THROW({
        DataType value;
        value.InitDataType(TDMSType::Void, DataType::MakeData<std::int32_t>(1));
    });
}

TEST(DataTypeMemory, DestructorRunsCleanlyWhileUnwinding) {
    // A throwing destructor during unwinding is std::terminate, which no test
    // can catch - so this asserts the surviving path explicitly.
    struct Thrower {
        ~Thrower() = default;
    };
    EXPECT_THROW(
        {
            DataType value;
            value.InitDataType(TDMSType::Integer32, DataType::MakeData<std::int32_t>(1));
            throw std::runtime_error("unwind");
        },
        std::runtime_error);
}

TEST(DataTypeMemory, MoveTransfersThePayload) {
    DataType source;
    source.InitDataType(TDMSType::DoubleFloat, DataType::MakeData<double>(-2.25));
    DataType moved = std::move(source);
    EXPECT_EQ(moved.GetDataType(), TDMSType::DoubleFloat);
    EXPECT_DOUBLE_EQ(moved.GetData<double>(), -2.25);
}

TEST(DataTypeMemory, MoveAssignmentOntoALiveValueIsSafe) {
    DataType target;
    target.InitDataType(TDMSType::Integer32, DataType::MakeData<std::int32_t>(1));
    DataType source;
    source.InitDataType(TDMSType::DoubleFloat, DataType::MakeData<double>(3.5));

    target = std::move(source);

    EXPECT_EQ(target.GetDataType(), TDMSType::DoubleFloat);
    EXPECT_DOUBLE_EQ(target.GetData<double>(), 3.5);
}

TEST(DataTypeMemory, StringKeepsItsExplicitLengthAndContent) {
    const std::string text = "Voltage";
    auto* buffer = new std::uint8_t[text.size()];
    std::memcpy(buffer, text.data(), text.size());
    DataType value;
    value.InitStringType(static_cast<std::uint32_t>(text.size()), buffer);

    EXPECT_EQ(value.GetDataType(), TDMSType::String);
    EXPECT_EQ(value.GetLength(), text.size());
    EXPECT_EQ(value.GetDataString(), text);
}

TEST(DataTypeMemory, StringPreservesEmbeddedNulsAndIsNotTerminated) {
    const std::string text("a\0b", 3);
    auto* buffer = new std::uint8_t[text.size()];
    std::memcpy(buffer, text.data(), text.size());
    DataType value;
    value.InitStringType(static_cast<std::uint32_t>(text.size()), buffer);
    EXPECT_EQ(value.GetLength(), 3u);
    EXPECT_EQ(value.GetDataString(), text);
}

TEST(DataTypeMemory, SurvivesContainerChurn) {
    // map<string, DataType> and vector<DataType> are how the library actually
    // stores these; both used to be able to hit the throwing destructor.
    std::map<std::string, DataType> properties;
    for (int i = 0; i < 100; i++) {
        DataType value;
        value.InitDataType(TDMSType::Integer32, DataType::MakeData<std::int32_t>(i));
        properties["key" + std::to_string(i)] = value;
    }
    std::map<std::string, DataType> copy = properties;
    properties.erase("key0");
    copy["key1"] = copy.at("key2");
    EXPECT_EQ(copy.at("key1").GetData<std::int32_t>(), 2);

    std::vector<DataType> values;
    for (int i = 0; i < 100; i++) {
        DataType value;
        value.InitDataType(TDMSType::DoubleFloat, DataType::MakeData<double>(i));
        values.push_back(value);   // forces reallocation, so copies and moves
    }
    EXPECT_DOUBLE_EQ(values.back().GetData<double>(), 99.0);
}

TEST(DataTypeMemory, RawVectorEntriesReportTheirSizeInBytes) {
    auto buffer = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[4 * sizeof(float)]);
    DataType value;
    value.InitRaw(TDMSType::SingleFloat, 4, buffer);

    auto vec = value.GetRawVector();
    ASSERT_EQ(vec.size(), 1u);
    EXPECT_EQ(vec[0]->size, 4u * sizeof(float)) << "Raw::size is documented as bytes";
    EXPECT_EQ(vec[0]->dataType, TDMSType::SingleFloat);
    EXPECT_EQ(vec[0]->data, buffer);
}

TEST(DataTypeMemory, ToStringOfLargeDoubleDoesNotOverflowItsBuffer) {
    // ToString used to sprintf("%lf", ...) into char[22]; "%f" of DBL_MAX needs
    // more than 300 characters.
    DataType value;
    value.InitDataType(TDMSType::DoubleFloat, DataType::MakeData<double>(1.7976931348623157e308));
    const std::string text = value.ToString();
    EXPECT_GT(text.size(), 300u);
    EXPECT_EQ(text.find_first_not_of("0123456789.-"), std::string::npos) << text.substr(0, 40);
}

TEST(DataTypeMemory, ToStringOfAnUnknownTypeIsDeterministic) {
    // The default arm used to return std::string(cstr) with cstr uninitialised.
    DataType value;
    value.InitDataType(TDMSType::ExtendedFloat, nullptr);
    EXPECT_EQ(value.ToString(), "Error");
    EXPECT_EQ(value.ToTypeString(), "Error");
}

TEST(DataTypeMemory, TypeStringIsStableForEveryNamedType) {
    const struct {
        TDMSType type;
        const char* name;
    } cases[] = {
        {TDMSType::Empty, "Empty"},          {TDMSType::Void, "Void"},
        {TDMSType::Integer8, "Integer8"},    {TDMSType::Integer16, "Integer16"},
        {TDMSType::Integer32, "Integer32"},  {TDMSType::Integer64, "Integer64"},
        {TDMSType::UnsignedInteger8, "UInteger8"}, {TDMSType::UnsignedInteger16, "UInteger16"},
        {TDMSType::UnsignedInteger32, "UInteger32"}, {TDMSType::UnsignedInteger64, "UInteger64"},
        {TDMSType::SingleFloat, "SingleFloat"}, {TDMSType::DoubleFloat, "DoubleFloat"},
        {TDMSType::Boolean, "Boolean"},      {TDMSType::TimeStamp, "TimeStamp"},
        {TDMSType::String, "String"},
    };
    for (const auto& c : cases) {
        DataType value;
        value.InitDataType(c.type, nullptr);
        EXPECT_EQ(value.ToTypeString(), c.name);
    }
}
