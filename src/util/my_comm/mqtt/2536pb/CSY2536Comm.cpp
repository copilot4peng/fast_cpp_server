// =============================================================================
// 文件：CSY2536Comm.cpp
// 模块：my_csy2536_protocol
// 说明：把 FastMQTT 订阅接入系统的协议桥接模块实现。
// =============================================================================

#include "CSY2536Comm.h"

#include <sstream>

#include "CSY2536CallBackFuncs.h"
#include "FastMQTT.hpp"
#include "MyLog.h"

namespace csy2536 {

CSY2536Comm& CSY2536Comm::GetInstance() {
	static CSY2536Comm instance;
	return instance;
}

CSY2536Comm::~CSY2536Comm() = default;

bool CSY2536Comm::Initialize(const nlohmann::json& config) {
	std::lock_guard<std::mutex> lock(mutex_);
    MYLOG_INFO("【CSY2536Comm】开始初始化------------------------------------------------------------");
	if (initialized_) {
		MYLOG_WARN("【CSY2536Comm】重复初始化被拒绝");
		return false;
	}

	enable_ = config.value("enable", true);
	default_qos_ = config.value("default_qos", 1);
	MYLOG_INFO("【CSY2536Comm】开始初始化，当前配置 enable={} default_qos={}", enable_, default_qos_);

	AppendDefaultTopics();
	Setting2536CallbackForTopic();

	initialized_ = true;
	MYLOG_INFO("【CSY2536Comm】初始化成功 enable={} topics_count={} default_qos={}",
			   enable_, topics_.size(), default_qos_);
    MYLOG_INFO("【CSY2536Comm】初始化结束------------------------------------------------------------");
	return true;
}

bool CSY2536Comm::AppendDefaultTopics() {
	MYLOG_INFO("【CSY2536Comm】开始追加默认主题");
	topics_.push_back("yingji/situation_ui");
	MYLOG_INFO("【CSY2536Comm】追加默认主题完成：第 1 个 topic = yingji/situation_ui");
	MYLOG_INFO("【CSY2536Comm】追加默认主题结束");
	return true;
}

bool CSY2536Comm::Start() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (!initialized_) {
		MYLOG_ERROR("【CSY2536Comm】Start 失败：尚未初始化");
		return false;
	}
	if (!enable_) {
		MYLOG_WARN("【CSY2536Comm】模块未启用，Start 直接返回");
		return false;
	}
	if (running_) {
		MYLOG_WARN("【CSY2536Comm】模块已启动");
		return true;
	}

	auto& mqtt = fast_mqtt::FastMQTT::GetInstance();
	if (!mqtt.IsReady()) {
		MYLOG_WARN("【CSY2536Comm】FastMQTT 尚未就绪，暂不注册订阅");
		return false;
	}

	for (std::size_t index = 0; index < topics_.size(); ++index) {
		const std::string& topic = topics_[index];
		MYLOG_INFO("【CSY2536Comm】启动阶段正在注册第 {}/{} 个 topic：{}", index + 1, topics_.size(), topic);
		SubscribeTopicLocked(topic, default_qos_);
	}

	running_ = true;
	MYLOG_INFO("【CSY2536Comm】启动完成，已注册 {} 个主题", topic_entries_.size());
	return true;
}

void CSY2536Comm::Stop() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (!initialized_) {
		return;
	}

	for (auto& kv : topic_entries_) {
		if (kv.second.handle != 0) {
			fast_mqtt::FastMQTT::GetInstance().UnregisterCallback(kv.second.handle);
			kv.second.handle = 0;
		}
		kv.second.callbacks.clear();
	}
	topic_entries_.clear();
	running_ = false;
	MYLOG_INFO("【CSY2536Comm】模块已停止");
}

bool CSY2536Comm::IsRunning() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return running_;
}

