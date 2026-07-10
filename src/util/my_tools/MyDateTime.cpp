#include "MyDateTime.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <thread>

#include "MyLog.h"

namespace DateTimeTools {
namespace {

const int64_t kMillisPerSecond = 1000LL;
const int64_t kMicrosPerSecond = 1000000LL;
const int64_t kNanosPerSecond = 1000000000LL;
const int64_t kSecondsPerMinute = 60LL;
const int64_t kSecondsPerHour = 60LL * kSecondsPerMinute;
const int64_t kSecondsPerDay = 24LL * kSecondsPerHour;

bool LocalTime(std::time_t seconds, std::tm* out)
{
	if (out == NULL) {
		return false;
	}
#if defined(_WIN32)
	return localtime_s(out, &seconds) == 0;
#else
	return localtime_r(&seconds, out) != NULL;
#endif
}

bool GmTime(std::time_t seconds, std::tm* out)
{
	if (out == NULL) {
		return false;
	}
#if defined(_WIN32)
	return gmtime_s(out, &seconds) == 0;
#else
	return gmtime_r(&seconds, out) != NULL;
#endif
}

std::string FormatTm(const std::tm& tm_value, const std::string& format)
{
	std::ostringstream oss;
	oss << std::put_time(&tm_value, format.c_str());
	return oss.str();
}

std::string FormatLocal(std::time_t seconds, const std::string& format)
{
	std::tm tm_value;
	if (!LocalTime(seconds, &tm_value)) {
		MYLOG_ERROR("[DateTimeTools] 本地时间格式化失败, seconds={}", static_cast<int64_t>(seconds));
		return std::string();
	}
	return FormatTm(tm_value, format);
}

void SplitTimestamp(int64_t timestamp, int64_t unit_per_second, std::time_t* seconds, int64_t* fraction)
{
	int64_t sec = timestamp / unit_per_second;
	int64_t frac = timestamp % unit_per_second;
	if (frac < 0) {
		frac += unit_per_second;
		--sec;
	}
	*seconds = static_cast<std::time_t>(sec);
	*fraction = frac;
}

std::string FormatFraction(int64_t value, int width)
{
	std::ostringstream oss;
	oss << std::setw(width) << std::setfill('0') << value;
	return oss.str();
}

std::string FormatWithFraction(int64_t timestamp, int64_t unit_per_second, int width, const std::string& format)
{
	std::time_t seconds = 0;
	int64_t fraction = 0;
	SplitTimestamp(timestamp, unit_per_second, &seconds, &fraction);
	const std::string base = FormatLocal(seconds, format);
	if (base.empty()) {
		return std::string();
	}
	return base + "." + FormatFraction(fraction, width);
}

bool ParseFraction(const std::string& text, size_t dot_pos, int target_width, int64_t* fraction)
{
	*fraction = 0;
	if (dot_pos == std::string::npos) {
		return true;
	}

	std::string digits = text.substr(dot_pos + 1);
	if (digits.empty()) {
		return false;
	}
	if (digits.size() > static_cast<size_t>(target_width)) {
		digits.resize(static_cast<size_t>(target_width));
	}
	for (size_t i = 0; i < digits.size(); ++i) {
		if (digits[i] < '0' || digits[i] > '9') {
			return false;
		}
	}
	while (digits.size() < static_cast<size_t>(target_width)) {
		digits.push_back('0');
	}
	*fraction = std::strtoll(digits.c_str(), NULL, 10);
	return true;
}

bool ParseDateTimeToSeconds(const std::string& datetime, std::time_t* seconds)
{
	const size_t dot_pos = datetime.find('.');
	const std::string main_part = datetime.substr(0, dot_pos);

	int year = 0;
	int month = 0;
	int day = 0;
	int hour = 0;
	int minute = 0;
	int second = 0;
	char tail = '\0';
	const int count = std::sscanf(main_part.c_str(), "%d-%d-%d %d:%d:%d%c",
								  &year, &month, &day, &hour, &minute, &second, &tail);
	if (count != 6) {
		return false;
	}
	if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
		minute < 0 || minute > 59 || second < 0 || second > 60) {
		return false;
	}

