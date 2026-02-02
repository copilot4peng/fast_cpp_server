#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include <sstream>

namespace my_api::validators {

/**
 * validateNotEmpty - 校验字符串非空
 * 返回 true 表示通过，false 表示失败（并可在调用方返回 400）
 */
inline bool validateNotEmpty(const std::string& v, const std::string& fieldName) {
    if (v.empty()) {
        // 可改为更丰富的日志或错误码
        return false;
    }
    return true;
}

/**
 * validateMaxLength - 校验字符串长度上限（用于防止大 payload）
 * 返回 true 表示通过；当失败时 err_out 会被填充
 */
inline bool validateMaxLength(const std::string& v, size_t maxLen, const std::string& fieldName, std::string& err_out) {
    if (v.size() > maxLen) {
        std::ostringstream ss;
        ss << fieldName << " length exceed max " << maxLen;
        err_out = ss.str();
        return false;
    }
    return true;
}

/**
 * parseJsonString - 将 JSON 字符串解析为 nlohmann::json，捕获异常并返回错误信息
 */
inline bool parseJsonString(const std::string& s, nlohmann::json& out, std::string& err_out) {
    try {
        out = nlohmann::json::parse(s);
        return true;
    } catch (const nlohmann::json::parse_error& e) {
        err_out = e.what();
        return false;
    } catch (const std::exception& e) {
        err_out = e.what();
        return false;
    }
}

/**
 * optional: whitelistKeysCheck - 验证 JSON 对象仅包含允许的 key（白名单）
 * 返回 false 时，err_out 包含异常描述
 */
inline bool whitelistKeysCheck(const nlohmann::json& j, const std::vector<std::string>& allowedKeys, std::string& err_out) {
    if (!j.is_object()) {
        err_out = "not an object";
        return false;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& k = it.key();
        if (std::find(allowedKeys.begin(), allowedKeys.end(), k) == allowedKeys.end()) {
            std::ostringstream ss;
            ss << "unexpected key: " << k;
            err_out = ss.str();
            return false;
        }
    }
    return true;
}

} // namespace my_api::validators