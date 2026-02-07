#pragma once

#include "IEdge.h"
#include "MyMavVehicle.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <thread>
#include <atomic>

#include "MyData.h"
#include "MyLog.h"
#include "IDevice.h"
#include "ICommandNormalizer.h"
#include "TaskQueue.h"

namespace my_edge::demo {

using namespace my_data;

/**
 * @brief TUNAEdge（第三版）
 * - 不再持有 persistent normalizers_by_type_
 * - 在 Submit 时按需创建 normalizer（临时），使用后销毁
 * - 其余成员与 UUVEdge 保持一致（设备、队列、snapshot、vehicle 等）
 */
class TUNAEdge : public IEdge {
public:
    TUNAEdge();
    explicit TUNAEdge(const nlohmann::json& cfg, std::string* err = nullptr);
    ~TUNAEdge() override;

    // IEdge 接口实现
    bool Init(const nlohmann::json& cfg, std::string* err) override;
    bool Start(std::string* err) override;
    SubmitResult Submit(const my_data::RawCommand& cmd) override;
    my_data::EdgeStatus GetStatusSnapshot() const override;
    void SetEStop(bool active, const std::string& reason) override;
    void Shutdown() override;
    my_data::EdgeId Id() const override;
    std::string EdgeType() const override;
    void ShowAnalyzeInitArgs(const nlohmann::json& cfg) const override;
    nlohmann::json DumpInternalInfo() const override;
    bool AppendJsonTask(const nlohmann::json& task) override;
    bool AppendTask(const my_data::Task& task) override;

private:
    // 辅助函数
    std::string ToString(RunState s) const;
    SubmitResult MakeResult(SubmitCode code, const std::string& msg,
                            const my_data::RawCommand& cmd,
                            const my_data::DeviceId& device_id = "",
                            const my_data::TaskId& task_id = 0,
                            std::int64_t queue_size_after = 0) const;

    void StartStatusSnapshotThreadLocked();
    void StopStatusSnapshotThreadLocked();
    void StatusSnapshotLoop();

    bool AppendTaskToTargetTaskQueue(const my_data::DeviceId& device_id, const my_data::Task& task);

private:
    // 基本标识与配置
    my_data::EdgeId edge_id_{"tuna-default"};
    std::string edge_type_{"tuna"};
    std::string version_{"0.0"};
    nlohmann::json cfg_;
    std::atomic<RunState> run_state_{RunState::Initializing};
    std::int64_t boot_at_ms_{0};

    // EStop 控制
    std::atomic<bool> estop_{false};
    mutable std::mutex estop_mu_;
    std::string estop_reason_;
    bool allow_queue_when_estop_{false};

    // 设备/队列 管理（与 UUVEdge 一致）
    std::unordered_map<my_data::DeviceId, std::unique_ptr<my_device::IDevice>> devices_;
    std::unordered_map<my_data::DeviceId, std::unique_ptr<my_control::TaskQueue>> queues_;
    std::unordered_map<std::string, std::string> device_type_by_id_;

    mutable std::shared_mutex rw_mutex_;

    // Status snapshot
    bool status_snapshot_enable_{false};
    int status_snapshot_interval_ms_{5000};
    std::atomic<bool> snapshot_stop_{false};
    std::thread snapshot_thread_;

    // 内置 MyMavVehicle，用于与潜航器交互
    std::unique_ptr<MyMavVehicle> vehicle_;
};

} // namespace my_edge::demo