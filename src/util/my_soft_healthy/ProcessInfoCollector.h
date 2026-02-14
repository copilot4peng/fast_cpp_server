#pragma once
// 进程级采集器（ProcessInfoCollector）
// 负责：解析 /proc，列出 pid，解析 stat/cmdline/status/io，构建 pid->ppid map，构建进程子树
#include "SoftHealthSnapshot.h"
#include "SoftHealthMonitorConfig.h"
#include <string>
#include <unordered_map>
#include <vector>


namespace MySoftHealthy {
class ProcessInfoCollector {
public:
  ProcessInfoCollector() = default;
  ~ProcessInfoCollector() = default;

  // 列出系统中所有 pid（数字目录）
  std::vector<int> list_all_pids() const;

  // 读取 /proc/<pid>/stat，返回是否成功，并填充进程的累积字段
  bool read_proc_stat(int pid, ProcessSnapshot& out) const;

  // 读取 /proc/<pid>/cmdline
  std::string read_cmdline(int pid) const;

  // 读取 /proc/<pid>/status 并从中解析内存与上下文切换字段
  void read_status_fields(int pid, ProcessSnapshot& out, const SoftHealthMonitorConfig& cfg) const;

  // 读取 /proc/<pid>/io 并填充 io 字段（若配置允许）
  void read_proc_io(int pid, ProcessSnapshot& out) const;

  // 统计 /proc/<pid>/task 下的线程数
  uint32_t count_threads(int pid) const;

  // 统计进程打开的 fd 数
  uint32_t count_open_fds(int pid) const;

  // 构建 pid->ppid map 与 ppid->children map（扫描所有 pid）
  void build_pid_maps(std::unordered_map<int,int>& pid_to_ppid,
                      std::unordered_map<int, std::vector<int>>& ppid_children) const;

  // 从 roots 构建子树 pid 列表（包含 roots）
  std::vector<int> collect_subtree(const std::vector<int>& roots,
                                   const std::unordered_map<int, std::vector<int>>& ppid_children) const;

  // 为多个 pid 采集基本 ProcessSnapshot（不包含 threads 详细），并填充到 snap->processes 与 pid_tid_ticks (仅 tid map 空)
  void collect_processes_basic(SoftHealthSnapshot& snap, const SoftHealthMonitorConfig& cfg) const;
};

} // namespace MySoftHealthy