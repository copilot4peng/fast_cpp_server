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
MyAPIResponsePtr HeartBeatController::getHeartbeat() {
    MYLOG_INFO("[API] Heartbeat GET");

    return ok("alive");
}

// POST /v1/heartbeat

MyAPIResponsePtr HeartBeatController::postHeartbeat(
    const oatpp::Object<my_api::dto::HeartbeatDto>& heartbeat
) {
    if (!heartbeat) {
        return badRequest("heartbeat body is empty");
    }

    auto result =
        oatpp::Vector<oatpp::Object<my_api::dto::HeartbeatDto>>::createShared();
    MYLOG_INFO(
        "[API] Heartbeat POST from={}, timestamp={}, status={}",
        heartbeat->from ? heartbeat->from->c_str() : "null",
        heartbeat->timestamp ? *heartbeat->timestamp : 0,
        heartbeat->status ? heartbeat->status->c_str() : "null"
    );

    // 👉 后续你可以在这里：
    // - 更新设备在线状态
    // - 写数据库
    // - 更新心跳时间戳
    heartbeat->heartbeat = HeartbeatManager::GetInstance().GetHeartbeatSnapshot().dump();
    result->push_back(heartbeat);

    // return ok("heartbeat received");
    // return createDtoResponse(Status::CODE_200, heartbeat);
    return createDtoResponse(Status::CODE_200, result);
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
