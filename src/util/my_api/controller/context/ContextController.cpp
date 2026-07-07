/**
 * @file ContextController.cpp
 * @brief 运行时上下文（MyContext）REST API 控制器实现
 */

#include "ContextController.h"

#include <nlohmann/json.hpp>

#include "MyLog.h"
#include "MyContext.h"

namespace my_api::context_api {

using namespace my_api::base;

// ============================================================================
// 构造 / 工厂
// ============================================================================

ContextController::ContextController(const std::shared_ptr<ObjectMapper>& objectMapper)
    : BaseApiController(objectMapper) {}

std::shared_ptr<ContextController> ContextController::createShared(
    const std::shared_ptr<ObjectMapper>& objectMapper) {
    return std::make_shared<ContextController>(objectMapper);
}

// ============================================================================
// 内部辅助
// ============================================================================

namespace {

/**
 * @brief 将 oatpp::Any 转成 nlohmann::json
 * @return 解析成功返回 true，并写入 out；失败写入 error
 */
bool ConvertAnyToJson(const oatpp::Any& value,
                      const std::shared_ptr<oatpp::data::mapping::ObjectMapper>& objectMapper,
                      nlohmann::json& out,
                      std::string& error) {
    if (!objectMapper) {
        error = "对象映射器不可用";
        return false;
    }

    try {
        const oatpp::Void valueVoid(value);
        const oatpp::String jsonText = objectMapper->writeToString(valueVoid);
        if (!jsonText) {
            error = "value 序列化失败";
            return false;
        }

        nlohmann::json parsed = nlohmann::json::parse(jsonText->c_str(), nullptr, false);
        if (parsed.is_discarded()) {
            error = "value 不是合法的 JSON";
            return false;
        }

        out = std::move(parsed);
        return true;
    } catch (const std::exception& e) {
        error = std::string("解析 value 失败: ") + e.what();
        return false;
    }
}

}  // namespace

// ============================================================================
// 存活检查
// ============================================================================

MyAPIResponsePtr ContextController::getOnline() {
    MYLOG_INFO("[API-Context] GET /v1/context/online");
    return jsonOk({{"status", "alive"}}, "context api alive");
}

// ============================================================================
// 获取所有上下文
// ============================================================================

MyAPIResponsePtr ContextController::getAllContext() {
    MYLOG_INFO("[API-Context] GET /v1/context/all");
    try {
        nlohmann::json data = my_comm::MyContext::GetInstance().GetAllJson();
        return jsonOk(data, "获取全部上下文成功");
    } catch (const std::exception& e) {
        MYLOG_ERROR("[API-Context] 获取全部上下文失败: {}", e.what());
        return jsonError(500, std::string("获取全部上下文失败: ") + e.what());
    }
}

// ============================================================================
// 查看某个上下文
// ============================================================================

MyAPIResponsePtr ContextController::getContext(
    const oatpp::Object<my_api::dto::ContextNameRequestDto>& requestDto) {
    try {
        if (!requestDto || !requestDto->name || requestDto->name->empty()) {
            return jsonError(400, "缺少 name 字段");
        }
        const std::string name = requestDto->name->c_str();
        MYLOG_INFO("[API-Context] POST /v1/context/get name={}", name);

        nlohmann::json entry;
        if (!my_comm::MyContext::GetInstance().GetEntryJson(name, entry)) {
            return jsonError(404, "上下文不存在: " + name);
        }
        return jsonOk(entry, "查询上下文成功");
    } catch (const std::exception& e) {
        MYLOG_ERROR("[API-Context] 查询上下文失败: {}", e.what());
        return jsonError(500, std::string("查询上下文失败: ") + e.what());
    }
}

// ============================================================================
// 设置（upsert）上下文
// ============================================================================

MyAPIResponsePtr ContextController::setContext(
    const oatpp::Object<my_api::dto::ContextWriteRequestDto>& requestDto) {
    try {
        if (!requestDto || !requestDto->name || requestDto->name->empty()) {
            return jsonError(400, "缺少有效的 name 字段");
        }

        const std::string name = requestDto->name->c_str();
        nlohmann::json valueJson;
        std::string valueErr;
        if (!ConvertAnyToJson(requestDto->value, getDefaultObjectMapper(), valueJson, valueErr)) {
            return jsonError(400, valueErr);
        }

        std::string description;
        if (requestDto->description && !requestDto->description->empty()) {
            description = requestDto->description->c_str();
        }

        MYLOG_INFO("[API-Context] POST /v1/context/set name={}", name);

        if (!my_comm::MyContext::GetInstance().Set(name, valueJson, description)) {
            return jsonError(500, "设置上下文失败");
        }
        nlohmann::json entry;
        my_comm::MyContext::GetInstance().GetEntryJson(name, entry);
        return jsonOk(entry, "设置上下文成功");
    } catch (const std::exception& e) {
        MYLOG_ERROR("[API-Context] 设置上下文失败: {}", e.what());
        return jsonError(500, std::string("设置上下文失败: ") + e.what());
    }
}

// ============================================================================
// 修改某个已存在的上下文
// ============================================================================

MyAPIResponsePtr ContextController::updateContext(
    const oatpp::Object<my_api::dto::ContextWriteRequestDto>& requestDto) {
    try {
        if (!requestDto || !requestDto->name || requestDto->name->empty()) {
            return jsonError(400, "缺少有效的 name 字段");
        }

        const std::string name = requestDto->name->c_str();
        nlohmann::json valueJson;
        std::string valueErr;
        if (!ConvertAnyToJson(requestDto->value, getDefaultObjectMapper(), valueJson, valueErr)) {
            return jsonError(400, valueErr);
        }

        MYLOG_INFO("[API-Context] POST /v1/context/update name={}", name);

        std::string err;
        if (!my_comm::MyContext::GetInstance().UpdateValue(name, valueJson, err)) {
            return jsonError(404, err);
        }
        if (requestDto->description && !requestDto->description->empty()) {
            std::string desc_err;
            my_comm::MyContext::GetInstance().UpdateDescription(
                name, requestDto->description->c_str(), desc_err);
        }
        nlohmann::json entry;
        my_comm::MyContext::GetInstance().GetEntryJson(name, entry);
        return jsonOk(entry, "修改上下文成功");
    } catch (const std::exception& e) {
        MYLOG_ERROR("[API-Context] 修改上下文失败: {}", e.what());
        return jsonError(500, std::string("修改上下文失败: ") + e.what());
    }
}

// ============================================================================
// 删除某个上下文
// ============================================================================

MyAPIResponsePtr ContextController::deleteContext(
    const oatpp::Object<my_api::dto::ContextNameRequestDto>& requestDto) {
    try {
        if (!requestDto || !requestDto->name || requestDto->name->empty()) {
            return jsonError(400, "缺少 name 字段");
        }
        const std::string name = requestDto->name->c_str();
        MYLOG_INFO("[API-Context] POST /v1/context/delete name={}", name);

        if (!my_comm::MyContext::GetInstance().Erase(name)) {
            return jsonError(404, "上下文不存在: " + name);
        }
        return jsonOk(nlohmann::json::object(), "删除上下文成功");
    } catch (const std::exception& e) {
        MYLOG_ERROR("[API-Context] 删除上下文失败: {}", e.what());
        return jsonError(500, std::string("删除上下文失败: ") + e.what());
    }
}

}  // namespace my_api::context_api
