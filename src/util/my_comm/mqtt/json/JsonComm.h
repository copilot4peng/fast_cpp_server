#pragma once

// =============================================================================
// 文件：JsonComm.h
// 模块：my_comm / mqtt / json
// 说明：把 FastMQTT 订阅接入系统的 JSON 协议桥接模块。
//
// 设计定位（对照 mqtt/2536pb）：
//   - FastMQTT 只负责 MQTT 字节传输；
//   - 2536pb 负责 protobuf（CS-Y2536）协议接入；
//   - 本模块 JsonComm 负责“订阅主题 + 解析 JSON 报文 + 打印/分发”。
//
// 当前版本能力：
//   1. 订阅配置中的主题，收到消息后用 nlohmann::json 解析；
//   2. 打印统一格式的中文日志；
//   3. 提供 RegisterParsedCallback 扩展点，把解析后的 JSON 交给业务模块；
//   4. 不负责启动 FastMQTT，要求调用方提前把 FastMQTT 拉起来（IsReady）。
//
// 强制对外接口（由需求指定，必须存在）：
//   - static JsonComm& GetInstance();  单例入口
//   - bool          Init(...);         初始化
//   - bool          Start();           启动订阅
//   - nlohmann::json Status() const;   状态查询
// =============================================================================

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "FastMQTTTypes.hpp"

namespace json_comm {

/**
 * @brief JSON 通信桥接模块（单例）。
 *
 * 使用方式：
 *   1. 先确保 fast_mqtt::FastMQTT 已 Initialize + Start；
 *   2. 再调用 JsonComm::GetInstance().Init(...);
 *   3. 然后 Start()，模块会对配置中的主题执行 RegisterCallback；
 *   4. 收到 MQTT 字节后先 json::parse，再打印摘要并分发给业务回调。
 */
class JsonComm final {
public:
    /**
     * @brief 解析后的 JSON 消息回调类型。
     *
     * 该回调在本模块完成 JSON 解析之后被调用，业务方拿到的是已经解析好的
     * nlohmann::json，而不需要关心 MQTT 传输与原始字节。
     */
    using ParsedCallback = std::function<void(const std::string& topic,
                                              const nlohmann::json& msg)>;

    /// @brief 获取全局唯一实例（懒汉式单例，线程安全）。
    static JsonComm& GetInstance();

    JsonComm(const JsonComm&) = delete;
    JsonComm& operator=(const JsonComm&) = delete;
    JsonComm(JsonComm&&) = delete;
    JsonComm& operator=(JsonComm&&) = delete;

    // -------------------------------------------------------------------------
    // 一、生命周期接口（Init / Start / Stop）
    // -------------------------------------------------------------------------
    /**
     * @brief 初始化模块配置。
     *
     * 支持的配置格式：
     * @code
     * {
     *   "enable": true,
     *   "topics": ["/status", "/heartbeat"],
     *   "default_qos": 1
     * }
     * @endcode
     *
     * 若未提供 topics，则默认订阅 "/status"。
     *
     * @param config 配置 JSON，缺省为一个空对象（全部取默认值）。
     * @return true 初始化成功；false 重复初始化被拒绝。
     */
    bool Init(const nlohmann::json& config = nlohmann::json::object());

    /**
     * @brief 启动模块并对配置中的主题注册订阅回调。
     *
     * @return true 启动成功；false（未初始化 / 未启用 / FastMQTT 未就绪 / 重复启动）。
     */
    bool Start();

    /**
     * @brief 停止模块并注销所有已注册回调。
     */
    void Stop();

    /**
     * @brief 是否已启动。
     */
    bool IsRunning() const;

    // -------------------------------------------------------------------------
    // 二、业务扩展接口
    // -------------------------------------------------------------------------
    /**
     * @brief 注册一个“解析后的 JSON 回调”。
     *
     * 回调会在本模块完成 json::parse 之后被调用；同一个 topic 可注册多个回调。
     * 若模块已经 Start，且该 topic 尚未订阅，则会立即补订阅。
     *
     * @param topic    订阅主题。
     * @param callback 业务回调。
     * @param qos      订阅 QoS。
     * @return 回调句柄（>0）；返回 0 表示失败（空回调或未初始化）。
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
                                             int qos=1);

    /**
	 * @brief 为指定主题设置 json 回调。
	 *
	 * @return true 设置成功。
	 * @return false 设置失败。
	 */
	bool SettingJsonCallbackForTopic();
    
    /**
     * @brief 清空某个 topic 的全部回调（包括底层订阅）。
     */
    void ClearTopic(const std::string& topic);

    /**
     * @brief 清空所有回调并注销所有订阅。
     */
    void ClearAll();

    // -------------------------------------------------------------------------
    // 三、状态查询接口
    // -------------------------------------------------------------------------
    /**
     * @brief 获取当前模块状态摘要。
     *
     * @return 包含 initialized / running / enable / default_qos / topics 等字段的 JSON。
     */
    nlohmann::json Status() const;

private:
    JsonComm() = default;
    ~JsonComm();

    /// @brief 把配置里默认的示例主题追加进订阅列表（可按需扩展）。
    bool AppendDefaultTopics();

    /**
     * @brief 在持锁状态下订阅某个主题，并把本模块的分发回调注册到 FastMQTT。
     */
    void SubscribeTopicLocked(const std::string& topic, int qos);

    /**
     * @brief 在持锁状态下注销某个主题的订阅。
     */
    void UnsubscribeTopicLocked(const std::string& topic);

    /**
     * @brief FastMQTT 收到原始消息后的统一入口：解析 JSON、打印、分发。
     */
    void OnRawMessage(const std::string& topic_filter, const fast_mqtt::Message& msg);

    /**
     * @brief 打印一条已解析的 JSON 消息（统一中文日志格式）。
     */
    void PrintMessage(const std::string& topic_filter,
                      const fast_mqtt::Message& msg,
                      const nlohmann::json& msg_json) const;

private:
    // 单个 topic 的订阅条目：底层 FastMQTT 句柄 + 本模块维护的业务回调列表。
    struct TopicEntry {
        std::uint64_t handle{0};   ///< FastMQTT 回调句柄（0 表示尚未订阅）。
        std::string   topic;       ///< 主题名。
        int           qos{1};      ///< 订阅 QoS。
        struct CallbackItem {
            std::uint64_t  handle{0};   ///< 业务回调句柄。
            ParsedCallback callback;    ///< 业务回调。
        };
        std::vector<CallbackItem> callbacks;  ///< 该主题下的业务回调集合。
    };

    mutable std::mutex mutex_;                       ///< 保护以下所有可变状态。
    bool initialized_{false};                        ///< 是否已初始化。
    bool running_{false};                            ///< 是否已启动。
    bool enable_{true};                              ///< 是否启用本模块。
    int  default_qos_{1};                            ///< 默认订阅 QoS。
    std::string default_topic{"/json/default"};    ///< 默认订阅主题。
    std::vector<std::string> topics_{};              ///< 待订阅主题列表。
    std::map<std::string, TopicEntry> topic_entries_;///< topic -> 订阅条目。
    std::uint64_t next_handle_{1};                   ///< 业务回调句柄自增计数。
};

}  // namespace json_comm
