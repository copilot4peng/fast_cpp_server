#include "AirdropLockController.h"

#include <nlohmann/json.hpp>

#include "MyLog.h"
#include "my_airdrop_lock.h"

namespace my_api::airdrop_lock_api {

using namespace my_api::base;

namespace {

std::string TrimCopy(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

} // namespace

AirdropLockController::AirdropLockController(const std::shared_ptr<ObjectMapper>& objectMapper)
    : BaseApiController(objectMapper) {}

std::shared_ptr<AirdropLockController> AirdropLockController::createShared(
    const std::shared_ptr<ObjectMapper>& objectMapper) {
    return std::make_shared<AirdropLockController>(objectMapper);
}

MyAPIResponsePtr AirdropLockController::initAirdropLock(const oatpp::String& body) {
    MYLOG_INFO("[API-空投锁] POST /v1/airdrop_lock/init 收到初始化请求");
    if (!body || body->empty()) {
        MYLOG_WARN("[API-空投锁] 初始化失败：请求体为空");
        return jsonError(400, "请求体不能为空，需要提供空投锁初始化 JSON");
    }

    nlohmann::json config = nlohmann::json::parse(body->c_str(), nullptr, false);
    if (config.is_discarded() || !config.is_object()) {
        MYLOG_WARN("[API-空投锁] 初始化失败：请求体不是合法 JSON 对象");
        return jsonError(400, "请求体必须是合法 JSON 对象");
    }

    std::string error;
    auto& manager = my_airdrop_lock::AirdropLockManager::GetInstance();
    if (!manager.Init(config, &error)) {
        MYLOG_ERROR("[API-空投锁] 初始化失败：{}", error);
        return jsonError(500, "空投锁初始化失败", {{"error", error}});
    }

    MYLOG_INFO("[API-空投锁] 初始化成功，已完成默认占空比/频率设置和关闭锁保护动作");
    return jsonOk(manager.GetStatus(), "空投锁初始化成功");
}

MyAPIResponsePtr AirdropLockController::setDutyCycle(
    const oatpp::Object<my_api::dto::AirdropLockDutyCycleDto>& dutyDto) {
    if (!dutyDto || !dutyDto->duty_cycle) {
        MYLOG_WARN("[API-空投锁] 设置占空比/频率失败：缺少 duty_cycle 参数");
        return jsonError(400, "缺少参数 duty_cycle，示例：50 或 F050");
    }

    const std::string duty_cycle = TrimCopy(dutyDto->duty_cycle->c_str());
    if (duty_cycle.empty()) {
        MYLOG_WARN("[API-空投锁] 设置占空比/频率失败：duty_cycle 为空");
        return jsonError(400, "参数 duty_cycle 不能为空");
    }

    MYLOG_INFO("[API-空投锁] POST /v1/airdrop_lock/duty_cycle 设置占空比/频率: {}", duty_cycle);
    std::string error;
    auto& manager = my_airdrop_lock::AirdropLockManager::GetInstance();
    if (!manager.SetDutyCycle(duty_cycle, &error)) {
        MYLOG_ERROR("[API-空投锁] 设置占空比/频率失败：{}", error);
        return jsonError(503, "设置空投锁占空比/频率失败", {{"error", error}});
    }

    return jsonOk(manager.GetStatus(), "占空比/频率设置成功");
}

MyAPIResponsePtr AirdropLockController::openDropper() {
    MYLOG_INFO("[API-空投锁] POST /v1/airdrop_lock/open 请求打开空投锁");
    std::string error;
    auto& manager = my_airdrop_lock::AirdropLockManager::GetInstance();
    if (!manager.OpenDropper(&error)) {
        MYLOG_ERROR("[API-空投锁] 打开空投锁失败：{}", error);
        return jsonError(503, "打开空投锁失败", {{"error", error}});
    }

    return jsonOk(manager.GetStatus(), "空投锁已打开");
}

MyAPIResponsePtr AirdropLockController::closeDropper() {
    MYLOG_INFO("[API-空投锁] POST /v1/airdrop_lock/close 请求关闭空投锁");
    std::string error;
    auto& manager = my_airdrop_lock::AirdropLockManager::GetInstance();
    if (!manager.CloseDropper(&error)) {
        MYLOG_ERROR("[API-空投锁] 关闭空投锁失败：{}", error);
        return jsonError(503, "关闭空投锁失败", {{"error", error}});
    }

    return jsonOk(manager.GetStatus(), "空投锁已关闭");
}

MyAPIResponsePtr AirdropLockController::getStatus() {
    MYLOG_INFO("[API-空投锁] GET /v1/airdrop_lock/status 获取空投锁状态");
    return jsonOk(my_airdrop_lock::AirdropLockManager::GetInstance().GetStatus(), "空投锁状态获取成功");
}

} // namespace my_api::airdrop_lock_api