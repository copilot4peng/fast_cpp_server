#include "SoftHealthSnapshot.h"
// 目前不需要额外函数；此 cpp 文件预留给未来扩展（JSON 输出、序列化等）。
#include <iostream>
#include <nlohmann/json.hpp>
#include "MyLog.h"

namespace MySoftHealthy {

void printSoftHealthSnapshotAsJson(const MySoftHealthy::SoftHealthSnapshot& snap) {
    using namespace std::chrono;
    auto uptime = duration_cast<seconds>(snap.ts.time_since_epoch()).count();
    nlohmann::json j;

    j["timestamp"] = uptime;
    j["host"] = {
        {"num_cpus", snap.host.num_cpus},
        {"clk_tck", snap.host.clk_tck},
        {"total_cpu_jiffies", snap.host.total_cpu_jiffies}
    };

    j["target"] = {
        {"pid", snap.target_pid ? nlohmann::json(*snap.target_pid) : nlohmann::json(nullptr)},
        {"name", snap.target_name ? nlohmann::json(*snap.target_name) : nlohmann::json(nullptr)},
        {"cmdline_regex", snap.target_cmdline_regex ? nlohmann::json(*snap.target_cmdline_regex) : nlohmann::json(nullptr)}
    };

    nlohmann::json processes_json = nlohmann::json::object();
    for (const auto& [pid, proc] : snap.processes) {
        processes_json[std::to_string(pid)] = {
            {"pid", proc.pid},
            {"start_time_ticks", proc.start_time_ticks},
            {"ppid", proc.ppid},
            {"name", proc.name},
            {"cmdline", proc.cmdline},
            {"state", std::string(1, proc.state)},
            {"threads_count", proc.threads_count},
            {"utime_ticks", proc.utime_ticks},
            {"stime_ticks", proc.stime_ticks},
            {"cpu_pct_machine", proc.cpu_pct_machine},
            {"cpu_pct_core", proc.cpu_pct_core},
            {"vm_rss_bytes", proc.vm_rss_bytes},
            {"vm_size_bytes", proc.vm_size_bytes},
            {"io_rchar", proc.io_rchar ? nlohmann::json(*proc.io_rchar) : nlohmann::json(nullptr)},
            {"io_wchar", proc.io_wchar ? nlohmann::json(*proc.io_wchar) : nlohmann::json(nullptr)},
            {"io_read_bytes", proc.io_read_bytes ? nlohmann::json(*proc.io_read_bytes) : nlohmann::json(nullptr)},
            {"io_write_bytes", proc.io_write_bytes ? nlohmann::json(*proc.io_write_bytes) : nlohmann::json(nullptr)},
            {"voluntary_ctxt_switches", proc.voluntary_ctxt_switches ? nlohmann::json(*proc.voluntary_ctxt_switches) : nlohmann::json(nullptr)},
            {"nonvoluntary_ctxt_switches", proc.nonvoluntary_ctxt_switches ? nlohmann::json(*proc.nonvoluntary_ctxt_switches) : nlohmann::json(nullptr)},
            {"children", proc.children},
            {"top_threads", nlohmann::json::array()}
        };
        for (const auto& thread : proc.top_threads) {
            processes_json[std::to_string(pid)]["top_threads"].push_back({
                {"tid", thread.tid},
                {"name", thread.name},
                {"state", std::string(1, thread.state)},
                {"priority", thread.priority},
                {"nice", thread.nice},
                {"policy", thread.policy},
                {"utime_ticks", thread.utime_ticks},
                {"stime_ticks", thread.stime_ticks},
                {"cpu_pct_machine", thread.cpu_pct_machine},
                {"cpu_pct_core", thread.cpu_pct_core},
                {"rchar", thread.rchar ? nlohmann::json(*thread.rchar) : nlohmann::json(nullptr)},
                {"wchar", thread.wchar ? nlohmann::json(*thread.wchar) : nlohmann::json(nullptr)},
                {"read_bytes", thread.read_bytes ? nlohmann::json(*thread.read_bytes) : nlohmann::json(nullptr)},
                {"write_bytes", thread.write_bytes ? nlohmann::json(*thread.write_bytes) : nlohmann::json(nullptr)},
                {"voluntary_ctxt_switches", thread.voluntary_ctxt_switches ? nlohmann::json(*thread.voluntary_ctxt_switches) : nlohmann::json(nullptr)},
                {"nonvoluntary_ctxt_switches", thread.nonvoluntary_ctxt_switches ? nlohmann::json(*thread.nonvoluntary_ctxt_switches) : nlohmann::json(nullptr)}
            });
        }
    }
    j["processes"] = processes_json;

    MYLOG_INFO("SoftHealthSnapshot JSON:\n" + j.dump(4));

    return;
};


void printSoftHealthSnapshotAsJsonZH(const MySoftHealthy::SoftHealthSnapshot& snap) {
    using namespace std::chrono;
    auto uptime = duration_cast<seconds>(snap.ts.time_since_epoch()).count();
    nlohmann::json j;

    j["时间戳"] = uptime;
    j["主机信息"] = {
        {"CPU数量", snap.host.num_cpus},
        {"时钟频率", snap.host.clk_tck},
        {"总CPU jiffies", snap.host.total_cpu_jiffies}
    };

    j["目标信息"] = {
        {"PID", snap.target_pid ? nlohmann::json(*snap.target_pid) : nlohmann::json(nullptr)},
        {"进程名", snap.target_name ? nlohmann::json(*snap.target_name) : nlohmann::json(nullptr)},
        {"命令行正则", snap.target_cmdline_regex ? nlohmann::json(*snap.target_cmdline_regex) : nlohmann::json(nullptr)}
    };

    nlohmann::json processes_json = nlohmann::json::object();
    for (const auto& [pid, proc] : snap.processes) {
        processes_json[std::to_string(pid)] = {
            {"PID", proc.pid},
            {"启动时间ticks", proc.start_time_ticks},
            {"父PID", proc.ppid},
            {"进程名", proc.name},
            {"命令行", proc.cmdline},
            {"状态", std::string(1, proc.state)},
            {"线程数", proc.threads_count},
            {"用户态ticks", proc.utime_ticks},
            {"内核态ticks", proc.stime_ticks},
            {"全机CPU%", proc.cpu_pct_machine},
            {"单核CPU%", proc.cpu_pct_core},
            {"物理内存RSS", proc.vm_rss_bytes},
            {"虚拟内存大小", proc.vm_size_bytes},
            {"读字符数", proc.io_rchar ? nlohmann::json(*proc.io_rchar) : nlohmann::json(nullptr)},
            {"写字符数", proc.io_wchar ? nlohmann::json(*proc.io_wchar) : nlohmann::json(nullptr)},
            {"读字节数", proc.io_read_bytes ? nlohmann::json(*proc.io_read_bytes) : nlohmann::json(nullptr)},
            {"写字节数", proc.io_write_bytes ? nlohmann::json(*proc.io_write_bytes) : nlohmann::json(nullptr)},
            {"自愿上下文切换", proc.voluntary_ctxt_switches ? nlohmann::json(*proc.voluntary_ctxt_switches) : nlohmann::json(nullptr)},
            {"非自愿上下文切换", proc.nonvoluntary_ctxt_switches ? nlohmann::json(*proc.nonvoluntary_ctxt_switches) : nlohmann::json(nullptr)},
            {"子进程列表", proc.children},
            {"Top线程列表", nlohmann::json::array()}
        };
        for (const auto& thread : proc.top_threads) {
            processes_json[std::to_string(pid)]["Top线程列表"].push_back({
                {"TID", thread.tid},
                {"线程名", thread.name},
                {"状态", std::string(1, thread.state)},
                {"优先级", thread.priority},
                {"nice值", thread.nice},
                {"调度策略", thread.policy},
                {"用户态ticks", thread.utime_ticks},
                {"内核态ticks", thread.stime_ticks},
                {"全机CPU%", thread.cpu_pct_machine},   
                {"单核CPU%", thread.cpu_pct_core},
                {"读字符数", thread.rchar ? nlohmann::json(*thread.rchar) : nlohmann::json(nullptr)},
                {"写字符数", thread.wchar ? nlohmann::json(*thread.wchar) : nlohmann::json(nullptr)},
                {"读字节数", thread.read_bytes ? nlohmann::json(*thread.read_bytes) : nlohmann::json(nullptr)},
                {"写字节数", thread.write_bytes ? nlohmann::json(*thread.write_bytes) : nlohmann::json(nullptr)},
                {"自愿上下文切换", thread.voluntary_ctxt_switches ? nlohmann::json(*thread.voluntary_ctxt_switches) : nlohmann::json(nullptr)},
                {"非自愿上下文切换", thread.nonvoluntary_ctxt_switches ? nlohmann::json(*thread.nonvoluntary_ctxt_switches) : nlohmann::json(nullptr)}
            });
        }
    }
    j["进程列表"] = processes_json;

    MYLOG_INFO("SoftHealthSnapshot JSON:\n" + j.dump(4));

    return;
}
} // namespace MySoftHealthy