std::uint64_t CSY2536Comm::RegisterParsedCallback(const std::string& topic,
										  ParsedCallback callback,
										  int qos) {
	if (!callback) {
		MYLOG_WARN("【CSY2536Comm】忽略空回调 topic={}", topic);
		return 0;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	if (!initialized_) {
		MYLOG_ERROR("【CSY2536Comm】注册回调失败：尚未初始化 topic={}", topic);
		return 0;
	}

	const std::uint64_t callback_handle = next_handle_++;
	auto& entry = topic_entries_[topic];
	entry.topic = topic;
	entry.qos = qos;
	entry.callbacks.push_back(TopicEntry::CallbackItem{callback_handle, std::move(callback)});

	if (running_ && entry.handle == 0) {
		SubscribeTopicLocked(topic, qos);
	}

	MYLOG_INFO("【CSY2536Comm】注册解析回调成功 topic={} callback_count={}",
			   topic, entry.callbacks.size());
	return callback_handle;
}

void CSY2536Comm::ClearTopic(const std::string& topic) {
	std::lock_guard<std::mutex> lock(mutex_);
	UnsubscribeTopicLocked(topic);
	topic_entries_.erase(topic);
	MYLOG_INFO("【CSY2536Comm】已清空主题 topic={}", topic);
}

void CSY2536Comm::ClearAll() {
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto& kv : topic_entries_) {
		if (kv.second.handle != 0) {
			fast_mqtt::FastMQTT::GetInstance().UnregisterCallback(kv.second.handle);
			kv.second.handle = 0;
		}
	}
	topic_entries_.clear();
	running_ = false;
	MYLOG_INFO("【CSY2536Comm】已清空全部主题回调");
}

nlohmann::json CSY2536Comm::Status() const {
	std::lock_guard<std::mutex> lock(mutex_);
	nlohmann::json status;
	status["initialized"] = initialized_;
	status["running"] = running_;
	status["enable"] = enable_;
	status["default_qos"] = default_qos_;
	status["configured_topic_count"] = topics_.size();
	status["configured_topics"] = topics_;

	nlohmann::json registered_topics = nlohmann::json::array();
	for (const auto& kv : topic_entries_) {
		nlohmann::json topic_item;
		topic_item["topic"] = kv.first;
		topic_item["qos"] = kv.second.qos;
		topic_item["handle"] = kv.second.handle;
		topic_item["callback_count"] = kv.second.callbacks.size();
		topic_item["registered"] = (kv.second.handle != 0);
		registered_topics.push_back(std::move(topic_item));
	}
	status["registered_topic_count"] = registered_topics.size();
	status["registered_topics"] = std::move(registered_topics);
	return status;
}

std::string CSY2536Comm::BuildSummary(const CSY2536::MsgInfo& msg) {
	std::ostringstream oss;
	oss << " send_id=" << msg.send_id();
	oss << " seq=" << msg.seq();
	oss << " receive_id_count=" << msg.receive_id_size();
	oss << " receive_name_count=" << msg.receive_name_size();
	if (msg.has_ack()) {
		oss << " ack=" << msg.ack();
	}
	if (msg.has_session_hash()) {
		oss << " session_hash=" << msg.session_hash();
	}

	const auto* descriptor = msg.GetDescriptor();
	const auto* reflection = msg.GetReflection();
	if (descriptor != nullptr && reflection != nullptr && descriptor->oneof_decl_count() > 0) {
		const auto* oneof = descriptor->oneof_decl(0);
		const auto* field = reflection->GetOneofFieldDescriptor(msg, oneof);
		if (field != nullptr) {
			oss << " type=" << field->name();
		}
	}

	return oss.str();
}

void CSY2536Comm::SubscribeTopicLocked(const std::string& topic, int qos) {
	auto& entry = topic_entries_[topic];
	entry.topic = topic;
	entry.qos = qos;

	if (entry.handle != 0) {
		return;
	}

	MYLOG_INFO("【CSY2536Comm】准备向 FastMQTT 注册 topic={}", topic);
	auto callback = [this, topic](const fast_mqtt::Message& msg) {
		OnRawMessage(topic, msg);
	};

	entry.handle = fast_mqtt::FastMQTT::GetInstance().RegisterCallback(topic, callback, qos);
	if (entry.handle == 0) {
		MYLOG_ERROR("【CSY2536Comm】订阅失败 topic={} qos={}", topic, qos);
	} else {
		MYLOG_INFO("【CSY2536Comm】订阅成功 topic={} qos={} handle={}", topic, qos, entry.handle);
	}
}

