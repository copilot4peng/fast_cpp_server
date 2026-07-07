#pragma once

/**
 * @file my_airdrop_lock.h
 * @brief 空投锁管理器
 *
 * 空投锁管理器负责维护空投锁串口连接、初始化参数、开锁/关锁命令下发和状态查询。
 * 该类是进程级单例，业务层和 REST API 均通过 GetInstance() 获取同一个设备状态。
 */

#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "MySerial.h"

namespace my_airdrop_lock {

/**
 * @brief 空投锁逻辑状态
 */
enum class AirdropLockState {
	Uninitialized,
	Disabled,
	Closed,
	Open,
	Error
};

/**
 * @brief 空投锁初始化配置
 */
struct AirdropLockConfig {
	bool enabled{false};
	std::string device{"/dev/ttyUSB0"};
	int baud_rate{9600};
	int data_bits{8};
	int stop_bits{1};
	std::string default_pwm_frequency{"50"};
	std::string open_cmd{"D005"};
	std::string lock_cmd{"D010"};

	nlohmann::json ToJson() const;
};

/**
 * @brief 空投锁管理器单例
 *
 * 初始化成功后会立即执行两步保护动作：
 * 1. 按 default_PWM_frequency 下发 Fxxx 占空比/频率命令。
 * 2. 下发 lock_cmd，确保设备默认处于关闭锁状态。
 */
class AirdropLockManager {
public:
	static AirdropLockManager& GetInstance();

	AirdropLockManager(const AirdropLockManager&) = delete;
	AirdropLockManager& operator=(const AirdropLockManager&) = delete;

	bool Init(const nlohmann::json& config, std::string* err = nullptr);
    bool Start();
	bool SetDutyCycle(const std::string& duty_cycle, std::string* err = nullptr);
	bool SetDutyCycle(int duty_cycle, std::string* err = nullptr);
	bool OpenDropper(std::string* err = nullptr);
	bool CloseDropper(std::string* err = nullptr);
    bool FristPowerOnProtection();
	nlohmann::json GetStatus() const;

private:
	AirdropLockManager() = default;

	static AirdropLockConfig ParseConfig(const nlohmann::json& config);
	static std::string NormalizeDutyCycleCommand(const std::string& duty_cycle);
	static std::string StateToString(AirdropLockState state);

	bool SendCommandLocked(const std::string& command,
						   const std::string& action_name,
						   std::string* err);
	void SetErrorLocked(const std::string& error);

	mutable std::mutex mutex_;
	bool initialized_{false};
	AirdropLockState state_{AirdropLockState::Uninitialized};
	AirdropLockConfig config_;
	nlohmann::json init_config_{nlohmann::json::object()};
	std::string last_error_;
	std::string last_command_;
	std::string current_duty_cycle_;
	my_serial::MySerial serial_;
};

} // namespace my_airdrop_lock
