#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "BaseEdge.h"
#include "MyControl.h"
#include "MyLog.h"

namespace my_edge::demo {

/**
 * @brief UAVEdge：无人机边缘运行时（demo）
 *
 * 设计意图：
 * - 继承 BaseEdge，复用通用的 Init/Start/Submit/Shutdown/self_action/snapshot 框架
 * - Normalize 策略：按需临时创建 normalizer（简单可靠）
 * - 支持外部下发 self 任务：device_id="self"，由 BaseEdge 入队并由 self_action 线程执行
 */
class UAVEdge final : public my_edge::BaseEdge {
public:
  UAVEdge();
  explicit UAVEdge(const nlohmann::json& cfg, std::string* err = nullptr);
  ~UAVEdge() override = default;

protected:
  /**
   * @brief Normalize：把 RawCommand 转成 Task
   *
   * BaseEdge 已完成：
   * - run_state / estop / payload / device_id 校验
   * - device_id -> type 查找
   *
   * 这里只需要根据 device_type 创建 normalizer 并生成 task。
   */
  std::optional<my_data::Task> NormalizeCommandLocked(const my_data::RawCommand& cmd,
                                                      const my_data::DeviceId& device_id,
                                                      const std::string& device_type,
                                                      std::string* err) override;

  /**
   * @brief 执行 self task（可扩展）
   *
   * 你可以在这里实现 edge 自身能力，例如：
   * - capability="edge", action="estop"：触发紧急停止
   * - capability="edge", action="clear_estop"：解除紧急停止
   * - capability="edge", action="heartbeat_now"：触发一次立即心跳上报
   */
  void ExecuteSelfTaskLocked() override;
  void ExecuteOtherTaskLocked(const my_data::Task& task) override;
};

} // namespace my_edge::demo