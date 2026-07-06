// =============================================================================
// 文件：FastMQTT.cpp
// 模块：FastMQTT —— 全系统唯一的 MQTT 通信模块实现。
//
// 线程模型说明（关键）：
//   为保证 mosquitto 句柄的“网络状态”仅被单一线程操作，实现约定如下：
//     - Receiver 线程：唯一负责 connect / mosquitto_loop / reconnect 的线程，
//       即“网络所有权”归 Receiver。它在断开时按指数退避重连。
//     - ConnectionManager 线程：只做 IP 连通性探测（TCP connect 测试）与状态聚合，
//       不直接触碰 mosquitto 网络状态，避免多线程竞争。
//     - Sender 线程：调用 mosquitto_publish；mosquitto 允许在网络循环运行时
//       从其它线程发布，另加 pub_mutex_ 保护更稳妥。
//     - Dispatcher 线程：只消费接收队列并执行业务回调，不触碰 mosquitto。
// =============================================================================

#include "FastMQTT.hpp"

#include <chrono>
#include <cstring>
#include <cerrno>
#include <exception>

#include <mosquitto.h>

// POSIX 套接字，用于 ConnectionManager 的 TCP 连通性探测。
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "MyLog.h"

namespace fast_mqtt {

namespace {
// mosquitto_lib_init / cleanup 为进程级全局操作，使用引用计数保证只初始化一次。
std::atomic<int> g_lib_ref{0};

// 指数退避序列（秒）：1, 2, 5, 10, 20, 30, 30, ...
const int kBackoffTable[] = {1, 2, 5, 10, 20, 30};
constexpr std::size_t kBackoffTableSize = sizeof(kBackoffTable) / sizeof(kBackoffTable[0]);
}  // namespace

// -----------------------------------------------------------------------------
// 单例与构造 / 析构
// -----------------------------------------------------------------------------
FastMQTT& FastMQTT::GetInstance() {
    static FastMQTT instance;
    return instance;
}

FastMQTT::FastMQTT() = default;

FastMQTT::~FastMQTT() {
    try {
        Destroy();
    } catch (...) {
        // 析构中绝不抛出异常。
    }
}

std::int64_t FastMQTT::NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void FastMQTT::SetState(LifecycleState s) {
    state_.store(s);
}

// -----------------------------------------------------------------------------
// 初始化
// -----------------------------------------------------------------------------
bool FastMQTT::Initialize(const nlohmann::json& config) {
    std::lock_guard<std::mutex> lk(lifecycle_mutex_);

    // 生命周期约束：Initialize 只能成功一次。
    if (state_.load() != LifecycleState::Uninitialized) {
        MYLOG_WARN("【MQTT】重复初始化被拒绝，当前状态={}", LifecycleStateToString(state_.load()));
        return false;
    }

    try {
        config_ = FastMQTTConfig::FromJson(config);
    } catch (const std::exception& e) {
        MYLOG_ERROR("【MQTT】解析配置失败：{}", e.what());
        return false;
    }

    if (!config_.enable) {
        // 未启用时也允许初始化，但不创建 mosquitto 句柄，Start 将直接返回。
        MYLOG_WARN("【MQTT】配置中 enable=false，MQTT 功能未启用");
        SetState(LifecycleState::Initialized);
        return true;
    }

    if (config_.broker.host.empty()) {
        MYLOG_ERROR("【MQTT】初始化失败：broker.host 为空");
        return false;
    }

    // 进程级库初始化（引用计数）。
    if (g_lib_ref.fetch_add(1) == 0) {
        mosquitto_lib_init();
    }

    // 创建客户端句柄，userdata 指向 this 以便回调转发。
    mosq_ = mosquitto_new(config_.broker.client_id.empty() ? nullptr : config_.broker.client_id.c_str(),
                          config_.broker.clean_session,
                          this);
    if (!mosq_) {
        MYLOG_ERROR("【MQTT】mosquitto_new 失败");
        if (g_lib_ref.fetch_sub(1) == 1) {
            mosquitto_lib_cleanup();
        }
        return false;
    }

    // 设置用户名 / 密码。
    if (!config_.broker.username.empty()) {
        int rc = mosquitto_username_pw_set(
            mosq_, config_.broker.username.c_str(),
            config_.broker.password.empty() ? nullptr : config_.broker.password.c_str());
        if (rc != MOSQ_ERR_SUCCESS) {
            MYLOG_ERROR("【MQTT】设置用户名/密码失败：{}", mosquitto_strerror(rc));
        }
    } else {
        MYLOG_WARN("【MQTT】未设置用户名/密码，使用匿名连接");
    }

    // 绑定回调。
    mosquitto_connect_callback_set(mosq_, &FastMQTT::OnConnectTrampoline);
    mosquitto_disconnect_callback_set(mosq_, &FastMQTT::OnDisconnectTrampoline);
    mosquitto_message_callback_set(mosq_, &FastMQTT::OnMessageTrampoline);
    mosquitto_log_callback_set(mosq_, &FastMQTT::OnLogTrampoline);

    // 创建收发队列。
    send_queue_ = std::unique_ptr<BlockingQueue<Message>>(
        new BlockingQueue<Message>(config_.thread.send_queue_size));
    recv_queue_ = std::unique_ptr<BlockingQueue<Message>>(
        new BlockingQueue<Message>(config_.thread.recv_queue_size));

    SetState(LifecycleState::Initialized);
    MYLOG_INFO("【MQTT】初始化成功 host={} port={} client_id={}",
               config_.broker.host, config_.broker.port, config_.broker.client_id);
    return true;
}

// -----------------------------------------------------------------------------
// 启动
// -----------------------------------------------------------------------------
bool FastMQTT::Start() {
    std::lock_guard<std::mutex> lk(lifecycle_mutex_);

    if (state_.load() != LifecycleState::Initialized) {
        MYLOG_WARN("【MQTT】Start 被拒绝，当前状态={}", LifecycleStateToString(state_.load()));
        return false;
    }

    if (!config_.enable) {
        MYLOG_WARN("【MQTT】enable=false，Start 直接返回");
        return false;
    }

    SetState(LifecycleState::Starting);
    running_.store(true);
    ResetBackoff();

    // 依次创建四个后台线程。
    conn_thread_ = std::thread(&FastMQTT::ConnectionManagerLoop, this);
    recv_thread_ = std::thread(&FastMQTT::ReceiverLoop, this);
    disp_thread_ = std::thread(&FastMQTT::DispatcherLoop, this);
    send_thread_ = std::thread(&FastMQTT::SenderLoop, this);

    SetState(LifecycleState::Running);
    MYLOG_INFO("【MQTT】模块启动完成，后台线程已就绪");
    return true;
}

// -----------------------------------------------------------------------------
// 停止
// -----------------------------------------------------------------------------
void FastMQTT::Stop() {
    std::lock_guard<std::mutex> lk(lifecycle_mutex_);

    const LifecycleState s = state_.load();
    if (s != LifecycleState::Running && s != LifecycleState::Starting) {
        // 已停止或未启动，幂等返回。
        return;
    }

    MYLOG_INFO("【MQTT】准备退出线程");
    SetState(LifecycleState::Stopping);
    running_.store(false);

    // 唤醒可能阻塞在队列上的线程。
    if (send_queue_) send_queue_->Shutdown();
    if (recv_queue_) recv_queue_->Shutdown();

    // 断开连接，促使 mosquitto_loop 返回。
    if (mosq_) {
        mosquitto_disconnect(mosq_);
    }

    // join 所有线程。
    if (conn_thread_.joinable()) conn_thread_.join();
    if (recv_thread_.joinable()) recv_thread_.join();
    if (disp_thread_.joinable()) disp_thread_.join();
    if (send_thread_.joinable()) send_thread_.join();

    broker_connected_.store(false);
    session_connected_.store(false);
    ready_.store(false);
    ip_alive_.store(false);

    SetState(LifecycleState::Stopped);
    MYLOG_INFO("【MQTT】MQTT模块停止完成");
}

// -----------------------------------------------------------------------------
// 销毁
// -----------------------------------------------------------------------------
void FastMQTT::Destroy() {
    // 先停止线程。
    Stop();

    std::lock_guard<std::mutex> lk(lifecycle_mutex_);
    if (mosq_) {
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
        if (g_lib_ref.fetch_sub(1) == 1) {
            mosquitto_lib_cleanup();
        }
    }

    ClearAllCallbacks();
    send_queue_.reset();
    recv_queue_.reset();
    SetState(LifecycleState::Uninitialized);
}

// -----------------------------------------------------------------------------
// 获取模块状态
// -----------------------------------------------------------------------------
nlohmann::json FastMQTT::Status() {
    std::lock_guard<std::mutex> lk(lifecycle_mutex_);
    nlohmann::json status;
    status["state"] = static_cast<int>(state_.load());
    status["ready"] = ready_.load();
    status["broker_connected"] = broker_connected_.load();
    status["session_connected"] = session_connected_.load();
    status["ip_alive"] = ip_alive_.load();

    {
        std::lock_guard<std::mutex> cb_lk(cb_mutex_);
        std::size_t callback_count = 0;
        nlohmann::json callback_topics = nlohmann::json::array();
        for (const auto& kv : callbacks_) {
            callback_count += kv.second.size();
            nlohmann::json item;
            item["topic"] = kv.first;
            item["callback_count"] = kv.second.size();

            nlohmann::json handles = nlohmann::json::array();
            nlohmann::json qos_values = nlohmann::json::array();
            for (const auto& entry : kv.second) {
                handles.push_back(entry.handle);
                qos_values.push_back(entry.qos);
            }
            item["handles"] = handles;
            item["qos_list"] = qos_values;
            callback_topics.push_back(std::move(item));
        }
        status["callback_count"] = callback_count;
        status["callback_topics"] = std::move(callback_topics);
    }

    status["send_queue_size"] = send_queue_ ? send_queue_->Size() : 0;
    status["recv_queue_size"] = recv_queue_ ? recv_queue_->Size() : 0;

    nlohmann::json queue_status;
    queue_status["send"] = send_queue_ ? send_queue_->Size() : 0;
    queue_status["recv"] = recv_queue_ ? recv_queue_->Size() : 0;
    status["queues"] = std::move(queue_status);

    nlohmann::json thread_status;
    thread_status["conn_thread"] = {
        {"alive", conn_thread_.joinable()},
        {"running", running_.load() && conn_thread_.joinable()}
    };
    thread_status["recv_thread"] = {
        {"alive", recv_thread_.joinable()},
        {"running", running_.load() && recv_thread_.joinable()}
    };
    thread_status["disp_thread"] = {
        {"alive", disp_thread_.joinable()},
        {"running", running_.load() && disp_thread_.joinable()}
    };
    thread_status["send_thread"] = {
        {"alive", send_thread_.joinable()},
        {"running", running_.load() && send_thread_.joinable()}
    };
    status["threads"] = std::move(thread_status);

    return status;
}

// -----------------------------------------------------------------------------
// 发布消息
// -----------------------------------------------------------------------------
bool FastMQTT::Publish(const std::string& topic, const std::string& payload) {
    return Publish(topic, payload, config_.def.qos, config_.def.retain);
}

bool FastMQTT::Publish(const std::string& topic, const std::string& payload, int qos) {
    return Publish(topic, payload, qos, config_.def.retain);
}

bool FastMQTT::Publish(const std::string& topic, const std::string& payload, int qos, bool retain) {
    if (state_.load() != LifecycleState::Running || !send_queue_) {
        MYLOG_WARN("【MQTT】发送被拒绝：模块未运行 Topic={}", topic);
        return false;
    }

    Message msg(topic, payload, qos, retain);
    msg.timestamp = NowSeconds();

    // 业务线程只负责入队，真正发送由 Sender 线程完成。
    if (!send_queue_->Push(std::move(msg))) {
        stats_.send_dropped.fetch_add(1);
        MYLOG_WARN("【MQTT】发送队列已满，消息被丢弃 Topic={}", topic);
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// 回调管理
// -----------------------------------------------------------------------------
std::uint64_t FastMQTT::RegisterCallback(const std::string& topic, MessageCallback callback, int qos) {
    if (!callback) {
        MYLOG_WARN("【MQTT】注册回调被忽略：回调为空 Topic={}", topic);
        return 0;
    }

    const std::uint64_t handle = next_handle_.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        CallbackEntry entry;
        entry.handle = handle;
        entry.filter = topic;
        entry.callback = std::move(callback);
        entry.qos = qos;
        // callbacks_[topic].push_back(std::move(entry));
        callbacks_[topic].insert(callbacks_[topic].end(), std::move(entry));
    }

    // 若已连接，立即订阅；否则等待连接成功后由 ResubscribeAll 统一订阅。
    if (session_connected_.load() && mosq_) {
        std::lock_guard<std::mutex> lk(pub_mutex_);
        int rc = mosquitto_subscribe(mosq_, nullptr, topic.c_str(), qos);
        if (rc != MOSQ_ERR_SUCCESS) {
            MYLOG_WARN("【MQTT】订阅失败 Topic={} err={}", topic, mosquitto_strerror(rc));
        } else {
            MYLOG_INFO("【MQTT】订阅成功 Topic={} qos={}", topic, qos);
        }
    }
    return handle;
}

bool FastMQTT::UnregisterCallback(std::uint64_t handle) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    for (auto it = callbacks_.begin(); it != callbacks_.end(); ++it) {
        auto& vec = it->second;
        for (auto vit = vec.begin(); vit != vec.end(); ++vit) {
            if (vit->handle == handle) {
                vec.erase(vit);
                if (vec.empty()) {
                    // 该 Topic 无剩余回调，取消订阅。
                    if (session_connected_.load() && mosq_) {
                        std::lock_guard<std::mutex> plk(pub_mutex_);
                        mosquitto_unsubscribe(mosq_, nullptr, it->first.c_str());
                    }
                    callbacks_.erase(it);
                }
                return true;
            }
        }
    }
    return false;
}

void FastMQTT::ClearCallback(const std::string& topic) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    auto it = callbacks_.find(topic);
    if (it != callbacks_.end()) {
        if (session_connected_.load() && mosq_) {
            std::lock_guard<std::mutex> plk(pub_mutex_);
            mosquitto_unsubscribe(mosq_, nullptr, topic.c_str());
        }
        callbacks_.erase(it);
    }
}

