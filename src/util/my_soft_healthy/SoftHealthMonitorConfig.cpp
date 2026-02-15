#include "SoftHealthMonitorConfig.h"
// 目前配置类仅为 POD / 值语义，不需要实现额外逻辑。
// 如果需要从文件或 CLI 初始化，建议在外层 CLI 层构建并传入 Manager。

namespace MySoftHealthy {


void generateSampleConfigByJson(const nlohmann::json& j, SoftHealthMonitorConfig& cfg) {
  if (j.contains("interval_seconds"))                                               cfg.interval_seconds      = j["interval_seconds"].get<int>();
  if (j.contains("threads_topn"))                                                   cfg.threads_topn          = j["threads_topn"].get<int>();
  if (j.contains("include_proc_io"))                                                cfg.include_proc_io       = j["include_proc_io"].get<bool>();
  if (j.contains("include_thread_io"))                                              cfg.include_thread_io     = j["include_thread_io"].get<bool>();
  if (j.contains("include_ctx_switches"))                                           cfg.include_ctx_switches  = j["include_ctx_switches"].get<bool>();
  if (j.contains("target_pid") && !j["target_pid"].is_null())                       cfg.target_pid            = j["target_pid"].get<int>();
  if (j.contains("target_name") && !j["target_name"].is_null())                     cfg.target_name           = j["target_name"].get<std::string>();
  if (j.contains("target_cmdline_regex") && !j["target_cmdline_regex"].is_null())   cfg.target_cmdline_regex  = j["target_cmdline_regex"].get<std::string>();
}



void ShowSoftHealthMonitorConfigAsJson(const SoftHealthMonitorConfig& cfg) {
  nlohmann::json j;
  j["interval_seconds"] = cfg.interval_seconds;
  j["threads_topn"] = cfg.threads_topn;
  j["include_proc_io"] = cfg.include_proc_io;
  j["include_thread_io"] = cfg.include_thread_io;
  j["include_ctx_switches"] = cfg.include_ctx_switches;
  if (cfg.target_pid) j["target_pid"] = *cfg.target_pid;
  if (cfg.target_name) j["target_name"] = *cfg.target_name;
  if (cfg.target_cmdline_regex) j["target_cmdline_regex"] = *cfg.target_cmdline_regex;

  MYLOG_INFO("当前 SoftHealthMonitorConfig 配置:\n{}", j.dump(4));
  std::cout << "当前 SoftHealthMonitorConfig 配置:\n" << j.dump(4) << std::endl;
}

}