/**
 * TDMS timestamps.
 *
 * A TDMS timestamp is a 64-bit unsigned count of 2^-64 fractions of a second
 * followed by 64-bit signed seconds since 1904-01-01T00:00:00 UTC. The epoch
 * offset used to be derived with mktime(), which interprets its input as LOCAL
 * time, so the recorded UTC timestamp shifted with the machine's timezone -
 * measured as 2082844800 under UTC but 2082853817 under Europe/Moscow.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>

#include "data_type.h"

using TDMS::DataType;
using TDMS::TDMSType;

namespace {

// 66 years (1904..1969) = 66 * 365 days + 17 leap days = 24107 days.
constexpr std::int64_t kSecondsFrom1904To1970 = 24107LL * 86400LL;
static_assert(kSecondsFrom1904To1970 == 2082844800LL);

class ScopedTimezone {
   public:
    explicit ScopedTimezone(const char* tz) {
        const char* previous = std::getenv("TZ");
        m_hadTz = previous != nullptr;
        if (m_hadTz) {
            m_previous = previous;
        }
        setenv("TZ", tz, 1);
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

TEST(TimeStamp, EpochOffsetMatchesTheSpecification) {
    std::unique_ptr<std::uint64_t[]> value(DataType::GetRawTimeValue(0));
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value[0], 0u) << "sub-second fraction is always zero";
    EXPECT_EQ(value[1], static_cast<std::uint64_t>(kSecondsFrom1904To1970));
}

TEST(TimeStamp, EpochOffsetMatchesTheLeapYearComputation) {
    const auto isLeapYear = [](int year) {
        if (year % 400 == 0) return true;
        if (year % 100 == 0) return false;
        return year % 4 == 0;
    };
    std::int64_t seconds = 0;
    for (int year = 1904; year < 1970; year++) {
        seconds += 24 * 60 * 60 * (isLeapYear(year) ? 366 : 365);
    }
    EXPECT_EQ(seconds, kSecondsFrom1904To1970);
}

TEST(TimeStamp, IsIndependentOfTheLocalTimezone) {
    std::unique_ptr<std::uint64_t[]> reference;
    {
        ScopedTimezone tz("UTC");
        reference.reset(DataType::GetRawTimeValue(1700000000));
    }
    ASSERT_NE(reference, nullptr);

    for (const char* zone : {"Europe/Moscow", "Pacific/Kiritimati", "America/Los_Angeles"}) {
        SCOPED_TRACE(zone);
        ScopedTimezone tz(zone);
        std::unique_ptr<std::uint64_t[]> value(DataType::GetRawTimeValue(1700000000));
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(value[1], reference[1]);
    }
}

TEST(TimeStamp, AdvancesOneForOneWithTimeT) {
    std::unique_ptr<std::uint64_t[]> a(DataType::GetRawTimeValue(1000000000));
    std::unique_ptr<std::uint64_t[]> b(DataType::GetRawTimeValue(1000000060));
    EXPECT_EQ(b[1] - a[1], 60u);
}

TEST(TimeStamp, ToStringRendersTheEncodedCalendarDate) {
    auto* value = DataType::GetRawTimeValue(0);
    DataType stamp;
    stamp.InitDataType(TDMSType::TimeStamp, value);
    const std::string text = stamp.ToString();
    EXPECT_NE(text.find("year: 1970"), std::string::npos) << text;
    EXPECT_NE(text.find("month: 01"), std::string::npos) << text;
}