void FastMQTT::ClearAllCallbacks() {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    callbacks_.clear();
}

// -----------------------------------------------------------------------------
// 状态接口
// -----------------------------------------------------------------------------
bool FastMQTT::IsReady() const { return ready_.load(); }
bool FastMQTT::IsConnected() const { return broker_connected_.load(); }
bool FastMQTT::IsIPAlive() const { return ip_alive_.load(); }
bool FastMQTT::IsEnabled() const { return config_.enable; }

nlohmann::json FastMQTT::GetStatistics() const {
    nlohmann::json j;
    j["reconnect_count"] = stats_.reconnect_count.load();
    j["last_connect_time"] = stats_.last_connect_time.load();
    j["last_send_time"] = stats_.last_send_time.load();
    j["last_recv_time"] = stats_.last_recv_time.load();
    j["send_success"] = stats_.send_success.load();
    j["send_failed"] = stats_.send_failed.load();
    j["recv_count"] = stats_.recv_count.load();
    j["callback_failed"] = stats_.callback_failed.load();
    j["send_dropped"] = stats_.send_dropped.load();
    j["recv_dropped"] = stats_.recv_dropped.load();
    return j;
}

nlohmann::json FastMQTT::GetHealthStatus() const {
    nlohmann::json j;
    j["enable"] = config_.enable;
    j["state"] = LifecycleStateToString(state_.load());
    j["ready"] = ready_.load();
    j["broker_connected"] = broker_connected_.load();
    j["ip_alive"] = ip_alive_.load();
    j["reconnect_count"] = stats_.reconnect_count.load();
    j["last_connect_time"] = stats_.last_connect_time.load();
    j["last_send_time"] = stats_.last_send_time.load();
    j["last_recv_time"] = stats_.last_recv_time.load();
    j["send_queue_size"] = GetSendQueueSize();
    j["recv_queue_size"] = GetReceiveQueueSize();
    j["send_success"] = stats_.send_success.load();
    j["send_failed"] = stats_.send_failed.load();
    j["recv_count"] = stats_.recv_count.load();
    j["callback_failed"] = stats_.callback_failed.load();
    return j;
}

