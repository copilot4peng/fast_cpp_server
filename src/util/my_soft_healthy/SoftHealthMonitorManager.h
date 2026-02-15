#pragma once
// Manager：统一调度、后台 loop、发布 snapshot、getData 接口
// 使用 atomic shared_ptr<const SoftHealthSnapshot> 发布不可变快照，保证并发读取安全
#include "SoftHealthMonitorConfig.h"
#include "SoftHealthSnapshot.h"
#include "ProcessInfoCollector.h"
#include "ThreadInfoCollector.h"
#include "ResourceUsageAnalyzer.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>


namespace MySoftHealthy {
class SoftHealthMonitorManager {
public:
  // 单例访问接口：首次调用用 cfg 初始化，后续调用忽略传入 cfg
  static SoftHealthMonitorManager& getInstance() {
    static SoftHealthMonitorManager instance;
    return instance;
  }

  ~SoftHealthMonitorManager();

  // 禁用拷贝/移动
  SoftHealthMonitorManager(const SoftHealthMonitorManager&) = delete;
  SoftHealthMonitorManager& operator=(const SoftHealthMonitorManager&) = delete;
  SoftHealthMonitorManager(SoftHealthMonitorManager&&) = delete;
  SoftHealthMonitorManager& operator=(SoftHealthMonitorManager&&) = delete;

  // 启动后台 loop（非阻塞）
  void start();

  // 停止并等待退出
  void stop();

  void init(const SoftHealthMonitorConfig& cfg) {
    applyConfig(cfg);
  }

  // 立即触发一次采样并返回快照（阻塞）
  std::shared_ptr<const SoftHealthSnapshot> refresh_now();

  // 非阻塞获取当前快照（shared_ptr 保证线程安全）
  std::shared_ptr<const SoftHealthSnapshot> getData() const;

  // 更新配置（线程安全，下一轮生效）
  void applyConfig(const SoftHealthMonitorConfig& cfg);

private:
  explicit SoftHealthMonitorManager();

  void loop();

private:
  SoftHealthMonitorConfig     cfg_;
  ProcessInfoCollector        proc_col_;
  ThreadInfoCollector         thread_col_;
  ResourceUsageAnalyzer       analyzer_;

  // 使用普通的 shared_ptr 存储当前 snapshot，并通过 std::atomic_store / std::atomic_load
  // 保证并发替换/读取的原子性（避免使用 std::atomic<std::shared_ptr<...>>，
  // 因为某些 libstdc++ 实现对非平凡类型不支持作为模板参数）。
  std::shared_ptr<const SoftHealthSnapshot> current_snapshot_;
  std::shared_ptr<SoftHealthSnapshot>       prev_snapshot_; // 用于 delta 计算（仅管理器内部）

  std::thread                     worker_;
  mutable std::mutex              mtx_;
  std::condition_variable_any     cv_;
  bool                            running_ = false;
};

} // namespace MySoftHealthy