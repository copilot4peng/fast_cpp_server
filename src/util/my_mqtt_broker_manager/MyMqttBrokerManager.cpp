#include "MyMqttBrokerManager.h"
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

void MyMqttBrokerManager::Stop() {
    MYLOG_INFO("停止 MyMqttBrokerManager");
    if (broker_pid_ > 0) {
        // 发送终止信号
        kill(broker_pid_, SIGTERM);
        broker_pid_ = -1;
    }

    if (broker_manager_thread_.joinable()) {
        broker_manager_thread_.join();
    }
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