#include "CSY2536CallBackFuncs.h"

#include <sstream>

#include "MyLog.h"

namespace csy2536 {

namespace {
const std::string kGenericTitle = "【CSY2536】收到消息";
}  // namespace

CSY2536CallBackFuncs& CSY2536CallBackFuncs::GetInstance() {
    static CSY2536CallBackFuncs instance;
    return instance;
}

fast_mqtt::MessageCallback CSY2536CallBackFuncs::GetCallbackForTopic(const std::string& topic) {
    std::lock_guard<std::mutex> lock(mutex_);
    // auto it = cache_.find(topic);
    // if (it != cache_.end()) {
    //     return it->second;
    // }

    const std::string title = topic.empty() ? kGenericTitle : ("【CSY2536】topic=" + topic);
    auto callback = BuildCallback(topic, title);
    cache_.emplace(topic, callback);
    return callback;
}

fast_mqtt::MessageCallback CSY2536CallBackFuncs::BuildCallback(const std::string& topic,
                                                               const std::string& title) {
    MYLOG_INFO("【CSY2536CallBackFuncs】为 topic={} 构建回调函数", topic);
    return [topic, title](const fast_mqtt::Message& msg) {
        CSY2536::MsgInfo parsed_msg;
        if (!parsed_msg.ParseFromString(msg.payload)) {
            MYLOG_WARN("【CSY2536】消息解析失败 topic={} payload_size={}", msg.topic, msg.payload.size());
            return;
        }

        // LogParsedMessage(title, msg, parsed_msg);

        MYLOG_INFO("【CSY2536】处理消息 topic={}", msg.topic);
        if (!topic.empty() && topic == "yingji/situation_ui") {
            MYLOG_INFO("【CSY2536】执行 yingji/situation_ui 专属处理逻辑");
        } else if (!topic.empty() && topic == "test") {
            MYLOG_INFO("【CSY2536】执行 test 专属处理逻辑");
        } else {
            MYLOG_INFO("【CSY2536】执行通用处理逻辑");
        }
    };
}

std::string CSY2536CallBackFuncs::BuildSummary(const CSY2536::MsgInfo& msg) {
    std::ostringstream oss;
    oss << " > send_id=" << msg.send_id() << "\n";
    oss << " > seq=" << msg.seq() << "\n";
    oss << " > receive_id_count=" << msg.receive_id_size() << "\n";
    oss << " > receive_name_count=" << msg.receive_name_size() << "\n";
    if (msg.has_ack()) {
        oss << " > ack=" << msg.ack() << "\n";
    }
    if (msg.has_session_hash()) {
        oss << " > session_hash=" << msg.session_hash() << "\n";
    }

    const auto* descriptor = msg.GetDescriptor();
    const auto* reflection = msg.GetReflection();
    if (descriptor != nullptr && reflection != nullptr && descriptor->oneof_decl_count() > 0) {
        const auto* oneof = descriptor->oneof_decl(0);
        const auto* field = reflection->GetOneofFieldDescriptor(msg, oneof);
        if (field != nullptr) {
            oss << " > type=" << field->name() << "\n";
        }
    }

    oss << " > short_debug=" << msg.ShortDebugString();
    return oss.str();
}

void CSY2536CallBackFuncs::LogParsedMessage(const std::string& title,
                                            const fast_mqtt::Message& msg,
                                            const CSY2536::MsgInfo& parsed_msg) {
    MYLOG_INFO("----------------------------------------------------------------");
    MYLOG_INFO("{}", title);
    MYLOG_INFO(" * filter/topic={}", msg.topic);
    MYLOG_INFO(" * summary={}", BuildSummary(parsed_msg));
    MYLOG_INFO("----------------------------------------------------------------");
}

}  // namespace csy2536