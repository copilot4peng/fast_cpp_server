#pragma once

#include <sstream>
#include <nlohmann/json.hpp>
#include <string>

#include "MyData.h"
#include "IDevice.h"
#include "TaskQueue.h"
#include "demo/Task.h"

namespace my_edge {

using namespace my_data;
using namespace my_control;
using namespace my_device;

using DeviceID_Type___Mapping = std::unordered_map<my_data::DeviceId, std::string>;
using DeviceID_Tasks__Mapping = std::unordered_map<my_data::DeviceId, std::unique_ptr<my_control::TaskQueue>>;
using DeviceID_Device_Mapping = std::unordered_map<my_data::DeviceId, std::unique_ptr<my_device::IDevice>>;

/**
 * @brief Submit 结果码：用于表达 Submit 的详细含义（替代 bool）
 */
enum class SubmitCode {
  Ok = 0,            ///< 入队成功（不代表执行成功）
  NotRunning = 1,    ///< Edge 未 Start（Ready 状态拒绝入队）
  EStop = 2,         ///< EStop 激活，默认拒绝入队（可配置允许入队）
  InvalidCommand = 3,///< 规范化失败（字段缺失/格式错误）
  UnknownDevice = 4, ///< 未注册 device_id
  QueueShutdown = 5, ///< 队列已 shutdown（通常是 shutdown 过程中）
  InternalError = 6  ///< 未知异常
};

/**
 * @brief Submit 结构化返回
 */
struct SubmitResult {
  SubmitCode code{SubmitCode::InternalError};
  std::string message{};

  my_data::EdgeId edge_id{};
  my_data::DeviceId device_id{};
  my_data::CommandId command_id{};
  my_data::TaskId task_id{};

  std::int64_t queue_size_after{0};

  bool ok() const { return code == SubmitCode::Ok; }

  std::string toString() const {
    std::ostringstream oss;
    oss << "SubmitResult{code=" << static_cast<int>(code)
        << ", message=" << message
        << ", edge_id=" << edge_id
        << ", device_id=" << device_id
        << ", command_id=" << command_id
        << ", task_id=" << task_id
        << ", queue_size_after=" << queue_size_after
        << "}";
    return oss.str();
  }
};

/**
 * @brief 将 SubmitCode 转为可读字符串（便于日志与 API 输出）
 */
inline std::string ToString(SubmitCode c) {
  switch (c) {
    case SubmitCode::Ok: return "Ok";
    case SubmitCode::NotRunning: return "NotRunning";
    case SubmitCode::EStop: return "EStop";
    case SubmitCode::InvalidCommand: return "InvalidCommand";
    case SubmitCode::UnknownDevice: return "UnknownDevice";
    case SubmitCode::QueueShutdown: return "QueueShutdown";
    case SubmitCode::InternalError: return "InternalError";
    default: return "UnknownSubmitCode";
  }
}

/**
 * @brief Edge 运行状态（用于区分 Init/Start 阶段，以及运行中状态）
 * 
 * - Initializing: 正在 Init，尚未准备好接受命令
 * - Ready: Init 完成，已准备好接受命令，但尚未 Start
 * - Running: 已 Start，正常运行中
 * - Stopping: 收到停止信号，正在停止中（拒绝新命令，等待设备/队列清理完成）
 * - Stopped: 已完全停止，所有设备/队列已清理完成
 * - RunOver: 运行结束（用于特殊场景，如单次运行后退出）
 */
enum class RunState { Initializing, Ready, Running, Stopping, Stopped, RunOver };


static std::string RunStateToString(RunState s) {
    switch (s) {
        case RunState::Initializing:  return "Initializing";
        case RunState::Ready:         return "Ready";
        case RunState::Running:       return "Running";
        case RunState::Stopping:      return "Stopping";
        case RunState::Stopped:       return "Stopped";
        case RunState::RunOver:       return "RunOver";
        default:                      return "UnknownRunState";
    }
}

/**
 * @brief Edge Runtime 接口（Init/Start 分离）
 */
class IEdge {
public:
  virtual ~IEdge() = default;

  /**
   * @brief 初始化：仅装配，不启动线程
   */
  virtual bool Init(const nlohmann::json& cfg, std::string* err) = 0;

  /**
   * @brief 启动：启动所有 device/workflow
   */
  virtual bool Start(std::string* err) = 0;

  /**
   * @brief Submit：命令入口（Normalize 并入队）
   */
  virtual SubmitResult Submit(const my_data::RawCommand& cmd) = 0;

  /**
   * @brief 获取状态快照（EdgeStatus，补 queue_depth/pending/running）
   */
  virtual my_data::EdgeStatus GetStatusSnapshot() const = 0;

  /**
   * @brief 设置紧急停止
   */
  virtual void SetEStop(bool active, const std::string& reason) = 0;

  /**
   * @brief 全局停机：Stop devices + Shutdown queues + Join
   */
  virtual void Shutdown() = 0;

  /**
   * @brief 获取 Edge ID
   * 
   * @return my_data::EdgeId 
   */
  virtual my_data::EdgeId Id() const = 0;

  /**
   * @brief 获取 Edge 类型
   * 
   * @return std::string 
   */
  virtual std::string EdgeType() const = 0;

  /**
   * @brief 解释 Init 的入参并打印到日志（用于调试/前置检查）
   * @param cfg Init 时接收的 JSON 配置（不会修改）
   */
  virtual void ShowAnalyzeInitArgs(const nlohmann::json& cfg) const = 0;

  // 获取类内元素,有几个队列，几个映射关系等，以及设备信息等（仅供调试/日志使用）
  virtual nlohmann::json DumpInternalInfo() const = 0;

  /**
     * @brief 向 Edge 添加任务。
     * @param task 要添加的任务的 JSON 表示。
     * @return 如果成功添加任务则返回 true，否则返回 false。
     */
  virtual bool AppendJsonTask(const nlohmann::json& task) = 0;

  /**
     * @brief 向 Edge 添加任务。
     * @param task 要添加的任务对象。
     * @return 如果成功添加任务则返回 true，否则返回 false。
     */
  virtual bool AppendTask(const my_data::Task& task) = 0;
};

} // namespace my_edge