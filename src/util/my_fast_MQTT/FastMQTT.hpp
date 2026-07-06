#pragma once

// =============================================================================
// 文件：FastMQTT.hpp
// 模块：FastMQTT —— 全系统唯一的 MQTT 通信模块（单例）。
//
// 职责：
//   - 与 MQTT Broker 建立连接、断线自动重连；
//   - 消息发送 / 接收；
//   - Topic 回调管理；
//   - 线程安全消息队列（发送队列 + 接收队列）；
//   - 连接状态监控与健康状态统计。
//
// 不负责：
//   - protobuf 编解码（由协议层负责）；
//   - 业务逻辑、Topic 内容解释、Controller 调度。
//
// 依赖（严格受限）：
//   MyLog、Mosquitto Client Library、BlockingQueue、nlohmann::json。
//   禁止依赖 HTTP / Controller / 业务模块 / protobuf 业务类型 / 数据库 / UI。
//
// 线程模型：四个后台线程
//   1. ConnectionManager —— IP 连通性监测与状态聚合；
//   2. Receiver          —— MQTT 网络循环，收到消息压入接收队列；
//   3. Dispatcher        —— 消费接收队列，按 Topic 派发业务回调；
//   4. Sender            —— 消费发送队列，统一执行 MQTT Publish。
// =============================================================================

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "BlockingQueue.hpp"
#include "FastMQTTTypes.hpp"

// 前置声明 mosquitto，避免在头文件暴露第三方类型给业务层。
struct mosquitto;
struct mosquitto_message;

namespace fast_mqtt {

/**
 * @brief FastMQTT 通信模块单例。
 *
 * 使用方式：
 * @code
 *   FastMQTT::GetInstance().Initialize(config_json);
 *   FastMQTT::GetInstance().RegisterCallback("/status", cb);
 *   FastMQTT::GetInstance().Start();
 *   FastMQTT::GetInstance().Publish("/heartbeat", payload);
 *   ...
 *   FastMQTT::GetInstance().Stop();
 * @endcode
 */
class FastMQTT {
public:
    // ------------------------------------------------------------------
    // 单例获取
    // ------------------------------------------------------------------
    /**
     * @brief 获取全局唯一实例。
     */
    static FastMQTT& GetInstance();

    // 禁止拷贝与移动，保证全局唯一。
    FastMQTT(const FastMQTT&) = delete;
    FastMQTT& operator=(const FastMQTT&) = delete;
    FastMQTT(FastMQTT&&) = delete;
    FastMQTT& operator=(FastMQTT&&) = delete;

    // ------------------------------------------------------------------
    // 生命周期接口
    // ------------------------------------------------------------------
    /**
     * @brief 初始化模块（只能成功调用一次）。
     * @param config JSON 配置，可为 mqtt 节点或包含 mqtt 键的顶层对象。
     * @return true 初始化成功；false 重复初始化或配置无效。
     */
    bool Initialize(const nlohmann::json& config);

    /**
     * @brief 启动模块（只能成功调用一次），创建并运行四个后台线程。
     * @return true 启动成功；false 未初始化 / 已启动 / 未启用。
     */
    bool Start();

    /**
     * @brief 停止模块，安全退出所有后台线程并断开连接。
     *        可重复调用，幂等安全。
     */
    void Stop();

    /**
     * @brief 销毁模块，释放资源，状态回到未初始化。
     *        内部会先调用 Stop()。
     */
    void Destroy();

    /** 
     * @brief 获取模块的当前状态（JSON）。 
     */
    nlohmann::json Status();

    // ------------------------------------------------------------------
    // 发布消息接口（业务线程永不直接调用 mosquitto_publish）
    // ------------------------------------------------------------------
    /**
     * @brief 发布消息（使用默认 QoS 与 retain）。
     * @return true 已成功进入发送队列；false 队列满 / 模块未就绪。
     */
    bool Publish(const std::string& topic, const std::string& payload);

    /**
     * @brief 发布消息（指定 QoS，使用默认 retain）。
     */
    bool Publish(const std::string& topic, const std::string& payload, int qos);

    /**
     * @brief 发布消息（指定 QoS 与 retain）。
     */
    bool Publish(const std::string& topic, const std::string& payload, int qos, bool retain);

    // ------------------------------------------------------------------
    // Topic 回调管理（一个 Topic 可注册多个回调）
    // ------------------------------------------------------------------
    /**
     * @brief 注册某个 Topic 过滤器的回调，并自动订阅该主题。
     * @param topic 主题过滤器，支持 MQTT 通配符 + 与 #。
     * @param callback 业务回调。
     * @param qos 订阅 QoS。
     * @return 回调句柄（>0）。可用于 UnregisterCallback；返回 0 表示失败。
     */
    std::uint64_t RegisterCallback(const std::string& topic,
                                   MessageCallback callback,
                                   int qos = 1);

    /**
     * @brief 按句柄注销某个回调。
     * @param handle RegisterCallback 返回的句柄。
     * @return true 找到并移除；false 未找到。
     */
    bool UnregisterCallback(std::uint64_t handle);

    /**
     * @brief 清空某个 Topic 上的全部回调。
     * @param topic 主题过滤器。
     */
    void ClearCallback(const std::string& topic);

    /**
     * @brief 清空所有 Topic 的全部回调。
     */
    void ClearAllCallbacks();

