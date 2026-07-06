// =============================================================================
// 文件：JsonCallBackFuncs.cpp
// 模块：my_comm / mqtt / json
// 说明：JSON 消息回调集合（单例工厂）的实现。
// =============================================================================

#include "JsonCallBackFuncs.h"

#include <sstream>

#include "MyLog.h"

namespace json_comm {

namespace {
// 当 topic 为空时使用的通用日志标题。
const std::string kGenericTitle = "【JSON】收到消息";
}  // namespace

JsonCallBackFuncs& JsonCallBackFuncs::GetInstance() {
    // 函数内 static 局部变量，天然线程安全的懒汉单例。
    static JsonCallBackFuncs instance;
    return instance;
}

fast_mqtt::MessageCallback JsonCallBackFuncs::GetCallbackForTopic(const std::string& topic) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 命中缓存则直接复用，避免每次都重新构造回调闭包。
    auto it = cache_.find(topic);
    if (it != cache_.end()) {
        MYLOG_DEBUG("【JsonCallBackFuncs】命中缓存回调 topic={}", topic);
        return it->second;
    }

    // 未命中则新建一个回调并写入缓存。
    const std::string title = topic.empty() ? kGenericTitle : ("【JSON】topic=" + topic);
    auto callback = BuildCallback(topic, title);
    cache_.emplace(topic, callback);
    return callback;
}

fast_mqtt::MessageCallback JsonCallBackFuncs::BuildCallback(const std::string& topic,
                                                            const std::string& title) {
    MYLOG_INFO("【JsonCallBackFuncs】为 topic={} 构建 JSON 回调函数", topic);

    // 用值捕获 topic / title，保证回调脱离本函数栈后依然可用。
    return [topic, title](const fast_mqtt::Message& msg) {
        // 1. 把原始载荷解析为 JSON。解析失败不抛异常，只记录告警。
        nlohmann::json parsed_msg = nlohmann::json::parse(msg.payload, nullptr, false);
        if (parsed_msg.is_discarded()) {
            MYLOG_WARN("【JSON】消息解析失败 topic={} payload_size={}", msg.topic, msg.payload.size());
            MYLOG_WARN("【JSON】payload={}", msg.payload.c_str());
            return;
        }

        // 2. 打印统一格式的中文摘要日志。
        LogParsedMessage(title, msg, parsed_msg);

        // 3. 按 topic 走对应的处理分支（此处为示例，实际业务可自行扩展）。
        MYLOG_INFO("【JSON】处理消息 topic={}", msg.topic);
        if (!topic.empty() && topic == "test") {
            MYLOG_INFO("【JSON】执行 test 专属处理逻辑");
        } else if (!topic.empty() && topic == "/json/default") {
            MYLOG_INFO("【JSON】执行 /json/default 专属处理逻辑");
        } else {
            MYLOG_INFO("【JSON】执行通用处理逻辑");
        }
    };
}

std::string JsonCallBackFuncs::BuildSummary(const nlohmann::json& msg) {
    std::ostringstream oss;

    // 输出 JSON 顶层类型，便于快速判断报文形态。
    if (msg.is_object()) {
        oss << " type=object";
        oss << " key_count=" << msg.size();
        // 逐个列出顶层键名，方便在日志里定位字段。
        oss << " keys=[";
        bool first = true;
        for (auto it = msg.begin(); it != msg.end(); ++it) {
            if (!first) {
                oss << ",";
            }
            oss << it.key();
            first = false;
        }
        oss << "]";
    } else if (msg.is_array()) {
        oss << " type=array";
        oss << " size=" << msg.size();
    } else {
        oss << " type=scalar";
    }

    return oss.str();
}

void JsonCallBackFuncs::LogParsedMessage(const std::string& title,
                                         const fast_mqtt::Message& msg,
                                         const nlohmann::json& parsed_msg) {
    MYLOG_INFO("----------------------------------------------------------------");
    MYLOG_INFO("{}", title);
    MYLOG_INFO(" * filter/topic={}", msg.topic);
    MYLOG_INFO(" * summary={}", BuildSummary(parsed_msg));
    // dump(-1) 输出紧凑单行 JSON，避免多行刷屏。
    MYLOG_INFO(" * json={}", parsed_msg.dump());
    MYLOG_INFO("----------------------------------------------------------------");
}

}  // namespace json_comm
