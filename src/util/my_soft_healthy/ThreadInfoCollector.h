#pragma once
// 线程级采集器（ThreadInfoCollector）
// 两阶段采集：轻量采集 ticks -> 选 topN -> 采详细字段（comm/status/io）
#include "SoftHealthSnapshot.h"
#include "SoftHealthMonitorConfig.h"
#include <vector>
#include <unordered_map>

namespace MySoftHealthy {
class ThreadInfoCollector {
public:
  ThreadInfoCollector() = default;
  ~ThreadInfoCollector() = default;

  // 返回 pid 的所有 tid（task 目录）
  std::vector<int> list_tids(int pid) const;

  // 读取 /proc/<pid>/task/<tid>/stat（轻量，返回 utime+stime、state、priority、nice、processor、comm）
  bool read_task_stat(int pid, int tid,
                      uint64_t& out_utime, uint64_t& out_stime,
                      char& out_state, int& out_priority, int& out_nice,
                      int& out_processor, std::string& out_comm) const;

  // 对某 pid 采集 topN 线程（基于 prev_tid_ticks 可准确计算 delta），并返回 ThreadSnapshot 列表
  // prev_tid_ticks 可为空（首次采样）
  std::vector<ThreadSnapshot> collect_topn_threads(int pid,
                                                   const std::unordered_map<int, uint64_t>* prev_tid_ticks,
                                                   const SoftHealthMonitorConfig& cfg,
                                                   uint64_t prev_host_total,
                                                   uint64_t curr_host_total,
                                                   int host_num_cpus) const;
}; // class ThreadInfoCollector

} // namespace MySoftHealthy