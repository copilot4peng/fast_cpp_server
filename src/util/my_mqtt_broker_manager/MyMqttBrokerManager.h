#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include <memory>
#include <thread>
#include <chrono>
#include <csignal>
#include <unistd.h>


namespace my_mqtt_broker_manager {

class MyMqttBrokerManager {
public:
    // 获取单例实例
    static MyMqttBrokerManager& GetInstance();

    // 初始化配置
    void Init(const nlohmann::json& config);

    // 启动 Broker 服务
    bool Start();

    // 停止 Broker 服务
    void Stop();

    // 检查 Broker 是否运行
    bool IsRunning() const;

    // 获取心跳信息
    nlohmann::json GetHeartbeat() const;

private:
    MyMqttBrokerManager() = default;
    ~MyMqttBrokerManager();

    // 启动并管理 Broker 子进程
    void BrokerProcessManager();

    /**
     * @brief 定期检查 Broker 健康状况
     * 
     */
    void HealthCheck();

    // Broker 子进程 PID
    pid_t broker_pid_{-1};

    // 配置文件路径
    std::string broker_bin_;
    std::string broker_config_;
    int broker_port_;
    bool broker_foreground_{false};

    // 心跳状态
    bool broker_running_{false};

    // 配置初始化成功标记
    bool config_initialized_{false};

    // 线程
    std::thread broker_manager_thread_;
};

};