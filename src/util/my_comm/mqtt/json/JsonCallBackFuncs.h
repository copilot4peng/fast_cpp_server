#pragma once

// =============================================================================
// 文件：JsonCallBackFuncs.h
// 模块：my_comm / mqtt / json
// 说明：JSON 消息回调集合（单例工厂）。
//
// 设计目标：
//   - 对外提供“按 topic 获取 fast_mqtt::MessageCallback”的能力；
//   - 内部封装 nlohmann::json 的解析与中文日志打印；
//   - 后续可以在这里为不同 topic 扩展各自的专属处理逻辑，
//     而完全不需要改动底层 FastMQTT 传输层。
//
// 与 2536pb（protobuf）的差异：
//   - 2536pb 的载荷是 protobuf 序列化字节，需要 ParseFromString；
//   - JSON 的载荷是 UTF-8 文本，直接用 nlohmann::json::parse 解析即可，
//     因此本模块不依赖任何 protobuf 头文件。
// =============================================================================

#include <map>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "FastMQTTTypes.hpp"

namespace json_comm {

/**
 * @brief JSON 回调函数集合单例。
 *
 * 该类负责把不同 topic 对应的 fast_mqtt::MessageCallback 生产出来。
 * 当前版本所有回调都会：
 *   1. 把 MQTT 原始载荷解析为 nlohmann::json；
 *   2. 打印中文摘要日志；
 *   3. 按 topic 走对应的专属 / 通用处理分支。
 */
class JsonCallBackFuncs final {
public:
    /// @brief 获取全局唯一实例（懒汉式单例，线程安全）。
    static JsonCallBackFuncs& GetInstance();

    JsonCallBackFuncs(const JsonCallBackFuncs&) = delete;
    JsonCallBackFuncs& operator=(const JsonCallBackFuncs&) = delete;
    JsonCallBackFuncs(JsonCallBackFuncs&&) = delete;
    JsonCallBackFuncs& operator=(JsonCallBackFuncs&&) = delete;

    /**
     * @brief 根据 topic 获取一个 MessageCallback。
     *
     * 若该 topic 已经生产过回调，则直接复用缓存；否则新建一个并缓存。
     *
     * @param topic 订阅主题。
     * @return 可直接注册进 FastMQTT 的回调函数。
     */
    fast_mqtt::MessageCallback GetCallbackForTopic(const std::string& topic);

    /**
     * @brief 把一段 JSON 对象转成简短的中文摘要，便于日志打印。
     *
     * @param msg 已解析的 JSON。
     * @return 摘要字符串（键数量、类型、简短内容）。
     */
    static std::string BuildSummary(const nlohmann::json& msg);

private:
    JsonCallBackFuncs() = default;

    /**
     * @brief 真正构造某个 topic 的回调闭包。
     *
     * @param topic 订阅主题。
     * @param title 日志标题，便于区分不同 topic。
     */
    fast_mqtt::MessageCallback BuildCallback(const std::string& topic,
                                             const std::string& title);

    /**
     * @brief 打印一条已解析的 JSON 消息（统一日志格式）。
     */
    static void LogParsedMessage(const std::string& title,
                                 const fast_mqtt::Message& msg,
                                 const nlohmann::json& parsed_msg);

private:
    std::mutex mutex_;                                            ///< 保护 cache_。
    std::map<std::string, fast_mqtt::MessageCallback> cache_;     ///< topic -> 回调缓存。
};

}  // namespace json_comm
