#include "gtest/gtest.h"

#include <ctime>
#include <string>

#include "MyDateTime.h"
#include "MyTimer.h"

TEST(DateTimeToolsTest, TimestampBasicValuesAreValid) {
    const std::time_t now = DateTimeTools::Now();
    const int64_t now_ms = DateTimeTools::NowMs();
    const int64_t now_us = DateTimeTools::NowUs();
    const int64_t now_ns = DateTimeTools::NowNs();

    EXPECT_GT(now, static_cast<std::time_t>(0));
    EXPECT_GE(now_ms / 1000LL, static_cast<int64_t>(now) - 1);
    EXPECT_GE(now_us / 1000000LL, static_cast<int64_t>(now) - 1);
    EXPECT_GE(now_ns / 1000000000LL, static_cast<int64_t>(now) - 1);
    EXPECT_FALSE(DateTimeTools::GetTimestampMs().empty());
}

TEST(DateTimeToolsTest, ParseAndFormatSecondRoundTrip) {
    const std::string text = "2025-06-11 10:26:52";
    const std::time_t timestamp = DateTimeTools::Parse(text);

    ASSERT_NE(timestamp, static_cast<std::time_t>(-1));
    EXPECT_EQ(DateTimeTools::Format(timestamp), text);
}

TEST(DateTimeToolsTest, ParseAndFormatFractionRoundTrip) {
    const int64_t ms = DateTimeTools::ParseMs("2025-06-11 10:26:52.123");
    const int64_t us = DateTimeTools::ParseUs("2025-06-11 10:26:52.123456");
    const int64_t ns = DateTimeTools::ParseNs("2025-06-11 10:26:52.123456789");

    ASSERT_NE(ms, -1);
    ASSERT_NE(us, -1);
    ASSERT_NE(ns, -1);
    EXPECT_EQ(DateTimeTools::FormatMs(ms), "2025-06-11 10:26:52.123");
    EXPECT_EQ(DateTimeTools::FormatUs(us), "2025-06-11 10:26:52.123456");
    EXPECT_EQ(DateTimeTools::FormatNs(ns), "2025-06-11 10:26:52.123456789");
}

TEST(DateTimeToolsTest, DateCalculations) {
    const std::time_t timestamp = DateTimeTools::Parse("2024-02-29 12:00:00");
    ASSERT_NE(timestamp, static_cast<std::time_t>(-1));

    EXPECT_TRUE(DateTimeTools::IsLeapYear(2024));
    EXPECT_FALSE(DateTimeTools::IsLeapYear(2025));
    EXPECT_EQ(DateTimeTools::DaysInMonth(2024, 2), 29);
    EXPECT_EQ(DateTimeTools::DaysInMonth(2025, 2), 28);
    EXPECT_EQ(DateTimeTools::AddSeconds(timestamp, 10), timestamp + 10);
    EXPECT_EQ(DateTimeTools::DiffSeconds(timestamp, timestamp + 3600), 3600);
    EXPECT_EQ(DateTimeTools::DiffHours(timestamp, timestamp + 7200), 2);
}

TEST(DateTimeToolsTest, TimerAndStopwatchWork) {
    DateTimeTools::Timer timer;
    DateTimeTools::SleepMs(1);
    EXPECT_GE(timer.ElapsedMicroseconds(), 1);

    DateTimeTools::Stopwatch stopwatch;
    stopwatch.Start();
    DateTimeTools::SleepMs(1);
    stopwatch.Pause();
    const int64_t paused_ms = stopwatch.ElapsedMilliseconds();
    DateTimeTools::SleepMs(1);
    EXPECT_EQ(stopwatch.ElapsedMilliseconds(), paused_ms);
    stopwatch.Resume();
    DateTimeTools::SleepMs(1);
    stopwatch.Stop();
    EXPECT_GE(stopwatch.ElapsedMilliseconds(), paused_ms);
}

TEST(DateTimeToolsTest, DateTimeStructAndTimezoneAreReadable) {
    const DateTimeTools::DateTime value = DateTimeTools::NowDateTime();

    EXPECT_GE(value.year, 2020);
    EXPECT_GE(value.month, 1);
    EXPECT_LE(value.month, 12);
    EXPECT_GE(value.day, 1);
    EXPECT_LE(value.day, 31);
    EXPECT_GE(value.millisecond, 0);
    EXPECT_LT(value.millisecond, 1000);
    EXPECT_GE(value.microsecond, 0);
    EXPECT_LT(value.microsecond, 1000000);
    EXPECT_FALSE(DateTimeTools::GetTimezoneName().empty());
}
