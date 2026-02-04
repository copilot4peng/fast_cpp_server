// MyBashEnvBot.h
#pragma once

#include "../MyScriptManager.h"
#include "MyLog.h"

#include <string>
#include <vector>

// 提供受限的 shell 脚本执行辅助。命名空间为 `bash`，与 MyScriptManager 的调用约定一致。
namespace bash {

class MyBashEnvBot {
public:
    // 运行一段 bash 脚本，支持超时、内存/CPU 限制
    static my_script::ExecResult runBash(const std::string& script,
                                        int timeout_seconds = 0,
                                        size_t memory_limit_bytes = 0,
                                        int cpu_time_limit_seconds = 0);
};

} // namespace bash