	std::tm tm_value = {};
	tm_value.tm_year = year - 1900;
	tm_value.tm_mon = month - 1;
	tm_value.tm_mday = day;
	tm_value.tm_hour = hour;
	tm_value.tm_min = minute;
	tm_value.tm_sec = second;
	tm_value.tm_isdst = -1;

	const std::time_t parsed = std::mktime(&tm_value);
	if (parsed == static_cast<std::time_t>(-1)) {
		return false;
	}

	std::tm check_tm;
	if (!LocalTime(parsed, &check_tm)) {
		return false;
	}
	if (check_tm.tm_year != year - 1900 || check_tm.tm_mon != month - 1 ||
		check_tm.tm_mday != day || check_tm.tm_hour != hour || check_tm.tm_min != minute ||
		check_tm.tm_sec != second) {
		return false;
	}

	*seconds = parsed;
	return true;
}

int64_t ParseWithFraction(const std::string& datetime, int64_t unit_per_second, int fraction_width)
{
	std::time_t seconds = 0;
	if (!ParseDateTimeToSeconds(datetime, &seconds)) {
		MYLOG_WARN("[DateTimeTools] 时间字符串解析失败, 输入='{}', 期望格式=yyyy-MM-dd HH:mm:ss[.小数]", datetime);
		return -1;
	}

	int64_t fraction = 0;
	const size_t dot_pos = datetime.find('.');
	if (!ParseFraction(datetime, dot_pos, fraction_width, &fraction)) {
		MYLOG_WARN("[DateTimeTools] 时间字符串小数部分解析失败, 输入='{}'", datetime);
		return -1;
	}

	return static_cast<int64_t>(seconds) * unit_per_second + fraction;
}

std::tm CurrentLocalTm()
{
	std::tm tm_value;
	if (!LocalTime(Now(), &tm_value)) {
		MYLOG_ERROR("[DateTimeTools] 获取当前本地时间结构失败");
		std::tm empty_tm = {};
		empty_tm.tm_sec = 0;
		empty_tm.tm_min = 0;
		empty_tm.tm_hour = 0;
		empty_tm.tm_mday = 1;
		empty_tm.tm_mon = 0;
		empty_tm.tm_year = 70;
		empty_tm.tm_wday = 4;
		empty_tm.tm_yday = 0;
		empty_tm.tm_isdst = 0;
		return empty_tm;
	}
	return tm_value;
}

}  // namespace

std::time_t Now()
{
	return std::time(NULL);
}

int64_t NowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t NowUs()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t NowNs()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string GetTimestamp()
{
	return std::to_string(static_cast<int64_t>(Now()));
}

std::string GetTimestampMs()
{
	return std::to_string(NowMs());
}

std::string GetTimestampUs()
{
	return std::to_string(NowUs());
}

std::string GetTimestampNs()
{
	return std::to_string(NowNs());
}

std::string ToString()
{
	return Format(Now());
}

std::string ToStringMs()
{
	return FormatMs(NowMs());
}

std::string ToStringUs()
{
	return FormatUs(NowUs());
}

std::string ToString(const std::string& format)
{
	return Format(Now(), format);
}

std::string Format(std::time_t seconds, const std::string& format)
{
	return FormatLocal(seconds, format);
}

std::string FormatMs(int64_t milliseconds, const std::string& format)
{
	return FormatWithFraction(milliseconds, kMillisPerSecond, 3, format);
}

std::string FormatUs(int64_t microseconds, const std::string& format)
{
	return FormatWithFraction(microseconds, kMicrosPerSecond, 6, format);
}

std::string FormatNs(int64_t nanoseconds, const std::string& format)
{
	return FormatWithFraction(nanoseconds, kNanosPerSecond, 9, format);
}

std::time_t Parse(const std::string& datetime)
{
	std::time_t seconds = 0;
	if (!ParseDateTimeToSeconds(datetime, &seconds)) {
		MYLOG_WARN("[DateTimeTools] 秒级时间字符串解析失败, 输入='{}', 期望格式=yyyy-MM-dd HH:mm:ss", datetime);
		return static_cast<std::time_t>(-1);
	}
	return seconds;
}

