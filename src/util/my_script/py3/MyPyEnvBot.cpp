// MyPyEnvBot.cpp
#include "MyPyEnvBot.h"
#include "MyLog.h"
#include "bash/MyBashEnvBot.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <sstream>

namespace py3 {

bool MyPyEnvBot::createEnv(const std::string& path, std::string* out_msg) {
    MYLOG_INFO("[MyPyEnvBot] createEnv: {}", path);
    std::string cmd = "python3 -m venv '" + path + "'";
    auto r = bash::MyBashEnvBot::runBash(cmd, 300, 0, 0);
    if (out_msg) {
        std::ostringstream ss; ss << "stdout:" << r.stdout_str << " stderr:" << r.stderr_str; *out_msg = ss.str();
    }
    if (r.exit_code == 0 && !r.timed_out) {
        MYLOG_INFO("[MyPyEnvBot] createEnv ok: {}", path);
        return true;
    }
    MYLOG_ERROR("[MyPyEnvBot] createEnv failed: exit={} timed_out={}", r.exit_code, r.timed_out);
    return false;
}

my_script::ExecResult MyPyEnvBot::installPackages(const std::string& venvPath,
                                                  const std::vector<std::string>& packages,
                                                  int timeout_seconds) {
    MYLOG_INFO("[MyPyEnvBot] installPackages: venv={} count={}", venvPath, packages.size());
    std::string py = getPythonPath(venvPath);
    std::string cmd = py + " -m pip install --disable-pip-version-check";
    for (const auto& p : packages) cmd += " " + p;
    return bash::MyBashEnvBot::runBash(cmd, timeout_seconds, 0, 0);
}

std::vector<std::string> MyPyEnvBot::listInstalled(const std::string& venvPath, int timeout_seconds) {
    MYLOG_INFO("[MyPyEnvBot] listInstalled: {}", venvPath);
    std::string pip = getPipPath(venvPath);
    std::string cmd = pip + " list --disable-pip-version-check";
    auto r = bash::MyBashEnvBot::runBash(cmd, timeout_seconds, 0, 0);
    std::vector<std::string> out;
    if (r.exit_code != 0) {
        MYLOG_WARN("[MyPyEnvBot] pip list failed exit={} timed_out={}", r.exit_code, r.timed_out);
        return out;
    }
    std::istringstream iss(r.stdout_str);
    std::string line;
    bool header_skipped = false;
    while (std::getline(iss, line)) {
        if (!header_skipped) { header_skipped = true; continue; }
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

bool MyPyEnvBot::removeEnv(const std::string& path, std::string* out_msg) {
    MYLOG_INFO("[MyPyEnvBot] removeEnv: {}", path);
    try {
        std::filesystem::remove_all(path);
        if (out_msg) *out_msg = "removed";
        return true;
    } catch (const std::exception& e) {
        if (out_msg) *out_msg = e.what();
        MYLOG_ERROR("[MyPyEnvBot] removeEnv exception: {}", e.what());
        return false;
    }
}

std::string MyPyEnvBot::getPythonPath(const std::string& venvPath) {
    return venvPath + "/bin/python";
}

std::string MyPyEnvBot::getPipPath(const std::string& venvPath) {
    return venvPath + "/bin/pip";
}

bool MyPyEnvBot::checkHealth(const std::string& venvPath, std::string* out_msg) {
    MYLOG_INFO("[MyPyEnvBot] checkHealth: {}", venvPath);
    std::string py = getPythonPath(venvPath);
    auto r = bash::MyBashEnvBot::runBash(py + " -V", 10, 0, 5);
    if (r.exit_code == 0) {
        if (out_msg) *out_msg = r.stdout_str;
        return true;
    }
    if (out_msg) *out_msg = r.stderr_str.empty() ? r.stdout_str : r.stderr_str;
    return false;
}

my_script::ExecResult MyPyEnvBot::runPythonScript(const std::string& venvPath,
                                                 const std::string& scriptPath,
                                                 const std::vector<std::string>& args,
                                                 int timeout_seconds,
                                                 size_t memory_limit_bytes,
                                                 int cpu_time_limit_seconds) {
    std::string py = getPythonPath(venvPath);
    std::string cmd = py + " '" + scriptPath + "'";
    for (const auto& a : args) cmd += " '" + a + "'";
    MYLOG_INFO("[MyPyEnvBot] runPythonScript: {}", cmd);
    return bash::MyBashEnvBot::runBash(cmd, timeout_seconds, memory_limit_bytes, cpu_time_limit_seconds);
}

my_script::ExecResult MyPyEnvBot::runInCurrentEnv(const std::string& scriptPath,
                                                  const std::vector<std::string>& args,
                                                  int timeout_seconds,
                                                  size_t memory_limit_bytes,
                                                  int cpu_time_limit_seconds) {
    // 使用系统 python3 执行脚本
    std::string cmd = std::string("python3 '") + scriptPath + "'";
    for (const auto& a : args) cmd += " '" + a + "'";
    MYLOG_INFO("[MyPyEnvBot] runInCurrentEnv: {}", cmd);
    return bash::MyBashEnvBot::runBash(cmd, timeout_seconds, memory_limit_bytes, cpu_time_limit_seconds);
}

nlohmann::json MyPyEnvBot::getCurrentPythonInfo(int timeout_seconds) {
    MYLOG_INFO("[MyPyEnvBot] getCurrentPythonInfo");
    nlohmann::json info = {};
    info["request_time"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    info["exec_status"] = "failed";
    // 组装 Python 命令，输出 JSON
    std::string pycmd =
        "python3 -c \"import sys,platform,json,subprocess;\n"
        "info={'executable':sys.executable,'version':sys.version,'platform':platform.platform(),'prefix':sys.prefix};\n"
        "try:\n  p=subprocess.run([sys.executable,'-m','pip','--version'],capture_output=True,text=True,check=False)\n  info['pip_version']=p.stdout.strip()\nexcept Exception as e:\n  info['pip_version']=str(e)\nprint(json.dumps(info))\"";

    std::string check_pip_list_cmd = "python3 -m pip list --disable-pip-version-check --format=json";
    auto r = bash::MyBashEnvBot::runBash(check_pip_list_cmd, timeout_seconds, 0, 5);
    if (r.exit_code == 0 && !r.timed_out) {
        try {
            nlohmann::json pip_list_json = nlohmann::json::parse(r.stdout_str);
            info["installed_packages"] = pip_list_json;
            MYLOG_INFO("[MyPyEnvBot] getCurrentPythonInfo installed packages: \n {}", pip_list_json.dump());
        } catch (const std::exception& e) {
            MYLOG_ERROR("[MyPyEnvBot] getCurrentPythonInfo pip list JSON parse error: {}", e.what());
        }
    } else {
        MYLOG_INFO("[MyPyEnvBot] getCurrentPythonInfo pip list command failed");
    }

    r = bash::MyBashEnvBot::runBash(pycmd, timeout_seconds, 0, 5);
    if (r.exit_code == 0 && !r.timed_out) {
        // 返回 stdout (JSON)
        if (!r.stdout_str.empty()) {
            MYLOG_INFO("[MyPyEnvBot] getCurrentPythonInfo raw: {}", r.stdout_str);
        } else {
            MYLOG_WARN("[MyPyEnvBot] getCurrentPythonInfo empty stdout");
        }
        try {
            nlohmann::json returned_json = nlohmann::json::parse(r.stdout_str);
            info["exec_status"] = "success";
            info["python_info"] = returned_json;
            MYLOG_INFO("[MyPyEnvBot] getCurrentPythonInfo parsed JSON: \n {}", info.dump(4));
        } catch(const std::exception& e) {
            MYLOG_ERROR("[MyPyEnvBot] getCurrentPythonInfo JSON parse error: {}", e.what());
        }
    }

    MYLOG_WARN("[MyPyEnvBot] getCurrentPythonInfo failed exit={} timed_out={} stderr={}", r.exit_code, r.timed_out, r.stderr_str);
    return info;
}

} // namespace py3