std::size_t FastMQTT::GetSendQueueSize() const {
    return send_queue_ ? send_queue_->Size() : 0;
}

std::size_t FastMQTT::GetReceiveQueueSize() const {
    return recv_queue_ ? recv_queue_->Size() : 0;
}

// -----------------------------------------------------------------------------
// mosquitto C 回调转发
// -----------------------------------------------------------------------------
void FastMQTT::OnConnectTrampoline(struct mosquitto*, void* obj, int rc) {
    if (auto* self = static_cast<FastMQTT*>(obj)) self->HandleConnect(rc);
}

void FastMQTT::OnDisconnectTrampoline(struct mosquitto*, void* obj, int rc) {
    if (auto* self = static_cast<FastMQTT*>(obj)) self->HandleDisconnect(rc);
}

void FastMQTT::OnMessageTrampoline(struct mosquitto*, void* obj, const struct mosquitto_message* msg) {
    if (auto* self = static_cast<FastMQTT*>(obj)) self->HandleMessage(msg);
}

void FastMQTT::OnLogTrampoline(struct mosquitto*, void*, int, const char* str) {
    if (str) MYLOG_DEBUG("【MQTT】[mosquitto] {}", str);
}

void FastMQTT::HandleConnect(int rc) {
    if (rc == 0) {
        broker_connected_.store(true);
        session_connected_.store(true);
        ready_.store(config_.enable);
        stats_.last_connect_time.store(NowSeconds());
        ResetBackoff();
        MYLOG_INFO("【MQTT】Broker连接成功");
        // 连接（或重连）成功后，重新订阅所有已注册主题。
        ResubscribeAll();
    } else {
        broker_connected_.store(false);
        session_connected_.store(false);
        ready_.store(false);
        MYLOG_WARN("【MQTT】Broker连接失败 rc={}", rc);
    }
}

