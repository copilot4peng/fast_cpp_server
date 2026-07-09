#pragma once

/**
 * @file AirdropLockController.h
 * @brief 空投锁模块 REST API 控制器
 *
 * 对外暴露以下接口：
 * - POST /v1/airdrop_lock/init            : 初始化空投锁管理器
 * - POST /v1/airdrop_lock/duty_cycle      : 设置占空比/频率
 * - POST /v1/airdrop_lock/open            : 打开空投锁
 * - POST /v1/airdrop_lock/close           : 关闭空投锁
 * - GET  /v1/airdrop_lock/status          : 获取空投锁状态
 */

#include "BaseApiController.hpp"
#include "dto/airdrop_lock/AirdropLockDto.hpp"
#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/web/server/api/ApiController.hpp"

namespace my_api::airdrop_lock_api {

#include OATPP_CODEGEN_BEGIN(ApiController)

class AirdropLockController : public base::BaseApiController {
public:
    static constexpr const char* SWAGGER_TAG = "AirdropLockController";
    explicit AirdropLockController(const std::shared_ptr<ObjectMapper>& objectMapper);

    static std::shared_ptr<AirdropLockController> createShared(
        const std::shared_ptr<ObjectMapper>& objectMapper);

    ENDPOINT_INFO(initAirdropLock) {
        info->addTag(SWAGGER_TAG);
        info->summary = "初始化空投锁管理器";
        info->description = "使用 JSON 参数初始化空投锁串口。初始化成功后会自动设置默认占空比/频率并关闭空投锁。";
        info->addConsumes<oatpp::Object<my_api::dto::AirdropLockInitDto>>("application/json");
        info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
        info->addResponse<oatpp::String>(Status::CODE_400, "application/json");
        info->addResponse<oatpp::String>(Status::CODE_500, "application/json");
    }
    ENDPOINT("POST", "/v1/airdrop_lock/init", initAirdropLock,
             BODY_STRING(oatpp::String, body));

    ENDPOINT_INFO(setDutyCycle) {
        info->addTag(SWAGGER_TAG);
        info->summary = "设置空投锁占空比/频率";
        info->description = "设置空投锁 PWM 占空比/频率，支持 duty_cycle=50 或 F050，最终下发 ASCII 命令 F050。";
        info->addConsumes<oatpp::Object<my_api::dto::AirdropLockDutyCycleDto>>("application/json");
        info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
        info->addResponse<oatpp::String>(Status::CODE_400, "application/json");
        info->addResponse<oatpp::String>(Status::CODE_503, "application/json");
    }
    ENDPOINT("POST", "/v1/airdrop_lock/duty_cycle", setDutyCycle,
             BODY_DTO(oatpp::Object<my_api::dto::AirdropLockDutyCycleDto>, dutyDto));

    ENDPOINT_INFO(openDropper) {
        info->addTag(SWAGGER_TAG);
        info->summary = "打开空投锁";
        info->description = "向空投锁设备下发 open_cmd，默认命令为 D005。";
        info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
        info->addResponse<oatpp::String>(Status::CODE_503, "application/json");
    }
    ENDPOINT("POST", "/v1/airdrop_lock/open", openDropper);

    ENDPOINT_INFO(closeDropper) {
        info->addTag(SWAGGER_TAG);
        info->summary = "关闭空投锁";
        info->description = "向空投锁设备下发 lock_cmd，默认命令为 D010。";
        info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
        info->addResponse<oatpp::String>(Status::CODE_503, "application/json");
    }
    ENDPOINT("POST", "/v1/airdrop_lock/close", closeDropper);

    ENDPOINT_INFO(getStatus) {
        info->addTag(SWAGGER_TAG);
        info->summary = "获取空投锁状态";
        info->description = "返回空投锁初始化状态、串口状态、锁状态、最近命令和最近错误。";
        info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
    }
    ENDPOINT("GET", "/v1/airdrop_lock/status", getStatus);
};

#include OATPP_CODEGEN_END(ApiController)

} // namespace my_api::airdrop_lock_api