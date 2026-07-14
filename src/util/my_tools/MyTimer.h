#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace DateTimeTools {

/**
 * @brief 轻量级耗时计时器。
 *
 * Timer 使用 steady_clock，不受系统时间修改、NTP 校时、时区变化影响。
 * 构造后自动开始计时，适合函数耗时、模块耗时、接口处理耗时统计。
 */
class Timer
{
public:
	Timer();

	/** @brief 开始计时；如果已经在计时，会从当前时刻重新开始。 */
	void Start(bool showLog = true);

	/** @brief 重新计时，等价于 Start，语义上用于新一轮测量。 */
	void Restart(bool showLog = true);

	/** @brief 重置计时器，清零并停止。 */
	void Reset();

	/** @brief 获取已经过秒数。 */
	double ElapsedSeconds() const;

	/** @brief 获取已经过毫秒数。 */
	int64_t ElapsedMilliseconds() const;

	/** @brief 获取已经过微秒数。 */
	int64_t ElapsedMicroseconds() const;

	/** @brief 获取已经过纳秒数。 */
	int64_t ElapsedNanoseconds() const;

private:
	std::chrono::steady_clock::time_point start_time_;
	bool running_;
};

/**
 * @brief 作用域自动计时器。
 *
 * 构造时开始计时，析构时自动输出中文日志，特别适合大型项目中统计代码块耗时：
 * ScopedTimer timer("YOLO Detect");
 */
class ScopedTimer
{
public:
	explicit ScopedTimer(const std::string& name);
	~ScopedTimer();

	ScopedTimer(const ScopedTimer&) = delete;
	ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
	std::string name_;
	Timer timer_;
};

/**
 * @brief 可暂停、恢复、停止的秒表。
 *
 * Stopwatch 适合 GUI、测试、人工操作流程统计；Start 会清零并开始，Pause/Resume
 * 会保留已经累计的时间，Stop 会固定最终耗时。
 */
class Stopwatch
{
public:
	Stopwatch();

	/** @brief 清零并开始计时。 */
	void Start();

	/** @brief 暂停计时，保留已累计耗时。 */
	void Pause();

	/** @brief 从暂停状态继续计时。 */
	void Resume();

	/** @brief 停止计时，保留最终耗时。 */
	void Stop();

	/** @brief 清零并恢复到未启动状态。 */
	void Reset();

	/** @brief 获取累计秒数。 */
	double ElapsedSeconds() const;

	/** @brief 获取累计毫秒数。 */
	int64_t ElapsedMilliseconds() const;

	/** @brief 获取累计微秒数。 */
	int64_t ElapsedMicroseconds() const;

	/** @brief 获取累计纳秒数。 */
	int64_t ElapsedNanoseconds() const;

	/** @brief 当前是否正在计时。 */
	bool IsRunning() const;

	/** @brief 当前是否处于暂停状态。 */
	bool IsPaused() const;

private:
	int64_t CurrentElapsedNanoseconds() const;

	std::chrono::steady_clock::time_point start_time_;
	int64_t accumulated_ns_;
	bool running_;
	bool paused_;
};

}  // namespace DateTimeTools

