#include "SoftHealthyController.h"
#include "SoftHealthMonitorManager.h"
#include "SoftHealthSnapshot.h"
#include "SoftHealthMonitorConfig.h"
#include "MyLog.h"

namespace my_api::soft_healthy {

using namespace my_api::base;
using namespace MySoftHealthy;

SoftHealthyController::SoftHealthyController(const std::shared_ptr<ObjectMapper>& objectMapper)
    : BaseApiController(objectMapper) {}

std::shared_ptr<SoftHealthyController> SoftHealthyController::createShared(const std::shared_ptr<ObjectMapper>& objectMapper) {
    return std::make_shared<SoftHealthyController>(objectMapper);
}

MyAPIResponsePtr SoftHealthyController::getSoftHealthyOnline() {
    MYLOG_INFO("[API] SoftHealthy GET online");
    std::string status = "{\"status\": \"alive\"}";
    auto resp = createResponse(Status::CODE_200, status);
    resp->putHeader("Content-Type", "application/json");
    return resp;
}

MyAPIResponsePtr SoftHealthyController::getSoftHealthyData() {
    MYLOG_INFO("[API] SoftHealthy GET Data");
    auto& monitor = SoftHealthMonitorManager::getInstance();
    auto snap = monitor.getData();
    if (!snap) {
        return createResponse(Status::CODE_204, "{}");
    }

    nlohmann::json j;
    using namespace std::chrono;
    auto uptime = duration_cast<seconds>(snap->ts.time_since_epoch()).count();
    j["timestamp"] = uptime;
    j["host"] = { {"num_cpus", snap->host.num_cpus}, {"clk_tck", snap->host.clk_tck}, {"total_cpu_jiffies", snap->host.total_cpu_jiffies} };
    j["processes_count"] = snap->processes.size();
    j["roots"] = snap->roots;

    // 简要返回每个进程的 pid/name/cpu，并包含 Top 线程列表（线程级别粒度）
    nlohmann::json procs = nlohmann::json::array();
    int cnt = 0;
    for (const auto &kv : snap->processes) {
        if (cnt++ >= 50) break; // 限制返回进程数量
        const auto &p = kv.second;
        nlohmann::json proc_j = {
            {"pid", p.pid},
            {"name", p.name},
            {"cpu_pct_machine", p.cpu_pct_machine},
            {"cpu_pct_core", p.cpu_pct_core},
            {"vm_rss_bytes", p.vm_rss_bytes}
        };

        // threads
        nlohmann::json threads = nlohmann::json::array();
        for (const auto &t : p.top_threads) {
            threads.push_back({
                {"tid", t.tid},
                {"name", t.name},
                {"state", std::string(1, t.state)},
                {"priority", t.priority},
                {"nice", t.nice},
                {"cpu_pct_machine", t.cpu_pct_machine},
                {"cpu_pct_core", t.cpu_pct_core},
                {"utime_ticks", t.utime_ticks},
                {"stime_ticks", t.stime_ticks}
            });
        }
        proc_j["top_threads"] = threads;
        procs.push_back(proc_j);
    }
    j["processes"] = procs;

    auto resp = createResponse(Status::CODE_200, j.dump());
    resp->putHeader("Content-Type", "application/json");
    return resp;
}

MyAPIResponsePtr SoftHealthyController::refreshSoftHealthyNow() {
    MYLOG_INFO("[API] SoftHealthy REFRESH Now");
    auto& monitor = SoftHealthMonitorManager::getInstance();
    auto snap = monitor.refresh_now();
    if (!snap) return createResponse(Status::CODE_204, "{}");

    nlohmann::json j;
    using namespace std::chrono;
    auto uptime = duration_cast<seconds>(snap->ts.time_since_epoch()).count();
    j["timestamp"] = uptime;
    j["host"] = { {"num_cpus", snap->host.num_cpus}, {"clk_tck", snap->host.clk_tck}, {"total_cpu_jiffies", snap->host.total_cpu_jiffies} };
    j["processes_count"] = snap->processes.size();
    j["roots"] = snap->roots;

    // 包含简要进程与线程信息（限制返回量）
    nlohmann::json procs2 = nlohmann::json::array();
    int cnt2 = 0;
    for (const auto &kv : snap->processes) {
        if (cnt2++ >= 50) break;
        const auto &p = kv.second;
        nlohmann::json proc_j = {
            {"pid", p.pid},
            {"name", p.name},
            {"cpu_pct_machine", p.cpu_pct_machine},
            {"cpu_pct_core", p.cpu_pct_core}
        };
        proc_j["top_threads_count"] = p.top_threads.size();
        nlohmann::json threads = nlohmann::json::array();
        for (const auto &t : p.top_threads) {
            threads.push_back({
                {"tid", t.tid},
                {"name", t.name},
                {"cpu_pct_machine", t.cpu_pct_machine},
                {"cpu_pct_core", t.cpu_pct_core}
            });
        }
        proc_j["top_threads"] = threads;
        procs2.push_back(proc_j);
    }
    j["processes"] = procs2;

    auto resp = createResponse(Status::CODE_200, j.dump());
    resp->putHeader("Content-Type", "application/json");
    return resp;
}

MyAPIResponsePtr SoftHealthyController::getSoftHealthyConfig() {
    MYLOG_INFO("[API] SoftHealthy GET Config");
    nlohmann::json j;
    j["message"] = "SoftHealthMonitorManager does not expose config getter. Use Pipeline to init/applyConfig.";
    return createResponse(Status::CODE_200, j.dump());
}

} // namespace my_api::soft_healthy
