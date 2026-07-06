// =============================================================================
// 文件：JsonComm.cpp
// 模块：my_comm / mqtt / json
// 说明：JSON 协议桥接模块 JsonComm 的实现。
//
// 参考对象：src/util/my_comm/mqtt/2536pb/CSY2536Comm.cpp
//   - 结构保持一致：Init -> AppendDefaultTopics -> Start -> 订阅 -> OnRawMessage 分发；
//   - 区别在于：载荷解析从 protobuf 换成 nlohmann::json，去掉 protobuf 依赖。
// =============================================================================

#include "JsonComm.h"

#include <sstream>

#include "FastMQTT.hpp"
#include "JsonCallBackFuncs.h"
#include "MyLog.h"

namespace json_comm {

namespace {
/**
 * @brief 从配置里解析 topics 列表；若缺省则给一个默认主题。
 */
std::vector<std::string> ParseTopics(const nlohmann::json& config) {
    std::vector<std::string> topics;
    if (config.empty()) {
        MYLOG_WARN("【JsonComm】配置为空，使用默认主题 /status");
        topics.push_back("/json/default");
        return topics;
    }
    if (config.contains("topics") && config["topics"].is_array()) {
        for (const auto& item : config["topics"]) {
            if (item.is_string()) {
                topics.push_back(item.get<std::string>());
            }
        }
    }
    if (topics.empty()) {
        topics.push_back("/json/default");
    }
    return topics;
}
}  // namespace

JsonComm& JsonComm::GetInstance() {
    static JsonComm instance;
    return instance;
}

JsonComm::~JsonComm() {
    // 析构时不主动操作 FastMQTT，交由使用方通过 Stop() 显式收尾，避免退出顺序问题。
}

bool JsonComm::Init(const nlohmann::json& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        MYLOG_WARN("【JsonComm】重复初始化被拒绝");
        return false;
    }

    // 读取基础配置，缺省字段全部走默认值，保证健壮性。
    enable_         = config.value("enable", true);
    default_qos_    = config.value("default_qos", 1);
    topics_         = ParseTopics(config);

    // 追加内置示例主题（可按需删除）。
    AppendDefaultTopics();

    initialized_ = true;
    MYLOG_INFO("【JsonComm】初始化成功 enable={} topics_count={} default_qos={}",
               enable_, topics_.size(), default_qos_);
    return true;
}

bool JsonComm::AppendDefaultTopics() {
    // 示例：追加两个常用主题。实际项目可通过配置文件完全覆盖。
    topics_.push_back(default_topic);
    MYLOG_INFO("【JsonComm】追加默认主题完成：第 1 个 topic = {} 到 topics 列表", default_topic);
    return true;
}

bool JsonComm::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        MYLOG_ERROR("【JsonComm】Start 失败：尚未初始化");
        return false;
    }
    if (!enable_) {
        MYLOG_WARN("【JsonComm】模块未启用，Start 直接返回");
        return false;
    }
    if (running_) {
        MYLOG_WARN("【JsonComm】模块已启动");
        return true;
    }

    // 要求 FastMQTT 已经就绪（已连接 Broker），否则无法订阅。
    auto& mqtt = fast_mqtt::FastMQTT::GetInstance();
    if (!mqtt.IsReady()) {
        MYLOG_WARN("【JsonComm】FastMQTT 尚未就绪，暂不注册订阅");
        return false;
    }

    // 依次对每个主题注册“本模块的分发回调”。
    for (const auto& topic : topics_) {
        SubscribeTopicLocked(topic, default_qos_);
    }

    running_ = true;
    MYLOG_INFO("【JsonComm】启动完成，已注册 {} 个主题", topic_entries_.size());
    return true;
}

void JsonComm::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return;
    }

    // 注销所有 FastMQTT 订阅，并清空业务回调。
    for (auto& kv : topic_entries_) {
        if (kv.second.handle != 0) {
            fast_mqtt::FastMQTT::GetInstance().UnregisterCallback(kv.second.handle);
            kv.second.handle = 0;
        }
        kv.second.callbacks.clear();
    }
    topic_entries_.clear();
    running_ = false;
    MYLOG_INFO("【JsonComm】模块已停止");
}

bool JsonComm::IsRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

std::uint64_t JsonComm::RegisterCallbackToFastMQTT(const std::string& topic,
                                                   fast_mqtt::MessageCallback callback,
                                                   int qos) {
    if (!callback) {
        MYLOG_WARN("【JsonComm】忽略空回调 topic={}", topic);
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        MYLOG_ERROR("【JsonComm】注册回调失败：尚未初始化 topic={}", topic);
        return 0;
    }

    const std::uint64_t callback_handle = next_handle_++;
    auto& entry = topic_entries_[topic];
    entry.topic = topic;
    entry.qos = qos;

    // 注册到底层 FastMQTT。
    auto& mqtt = fast_mqtt::FastMQTT::GetInstance();
    entry.handle = mqtt.RegisterCallback(topic, callback, qos);
    if (entry.handle == 0) {
        MYLOG_ERROR("【JsonComm】注册到底层 FastMQTT 失败 topic={} qos={}", topic, qos);
        return 0;
    }

    MYLOG_INFO("【JsonComm】注册到底层 FastMQTT 成功 topic={} handle={}", topic, entry.handle);
    return callback_handle;
}

