// MyPyEnvBot.h
#pragma once

#include "../MyScriptManager.h"
#include "MyLog.h"

#include <string>
#include <vector>
#include <nlohmann/json.hpp>


// Python 环境管理（venv）工具，命名空间为 `py3`。
// 提供创建/删除 venv、安装包、列出已安装包、获取 python/pip 路径、执行脚本与健康检查。
namespace py3 {

class MyPyEnvBot {
public:

    /**
     * @brief Create a Env object
     * 
     * @param path 
     * @param out_msg 
     * @return true 
     * @return false 
     */
    static bool createEnv(const std::string& path, std::string* out_msg = nullptr);

    /** 
     * @brief Install packages into the specified venv
     * 
     * @param venvPath 
     * @param packages 
     * @param timeout_seconds 
     * @return my_script::ExecResult 
     */
    static my_script::ExecResult installPackages(const std::string& venvPath,
                                                const std::vector<std::string>& packages,
                                                int timeout_seconds = 120);

    /** 
     * @brief List installed packages in the specified venv
     * 
     * @param venvPath 
     * @param timeout_seconds 
     * @return std::vector<std::string> 
     */
    static std::vector<std::string> listInstalled(const std::string& venvPath, int timeout_seconds = 30);
    
    /** 
     * @brief Remove the specified venv
     * 
     * @param path 
     * @param out_msg 
     * @return true 
     * @return false 
     */
    static bool removeEnv(const std::string& path, std::string* out_msg = nullptr);
    
    /** 
     * @brief Get the Python executable path within the specified venv
     * 
     * @param venvPath 
     * @return std::string 
     */
    static std::string getPythonPath(const std::string& venvPath);
    
    /** 
     * @brief Get the Pip executable path within the specified venv
     * 
     * @param venvPath 
     * @return std::string 
     */
    static std::string getPipPath(const std::string& venvPath);
    
    /** 
     * @brief Check the health of the specified venv
     * 
     * @param venvPath 
     * @param out_msg 
     * @return true 
     * @return false 
     */
    static bool checkHealth(const std::string& venvPath, std::string* out_msg = nullptr);
    
    /** 
     * @brief Run a Python script within the specified venv
     * 
     * @param venvPath 
     * @param scriptPath 
     * @param args 
     * @param timeout_seconds 
     * @param memory_limit_bytes 
     * @param cpu_time_limit_seconds 
     * @return my_script::ExecResult 
     */
    static my_script::ExecResult runPythonScript(const std::string& venvPath,
                                                const std::string& scriptPath,
                                                const std::vector<std::string>& args = {},
                                                int timeout_seconds = 0,
                                                size_t memory_limit_bytes = 0,
                                                int cpu_time_limit_seconds = 0);

    /**
     * @brief Run a Python script using the system/current python (not a venv)
     */
    static my_script::ExecResult runInCurrentEnv(const std::string& scriptPath,
                                                 const std::vector<std::string>& args = {},
                                                 int timeout_seconds = 0,
                                                 size_t memory_limit_bytes = 0,
                                                 int cpu_time_limit_seconds = 0);

    /**
     * @brief Query current system python environment information as JSON string.
     *
     * Returns a JSON string with keys: executable, version, platform, prefix, pip_version.
     */
    static nlohmann::json getCurrentPythonInfo(int timeout_seconds = 5);
};

} // namespace py3
