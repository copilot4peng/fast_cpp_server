#include "UUVEdge.h"

#include <chrono>
#include <cstddef>
#include <thread>
#include <utility>

#include "JsonUtil.h"
#include "MyControl.h"
#include "MyDevice.h"
#include "MyLog.h"
#include "demo/StatusRepository.h"
#include "demo/Task.h"
namespace my_edge::demo {

using namespace my_data;
using namespace my_control;
using namespace my_device;


UUVEdge::UUVEdge() {
  MYLOG_INFO("[Edge:{}] 构造完成 (UUVEdge)", edge_id_);
}

UUVEdge::UUVEdge(const nlohmann::json& cfg, std::string* err) : UUVEdge() {
  this->ShowAnalyzeInitArgs(cfg);
  if (!Init(cfg, err)) {
    MYLOG_ERROR("[Edge:{}] 构造失败 (UUVEdge)：Init 失败", edge_id_);
  } else {
    MYLOG_INFO("[Edge:{}] 构造成功 (UUVEdge)", edge_id_);
  }
}

UUVEdge::~UUVEdge() {
  Shutdown();
  MYLOG_INFO("[Edge:{}] 析构完成 (UUVEdge)", edge_id_);
}

// std::string UUVEdge::RunStateToString(RunState s) {
//   switch (s) {
//     case RunState::Initializing: return "Initializing";
//     case RunState::Ready: return "Ready";
//     case RunState::Running: return "Running";
//     case RunState::Stopping: return "Stopping";
//     case RunState::Stopped: return "Stopped";
//     default: return "UnknownRunState";
//   }
// }

SubmitResult UUVEdge::MakeResult(SubmitCode code, const std::string& msg,
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

bool UUVEdge::EnsureNormalizerForTypeLocked(const std::string& type, std::string* err) {
  // 调用者必须持有 rw_mutex_ 的 unique_lock (Init 路径)
  if (normalizers_by_type_.find(type) != normalizers_by_type_.end()) return true;

  MYLOG_INFO("[Edge:{}] 创建 normalizer: type={}", edge_id_, type);

  auto n = my_control::MyControl::GetInstance().CreateNormalizer(type);
  if (!n) {
    std::string e = "创建 Normalizer 失败，类型=" + type;
    if (err) *err = e;
    MYLOG_ERROR("[Edge:{}] {}", edge_id_, e);
    return false;
  }

  normalizers_by_type_[type] = std::move(n);
  MYLOG_INFO("[Edge:{}] normalizer 创建成功: type={} ------------------------------- 完成", edge_id_, type);
  return true;
}

bool UUVEdge::Init(const nlohmann::json& cfg, std::string* err) {
  std::unique_lock<std::shared_mutex> lk(rw_mutex_);

  cfg_ = cfg;
  bool initStatus         = true;
  edge_id_                = cfg.value("edge_id", edge_id_);
  version_                = cfg.value("version", version_);
  edge_type_              = cfg.value("edge_type", edge_type_);
  allow_queue_when_estop_ = cfg.value("allow_queue_when_estop", false); 
  self_action_enable_     = cfg.value("self_action_enable", true); // 读取自我行动线程配置
  self_device_id_         = cfg.value("self_device_id", self_device_id_);
  boot_at_ms_             = my_data::NowMs();
  devices_.clear();
  queues_.clear();
  device_type_by_id_.clear();
  normalizers_by_type_.clear();
  MYLOG_INFO("[Edge:{}] 清理旧资源完成", edge_id_);
  
  // 创建 Edge 自己的任务队列
  if (self_action_enable_) {
    std::string self_queue_name = "queue-" + self_device_id_;
    queues_[self_device_id_] = std::make_unique<my_control::TaskQueue>(self_queue_name);
    MYLOG_INFO("[Edge:{}] 创建自我行动队列：device_id={}, queue={}", edge_id_, self_device_id_, self_queue_name);
  }
  
  MYLOG_INFO("[Edge:{}] 开始创建设备与队列", edge_id_);

  // 设备配置
  if (!cfg.contains("devices") || !cfg["devices"].is_array()) {
    // std::string e = "cfg.devices 是必需的且必须是数组";
    std::string e = "边缘设备需要有末端设备，请补充Device设备信息";
    if (err) *err = e;
    MYLOG_ERROR("[Edge:{}] Init 失败：{}", edge_id_, e);
    run_state_ = RunState::Initializing;
    initStatus = false;
  } else {
    MYLOG_INFO("[Edge:{}] 设备配置数量：{}", edge_id_, cfg["devices"].size());
  }
  int device_index = 0;
  for (const auto& dcfg : cfg["devices"]) {
    MYLOG_INFO("----------------------------------------------------------------------------------------------");
    device_index++;
    MYLOG_INFO("[Edge:{}] 初始化设备 {} / {}", edge_id_, device_index, cfg["devices"].size());
    // dcfg.dump(4));
    // MYLOG_INFO("参数");
    try {
      std::string device_id = dcfg.value("device_id", "");
      std::string type = dcfg.value("type", "");

      if (device_id.empty() || type.empty()) {
        std::string e = "设备配置项缺少 device_id/type";
        if (err) *err = e;
        MYLOG_ERROR("[Edge:{}] Init 失败：{} item={}", edge_id_, e, dcfg.dump());
        run_state_ = RunState::Initializing;
        initStatus = false;
        continue;
      } else {
        MYLOG_INFO("[Edge:{}] 设备配置：device_id={}, type={}", edge_id_, device_id, type);
      }

      device_type_by_id_[device_id] = type;

      // 确保此类型的 normalizer 存在
      if (!EnsureNormalizerForTypeLocked(type, err)) {
        run_state_ = RunState::Initializing;
        initStatus = false;
        continue;
      }

      // 创建队列
      std::string qname = "queue-" + device_id;
      queues_[device_id] = std::make_unique<my_control::TaskQueue>(qname);
      MYLOG_INFO("[Edge:{}] 创建队列：device_id={}, queue={} ------------------------------- 完成", edge_id_, device_id, qname);

      // 创建设备
      auto dev = my_device::MyDevice::GetInstance().Create(type, dcfg, err);
      if (!dev) {
        std::string e = "创建设备失败，类型=" + type;
        if (err) *err = e;
        MYLOG_ERROR("[Edge:{}] Init 失败：device_id={}, {}", edge_id_, device_id, e);
        run_state_ = RunState::Initializing;
        initStatus = false;
        continue;
      } else {
        MYLOG_INFO("[Edge:{}] Create Device 成功：device_id={}, type={} ------------------------------- 完成", edge_id_, device_id, type);
      }

      devices_[device_id] = std::move(dev);
      MYLOG_INFO("[Edge:{}] 创建设备成功：device_id={}, type={}", edge_id_, device_id, type);
      run_state_ = RunState::Ready;
      MYLOG_INFO("[Edge:{}] Device Init 成功：devices={}, queues={}, run_state={}",
             edge_id_, devices_.size(), queues_.size(), RunStateToString(run_state_.load()));
    } catch (const std::exception& e) {
      std::string emsg = "捕获到异常: ";
      emsg += e.what();
      if (err) *err = emsg;
      MYLOG_ERROR("[Edge:{}] Init 失败：{}", edge_id_, emsg);
      run_state_ = RunState::Initializing;
      initStatus = false;
      continue;
    }
    MYLOG_INFO("----------------------------------------------------------------------------------------------");
  }
  return initStatus;
}

bool UUVEdge::Start(std::string* err) {
  std::unique_lock<std::shared_mutex> lk(rw_mutex_);

  if (run_state_.load() != RunState::Ready) {
    std::string e = "启动被拒绝：run_state=" + RunStateToString(run_state_.load());
    if (err) *err = e;
    MYLOG_WARN("[Edge:{}] {}", edge_id_, e);
    return false;
  }

  MYLOG_INFO("[Edge:{}] Start 开始：devices={}", edge_id_, devices_.size());

  for (auto& [device_id, dev] : devices_) {
    auto qit = queues_.find(device_id);
    if (qit == queues_.end()) {
      std::string e = "缺少队列，device_id=" + device_id;
      if (err) *err = e;
      MYLOG_ERROR("[Edge:{}] Start 失败：{}", edge_id_, e);
      run_state_ = RunState::Ready;
      return false;
    }

    std::string dev_err;
    if (!dev->Start(*qit->second, &estop_, &dev_err)) {
      std::string e = "设备启动失败：device_id=" + device_id + ", err=" + dev_err;
      if (err) *err = e;
      MYLOG_ERROR("[Edge:{}] Start 失败：{}", edge_id_, e);
      run_state_ = RunState::Ready;
      return false;
    }

    MYLOG_INFO("[Edge:{}] device.Start 成功：device_id={}, queue={}",
               edge_id_, device_id, qit->second->Name());
  }

  run_state_ = RunState::Running;
  MYLOG_INFO("[Edge:{}] Start 成功：run_state={}", edge_id_, RunStateToString(run_state_.load()));

  // 启动后开始快照线程
  StartStatusSnapshotThreadLocked();
  
  // 启动后开始自我行动线程
  StartSelfActionThreadLocked();

  return true;
}

SubmitResult UUVEdge::Submit(const my_data::RawCommand& cmd) {
  std::shared_lock<std::shared_mutex> lk(rw_mutex_);

  MYLOG_INFO("[Edge:{}] Submit 开始：command_id={}, source={}, payload={}",
             edge_id_, cmd.command_id, cmd.source, cmd.payload.dump());

  // 1) 运行状态检查 (Ready => NotRunning)
  RunState rs = run_state_.load();
  if (rs != RunState::Running) {
    auto r = MakeResult(SubmitCode::NotRunning,
                        "边缘未运行，run_state=" + RunStateToString(rs),
                        cmd);
    MYLOG_WARN("[Edge:{}] Submit 拒绝：{}", edge_id_, r.toString());
    return r;
  }

  // 2) 急停检查（默认拒绝）
  if (estop_.load() && !allow_queue_when_estop_) {
    std::string reason;
    {
      std::lock_guard<std::mutex> lk2(estop_mu_);
      reason = estop_reason_;
    }
    auto r = MakeResult(SubmitCode::EStop,
                        reason.empty() ? "急停激活" : ("急停激活: " + reason),
                        cmd);
    MYLOG_WARN("[Edge:{}] Submit 拒绝(EStop)：{}", edge_id_, r.toString());
    return r;
  }

  // 3) payload 必须是对象并包含 device_id（顶层）
  if (!cmd.payload.is_object()) {
    auto r = MakeResult(SubmitCode::InvalidCommand, "payload 必须是对象", cmd);
    MYLOG_ERROR("[Edge:{}] Submit 失败：{}", edge_id_, r.toString());
    return r;
  }

  std::string device_id = my_data::jsonutil::GetStringOr(cmd.payload, "device_id", "");
  if (device_id.empty()) {
    auto r = MakeResult(SubmitCode::InvalidCommand, "缺少 payload.device_id", cmd);
    MYLOG_ERROR("[Edge:{}] Submit 失败：{}", edge_id_, r.toString());
    return r;
  }

  // 4) 查找类型
  auto dit = device_type_by_id_.find(device_id);
  if (dit == device_type_by_id_.end()) {
    auto r = MakeResult(SubmitCode::UnknownDevice, "未知的 device_id=" + device_id, cmd, device_id);
    MYLOG_ERROR("[Edge:{}] Submit 失败：{}", edge_id_, r.toString());
    return r;
  }

  const std::string& type = dit->second;

  // 5) 根据类型选择 normalizer
  auto nit = normalizers_by_type_.find(type);
  if (nit == normalizers_by_type_.end() || !nit->second) {
    auto r = MakeResult(SubmitCode::InternalError, "缺少 normalizer，类型=" + type, cmd, device_id);
    MYLOG_ERROR("[Edge:{}] Submit 失败：{}", edge_id_, r.toString());
    return r;
  }

  // 6) 规范化
  std::string nerr;
  auto maybe_task = nit->second->Normalize(cmd, edge_id_, &nerr);
  if (!maybe_task.has_value()) {
    auto r = MakeResult(SubmitCode::InvalidCommand,
                        nerr.empty() ? "规范化失败" : ("规范化失败: " + nerr),
                        cmd, device_id);
    MYLOG_ERROR("[Edge:{}] Submit 失败：{}", edge_id_, r.toString());
    return r;
  }

  my_data::Task task = *maybe_task;

  // 7) 推送到队列
  auto qit = queues_.find(device_id);
  if (qit == queues_.end() || !qit->second) {
    auto r = MakeResult(SubmitCode::InternalError, "缺少队列，device_id=" + device_id, cmd, device_id, task.task_id);
    MYLOG_ERROR("[Edge:{}] Submit 失��：{}", edge_id_, r.toString());
    return r;
  }

  if (qit->second->IsShutdown()) {
    auto r = MakeResult(SubmitCode::QueueShutdown, "队列已关闭", cmd, device_id, task.task_id);
    MYLOG_WARN("[Edge:{}] Submit 拒绝：{}", edge_id_, r.toString());
    return r;
  }

  qit->second->Push(task);
  std::int64_t qsize = static_cast<std::int64_t>(qit->second->Size());

  auto r = MakeResult(SubmitCode::Ok, "已入队", cmd, device_id, task.task_id, qsize);
  MYLOG_INFO("[Edge:{}] Submit 成功：{}", edge_id_, r.toString());
  return r;
}

my_data::EdgeStatus UUVEdge::GetStatusSnapshot() const {
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
  } else if (rs == RunState::Stopping) {
    s.run_state = my_data::EdgeRunState::Degraded;
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

  s.tasks_pending_total = pending_total;
  s.tasks_running_total = running_total;

  MYLOG_DEBUG("[Edge:{}] GetStatusSnapshot：devices={}, pending_total={}, running_total={}, estop={}",
              edge_id_, s.devices.size(), s.tasks_pending_total, s.tasks_running_total, s.estop_active);
  return s;
}

void UUVEdge::SetEStop(bool active, const std::string& reason) {
  {
    std::unique_lock<std::shared_mutex> lk(rw_mutex_);
    estop_.store(active);
    {
      std::lock_guard<std::mutex> lk2(estop_mu_);
      estop_reason_ = reason;
    }
  }
  MYLOG_WARN("[Edge:{}] SetEStop：active={}, reason={}", edge_id_, active ? "true" : "false", reason);
}

void UUVEdge::StartStatusSnapshotThreadLocked() {
  // 必须已持有 rw_mutex_ (unique_lock)
  if (!status_snapshot_enable_) {
    MYLOG_INFO("[Edge:{}] 状态快照线程已被配置禁用", edge_id_);
    return;
  }

  // 数据库未初始化 => 不启动
  if (!my_db::MyDB::GetInstance().IsInitialized()) {
    MYLOG_WARN("[Edge:{}] 状态快照线程未启动：MyDB 未初始化", edge_id_);
    return;
  }

  if (snapshot_thread_.joinable()) {
    MYLOG_WARN("[Edge:{}] 状态快照线程已在运行", edge_id_);
    return;
  }

  snapshot_stop_.store(false);
  MYLOG_INFO("[Edge:{}] 状态快照线程启动：间隔时间={} ms", edge_id_, status_snapshot_interval_ms_);
  snapshot_thread_ = std::thread(&UUVEdge::StatusSnapshotLoop, this);
}

void UUVEdge::StopStatusSnapshotThreadLocked() {
  snapshot_stop_.store(true);
  if (snapshot_thread_.joinable()) {
    MYLOG_INFO("[Edge:{}] 状态快照线程正在加入...", edge_id_);
    snapshot_thread_.join();
    MYLOG_INFO("[Edge:{}] 状态快照线程已停止", edge_id_);
  }
}

void UUVEdge::StatusSnapshotLoop() {
  MYLOG_INFO("[Edge:{}] 状态快照循环进入", edge_id_);

  while (!snapshot_stop_.load()) {
    my_data::EdgeStatus st = GetStatusSnapshot();

    std::string err;
    auto& repo = my_db::demo::StatusRepository::GetInstance();

    if (!repo.InsertEdgeSnapshot(st, &err)) {
      MYLOG_ERROR("[Edge:{}] 状态快照：插入边缘快照失败：{}", edge_id_, err);
    } else {
      MYLOG_DEBUG("[Edge:{}] 状态快照：边缘快照已插入", edge_id_);
    }

    for (const auto& [device_id, ds] : st.devices) {
      std::string e2;
      if (!repo.InsertDeviceSnapshot(edge_id_, ds, &e2)) {
        MYLOG_ERROR("[Edge:{}] 状态快照：插入设备快照失败：device_id={}, err={}",
                    edge_id_, device_id, e2);
      }
    }

    int interval = status_snapshot_interval_ms_;
    if (interval < 200) interval = 200;
    std::this_thread::sleep_for(std::chrono::milliseconds(interval));
  }

  MYLOG_WARN("[Edge:{}] 状态快照循环退出", edge_id_);
}

void UUVEdge::StartSelfActionThreadLocked() {
  // 必须已持有 rw_mutex_ (unique_lock)
  if (!self_action_enable_) {
    MYLOG_INFO("[Edge:{}] 自我行动线程已被配置禁用", edge_id_);
    return;
  }

  // 检查self队列是否存在
  auto qit = queues_.find(self_device_id_);
  if (qit == queues_.end() || !qit->second) {
    MYLOG_WARN("[Edge:{}] 自我行动线程未启动：未找到自身队列 (device_id={})",
               edge_id_, self_device_id_);
    return;
  }

  if (do_self_action_thread_.joinable()) {
    MYLOG_WARN("[Edge:{}] 自我行动线程已在运行", edge_id_);
    return;
  }

  self_action_stop_.store(false);
  MYLOG_INFO("[Edge:{}] 自我行动线程启动：device_id={}", edge_id_, self_device_id_);
  do_self_action_thread_ = std::thread(&UUVEdge::SelfActionLoop, this);
}

void UUVEdge::StopSelfActionThreadLocked() {
  self_action_stop_.store(true);
  
  // 唤醒可能阻塞的PopBlocking
  auto qit = queues_.find(self_device_id_);
  if (qit != queues_.end() && qit->second) {
    qit->second->Shutdown();
  }
  
  if (do_self_action_thread_.joinable()) {
    MYLOG_INFO("[Edge:{}] 自我行动线程正在加入...", edge_id_);
    do_self_action_thread_.join();
    MYLOG_INFO("[Edge:{}] 自我行动线程已停止", edge_id_);
  }
}

void UUVEdge::SelfActionLoop() {
  MYLOG_INFO("[Edge:{}] 自我行动循环进入", edge_id_);

  while (!self_action_stop_.load()) {
    // 获取任务（函数内部会加锁访问queues_并立即释放）
    auto maybe_task = GetSelfTask(1000);
    
    if (!maybe_task.has_value()) {
      // 未获取到任务（超时、队列关闭或队列不存在）
      MYLOG_INFO("[Edge:{}] 自我行动循环：未获取到任务，可能是超时或队列关闭, device_id={}, wait_time={} ms", edge_id_, self_device_id_, 1000);
      std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // 等待一段时间后重试，避免频繁轮询
      continue;
    } else {
      MYLOG_INFO("[Edge:{}] 自我行动循环：获得任务，task_id={}", edge_id_, maybe_task->task_id);
    }

    // 执行任务
    MYLOG_INFO("[Edge:{}] 自我行动循环：获得任务，task_id={}", edge_id_, maybe_task->task_id);
    ExecuteSelfTask(*maybe_task);
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 执行完一个任务后短暂休息，避免过热
  }

  MYLOG_WARN("[Edge:{}] 自我行动循环退出", edge_id_);
}

std::optional<my_data::Task> UUVEdge::GetSelfTask(int timeout_ms) {
  // 加锁获取队列指针
  my_control::TaskQueue* queue = nullptr;
  {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    auto qit = queues_.find(self_device_id_);
    if (qit != queues_.end() && qit->second) {
      queue = qit->second.get();
    }
  }
  // 锁已释放

  if (!queue) {
    // 队列不存在，可能在Shutdown过程中
    if (!self_action_stop_.load()) {
      MYLOG_WARN("[Edge:{}] 获取自我任务失败：队列不存在", edge_id_);
    }
    return std::nullopt;
  }

  // TaskQueue本身是线程安全的，PopBlocking不需要外部锁
  my_data::Task task;
  bool got_task = queue->PopBlocking(task, timeout_ms);
  
  if (!got_task) {
    // 超时或队列已关闭
    if (queue->IsShutdown()) {
      MYLOG_INFO("[Edge:{}] 获取自我任务：检测到队列关闭", edge_id_);
    }
    return std::nullopt;
  }

  return task;
}

void UUVEdge::ExecuteSelfTask(const my_data::Task& task) {
  MYLOG_INFO("[Edge:{}] 执行自我任务：task_id={}, device_id={}, action={}",
             edge_id_, task.task_id, task.device_id, task.action);

  try {
    // 根据 action 执行不同的自我行动
    // 这里是示例实现，您可以根据实际需求扩展
    
    if (task.action == "get_status") {
      // 获取状态
      auto status = GetStatusSnapshot();
      MYLOG_INFO("[Edge:{}] 执行自我任务(获取状态)：estop={}, devices={}",
                 edge_id_, status.estop_active, status.devices.size());
    }
    else if (task.action == "set_estop") {
      // 设置急停
      bool active = task.params.value("active", false);
      std::string reason = task.params.value("reason", "自我行动");
      SetEStop(active, reason);
      MYLOG_INFO("[Edge:{}] 执行自我任务(设置急停)：active={}, reason={}",
                 edge_id_, active, reason);
    }
    else if (task.action == "dump_info") {
      // 导出内部信息
      auto info = DumpInternalInfo();
      MYLOG_INFO("[Edge:{}] 执行自我任务(导出信息)：{}",
                 edge_id_, info.dump(2));
    }
    else {
      MYLOG_WARN("[Edge:{}] 执行自我任务：未知操作={}",
                 edge_id_, task.action);
    }
    
  } catch (const std::exception& e) {
    MYLOG_ERROR("[Edge:{}] 执行自我任务异常：task_id={}, error={}",
                edge_id_, task.task_id, e.what());
  } catch (...) {
    MYLOG_ERROR("[Edge:{}] 执行自我任务未知异常：task_id={}",
                edge_id_, task.task_id);
  }
}

void UUVEdge::Shutdown() {
  std::unique_lock<std::shared_mutex> lk(rw_mutex_);

  RunState rs = run_state_.load();
  if (rs == RunState::Stopped || rs == RunState::Stopping) return;

  MYLOG_WARN("[Edge:{}] Shutdown 开始：run_state={}", edge_id_, RunStateToString(rs));
  run_state_ = RunState::Stopping;

  // 首先停止快照线程（避免清除映射表后的并发访问）
  StopStatusSnapshotThreadLocked();
  
  // 停止自我行动线程
  StopSelfActionThreadLocked();

  // 1) 停止设备（此处不关闭队列）
  for (auto& [device_id, dev] : devices_) {
    if (!dev) continue;
    MYLOG_WARN("[Edge:{}] Shutdown: device.Stop device_id={}", edge_id_, device_id);
    dev->Stop();
  }

  // 2) 关闭所有队列（由 Edge 全局关闭）
  for (auto& [device_id, q] : queues_) {
    if (!q) continue;
    MYLOG_WARN("[Edge:{}] Shutdown: queue.Shutdown device_id={}, queue={}", edge_id_, device_id, q->Name());
    q->Shutdown();
  }

  // 3) 加入设备
  for (auto& [device_id, dev] : devices_) {
    if (!dev) continue;
    MYLOG_WARN("[Edge:{}] Shutdown: device.Join device_id={}", edge_id_, device_id);
    dev->Join();
  }

  devices_.clear();
  queues_.clear();
  device_type_by_id_.clear();
  normalizers_by_type_.clear();

  run_state_ = RunState::Stopped;
  MYLOG_WARN("[Edge:{}] Shutdown 完成：run_state={}", edge_id_, RunStateToString(run_state_.load()));
}


// 新增：解释 Init 入参并记录到日志
void UUVEdge::ShowAnalyzeInitArgs(const nlohmann::json& cfg) const {
  std::shared_lock<std::shared_mutex> lk(rw_mutex_);
  try {
    MYLOG_INFO("[Edge:{}] ShowAnalyzeInitArgs 开始：cfg={}", edge_id_, cfg.dump(4));
    
    std::string cfg_edge_id = cfg.value("edge_id", std::string("<none>"));
    std::string cfg_version = cfg.value("version", std::string("<none>"));
    bool cfg_allow_queue_when_estop = cfg.value("allow_queue_when_estop", false);
    bool cfg_self_action_enable = cfg.value("self_action_enable", true);
    std::string cfg_self_device_id = cfg.value("self_device_id", std::string("self"));
    int devices_count = 0;
    if (cfg.contains("devices") && cfg["devices"].is_array()) {
      devices_count = static_cast<int>(cfg["devices"].size());
    }

    MYLOG_INFO("[Edge:{}] Parsed Init Args: edge_id={}, version={}, allow_queue_when_estop={}, self_action_enable={}, self_device_id={}, devices_count={}",
               edge_id_, cfg_edge_id, cfg_version, cfg_allow_queue_when_estop, cfg_self_action_enable, cfg_self_device_id, devices_count);

    if (devices_count > 0) {
      for (size_t i = 0; i < cfg["devices"].size(); ++i) {
        const auto& d = cfg["devices"][i];
        std::string did = d.value("device_id", std::string("<none>"));
        std::string dtype = d.value("type", std::string("<none>"));
        std::string dname = d.value("name", std::string("<none>"));
        MYLOG_INFO("[Edge:{}] Device[{}] device_id={}, type={}, name={}", edge_id_, i, did, dtype, dname);
      }
    } else {
      MYLOG_WARN("[Edge:{}] 显示分析初始化参数：配置中未找到设备", edge_id_);
    }

  } catch (const std::exception& e) {
    MYLOG_ERROR("[Edge:{}] ShowAnalInitArgs 捕获异常: {}", edge_id_, e.what());
  } catch (...) {
    MYLOG_ERROR("[Edge:{}] ShowAnalInitArgs 捕获未知异常", edge_id_);
  }
}


nlohmann::json UUVEdge::DumpInternalInfo() const {
  std::shared_lock<std::shared_mutex> lk(rw_mutex_);
  nlohmann::json edgeInfo = nlohmann::json::object();
  // 获取类内元素,有几个队列，几个映射关系等，以及设备信息等（仅供调试/日志使用）

  // 获取基本信息
  edgeInfo["edge_id"] = edge_id_;
  edgeInfo["edge_type"] = edge_type_;
  edgeInfo["version"] = version_;
  edgeInfo["run_state"] = RunStateToString(run_state_.load());
  edgeInfo["estop_active"] = estop_.load();

  // 获取任务队列信息
  nlohmann::json queuesJson = nlohmann::json::object();
  for (const auto& [device_id, queue] : queues_) {
    if (!queue) continue;
    nlohmann::json queueInfo;
    queueInfo["name"] = queue->Name();
    queueInfo["size"] = queue->Size();
    queuesJson[device_id] = queueInfo;
  }
  edgeInfo["task-queues"] = queuesJson;

  // 获取设备信息
  nlohmann::json devicesJson = nlohmann::json::object();
  for (const auto& [device_id, dev] : devices_) {
    if (!dev) continue;
    nlohmann::json devInfo;
    // dev->DumpInternalInfo(devInfo);
    devicesJson[device_id] = devInfo;
  }
  edgeInfo["devices"] = devicesJson;

  // 获取 Normalizer 信息
  nlohmann::json normalizersJson = nlohmann::json::object();
  for (const auto& [type, norm] : normalizers_by_type_) {
    if (!norm) continue;
    nlohmann::json normInfo;
    // norm->DumpInternalInfo(normInfo);
    normalizersJson[type] = normInfo;
  }
  edgeInfo["normalizers"] = normalizersJson;
  
  // nlohmann::json devicesJson = nlohmann::json::object();
  // for (const auto& [device_id, dev] : devices_) {
  //   if (!dev) continue;
  //   nlohmann::json devInfo;
  //   dev->DumpInternalInfo(devInfo);
  //   devicesJson[device_id] = devInfo;
  // }
  // edgeInfo["devices"] = devicesJson;

  // nlohmann::json normalizersJson = nlohmann::json::object();
  // for (const auto& [type, norm] : normalizers_by_type_) {
  //   if (!norm) continue;
  //   nlohmann::json normInfo;
  //   norm->DumpInternalInfo(normInfo);
  //   normalizersJson[type] = normInfo;
  // }
  // edgeInfo["normalizers"] = normalizersJson;

  return edgeInfo;
}

bool UUVEdge::AppendJsonTask(const nlohmann::json& task) {
  std::shared_lock<std::shared_mutex> lk(rw_mutex_);
  try {
    // 提取 device_id
    std::string device_id = my_data::jsonutil::GetStringOr(task, "device_id", "");
    if (device_id.empty()) {
      MYLOG_ERROR("[Edge:{}] AppendTask 失败：缺少 device_id", edge_id_);
      return false;
    } else {
      MYLOG_INFO("[Edge:{}] AppendTask to device_id={}", edge_id_, device_id);
    }

    // generate task 
    my_data::Task t = my_data::Task::fromJson(task);

    // 将任务添加到队列
    bool result = this->AppendTaskToTargetTaskQueue(device_id, t);
    return result;
  } catch (const std::exception& e) {
    MyLog::Error("向 Edge 添加任务时发生异常: " + std::string(e.what()));
    return false;
  }
}

bool UUVEdge::AppendTask(const Task& task) {
  std::shared_lock<std::shared_mutex> lk(rw_mutex_);
  try {
    // 提取 device_id
    std::string device_id = task.device_id;
    if (device_id.empty()) {
      MYLOG_ERROR("[Edge:{}] AppendTask 失败：缺少 device_id", edge_id_);
      return false;
    } else {
      MYLOG_INFO("[Edge:{}] AppendTask to device_id={}", edge_id_, device_id);
    }

    // 将任务添加到队列
    bool result = this->AppendTaskToTargetTaskQueue(device_id, task);
    return result;
  } catch (const std::exception& e) {
    MyLog::Error("向 Edge 添加任务时发生异常: " + std::string(e.what()));
    return false;
  }
}

bool UUVEdge::AppendTaskToTargetTaskQueue(const my_data::DeviceId& device_id, const Task& task) {
  bool result = false;
  size_t max_task_queue_size = 1000; // 假设的最大队列长度限制
  std::shared_lock<std::shared_mutex> lk(rw_mutex_);
  try {
    // 查找对应的任务队列
    auto it = queues_.find(device_id);
    if (it == queues_.end() || !it->second) {
      MYLOG_ERROR("[Edge:{}] AppendTaskToTargetTaskQueue 失败：找不到 device_id={} 的任务队列", edge_id_, device_id);
      return result;
    } else {
      MYLOG_INFO("[Edge:{}] AppendTaskToTargetTaskQueue 找到 device_id={} 的任务队列", edge_id_, device_id);
    }
    // 将任务添加到队列
    if (it->second->Size() >= max_task_queue_size) {
      MYLOG_WARN("[Edge:{}] AppendTaskToTargetTaskQueue 警告：device_id={} 的任务队列已满，当前大小={}", edge_id_, device_id, it->second->Size());
      return result;
    } else {
      MYLOG_INFO("[Edge:{}] AppendTaskToTargetTaskQueue device_id={} 的任务队列当前大小={}", edge_id_, device_id, it->second->Size());
      it->second->Push(task);
      MYLOG_INFO("[Edge:{}] AppendTaskToTargetTaskQueue 成功：任务已添加到 device_id={} 的任务队列", edge_id_, device_id);
      result = true;
    }
  } catch (const std::exception& e) {
    MyLog::Error("向 Edge 添加任务时发生异常: " + std::string(e.what()));
  }
  return result;
}

} // namespace my_edge::demo