#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "IEdge.h"
#include "MyData.h"
#include "MyLog.h"
#include "IDevice.h"
#include "TaskQueue.h"

namespace my_edge {

/**
 * @brief BaseEdge：实现 IEdge 的通用骨架
 *
 * 设计目标：
 * 1) Edge 自身具备执行能力：内置 self 队列 + self_action 线程
 * 2) device 是可选的：cfg.devices 可以为空/缺失
 * 3) 心跳/上报线程独立：snapshot_thread_ 负责心跳上报（也可扩展为状态上报）
 * 4) Submit/Init/Start/Shutdown 的并发与状态机统一
 *
 * 子类只需要关注差异点：
 * - Normalize 策略（缓存/临时创建 normalizer 等）
 * - self task 执行策略（覆盖 ExecuteSelfTaskLocked）
 * - edge_type / 默认 edge_id / 扩展状态字段等
 */
class BaseEdge : public IEdge {
public:
  BaseEdge();
  explicit BaseEdge(std::string edge_type);
  ~BaseEdge() override;

  // IEdge 实现
  bool Init(const nlohmann::json& cfg, std::string* err) override;          // 负责解析 cfg 和初始化成员变量，但不启动设备线程
  bool Start(std::string* err) override;                                    // 负责启动设备线程、self action 线程和 snapshot 线程，切换 run_state 到 Running
  SubmitResult Submit(const my_data::RawCommand& cmd) override;             // 负责 run_state/estop/device_id 校验，调用 NormalizeCommandLocked 获取 Task，并分发到对应队列
  my_data::EdgeStatus GetStatusSnapshot() const override;                   // 负责收集基本状态信息（run_state、estop、队列长度等），并调用 ReportHeartbeatLocked 上报
  void SetEStop(bool active, const std::string& reason) override;           // 负责设置 estop 状态和原因，并在日志中记录
  void Shutdown() override;                                                 // 负责停止设备线程、self action 线程和 snapshot 线程，清理资源，切换 run_state 到 Stopped
  my_data::EdgeId Id() const override;                                      // 负责返回 edge_id        
  std::string EdgeType() const override;                                    // 负责返回 edge_type
  void ShowAnalyzeInitArgs(const nlohmann::json& cfg) const override;       // 负责解析 Init 入参并记录到日志
  nlohmann::json DumpInternalInfo() const override;                         // 负责输出内部状态信息（受 rw_mutex_ 保护）
  bool AppendJsonTask(const nlohmann::json& task) override;                 // 负责把 JSON 任务转换成 Task 并调用 AppendTask
  bool AppendTask(const my_data::Task& task) override;                      // 负责把 Task 分发到对应队列（受 rw_mutex_ 保护）  

protected:
  // -------- 子类需要实现/可覆盖的钩子 --------

  /**
   * @brief Normalize：把 RawCommand 转成 Task。
   * 注意：BaseEdge 已做 run_state/estop/payload/device_id 校验，这里只负责“生成 task”。
   *
   * @return 失败返回 nullopt，并在 err 中写入原因。
   */
  virtual std::optional<my_data::Task> NormalizeCommandLocked(const my_data::RawCommand& cmd,
                                                              const my_data::DeviceId& device_id,
                                                              const std::string& device_type,
                                                              std::string* err) = 0;

  /**
   * @brief self task 执行入口（可由子类扩展）
   * 默认实现会打印日志，并根据 capability/action 做少量内置动作。
   */
  virtual void ExecuteSelfTaskLocked(const my_data::Task& task);

  /**
   * @brief 心跳上报（snapshot thread 周期调用）
   * 默认实现：只打日志。后续你可以接入 MQTT/HTTP 等上报。
   */
  virtual void ReportHeartbeatLocked();

protected:
  // -------- 受 rw_mutex_ 保护的成员 --------
  mutable std::shared_mutex rw_mutex_;

  // 基本标识与配置
  my_data::EdgeId edge_id_{"edge-default"};
  std::string edge_type_{"base"};
  std::string version_{"0.0"};
  nlohmann::json cfg_;
  std::int64_t boot_at_ms_{0};

  // 运行状态
  std::atomic<RunState> run_state_{RunState::Initializing};

  // EStop
  std::atomic<bool> estop_{false};
  mutable std::mutex estop_mu_;
  std::string estop_reason_{};
  bool allow_queue_when_estop_{false};

  // device_id -> type（包含 self）
  std::unordered_map<my_data::DeviceId, std::string> device_type_by_id_;

  // device_id -> queue/device
  std::unordered_map<my_data::DeviceId, std::unique_ptr<my_control::TaskQueue>> queues_;
  std::unordered_map<my_data::DeviceId, std::unique_ptr<my_device::IDevice>> devices_;

  // -------- self action thread config/state --------
  bool self_action_enable_{true};                // 默认启用
  std::string self_device_id_{"self"};          // edge 自己的 device_id
  std::atomic<bool> self_action_stop_{false};
  std::thread self_action_thread_;

  // -------- heartbeat/snapshot thread config/state --------
  bool snapshot_enable_{true};                   // 默认启用
  int snapshot_interval_ms_{2000};               // 默认 2s 心跳
  std::atomic<bool> snapshot_stop_{false};
  std::thread snapshot_thread_;

protected:
  // -------- 线程相关（锁内调用） --------
  void StartSelfActionThreadLocked();
  void StopSelfActionThreadLocked();
  void SelfActionLoop();

  void StartSnapshotThreadLocked();
  void StopSnapshotThreadLocked();
  void SnapshotLoop();

  // -------- 通用工具函数 --------
  SubmitResult MakeResult(SubmitCode code,
                          const std::string& msg,
                          const my_data::RawCommand& cmd,
                          const my_data::DeviceId& device_id = "",
                          const my_data::TaskId& task_id = 0,
                          std::int64_t queue_size_after = 0) const;

  bool AppendTaskToQueueLocked(const my_data::DeviceId& device_id, const my_data::Task& task, std::string* err);
};

} // namespace my_edge