void FastMQTT::HandleDisconnect(int rc) {
    broker_connected_.store(false);
    session_connected_.store(false);
    ready_.store(false);
    if (!running_.load()) {
        MYLOG_INFO("【MQTT】Broker连接断开（模块正在停止）rc={}", rc);
        return;
    }
    MYLOG_WARN("【MQTT】Broker连接断开 rc={}", rc);
}

void FastMQTT::HandleMessage(const struct mosquitto_message* msg) {
    if (!msg || !msg->topic || !recv_queue_) return;

    Message m;
    m.topic = msg->topic;
    if (msg->payload && msg->payloadlen > 0) {
        m.payload.assign(static_cast<const char*>(msg->payload),
                         static_cast<std::size_t>(msg->payloadlen));
    }
    m.qos = msg->qos;
    m.retain = msg->retain;
    m.timestamp = NowSeconds();

    stats_.recv_count.fetch_add(1);
    stats_.last_recv_time.store(m.timestamp);

    // Receiver 线程绝不执行业务，仅入队。
    if (!recv_queue_->Push(std::move(m))) {
        stats_.recv_dropped.fetch_add(1);
        MYLOG_WARN("【MQTT】接收队列已满，消息被丢弃 Topic={}", msg->topic);
    } else {
        MYLOG_DEBUG("【MQTT】Topic={} 消息入队成功, queue_size={}", msg->topic, recv_queue_->Size());
    }
}