    // ------------------------------------------------------------------
    // 状态接口
    // ------------------------------------------------------------------
    /** @brief 是否真正就绪（可进行业务通信）。 */
    bool IsReady() const;
    /** @brief Broker 是否已连接（含 MQTT 会话已建立）。 */
    bool IsConnected() const;
    /** @brief 目标主机 IP 是否连通。 */
    bool IsIPAlive() const;
    /** @brief 是否启用 MQTT 功能。 */
    bool IsEnabled() const;

    /** @brief 获取统计信息（JSON）。 */
    nlohmann::json GetStatistics() const;
    /** @brief 获取健康状态（JSON），供 Heartbeat 模块直接读取。 */
    nlohmann::json GetHealthStatus() const;

    // ------------------------------------------------------------------
    // 队列接口
    // ------------------------------------------------------------------
    /** @brief 当前发送队列长度。 */
    std::size_t GetSendQueueSize() const;
    /** @brief 当前接收队列长度。 */
    std::size_t GetReceiveQueueSize() const;

private:
    FastMQTT();
    ~FastMQTT();

    // ------------------------------------------------------------------
    // 内部：mosquitto C 回调（静态转发到成员函数）
    // ------------------------------------------------------------------
    static void OnConnectTrampoline(struct mosquitto* m, void* obj, int rc);
    static void OnDisconnectTrampoline(struct mosquitto* m, void* obj, int rc);
    static void OnMessageTrampoline(struct mosquitto* m, void* obj, const struct mosquitto_message* msg);
    static void OnLogTrampoline(struct mosquitto* m, void* obj, int level, const char* str);

    void HandleConnect(int rc);
    void HandleDisconnect(int rc);
    void HandleMessage(const struct mosquitto_message* msg);

    // ------------------------------------------------------------------
    // 内部：四个后台线程主循环
    // ------------------------------------------------------------------
    void ConnectionManagerLoop();  ///< 线程一：IP 连通性监测与状态聚合。
    void ReceiverLoop();           ///< 线程二：MQTT 网络循环与自动重连。
    void DispatcherLoop();         ///< 线程三：消费接收队列并派发回调。
    void SenderLoop();             ///< 线程四：消费发送队列并执行发布。

    // ------------------------------------------------------------------
    // 内部：辅助函数
    // ------------------------------------------------------------------
    bool DoPublish(const Message& msg);                 ///< 实际执行 mosquitto_publish。
    void DispatchToCallbacks(const Message& msg);       ///< 匹配并执行回调。
    void ResubscribeAll();                              ///< 连接成功后重新订阅所有已注册主题。
    int  NextBackoffSeconds();                          ///< 计算下一次重连退避秒数。
    void ResetBackoff();                                ///< 重置退避。
    static bool TopicMatch(const std::string& filter, const std::string& topic);  ///< MQTT 主题匹配。
    static bool CheckTcpAlive(const std::string& host, int port, int timeout_ms); ///< TCP 连通性探测。
    static std::int64_t NowSeconds();                   ///< 当前 Unix 秒。

    void SetState(LifecycleState s);   ///< 线程安全地设置生命周期状态。

private:
    // 一个 Topic 上的一条回调记录。
    struct CallbackEntry {
        std::uint64_t   handle{0};  ///< 唯一句柄。
        std::string     filter;     ///< 主题过滤器。
        MessageCallback callback;   ///< 回调函数。
        int             qos{1};     ///< 订阅 QoS。
    };

    // ---- 配置与状态 ----
    FastMQTTConfig                 config_;                ///< 运行配置。
    std::atomic<LifecycleState>    state_{LifecycleState::Uninitialized};  ///< 生命周期状态。

    std::atomic<bool> ip_alive_{false};          ///< IP 连通状态。
    std::atomic<bool> broker_connected_{false};  ///< Broker 连接状态。
    std::atomic<bool> session_connected_{false}; ///< MQTT 会话状态。
    std::atomic<bool> ready_{false};             ///< 就绪状态（可业务通信）。
    std::atomic<bool> running_{false};           ///< 线程运行标志。

    // ---- mosquitto 句柄 ----
    struct mosquitto* mosq_{nullptr};   ///< mosquitto 客户端句柄。
    std::mutex        pub_mutex_;       ///< 保护 publish/subscribe 调用。

    // ---- 队列 ----
    std::unique_ptr<BlockingQueue<Message>> send_queue_;  ///< 发送队列。
    std::unique_ptr<BlockingQueue<Message>> recv_queue_;  ///< 接收队列。

    // ---- 回调表 ----
    mutable std::mutex                                     cb_mutex_;   ///< 保护回调表。
    std::map<std::string, std::vector<CallbackEntry>>      callbacks_;  ///< Topic -> 回调列表。
    std::atomic<std::uint64_t>                             next_handle_{1};  ///< 句柄自增。

    // ---- 后台线程 ----
    std::thread conn_thread_;   ///< ConnectionManager 线程。
    std::thread recv_thread_;   ///< Receiver 线程。
    std::thread disp_thread_;   ///< Dispatcher 线程。
    std::thread send_thread_;   ///< Sender 线程。

    // ---- 重连退避 ----
    std::mutex backoff_mutex_;  ///< 保护退避索引。
    std::size_t backoff_index_{0};  ///< 当前退避档位。

    // ---- 统计 ----
    Statistics stats_;  ///< 运行统计。

    std::mutex lifecycle_mutex_;  ///< 保护 Initialize/Start/Stop 的串行化。
};

}  // namespace fast_mqtt