bool CSY2536Comm::Setting2536CallbackForTopic() {
	MYLOG_INFO("【CSY2536Comm】开始为 topic 注册 2536 回调，topic_count={}", topics_.size());
	if (topics_.empty()) {
		MYLOG_WARN("【CSY2536Comm】当前没有可注册的 topic");
		return false;
	}

	for (std::size_t index = 0; index < topics_.size(); ++index) {
		const std::string& topic = topics_[index];
		MYLOG_INFO("【CSY2536Comm】初始化阶段正在注册第 {}/{} 个 topic：{}", index + 1, topics_.size(), topic);
		RegisterCallbackToFastMQTT(topic,
							   CSY2536CallBackFuncs::GetInstance().GetCallbackForTopic(topic),
							   default_qos_);
	}

	MYLOG_INFO("【CSY2536Comm】为主题注册 2536 回调完成，已处理 {} 个 topic", topics_.size());
	return true;
}

std::uint64_t CSY2536Comm::RegisterCallbackToFastMQTT(const std::string& topic,
											 fast_mqtt::MessageCallback callback,
											 int qos) {
	return fast_mqtt::FastMQTT::GetInstance().RegisterCallback(topic, callback, qos);
}

void CSY2536Comm::UnsubscribeTopicLocked(const std::string& topic) {
	auto it = topic_entries_.find(topic);
	if (it == topic_entries_.end()) {
		return;
	}
	if (it->second.handle != 0) {
		fast_mqtt::FastMQTT::GetInstance().UnregisterCallback(it->second.handle);
		it->second.handle = 0;
	}
}

void CSY2536Comm::OnRawMessage(const std::string& topic_filter, const fast_mqtt::Message& msg) {
	CSY2536::MsgInfo info;
	if (!info.ParseFromString(msg.payload)) {
		MYLOG_WARN("【CSY2536Comm】收到无法解析的消息 topic={} payload_size={}", msg.topic, msg.payload.size());
		MYLOG_WARN("【CSY2536Comm】payload={}", msg.payload.c_str());
		return;
	}

	PrintMessage(topic_filter, msg, info);

	std::vector<TopicEntry::CallbackItem> callbacks;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = topic_entries_.find(topic_filter);
		if (it != topic_entries_.end()) {
			callbacks = it->second.callbacks;
		}
	}

	for (const auto& callback : callbacks) {
		try {
			MYLOG_DEBUG("+++++++++++++++++++++++++++++++++++++++++++++++V");
			callback.callback(msg.topic, info);
			MYLOG_DEBUG("+++++++++++++++++++++++++++++++++++++++++++++++A");
		} catch (const std::exception& e) {
			MYLOG_ERROR("【CSY2536Comm】外部回调执行失败 topic={} err={}", msg.topic, e.what());
		} catch (...) {
			MYLOG_ERROR("【CSY2536Comm】外部回调执行失败 topic={} err=unknown", msg.topic);
		}
	}
}

void CSY2536Comm::PrintMessage(const std::string& topic_filter,
						   const fast_mqtt::Message& msg,
						   const CSY2536::MsgInfo& msg_info) const {
	const std::string summary = BuildSummary(msg_info);
	MYLOG_INFO("----------------------------------------------------------------");
	MYLOG_INFO("【CSY2536Comm】收到 MQTT ");
	MYLOG_INFO(" * filter ={}", topic_filter);
	MYLOG_INFO(" * topic  ={}", msg.topic);
	MYLOG_INFO(" * summary:{}", summary.c_str());
	MYLOG_INFO(" * pb2str :{}", msg_info.ShortDebugString().c_str());
	MYLOG_INFO("----------------------------------------------------------------");
}

}  // namespace csy2536
