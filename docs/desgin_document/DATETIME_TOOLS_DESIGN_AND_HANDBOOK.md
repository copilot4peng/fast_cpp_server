# DateTimeTools 时间工具库设计文档与使用手册

## 1. 模块定位

DateTimeTools 用于统一 fast_cpp_server 项目中的时间处理能力，避免业务代码分散使用 time、chrono、localtime、strftime、sleep 等底层接口。

模块特点：

1. 全部使用 C++11 标准库实现。
2. 不依赖 Boost，不新增第三方库依赖。
3. 兼容 Kylin、Ubuntu、ARM64 等 Linux 环境。
4. 真实时间使用 system_clock/time_t，性能计时使用 steady_clock。
5. 解析失败、计时器关键状态、ScopedTimer 自动耗时输出均使用中文日志。

源码位置：

1. src/util/my_tools/MyDateTime.h
2. src/util/my_tools/MyDateTime.cpp
3. src/util/my_tools/MyTimer.h
4. src/util/my_tools/MyTimer.cpp

## 2. 命名空间

所有接口都位于 DateTimeTools 命名空间。

```cpp
#include "MyDateTime.h"
#include "MyTimer.h"

auto now_ms = DateTimeTools::NowMs();
```

## 3. 当前时间接口

| 接口 | 返回值 | 说明 |
| --- | --- | --- |
| Now() | std::time_t | 当前 Unix 时间，单位秒 |
| NowMs() | int64_t | 当前 Unix 时间，单位毫秒 |
| NowUs() | int64_t | 当前 Unix 时间，单位微秒 |
| NowNs() | int64_t | 当前 Unix 时间，单位纳秒 |
| GetTimestamp() | std::string | 秒级时间戳字符串 |
| GetTimestampMs() | std::string | 毫秒级时间戳字符串 |
| GetTimestampUs() | std::string | 微秒级时间戳字符串 |
| GetTimestampNs() | std::string | 纳秒级时间戳字符串 |

示例：

```cpp
std::time_t seconds = DateTimeTools::Now();
int64_t milliseconds = DateTimeTools::NowMs();
std::string timestamp = DateTimeTools::GetTimestampMs();
```

## 4. 格式化接口

| 接口 | 说明 |
| --- | --- |
| ToString() | 当前时间格式化为 yyyy-MM-dd HH:mm:ss |
| ToStringMs() | 当前时间格式化为 yyyy-MM-dd HH:mm:ss.SSS |
| ToStringUs() | 当前时间格式化为 yyyy-MM-dd HH:mm:ss.ffffff |
| ToString(format) | 当前时间按 strftime 格式输出 |
| Format(time_t) | 秒级时间戳转字符串 |
| FormatMs(int64_t) | 毫秒级时间戳转字符串 |
| FormatUs(int64_t) | 微秒级时间戳转字符串 |
| FormatNs(int64_t) | 纳秒级时间戳转字符串 |

示例：

```cpp
std::string text1 = DateTimeTools::ToString();
std::string text2 = DateTimeTools::ToStringMs();
std::string text3 = DateTimeTools::ToString("%Y/%m/%d %H:%M:%S");

std::string from_sec = DateTimeTools::Format(1749657612);
std::string from_ms = DateTimeTools::FormatMs(1749657612123LL);
```

说明：C++ 在 Linux/ARM64 上 std::time_t 和 int64_t 经常是同一种底层类型，无法稳定提供 Format(time_t) 与 Format(int64_t) 两个仅参数类型不同的重载。因此本模块使用 Format 表示秒级格式化，使用 FormatMs/FormatUs/FormatNs 表示更高精度格式化，避免二义性和跨平台编译问题。

## 5. 解析接口

| 接口 | 输入格式 | 返回值 |
| --- | --- | --- |
| Parse() | yyyy-MM-dd HH:mm:ss | 秒级时间戳 |
| ParseMs() | yyyy-MM-dd HH:mm:ss[.SSS] | 毫秒级时间戳 |
| ParseUs() | yyyy-MM-dd HH:mm:ss[.ffffff] | 微秒级时间戳 |
| ParseNs() | yyyy-MM-dd HH:mm:ss[.nnnnnnnnn] | 纳秒级时间戳 |

解析失败时返回 -1，并输出中文 WARN 日志。

示例：

```cpp
std::time_t t = DateTimeTools::Parse("2025-06-11 10:26:52");
int64_t ms = DateTimeTools::ParseMs("2025-06-11 10:26:52.123");
int64_t us = DateTimeTools::ParseUs("2025-06-11 10:26:52.123456");
int64_t ns = DateTimeTools::ParseNs("2025-06-11 10:26:52.123456789");
```

## 6. UTC、本地时间和日期字段

常用接口：

1. GetUtcTime()
2. GetLocalTime()
3. Today()
4. CurrentYear()
5. CurrentMonth()
6. CurrentDay()
7. CurrentHour()
8. CurrentMinute()
9. CurrentSecond()

示例：

```cpp
std::string utc = DateTimeTools::GetUtcTime();
std::string local = DateTimeTools::GetLocalTime();
std::string today = DateTimeTools::Today();
int year = DateTimeTools::CurrentYear();
```

## 7. 星期、年内天数、月份天数、闰年