// -----------------------------------------------------------------------------
// 线程一：ConnectionManager —— IP 连通性监测与状态聚合
// -----------------------------------------------------------------------------
void FastMQTT::ConnectionManagerLoop() {
    MYLOG_INFO("【MQTT】ConnectionManager线程启动");
    while (running_.load()) {
        try {
            const bool alive = CheckTcpAlive(config_.broker.host, config_.broker.port, 1000);
            ip_alive_.store(alive);
            if (!alive) {
                MYLOG_DEBUG("【MQTT】目标 {}:{} 网络不可达", config_.broker.host, config_.broker.port);
            }
        } catch (const std::exception& e) {
            MYLOG_ERROR("【MQTT】ConnectionManager异常：{}", e.what());
        } catch (...) {
            MYLOG_ERROR("【MQTT】ConnectionManager未知异常");
        }
        // 每 2 秒探测一次，退出时快速响应。
        for (int i = 0; i < 20 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    MYLOG_INFO("【MQTT】ConnectionManager线程退出");
}

// -----------------------------------------------------------------------------
// 线程二：Receiver —— MQTT 网络循环与自动重连（网络所有权线程）
// -----------------------------------------------------------------------------
void FastMQTT::ReceiverLoop() {
    MYLOG_INFO("【MQTT】Receiver线程启动");
    MYLOG_INFO("【MQTT】开始连接 Broker host={} port={}", config_.broker.host, config_.broker.port);

    bool ever_connected = false;  // 是否成功发起过一次连接（决定用 connect 还是 reconnect）。

    while (running_.load()) {
        try {
            // ---- 步骤 1：发起连接或重连 ----
            int rc;
            {
                std::lock_guard<std::mutex> lk(pub_mutex_);
                if (!ever_connected) {
                    rc = mosquitto_connect(mosq_, config_.broker.host.c_str(),
                                           config_.broker.port, config_.broker.keep_alive);
                } else {
                    stats_.reconnect_count.fetch_add(1);
                    MYLOG_INFO("【MQTT】开始自动重连");
                    rc = mosquitto_reconnect(mosq_);
                }
            }
            ever_connected = true;

            if (rc != MOSQ_ERR_SUCCESS) {
                // 发起连接失败，按退避策略等待后重试。
                const int wait = NextBackoffSeconds();
                MYLOG_WARN("【MQTT】连接 Broker 失败：{}，{} 秒后重试", mosquitto_strerror(rc), wait);
                for (int i = 0; i < wait * 10 && running_.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (!config_.broker.auto_reconnect) {
                    MYLOG_WARN("【MQTT】auto_reconnect=false，Receiver线程退出");
                    break;
                }
                continue;
            }

            // ---- 步骤 2：网络循环，处理 CONNACK / 收发 / 心跳 ----
            // mosquitto_loop 返回非成功即视为断开，跳出触发重连。
            while (running_.load()) {
                int lrc = mosquitto_loop(mosq_, 100 /*ms*/, 1);
                if (lrc == MOSQ_ERR_SUCCESS) {
                    continue;
                }
                broker_connected_.store(false);
                session_connected_.store(false);
                ready_.store(false);
                if (running_.load()) {
                    MYLOG_WARN("【MQTT】网络循环返回 {}，判定为断开", mosquitto_strerror(lrc));
                }
                break;
            }

            // ---- 步骤 3：断线退避等待后重连 ----
            if (running_.load()) {
                if (!config_.broker.auto_reconnect) {
                    MYLOG_WARN("【MQTT】auto_reconnect=false，Receiver线程退出");
                    break;
                }
                const int wait = NextBackoffSeconds();
                MYLOG_INFO("【MQTT】{} 秒后尝试重连", wait);
                for (int i = 0; i < wait * 10 && running_.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        } catch (const std::exception& e) {
            MYLOG_ERROR("【MQTT】Receiver线程异常：{}", e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } catch (...) {
            MYLOG_ERROR("【MQTT】Receiver线程未知异常");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    MYLOG_INFO("【MQTT】Receiver线程退出");
}

// -----------------------------------------------------------------------------
// 线程三：Dispatcher —— 消费接收队列并派发回调
// -----------------------------------------------------------------------------
void FastMQTT::DispatcherLoop() {
    MYLOG_INFO("【MQTT】Dispatcher线程启动");
    while (running_.load()) {
        try {
            Message msg;
            // 带超时出队，便于周期性检查退出标志。
            if (!recv_queue_->Pop(msg, 200)) {
                continue;  // 超时或已 shutdown。
            }
            MYLOG_DEBUG("【MQTT】工作线程 收到消息 Topic={}", msg.topic);
            DispatchToCallbacks(msg);
        } catch (const std::exception& e) {
            MYLOG_ERROR("【MQTT】Dispatcher线程异常：{}", e.what());
        } catch (...) {
            MYLOG_ERROR("【MQTT】Dispatcher线程未知异常");
        }
    }
    MYLOG_INFO("【MQTT】Dispatcher线程退出");
}

// -----------------------------------------------------------------------------
// 线程四：Sender —— 消费发送队列并执行发布
// -----------------------------------------------------------------------------
void FastMQTT::SenderLoop() {
    MYLOG_INFO("【MQTT】Sender线程启动");
    while (running_.load()) {
        try {
            Message msg;
            if (!send_queue_->Pop(msg, 200)) {
                continue;
            }
            if (DoPublish(msg)) {
                stats_.send_success.fetch_add(1);
                stats_.last_send_time.store(NowSeconds());
                MYLOG_DEBUG("【MQTT】发送消息 Topic={}", msg.topic);
            } else {
                stats_.send_failed.fetch_add(1);
            }
        } catch (const std::exception& e) {
            MYLOG_ERROR("【MQTT】Sender线程异常：{}", e.what());
        } catch (...) {
            MYLOG_ERROR("【MQTT】Sender线程未知异常");
        }
    }
    MYLOG_INFO("【MQTT】Sender线程退出");
}

// -----------------------------------------------------------------------------
// 辅助函数
// -----------------------------------------------------------------------------
bool FastMQTT::DoPublish(const Message& msg) {
    if (!mosq_ || !broker_connected_.load()) {
        MYLOG_WARN("【MQTT】发布跳过：未连接 Topic={}", msg.topic);
        return false;
    }
    std::lock_guard<std::mutex> lk(pub_mutex_);
    int mid = 0;
    int rc = mosquitto_publish(mosq_, &mid, msg.topic.c_str(),
                               static_cast<int>(msg.payload.size()),
                               msg.payload.data(), msg.qos, msg.retain);
    if (rc != MOSQ_ERR_SUCCESS) {
        MYLOG_ERROR("【MQTT】发布失败：{} Topic={}", mosquitto_strerror(rc), msg.topic);
        return false;
    }
    return true;
}

void FastMQTT::DispatchToCallbacks(const Message& msg) {
    // 拷贝命中的回调，避免持锁执行业务回调造成死锁或长时间占锁。
    std::vector<CallbackEntry> matched;
    {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        for (const auto& kv : callbacks_) {
            if (TopicMatch(kv.first, msg.topic)) {
                for (const auto& e : kv.second) {
                    // matched.push_back(e);
                    matched.insert(matched.begin(), e);
                }
            }
        }
    }

    MYLOG_DEBUG("【MQTT】消息 Topic={} 命中回调数量={}", msg.topic, matched.size());

    // 逐个执行，单个失败不影响其它回调。
    int index = 1;
    for (const auto& e : matched) {
        try {
            MYLOG_DEBUG("+++++++++++++++++++++++++++++++++++++++++++++++-V --- No.{}/{}", index, matched.size());
            e.callback(msg);
            MYLOG_DEBUG("+++++++++++++++++++++++++++++++++++++++++++++++-A");
        } catch (const std::exception& ex) {
            stats_.callback_failed.fetch_add(1);
            MYLOG_ERROR("【MQTT】回调执行失败 filter={} err={}", e.filter, ex.what());
        } catch (...) {
            stats_.callback_failed.fetch_add(1);
            MYLOG_ERROR("【MQTT】回调执行未知异常 filter={}", e.filter);
        }
        index++;
    }
    if (!matched.empty()) {
        MYLOG_DEBUG("【MQTT】回调执行完成 Topic={} 命中={}", msg.topic, matched.size());
    }
}

void FastMQTT::ResubscribeAll() {
    std::vector<std::pair<std::string, int>> subs;
    {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        for (const auto& kv : callbacks_) {
            // 同一 Topic 上的多个回调，取其中一个 qos 订阅一次即可。
            int qos = kv.second.empty() ? 1 : kv.second.front().qos;
            subs.emplace_back(kv.first, qos);
        }
    }
    std::lock_guard<std::mutex> lk(pub_mutex_);
    for (const auto& s : subs) {
        if (!mosq_) break;
        int rc = mosquitto_subscribe(mosq_, nullptr, s.first.c_str(), s.second);
        if (rc != MOSQ_ERR_SUCCESS) {
            MYLOG_WARN("【MQTT】重新订阅失败 Topic={} err={}", s.first, mosquitto_strerror(rc));
        } else {
            MYLOG_INFO("【MQTT】重新订阅成功 Topic={} qos={}", s.first, s.second);
        }
    }
}

int FastMQTT::NextBackoffSeconds() {
    std::lock_guard<std::mutex> lk(backoff_mutex_);
    const std::size_t idx = backoff_index_;
    if (backoff_index_ + 1 < kBackoffTableSize) {
        ++backoff_index_;
    }
    return kBackoffTable[idx < kBackoffTableSize ? idx : kBackoffTableSize - 1];
}

void FastMQTT::ResetBackoff() {
    std::lock_guard<std::mutex> lk(backoff_mutex_);
    backoff_index_ = 0;
}

bool FastMQTT::TopicMatch(const std::string& filter, const std::string& topic) {
    // 使用 mosquitto 库提供的标准匹配实现，正确处理 + 与 # 通配符。
    bool result = false;
    if (mosquitto_topic_matches_sub(filter.c_str(), topic.c_str(), &result) != MOSQ_ERR_SUCCESS) {
        return false;
    }
    return result;
}

bool FastMQTT::CheckTcpAlive(const std::string& host, int port, int timeout_ms) {
    // 通过一次非阻塞 TCP connect 判断目标主机端口是否可达。
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    const std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        return false;
    }

    bool ok = false;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        int fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        // 设为非阻塞。
        int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        int rc = ::connect(fd, p->ai_addr, p->ai_addrlen);
        if (rc == 0) {
            ok = true;
            ::close(fd);
            break;
        }
        if (errno == EINPROGRESS) {
            fd_set wset;
            FD_ZERO(&wset);
            FD_SET(fd, &wset);
            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            rc = ::select(fd + 1, nullptr, &wset, nullptr, &tv);
            if (rc > 0 && FD_ISSET(fd, &wset)) {
                int err = 0;
                socklen_t len = sizeof(err);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                    ok = true;
                }
            }
        }
        ::close(fd);
        if (ok) break;
    }

    freeaddrinfo(res);
    return ok;
}

}  // namespace fast_mqtt