std::uint64_t JsonComm::RegisterParsedCallback(const std::string& topic,
                                               ParsedCallback callback,
                                               int qos) {
    if (!callback) {
        MYLOG_WARN("【JsonComm】忽略空回调 topic={}", topic);
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        MYLOG_ERROR("【JsonComm】注册回调失败：尚未初始化 topic={}", topic);
        return 0;
    }

    const std::uint64_t callback_handle = next_handle_++;
    auto& entry = topic_entries_[topic];
    entry.topic = topic;
    entry.qos = qos;
    entry.callbacks.push_back(TopicEntry::CallbackItem{callback_handle, std::move(callback)});

    // 若模块已经启动，但该主题还没有底层订阅，则立即补订阅。
    if (running_ && entry.handle == 0) {
        SubscribeTopicLocked(topic, qos);
    }

    MYLOG_INFO("【JsonComm】注册解析回调成功 topic={} callback_count={}",
               topic, entry.callbacks.size());
    return callback_handle;
}

void JsonComm::ClearTopic(const std::string& topic) {
    std::lock_guard<std::mutex> lock(mutex_);
    UnsubscribeTopicLocked(topic);
    topic_entries_.erase(topic);
    MYLOG_INFO("【JsonComm】已清空主题 topic={}", topic);
}

void JsonComm::ClearAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& kv : topic_entries_) {
        if (kv.second.handle != 0) {
            fast_mqtt::FastMQTT::GetInstance().UnregisterCallback(kv.second.handle);
            kv.second.handle = 0;
        }
    }
    topic_entries_.clear();
    running_ = false;
    MYLOG_INFO("【JsonComm】已清空全部主题回调");
}

nlohmann::json JsonComm::Status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json status;
    status["initialized"] = initialized_;
    status["running"] = running_;
    status["enable"] = enable_;
    status["default_qos"] = default_qos_;
    status["topic_count"] = topic_entries_.size();
    status["topics"] = topics_;
    return status;
}

void JsonComm::SubscribeTopicLocked(const std::string& topic, int qos) {
    auto& entry = topic_entries_[topic];
    entry.topic = topic;
    entry.qos = qos;

    // 已经订阅过则不重复注册。
    if (entry.handle != 0) {
        return;
    }

    // 注册“本模块的分发回调”到 FastMQTT：收到原始消息后进入 OnRawMessage 统一处理。
    const std::uint64_t handle = fast_mqtt::FastMQTT::GetInstance().RegisterCallback(
        topic,
        [this, topic](const fast_mqtt::Message& msg) { OnRawMessage(topic, msg); },
        qos);

    if (handle == 0) {
        MYLOG_ERROR("【JsonComm】订阅失败 topic={} qos={}", topic, qos);
        return;
    }

    entry.handle = handle;
    MYLOG_INFO("【JsonComm】订阅成功 topic={} qos={} handle={}", topic, qos, entry.handle);
}

void JsonComm::UnsubscribeTopicLocked(const std::string& topic) {
    auto it = topic_entries_.find(topic);
    if (it == topic_entries_.end()) {
        return;
    }
    if (it->second.handle != 0) {
        fast_mqtt::FastMQTT::GetInstance().UnregisterCallback(it->second.handle);
        it->second.handle = 0;
    }
}

void JsonComm::OnRawMessage(const std::string& topic_filter, const fast_mqtt::Message& msg) {
    // 1. 把原始载荷解析为 JSON；解析失败不抛异常，仅告警后返回。
    nlohmann::json msg_json = nlohmann::json::parse(msg.payload, nullptr, false);
    if (msg_json.is_discarded()) {
        MYLOG_WARN("【JsonComm】收到无法解析的消息 topic={} payload_size={}", msg.topic, msg.payload.size());
        MYLOG_WARN("【JsonComm】payload={}", msg.payload.c_str());
        return;
    }

    // 2. 打印统一格式的中文摘要日志。
    PrintMessage(topic_filter, msg, msg_json);

    // 3. 拷贝命中回调后再执行，避免持锁调用业务回调造成阻塞或死锁。
    std::vector<TopicEntry::CallbackItem> callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = topic_entries_.find(topic_filter);
        if (it != topic_entries_.end()) {
            callbacks = it->second.callbacks;
        }
    }

    // 4. 逐个执行业务回调；单个回调异常不影响其它回调。
    for (const auto& item : callbacks) {
        try {
            item.callback(msg.topic, msg_json);
        } catch (const std::exception& e) {
            MYLOG_ERROR("【JsonComm】外部回调执行失败 topic={} err={}", msg.topic, e.what());
        } catch (...) {
            MYLOG_ERROR("【JsonComm】外部回调执行失败 topic={} err=unknown", msg.topic);
        }
    }
}

void JsonComm::PrintMessage(const std::string& topic_filter,
                            const fast_mqtt::Message& msg,
                            const nlohmann::json& msg_json) const {
    const std::string summary = JsonCallBackFuncs::BuildSummary(msg_json);
    MYLOG_INFO("----------------------------------------------------------------");
    MYLOG_INFO("【JsonComm】收到 MQTT (JSON)");
    MYLOG_INFO(" * filter ={}", topic_filter);
    MYLOG_INFO(" * topic  ={}", msg.topic);
    MYLOG_INFO(" * summary:{}", summary.c_str());
    MYLOG_INFO(" * json   :{}", msg_json.dump().c_str());
    MYLOG_INFO("----------------------------------------------------------------");
}

}  // namespace json_comm
