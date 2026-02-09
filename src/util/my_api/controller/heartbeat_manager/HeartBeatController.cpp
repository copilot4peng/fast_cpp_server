#include "HeartBeatController.h"
#include "dto/HeartbeatDto.hpp"
#include "MyHeartbeatManager.h"
#include "BaseApiController.hpp"
#include "MyLog.h"

namespace my_api::heartbeat {

using namespace my_api::base;
using namespace my_heartbeat;

HeartBeatController::HeartBeatController(const std::shared_ptr<ObjectMapper>& objectMapper)
    : BaseApiController(objectMapper) {}

std::shared_ptr<HeartBeatController> HeartBeatController::createShared(
    const std::shared_ptr<ObjectMapper>& objectMapper
) {
    return std::make_shared<HeartBeatController>(objectMapper);
}

// GET /v1/heartbeat
MyAPIResponsePtr HeartBeatController::getHeartbeatOnline() {
    MYLOG_INFO("[API] Heartbeat GET");

    std::string status = "{\"status\": \"alive\"}";
    auto resp = createResponse(Status::CODE_200, status);
    resp->putHeader("Content-Type", "application/json");
    return resp;
}

// GET /v1/heartbeat/data

MyAPIResponsePtr HeartBeatController::getHeartbeatData() {
    MYLOG_INFO("[API] Heartbeat GET Data");

    // 👉 后续你可以在这里：
    // - 更新设备在线状态
    // - 写数据库
    // - 更新心跳时间戳
    auto heartbeat = HeartbeatManager::GetInstance().GetHeartbeatSnapshot().dump();

    // return ok("heartbeat received");
    // return createDtoResponse(Status::CODE_200, heartbeat);

    auto resp = createResponse(Status::CODE_200, heartbeat);
    resp->putHeader("Content-Type", "application/json");
    return resp;
}


MyAPIResponsePtr HeartBeatController::getHeartbeatConfig() {
    MYLOG_INFO("[API] Heartbeat GET Config");

    nlohmann::json config = HeartbeatManager::GetInstance().GetInitConfig();

    auto dto = my_api::dto::HeartbeatDto::createShared();
    dto->from = "HeartbeatManager";
    dto->timestamp = static_cast<v_int64>(std::time(nullptr));
    dto->status = "config";
    dto->heartbeat = config.dump();

    return createDtoResponse(Status::CODE_200, dto);
}


} // namespace my_api::heartbeat
