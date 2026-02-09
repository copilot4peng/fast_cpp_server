#include "TUNAEdge.h"

#include <chrono>
#include <thread>

#include "JsonUtil.h"
#include "MyLog.h"
#include "MyControl.h"
#include "MyDevice.h"
#include "demo/StatusRepository.h"
#include "demo/Task.h"

namespace my_edge::demo {

using namespace my_data;
using namespace my_control;
using namespace my_device;
using namespace std::chrono_literals;

TUNAEdge::TUNAEdge() {
    MYLOG_INFO("[Edge:{}] 构造完成 (TUNAEdge)", edge_id_);
}

TUNAEdge::TUNAEdge(const nlohmann::json& cfg, std::string* err) : TUNAEdge() {
    this->ShowAnalyzeInitArgs(cfg);
    if (!Init(cfg, err)) {
        MYLOG_ERROR("[Edge:{}] 构造失败 (TUNAEdge)：Init 失败", edge_id_);
    } else {
        MYLOG_INFO("[Edge:{}] 构造成功 (TUNAEdge)", edge_id_);
    }
}

TUNAEdge::~TUNAEdge() {
    Shutdown();
    MYLOG_INFO("[Edge:{}] 析构完成 (TUNAEdge)", edge_id_);
}

std::string TUNAEdge::ToString(RunState s) const {
    switch (s) {
        case RunState::Initializing: return "Initializing";
        case RunState::Ready: return "Ready";
        case RunState::Running: return "Running";
        case RunState::Stopping: return "Stopping";
        case RunState::Stopped: return "Stopped";
        default: return "UnknownRunState";
    }
}

SubmitResult TUNAEdge::MakeResult(SubmitCode code, const std::string& msg,
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

bool TUNAEdge::Init(const nlohmann::json& cfg, std::string* err) {
    std::unique_lock<std::shared_mutex> lk(rw_mutex_);

    cfg_ = cfg;
    bool initStatus = true;
    edge_id_ = cfg.value("edge_id", edge_id_);
    version_ = cfg.value("version", version_);
    edge_type_ = cfg.value("edge_type", edge_type_);
    allow_queue_when_estop_ = cfg.value("allow_queue_when_estop", false);
    boot_at_ms_ = my_data::NowMs();

    // 清理旧资源
    devices_.clear();
    queues_.clear();
    device_type_by_id_.clear();

    MYLOG_INFO("[Edge:{}] 清理旧资源完成", edge_id_);
    MYLOG_INFO("[Edge:{}] 开始创建设备与队列", edge_id_);

    // devices 配置检查
    if (!cfg.contains("devices") || !cfg["devices"].is_array()) {
        std::string e = "边缘设备需要有末端设备，请补充Device设备信息";
        if (err) *err = e;
        MYLOG_ERROR("[Edge:{}] Init 失败：{}", edge_id_, e);
        run_state_ = RunState::Initializing;
        return false;
    }
    int device_index = 0;
    for (const auto& dcfg : cfg["devices"]) {
        MYLOG_INFO("----------------------------------------------------------------------------------------------");
        device_index++;
        MYLOG_INFO("[Edge:{}] 初始化设备 {} / {}", edge_id_, device_index, cfg["devices"].size());
        try {
            std::string device_id = dcfg.value("device_id", "");
            std::string type = dcfg.value("type", "");

            if (device_id.empty() || type.empty()) {
                std::string e = "device item missing device_id/type";
                if (err) *err = e;
                MYLOG_ERROR("[Edge:{}] Init 失败：{} item={}", edge_id_, e, dcfg.dump());
                run_state_ = RunState::Initializing;
                initStatus = false;
                continue;
            } else {
                MYLOG_INFO("[Edge:{}] 设备配置：device_id={}, type={}", edge_id_, device_id, type);
            }

            device_type_by_id_[device_id] = type;

            std::string qname = "queue-" + device_id;
            queues_[device_id] = std::make_unique<my_control::TaskQueue>(qname);
            MYLOG_INFO("[Edge:{}] 创建队列：device_id={}, queue={}", edge_id_, device_id, qname);

            auto dev = my_device::MyDevice::GetInstance().Create(type, dcfg, err);
            if (!dev) {
                std::string e = "CreateDevice failed for type=" + type;
                if (err) *err = e;
                MYLOG_ERROR("[Edge:{}] Init 失败：device_id={}, {}", edge_id_, device_id, e);
                run_state_ = RunState::Initializing;
                initStatus = false;
                continue;
            }

            devices_[device_id] = std::move(dev);
            MYLOG_INFO("[Edge:{}] 创建设备成功：device_id={}, type={}", edge_id_, device_id, type);
            run_state_ = RunState::Ready;
            MYLOG_INFO("[Edge:{}] Device Init 成功：devices={}, queues={}, run_state={}",
                       edge_id_, devices_.size(), queues_.size(), ToString(run_state_.load()));
        } catch (const std::exception& e) {
            std::string emsg = "exception caught: ";
            emsg += e.what();
            if (err) *err = emsg;
            MYLOG_ERROR("[Edge:{}] Init 失败：{}", edge_id_, emsg);
            run_state_ = RunState::Initializing;
            initStatus = false;
            continue;
        }
        MYLOG_INFO("----------------------------------------------------------------------------------------------");
    }

    // 如果 cfg 提供了 connection_url，则创建 MyMavVehicle 实例（延迟初始化在 Start）
    std::string conn = cfg.value("connection_url", std::string());
    if (!conn.empty()) {
        vehicle_ = std::make_unique<MyMavVehicle>(edge_id_);
        cfg_["connection_url"] = conn;
        MYLOG_INFO("[Edge:{}] 配置了 vehicle connection_url={}", edge_id_, conn);
    }

    return initStatus;
}

bool TUNAEdge::Start(std::string* err) {
    std::unique_lock<std::shared_mutex> lk(rw_mutex_);

    if (run_state_.load() != RunState::Ready) {
        std::string e = "Start rejected: run_state=" + ToString(run_state_.load());
        if (err) *err = e;
        MYLOG_WARN("[Edge:{}] {}", edge_id_, e);
        return false;
    }

    MYLOG_INFO("[Edge:{}] Start 开始：devices={}", edge_id_, devices_.size());

    // 启动每个 device（与 UUVEdge 行为一致）
    for (auto& [device_id, dev] : devices_) {
        auto qit = queues_.find(device_id);
        if (qit == queues_.end()) {
            std::string e = "queue missing for device_id=" + device_id;
            if (err) *err = e;
            MYLOG_ERROR("[Edge:{}] Start 失败：{}", edge_id_, e);
            run_state_ = RunState::Ready;
            return false;
        }

        std::string dev_err;
        if (!dev->Start(*qit->second, &estop_, &dev_err)) {
            std::string e = "device.Start failed: device_id=" + device_id + ", err=" + dev_err;
            if (err) *err = e;
            MYLOG_ERROR("[Edge:{}] Start 失败：{}", edge_id_, e);
            run_state_ = RunState::Ready;
            return false;
        }

        MYLOG_INFO("[Edge:{}] device.Start 成功：device_id={}, queue={}", edge_id_, device_id, qit->second->Name());
    }

    // 若配置了 vehicle，则在 Start 时初始化 vehicle 连接
    if (vehicle_) {
        std::string conn = cfg_.value("connection_url", std::string());
        if (!conn.empty()) {
            if (!vehicle_->Init(conn)) {
                std::string e = "vehicle Init failed for connection_url=" + conn;
                if (err) *err = e;
                MYLOG_ERROR("[Edge:{}] {}", edge_id_, e);
                // 这里不立即失败全部 Start，但记录警告
            } else {
                MYLOG_INFO("[Edge:{}] vehicle 初始化成功 (connection_url={})", edge_id_, conn);
            }
        }
    }

    run_state_ = RunState::Running;
    MYLOG_INFO("[Edge:{}] Start 成功：run_state={}", edge_id_, ToString(run_state_.load()));

    StartStatusSnapshotThreadLocked();
    return true;
}

SubmitResult TUNAEdge::Submit(const my_data::RawCommand& cmd) {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);

    MYLOG_INFO("[Edge:{}] Submit 开始：command_id={}, source={}, payload={}", edge_id_, cmd.command_id, cmd.source, cmd.payload.dump());

    RunState rs = run_state_.load();
    if (rs != RunState::Running) {
        auto r = MakeResult(SubmitCode::NotRunning, "edge is not running, run_state=" + ToString(rs), cmd);
        MYLOG_WARN("[Edge:{}] Submit 拒绝：{}", edge_id_, r.toString());
        return r;
    }

    if (estop_.load() && !allow_queue_when_estop_) {
        std::string reason;
        {
            std::lock_guard<std::mutex> lk2(estop_mu_);
            reason = estop_reason_;
        }
        auto r = MakeResult(SubmitCode::EStop, reason.empty() ? "estop active" : ("estop active: " + reason), cmd);
        MYLOG_WARN("[Edge:{}] Submit 拒绝(EStop)：{}", edge_id_, r.toString());
        return r;
    }

    if (!cmd.payload.is_object()) {
        auto r = MakeResult(SubmitCode::InvalidCommand, "payload must be object", cmd);
        MYLOG_ERROR("[Edge:{}] Submit 失败：{}", edge_id_, r.toString());
        return r;
    }

    std::string device_id = my_data::jsonutil::GetStringOr(cmd.payload, "device_id", "");
    if (device_id.empty()) {
        auto r = MakeResult(SubmitCode::InvalidCommand, "missing payload.device_id", cmd);
        MYLOG_ERROR("[Edge:{}] Submit 失败：{}", edge_id_, r.toString());
        return r;
    }

    auto dit = device_type_by_id_.find(device_id);
    if (dit == device_type_by_id_.end()) {
        auto r = MakeResult(SubmitCode::UnknownDevice, "unknown device_id=" + device_id, cmd, device_id);
        MYLOG_ERROR("[Edge:{}] Submit 失败：{}", edge_id_, r.toString());
        return r;
    }
    const std::string& type = dit->second;

    // 按需临时创建 normalizer（不再持有 map）
    std::string nerr;
    auto normalizer = my_control::MyControl::GetInstance().CreateNormalizer(type);
    if (!normalizer) {
        auto r = MakeResult(SubmitCode::InternalError, "normalizer create failed for type=" + type, cmd, device_id);
        MYLOG_ERROR("[Edge:{}] Submit 失败：{}", edge_id_, r.toString());
        return r;
    }

    auto maybe_task = normalizer->Normalize(cmd, edge_id_, &nerr);
    if (!maybe_task.has_value()) {
        auto r = MakeResult(SubmitCode::InvalidCommand,
                            nerr.empty() ? "normalize failed" : ("normalize failed: " + nerr),
                            cmd, device_id);
        MYLOG_ERROR("[Edge:{}] Submit 失败：{}", edge_id_, r.toString());
        return r;
    }

    my_data::Task task = *maybe_task;

    auto qit = queues_.find(device_id);
    if (qit == queues_.end() || !qit->second) {
        auto r = MakeResult(SubmitCode::InternalError, "queue missing for device_id=" + device_id, cmd, device_id, task.task_id);
        MYLOG_ERROR("[Edge:{}] Submit 失败：{}", edge_id_, r.toString());
        return r;
    }

    if (qit->second->IsShutdown()) {
        auto r = MakeResult(SubmitCode::QueueShutdown, "queue already shutdown", cmd, device_id, task.task_id);
        MYLOG_WARN("[Edge:{}] Submit 拒绝：{}", edge_id_, r.toString());
        return r;
    }

    qit->second->Push(task);
    std::int64_t qsize = static_cast<std::int64_t>(qit->second->Size());
    auto r = MakeResult(SubmitCode::Ok, "queued", cmd, device_id, task.task_id, qsize);
    MYLOG_INFO("[Edge:{}] Submit 成功：{}", edge_id_, r.toString());
    return r;
}

my_data::EdgeStatus TUNAEdge::GetStatusSnapshot() const {
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

    // 若 vehicle_ 存在，附带 vehicle 状态信息
    if (vehicle_) {
        MavVehicleStatus myMavVehicleStatus = vehicle_->GetStatus();
        // s.extra["vehicle_connected"]        = myMavVehicleStatus.connected;
        // s.extra["vehicle_armed"]            = myMavVehicleStatus.armed;
        // s.extra["vehicle_mode"]             = myMavVehicleStatus.flight_mode;
        // s.extra["vehicle_battery_voltage"]  = myMavVehicleStatus.battery_voltage;
    }

    MYLOG_DEBUG("[Edge:{}] GetStatusSnapshot：devices={}, pending_total={}, running_total={}, estop={}",
                edge_id_, s.devices.size(), s.tasks_pending_total, s.tasks_running_total, s.estop_active);
    return s;
}

void TUNAEdge::SetEStop(bool active, const std::string& reason) {
    {
        std::unique_lock<std::shared_mutex> lk(rw_mutex_);
        estop_.store(active);
        {
            std::lock_guard<std::mutex> lk2(estop_mu_);
            estop_reason_ = reason;
        }
    }
    MYLOG_WARN("[Edge:{}] SetEStop：active={}, reason={}", edge_id_, active ? "true" : "false", reason);
    // 若激活 estop，尝试立即 disarm vehicle
    if (active && vehicle_) {
        vehicle_->Disarm();
        MYLOG_WARN("[Edge:{}] EStop 激活，已尝试 vehicle Disarm", edge_id_);
    }
}

void TUNAEdge::StartStatusSnapshotThreadLocked() {
    if (!status_snapshot_enable_) {
        MYLOG_INFO("[Edge:{}] StatusSnapshotThread disabled by cfg", edge_id_);
        return;
    }

    if (!my_db::MyDB::GetInstance().IsInitialized()) {
        MYLOG_WARN("[Edge:{}] StatusSnapshotThread not started: MyDB not initialized", edge_id_);
        return;
    }

    if (snapshot_thread_.joinable()) {
        MYLOG_WARN("[Edge:{}] StatusSnapshotThread already running", edge_id_);
        return;
    }

    snapshot_stop_.store(false);
    MYLOG_INFO("[Edge:{}] StatusSnapshotThread start: interval_ms={}", edge_id_, status_snapshot_interval_ms_);
    snapshot_thread_ = std::thread(&TUNAEdge::StatusSnapshotLoop, this);
}

void TUNAEdge::StopStatusSnapshotThreadLocked() {
    snapshot_stop_.store(true);
    if (snapshot_thread_.joinable()) {
        MYLOG_INFO("[Edge:{}] StatusSnapshotThread join...", edge_id_);
        snapshot_thread_.join();
        MYLOG_INFO("[Edge:{}] StatusSnapshotThread stopped", edge_id_);
    }
}

void TUNAEdge::StatusSnapshotLoop() {
    MYLOG_INFO("[Edge:{}] StatusSnapshotLoop enter", edge_id_);
    while (!snapshot_stop_.load()) {
        my_data::EdgeStatus st = GetStatusSnapshot();

        std::string err;
        auto& repo = my_db::demo::StatusRepository::GetInstance();

        if (!repo.InsertEdgeSnapshot(st, &err)) {
            MYLOG_ERROR("[Edge:{}] StatusSnapshot: InsertEdgeSnapshot failed: {}", edge_id_, err);
        } else {
            MYLOG_DEBUG("[Edge:{}] StatusSnapshot: edge snapshot inserted", edge_id_);
        }

        for (const auto& [device_id, ds] : st.devices) {
            std::string e2;
            if (!repo.InsertDeviceSnapshot(edge_id_, ds, &e2)) {
                MYLOG_ERROR("[Edge:{}] StatusSnapshot: InsertDeviceSnapshot failed: device_id={}, err={}", edge_id_, device_id, e2);
            }
        }

        int interval = status_snapshot_interval_ms_;
        if (interval < 200) interval = 200;
        std::this_thread::sleep_for(std::chrono::milliseconds(interval));
    }
    MYLOG_WARN("[Edge:{}] StatusSnapshotLoop exit", edge_id_);
}

void TUNAEdge::Shutdown() {
    std::unique_lock<std::shared_mutex> lk(rw_mutex_);

    RunState rs = run_state_.load();
    if (rs == RunState::Stopped || rs == RunState::Stopping) return;

    MYLOG_WARN("[Edge:{}] Shutdown 开始：run_state={}", edge_id_, ToString(rs));
    run_state_ = RunState::Stopping;

    StopStatusSnapshotThreadLocked();

    for (auto& [device_id, dev] : devices_) {
        if (!dev) continue;
        MYLOG_WARN("[Edge:{}] Shutdown: device.Stop device_id={}", edge_id_, device_id);
        dev->Stop();
    }

    for (auto& [device_id, q] : queues_) {
        if (!q) continue;
        MYLOG_WARN("[Edge:{}] Shutdown: queue.Shutdown device_id={}, queue={}", edge_id_, device_id, q->Name());
        q->Shutdown();
    }

    for (auto& [device_id, dev] : devices_) {
        if (!dev) continue;
        MYLOG_WARN("[Edge:{}] Shutdown: device.Join device_id={}", edge_id_, device_id);
        dev->Join();
    }

    // 停止并断开 vehicle（如存在）
    if (vehicle_) {
        MYLOG_WARN("[Edge:{}] Shutdown: 停止 vehicle 连接", edge_id_);
        vehicle_->Stop();
        vehicle_.reset();
    }

    devices_.clear();
    queues_.clear();
    device_type_by_id_.clear();

    run_state_ = RunState::Stopped;
    MYLOG_WARN("[Edge:{}] Shutdown 完成：run_state={}", edge_id_, ToString(run_state_.load()));
}

void TUNAEdge::ShowAnalyzeInitArgs(const nlohmann::json& cfg) const {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    try {
        MYLOG_INFO("[Edge:{}] ShowAnalyzeInitArgs 开始：cfg={}", edge_id_, cfg.dump(4));
        std::string cfg_edge_id = cfg.value("edge_id", std::string("<none>"));
        std::string cfg_version = cfg.value("version", std::string("<none>"));
        bool cfg_allow_queue_when_estop = cfg.value("allow_queue_when_estop", false);
        int devices_count = 0;
        if (cfg.contains("devices") && cfg["devices"].is_array()) {
            devices_count = static_cast<int>(cfg["devices"].size());
        }
        MYLOG_INFO("[Edge:{}] Parsed Init Args: edge_id={}, version={}, allow_queue_when_estop={}, devices_count={}",
                   edge_id_, cfg_edge_id, cfg_version, cfg_allow_queue_when_estop, devices_count);
        if (devices_count > 0) {
            for (size_t i = 0; i < cfg["devices"].size(); ++i) {
                const auto& d = cfg["devices"][i];
                std::string did = d.value("device_id", std::string("<none>"));
                std::string dtype = d.value("type", std::string("<none>"));
                std::string dname = d.value("name", std::string("<none>"));
                MYLOG_INFO("[Edge:{}] Device[{}] device_id={}, type={}, name={}", edge_id_, i, did, dtype, dname);
            }
        } else {
            MYLOG_WARN("[Edge:{}] ShowAnalInitArgs: no devices found in cfg", edge_id_);
        }
    } catch (const std::exception& e) {
        MYLOG_ERROR("[Edge:{}] ShowAnalInitArgs 捕获异常: {}", edge_id_, e.what());
    } catch (...) {
        MYLOG_ERROR("[Edge:{}] ShowAnalInitArgs 捕获未知异常", edge_id_);
    }
}

nlohmann::json TUNAEdge::DumpInternalInfo() const {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    nlohmann::json edgeInfo = nlohmann::json::object();

    edgeInfo["edge_id"] = edge_id_;
    edgeInfo["edge_type"] = edge_type_;
    edgeInfo["version"] = version_;
    edgeInfo["run_state"] = ToString(run_state_.load());
    edgeInfo["estop_active"] = estop_.load();

    nlohmann::json queuesJson = nlohmann::json::object();
    for (const auto& [device_id, queue] : queues_) {
        if (!queue) continue;
        nlohmann::json queueInfo;
        queueInfo["name"] = queue->Name();
        queueInfo["size"] = queue->Size();
        queuesJson[device_id] = queueInfo;
    }
    edgeInfo["task-queues"] = queuesJson;

    nlohmann::json devicesJson = nlohmann::json::object();
    for (const auto& [device_id, dev] : devices_) {
        if (!dev) continue;
        nlohmann::json devInfo;
        devicesJson[device_id] = devInfo;
    }
    edgeInfo["devices"] = devicesJson;

    nlohmann::json normalizersJson = nlohmann::json::object();
    // persistent normalizers 已移除，保留此键为空以兼容上层显示
    edgeInfo["normalizers"] = normalizersJson;

    if (vehicle_) {
        MavVehicleStatus vs = vehicle_->GetStatus();
        edgeInfo["vehicle"] = {
            {"connected", vs.connected},
            {"armed", vs.armed},
            {"flight_mode", vs.flight_mode},
            {"battery_voltage", vs.battery_voltage}
        };
    }

    return edgeInfo;
}

bool TUNAEdge::AppendJsonTask(const nlohmann::json& task) {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    try {
        std::string device_id = my_data::jsonutil::GetStringOr(task, "device_id", "");
        if (device_id.empty()) {
            MYLOG_ERROR("[Edge:{}] AppendTask 失败：缺少 device_id", edge_id_);
            return false;
        } else {
            MYLOG_INFO("[Edge:{}] AppendTask to device_id={}", edge_id_, device_id);
        }
        my_data::Task t = my_data::Task::fromJson(task);
        bool result = this->AppendTaskToTargetTaskQueue(device_id, t);
        return result;
    } catch (const std::exception& e) {
        MyLog::Error("向 Edge 添加任务时发生异常: " + std::string(e.what()));
        return false;
    }
}

bool TUNAEdge::AppendTask(const Task& task) {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    try {
        std::string device_id = task.device_id;
        if (device_id.empty()) {
            MYLOG_ERROR("[Edge:{}] AppendTask 失败：缺少 device_id", edge_id_);
            return false;
        } else {
            MYLOG_INFO("[Edge:{}] AppendTask to device_id={}", edge_id_, device_id);
        }
        bool result = this->AppendTaskToTargetTaskQueue(device_id, task);
        return result;
    } catch (const std::exception& e) {
        MyLog::Error("向 Edge 添加任务时发生异常: " + std::string(e.what()));
        return false;
    }
}

bool TUNAEdge::AppendTaskToTargetTaskQueue(const my_data::DeviceId& device_id, const Task& task) {
    bool result = false;
    size_t max_task_queue_size = 1000;
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    try {
        auto it = queues_.find(device_id);
        if (it == queues_.end() || !it->second) {
            MYLOG_ERROR("[Edge:{}] AppendTaskToTargetTaskQueue 失败：找不到 device_id={} 的任务队列", edge_id_, device_id);
            return result;
        } else {
            MYLOG_INFO("[Edge:{}] AppendTaskToTargetTaskQueue 找到 device_id={} 的任务队列", edge_id_, device_id);
        }
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

my_data::EdgeId TUNAEdge::Id() const {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    return edge_id_;
}

std::string TUNAEdge::EdgeType() const {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    return edge_type_;
}

} // namespace my_edge::demo