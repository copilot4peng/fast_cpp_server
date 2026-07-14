#pragma once

/**
 * @brief 气体检测仪轮询模块的数据类型定义。
 *
 * 这里只放配置结构和单次查询结果结构，便于其它模块单独复用这些类型，
 * 而不需要引入完整的轮询单例实现。
 */

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace my_gas_detector_poll {

/**
 * @brief 气体检测仪轮询配置。
 *
 * 配置字段与 Python 工具保持一致，同时兼容项目中串口模块常用的
 * device/port、baud_rate/baudrate 等字段名称。所有时间单位均为毫秒。
 */
struct GasDetectorPollConfig {
    bool enabled;
    std::string device;
    std::uint32_t baud_rate;
    int data_bits;
    int stop_bits;
    std::string parity;
    std::string flowcontrol;
    std::vector<std::uint8_t> addresses;
    std::uint32_t interval_ms;
    std::uint32_t timeout_ms;
    std::uint16_t start_register;
    std::uint16_t register_count;
    std::uint32_t read_slice_timeout_ms;
    bool log_raw_frame;
    bool debug_log_enabled;
    bool simulate_mode;

    /**
     * @brief 构造与 Python 程序一致的默认配置。
     * 默认轮询 1～6 号设备，串口参数为 9600、8N1。
     */
    GasDetectorPollConfig();

    /** @brief 将规范化后的配置转换成 JSON，便于状态接口返回。 */
    nlohmann::json ToJson() const;
};

} // namespace my_gas_detector_poll
