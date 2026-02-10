#include "BaseEdge.h"

#include <chrono>

#include "JsonUtil.h"
#include "MyDevice.h"

namespace my_edge {

using namespace my_data;
using namespace my_device;

BaseEdge::BaseEdge() {
  MYLOG_INFO("[BaseEdge] 构造完成，默认 edge_type={}", edge_type_);
}

BaseEdge::BaseEdge(std::string edge_type) : edge_type_(std::move(edge_type)) {
  MYLOG_INFO("[BaseEdge] 构造完成，edge_type={}", edge_type_);
}

BaseEdge::~BaseEdge() {
  Shutdown();
  MYLOG_INFO("[Edge:{}] BaseEdge 析构完成", edge_id_);
}

bool BaseEdge::Init(const nlohmann::json& cfg, std::string* err) {
  std::unique_lock<std::shared_mutex> lk(rw_mutex_);
  run_state_ = RunState::Initializing;

  cfg_ = cfg;
  edge_id_ = cfg.value("edge_id", edge_id_);
  version_ = cfg.value("version", version_);
  edge_type_ = cfg.value("edge_type", edge_type_);
  allow_queue_when_estop_ = cfg.value("allow_queue_when_estop", false);

  // self/snapshot 配置
  self_action_enable_ = cfg.value("self_action_enable", self_action_enable_);
  self_device_id_ = cfg.value("self_device_id", self_device_id_);
  snapshot_enable_ = cfg.value("snapshot_enable", snapshot_enable_);
  snapshot_interval_ms_ = cfg.value("snapshot_interval_ms", snapshot_interval_ms_);

  boot_at_ms_ = my_data::NowMs();

  // 清理旧资源（支持重复 Init）
  devices_.clear();
  queues_.clear();
  device_type_by_id_.clear();

  MYLOG_INFO("[Edge:{}] Init 开始：edge_type={}, version={}, self_action_enable={}, snapshot_enable={}, interval_ms={}",
             edge_id_, edge_type_, self_action_enable_ ? "true" : "false",
             snapshot_enable_ ? "true" : "false", snapshot_interval_ms_);

  // 1) 永远创建 self 队列（Edge ��身能力）
  {
    const std::string qname = "queue-" + self_device_id_;
    queues_[self_device_id_] = std::make_unique<my_control::TaskQueue>(qname);
    device_type_by_id_[self_device_id_] = "self";
    MYLOG_INFO("[Edge:{}] 创建 self 队列成功：device_id={}, queue={}", edge_id_, self_device_id_, qname);
  }

  // 2) devices 是可选的
  if (cfg.contains("devices") && cfg["devices"].is_array()) {
    int idx = 0;
    for (const auto& dcfg : cfg["devices"]) {
      idx++;
      try {
        std::string device_id = dcfg.value("device_id", "");
        std::string type = dcfg.value("type", "");
        if (device_id.empty() || type.empty()) {
          std::string e = "devices[" + std::to_string(idx) + "] 缺少 device_id/type";
          MYLOG_ERROR("[Edge:{}] Init 失败：{}，item={}", edge_id_, e, dcfg.dump());
          if (err) *err = e;
          continue; // 不直接 return false，让 Edge 仍能以 self 模式运行
        }

        device_type_by_id_[device_id] = type;

        std::string qname = "queue-" + device_id;
        queues_[device_id] = std::make_unique<my_control::TaskQueue>(qname);
        MYLOG_INFO("[Edge:{}] 创建队列：device_id={}, type={}, queue={}", edge_id_, device_id, type, qname);

        auto dev = my_device::MyDevice::GetInstance().Create(type, dcfg, err);
        if (!dev) {
          std::string e = "创建设备失败：device_id=" + device_id + ", type=" + type;
          MYLOG_ERROR("[Edge:{}] {}", edge_id_, e);
          if (err) *err = e;
          continue;
        }
        devices_[device_id] = std::move(dev);
        MYLOG_INFO("[Edge:{}] 创建设备成功：device_id={}, type={}", edge_id_, device_id, type);
      } catch (const std::exception& e) {
        std::string emsg = std::string("Init 捕获异常：") + e.what();
        MYLOG_ERROR("[Edge:{}] {}", edge_id_, emsg);
        if (err) *err = emsg;
        continue;
      }
    }
  } else {
    MYLOG_WARN("[Edge:{}] 未配置 devices，Edge 将以 self 模式运行（仅 self_action + snapshot）", edge_id_);
  }

  run_state_ = RunState::Ready;
  MYLOG_INFO("[Edge:{}] Init 完成：run_state=Ready，devices={}, queues={}",
             edge_id_, devices_.size(), queues_.size());
  return true;
}

bool BaseEdge::Start(std::string* err) {
  std::unique_lock<std::shared_mutex> lk(rw_mutex_);

  if (run_state_.load() != RunState::Ready) {
    std::string e = "Start 被拒绝：当前 run_state=" + RunStateToString(run_state_.load());
    MYLOG_WARN("[Edge:{}] {}", edge_id_, e);
    if (err) *err = e;
    return false;
  }

  MYLOG_INFO("[Edge:{}] Start 开始：devices={}", edge_id_, devices_.size());

  // 1) 启动所有 device（如果有）
  for (auto& [device_id, dev] : devices_) {
    auto qit = queues_.find(device_id);
    if (qit == queues_.end() || !qit->second) {
      std::string e = "Start 失败：找不到 device_id=" + device_id + " 对应的队列";
      MYLOG_ERROR("[Edge:{}] {}", edge_id_, e);
      if (err) *err = e;
      return false;
    }

    std::string dev_err;
    if (!dev->Start(*qit->second, &estop_, &dev_err)) {
      std::string e = "device.Start 失败：device_id=" + device_id + ", err=" + dev_err;
      MYLOG_ERROR("[Edge:{}] {}", edge_id_, e);
      if (err) *err = e;
      return false;
    }

    MYLOG_INFO("[Edge:{}] device.Start 成功：device_id={}, queue={}", edge_id_, device_id, qit->second->Name());
  }

  // 2) 启动 self action thread
  StartSelfActionThreadLocked();

  // 3) 启动心跳/上报线程
  StartSnapshotThreadLocked();

  run_state_ = RunState::Running;
  MYLOG_INFO("[Edge:{}] Start 成功：run_state=Running", edge_id_);
  return true;
}

SubmitResult BaseEdge::Submit(const my_data::RawCommand& cmd) {
  std::shared_lock<std::shared_mutex> lk(rw_mutex_);

  MYLOG_INFO("[Edge:{}] Submit：command_id={}, source={}, payload={}",
             edge_id_, cmd.command_id, cmd.source, cmd.payload.dump());

  // 1) run_state
  if (run_state_.load() != RunState::Running) {
    return MakeResult(SubmitCode::NotRunning,
                      "edge 未运行，run_state=" + RunStateToString(run_state_.load()),
                      cmd);
  }

  // 2) estop
  if (estop_.load() && !allow_queue_when_estop_) {
    std::string reason;
    {
      std::lock_guard<std::mutex> lk2(estop_mu_);
      reason = estop_reason_;
    }
    return MakeResult(SubmitCode::EStop,
                      reason.empty() ? "estop 已激活" : ("estop 已激活：" + reason),
                      cmd);
  }

  // 3) payload 校验
  if (!cmd.payload.is_object()) {
    return MakeResult(SubmitCode::InvalidCommand, "payload 必须是对象", cmd);
  }

  // 4) device_id
  std::string device_id = my_data::jsonutil::GetStringOr(cmd.payload, "device_id", "");
  if (device_id.empty()) {
    return MakeResult(SubmitCode::InvalidCommand, "缺少 payload.device_id", cmd);
  }

  // 5) type
  auto dit = device_type_by_id_.find(device_id);
  if (dit == device_type_by_id_.end()) {
    return MakeResult(SubmitCode::UnknownDevice, "未知 device_id=" + device_id, cmd, device_id);
  }
  const std::string& type = dit->second;

  // 6) normalize（由子类决定策略）
  std::string nerr;
  auto maybe_task = NormalizeCommandLocked(cmd, device_id, type, &nerr);
  if (!maybe_task.has_value()) {
    return MakeResult(SubmitCode::InvalidCommand,
                      nerr.empty() ? "Normalize 失败" : ("Normalize 失败：" + nerr),
                      cmd, device_id);
  }
  my_data::Task task = *maybe_task;

  // 7) push queue
  std::string qerr;
  if (!AppendTaskToQueueLocked(device_id, task, &qerr)) {
    return MakeResult(SubmitCode::InternalError,
                      qerr.empty() ? "入队失败" : ("入队失败：" + qerr),
                      cmd, device_id, task.task_id);
  }

  auto qit = queues_.find(device_id);
  std::int64_t qsize = (qit != queues_.end() && qit->second)
                         ? static_cast<std::int64_t>(qit->second->Size())
                         : 0;

  return MakeResult(SubmitCode::Ok, "已入队", cmd, device_id, task.task_id, qsize);
}

my_data::EdgeStatus BaseEdge::GetStatusSnapshot() const {
  std::shared_lock<std::shared_mutex> lk(rw_mutex_);

  my_data::EdgeStatus s;
  s.edge_id = edge_id_;
  s.boot_at_ms = boot_at_ms_;
  s.version = version_;

  RunState rs = run_state_.load();
  if (rs == RunState::Initializing || rs == RunState::Ready) {
    s.run_state = my_data::EdgeRunState::Initializing;
  } else if (rs == RunState::Running) {
    s.run_state = estop_.load() ? my_data::EdgeRunState::EStop : my_data::EdgeRunState::Running;
  } else {
    s.run_state = my_data::EdgeRunState::Degraded;
  }

  s.estop_active = estop_.load();
  {
    std::lock_guard<std::mutex> lk2(estop_mu_);
    s.estop_reason = estop_reason_;
  }

  std::int64_t pending_total = 0;
  std::int64_t running_total = 0;

  for (const auto& [device_id, dev] : devices_) {
    if (!dev) continue;

    my_data::DeviceStatus ds = dev->GetStatusSnapshot();

    auto qit = queues_.find(device_id);
    if (qit != queues_.end() && qit->second) {
      ds.queue_depth = static_cast<std::int64_t>(qit->second->Size());
      pending_total += ds.queue_depth;
    }
    if (ds.work_state == my_data::DeviceWorkState::Busy) {
      running_total += 1;
    }
    s.devices[device_id] = ds;
  }

  // 也把 self 的队列深度统计进去（self 没有 DeviceStatus，可放到 tasks_pending_total）
  auto self_qit = queues_.find(self_device_id_);
  if (self_qit != queues_.end() && self_qit->second) {
    pending_total += static_cast<std::int64_t>(self_qit->second->Size());
  }

  s.tasks_pending_total = pending_total;
  s.tasks_running_total = running_total;
  return s;
}

void BaseEdge::SetEStop(bool active, const std::string& reason) {
  {
    std::unique_lock<std::shared_mutex> lk(rw_mutex_);
    estop_.store(active);
    {
      std::lock_guard<std::mutex> lk2(estop_mu_);
      estop_reason_ = reason;
    }
  }
  MYLOG_WARN("[Edge:{}] SetEStop：active={}, reason={}",
             edge_id_, active ? "true" : "false", reason);
}

void BaseEdge::Shutdown() {
  std::unique_lock<std::shared_mutex> lk(rw_mutex_);

  RunState rs = run_state_.load();
  if (rs == RunState::Stopped || rs == RunState::Stopping) return;

  MYLOG_WARN("[Edge:{}] Shutdown 开始：run_state={}", edge_id_, RunStateToString(rs));
  run_state_ = RunState::Stopping;

  // 先停线程（避免线程访问被清理的 queues_/devices_）
  StopSnapshotThreadLocked();
  StopSelfActionThreadLocked();

  // stop devices
  for (auto& [device_id, dev] : devices_) {
    if (!dev) continue;
    MYLOG_WARN("[Edge:{}] Shutdown：device.Stop device_id={}", edge_id_, device_id);
    dev->Stop();
  }

  // shutdown queues（包含 self）
  for (auto& [device_id, q] : queues_) {
    if (!q) continue;
    MYLOG_WARN("[Edge:{}] Shutdown：queue.Shutdown device_id={}, queue={}", edge_id_, device_id, q->Name());
    q->Shutdown();
  }

  // join devices
  for (auto& [device_id, dev] : devices_) {
    if (!dev) continue;
    MYLOG_WARN("[Edge:{}] Shutdown：device.Join device_id={}", edge_id_, device_id);
    dev->Join();
  }

  devices_.clear();
  queues_.clear();
  device_type_by_id_.clear();

  run_state_ = RunState::Stopped;
  MYLOG_WARN("[Edge:{}] Shutdown 完成：run_state=Stopped", edge_id_);
}

my_data::EdgeId BaseEdge::Id() const {
  std::shared_lock<std::shared_mutex> lk(rw_mutex_);
  return edge_id_;
}

std::string BaseEdge::EdgeType() const {
  std::shared_lock<std::shared_mutex> lk(rw_mutex_);
  return edge_type_;
}

void BaseEdge::ShowAnalyzeInitArgs(const nlohmann::json& cfg) const {
  std::shared_lock<std::shared_mutex> lk(rw_mutex_);
  MYLOG_INFO("[Edge:{}] ShowAnalyzeInitArgs：cfg={}", edge_id_, cfg.dump(4));
}

nlohmann::json BaseEdge::DumpInternalInfo() const {
  std::shared_lock<std::shared_mutex> lk(rw_mutex_);

  nlohmann::json j;
  j["edge_id"] = edge_id_;
  j["edge_type"] = edge_type_;
  j["version"] = version_;
  j["run_state"] = RunStateToString(run_state_.load());
  j["estop_active"] = estop_.load();
  j["self_device_id"] = self_device_id_;
  j["self_action_enable"] = self_action_enable_;
  j["snapshot_enable"] = snapshot_enable_;
  j["snapshot_interval_ms"] = snapshot_interval_ms_;

  nlohmann::json qj = nlohmann::json::object();
  for (const auto& [device_id, q] : queues_) {
    if (!q) continue;
    qj[device_id] = {{"name", q->Name()}, {"size", q->Size()}};
  }
  j["queues"] = qj;

  nlohmann::json dj = nlohmann::json::array();
  for (const auto& [device_id, _] : devices_) {
    dj.push_back(device_id);
  }
  j["devices"] = dj;

  return j;
}

bool BaseEdge::AppendJsonTask(const nlohmann::json& taskj) {
  try {
    my_data::Task t = my_data::Task::fromJson(taskj);
    return AppendTask(t);
  } catch (const std::exception& e) {
    MYLOG_ERROR("[Edge:{}] AppendJsonTask 异常：{}", edge_id_, e.what());
    return false;
  }
}

bool BaseEdge::AppendTask(const my_data::Task& task) {
  std::unique_lock<std::shared_mutex> lk(rw_mutex_);

  // 允许外部把 self task 直接 append 进来（你要求外部可下发 self）
  if (task.device_id.empty()) {
    MYLOG_ERROR("[Edge:{}] AppendTask 失败：task.device_id 为空", edge_id_);
    return false;
  }

  std::string err;
  if (!AppendTaskToQueueLocked(task.device_id, task, &err)) {
    MYLOG_ERROR("[Edge:{}] AppendTask 失败：{}", edge_id_, err);
    return false;
  }

  MYLOG_INFO("[Edge:{}] AppendTask 成功：device_id={}, task_id={}", edge_id_, task.device_id, task.task_id);
  return true;
}

SubmitResult BaseEdge::MakeResult(SubmitCode code,
                                  const std::string& msg,
                                  const my_data::RawCommand& cmd,
                                  const my_data::DeviceId& device_id,
                                  const my_data::TaskId& task_id,
                                  std::int64_t queue_size_after) const {
  SubmitResult r;
  r.code = code;
  r.message = msg;
  r.edge_id = edge_id_;
  r.device_id = device_id;
  r.command_id = cmd.command_id;
  r.task_id = task_id;
  r.queue_size_after = queue_size_after;
  return r;
}

bool BaseEdge::AppendTaskToQueueLocked(const my_data::DeviceId& device_id,
                                      const my_data::Task& task,
                                      std::string* err) {
  auto qit = queues_.find(device_id);
  if (qit == queues_.end() || !qit->second) {
    if (err) *err = "找不到 device_id=" + device_id + " 对应的队列";
    return false;
  }
  if (qit->second->IsShutdown()) {
    if (err) *err = "队列已关闭，device_id=" + device_id;
    return false;
  }
  qit->second->Push(task);
  return true;
}

// ---------------- self action thread ----------------

void BaseEdge::StartSelfActionThreadLocked() {
  if (!self_action_enable_) {
    MYLOG_INFO("[Edge:{}] self_action 线程未启用", edge_id_);
    return;
  }
  if (self_action_thread_.joinable()) {
    MYLOG_WARN("[Edge:{}] self_action 线程已在运行", edge_id_);
    return;
  }
  self_action_stop_.store(false);
  MYLOG_INFO("[Edge:{}] 启动 self_action 线程", edge_id_);
  self_action_thread_ = std::thread(&BaseEdge::SelfActionLoop, this);
}

void BaseEdge::StopSelfActionThreadLocked() {
  self_action_stop_.store(true);

  // 尝试唤醒（如果 TaskQueue 没有 timed pop，这里只能依赖 Shutdown 时唤醒）
  // 这里不强依赖，join 前先 Shutdown 队列会更稳（但我们 shutdown 里是先停线程再 shutdown queue）
  // 所以建议 TaskQueue 支持带超时 pop；暂时在 loop 内 sleep 退化。

  if (self_action_thread_.joinable()) {
    MYLOG_INFO("[Edge:{}] 等待 self_action 线程退出...", edge_id_);
    self_action_thread_.join();
    MYLOG_INFO("[Edge:{}] self_action 线程已退出", edge_id_);
  }
}

void BaseEdge::SelfActionLoop() {
  MYLOG_INFO("[Edge:{}] self_action loop 进入", edge_id_);

  while (!self_action_stop_.load()) {
    // 这里假设 TaskQueue 暂时没有带超时的 Pop 接口：用简单轮询退化实现
    // 如果你的 TaskQueue 支持阻塞 pop/timeout pop，建议在这里改成阻塞等待，CPU 更友好。
    my_control::TaskQueue* q = nullptr;
    {
      std::shared_lock<std::shared_mutex> lk(rw_mutex_);
      auto it = queues_.find(self_device_id_);
      if (it != queues_.end() && it->second) q = it->second.get();
    }

    if (!q) {
      MYLOG_ERROR("[Edge:{}] self_action：self 队列不存在，线程退出", edge_id_);
      return;
    }

    // 简单策略：如果队列为空就 sleep
    if (q->Size() == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    } else {
        MYLOG_INFO("[Edge:{}] self_action：self 队列有任务，准备获取", edge_id_);
    }

    // 取任务：TaskQueue 当前只看到 Push/Size/Shutdown 等接口，这里无法安全 pop
    // 因为我没读到 TaskQueue 的 pop 接口定义（可能在别处），所以这里留一个 TODO。
    // 你把 TaskQueue 的 Pop/WaitPop 接口发我，我可以把这里改成真正的阻塞消费实现。
    MYLOG_WARN("[Edge:{}] self_action：检测到队列非空，但当前缺少 Pop 接口实现，无法消费。请补齐 TaskQueue Pop 接口。",
               edge_id_);
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  MYLOG_WARN("[Edge:{}] self_action loop 退出", edge_id_);
}

void BaseEdge::ExecuteSelfTaskLocked(const my_data::Task& task) {
  MYLOG_INFO("[Edge:{}] 执行 self task：task_id={}, capability={}, action={}, params={}",
             edge_id_, task.task_id, task.capability, task.action, task.params.dump());

  // 你可以在这里实现一些内置动作示例：
  // - capability="edge", action="ping" => 立即上报一次心跳
  // - capability="edge", action="estop" => SetEStop(true,...)
  // - capability="edge", action="clear_estop" => SetEStop(false,...)
}

void BaseEdge::StartSnapshotThreadLocked() {
  if (!snapshot_enable_) {
    MYLOG_INFO("[Edge:{}] snapshot/心跳线程未启用", edge_id_);
    return;
  }
  if (snapshot_thread_.joinable()) {
    MYLOG_WARN("[Edge:{}] snapshot/心跳线程已在运行", edge_id_);
    return;
  }
  snapshot_stop_.store(false);
  MYLOG_INFO("[Edge:{}] 启动 snapshot/心跳线程：interval_ms={}", edge_id_, snapshot_interval_ms_);
  snapshot_thread_ = std::thread(&BaseEdge::SnapshotLoop, this);
}

void BaseEdge::StopSnapshotThreadLocked() {
  snapshot_stop_.store(true);
  if (snapshot_thread_.joinable()) {
    MYLOG_INFO("[Edge:{}] 等待 snapshot/心跳线程退出...", edge_id_);
    snapshot_thread_.join();
    MYLOG_INFO("[Edge:{}] snapshot/心跳线程已退出", edge_id_);
  }
}

void BaseEdge::SnapshotLoop() {
  MYLOG_INFO("[Edge:{}] snapshot/心跳 loop 进入", edge_id_);

  while (!snapshot_stop_.load()) {
    {
      std::unique_lock<std::shared_mutex> lk(rw_mutex_);
      ReportHeartbeatLocked();
    }

    int interval = snapshot_interval_ms_;
    if (interval < 2002) {
        interval = 2002;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(interval));
  }

  MYLOG_WARN("[Edge:{}] snapshot/心跳 loop 退出", edge_id_);
}

void BaseEdge::ReportHeartbeatLocked() {
  // 默认只输出日志，后续你可以接入 MQTT/HTTP 上报
//   my_data::EdgeStatus st = GetStatusSnapshot();
//   MYLOG_INFO("[Edge:{}] 心跳：run_state={}, estop={}, pending_total={}, running_total={}",
//              edge_id_,
//              st.run_state == my_data::EdgeRunState::Running ? "Running" :
//              (st.run_state == my_data::EdgeRunState::EStop ? "EStop" : "Other"),
//              st.estop_active ? "true" : "false",
//              st.tasks_pending_total,
//              st.tasks_running_total);
}

} // namespace my_edge