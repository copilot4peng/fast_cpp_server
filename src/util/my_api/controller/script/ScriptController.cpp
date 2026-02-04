/**
 * File: /home/cs/DockerRoot/fast_cpp_server/src/util/my_api/controller/script/ScriptController.cpp
 *
 * Implementation for MyScriptController
 */

#include "BaseApiController.hpp"
#include "ScriptController.h"
#include "py3/MyPyEnvBot.h"
#include "MyLog.h"

using namespace my_api::my_script_api;
using namespace my_api::base;
using json = nlohmann::json;

MyScriptController::MyScriptController(const std::shared_ptr<ObjectMapper>& objectMapper)
    : base::BaseApiController(objectMapper)
{
        MYLOG_INFO("[MyScriptController] constructed");
}

std::shared_ptr<MyScriptController> MyScriptController::createShared(const std::shared_ptr<ObjectMapper>& objectMapper) {
        return std::make_shared<MyScriptController>(objectMapper);
}

MyAPIResponsePtr MyScriptController::getCurrentPythonInfo(const std::shared_ptr<IncomingRequest>& /*request*/) {
        MYLOG_INFO("[MyScriptController] getCurrentPythonInfo called");

        try {
                json info = py3::MyPyEnvBot::getCurrentPythonInfo(5);
                std::string body = info.dump();
                auto resp = createResponse(Status::CODE_200, body);
                resp->putHeader("Content-Type", "application/json");
                return resp;
        } catch (const std::exception& e) {
                MYLOG_ERROR("[MyScriptController] exception: {}", e.what());
                auto resp = createResponse(Status::CODE_500, std::string("{\"error\":\"internal\"}"));
                resp->putHeader("Content-Type", "application/json");
                return resp;
        }
}

MyAPIResponsePtr MyScriptController::runPythonScript(const oatpp::String& body) {
    MYLOG_INFO("[MyScriptController] runPythonScript called");

    try {
        // 解析请求体 JSON
        auto req_json = json::parse(body->c_str());
        MYLOG_INFO("[MyScriptController] runPythonScript request: \n {}", req_json.dump(4));
        std::string venv_path = req_json.value("venv_path", "/usr/bin/python3");
        std::string script_path = req_json.value("script_path", "");
        std::vector<std::string> args = req_json.value("args", std::vector<std::string>{});
        int timeout_seconds = req_json.value("timeout_seconds", 60);
        size_t memory_limit_bytes = req_json.value("memory_limit_bytes", 0);
        int cpu_time_limit_seconds = req_json.value("cpu_time_limit_seconds", 30);

        // 调用 MyPyEnvBot 执行脚本
        // auto result = py3::MyPyEnvBot::runPythonScript(
        //     venv_path,
        //     script_path,
        //     args,
        //     timeout_seconds,
        //     memory_limit_bytes,
        //     cpu_time_limit_seconds
        // );

        auto result = py3::MyPyEnvBot::runInCurrentEnv(
            script_path,
            args,
            timeout_seconds,
            memory_limit_bytes,
            cpu_time_limit_seconds
        );

        // 构造响应 JSON
        json resp_json;
        resp_json["exit_code"] = result.exit_code;
        resp_json["timed_out"] = result.timed_out;
        resp_json["stdout"] = result.stdout_str;
        resp_json["stderr"] = result.stderr_str;
        std::string resp_body = resp_json.dump();
        auto resp = createResponse(Status::CODE_200, resp_body);
        resp->putHeader("Content-Type", "application/json");
        return resp;
    } catch (const std::exception& e) {
        MYLOG_ERROR("[MyScriptController] exception: {}", e.what());
        auto resp = createResponse(Status::CODE_500, std::string("{\"error\":\"internal\"}"));
        resp->putHeader("Content-Type", "application/json");
        return resp;
    }
}