接口：

1. DayOfWeek()：返回 1~7，1 表示周一，7 表示周日。
2. DayOfYear()：返回 1~366。
3. DaysInMonth()：当前月份天数。
4. DaysInMonth(year, month)：指定月份天数。
5. IsLeapYear()：当前年份是否闰年。
6. IsLeapYear(year)：指定年份是否闰年。

示例：

```cpp
bool leap = DateTimeTools::IsLeapYear(2024);
int days = DateTimeTools::DaysInMonth(2024, 2); // 29
int week = DateTimeTools::DayOfWeek();
```

## 8. 时间计算和判断

接口：

1. AddDays(days) / AddDays(base_time, days)
2. AddHours(hours) / AddHours(base_time, hours)
3. AddMinutes(minutes) / AddMinutes(base_time, minutes)
4. AddSeconds(seconds) / AddSeconds(base_time, seconds)
5. DiffSeconds(begin, end)
6. DiffMilliseconds(begin_ms, end_ms)
7. DiffMinutes(begin, end)
8. DiffHours(begin, end)
9. DiffDays(begin, end)
10. IsToday(seconds)
11. IsSameDay(left, right)
12. IsExpired(expire_time)
13. IsExpiredMs(start_ms, timeout_ms)

示例：

```cpp
std::time_t deadline = DateTimeTools::AddSeconds(30);
if (DateTimeTools::IsExpired(deadline)) {
    // 已超时
}

int64_t start = DateTimeTools::NowMs();
// ...
int64_t cost = DateTimeTools::DiffMilliseconds(start, DateTimeTools::NowMs());
```

## 9. 睡眠接口

接口：

1. SleepMs(milliseconds)
2. SleepUs(microseconds)
3. SleepSeconds(seconds)

示例：

```cpp
DateTimeTools::SleepMs(10);
DateTimeTools::SleepUs(500);
DateTimeTools::SleepSeconds(1);
```

## 10. 时区接口

接口：

1. GetTimezoneOffset()：返回本地时区相对 UTC 的偏移秒数，中国标准时间通常为 28800。
2. GetTimezoneName()：返回当前时区名称，例如 CST、UTC。

示例：

```cpp
int offset = DateTimeTools::GetTimezoneOffset();
std::string name = DateTimeTools::GetTimezoneName();
```

## 11. 单调时钟接口

接口：

1. SteadyNowMs()
2. SteadyNowUs()
3. SteadyNowNs()

说明：单调时钟只适合计算耗时和超时，不适合格式化成人类日期。它不受系统时间回拨影响。

示例：

```cpp
uint64_t begin = DateTimeTools::SteadyNowMs();
// ...
uint64_t cost = DateTimeTools::SteadyNowMs() - begin;
```

## 12. Timer

Timer 构造后自动开始计时，适合最常见的函数耗时统计。

```cpp
DateTimeTools::Timer timer;

// 执行业务逻辑

MYLOG_INFO("耗时: {} ms", timer.ElapsedMilliseconds());
```

接口：

1. Start()
2. Restart()
3. Reset()
4. ElapsedSeconds()
5. ElapsedMilliseconds()
6. ElapsedMicroseconds()
7. ElapsedNanoseconds()

## 13. ScopedTimer

ScopedTimer 构造时开始计时，析构时自动输出中文日志。

```cpp
{
    DateTimeTools::ScopedTimer timer("YOLO Detect");
    Detect();
}
```

日志示例：

```text
[DateTimeTools::ScopedTimer] YOLO Detect cost 32 ms
```

## 14. Stopwatch

Stopwatch 适合 GUI、测试、人工操作流程统计，支持暂停和继续。

```cpp
DateTimeTools::Stopwatch sw;
sw.Start();

// 第一段操作
sw.Pause();

// 暂停期间不累计
sw.Resume();

// 第二段操作
sw.Stop();

std::cout << sw.ElapsedMilliseconds() << std::endl;
```

接口：

1. Start()
2. Pause()
3. Resume()
4. Stop()
5. Reset()
6. ElapsedSeconds()
7. ElapsedMilliseconds()
8. ElapsedMicroseconds()
9. ElapsedNanoseconds()
10. IsRunning()
11. IsPaused()

## 15. DateTime 结构体

DateTime 结构体适合业务代码直接读取年月日时分秒，避免重复拆 tm。

```cpp
DateTimeTools::DateTime dt = DateTimeTools::NowDateTime();

std::cout << dt.year << "-" << dt.month << "-" << dt.day << std::endl;
std::cout << dt.hour << ":" << dt.minute << ":" << dt.second << std::endl;
std::cout << dt.millisecond << " ms" << std::endl;
std::cout << dt.microsecond << " us" << std::endl;
```

结构体字段：

```cpp
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
```

## 16. 推荐实践

1. 业务日志时间展示使用 ToStringMs()。
2. 数据库存储建议使用 NowMs()，便于排序和跨语言处理。
3. 性能耗时统计使用 Timer、ScopedTimer 或 SteadyNowMs()。
4. 超时判断优先使用 steady_clock 相关接口或 Timer，避免系统时间回拨影响。
5. 对外协议若要求 Unix 时间戳，需要明确单位是秒、毫秒、微秒还是纳秒。
