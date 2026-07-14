#pragma once

#include <ctime>
#include <cstdint>
#include <string>

namespace DateTimeTools {

/**
 * @brief 业务侧常用的拆分日期时间结构。
 *
 * 说明：
 * 1. year/month/day/hour/minute/second 使用本地时区。
 * 2. millisecond 与 microsecond 来自同一次系统时间采样，便于日志、GUI、测试展示。
 */
struct DateTime
{
	int year;
	int month;
	int day;

	int hour;
	int minute;
	int second;

	int millisecond;
	int microsecond;
};

// =========================
// 当前系统时间
// =========================

/** @brief 获取当前 Unix 时间戳，单位：秒。 */
std::time_t Now();

/** @brief 获取当前 Unix 时间戳，单位：毫秒。 */
int64_t NowMs();

/** @brief 获取当前 Unix 时间戳，单位：微秒。 */
int64_t NowUs();

/** @brief 获取当前 Unix 时间戳，单位：纳秒。 */
int64_t NowNs();

/** @brief 获取秒级时间戳字符串。 */
std::string GetTimestamp();

/** @brief 获取毫秒级时间戳字符串。 */
std::string GetTimestampMs();

/** @brief 获取微秒级时间戳字符串。 */
std::string GetTimestampUs();

/** @brief 获取纳秒级时间戳字符串。 */
std::string GetTimestampNs();

// =========================
// 格式化输出
// =========================

/** @brief 当前本地时间格式化为 yyyy-MM-dd HH:mm:ss。 */
std::string ToString();

/** @brief 当前本地时间格式化为 yyyy-MM-dd HH:mm:ss.SSS。 */
std::string ToStringMs();

/** @brief 当前本地时间格式化为 yyyy-MM-dd HH:mm:ss.ffffff。 */
std::string ToStringUs();

/** @brief 当前本地时间按 strftime 格式输出，例如 "%Y/%m/%d %H:%M:%S"。 */
std::string ToString(const std::string& format);

/** @brief 指定秒级时间戳格式化，默认 yyyy-MM-dd HH:mm:ss。 */
std::string Format(std::time_t seconds, const std::string& format = "%Y-%m-%d %H:%M:%S");

/** @brief 指定毫秒级时间戳格式化，默认 yyyy-MM-dd HH:mm:ss.SSS。 */
std::string FormatMs(int64_t milliseconds, const std::string& format = "%Y-%m-%d %H:%M:%S");

/** @brief 指定微秒级时间戳格式化，默认 yyyy-MM-dd HH:mm:ss.ffffff。 */
std::string FormatUs(int64_t microseconds, const std::string& format = "%Y-%m-%d %H:%M:%S");

/** @brief 指定纳秒级时间戳格式化，默认 yyyy-MM-dd HH:mm:ss.nnnnnnnnn。 */
std::string FormatNs(int64_t nanoseconds, const std::string& format = "%Y-%m-%d %H:%M:%S");

// =========================
// 字符串解析
// =========================

/** @brief 解析 yyyy-MM-dd HH:mm:ss，返回秒级 Unix 时间戳；失败返回 -1 并输出中文日志。 */
std::time_t Parse(const std::string& datetime);

/** @brief 解析 yyyy-MM-dd HH:mm:ss[.SSS]，返回毫秒级 Unix 时间戳；失败返回 -1。 */
int64_t ParseMs(const std::string& datetime);

/** @brief 解析 yyyy-MM-dd HH:mm:ss[.ffffff]，返回微秒级 Unix 时间戳；失败返回 -1。 */
int64_t ParseUs(const std::string& datetime);

/** @brief 解析 yyyy-MM-dd HH:mm:ss[.nnnnnnnnn]，返回纳秒级 Unix 时间戳；失败返回 -1。 */
int64_t ParseNs(const std::string& datetime);

// =========================
// UTC 与本地时间
// =========================

/** @brief 获取当前 UTC 时间，格式 yyyy-MM-dd HH:mm:ss。 */
std::string GetUtcTime();

/** @brief 获取当前本地时间，格式 yyyy-MM-dd HH:mm:ss。 */
std::string GetLocalTime();

// =========================
// 日期字段
// =========================

/** @brief 当前日期，格式 yyyy-MM-dd。 */
std::string Today();

/** @brief 当前年份，例如 2026。 */
int CurrentYear();

/** @brief 当前月份，范围 1~12。 */
int CurrentMonth();

/** @brief 当前日期，范围 1~31。 */
int CurrentDay();

/** @brief 当前小时，范围 0~23。 */
int CurrentHour();

/** @brief 当前分钟，范围 0~59。 */
int CurrentMinute();

/** @brief 当前秒，范围 0~60，闰秒场景可能出现 60。 */
int CurrentSecond();

/** @brief 获取当前星期，范围 1~7，1 表示周一，7 表示周日。 */
int DayOfWeek();

/** @brief 获取指定时间的星期，范围 1~7，1 表示周一，7 表示周日。 */
int DayOfWeek(std::time_t seconds);

/** @brief 获取当前是一年中的第几天，范围 1~366。 */
int DayOfYear();

/** @brief 获取指定时间是一年中的第几天，范围 1~366。 */
int DayOfYear(std::time_t seconds);

/** @brief 获取当前月份天数。 */
int DaysInMonth();

/** @brief 获取指定年月的月份天数，month 范围 1~12；非法参数返回 0。 */
int DaysInMonth(int year, int month);

/** @brief 判断当前年份是否为闰年。 */
bool IsLeapYear();

/** @brief 判断指定年份是否为闰年。 */
bool IsLeapYear(int year);

/** @brief 获取当前本地日期时间结构体。 */
DateTime NowDateTime();

// =========================
// 时间计算
// =========================

/** @brief 在当前时间基础上增加天数。 */
std::time_t AddDays(int days);

/** @brief 在指定时间基础上增加天数。 */
std::time_t AddDays(std::time_t base_time, int days);

/** @brief 在当前时间基础上增加小时。 */
std::time_t AddHours(int hours);

/** @brief 在指定时间基础上增加小时。 */
std::time_t AddHours(std::time_t base_time, int hours);

/** @brief 在当前时间基础上增加分钟。 */
std::time_t AddMinutes(int minutes);

/** @brief 在指定时间基础上增加分钟。 */
std::time_t AddMinutes(std::time_t base_time, int minutes);

/** @brief 在当前时间基础上增加秒。 */
std::time_t AddSeconds(int seconds);

/** @brief 在指定时间基础上增加秒。 */
std::time_t AddSeconds(std::time_t base_time, int seconds);

/** @brief 计算两个秒级时间戳差值，返回 end - begin，单位：秒。 */
int64_t DiffSeconds(std::time_t begin, std::time_t end);

/** @brief 计算两个毫秒级时间戳差值，返回 end_ms - begin_ms，单位：毫秒。 */
int64_t DiffMilliseconds(int64_t begin_ms, int64_t end_ms);

/** @brief 计算两个秒级时间戳差值，返回 end - begin，单位：分钟。 */
int64_t DiffMinutes(std::time_t begin, std::time_t end);

/** @brief 计算两个秒级时间戳差值，返回 end - begin，单位：小时。 */
int64_t DiffHours(std::time_t begin, std::time_t end);

/** @brief 计算两个秒级时间戳差值，返回 end - begin，单位：天。 */
int64_t DiffDays(std::time_t begin, std::time_t end);

// =========================
// 日期判断
// =========================

/** @brief 判断指定秒级时间戳是否为今天。 */
bool IsToday(std::time_t seconds);

/** @brief 判断两个秒级时间戳是否属于本地时区的同一天。 */
bool IsSameDay(std::time_t left, std::time_t right);

/** @brief 判断秒级截止时间是否已经过期。 */
bool IsExpired(std::time_t expire_time);

/** @brief 判断从 start_ms 开始经过 timeout_ms 后是否已经超时。 */
bool IsExpiredMs(int64_t start_ms, int64_t timeout_ms);

// =========================
// 睡眠
// =========================

/** @brief 当前线程睡眠指定毫秒。 */
void SleepMs(uint64_t milliseconds);

/** @brief 当前线程睡眠指定微秒。 */
void SleepUs(uint64_t microseconds);

/** @brief 当前线程睡眠指定秒。 */
void SleepSeconds(uint64_t seconds);

// =========================
// 时区
// =========================

/** @brief 获取当前本地时区相对 UTC 的偏移秒数，例如中国标准时间为 28800。 */
int GetTimezoneOffset();

/** @brief 获取当前时区名称，例如 CST、UTC。 */
std::string GetTimezoneName();

// =========================
// 单调时钟，适合性能统计和超时判断
// =========================

/** @brief 获取单调时钟毫秒值，不受系统时间回拨影响。 */
uint64_t SteadyNowMs();

/** @brief 获取单调时钟微秒值，不受系统时间回拨影响。 */
uint64_t SteadyNowUs();

/** @brief 获取单调时钟纳秒值，不受系统时间回拨影响。 */
uint64_t SteadyNowNs();

}  // namespace DateTimeTools

