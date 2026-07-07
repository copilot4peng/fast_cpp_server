#include "my_airdrop_lock.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "MyLog.h"

namespace my_airdrop_lock {

namespace {

std::string TrimCopy(const std::string& value) {
	const auto begin = value.find_first_not_of(" \t\r\n");
	if (begin == std::string::npos) {
		return "";
	}
	const auto end = value.find_last_not_of(" \t\r\n");
	return value.substr(begin, end - begin + 1);
}

std::string JsonStringOrNumber(const nlohmann::json& json,
							   const char* key,
							   const std::string& default_value) {
	if (!json.contains(key) || json.at(key).is_null()) {
		return default_value;
	}
	const auto& value = json.at(key);
	if (value.is_string()) {
		return value.get<std::string>();
	}
	if (value.is_number_integer() || value.is_number_unsigned() || value.is_number_float()) {
		return value.dump();
	}
	throw std::invalid_argument(std::string("字段 ") + key + " 必须是字符串或数字");
}

int JsonIntValue(const nlohmann::json& json, const char* key, int default_value) {
	if (!json.contains(key) || json.at(key).is_null()) {
		return default_value;
	}
	const auto& value = json.at(key);
	if (value.is_number_integer() || value.is_number_unsigned()) {
		return value.get<int>();
	}
	if (value.is_string()) {
		return std::stoi(value.get<std::string>());
	}
	throw std::invalid_argument(std::string("字段 ") + key + " 必须是整数或整数字符串");
}

bool IsAsciiCommand(const std::string& command) {
	return !command.empty() &&
		   std::all_of(command.begin(), command.end(), [](unsigned char ch) {
			   return std::isprint(ch) != 0;
		   });
}

} // namespace

nlohmann::json AirdropLockConfig::ToJson() const {
	return {
		{"enabled", enabled},
		{"device", device},
		{"baud_rate", baud_rate},
		{"data_bits", data_bits},
		{"stop_bits", stop_bits},
		{"default_PWM_frequency", default_pwm_frequency},
		{"open_cmd", open_cmd},
		{"lock_cmd", lock_cmd}
	};
}

AirdropLockManager& AirdropLockManager::GetInstance() {
	static AirdropLockManager instance;
	return instance;
}

bool AirdropLockManager::Init(const nlohmann::json& config, std::string* err) {
	std::lock_guard<std::mutex> lock(mutex_);
	MYLOG_INFO("[空投锁] 开始初始化空投锁管理器，入参: {}", config.dump(4));

	try {
		config_ = ParseConfig(config);
        this->current_duty_cycle_ = config_.default_pwm_frequency;
		init_config_ = config;
		last_error_.clear();
		last_command_.clear();
		current_duty_cycle_.clear();
		initialized_ = false;
		state_ = config_.enabled ? AirdropLockState::Closed : AirdropLockState::Disabled;

		if (!config_.enabled) {
			serial_.Close();
			initialized_ = true;
			MYLOG_WARN("[空投锁] 配置 enabled=false，模块保持禁用状态，不打开串口，不下发任何设备命令");
			if (err != nullptr) {
				err->clear();
			}
			return true;
		}

		nlohmann::json serial_config = {
			{"device", config_.device},
			{"baud_rate", config_.baud_rate},
			{"data_bits", config_.data_bits},
			{"stop_bits", config_.stop_bits},
			{"parity", "none"},
			{"flowcontrol", "none"},
			{"timeout_ms", 1000},
			{"auto_open", true}
		};

		std::string serial_error;
		if (!serial_.Init(serial_config, &serial_error)) {
			SetErrorLocked("串口初始化失败: " + serial_error);
			if (err != nullptr) {
				*err = last_error_;
			}
			MYLOG_ERROR("[空投锁] 初始化失败，串口无法打开，device={}, baud_rate={}, error={}",
						config_.device, config_.baud_rate, serial_error);
			return false;
		}

		initialized_ = true;
		state_ = AirdropLockState::Closed;
		MYLOG_INFO("[空投锁] 初始化完成：设备已打开串口、默认占空比/频率已设置、空投锁已关闭");
		if (err != nullptr) {
			err->clear();
		}
		return true;
	} catch (const std::exception& ex) {
		SetErrorLocked(ex.what());
		if (err != nullptr) {
			*err = last_error_;
		}
		MYLOG_ERROR("[空投锁] 初始化异常: {}", ex.what());
		return false;
	}
}

bool AirdropLockManager::Start() {
	std::lock_guard<std::mutex> lock(mutex_);
	if (!initialized_) {
		MYLOG_ERROR("[空投锁] 模块未初始化，无法启动");
		return false;
	}
    bool success = true;
    // success = FristPowerOnProtection();
	return success;
}

bool AirdropLockManager::FristPowerOnProtection() {
    bool success = false;

    MYLOG_INFO("[空投锁] 开始执行上电保护动作：先设置占空比/频率，再关闭空投锁");
    try {

        MYLOG_INFO("[空投锁] 下发默认占空比/频率命令: {}", config_.default_pwm_frequency);
        const std::string duty_cmd = NormalizeDutyCycleCommand(config_.default_pwm_frequency);
        std::string err;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        if (!SetDutyCycle(duty_cmd, &err)) {
            MYLOG_ERROR("[空投锁] 初始化失败：默认占空比/频率命令下发失败，command={}, error={}", duty_cmd, err);
            return false;
        } else {
            MYLOG_INFO("[空投锁] 默认占空比/频率命令下发成功，command={}", duty_cmd);
            current_duty_cycle_ = config_.default_pwm_frequency;
        }
        
        MYLOG_INFO("[空投锁] 下发关闭空投锁命令: {}", config_.lock_cmd);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        err.clear();
        if (!SendCommandLocked(config_.lock_cmd, "关闭空投锁", &err)) {
            MYLOG_ERROR("[空投锁] 初始化失败：关闭锁命令下发失败，command={}, error={}", config_.lock_cmd, err);
            return false;
        } else {
            MYLOG_INFO("[空投锁] 关闭空投锁命令下发成功，command={}", config_.lock_cmd);
        }
    } catch (const std::exception& ex) {
        MYLOG_ERROR("[空投锁] 上电保护动作异常: {}", ex.what());
        return false;
    }
    success = true;
    return success;
}

bool AirdropLockManager::SetDutyCycle(const std::string& duty_cycle, std::string* err) {
	std::lock_guard<std::mutex> lock(mutex_);
	const std::string command = NormalizeDutyCycleCommand(duty_cycle);
	if (!SendCommandLocked(command, "设置占空比/频率", err)) {
		return false;
	}
	current_duty_cycle_ = TrimCopy(duty_cycle);
	MYLOG_INFO("[空投锁] 占空比/频率设置成功，输入值={}, 实际命令={}", current_duty_cycle_, command);
	return true;
}

bool AirdropLockManager::SetDutyCycle(int duty_cycle, std::string* err) {
	return SetDutyCycle(std::to_string(duty_cycle), err);
}

bool AirdropLockManager::OpenDropper(std::string* err) {
    // bool a = SetDutyCycle(current_duty_cycle_, err);
	std::lock_guard<std::mutex> lock(mutex_);
	if (!SendCommandLocked(config_.open_cmd, "打开空投锁", err)) {
		return false;
	}
	state_ = AirdropLockState::Open;
	MYLOG_INFO("[空投锁] 打开空投锁成功，状态已切换为 open，command={}", config_.open_cmd);
	return true;
}

bool AirdropLockManager::CloseDropper(std::string* err) {
    // bool a = SetDutyCycle(current_duty_cycle_, err);
	std::lock_guard<std::mutex> lock(mutex_);
	if (!SendCommandLocked(config_.lock_cmd, "关闭空投锁", err)) {
		return false;
	}
	state_ = AirdropLockState::Closed;
	MYLOG_INFO("[空投锁] 关闭空投锁成功，状态已切换为 closed，command={}", config_.lock_cmd);
	return true;
}

nlohmann::json AirdropLockManager::GetStatus() const {
	std::lock_guard<std::mutex> lock(mutex_);
	nlohmann::json data;
	data["initialized"] = initialized_;
	data["enabled"] = config_.enabled;
	data["state"] = StateToString(state_);
	data["is_open"] = state_ == AirdropLockState::Open;
	data["serial_open"] = serial_.IsOpen();
	data["config"] = config_.ToJson();
	data["init_config"] = init_config_;
	data["current_duty_cycle"] = current_duty_cycle_;
	data["last_command"] = last_command_;
	data["last_error"] = last_error_;
	return data;
}

AirdropLockConfig AirdropLockManager::ParseConfig(const nlohmann::json& config) {
	if (!config.is_object()) {
		throw std::invalid_argument("空投锁初始化参数必须是 JSON 对象");
	}

	AirdropLockConfig parsed;
	parsed.enabled = config.value("enabled", parsed.enabled);
	parsed.device = JsonStringOrNumber(config, "device", parsed.device);
	parsed.baud_rate = JsonIntValue(config, "baud_rate", parsed.baud_rate);
	parsed.data_bits = JsonIntValue(config, "data_bits", parsed.data_bits);
	parsed.stop_bits = JsonIntValue(config, "stop_bits", parsed.stop_bits);
	parsed.default_pwm_frequency = JsonStringOrNumber(config, "default_PWM_frequency", parsed.default_pwm_frequency);
	parsed.open_cmd = JsonStringOrNumber(config, "open_cmd", parsed.open_cmd);
	parsed.lock_cmd = JsonStringOrNumber(config, "lock_cmd", parsed.lock_cmd);

	parsed.device = TrimCopy(parsed.device);
	parsed.default_pwm_frequency = TrimCopy(parsed.default_pwm_frequency);
	parsed.open_cmd = TrimCopy(parsed.open_cmd);
	parsed.lock_cmd = TrimCopy(parsed.lock_cmd);

	if (parsed.enabled && parsed.device.empty()) {
		throw std::invalid_argument("enabled=true 时 device 不能为空");
	}
	if (parsed.baud_rate <= 0) {
		throw std::invalid_argument("baud_rate 必须大于 0");
	}
	if (parsed.data_bits < 5 || parsed.data_bits > 8) {
		throw std::invalid_argument("data_bits 必须在 5~8 范围内");
	}
	if (parsed.stop_bits != 1 && parsed.stop_bits != 2) {
		throw std::invalid_argument("stop_bits 当前仅支持 1 或 2");
	}
	if (!IsAsciiCommand(parsed.open_cmd) || !IsAsciiCommand(parsed.lock_cmd)) {
		throw std::invalid_argument("open_cmd 和 lock_cmd 必须是非空可打印 ASCII 命令");
	}

	MYLOG_INFO("[空投锁] 初始化参数解析完成：enabled={}, device={}, baud_rate={}, data_bits={}, stop_bits={}, default_PWM_frequency={}, open_cmd={}, lock_cmd={}",
			   parsed.enabled ? "true" : "false",
			   parsed.device,
			   parsed.baud_rate,
			   parsed.data_bits,
			   parsed.stop_bits,
			   parsed.default_pwm_frequency,
			   parsed.open_cmd,
			   parsed.lock_cmd);
	return parsed;
}

std::string AirdropLockManager::NormalizeDutyCycleCommand(const std::string& duty_cycle) {
	std::string value = TrimCopy(duty_cycle);
	if (value.empty()) {
		throw std::invalid_argument("占空比/频率不能为空");
	}

	if (value.size() >= 1 && (value.front() == 'F' || value.front() == 'f')) {
		value.erase(value.begin());
	}

	if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char ch) {
			return std::isdigit(ch) != 0;
		})) {
		throw std::invalid_argument("占空比/频率必须是数字，示例：50 或 F050");
	}

	const int numeric_value = std::stoi(value);
	if (numeric_value < 0 || numeric_value > 999) {
		throw std::invalid_argument("占空比/频率范围必须在 0~999 之间");
	}

	std::string digits = std::to_string(numeric_value);
	while (digits.size() < 3) {
		digits.insert(digits.begin(), '0');
	}
	return "F" + digits;
}

