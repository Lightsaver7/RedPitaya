/**
 * Unit tests for src/tdms/tdms_types.h - TDMS type sizing, the 1904 epoch
 * constant, the owning property Value and the borrowing RawView.
 *
 * A good part of this file exists to pin down the memory-safety properties
 * that the old TDMS::DataType got wrong: the destructor must not throw, a
 * value must never be default-constructible into a state whose destructor
 * has to guess, copies must be independent, self-assignment must be safe,
 * and repeatedly overwriting a value in a container must not leak. Several of
 * those are expressed as static_assert so they are checked at compile time
 * rather than only when a test happens to exercise the path.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include "tdms_types.h"

using namespace rp_formatter_api::tdms;

// --- Compile-time guarantees ------------------------------------------------
// The old DataType had `~DataType() noexcept(false)` and threw std::runtime_error
// for several type/pointer combinations, so destroying one during stack
// unwinding terminated the process.
static_assert(std::is_nothrow_destructible_v<Value>);
static_assert(std::is_copy_constructible_v<Value>);
static_assert(std::is_copy_assignable_v<Value>);
static_assert(std::is_nothrow_move_constructible_v<Value>);
static_assert(std::is_nothrow_move_assignable_v<Value>);
// No default constructor: a Value's type code and payload width are fixed
// together by a factory, so a mismatched pair is not representable.
static_assert(!std::is_default_constructible_v<Value>);

static_assert(std::is_trivially_copyable_v<RawView>);
static_assert(std::is_trivially_destructible_v<RawView>);

namespace {

std::vector<std::uint8_t> Bytes(std::initializer_list<int> values) {
    std::vector<std::uint8_t> out;
    for (int v : values) {
        out.push_back(static_cast<std::uint8_t>(v));
    }
    return out;
}

// Restores TZ on scope exit so one test cannot leak a timezone into the rest
// of the binary.
class ScopedTimezone {
   public:
    explicit ScopedTimezone(const char* _tz) {
        const char* previous = std::getenv("TZ");
        m_hadTz = previous != nullptr;
        if (m_hadTz) {
            m_previous = previous;
        }
        setenv("TZ", _tz, 1);
        tzset();
    }

    ~ScopedTimezone() {
        if (m_hadTz) {
            setenv("TZ", m_previous.c_str(), 1);
        } else {
            unsetenv("TZ");
        }
        tzset();
    }

   private:
    bool m_hadTz = false;
    std::string m_previous;
};

}  // namespace

// --- Type sizing -----------------------------------------------------------

TEST(TdmsTypes, TypeSizeMatchesTheOnDiskWidthOfEveryFixedWidthType) {
    EXPECT_EQ(TypeSize(Type::Int8), 1u);
    EXPECT_EQ(TypeSize(Type::UInt8), 1u);
    EXPECT_EQ(TypeSize(Type::Boolean), 1u);
    EXPECT_EQ(TypeSize(Type::Int16), 2u);
    EXPECT_EQ(TypeSize(Type::UInt16), 2u);
    EXPECT_EQ(TypeSize(Type::Int32), 4u);
    EXPECT_EQ(TypeSize(Type::UInt32), 4u);
    EXPECT_EQ(TypeSize(Type::Float), 4u);
    EXPECT_EQ(TypeSize(Type::Int64), 8u);
    EXPECT_EQ(TypeSize(Type::UInt64), 8u);
    EXPECT_EQ(TypeSize(Type::Double), 8u);
    EXPECT_EQ(TypeSize(Type::Timestamp), 16u);
}

TEST(TdmsTypes, TypeSizeIsZeroForTheLengthPrefixedStringType) {
    EXPECT_EQ(TypeSize(Type::String), 0u);
}

TEST(TdmsTypes, TypeCodesMatchTheTdmsSpecification) {
    EXPECT_EQ(static_cast<std::uint32_t>(Type::Int8), 0x01u);
    EXPECT_EQ(static_cast<std::uint32_t>(Type::Int16), 0x02u);
    EXPECT_EQ(static_cast<std::uint32_t>(Type::Int32), 0x03u);
    EXPECT_EQ(static_cast<std::uint32_t>(Type::Int64), 0x04u);
    EXPECT_EQ(static_cast<std::uint32_t>(Type::UInt8), 0x05u);
    EXPECT_EQ(static_cast<std::uint32_t>(Type::UInt16), 0x06u);
    EXPECT_EQ(static_cast<std::uint32_t>(Type::UInt32), 0x07u);
    EXPECT_EQ(static_cast<std::uint32_t>(Type::UInt64), 0x08u);
    EXPECT_EQ(static_cast<std::uint32_t>(Type::Float), 0x09u);
    EXPECT_EQ(static_cast<std::uint32_t>(Type::Double), 0x0Au);
    EXPECT_EQ(static_cast<std::uint32_t>(Type::String), 0x20u);
    EXPECT_EQ(static_cast<std::uint32_t>(Type::Boolean), 0x21u);
    EXPECT_EQ(static_cast<std::uint32_t>(Type::Timestamp), 0x44u);
}

// --- 1904 epoch ------------------------------------------------------------

TEST(TdmsTypes, EpochConstantMatchesTheTdmsSpecification) {
    EXPECT_EQ(kSecondsFrom1904To1970, 2082844800LL);
}

TEST(TdmsTypes, EpochConstantMatchesTheOldLeapYearComputation) {
    // The previous CTDMSWriter summed day counts over 1904..1969 at runtime.
    // Recomputing it here proves the constant is not a new value and that
    // existing files keep the same timestamps.
    const auto isLeapYear = [](int year) {
        if (year % 400 == 0) {
            return true;
        }
        if (year % 100 == 0) {
            return false;
        }
        return year % 4 == 0;
    };

    std::int64_t seconds = 0;
    for (int year = 1904; year < 1970; year++) {
        seconds += 24 * 60 * 60 * (isLeapYear(year) ? 366 : 365);
    }
    EXPECT_EQ(seconds, kSecondsFrom1904To1970);
}

TEST(TdmsTypes, TimestampIsIndependentOfTheLocalTimezone) {
    // The old epoch helper went through mktime(), which interprets its input
    // as local time; the recorded UTC timestamp therefore shifted with the
    // machine's TZ setting.
    const std::int64_t unixSeconds = 1'700'000'000;

    Value utc = Value::TimestampFromUnixSeconds(unixSeconds);
    std::vector<std::uint8_t> shifted;
    {
        ScopedTimezone tz("Pacific/Kiritimati");  // UTC+14
        shifted = Value::TimestampFromUnixSeconds(unixSeconds).payload();
    }
    std::vector<std::uint8_t> behind;
    {
        ScopedTimezone tz("Pacific/Midway");  // UTC-11
        behind = Value::TimestampFromUnixSeconds(unixSeconds).payload();
    }

    EXPECT_EQ(shifted, utc.payload());
    EXPECT_EQ(behind, utc.payload());
}

// --- Value: payload encoding -----------------------------------------------

TEST(TdmsTypes, ScalarPayloadIsTheValueInLittleEndianWithNoPadding) {
    EXPECT_EQ(Value::Int8(-1).payload(), Bytes({0xFF}));
    EXPECT_EQ(Value::Int16(-2).payload(), Bytes({0xFE, 0xFF}));
    EXPECT_EQ(Value::Int32(-2).payload(), Bytes({0xFE, 0xFF, 0xFF, 0xFF}));
    EXPECT_EQ(Value::UInt8(200).payload(), Bytes({0xC8}));
    EXPECT_EQ(Value::UInt16(0x1234).payload(), Bytes({0x34, 0x12}));
    EXPECT_EQ(Value::UInt32(0x12345678u).payload(), Bytes({0x78, 0x56, 0x34, 0x12}));
    EXPECT_EQ(Value::UInt64(0x0123456789ABCDEFull).payload(), Bytes({0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01}));
    EXPECT_EQ(Value::Float(1.0f).payload(), Bytes({0x00, 0x00, 0x80, 0x3F}));
    EXPECT_EQ(Value::Double(1.0).payload(), Bytes({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F}));
}

TEST(TdmsTypes, EveryFactorySetsATypeWhoseSizeMatchesItsPayload) {
    const Value values[] = {
        Value::Int8(1),   Value::Int16(1),  Value::Int32(1),  Value::Int64(1),     Value::UInt8(1),
        Value::UInt16(1), Value::UInt32(1), Value::UInt64(1), Value::Float(1.f),   Value::Double(1.0),
        Value::Boolean(true), Value::Timestamp(0), Value::TimestampFromUnixSeconds(0),
    };
    for (const auto& value : values) {
        EXPECT_EQ(value.payload().size(), TypeSize(value.type())) << "type code " << static_cast<std::uint32_t>(value.type());
    }
}

TEST(TdmsTypes, BooleanIsEncodedAsASingleZeroOrOneByte) {
    EXPECT_EQ(Value::Boolean(false).type(), Type::Boolean);
    EXPECT_EQ(Value::Boolean(false).payload(), Bytes({0x00}));
    EXPECT_EQ(Value::Boolean(true).payload(), Bytes({0x01}));
}

TEST(TdmsTypes, StringPayloadCarriesItsOwnLengthPrefix) {
    const Value value = Value::String("Voltage");
    EXPECT_EQ(value.type(), Type::String);
    ASSERT_EQ(value.payload().size(), 4u + 7u);
    EXPECT_EQ(std::vector<std::uint8_t>(value.payload().begin(), value.payload().begin() + 4), Bytes({0x07, 0x00, 0x00, 0x00}));
    EXPECT_EQ(std::string(value.payload().begin() + 4, value.payload().end()), "Voltage");
}

TEST(TdmsTypes, StringPayloadPreservesEmbeddedNulsAndIsNotTerminated) {
    const std::string text("a\0b", 3);
    const Value value = Value::String(text);
    ASSERT_EQ(value.payload().size(), 4u + 3u);
    EXPECT_EQ(std::string(value.payload().begin() + 4, value.payload().end()), text);
}

TEST(TdmsTypes, TimestampPayloadIsFractionsThenSeconds) {
    const Value value = Value::Timestamp(0x2222222222222222LL, 0x1111111111111111ull);
    ASSERT_EQ(value.payload().size(), 16u);
    EXPECT_EQ(std::vector<std::uint8_t>(value.payload().begin(), value.payload().begin() + 8),
              Bytes({0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11}));
    EXPECT_EQ(std::vector<std::uint8_t>(value.payload().begin() + 8, value.payload().end()),
              Bytes({0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22}));
}

TEST(TdmsTypes, TimestampDefaultsToWholeSecondResolution) {
    const Value value = Value::Timestamp(1234);
    ASSERT_EQ(value.payload().size(), 16u);
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(value.payload()[static_cast<std::size_t>(i)], 0u) << "fraction byte " << i;
    }
}

TEST(TdmsTypes, TimestampFromUnixSecondsShiftsByTheEpochConstant) {
    EXPECT_EQ(Value::TimestampFromUnixSeconds(0).payload(), Value::Timestamp(kSecondsFrom1904To1970).payload());
    EXPECT_EQ(Value::TimestampFromUnixSeconds(1234).payload(), Value::Timestamp(kSecondsFrom1904To1970 + 1234).payload());
}

TEST(TdmsTypes, TimestampAdvancesOneForOneWithUnixSeconds) {
    const auto secondsOf = [](const Value& value) {
        std::int64_t seconds = 0;
        std::memcpy(&seconds, value.payload().data() + 8, sizeof(seconds));
        return seconds;
    };
    const Value a = Value::TimestampFromUnixSeconds(1'000'000'000);
    const Value b = Value::TimestampFromUnixSeconds(1'000'000'060);
    EXPECT_EQ(secondsOf(b) - secondsOf(a), 60);
}

// --- Value: ownership ------------------------------------------------------

TEST(TdmsTypes, CopiesAreIndependentOfTheOriginal) {
    const Value original = Value::UInt64(42);
    Value copy = original;
    EXPECT_EQ(copy.payload(), original.payload());
    EXPECT_NE(copy.payload().data(), original.payload().data());
}

TEST(TdmsTypes, CopyAssignmentReplacesTheWholeValue) {
    Value target = Value::Int32(1);
    const Value source = Value::String("Voltage");

    target = source;

    EXPECT_EQ(target.type(), Type::String);
    EXPECT_EQ(target.payload(), source.payload());
    EXPECT_NE(target.payload().data(), source.payload().data());
}

TEST(TdmsTypes, SelfAssignmentIsSafeAndLeavesTheValueIntact) {
    // The old DataType::operator= had no self-assignment guard and nulled its
    // buffer pointer before reallocating.
    Value value = Value::String("Voltage");
    const std::vector<std::uint8_t> before = value.payload();

    const Value& alias = value;
    value = alias;

    EXPECT_EQ(value.type(), Type::String);
    EXPECT_EQ(value.payload(), before);
}

TEST(TdmsTypes, MoveConstructionTransfersThePayload) {
    Value source = Value::Double(-2.25);
    const std::vector<std::uint8_t> expected = source.payload();

    const Value moved = std::move(source);

    EXPECT_EQ(moved.type(), Type::Double);
    EXPECT_EQ(moved.payload(), expected);
}

TEST(TdmsTypes, RepeatedOverwriteInAMapLeavesOneLiveValue) {
    // Regression for the old copy-assignment leak: every
    // `Properties[key] = value` used to leak the buffer it was replacing.
    // Meaningful under -fsanitize=address; here it also pins the semantics.
    std::map<std::string, Value> properties;
    properties.insert_or_assign("osc_rate", Value::UInt64(0));
    for (std::uint64_t i = 1; i <= 1000; i++) {
        properties.insert_or_assign("osc_rate", Value::UInt64(i));
    }

    ASSERT_EQ(properties.size(), 1u);
    EXPECT_EQ(properties.at("osc_rate").payload(), Value::UInt64(1000).payload());
}

// --- RawView ---------------------------------------------------------------

TEST(TdmsTypes, RawViewByteCountIsSampleCountTimesTypeSize) {
    const std::uint8_t buffer[64] = {};
    EXPECT_EQ((RawView{Type::Float, buffer, 0}.byteCount()), 0u);
    EXPECT_EQ((RawView{Type::Float, buffer, 10}.byteCount()), 40u);
    EXPECT_EQ((RawView{Type::Double, buffer, 8}.byteCount()), 64u);
    EXPECT_EQ((RawView{Type::Int8, buffer, 7}.byteCount()), 7u);
    EXPECT_EQ((RawView{Type::Timestamp, buffer, 4}.byteCount()), 64u);
}

TEST(TdmsTypes, RawViewKeepsTheSampleCountAtFullSixtyFourBitWidth) {
    // sampleCount is uint64_t, not size_t: on the 32-bit ARM target size_t
    // would truncate this and the on-disk count field would be wrong.
    const RawView view{Type::Int8, nullptr, 0x1'0000'0000ull};
    EXPECT_EQ(view.sampleCount, 0x1'0000'0000ull);
}

TEST(TdmsTypes, RawViewDoesNotOwnOrCopyTheSampleBuffer) {
    std::vector<float> samples = {1.f, 2.f, 3.f};
    const auto* address = reinterpret_cast<const std::uint8_t*>(samples.data());
    {
        const RawView view{Type::Float, address, samples.size()};
        EXPECT_EQ(view.data, address);
        EXPECT_EQ(view.byteCount(), samples.size() * sizeof(float));
    }
    // The buffer is still the caller's after the view is gone.
    samples[0] = 9.f;
    EXPECT_FLOAT_EQ(samples[0], 9.f);
}
