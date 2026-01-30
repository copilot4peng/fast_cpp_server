#include "MyMqttBrokerManager.h"
#include <sys/wait.h> 
#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <fstream>

#include "MyLog.h"

namespace my_mqtt_broker_manager {

MyMqttBrokerManager& MyMqttBrokerManager::GetInstance() {
    static MyMqttBrokerManager instance;
    return instance;
}

void MyMqttBrokerManager::Init(const nlohmann::json& config) {
    MYLOG_INFO("初始化 MyMqttBrokerManager 配置: {}", config.dump(4));
    try {
        if (config.contains("broker") && config["broker"].contains("bin")) {
            broker_bin_ = config["broker"]["bin"];
            broker_config_ = config["broker"]["config"];
            broker_port_ = config["broker"]["port"];
            broker_foreground_ = config["broker"]["foreground"];
            config_initialized_ = true;
        } else {
            MYLOG_ERROR("初始化 MyMqttBrokerManager 配置失败: broker.bin 或 broker.config 不存在");
        }
    } catch (const std::exception& e) {
        MYLOG_ERROR("初始化 MyMqttBrokerManager 配置失败: {}", e.what());
    }
}

bool MyMqttBrokerManager::Start() {
    if (!config_initialized_) {
        MYLOG_ERROR("MyMqttBrokerManager 配置未初始化");
        return false;
    }

    // 启动 Broker 进程管理
    broker_manager_thread_ = std::thread(&MyMqttBrokerManager::BrokerProcessManager, this);
    return true;
}

// void MyMqttBrokerManager::Stop() {
//     MYLOG_INFO("停止 MyMqttBrokerManager");
//     if (broker_pid_ > 0) {
//         // 发送终止信号
//         kill(broker_pid_, SIGTERM);
//         broker_pid_ = -1;
//     }

//     if (broker_manager_thread_.joinable()) {
//         broker_manager_thread_.join();
//     }
// }

void MyMqttBrokerManager::Stop() {
    MYLOG_INFO("停止 MyMqttBrokerManager");

    // 1) 首先关闭健康检查 / 管理循环，防止其在我们停止期间重启 broker
    broker_running_ = false;

    // 2) 如果有启动的 broker 进程，发送 SIGTERM 尝试优雅退出
    if (broker_pid_ > 0) {
        pid_t pid = broker_pid_;
        MYLOG_INFO("向 Broker 进程发送 SIGTERM，PID={}", pid);
        kill(pid, SIGTERM);

        // 3) 等待进程退出（轮询），最多等待 timeout_seconds 秒
        const int timeout_seconds = 5;
        int waited = 0;
        while (waited < timeout_seconds) {
            // kill(pid, 0) 用于检测进程是否仍存在
            if (kill(pid, 0) != 0) {
                // 进程不存在或已退出
                MYLOG_INFO("Broker 进程已退出（检测到 PID={} 不存在）", pid);
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
            waited++;
        }

        // 4) 如果超时后进程仍然存在，发送 SIGKILL 强制结束
        if (kill(pid, 0) == 0) {
            MYLOG_WARN("Broker 进程 PID={} 在 {} 秒内未退出，发送 SIGKILL 强制结束", pid, timeout_seconds);
            kill(pid, SIGKILL);
        }

        // 5) 使用 waitpid 回收子进程，避免僵尸
        int status = 0;
        pid_t w = waitpid(pid, &status, 0);
        if (w == pid) {
            MYLOG_INFO("waitpid 回收 Broker PID={}，status={}", pid, status);
        } else {
            MYLOG_WARN("waitpid 未能回收 PID={}，返回值 {}", pid, w);
        }

        // 6) 清理 pid 标记
        broker_pid_ = -1;
    } else {
        MYLOG_INFO("没有需要停止的 Broker 进程（broker_pid_ <= 0）");
    }

    // 7) 等待管理线程退出，但避免在同一线程中 join 自己导致死锁
    if (broker_manager_thread_.joinable()) {
        if (std::this_thread::get_id() != broker_manager_thread_.get_id()) {
            MYLOG_INFO("等待 Broker 管理线程退出并 join");
            broker_manager_thread_.join();
        } else {
            // 如果 Stop 是从管理线程内部调用（极少见），不进行 join
            MYLOG_WARN("Stop 在 Broker 管理线程内部被调用，跳过 self-join");
        }
    }

    MYLOG_INFO("MyMqttBrokerManager 已停止完成");
}

bool MyMqttBrokerManager::IsRunning() const {
    bool broker_running = false;
    broker_running = broker_pid_ > 0 && kill(broker_pid_, 0) == 0;
    MYLOG_INFO("Broker 进程状态: {}", broker_running ? "运行中" : "已停止");
    return broker_running;
}

nlohmann::json MyMqttBrokerManager::GetHeartbeat() const {
    nlohmann::json heartbeat_info;
    heartbeat_info["broker_status"] = broker_running_ ? "running" : "stopped";
    heartbeat_info["port"] = broker_port_;
    heartbeat_info["pid"] = broker_pid_;
    MYLOG_INFO("获取 Broker 心跳信息: {}", heartbeat_info.dump(4));
    return heartbeat_info;
}

void MyMqttBrokerManager::BrokerProcessManager() {
    // 启动 Broker 子进程
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程执行 Broker
        MYLOG_INFO("启动 Mosquitto Broker 进程...");
        execlp(broker_bin_.c_str(), "mosquitto", "-c", broker_config_.c_str(), broker_foreground_ ? "-v" : nullptr, (char*)nullptr);
        exit(127);
    }

    // 父进程保存子进程的 PID
    broker_pid_ = pid;
    broker_running_ = true;
    MYLOG_INFO("Mosquitto Broker 进程已启动, PID: {}", broker_pid_);
    // 启动健康检查
    HealthCheck();
}

void MyMqttBrokerManager::HealthCheck() {
    while (broker_running_) {
        if (IsRunning()) {
            MYLOG_INFO("MQTT Broker 进程健康检查成功");
            std::this_thread::sleep_for(std::chrono::seconds(10));
        } else {
            MYLOG_WARN("MQTT Broker 进程已停止； 重新启动...");
            Start();
        }
    }
}

MyMqttBrokerManager::~MyMqttBrokerManager() {
    Stop();
}

}; // namespace my_mqtt_broker_manager