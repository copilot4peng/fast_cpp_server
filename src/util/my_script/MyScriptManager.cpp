// MyScriptManager.cpp
// 实现 MyScriptManager 单例，协调 Python/Bash env bot 并封装执行与查询。

#include "MyScriptManager.h"
#include "bash/MyBashEnvBot.h"
#include "py3/MyPyEnvBot.h"
#include "MyLog.h"

#include <filesystem>
#include <sstream>

namespace my_script {

MyScriptManager& MyScriptManager::GetInstance() {
    static MyScriptManager inst;
    return inst;
}

MyScriptManager::MyScriptManager() {
    MYLOG_INFO("MyScriptManager initialized");
}

MyScriptManager::~MyScriptManager() {
    MYLOG_INFO("MyScriptManager shutting down");
}

ExecResult MyScriptManager::runProcess(const std::vector<std::string>& argv,
                                       int timeout_seconds,
                                       size_t memory_limit_bytes,
                                       int cpu_time_limit_seconds) {
    MYLOG_INFO("runProcess: argv[0]={}", argv.empty() ? "" : argv[0]);

    // 若命令以 python 开头，尝试直接用 py3 bot 运行（当 argv[0] 为完整 python 路径时）
    if (!argv.empty() && argv[0].find("python") != std::string::npos) {
        std::string joined;
        for (size_t i = 0; i < argv.size(); ++i) {
            if (i) joined += " ";
            joined += argv[i];
        }
        return bash::MyBashEnvBot::runBash(joined, timeout_seconds, memory_limit_bytes, cpu_time_limit_seconds);
    }

    // 一般命令通过 bash -lc 执行
    std::string joined;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) joined += " ";
        joined += argv[i];
    }
    return bash::MyBashEnvBot::runBash(joined, timeout_seconds, memory_limit_bytes, cpu_time_limit_seconds);
}

// Python venv 管理：代理到 MyPyEnvBot
bool MyScriptManager::createPythonEnv(const std::string& path, std::string* out_msg) {
    MYLOG_INFO("createPythonEnv: {}", path);
    bool ok = py3::MyPyEnvBot::createEnv(path, out_msg);
    if (ok) MYLOG_INFO("createPythonEnv success: {}", path);
    else MYLOG_ERROR("createPythonEnv failed: {}", path);
    return ok;
}

ExecResult MyScriptManager::installPythonPackages(const std::string& venvPath,
                                                  const std::vector<std::string>& packages,
                                                  int timeout_seconds) {
    MYLOG_INFO("installPythonPackages: venv={} packages_count={}", venvPath, packages.size());
    ExecResult r = py3::MyPyEnvBot::installPackages(venvPath, packages, timeout_seconds);
    if (r.exit_code == 0) MYLOG_INFO("installPythonPackages succeeded");
    else MYLOG_ERROR("installPythonPackages failed exit={} timed_out={}", r.exit_code, r.timed_out);
    return r;
}

std::vector<std::string> MyScriptManager::listInstalledPackages(const std::string& venvPath, int timeout_seconds) {
    MYLOG_INFO("listInstalledPackages: {}", venvPath);
    auto out = py3::MyPyEnvBot::listInstalled(venvPath, timeout_seconds);
    return out;
}

bool MyScriptManager::removePythonEnv(const std::string& path, std::string* out_msg) {
    MYLOG_INFO("removePythonEnv: {}", path);
    return py3::MyPyEnvBot::removeEnv(path, out_msg);
}

std::string MyScriptManager::getPythonPath(const std::string& venvPath) {
    return py3::MyPyEnvBot::getPythonPath(venvPath);
}

std::string MyScriptManager::getPipPath(const std::string& venvPath) {
    return py3::MyPyEnvBot::getPipPath(venvPath);
}

bool MyScriptManager::checkPythonEnvHealth(const std::string& venvPath, std::string* out_msg) {
    MYLOG_INFO("checkPythonEnvHealth: {}", venvPath);
    return py3::MyPyEnvBot::checkHealth(venvPath, out_msg);
}

ExecResult MyScriptManager::runBash(const std::string& script,
                                    int timeout_seconds,
                                    size_t memory_limit_bytes,
                                    int cpu_time_limit_seconds) {
    MYLOG_INFO("runBash request: timeout={} mem={} cpu={}", timeout_seconds, memory_limit_bytes, cpu_time_limit_seconds);
    return bash::MyBashEnvBot::runBash(script, timeout_seconds, memory_limit_bytes, cpu_time_limit_seconds);
}

} // namespace my_script