int64_t ParseMs(const std::string& datetime)
{
	return ParseWithFraction(datetime, kMillisPerSecond, 3);
}

int64_t ParseUs(const std::string& datetime)
{
	return ParseWithFraction(datetime, kMicrosPerSecond, 6);
}

int64_t ParseNs(const std::string& datetime)
{
	return ParseWithFraction(datetime, kNanosPerSecond, 9);
}

std::string GetUtcTime()
{
	std::tm tm_value;
	const std::time_t now = Now();
	if (!GmTime(now, &tm_value)) {
		MYLOG_ERROR("[DateTimeTools] UTC 时间格式化失败, seconds={}", static_cast<int64_t>(now));
		return std::string();
	}
	return FormatTm(tm_value, "%Y-%m-%d %H:%M:%S");
}

std::string GetLocalTime()
{
	return ToString();
}

std::string Today()
{
	return ToString("%Y-%m-%d");
}

int CurrentYear()
{
	return CurrentLocalTm().tm_year + 1900;
}

int CurrentMonth()
{
	return CurrentLocalTm().tm_mon + 1;
}

int CurrentDay()
{
	return CurrentLocalTm().tm_mday;
}

int CurrentHour()
{
	return CurrentLocalTm().tm_hour;
}

int CurrentMinute()
{
	return CurrentLocalTm().tm_min;
}

int CurrentSecond()
{
	return CurrentLocalTm().tm_sec;
}

int DayOfWeek()
{
	return DayOfWeek(Now());
}

int DayOfWeek(std::time_t seconds)
{
	std::tm tm_value;
	if (!LocalTime(seconds, &tm_value)) {
		MYLOG_ERROR("[DateTimeTools] 星期计算失败, seconds={}", static_cast<int64_t>(seconds));
		return 0;
	}
	return tm_value.tm_wday == 0 ? 7 : tm_value.tm_wday;
}

int DayOfYear()
{
	return DayOfYear(Now());
}

int DayOfYear(std::time_t seconds)
{
	std::tm tm_value;
	if (!LocalTime(seconds, &tm_value)) {
		MYLOG_ERROR("[DateTimeTools] 年内天数计算失败, seconds={}", static_cast<int64_t>(seconds));
		return 0;
	}
	return tm_value.tm_yday + 1;
}

int DaysInMonth()
{
	return DaysInMonth(CurrentYear(), CurrentMonth());
}

int DaysInMonth(int year, int month)
{
	if (month < 1 || month > 12) {
		MYLOG_WARN("[DateTimeTools] 月份天数计算失败, 非法月份: year={}, month={}", year, month);
		return 0;
	}

	static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (month == 2 && IsLeapYear(year)) {
		return 29;
	}
	return days[month - 1];
}

bool IsLeapYear()
{
	return IsLeapYear(CurrentYear());
}

