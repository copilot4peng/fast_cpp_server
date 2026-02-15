#ifndef SOFT_HEALTH_MONITOR_CONFIG_H
#define SOFT_HEALTH_MONITOR_CONFIG_H

#include <iostream>
#include <sstream>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>
#include "MyLog.h"


namespace MySoftHealthy {

struct SoftHealthMonitorConfig {
  // 采样周期（秒），默认 5
  int interval_seconds = 5;

  // per-process thread topN，默认 20；0 表示不输出线程；-1 表示全部
  int threads_topn = 20;

  // 是否采集进程/线程 IO（默认采集进程 IO，线程 IO 可选）
  bool include_proc_io = true;
  bool include_thread_io = false;

  // 是否采集上下文切换统计（process/thread）
  bool include_ctx_switches = true;

  // 目标解析（互斥）：三选一
  std::optional<int>          target_pid;
  std::optional<std::string>  target_name;          // 匹配 /proc/<pid>/comm
  std::optional<std::string>  target_cmdline_regex; // regex 匹配 cmdline

  // 并发度 / 超时等（留作扩展/保护）
  int max_concurrent_probes = 4;
  int per_pid_timeout_ms = 2000;
};



void generateSampleConfigByJson(const nlohmann::json& j, SoftHealthMonitorConfig& cfg);

void ShowSoftHealthMonitorConfigAsJson(const SoftHealthMonitorConfig& cfg);


} // namespace MySoftHealthy

#endif // SOFT_HEALTH_MONITOR_CONFIG_H