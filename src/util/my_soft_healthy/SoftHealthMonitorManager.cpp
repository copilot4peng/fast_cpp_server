#include "SoftHealthMonitorManager.h"
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <memory>

#include "MyLog.h"

namespace MySoftHealthy {


using namespace std::chrono;

SoftHealthMonitorManager::SoftHealthMonitorManager() {
  // 使用 std::atomic_store 对 shared_ptr 做原子写入，初始化为空
  std::atomic_store(&current_snapshot_, std::shared_ptr<const SoftHealthSnapshot>());
}

SoftHealthMonitorManager::~SoftHealthMonitorManager() {
  stop();
}

void SoftHealthMonitorManager::start() {
  std::lock_guard<std::mutex> lk(mtx_);
  if (running_) return;
  running_ = true;
  worker_ = std::thread(&SoftHealthMonitorManager::loop, this);
  MYLOG_INFO("SoftHealthMonitorManager 启动，interval={} 秒", cfg_.interval_seconds);
}

void SoftHealthMonitorManager::stop() {
  {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!running_) return;
    running_ = false;
    cv_.notify_all();
  }
  if (worker_.joinable()) worker_.join();
  MYLOG_INFO("SoftHealthMonitorManager 已停止");
}

std::shared_ptr<const SoftHealthSnapshot> SoftHealthMonitorManager::refresh_now() {
  // 同步采样一次（构建 curr），计算 delta，发布并返回
  auto curr = std::make_shared<SoftHealthSnapshot>();
  curr->ts = system_clock::now();
  curr->host.num_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
  curr->host.clk_tck = sysconf(_SC_CLK_TCK);
  // host total jiffies
  curr->host.total_cpu_jiffies = 0;
  // 由 collector 填充 processes、roots 等
  proc_col_.collect_processes_basic(*curr, cfg_);

  // 线程相关：为确保 prev 有 pid_tid_ticks，先填充 curr->pid_tid_ticks
  for (auto &kv : curr->processes) {
    int pid = kv.first;
    // 将 tids 和 ticks 填入 pid_tid_ticks（两阶段：先读 ticks）
    ThreadInfoCollector tcol;
    auto tids = tcol.list_tids(pid);
    for (int tid : tids) {
      uint64_t ut=0, st=0; char stch='?'; int prio=0, nice=0, processor=-1;
      std::string comm;
      if (tcol.read_task_stat(pid, tid, ut, st, stch, prio, nice, processor, comm)) {
        curr->pid_tid_ticks[pid][tid] = ut + st;
      }
    }
  }

  // Now collect topN threads per process using prev map if available
  for (auto &kv : curr->processes) {
    int pid = kv.first;
    const std::unordered_map<int,uint64_t>* prev_tid_map_ptr = nullptr;
    if (prev_snapshot_) {
      auto it_prev = prev_snapshot_->pid_tid_ticks.find(pid);
      if (it_prev != prev_snapshot_->pid_tid_ticks.end()) prev_tid_map_ptr = &it_prev->second;
    }
    ThreadInfoCollector tcol;
    auto top_threads = tcol.collect_topn_threads(pid, prev_tid_map_ptr, cfg_, prev_snapshot_ ? prev_snapshot_->host.total_cpu_jiffies : 0,
                                                 curr->host.total_cpu_jiffies, curr->host.num_cpus);
    kv.second.top_threads = std::move(top_threads);
  }

  // 分析 delta：cpu_pct 等
  analyzer_.compute_deltas(prev_snapshot_, curr, cfg_.interval_seconds);

  // 发布 snapshot（原子替换）
  // 注意：current_snapshot_ 的类型是 std::shared_ptr<const SoftHealthSnapshot>
  // 因此需要传入相同类型的 shared_ptr。`curr` 是 std::shared_ptr<SoftHealthSnapshot>,
  // 这里转换为 const 版本后再原子存储。
  std::atomic_store(&current_snapshot_, std::static_pointer_cast<const SoftHealthSnapshot>(curr));

  // 更新 prev_snapshot_
  prev_snapshot_ = curr;

  MYLOG_INFO("完成一次同步采样，进程数={}，roots_count={}", curr->processes.size(), curr->roots.size());
  return std::atomic_load(&current_snapshot_);
}

std::shared_ptr<const SoftHealthSnapshot> SoftHealthMonitorManager::getData() const {
  return std::atomic_load(&current_snapshot_);
}

void SoftHealthMonitorManager::applyConfig(const SoftHealthMonitorConfig& cfg) {
  std::lock_guard<std::mutex> lk(mtx_);
  cfg_ = cfg;
  MYLOG_INFO("配置已更新：interval={} threads_topn={}", cfg_.interval_seconds, cfg_.threads_topn);
  cv_.notify_all();
}

void SoftHealthMonitorManager::loop() {
  auto next = steady_clock::now();
  while (true) {
    {
      std::unique_lock<std::mutex> lk(mtx_);
      if (!running_) break;
    }

    // 采样并发布（同步）
    try {
      refresh_now();
    } catch (const std::exception& e) {
      MYLOG_ERROR("周期采样异常：{}", e.what());
    }

    // 等待到下一个周期或停止
    next += seconds(cfg_.interval_seconds);
    std::unique_lock<std::mutex> lk(mtx_);
    if (!running_) break;
    cv_.wait_until(lk, next, [this](){ return !running_; });
    if (!running_) break;
  }
}

} // namespace MySoftHealthy