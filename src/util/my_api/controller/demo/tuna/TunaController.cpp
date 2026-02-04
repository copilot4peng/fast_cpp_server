#pragma once

#include "BaseApiController.hpp"
#include "TunaController.h"
#include "oatpp/web/server/api/ApiController.hpp"
#include "oatpp/core/macro/codegen.hpp"

#include "MyLog.h"


namespace my_api::tuna {

#include OATPP_CODEGEN_BEGIN(ApiController)

using my_api::base::MyAPIResponsePtr;

    
TunaController::TunaController(
    const std::shared_ptr<ObjectMapper>& objectMapper
)
    : base::BaseApiController(objectMapper) {}


std::shared_ptr<TunaController> TunaController::createShared(
    const std::shared_ptr<ObjectMapper>& objectMapper
) {
    return std::make_shared<TunaController>(objectMapper);
}

MyAPIResponsePtr TunaController::tunaDown() {
    MYLOG_INFO("[API] 收到请求: GET /v1/tuna/down");

    // 模拟让金枪鱼潜航器下潜的逻辑
    MYLOG_INFO("金枪鱼潜航器正在下潜...");

    return ok("Tuna is diving down.");
}


MyAPIResponsePtr TunaController::tunaUp() {
    MYLOG_INFO("[API] 收到请求: POST /v1/tuna/up");

    // 模拟让金枪鱼潜航器上浮的逻辑
    MYLOG_INFO("金枪鱼潜航器正在上浮...");

    return ok("Tuna is surfacing up.");
}

#include OATPP_CODEGEN_END(ApiController)

} // namespace my_api::tuna