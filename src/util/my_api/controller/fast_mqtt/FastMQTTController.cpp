#include "FastMQTTController.h"

#include "MyLog.h"
#include "FastMQTT.hpp"

namespace my_api::fast_mqtt_api {

using namespace my_api::base;

FastMQTTController::FastMQTTController(const std::shared_ptr<ObjectMapper>& objectMapper)
    : BaseApiController(objectMapper) {}

std::shared_ptr<FastMQTTController> FastMQTTController::createShared(
    const std::shared_ptr<ObjectMapper>& objectMapper) {
    return std::make_shared<FastMQTTController>(objectMapper);
}

MyAPIResponsePtr FastMQTTController::getStatus() {
    MYLOG_INFO("[API-FastMQTT] GET /v1/fast_mqtt/status");

    try {
        auto& mqtt = fast_mqtt::FastMQTT::GetInstance();
        nlohmann::json data = mqtt.Status();
        data["statistics"] = mqtt.GetStatistics();
        data["health"] = mqtt.GetHealthStatus();
        return jsonOk(data, "获取 FastMQTT 状态成功");
    } catch (const std::exception& e) {
        MYLOG_ERROR("[API-FastMQTT] 获取状态失败: {}", e.what());
        return jsonError(500, std::string("获取 FastMQTT 状态失败: ") + e.what());
    }
}

}  // namespace my_api::fast_mqtt_api