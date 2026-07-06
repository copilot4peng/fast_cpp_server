#pragma once

// =============================================================================
// 文件：CSY2536Comm.h
// 模块：my_csy2536_protocol
// 说明：把 FastMQTT 订阅接入系统的协议桥接模块。
//
// 设计定位：
//   - FastMQTT 只负责 MQTT 传输；
//   - 本模块负责“订阅主题 + 解析 CS-Y2536 MsgInfo + 打印/分发”。
//
// 当前版本：
//   1. 只做消息解析与中文日志打印；
//   2. 预留后续把解析后的消息交给系统其它模块的扩展点；
//   3. 不负责启动 FastMQTT，要求调用方提前把 FastMQTT 拉起来。
// =============================================================================

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "CS-Y2536.pb.h"
#include "FastMQTTTypes.hpp"

namespace csy2536 {

/**
 * @brief CS-Y2536 通信桥接模块。
 *
 * 使用方式：
 *   1. 先确保 fast_mqtt::FastMQTT 已 Initialize + Start；
 *   2. 再调用 CSY2536Comm::GetInstance().Initialize(...);
 *   3. 然后 Start()，模块会自动对配置中的主题执行 RegisterCallback；
 *   4. 收到 MQTT 字节后先 ParseFromString，再打印当前消息摘要。
 *
 * 说明：
 *   该模块现在只做解析和打印，后续可通过 RegisterParsedCallback() 接入
 *   具体业务模块，而不需要改动 FastMQTT 层。
 */
class CSY2536Comm final {
public:
	using ParsedCallback = std::function<void(const std::string& topic,
												  const CSY2536::MsgInfo& msg)>;

	static CSY2536Comm& GetInstance();

	CSY2536Comm(const CSY2536Comm&) = delete;
	CSY2536Comm& operator=(const CSY2536Comm&) = delete;
	CSY2536Comm(CSY2536Comm&&) = delete;
	CSY2536Comm& operator=(CSY2536Comm&&) = delete;

	/**
	 * @brief 初始化模块配置。
	 *
	 * 支持的配置格式：
	 * {
	 *   "enable": true,
	 *   "topics": ["/status", "/heartbeat"],
	 *   "default_qos": 1
	 * }
	 *
	 * 若未提供 topics，则默认订阅 "/status"。
	 */
	bool Initialize(const nlohmann::json& config = nlohmann::json::object());

	/**
	 * @brief 初始化订阅的主题。
	 *
	 * @return true 初始化成功；false 配置无效或模块未启用。
	 */
	bool AppendDefaultTopics();

	/**
	 * @brief 启动模块并注册订阅回调。
	 * @return true 启动成功；false FastMQTT 未就绪或重复启动。
	 */
	bool Start();

	/**
	 * @brief 停止模块并注销已注册回调。
	 */
	void Stop();

	/**
	 * @brief 是否已启动。
	 */
	bool IsRunning() const;

	/**
	 * @brief 注册一个解析后的消息回调。
	 *
	 * 该回调在本模块完成 MsgInfo 解析之后调用，便于后续接入系统中的各个模块。
	 * 目前版本也会保留一份中文打印日志，所以即使没有外部回调也能正常工作。
	 */
	std::uint64_t RegisterParsedCallback(const std::string& topic,
										 ParsedCallback callback,
										 int qos = 1);

	/**
	 * @brief 注册一个回调到 FastMQTT。
	 *
	 * @param topic 订阅的主题。
	 * @param callback 回调函数。
	 * @param qos QoS 等级。
	 * @return std::uint64_t 回调句柄。
	 */
	std::uint64_t RegisterCallbackToFastMQTT(const std::string& topic,
											 fast_mqtt::MessageCallback callback,
											 int qos = 1);

	/**
	 * @brief 为指定主题设置 2536 回调。
	 *
	 * @return true 设置成功。
	 * @return false 设置失败。
	 */
	bool Setting2536CallbackForTopic();

	/**
	 * @brief 清空某个 Topic 的全部回调（包括内部订阅）。
	 */
	void ClearTopic(const std::string& topic);

	/**
	 * @brief 清空所有回调并注销所有订阅。
	 */
	void ClearAll();

	/**
	 * @brief 获取当前状态摘要。
	 *
	 * 返回值会包含已注册 topic 列表，便于上层直接查看当前桥接层实际接入了哪些主题。
	 */
	nlohmann::json Status() const;

	/**
	 * @brief 兼容旧接口：返回与 Status() 相同的状态摘要。
	 */
	nlohmann::json GetStatus() const { return Status(); }

	/**
	 * @brief 将 MsgInfo 解析为可读摘要，便于日志打印和测试。
	 */
	static std::string BuildSummary(const CSY2536::MsgInfo& msg);

private:
	CSY2536Comm() = default;
	~CSY2536Comm();

	struct TopicEntry {
		std::uint64_t handle{0};
		std::string topic;
		int qos{1};
		struct CallbackItem {
			std::uint64_t handle{0};
			ParsedCallback callback;
		};
		std::vector<CallbackItem> callbacks;
	};

	void SubscribeTopicLocked(const std::string& topic, int qos);
	void UnsubscribeTopicLocked(const std::string& topic);
	void OnRawMessage(const std::string& topic_filter, const fast_mqtt::Message& msg);
	void PrintMessage(const std::string& topic_filter,
					  const fast_mqtt::Message& msg,
					  const CSY2536::MsgInfo& msg_info) const;

private:
	mutable std::mutex mutex_;
	bool initialized_{false};
	bool running_{false};
	bool enable_{true};
	int default_qos_{1};
	std::vector<std::string> topics_{"/2536/default"};
	std::map<std::string, TopicEntry> topic_entries_;
	std::uint64_t next_handle_{1};
};

}  // namespace csy2536
