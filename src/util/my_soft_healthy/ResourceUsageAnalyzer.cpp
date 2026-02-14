#include "ResourceUsageAnalyzer.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include "MyLog.h"

using namespace std;

namespace MySoftHealthy {
    
void ResourceUsageAnalyzer::compute_deltas(const shared_ptr<SoftHealthSnapshot>& prev,
                                           const shared_ptr<SoftHealthSnapshot>& curr,
                                           int interval_seconds) {
  if (!curr) return;
  if (!prev) {
    MYLOG_INFO("首次采样，无法计算 CPU%%/IO 速率（下一次可计算）");
    return;
  }

  uint64_t prev_total = prev->host.total_cpu_jiffies;
  uint64_t curr_total = curr->host.total_cpu_jiffies;
  uint64_t host_delta = (curr_total >= prev_total) ? (curr_total - prev_total) : 0;

  for (auto& kv : curr->processes) {
    int pid = kv.first;
    ProcessSnapshot& p = kv.second;

    auto it_prev_proc = prev->processes.find(pid);
    if (it_prev_proc == prev->processes.end()) continue;
    const ProcessSnapshot& pp = it_prev_proc->second;

    // 防止 PID 复用（start time 必须相同）
    if (pp.start_time_ticks != p.start_time_ticks) continue;

    uint64_t prev_ticks = pp.utime_ticks + pp.stime_ticks;
    uint64_t curr_ticks = p.utime_ticks + p.stime_ticks;
    uint64_t delta = (curr_ticks >= prev_ticks) ? (curr_ticks - prev_ticks) : 0;

    if (host_delta > 0) {
      double pct_machine = (double)delta / (double)host_delta * 100.0;
      p.cpu_pct_machine = pct_machine;
      p.cpu_pct_core = pct_machine * curr->host.num_cpus;
    }

    // IO 速率（若都有 prev && curr）
    if (p.io_read_bytes && pp.io_read_bytes) {
      double dt = interval_seconds > 0 ? (double)interval_seconds : 1.0;
      uint64_t d_read = (p.io_read_bytes.value() >= pp.io_read_bytes.value()) ? (p.io_read_bytes.value() - pp.io_read_bytes.value()) : 0;
      uint64_t d_write = (p.io_write_bytes.value_or(0) >= pp.io_write_bytes.value_or(0)) ? (p.io_write_bytes.value_or(0) - pp.io_write_bytes.value_or(0)) : 0;
      // IO 速率暂不写回结构（可扩展）
      (void)d_read; (void)d_write;
    }

    // 线程 delta：使用 pid_tid_ticks 映射
    auto it_prev_tid_map = prev->pid_tid_ticks.find(pid);
    auto it_curr_tid_map = curr->pid_tid_ticks.find(pid);
    if (it_curr_tid_map == curr->pid_tid_ticks.end()) continue;
    const auto& curr_tid_map = it_curr_tid_map->second;
    unordered_map<int, uint64_t> prev_tid_map;
    if (it_prev_tid_map != prev->pid_tid_ticks.end()) prev_tid_map = it_prev_tid_map->second;

    // build list and fill thread cpu pct for top_threads
    vector<pair<int,uint64_t>> list; // tid -> delta
    for (const auto &tt : curr_tid_map) {
      int tid = tt.first;
      uint64_t curr_ticks_tid = tt.second;
      uint64_t prev_ticks_tid = 0;
      auto pit = prev_tid_map.find(tid);
      if (pit != prev_tid_map.end()) prev_ticks_tid = pit->second;
      uint64_t d = (curr_ticks_tid >= prev_ticks_tid) ? (curr_ticks_tid - prev_ticks_tid) : 0;
      list.emplace_back(tid, d);
    }
    sort(list.begin(), list.end(), [](auto &a, auto &b){
      if (a.second != b.second) return a.second > b.second;
      return a.first < b.first;
    });

    // 对 p.top_threads 填充 cpu_pct（匹配 tid）
    for (auto &tsnap : p.top_threads) {
      for (auto &entry : list) {
        if (entry.first == tsnap.tid) {
          uint64_t d = entry.second;
          if (host_delta > 0) {
            double pct_machine = (double)d / (double)host_delta * 100.0;
            tsnap.cpu_pct_machine = pct_machine;
            tsnap.cpu_pct_core = pct_machine * curr->host.num_cpus;
          }
          break;
        }
      }
    }
  }
}

} // namespace MySoftHealthy