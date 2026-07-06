#pragma once

// =============================================================================
// 文件：FastMQTTTypes.hpp
// 模块：FastMQTT
// 说明：FastMQTT 通信模块使用的公共数据类型定义。
//
//   本文件仅包含“纯通信层”类型（配置、消息、状态、统计），
//   不包含任何业务类型或 protobuf 类型，保证通信层与协议/业务完全解耦。
// =============================================================================

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace fast_mqtt {

// -----------------------------------------------------------------------------
// 一、生命周期状态
// -----------------------------------------------------------------------------
/**
 * @brief FastMQTT 生命周期状态。
 *
 * 状态迁移：
 *   Uninitialized -> Initialized -> Starting -> Running -> Stopping -> Stopped
 *
 * 约束：
 *   - Initialize 只能调用一次；
 *   - Start 只能调用一次；
 *   - Stop 可安全退出所有线程。
 */
enum class LifecycleState {
    Uninitialized = 0,  ///< 未初始化。
    Initialized,        ///< 已初始化（已解析配置、创建句柄）。
    Starting,           ///< 正在启动线程。
    Running,            ///< 运行中。
    Stopping,           ///< 正在停止。
    Stopped             ///< 已停止。
};

/**
 * @brief 将生命周期状态转换为可读字符串（便于日志与健康状态输出）。
 */
inline const char* LifecycleStateToString(LifecycleState s) {
    switch (s) {
        case LifecycleState::Uninitialized: return "Uninitialized";
        case LifecycleState::Initialized:   return "Initialized";
        case LifecycleState::Starting:      return "Starting";
        case LifecycleState::Running:       return "Running";
        case LifecycleState::Stopping:      return "Stopping";
        case LifecycleState::Stopped:       return "Stopped";
        default:                            return "Unknown";
    }
}

// -----------------------------------------------------------------------------
// 二、配置结构
// -----------------------------------------------------------------------------
/**
 * @brief Broker 连接配置。对应 JSON 中的 mqtt.broker 节点。
 */
struct BrokerConfig {
    std::string host{"127.0.0.1"};   ///< Broker 主机地址。
    int         port{1883};          ///< Broker 端口。
    std::string client_id;           ///< 客户端 ID，为空时由库自动生成。
    std::string username;            ///< 用户名，为空表示不使用鉴权。
    std::string password;            ///< 密码。
    int         keep_alive{60};      ///< MQTT KeepAlive 秒数。
    bool        clean_session{true}; ///< 是否清理会话。
    bool        auto_reconnect{true};///< 是否启用断线自动重连。
    int         connect_timeout_ms{5000};  ///< 连接超时（毫秒）。
};

/**
 * @brief 线程 / 队列配置。对应 JSON 中的 mqtt.thread 节点。
 */
struct ThreadConfig {
    std::size_t send_queue_size{1000};  ///< 发送队列最大长度。
    std::size_t recv_queue_size{1000};  ///< 接收队列最大长度。
};

/**
 * @brief 默认发布参数。对应 JSON 中的 mqtt.default 节点。
 */
struct DefaultConfig {
    int  qos{1};        ///< 默认 QoS。
    bool retain{false}; ///< 默认 retain。
};

/**
 * @brief FastMQTT 完整配置。对应 JSON 中的 mqtt 节点。
 */
struct FastMQTTConfig {
    bool          enable{true};  ///< 是否启用 MQTT 功能。
    BrokerConfig  broker;        ///< Broker 配置。
    ThreadConfig  thread;        ///< 线程 / 队列配置。
    DefaultConfig def;           ///< 默认发布参数。

    /**
     * @brief 从 JSON 解析配置。
     *
     * 支持两种输入：
     *   1. 直接传入 mqtt 节点内容 { "enable":..., "broker":{...}, ... }
     *   2. 传入包含 "mqtt" 键的顶层对象 { "mqtt": { ... } }
     *
     * @param j 输入 JSON。
     * @return 解析后的配置对象。缺失字段使用默认值，保证健壮性。
     */
    static FastMQTTConfig FromJson(const nlohmann::json& j) {
        // 兼容顶层包含 "mqtt" 的情况。
        const nlohmann::json& m = (j.contains("mqtt") && j["mqtt"].is_object())
                                      ? j["mqtt"]
                                      : j;
        FastMQTTConfig cfg;
        cfg.enable = m.value("enable", true);

        if (m.contains("broker") && m["broker"].is_object()) {
            const auto& b = m["broker"];
            cfg.broker.host               = b.value("host", cfg.broker.host);
            cfg.broker.port               = b.value("port", cfg.broker.port);
            cfg.broker.client_id          = b.value("client_id", cfg.broker.client_id);
            cfg.broker.username           = b.value("username", cfg.broker.username);
            cfg.broker.password           = b.value("password", cfg.broker.password);
            cfg.broker.keep_alive         = b.value("keep_alive", cfg.broker.keep_alive);
            cfg.broker.clean_session      = b.value("clean_session", cfg.broker.clean_session);
            cfg.broker.auto_reconnect     = b.value("auto_reconnect", cfg.broker.auto_reconnect);
            cfg.broker.connect_timeout_ms = b.value("connect_timeout_ms", cfg.broker.connect_timeout_ms);
        }

        if (m.contains("thread") && m["thread"].is_object()) {
            const auto& t = m["thread"];
            cfg.thread.send_queue_size = t.value("send_queue_size", cfg.thread.send_queue_size);
            cfg.thread.recv_queue_size = t.value("recv_queue_size", cfg.thread.recv_queue_size);
        }

        if (m.contains("default") && m["default"].is_object()) {
            const auto& d = m["default"];
            cfg.def.qos    = d.value("qos", cfg.def.qos);
            cfg.def.retain = d.value("retain", cfg.def.retain);
        }
        return cfg;
    }
};

// -----------------------------------------------------------------------------
// 三、消息结构
// -----------------------------------------------------------------------------
/**
 * @brief 统一消息结构，既用于发送也用于接收。
 *
 * 说明：
 *   - payload 为二进制安全的字节串（std::string 可容纳 '\0'），
 *     因此 protobuf 序列化后的字节可直接放入 payload。
 *   - 发送时使用 timestamp 记录入队时间；接收时使用 timestamp 记录收到时间。
 */
struct Message {
    std::string topic;       ///< 主题。
    std::string payload;     ///< 负载（二进制安全）。
    int         qos{0};      ///< QoS 等级。
    bool        retain{false};  ///< 是否 retain。
    std::int64_t timestamp{0};  ///< 时间戳（Unix 秒）：发送=入队时间，接收=收到时间。

    Message() = default;
    Message(std::string t, std::string p, int q = 0, bool r = false)
        : topic(std::move(t)), payload(std::move(p)), qos(q), retain(r) {}
};

/**
 * @brief 业务回调函数类型。
 *
 * 派发线程（Dispatcher）在匹配到 Topic 后调用该回调。
 * 回调中禁止执行长时间阻塞操作，避免拖慢派发线程。
 */
using MessageCallback = std::function<void(const Message&)>;

// -----------------------------------------------------------------------------
// 四、统计信息（供健康状态使用）
// -----------------------------------------------------------------------------
/**
 * @brief 运行期统计计数器。全部使用原子类型，保证多线程读写安全。
 */
struct Statistics {
    std::atomic<std::int64_t> reconnect_count{0};    ///< 累计重连次数。
    std::atomic<std::int64_t> last_connect_time{0};  ///< 最近一次连接成功时间。
    std::atomic<std::int64_t> last_send_time{0};     ///< 最近一次发送成功时间。
    std::atomic<std::int64_t> last_recv_time{0};     ///< 最近一次收到消息时间。
    std::atomic<std::int64_t> send_success{0};       ///< 发送成功计数。
    std::atomic<std::int64_t> send_failed{0};        ///< 发送失败计数。
    std::atomic<std::int64_t> recv_count{0};         ///< 收到消息计数。
    std::atomic<std::int64_t> callback_failed{0};    ///< 回调执行失败计数。
    std::atomic<std::int64_t> send_dropped{0};       ///< 发送队列满被丢弃计数。
    std::atomic<std::int64_t> recv_dropped{0};       ///< 接收队列满被丢弃计数。
};

}  // namespace fast_mqtt
