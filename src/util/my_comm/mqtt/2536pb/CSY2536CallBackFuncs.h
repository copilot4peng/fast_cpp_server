#pragma once

// =============================================================================
// 文件：CSY2536CallBackFuncs.h
// 模块：my_csy2536_protocol
// 说明：CS-Y2536 消息回调集合（单例工厂）。
//
// 设计目标：
//   - 对外提供按 topic 获取 MessageCallback 的能力；
//   - 内部封装 protobuf 解析与中文日志打印；
//   - 后续可在这里扩展不同 topic 的专属处理逻辑。
// =============================================================================

#include <map>
#include <mutex>
#include <string>

#include "CS-Y2536.pb.h"
#include "FastMQTTTypes.hpp"

namespace csy2536 {

/**
 * @brief CS-Y2536 回调函数集合单例。
 *
 * 这个类负责把不同 topic 对应的 fast_mqtt::MessageCallback 提取出来。
 * 当前版本所有回调都先做 MsgInfo 解析，再打印摘要日志；后续可以按 topic
 * 拓展更细的业务分发逻辑，而不需要改动 FastMQTT。
 */
class CSY2536CallBackFuncs final {
public:
	static CSY2536CallBackFuncs& GetInstance();

	CSY2536CallBackFuncs(const CSY2536CallBackFuncs&) = delete;
	CSY2536CallBackFuncs& operator=(const CSY2536CallBackFuncs&) = delete;
	CSY2536CallBackFuncs(CSY2536CallBackFuncs&&) = delete;
	CSY2536CallBackFuncs& operator=(CSY2536CallBackFuncs&&) = delete;

	/**
	 * @brief 根据 topic 获取一个 MessageCallback。
	 *
	 * 若 topic 有专属处理逻辑，则返回专属回调；否则返回通用回调。
	 */
	fast_mqtt::MessageCallback GetCallbackForTopic(const std::string& topic);

private:
	CSY2536CallBackFuncs() = default;

	fast_mqtt::MessageCallback BuildCallback(const std::string& topic,
											 const std::string& title);
	static std::string BuildSummary(const CSY2536::MsgInfo& msg);
	static void LogParsedMessage(const std::string& title,
								 const fast_mqtt::Message& msg,
								 const CSY2536::MsgInfo& parsed_msg);

private:
	std::mutex mutex_;
	std::map<std::string, fast_mqtt::MessageCallback> cache_;
};

}  // namespace csy2536