bool IsLeapYear(int year)
{
	return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

DateTime NowDateTime()
{
	const int64_t now_us = NowUs();
	std::time_t seconds = 0;
	int64_t fraction = 0;
	SplitTimestamp(now_us, kMicrosPerSecond, &seconds, &fraction);

	std::tm tm_value;
	if (!LocalTime(seconds, &tm_value)) {
		MYLOG_ERROR("[DateTimeTools] DateTime 结构生成失败, seconds={}", static_cast<int64_t>(seconds));
		DateTime empty = {1970, 1, 1, 0, 0, 0, 0, 0};
		return empty;
	}

	DateTime value;
	value.year = tm_value.tm_year + 1900;
	value.month = tm_value.tm_mon + 1;
	value.day = tm_value.tm_mday;
	value.hour = tm_value.tm_hour;
	value.minute = tm_value.tm_min;
	value.second = tm_value.tm_sec;
	value.millisecond = static_cast<int>(fraction / 1000LL);
	value.microsecond = static_cast<int>(fraction);
	return value;
}

std::time_t AddDays(int days)
{
	return AddDays(Now(), days);
}

std::time_t AddDays(std::time_t base_time, int days)
{
	return static_cast<std::time_t>(static_cast<int64_t>(base_time) + static_cast<int64_t>(days) * kSecondsPerDay);
}

std::time_t AddHours(int hours)
{
	return AddHours(Now(), hours);
}

std::time_t AddHours(std::time_t base_time, int hours)
{
	return static_cast<std::time_t>(static_cast<int64_t>(base_time) + static_cast<int64_t>(hours) * kSecondsPerHour);
}

std::time_t AddMinutes(int minutes)
{
	return AddMinutes(Now(), minutes);
}

std::time_t AddMinutes(std::time_t base_time, int minutes)
{
	return static_cast<std::time_t>(static_cast<int64_t>(base_time) + static_cast<int64_t>(minutes) * kSecondsPerMinute);
}

std::time_t AddSeconds(int seconds)
{
	return AddSeconds(Now(), seconds);
}

std::time_t AddSeconds(std::time_t base_time, int seconds)
{
	return static_cast<std::time_t>(static_cast<int64_t>(base_time) + static_cast<int64_t>(seconds));
}

int64_t DiffSeconds(std::time_t begin, std::time_t end)
{
	return static_cast<int64_t>(end) - static_cast<int64_t>(begin);
}

int64_t DiffMilliseconds(int64_t begin_ms, int64_t end_ms)
{
	return end_ms - begin_ms;
}

int64_t DiffMinutes(std::time_t begin, std::time_t end)
{
	return DiffSeconds(begin, end) / kSecondsPerMinute;
}

int64_t DiffHours(std::time_t begin, std::time_t end)
{
	return DiffSeconds(begin, end) / kSecondsPerHour;
}

int64_t DiffDays(std::time_t begin, std::time_t end)
{
	return DiffSeconds(begin, end) / kSecondsPerDay;
}

bool IsToday(std::time_t seconds)
{
	return IsSameDay(seconds, Now());
}

bool IsSameDay(std::time_t left, std::time_t right)
{
	std::tm left_tm;
	std::tm right_tm;
	if (!LocalTime(left, &left_tm) || !LocalTime(right, &right_tm)) {
		MYLOG_ERROR("[DateTimeTools] 同日判断失败, left={}, right={}", static_cast<int64_t>(left), static_cast<int64_t>(right));
		return false;
	}
	return left_tm.tm_year == right_tm.tm_year && left_tm.tm_yday == right_tm.tm_yday;
}

bool IsExpired(std::time_t expire_time)
{
	return Now() >= expire_time;
}

bool IsExpiredMs(int64_t start_ms, int64_t timeout_ms)
{
	return DiffMilliseconds(start_ms, NowMs()) >= timeout_ms;
}

void SleepMs(uint64_t milliseconds)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void SleepUs(uint64_t microseconds)
{
	std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
}

void SleepSeconds(uint64_t seconds)
{
	std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

int GetTimezoneOffset()
{
	const std::time_t now = Now();
	std::tm local_tm;
	std::tm utc_tm;
	if (!LocalTime(now, &local_tm) || !GmTime(now, &utc_tm)) {
		MYLOG_ERROR("[DateTimeTools] 获取时区偏移失败, seconds={}", static_cast<int64_t>(now));
		return 0;
	}

	const std::time_t local_as_epoch = std::mktime(&local_tm);
	const std::time_t utc_as_local_epoch = std::mktime(&utc_tm);
	return static_cast<int>(std::difftime(local_as_epoch, utc_as_local_epoch));
}

std::string GetTimezoneName()
{
	std::tm tm_value = CurrentLocalTm();
	char buffer[64] = {0};
	if (std::strftime(buffer, sizeof(buffer), "%Z", &tm_value) == 0) {
		MYLOG_WARN("[DateTimeTools] 获取时区名称失败，返回空字符串");
		return std::string();
	}
	return std::string(buffer);
}

uint64_t SteadyNowMs()
{
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint64_t SteadyNowUs()
{
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint64_t SteadyNowNs()
{
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

}  // namespace DateTimeTools

