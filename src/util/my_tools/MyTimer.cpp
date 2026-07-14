#include "MyTimer.h"

#include "MyLog.h"

namespace DateTimeTools {

Timer::Timer()
	: start_time_(std::chrono::steady_clock::now()),
	  running_(true)
{
}

void Timer::Start(bool showLog)
{
	start_time_ = std::chrono::steady_clock::now();
	running_ = true;
	if (showLog) {
		MYLOG_DEBUG("[DateTimeTools::Timer] 计时器已开始");
	}
}

void Timer::Restart(bool showLog)
{
	Start(showLog);
	if (showLog) {
		MYLOG_DEBUG("[DateTimeTools::Timer] 计时器已重新开始");
	}
}

void Timer::Reset()
{
	start_time_ = std::chrono::steady_clock::now();
	running_ = false;
	MYLOG_DEBUG("[DateTimeTools::Timer] 计时器已重置并停止");
}

double Timer::ElapsedSeconds() const
{
	return static_cast<double>(ElapsedNanoseconds()) / 1000000000.0;
}

int64_t Timer::ElapsedMilliseconds() const
{
	return ElapsedNanoseconds() / 1000000LL;
}

int64_t Timer::ElapsedMicroseconds() const
{
	return ElapsedNanoseconds() / 1000LL;
}

int64_t Timer::ElapsedNanoseconds() const
{
	if (!running_) {
		return 0;
	}
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now() - start_time_).count();
}

ScopedTimer::ScopedTimer(const std::string& name)
	: name_(name),
	  timer_()
{
	MYLOG_DEBUG("[DateTimeTools::ScopedTimer] 作用域计时开始, 名称={}", name_);
}

ScopedTimer::~ScopedTimer()
{
	try {
		MYLOG_INFO("[DateTimeTools::ScopedTimer] {} cost {} ms", name_, timer_.ElapsedMilliseconds());
	} catch (...) {
	}
}

Stopwatch::Stopwatch()
	: start_time_(std::chrono::steady_clock::now()),
	  accumulated_ns_(0),
	  running_(false),
	  paused_(false)
{
}

void Stopwatch::Start()
{
	accumulated_ns_ = 0;
	start_time_ = std::chrono::steady_clock::now();
	running_ = true;
	paused_ = false;
	MYLOG_DEBUG("[DateTimeTools::Stopwatch] 秒表已开始");
}

void Stopwatch::Pause()
{
	if (!running_) {
		MYLOG_DEBUG("[DateTimeTools::Stopwatch] 秒表未运行，忽略暂停请求");
		return;
	}
	accumulated_ns_ = CurrentElapsedNanoseconds();
	running_ = false;
	paused_ = true;
	MYLOG_DEBUG("[DateTimeTools::Stopwatch] 秒表已暂停, 已累计={} ms", accumulated_ns_ / 1000000LL);
}

void Stopwatch::Resume()
{
	if (!paused_) {
		MYLOG_DEBUG("[DateTimeTools::Stopwatch] 秒表未暂停，忽略恢复请求");
		return;
	}
	start_time_ = std::chrono::steady_clock::now();
	running_ = true;
	paused_ = false;
	MYLOG_DEBUG("[DateTimeTools::Stopwatch] 秒表已继续");
}

void Stopwatch::Stop()
{
	if (running_) {
		accumulated_ns_ = CurrentElapsedNanoseconds();
	}
	running_ = false;
	paused_ = false;
	MYLOG_DEBUG("[DateTimeTools::Stopwatch] 秒表已停止, 总耗时={} ms", accumulated_ns_ / 1000000LL);
}

void Stopwatch::Reset()
{
	accumulated_ns_ = 0;
	start_time_ = std::chrono::steady_clock::now();
	running_ = false;
	paused_ = false;
	MYLOG_DEBUG("[DateTimeTools::Stopwatch] 秒表已重置");
}

double Stopwatch::ElapsedSeconds() const
{
	return static_cast<double>(ElapsedNanoseconds()) / 1000000000.0;
}

int64_t Stopwatch::ElapsedMilliseconds() const
{
	return ElapsedNanoseconds() / 1000000LL;
}

int64_t Stopwatch::ElapsedMicroseconds() const
{
	return ElapsedNanoseconds() / 1000LL;
}

int64_t Stopwatch::ElapsedNanoseconds() const
{
	return CurrentElapsedNanoseconds();
}

bool Stopwatch::IsRunning() const
{
	return running_;
}

bool Stopwatch::IsPaused() const
{
	return paused_;
}

int64_t Stopwatch::CurrentElapsedNanoseconds() const
{
	if (!running_) {
		return accumulated_ns_;
	}
	const int64_t current_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now() - start_time_).count();
	return accumulated_ns_ + current_ns;
}

}  // namespace DateTimeTools

