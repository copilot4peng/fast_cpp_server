#include <gtest/gtest.h>

#include "MyLog.h"
#include "MyScriptManager.h"
#include "bash/MyBashEnvBot.h"
#include "py3/MyPyEnvBot.h"
#include <fstream>
#include <unistd.h>

using namespace my_script;

TEST(BashRun, Echo) {
    auto r = bash::MyBashEnvBot::runBash("echo hello", 5);
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_FALSE(r.timed_out);
    EXPECT_NE(r.stdout_str.find("hello"), std::string::npos);
}

TEST(BashTimeout, Sleep) {
    auto r = bash::MyBashEnvBot::runBash("sleep 2", 1);
    EXPECT_TRUE(r.timed_out || r.exit_code != 0);
}

TEST(ManagerRunBash, Simple) {
    auto& mgr = MyScriptManager::GetInstance();
    auto r = mgr.runBash("echo mgr_ok", 5);
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_str.find("mgr_ok"), std::string::npos);
}

TEST(PyEnv_Create_Run_Remove, Basic) {
    // 使用基于 pid 的临时目录，避免冲突
    pid_t pid = getpid();
    std::string tmpdir = std::string("/tmp/my_py_env_test_") + std::to_string(pid);

    // 确保删除旧目录
    py3::MyPyEnvBot::removeEnv(tmpdir, nullptr);

    std::string msg;
    bool created = py3::MyPyEnvBot::createEnv(tmpdir, &msg);
    EXPECT_TRUE(created) << "createEnv failed: " << msg;

    // 写入一个简单的 python 脚本
    std::string script = tmpdir + "/hello.py";
    {
        std::ofstream ofs(script);
        ofs << "print('py_env_ok')\n";
    }

    auto r = py3::MyPyEnvBot::runPythonScript(tmpdir, script, {}, 20);
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_FALSE(r.timed_out);
    EXPECT_NE(r.stdout_str.find("py_env_ok"), std::string::npos);

    bool removed = py3::MyPyEnvBot::removeEnv(tmpdir, &msg);
    EXPECT_TRUE(removed) << "removeEnv failed: " << msg;
}

TEST(PyRunInCurrentEnv, Simple) {
    pid_t pid = getpid();
    std::string script = std::string("/tmp/py_run_current_") + std::to_string(pid) + ".py";
    {
        std::ofstream ofs(script);
        ofs << "print('current_env_ok')\n";
    }
    auto r = py3::MyPyEnvBot::runInCurrentEnv(script, {}, 10);
    MYLOG_INFO("exit={} out={}", r.exit_code, r.stdout_str);
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_FALSE(r.timed_out);
    EXPECT_NE(r.stdout_str.find("current_env_ok"), std::string::npos);
}

TEST(PyGetCurrentInfo, Basic) {
    auto s = py3::MyPyEnvBot::getCurrentPythonInfo(5);
    EXPECT_GT(s.size(), 0u);
    // EXPECT_NE(s.find("executable"), std::string::npos);
}

