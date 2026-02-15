#include "ProcessInfoCollector.h"
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <regex>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_set>

#include "MyLog.h"

namespace MySoftHealthy {

using namespace std;

static std::string read_file_first_line(const std::string& path) {
  ifstream ifs(path);
  if (!ifs) return {};
  std::string line;
  getline(ifs, line);
  return line;
}

static std::string read_file_all(const std::string& path) {
  ifstream ifs(path, ios::in | ios::binary);
  if (!ifs) return {};
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

static std::string trim(std::string s) {
  auto not_space = [](unsigned char c){ return !isspace(c); };
  s.erase(s.begin(), find_if(s.begin(), s.end(), not_space));
  s.erase(find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

vector<int> ProcessInfoCollector::list_all_pids() const {
  vector<int> pids;
  DIR* d = opendir("/proc");
  if (!d) return pids;
  while (auto* ent = readdir(d)) {
    std::string name = ent->d_name;
    if (name.empty()) continue;
    bool digits = all_of(name.begin(), name.end(), [](char c){ return isdigit((unsigned char)c); });
    if (!digits) continue;
    try { pids.push_back(stoi(name)); } catch(...) {}
  }
  closedir(d);
  sort(pids.begin(), pids.end());
  return pids;
}

bool ProcessInfoCollector::read_proc_stat(int pid, ProcessSnapshot& out) const {
  std::string path = "/proc/" + std::to_string(pid) + "/stat";
  std::string content = read_file_all(path);
  if (content.empty()) return false;

  size_t lparen = content.find('(');
  size_t rparen = content.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos || rparen <= lparen) return false;

  std::string pid_str = trim(content.substr(0, lparen));
  try { out.pid = stoi(pid_str); } catch(...) { out.pid = pid; }

  out.name = content.substr(lparen + 1, rparen - lparen - 1);
  std::string rest = content.substr(rparen + 1);
  rest = trim(rest);
  std::stringstream iss(rest);
  vector<std::string> toks;
  std::string tok;
  while (iss >> tok) toks.push_back(tok);

  auto get_field = [&](int field_no)->optional<std::string> {
    int idx = field_no - 3;
    if (idx < 0 || idx >= (int)toks.size()) return nullopt;
    return toks[idx];
  };

  if (auto s = get_field(3)) out.state = (*s)[0];
  if (auto s = get_field(4)) try { out.ppid = stoi(*s); } catch(...) {}
  if (auto s = get_field(14)) try { out.utime_ticks = stoull(*s); } catch(...) {}
  if (auto s = get_field(15)) try { out.stime_ticks = stoull(*s); } catch(...) {}
  if (auto s = get_field(22)) try { out.start_time_ticks = stoull(*s); } catch(...) {}
  if (auto s = get_field(39)) {
    try { out.state = out.state; (void)stoi(*s); } catch(...) {}
    // processor stored in thread-level reading if needed
  }

  return true;
}

std::string ProcessInfoCollector::read_cmdline(int pid) const {
  std::string path = "/proc/" + std::to_string(pid) + "/cmdline";
  std::string raw = read_file_all(path);
  if (raw.empty()) return {};
  for (char &c : raw) if (c == '\0') c = ' ';
  return trim(raw);
}

void ProcessInfoCollector::read_status_fields(int pid, ProcessSnapshot& out, const SoftHealthMonitorConfig& cfg) const {
  std::string path = "/proc/" + std::to_string(pid) + "/status";
  std::string txt = read_file_all(path);
  if (txt.empty()) return;
  std::stringstream iss(txt);
  std::string line;
  while (getline(iss, line)) {
    if (line.rfind("VmRSS:", 0) == 0) {
      auto v = trim(line.substr(6));
      std::stringstream vs(v);
      uint64_t kb = 0; vs >> kb;
      out.vm_rss_bytes = kb * 1024ULL;
    } else if (line.rfind("VmSize:", 0) == 0) {
      auto v = trim(line.substr(7));
      std::stringstream vs(v);
      uint64_t kb = 0; vs >> kb;
      out.vm_size_bytes = kb * 1024ULL;
    } else if (cfg.include_ctx_switches && line.rfind("voluntary_ctxt_switches:",0) == 0) {
      auto v = trim(line.substr(strlen("voluntary_ctxt_switches:")));
      try { out.voluntary_ctxt_switches = stoull(v); } catch(...) {}
    } else if (cfg.include_ctx_switches && line.rfind("nonvoluntary_ctxt_switches:",0) == 0) {
      auto v = trim(line.substr(strlen("nonvoluntary_ctxt_switches:")));
      try { out.nonvoluntary_ctxt_switches = stoull(v); } catch(...) {}
    }
  }
}

void ProcessInfoCollector::read_proc_io(int pid, ProcessSnapshot& out) const {
  std::string path = "/proc/" + std::to_string(pid) + "/io";
  std::string txt = read_file_all(path);
  if (txt.empty()) return;
  std::stringstream iss(txt);
  std::string line;
  while (getline(iss, line)) {
    auto pos = line.find(':');
    if (pos == std::string::npos) continue;
    std::string key = trim(line.substr(0, pos));
    std::string val = trim(line.substr(pos + 1));
    try {
      uint64_t v = stoull(val);
      if (key == "rchar") out.io_rchar = v;
      else if (key == "wchar") out.io_wchar = v;
      else if (key == "read_bytes") out.io_read_bytes = v;
      else if (key == "write_bytes") out.io_write_bytes = v;
    } catch(...) {}
  }
}

uint32_t ProcessInfoCollector::count_threads(int pid) const {
  std::string dir = "/proc/" + std::to_string(pid) + "/task";
  DIR* d = opendir(dir.c_str());
  if (!d) return 0;
  uint32_t cnt = 0;
  while (auto* ent = readdir(d)) {
    std::string name = ent->d_name;
    if (name == "." || name == "..") continue;
    bool digits = all_of(name.begin(), name.end(), [](char c){ return isdigit((unsigned char)c); });
    if (!digits) continue;
    ++cnt;
  }
  closedir(d);
  return cnt;
}

uint32_t ProcessInfoCollector::count_open_fds(int pid) const {
  std::string dir = "/proc/" + std::to_string(pid) + "/fd";
  DIR* d = opendir(dir.c_str());
  if (!d) return 0;
  uint32_t cnt = 0;
  while (auto* ent = readdir(d)) {
    std::string name = ent->d_name;
    if (name == "." || name == "..") continue;
    ++cnt;
  }
  closedir(d);
  return cnt;
}

void ProcessInfoCollector::build_pid_maps(unordered_map<int,int>& pid_to_ppid,
                                          unordered_map<int, vector<int>>& ppid_children) const {
  pid_to_ppid.clear();
  ppid_children.clear();
  auto pids = list_all_pids();
  for (int pid : pids) {
    ProcessSnapshot tmp;
    if (!read_proc_stat(pid, tmp)) continue;
    pid_to_ppid[pid] = tmp.ppid;
    ppid_children[tmp.ppid].push_back(pid);
  }
}

std::vector<int> ProcessInfoCollector::collect_subtree(const std::vector<int>& roots,
                                                  const std::unordered_map<int, std::vector<int>>& ppid_children) const {

  MYLOG_INFO("开始构建子树，root_count={}，ppid_children_count={}", roots.size(), ppid_children.size());
  std::vector<int> res;
  std::unordered_set<int> vis;
  std::vector<int> stack = roots;
  while (!stack.empty()) {
    int pid = stack.back(); stack.pop_back();
    if (vis.count(pid)) continue;
    vis.insert(pid);
    res.push_back(pid);
    auto it = ppid_children.find(pid);
    if (it != ppid_children.end()) {
      for (int c : it->second) stack.push_back(c);
    }
  }
  std::sort(res.begin(), res.end());
  return res;
}

void ProcessInfoCollector::collect_processes_basic(SoftHealthSnapshot& snap, const SoftHealthMonitorConfig& cfg) const {
  // 先构建 pid maps
  std::unordered_map<int,int>               pid_to_ppid;
  std::unordered_map<int, std::vector<int>> ppid_children;
  build_pid_maps(pid_to_ppid, ppid_children);
  MYLOG_INFO("ProcessInfoCollector: 构建 pid maps 完成，pid_count={}", pid_to_ppid.size());

  MYLOG_INFO("ProcessInfoCollector: 解析目标进程，配置 target_pid={} target_name={} target_cmdline_regex={}",
             (cfg.target_pid ? to_string(*cfg.target_pid) : "nullopt"),
             (cfg.target_name ? *cfg.target_name : "nullopt"),
             (cfg.target_cmdline_regex ? *cfg.target_cmdline_regex : "nullopt"));
  // resolve roots
  std::vector<int> roots;
  if (cfg.target_pid) roots.push_back(*cfg.target_pid);
  else {
    // 如果按 name 或 regex
    if (cfg.target_name) {
      auto pids = list_all_pids();
      for (int pid : pids) {
        std::string comm = read_file_first_line("/proc/" + std::to_string(pid) + "/comm");
        // std::cout << "target_name: pid=" << pid << " comm=" << comm << std::endl;
        if (comm == *cfg.target_name) {
          roots.push_back(pid);
          MYLOG_WARN("按 target_name 解析到 root 进程 pid={} comm={}", pid, comm);
        }
      }
    } else if (cfg.target_cmdline_regex) {
      std::regex re;
      try { re = std::regex(*cfg.target_cmdline_regex, std::regex::ECMAScript); }
      catch (const std::exception& e) {
        MYLOG_WARN("命令行正则编译失败：{}", e.what());
      }
      auto pids = list_all_pids();
      for (int pid : pids) {
        std::string cmd = read_file_all("/proc/" + std::to_string(pid) + "/cmdline");
        if (cmd.empty()) {
          continue;
        }
        // std::cout << "re: pid=" << pid << " cmd=" << cmd << std::endl;
        for (char &c : cmd) if (c == '\0') c = ' ';
        if (std::regex_search(cmd, re)) {
          roots.push_back(pid);
          MYLOG_WARN("按 target_cmdline_regex 解析到 root 进程 pid={} cmd={}", pid, cmd);
        }
      }
    }
  }

  snap.roots = roots;
  if (roots.empty()) {
    MYLOG_WARN("ProcessInfoCollector: 未解析到任何 root 进程");
    return;
  }

  auto subtree = collect_subtree(roots, ppid_children);
  for (int pid : subtree) {
    ProcessSnapshot ps;
    if (!read_proc_stat(pid, ps)) {
      MYLOG_DEBUG("read_proc_stat 失败 pid={}", pid);
      continue;
    }
    ps.cmdline = read_cmdline(pid);
    read_status_fields(pid, ps, cfg);
    if (cfg.include_proc_io) read_proc_io(pid, ps);
    ps.threads_count = count_threads(pid);
    // children 填写在 manager/后处理（因为要按 subtree 过滤）
    snap.processes[pid] = std::move(ps);
    // 建立 pid_tid_ticks 结构时会在 ThreadInfoCollector 处填充
  }

  // 填充 children（只保留 subtree 中的 child）
  MYLOG_INFO("ProcessInfoCollector: 基本进程信息采集完成，subtree_size={}", subtree.size());
  for (auto &kv : snap.processes) {
    int pid = kv.first;
    auto it = ppid_children.find(pid);
    if (it == ppid_children.end()) continue;
    for (int ch : it->second) {
      if (snap.processes.find(ch) != snap.processes.end()) {
        kv.second.children.push_back(ch);
      }
      MYLOG_INFO("ProcessInfoCollector: pid={} child={}", pid, ch);
    }
    sort(kv.second.children.begin(), kv.second.children.end());
  }
}

}; // namespace MySoftHealthy