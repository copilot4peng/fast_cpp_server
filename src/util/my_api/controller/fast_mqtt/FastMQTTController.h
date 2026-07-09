#pragma once

/**
 * @file FastMQTTController.h
 * @brief FastMQTT 运行时状态查询 API 控制器
 *
 * 对外暴露以下接口：
 * - GET /v1/fast_mqtt/status : 查询 MQTT 运行时状态快照
 */

#include "BaseApiController.hpp"
#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/web/server/api/ApiController.hpp"

namespace my_api::fast_mqtt_api {

#include OATPP_CODEGEN_BEGIN(ApiController)

class FastMQTTController : public base::BaseApiController {
public:
    static constexpr const char* SWAGGER_TAG = "FastMQTTController";

    explicit FastMQTTController(const std::shared_ptr<ObjectMapper>& objectMapper);

    static std::shared_ptr<FastMQTTController> createShared(
        const std::shared_ptr<ObjectMapper>& objectMapper);

    ENDPOINT_INFO(getStatus) {
        info->addTag(SWAGGER_TAG);
        info->summary = "查看 FastMQTT 运行时状态";
        info->description = "返回 FastMQTT 单例的运行时快照，包括连接状态、Broker 地址、"
                            "发送/接收队列长度与容量、注册 topic 数量、每个 topic 的回调数。";
        info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
        info->addResponse<oatpp::String>(Status::CODE_500, "application/json");
    }
    ENDPOINT("GET", "/v1/fast_mqtt/status", getStatus);
};

#include OATPP_CODEGEN_END(ApiController)

}  // namespace my_api::fast_mqtt_api