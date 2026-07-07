#pragma once

/**
 * @file AirdropLockDto.hpp
 * @brief 空投锁模块 API 的 DTO 定义
 */

#include "oatpp/core/Types.hpp"
#include "oatpp/core/macro/codegen.hpp"

namespace my_api::dto {

#include OATPP_CODEGEN_BEGIN(DTO)

/**
 * @brief 空投锁初始化请求 DTO
 */
class AirdropLockInitDto : public oatpp::DTO {
    DTO_INIT(AirdropLockInitDto, DTO)

    /** 是否启用空投锁模块 */
    DTO_FIELD(Boolean, enabled);

    /** 串口设备路径，例如 /dev/ttyUSB0 */
    DTO_FIELD(String, device);

    /** 波特率，例如 9600 */
    DTO_FIELD(Int32, baud_rate);

    /** 数据位，例如 8 */
    DTO_FIELD(Int32, data_bits);

    /** 停止位，例如 1 */
    DTO_FIELD(Int32, stop_bits);

    /** 默认占空比/频率值，例如 50，管理器会转换为 F050 */
    DTO_FIELD(String, default_PWM_frequency);

    /** 开锁命令，例如 D005 */
    DTO_FIELD(String, open_cmd);

    /** 关锁命令，例如 D010 */
    DTO_FIELD(String, lock_cmd);
};

/**
 * @brief 设置占空比/频率请求 DTO
 */
class AirdropLockDutyCycleDto : public oatpp::DTO {
    DTO_INIT(AirdropLockDutyCycleDto, DTO)

    /** 占空比/频率值，支持 50 或 F050 形式 */
    DTO_FIELD(String, duty_cycle);
};

#include OATPP_CODEGEN_END(DTO)

} // namespace my_api::dto