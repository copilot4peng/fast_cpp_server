#pragma once
#include <string>
#include <functional>

namespace my_mqtt {

/**
 * IMqttPublisher - 抽象 MQTT 发布者接口
 * 业务层依赖该接口，便于替换实现（mosquitto / paho / mock）
 */
class IMqttPublisher {
public:
  virtual ~IMqttPublisher() = default;

  // 同步连接（或等待连接完成），返回是否连接成功
  virtual bool Connect() = 0;

  // 断开连接（并清理）
  virtual void Disconnect() = 0;

  // 发布 payload 到 topic，返回是否提交成功（不保证被接收）
  virtual bool Publish(const std::string& topic,
                       const std::string& payload,
                       int qos = 0,
                       bool retain = false) = 0;

  // 设置 LWT（will），在连接时生效
  virtual void SetLastWill(const std::string& topic,
                           const std::string& payload,
                           int qos = 1,
                           bool retain = true) = 0;
};

} // namespace my_mqtt