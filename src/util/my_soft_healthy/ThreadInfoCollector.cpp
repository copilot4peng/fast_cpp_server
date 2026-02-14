#include "ThreadInfoCollector.h"
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <sched.h>

#include "MyLog.h"

using namespace std;

namespace MySoftHealthy {

static string read_file_all(const string& path) {
  ifstream ifs(path, ios::in | ios::binary);
  if (!ifs) return {};
  ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

static string read_file_first_line(const string& path) {
  ifstream ifs(path);
  if (!ifs) return {};
  string line;
  getline(ifs, line);
  return line;
}

static string trim(string s) {
  auto not_space = [](unsigned char c){ return !isspace(c); };
  s.erase(s.begin(), find_if(s.begin(), s.end(), not_space));
  s.erase(find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

vector<int> ThreadInfoCollector::list_tids(int pid) const {
  vector<int> tids;
  string dir = "/proc/" + to_string(pid) + "/task";
  DIR* d = opendir(dir.c_str());
  if (!d) return tids;
  while (auto* ent = readdir(d)) {
    string name = ent->d_name;
    if (name == "." || name == "..") continue;
    bool digits = all_of(name.begin(), name.end(), [](char c){ return isdigit((unsigned char)c); });
    if (!digits) continue;
    try { tids.push_back(stoi(name)); } catch(...) {}
  }
  closedir(d);
  sort(tids.begin(), tids.end());
  return tids;
}

bool ThreadInfoCollector::read_task_stat(int pid, int tid,
                        uint64_t& out_utime, uint64_t& out_stime,
                        char& out_state, int& out_priority, int& out_nice,
                        int& out_processor, string& out_comm) const {
  string path = "/proc/" + to_string(pid) + "/task/" + to_string(tid) + "/stat";
  string content = read_file_all(path);
  if (content.empty()) return false;

  size_t lparen = content.find('(');
  size_t rparen = content.rfind(')');
  if (lparen == string::npos || rparen == string::npos || rparen <= lparen) return false;

  out_comm = content.substr(lparen + 1, rparen - lparen - 1);
  string rest = content.substr(rparen + 1);
  rest = trim(rest);
  istringstream iss(rest);
  vector<string> toks;
  string tok;
  while (iss >> tok) toks.push_back(tok);

  auto get_field = [&](int field_no)->optional<string> {
    int idx = field_no - 3;
    if (idx < 0 || idx >= (int)toks.size()) return nullopt;
    return toks[idx];
  };

  if (auto s = get_field(3)) out_state = (*s)[0];
  if (auto s = get_field(14)) try { out_utime = stoull(*s); } catch(...) { out_utime = 0; }
  if (auto s = get_field(15)) try { out_stime = stoull(*s); } catch(...) { out_stime = 0; }
  if (auto s = get_field(18)) try { out_priority = stoi(*s); } catch(...) { out_priority = 0; }
  if (auto s = get_field(19)) try { out_nice = stoi(*s); } catch(...) { out_nice = 0; }
  if (auto s = get_field(39)) try { out_processor = stoi(*s); } catch(...) { out_processor = -1; }

  return true;
}

static string policy_to_string(int pol) {
  switch (pol) {
    case SCHED_OTHER: return "OTHER";
    case SCHED_FIFO: return "FIFO";
    case SCHED_RR: return "RR";
#ifdef SCHED_BATCH
    case SCHED_BATCH: return "BATCH";
#endif
#ifdef SCHED_IDLE
    case SCHED_IDLE: return "IDLE";
#endif
#ifdef SCHED_DEADLINE
    case SCHED_DEADLINE: return "DEADLINE";
#endif
    default: return "UNKNOWN";
  }
}

static string get_thread_policy_best_effort(int tid) {
  int pol = sched_getscheduler(tid);
  if (pol == -1) return "UNKNOWN";
  return policy_to_string(pol);
}

vector<ThreadSnapshot> ThreadInfoCollector::collect_topn_threads(int pid,
    const unordered_map<int, uint64_t>* prev_tid_ticks,
    const SoftHealthMonitorConfig& cfg,
    uint64_t prev_host_total,
    uint64_t curr_host_total,
    int host_num_cpus) const {

  vector<int> tids = list_tids(pid);
  if (tids.empty()) return {};

  struct Light {
    int tid;
    uint64_t ticks;
    uint64_t delta;
    char state;
    int priority;
    int nice;
    int processor;
    string comm;
  };

  vector<Light> lights;
  lights.reserve(tids.size());

  for (int tid : tids) {
    uint64_t ut = 0, st = 0;
    char state = '?';
    int prio = 0, nice = 0, proc = -1;
    string comm;
    if (!read_task_stat(pid, tid, ut, st, state, prio, nice, proc, comm)) continue;
    uint64_t ticks = ut + st;
    uint64_t prev = 0;
    if (prev_tid_ticks) {
      auto it = prev_tid_ticks->find(tid);
      if (it != prev_tid_ticks->end()) prev = it->second;
    }
    uint64_t delta = (ticks >= prev) ? (ticks - prev) : 0;
    lights.push_back({tid, ticks, delta, state, prio, nice, proc, comm});
  }

  // 排序
  sort(lights.begin(), lights.end(), [](const Light& a, const Light& b){
    if (a.delta != b.delta) return a.delta > b.delta;
    return a.ticks > b.ticks;
  });

  int topn = cfg.threads_topn;
  if (topn == 0) return {};
  if (topn < 0) topn = (int)lights.size();
  if (topn > (int)lights.size()) topn = (int)lights.size();

  uint64_t host_delta = (curr_host_total >= prev_host_total) ? (curr_host_total - prev_host_total) : 0;

  vector<ThreadSnapshot> out;
  out.reserve(topn);
  for (int i = 0; i < topn; ++i) {
    const auto& L = lights[i];
    ThreadSnapshot t;
    t.tid = L.tid;
    t.name = L.comm.empty() ? read_file_first_line("/proc/" + to_string(pid) + "/task/" + to_string(L.tid) + "/comm") : L.comm;
    t.state = L.state;
    t.priority = L.priority;
    t.nice = L.nice;
    t.policy = get_thread_policy_best_effort(L.tid);
    t.utime_ticks = 0; t.utime_ticks = L.ticks - L.delta; // approximation not used elsewhere
    t.utime_ticks = L.ticks; // store absolute
    t.stime_ticks = 0; t.stime_ticks = 0; // we stored combined into utime+stime in ticks (but fields exist separately earlier)
    // better fill utime/stime separately if needed; simplified here

    if (host_delta > 0) {
      double pct_machine = (double)L.delta / (double)host_delta * 100.0;
      t.cpu_pct_machine = pct_machine;
      t.cpu_pct_core = pct_machine * host_num_cpus;
    }

    // 线程级 ctx/io（若配置开启）
    if (cfg.include_ctx_switches) {
      string status_txt = read_file_all("/proc/" + to_string(pid) + "/task/" + to_string(L.tid) + "/status");
      if (!status_txt.empty()) {
        istringstream iss(status_txt);
        string line;
        while (getline(iss, line)) {
          if (line.rfind("voluntary_ctxt_switches:", 0) == 0) {
            auto v = trim(line.substr(strlen("voluntary_ctxt_switches:")));
            try { t.voluntary_ctxt_switches = stoull(v); } catch(...) {}
          } else if (line.rfind("nonvoluntary_ctxt_switches:", 0) == 0) {
            auto v = trim(line.substr(strlen("nonvoluntary_ctxt_switches:")));
            try { t.nonvoluntary_ctxt_switches = stoull(v); } catch(...) {}
          }
        }
      }
    }

    if (cfg.include_thread_io) {
      string io_txt = read_file_all("/proc/" + to_string(pid) + "/task/" + to_string(L.tid) + "/io");
      if (!io_txt.empty()) {
        istringstream iss(io_txt);
        string line;
        while (getline(iss, line)) {
          auto pos = line.find(':');
          if (pos == string::npos) continue;
          string key = trim(line.substr(0, pos));
          string val = trim(line.substr(pos+1));
          try {
            uint64_t v = stoull(val);
            if (key == "rchar") t.rchar = v;
            else if (key == "wchar") t.wchar = v;
            else if (key == "read_bytes") t.read_bytes = v;
            else if (key == "write_bytes") t.write_bytes = v;
          } catch(...) {}
        }
      }
    }

    out.push_back(std::move(t));
  }

  return out;
}

}; // namespace MySoftHealthy