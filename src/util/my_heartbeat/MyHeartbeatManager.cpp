#include "MyHeartbeatManager.h"
#include <unistd.h>
#include <iostream>
#include <ctime>
#include <mutex>
#include <chrono>
#include <random>

#include "MyLog.h"
#include "MyEdges.h"
#include "MyEdgeManager.h"
#include "MosqPublisher.hpp"


using namespace edge_manager;


HeartbeatManager& HeartbeatManager::Instance() {
    static HeartbeatManager inst;
    return inst;
}

std::string HeartbeatManager::formatTopic(const std::string& fmt, const std::string& source) {
    std::string t = fmt;
    const std::string key = "{source}";
    size_t pos = 0;
    while ((pos = t.find(key, pos)) != std::string::npos) {
        t.replace(pos, key.length(), source);
        pos += source.length();
    }
    return t;
}

void HeartbeatManager::Init(const nlohmann::json& config) {
    config_ = config;
    MYLOG_INFO("初始化 HeartbeatManager 配置: {}", config_.dump(4));
    interval_sec_ = 5; // 如果不是对象，使用硬编码默认值
    try {
        interval_sec_ = config.value("interval_sec", 5);
        MYLOG_INFO("HeartbeatManager interval_sec set to {}", interval_sec_);
        simple_json4log = config.value("simple_json4log", false);
        MYLOG_INFO("HeartbeatManager config loaded: interval_sec = {}, simple_json4log = {}", 
                   interval_sec_, simple_json4log);
    } catch (const std::exception& e) {
        std::cout << "[HeartbeatManager] Failed to get interval_sec from global config: " << e.what() << std::endl;
    }

    source_id_ = config_.value("source_id", std::string());
    if (source_id_.empty()) {
        // 尝试用主机名作为默认 source_id
        char hn[128] = {0};
        if (gethostname(hn, sizeof(hn)) == 0) {
            source_id_ = hn;
        } else {
            source_id_ = "unknown";
        }
    }

    // 如果配置里有 targets 数组，可在这里解析并构建每个 Target（可选）
    // 简单实现：如果 targets_ 为空，则用默认 publisher/target（只添加一次）
    if (targets_.empty()) {
        // 使用配置里可能的 mqtt host/port/client_id
        std::string host = config_.value("mqtt_host", std::string("127.0.0.1"));
        int port = config_.value("mqtt_port", 1883);
        std::string client_id = config_.value("mqtt_client_id", std::string("hb_") + source_id_);

        // 创建默认 publisher（如果你想支持多个 target/broker，可以根据 config 构造多个）
        try {
            pub_ = std::make_shared<my_mqtt::MosqPublisher>(host, port, client_id);
            // 可选：设置 will
            std::string will_topic = config_.value("will_topic", std::string());
            if (will_topic.empty()) {
                will_topic = "system/heartbeats/" + source_id_;
            }
            std::string will_payload = config_.value("will_payload", std::string(R"({"status":"offline"})"));
            pub_->SetLastWill(will_topic, will_payload, 1, true);
        } catch (const std::exception& e) {
            MYLOG_ERROR("创建 MosqPublisher 失败: {}", e.what());
            pub_ = nullptr;
        }

        // 添加默认 target（只添加一次）
        Target t;
        t.publisher = pub_;
        t.topic_fmt = config_.value("topic_fmt", std::string("system/heartbeats/{source}"));
        t.qos = config_.value("qos", 1);
        t.retain = config_.value("retain", false);
        t.interval_seconds = config_.value("target_interval", 30);
        AddTarget(t);
    }

    start_time_ = time(nullptr);
    MYLOG_INFO("HeartbeatManager initialized with interval: {} seconds, source_id={}", interval_sec_, source_id_);
}

void HeartbeatManager::Start() {
    if (running_) {
        MYLOG_WARN("HeartbeatManager 已经在运行，忽略重复启动请求");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &t : targets_) {
            if (!t.publisher) {
                MYLOG_WARN("Heartbeat target has no publisher, topic_fmt={}", t.topic_fmt);
                continue;
            }
            // 尝试 Connect，但不阻塞太久（由 publisher 的实现控制超时）
            bool ok = false;
            try {
                ok = t.publisher->Connect();
            } catch (const std::exception& e) {
                MYLOG_ERROR("publisher Connect exception: {}", e.what());
                ok = false;
            }
            if (!ok) {
                MYLOG_WARN("Heartbeat publisher Connect failed for topic_fmt={}, will retry in background", t.topic_fmt);
            } else {
                MYLOG_INFO("Heartbeat publisher connected for topic_fmt={}", t.topic_fmt);
            }
        }
    }
    {
        MYLOG_INFO("Starting HeartbeatManager...");
        running_ = true;
        worker_ = std::thread(&HeartbeatManager::WorkerLoop, this);
    }   
}

