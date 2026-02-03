#pragma once
#include <string>

namespace my_mqtt {

/**
 * 业务层仅依赖这个接口（用于解耦）。
 * 注意：这是全新的接口，不复用旧 my_mqtt 模块的任何东西。
 */
class IMqttPublisher {
public:
    virtual ~IMqttPublisher() = default;

    // 发布 payload 到 topic，返回是否提交成功（不保证对端收到）
    virtual bool Publish(const std::string& topic,
                         const std::string& payload,
                         int qos = 0,
                         bool retain = false) = 0;
};

} // namespace my_new_mqtt