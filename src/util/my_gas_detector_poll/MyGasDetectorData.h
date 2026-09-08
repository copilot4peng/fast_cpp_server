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
    int address;                            ///< 从站地址。
    bool valid;                             ///< 本次结果是否通过通信和协议校验。
    bool timeout;                           ///< 是否因超时失败。
    bool crc_ok;                            ///< CRC 校验是否通过。
    int function_code;                      ///< 响应功能码。
    int byte_count;                         ///< 数据区字节数。
    int exception_code;                     ///< Modbus 异常响应中的异常码。
    std::uint16_t crc_received;             ///< 收到的 CRC 原始值。
    std::uint16_t crc_calculated;           ///< 根据响应帧计算得到的 CRC。
    double elapsed_ms;                      ///< 本次查询耗时，单位毫秒。
    std::uint16_t definition_register;      ///< R0 定义寄存器，包含单位编码和小数位信息。
    std::string unit;                       ///< 单位名称。
    int decimal_places;                     ///< 浓度显示所需的小数位数。
    std::uint16_t concentration_register;   ///< R1 当前浓度原始寄存器值。
    double concentration;                   ///< 当前浓度数值，保留原始寄存器值。
    std::string concentration_text;         ///< 当前浓度的格式化文本。
    std::uint16_t low_alarm_register;       ///< R2 低报警阈值原始寄存器值。
    double low_alarm;                       ///< 低报警阈值数值，保留原始寄存器值。
    std::string low_alarm_text;             ///< 低报警阈值的格式化文本。
    std::uint16_t high_alarm_register;      ///< R3 高报警阈值原始寄存器值。
    double high_alarm;                      ///< 高报警阈值数值，保留原始寄存器值。
    std::string high_alarm_text;            ///< 高报警阈值的格式化文本。
    std::uint16_t range_register;           ///< R4 气体量程原始寄存器值。
    double range_value;                     ///< 气体量程数值，保留原始寄存器值。
    std::string range_text;                 ///< 气体量程的格式化文本。
    std::uint16_t status_register;          ///< R5 传感器工作状态原始编码。
    std::string status;                     ///< 传感器工作状态文本。
    std::uint16_t ad_register;              ///< R6 传感器实时 AD 原始寄存器值。
    int ad_value;                           ///< 传感器实时 AD 数值。
    std::uint16_t temperature_register;     ///< R7 环境温度原始寄存器值。
    double temperature;                     ///< 环境温度，单位摄氏度。
    int gas_code;                           ///< R8 高字节中的气体类型编码。
    std::string gas;                        ///< 气体类型名称。
    std::uint16_t gas_register;             ///< R8 气体类型寄存器原始值。
    std::uint16_t humidity_register;        ///< R9 环境湿度原始寄存器值。
    double humidity;                        ///< 环境湿度，单位百分比。
    std::string timestamp;                  ///< 本次结果生成时间戳。
    std::string error;                      ///< 失败原因或状态说明。
    std::uint64_t sequence;                 ///< 本次结果的序号。

    /** @brief 构造一个“尚未查询”的结果对象。 */
    GasDetectorData();

    /** @brief 将结果转换成线程安全快照可用的 JSON 对象。 */
    nlohmann::json ToJson() const;

    nlohmann::json ToSimpleCNJson() const;

    nlohmann::json ToSimpleENJson() const;
};

} // namespace my_gas_detector_poll