void HeartbeatManager::Stop() {
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
    // Disconnect publishers
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &t : targets_) {
        if (t.publisher) {
            try { t.publisher->Disconnect(); } catch (...) {}
        }
    }
}

void HeartbeatManager::AddTarget(const Target& t) {
    std::lock_guard<std::mutex> lock(mutex_);
    // 防止重复添加相同 topic_fmt 的 target（简单去重策略）
    for (auto &ex : targets_) {
        if (ex.topic_fmt == t.topic_fmt) {
            MYLOG_WARN("Target with same topic_fmt already exists, skipping: {}", t.topic_fmt);
            return;
        }
    }
    targets_.push_back(t);
}

void HeartbeatManager::WorkerLoop() {
    // initial jitter
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> jitter_ms(0, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(jitter_ms(gen)));

    // while (running_) {
    //     try {
    //         BuildHeartbeat();
    //         this->SendHeartbeat();
    //         this->SendOnce();
    //     } catch (const std::exception& e) {
    //         MYLOG_ERROR("Heartbeat error: {}", e.what());
    //     }
    //     sleep(interval_sec_);
    // }
    while (running_.load()) {
        try {
            BuildHeartbeat();
            SendHeartbeat();
            SendOnce();
        } catch (const std::exception& e) {
            MYLOG_ERROR("Heartbeat error: {}", e.what());
        }
        // use std::this_thread::sleep_for instead of sleep
        for (int i = 0; i < interval_sec_ && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void HeartbeatManager::BuildHeartbeat() {
    // 加锁
    // std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json base = config_.value("base", nlohmann::json::object());

    base["pid"] = getpid();
    base["timestamp"] = time(nullptr);
    base["uptime_sec"] = time(nullptr) - start_time_;
    
    heartbeat_data_ = nlohmann::json::object();

    heartbeat_data_["base"] = base;
    heartbeat_data_["extra"] = config_.value("extra", nlohmann::json::object());

    nlohmann::json edgeData = ::edge_manager::MyEdgeManager::GetInstance().ShowEdgesStatus();
    nlohmann::json edgesData = my_edge::MyEdges::GetInstance().GetHeartbeatInfo();
    // heartbeat_data_["edge_devices"] = edgeData;
    // 添加边缘设备信息
    if (true) {
        heartbeat_data_["edge_summary"] = my_edge::MyEdges::GetInstance().GetHeartbeatInfo();
    }
    if (true) {
        heartbeat_data_["edge_managed_devices"] = ::edge_manager::MyEdgeManager::GetInstance().ShowEdgesStatus();
    }

}

nlohmann::json HeartbeatManager::GetHeartbeatSnapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return heartbeat_data_;
}

void HeartbeatManager::SendOnce() {
    // build minimal payload (additional fields can be merged from heartbeat_data_ if needed)
    nlohmann::json payload;
    auto now = std::chrono::system_clock::now();
    auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    payload["version"] = "1.0";
    payload["source"] = source_id_;
    payload["type"] = "heartbeat";
    payload["timestamp"] = ts;
    payload["seq"] = ++seq_;
    payload["status"] = "online";

    std::string s = payload.dump();

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &t : targets_) {
        if (!t.publisher) {
            MYLOG_WARN("Skipping publish: no publisher for target {}", t.topic_fmt);
            continue;
        }
        // format topic
        std::string topic = formatTopic(t.topic_fmt, source_id_);
        bool ok = false;
        // 如果 publisher 未连接，可以尝试一次连接（由 publisher 实现决定是否阻塞）
        try {
            // try publish directly, if it returns false we attempt a connect then publish once more
            ok = t.publisher->Publish(topic, s, t.qos, t.retain);
            if (!ok) {
                MYLOG_WARN("Publish returned false, attempting Connect() then retry for topic {}", topic);
                if (t.publisher->Connect()) {
                    ok = t.publisher->Publish(topic, s, t.qos, t.retain);
                } else {
                    MYLOG_WARN("Reconnect attempt failed for publisher topic_fmt={}", t.topic_fmt);
                }
            }
        } catch (const std::exception& e) {
            MYLOG_ERROR("Heartbeat Publish exception for topic {}: {}", topic, e.what());
            ok = false;
        }

        if (!ok) {
            MYLOG_WARN("Heartbeat publish failed target topic={}", topic);
        } else {
            MYLOG_INFO("Heartbeat published to target topic={}", topic);
        }
    }
}

void HeartbeatManager::SendHeartbeat() {
    std::string sender = config_.value("sender", "log");
    if (sender == "log") {
        if (simple_json4log) {
            MYLOG_INFO("Heartbeat: {}", this->heartbeat_data_["heartbeat"].dump());
            MYLOG_INFO("Heartbeat: {}", this->heartbeat_data_.dump(4));
        } else {
            MYLOG_INFO("Heartbeat Data: {}", this->heartbeat_data_.dump(4));
        }
    }
    // http / mqtt 可在此扩展
}
