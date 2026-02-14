#pragma once
#include "TaskResult.h"
#include "Types.h"
#include <nlohmann/json.hpp>
#include <string>

namespace my_data {

/**
 * @brief Task 状态（MVP）
 */
enum class TaskState {
  Pending = 0,
  Running = 1,
  Succeeded = 2,
  Failed = 3,
  Cancelled = 4,
};

/**
 * @brief Task（内部统一任务）
 *
 * @details
 * - 数据类中包含“幂等字段”，但 MVP 阶段不启用去重逻辑。
 * - params/output/policy 统一使用 nlohmann::json，初始化/扩展方便。
 */
struct Task {
  // 身份：全局唯一的任务标识与来源命令 ID
  // - `task_id`：任务的唯一标识（内部使用，可由外部或系统生成）
  // - `command_id`：原始命令的 ID（用于追溯命令与任务的映射）
  TaskId task_id{};
  CommandId command_id{};

  // 追踪信息（可选，用于分布式追踪/链路分析）
  // - `trace_id`：分布式追踪的跟踪 ID
  // - `span_id`：当前任务在 trace 中的 span 标识
  std::string trace_id{};
  std::string span_id{};

  // 路由信息：任务应被投递到哪个 Edge/Device
  // - `edge_id`：目标边缘节点 ID（通常由客户端指定或路由器填充）
  // - `device_id`：目标设备 ID（任务最终执行的设备标识）
  EdgeId edge_id{};
  DeviceId device_id{};

  // 能力与动作：描述要执行的功能和操作
  // - `capability`：能力域或模块名称（如 camera、motion）
  // - `action`：具体动作名称（如 capture、move_to）
  // - `params`：动作参数，以 JSON 表示，方便扩展任意结构
  std::string capability{};
  std::string action{};
  nlohmann::json params = nlohmann::json::object();

  // 幂等与去重（预留字段，MVP 阶段未启用去重逻辑）
  // - `idempotency_key`：用于幂等判定的 key（来自客户端或网关）
  // - `dedup_window_ms`：去重时间窗口（毫秒），表示在该窗口内基于 key 判定重复
  std::string idempotency_key{};
  std::int64_t dedup_window_ms{0};

  // 调度相关（预留）：用于优先级、创建时间、截止时间与调度策略
  // - `priority`：任务优先级，数值越大优先级越高（实现可自定义）
  // - `created_at_ms`：任务创建时间（毫秒时间戳）
  // - `deadline_at_ms`：任务截止/超时时间（毫秒时间戳），超过则可取消或降级
  // - `policy`：调度/重试等策略的 JSON 描述，便于扩展
  /**
   * @brief 任务优先级，数值越大优先级越高
   * 默认值为 0，表示普通优先级。
   * 1: 高优先级任务，执行后即清空 self_task 成员变量，避免重复执行（一次性任务）。
   * 0: 普通优先级任务，执行后保留 self_task 成员
   */
  int priority{0};
  TimestampMs created_at_ms{0};
  TimestampMs deadline_at_ms{0};
  nlohmann::json policy = nlohmann::json::object();

  // 运行时状态与结果
  // - `state`：任务当前状态（Pending/Running/Succeeded/Failed/Cancelled）
  // - `result`：任务执行结果/输出，使用 `TaskResult` 结构体统一封装
  TaskState state{TaskState::Pending};
  TaskResult result{};

  std::string toString() const;
  nlohmann::json toJson() const;
  static Task fromJson(const nlohmann::json& j);
};

std::string ToString(TaskState s);

} // namespace my_data