std::string AirdropLockManager::StateToString(AirdropLockState state) {
	switch (state) {
		case AirdropLockState::Uninitialized: return "uninitialized";
		case AirdropLockState::Disabled: return "disabled";
		case AirdropLockState::Closed: return "closed";
		case AirdropLockState::Open: return "open";
		case AirdropLockState::Error: return "error";
		default: return "unknown";
	}
}

bool AirdropLockManager::SendCommandLocked(const std::string& command,
										   const std::string& action_name,
										   std::string* err) {
	if (!initialized_ && action_name.rfind("初始化", 0) != 0) {
		SetErrorLocked("空投锁管理器尚未初始化");
		if (err != nullptr) {
			*err = last_error_;
		}
		MYLOG_WARN("[空投锁] {} 失败：管理器尚未初始化", action_name);
		return false;
	}

	if (!config_.enabled) {
		SetErrorLocked("空投锁模块已禁用，拒绝下发设备命令");
		if (err != nullptr) {
			*err = last_error_;
		}
		MYLOG_WARN("[空投锁] {} 被拒绝：模块当前为 disabled", action_name);
		return false;
	}

	if (!serial_.IsOpen()) {
		SetErrorLocked("空投锁串口未打开");
		if (err != nullptr) {
			*err = last_error_;
		}
		MYLOG_ERROR("[空投锁] {} 失败：串口未打开，device={}", action_name, config_.device);
		return false;
	}

	std::string write_error;
	const size_t written = serial_.Write(command, &write_error);
	if (!write_error.empty() || written != command.size()) {
		SetErrorLocked("命令发送失败，action=" + action_name + ", command=" + command +
					   ", written=" + std::to_string(written) +
					   ", error=" + write_error);
		if (err != nullptr) {
			*err = last_error_;
		}
		MYLOG_ERROR("[空投锁] {} 失败：command={}, written={}, expected={}, error={}",
					action_name, command, written, command.size(), write_error);
		return false;
	}

	last_command_ = command;
	last_error_.clear();
	if (err != nullptr) {
		err->clear();
	}
	MYLOG_INFO("[空投锁] {} 成功：command={}, 写入字节数={}", action_name, command, written);
	return true;
}

void AirdropLockManager::SetErrorLocked(const std::string& error) {
	last_error_ = error;
	state_ = config_.enabled ? AirdropLockState::Error : AirdropLockState::Disabled;
}

} // namespace my_airdrop_lock
