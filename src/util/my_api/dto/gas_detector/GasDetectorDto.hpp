#pragma once

/**
 * @file GasDetectorDto.hpp
 * @brief 气体检测仪 API 请求 DTO 定义。
 *
 * 约定：
 * - 所有数值参数均使用 JSON 数字，不接受把数字写成字符串；
 * - addresses 使用数字数组，例如 [1, 2, 3]，不使用 "1-3"；
 * - device、parity、flowcontrol 是设备协议本身的文本字段，因此保留为字符串。
 */

#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

namespace my_api::dto {

#include OATPP_CODEGEN_BEGIN(DTO)

/**
 * @brief 初始化气体检测仪轮询模块的请求参数。
 *
 * 必填数值字段由 controller 统一校验：
 * - baud_rate：大于 0 的波特率；
 * - data_bits：5～8；
 * - stop_bits：1 或 2；
 * - addresses：1～255 的 Modbus 从站地址数字数组；
 * - interval_ms、timeout_ms、read_slice_timeout_ms：大于 0，单位为毫秒；
 * - start_register：0～65535；
 * - register_count：当前协议固定为 10。
 */
class GasDetectorConfigDto : public oatpp::DTO {
    DTO_INIT(GasDetectorConfigDto, DTO)

    /** 是否启用气体检测仪轮询模块。 */
    DTO_FIELD(Boolean, enabled);

    /** 串口设备路径，例如 /dev/ttyUSB0。 */
    DTO_FIELD(String, device);

    /** 串口波特率，例如 9600；必须是 JSON 数字。 */
    DTO_FIELD(UInt32, baud_rate);

    /** 串口数据位，允许值为 5、6、7、8。 */
    DTO_FIELD(Int32, data_bits);

    /** 串口停止位，允许值为 1 或 2。 */
    DTO_FIELD(Int32, stop_bits);

    /** 串口校验方式：none、odd、even、mark、space。 */
    DTO_FIELD(String, parity);

    /** 串口流控方式：none、software、hardware。 */
    DTO_FIELD(String, flowcontrol);

    /** Modbus 从站地址数字数组，每个地址范围为 1～255。 */
    DTO_FIELD(List<Int32>, addresses);

    /** 轮询间隔，单位毫秒，必须大于 0。 */
    DTO_FIELD(UInt32, interval_ms);

    /** 单次响应整体超时时间，单位毫秒，必须大于 0。 */
    DTO_FIELD(UInt32, timeout_ms);

    /** Modbus 起始保持寄存器地址，范围 0～65535。 */
    DTO_FIELD(UInt32, start_register);

    /** 读取寄存器数量，当前气体检测仪协议必须为 10。 */
    DTO_FIELD(UInt32, register_count);

    /** 单次底层串口读取时间片，单位毫秒，必须大于 0 且不大于 timeout_ms。 */
    DTO_FIELD(UInt32, read_slice_timeout_ms);

    /** 是否记录收到的原始串口帧。 */
    DTO_FIELD(Boolean, log_raw_frame);

    /** 是否启用调试日志。 */
    DTO_FIELD(Boolean, debug_log_enabled);

    /** 是否启用模拟模式。 */
    DTO_FIELD(Boolean, simulate_mode);
};

#include OATPP_CODEGEN_END(DTO)

} // namespace my_api::dto
