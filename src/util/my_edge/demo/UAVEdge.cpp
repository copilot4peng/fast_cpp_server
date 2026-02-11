#include "UAVEdge.h"

#include "JsonUtil.h"
#include "MyData.h"

namespace my_edge::demo {

using namespace my_data;

UAVEdge::UAVEdge() : my_edge::BaseEdge("uav") {
  // BaseEdge 构造里会打印日志
  MYLOG_INFO("[UAVEdge] 构造完成");
}

UAVEdge::UAVEdge(const nlohmann::json& cfg, std::string* err) : UAVEdge() {
  this->ShowAnalyzeInitArgs(cfg);
  if (!Init(cfg, err)) {
    MYLOG_ERROR("[UAVEdge] 构造失败：Init 失败");
  } else {
    MYLOG_INFO("[UAVEdge] 构造成功");
  }
}

std::optional<my_data::Task> UAVEdge::NormalizeCommandLocked(const my_data::RawCommand& cmd,
                                                             const my_data::DeviceId& device_id,
                                                             const std::string& device_type,
                                                             std::string* err) {
  // 约定：self 也走 normalizer，这样外部下发 self task 的格式可以统一
  // device_type 在 BaseEdge 中对 self 固定写为 "self"
  // 你可以在 MyControl::CreateNormalizer("self") 里专门做一个 SelfNormalizer
  auto normalizer = my_control::MyControl::GetInstance().CreateNormalizer(device_type);
  if (!normalizer) {
    std::string e = "创建 normalizer 失败：type=" + device_type;
    MYLOG_ERROR("[Edge:{}] {}", edge_id_, e);
    if (err) *err = e;
    return std::nullopt;
  }

  std::string nerr;
  auto maybe_task = normalizer->Normalize(cmd, edge_id_, &nerr);
  if (!maybe_task.has_value()) {
    std::string e = nerr.empty() ? "Normalize 失败" : ("Normalize 失败：" + nerr);
    MYLOG_ERROR("[Edge:{}] device_id={}, type={}，{}", edge_id_, device_id, device_type, e);
    if (err) *err = e;
    return std::nullopt;
  }

  my_data::Task t = *maybe_task;

  // 防御式校验：确保 task.device_id 与路由一致（避免 normalizer 输出脏数据）
  if (t.device_id.empty()) {
    t.device_id = device_id;
  }
  if (t.device_id != device_id) {
    std::string e = "Normalize 输出的 task.device_id 与请求 device_id 不一致："
                    "task.device_id=" + t.device_id + ", req.device_id=" + device_id;
    MYLOG_ERROR("[Edge:{}] {}", edge_id_, e);
    if (err) *err = e;
    return std::nullopt;
  }

  MYLOG_INFO("[Edge:{}] Normalize 成功：device_id={}, type={}, task_id={}",
             edge_id_, device_id, device_type, t.task_id);
  return t;
}

void UAVEdge::ExecuteOtherTaskLocked(const my_data::Task& task) {
  // 先记录一条总日志，便于排查
  MYLOG_INFO("[Edge:{}] UAVEdge 执行 self task：task_id={}, capability={}, action={}, params={}",
             edge_id_, task.task_id, task.capability, task.action, task.params.dump());

  // 示例：实现一些内置 self task

  // 未识别任务：交给 BaseEdge 默认实现（保持兼容）
  my_edge::BaseEdge::ExecuteOtherTaskLocked(task);
}

void UAVEdge::ExecuteSelfTaskLocked() {
  // 先记录一条总日志，便于排查
  MYLOG_INFO("[Edge:{}] UAVEdge 执行 self task：task_id={}, capability={}, action={}, params={}",
             edge_id_, self_task.task_id, self_task.capability, self_task.action, self_task.params.dump());

  // 示例：实现一些内置 self task

  // 未识别任务：交给 BaseEdge 默认实现（保持兼容）
  my_edge::BaseEdge::ExecuteSelfTaskLocked();
}

} // namespace my_edge::demo