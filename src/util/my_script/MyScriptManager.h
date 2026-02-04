// MyScriptManager.h
#pragma once

#include <string>
#include <vector>
#include <memory>

// 管理 my_script 模块的 orchestrator（单例）。
// 负责对外 C++ API，协调 Python/Bash EnvBot，处理执行请求并暴露查询接口。
// 该头文件只声明公共接口，具体实现位于 MyScriptManager.cpp

namespace my_script {

struct ExecResult {
    int exit_code = -1;
    bool timed_out = false;
    std::string stdout_str;
    std::string stderr_str;
};

class MyScriptManager {
public:
    // 获取单例
    static MyScriptManager& GetInstance();

    // 直接执行外部进程（通用），返回 ExecResult
    ExecResult runProcess(const std::vector<std::string>& argv,
                          int timeout_seconds = 0,
                          size_t memory_limit_bytes = 0,
                          int cpu_time_limit_seconds = 0);

    // Python venv 管理接口
    bool createPythonEnv(const std::string& path, std::string* out_msg = nullptr);
    
    ExecResult installPythonPackages(const std::string& venvPath,
                                     const std::vector<std::string>& packages,
                                     int timeout_seconds = 120);

    std::vector<std::string> listInstalledPackages(const std::string& venvPath, int timeout_seconds = 30);
    
    bool removePythonEnv(const std::string& path, std::string* out_msg = nullptr);

    std::string getPythonPath(const std::string& venvPath);
    
    std::string getPipPath(const std::string& venvPath);
    
    bool checkPythonEnvHealth(const std::string& venvPath, std::string* out_msg = nullptr);

    // Bash helper
    ExecResult runBash(const std::string& script,
                       int timeout_seconds = 0,
                       size_t memory_limit_bytes = 0,
                       int cpu_time_limit_seconds = 0);

private:
    MyScriptManager();
    ~MyScriptManager();
    MyScriptManager(const MyScriptManager&) = delete;
    MyScriptManager& operator=(const MyScriptManager&) = delete;
};

} // namespace my_script
