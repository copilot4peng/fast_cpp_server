#pragma once

/**
 * @brief 气体检测仪轮询模块的数据类型定义。
 *
 * 这里只放配置结构和单次查询结果结构，便于其它模块单独复用这些类型，
 * 而不需要引入完整的轮询单例实现。
 */

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace my_gas_detector_poll {

/**
 * @brief 一台设备最近一次查询的解析结果。
 *
 * valid=false 表示本次结果没有通过完整的通信/协议校验；此时 error
 * 保存可供日志和状态接口展示的中文原因。valid=true 时，数值字段均为
 * 已经完成单位换算后的工程值，同时保留了寄存器原始值。
 */
struct GasDetectorData {
    int address;
    bool valid;
    bool timeout;
    bool crc_ok;
    int function_code;
    int byte_count;
    int exception_code;
    std::uint16_t crc_received;
    std::uint16_t crc_calculated;
    double elapsed_ms;

    std::uint16_t definition_register;
    std::string unit;
    int decimal_places;
    std::uint16_t concentration_register;
    double concentration;
    std::string concentration_text;
    std::uint16_t low_alarm_register;
    double low_alarm;
    std::string low_alarm_text;
    std::uint16_t high_alarm_register;
    double high_alarm;
    std::string high_alarm_text;
    std::uint16_t range_register;
    double range_value;
    std::string range_text;
    std::uint16_t status_register;
    std::string status;
    std::uint16_t ad_register;
    int ad_value;
    std::uint16_t temperature_register;
    double temperature;
    int gas_code;
    std::string gas;
    std::uint16_t gas_register;
    std::uint16_t humidity_register;
    double humidity;
    std::string timestamp;
    std::string error;
    std::uint64_t sequence;

    /** @brief 构造一个“尚未查询”的结果对象。 */
    GasDetectorData();

    /** @brief 将结果转换成线程安全快照可用的 JSON 对象。 */
    nlohmann::json ToJson() const;

    nlohmann::json ToSimpleCNJson() const;

    nlohmann::json ToSimpleENJson() const;
};

} // namespace my_gas_detector_poll
