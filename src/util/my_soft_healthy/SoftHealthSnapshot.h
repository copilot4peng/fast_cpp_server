#pragma once
// Snapshot 类型定义（用于 Manager 发布、供外界读取）
// 包含进程、线程和 Host 基线数据
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace MySoftHealthy {
struct ThreadSnapshot {
  int tid = 0;
  std::string name;
  char state = '?';
  int priority = 0;
  int nice = 0;
  std::string policy = "UNKNOWN";

  // 累积值（来自 /proc stat）
  uint64_t utime_ticks = 0;
  uint64_t stime_ticks = 0;

  // 差分计算后填充
  double cpu_pct_machine = 0.0;
  double cpu_pct_core = 0.0;

  // 可选字段
  std::optional<uint64_t> rchar;
  std::optional<uint64_t> wchar;
  std::optional<uint64_t> read_bytes;
  std::optional<uint64_t> write_bytes;

  std::optional<uint64_t> voluntary_ctxt_switches;
  std::optional<uint64_t> nonvoluntary_ctxt_switches;
};

struct ProcessSnapshot {
  int pid = 0;
  uint64_t start_time_ticks = 0; // 用于防止 PID 复用
  int ppid = -1;
  std::string name;
  std::string cmdline;
  char state = '?';
  uint32_t threads_count = 0;

  // 累积值
  uint64_t utime_ticks = 0;
  uint64_t stime_ticks = 0;

  // 差分后
  double cpu_pct_machine = 0.0;
  double cpu_pct_core = 0.0;

  // memory
  uint64_t vm_rss_bytes = 0;
  uint64_t vm_size_bytes = 0;

  // io 累积
  std::optional<uint64_t> io_rchar;
  std::optional<uint64_t> io_wchar;
  std::optional<uint64_t> io_read_bytes;
  std::optional<uint64_t> io_write_bytes;

  std::optional<uint64_t> voluntary_ctxt_switches;
  std::optional<uint64_t> nonvoluntary_ctxt_switches;

  // 衍生字段
  std::vector<int> children;              // 直接子进程 pid 列表
  std::vector<ThreadSnapshot> top_threads; // topN 线程快照
};

struct HostSnapshot {
  int num_cpus = 1;
  long clk_tck = 100;
  uint64_t total_cpu_jiffies = 0;
};

struct SoftHealthSnapshot {
  std::chrono::system_clock::time_point ts;
  HostSnapshot host;

  // target info（便于上层回显）
  std::optional<int> target_pid;
  std::optional<std::string> target_name;
  std::optional<std::string> target_cmdline_regex;

  std::vector<int> roots; // root pid 列表

  // pid -> ProcessSnapshot
  std::unordered_map<int, ProcessSnapshot> processes;

  // 为了准确计算 delta，保留 pid->(tid->ticks) 的映射（在发布 snapshot 时填充）
  std::unordered_map<int, std::unordered_map<int, uint64_t>> pid_tid_ticks;
};


void printSoftHealthSnapshotAsJsonZH(const MySoftHealthy::SoftHealthSnapshot& snap);
void printSoftHealthSnapshotAsJson(const MySoftHealthy::SoftHealthSnapshot& snap);

} // namespace MySoftHealthy