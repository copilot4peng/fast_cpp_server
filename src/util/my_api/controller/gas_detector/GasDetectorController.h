#pragma once

/**
 * @file GasDetectorController.h
 * @brief 气体检测仪轮询模块 REST API 控制器。
 *
 * 对外暴露以下接口（路由前缀 /v1/gas_detector）：
 * - POST /v1/gas_detector/init     : 使用 DTO 初始化串口和轮询配置
 * - POST /v1/gas_detector/start    : 启动轮询线程
 * - POST /v1/gas_detector/stop     : 停止轮询线程并关闭串口
 * - GET  /v1/gas_detector/config   : 获取规范化配置
 * - GET  /v1/gas_detector/realtime : 获取各传感器最近一次实时数值
 * - GET  /v1/gas_detector/simple_status : 获取工作线程、串口和简化传感器状态
 * - GET  /v1/gas_detector/status   : 获取完整运行状态和诊断信息
 */

#include "BaseApiController.hpp"
#include "dto/gas_detector/GasDetectorDto.hpp"
#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/web/server/api/ApiController.hpp"

namespace my_api::gas_detector_api {

#include OATPP_CODEGEN_BEGIN(ApiController)

class GasDetectorController : public base::BaseApiController {
public:
    static constexpr const char* SWAGGER_TAG = "GasDetectorController";

    explicit GasDetectorController(const std::shared_ptr<ObjectMapper>& objectMapper);

    static std::shared_ptr<GasDetectorController> createShared(
        const std::shared_ptr<ObjectMapper>& objectMapper);

    // ==================== 配置与生命周期接口 ====================

    ENDPOINT_INFO(initGasDetector) {
        info->addTag(SWAGGER_TAG);
        info->summary = "初始化气体检测仪轮询模块";
        info->description =
            "使用 DTO 初始化 Modbus-RTU 气体检测仪。所有波特率、时间、寄存器和地址参数必须传 JSON 数字，"
            "addresses 必须是数字数组；初始化成功后处于已初始化状态，不会自动启动轮询线程。";
        info->addConsumes<oatpp::Object<my_api::dto::GasDetectorConfigDto>>("application/json");
        info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
        info->addResponse<oatpp::String>(Status::CODE_400, "application/json");
        info->addResponse<oatpp::String>(Status::CODE_503, "application/json");
    }
    ENDPOINT("POST", "/v1/gas_detector/init", initGasDetector,
             BODY_DTO(oatpp::Object<my_api::dto::GasDetectorConfigDto>, configDto));

    ENDPOINT_INFO(startGasDetector) {
        info->addTag(SWAGGER_TAG);
        info->summary = "启动气体检测仪轮询";
        info->description = "启动 GasDetectorPoll 工作线程，持续轮询已配置的传感器地址。需要先完成初始化。";
        info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
        info->addResponse<oatpp::String>(Status::CODE_503, "application/json");
    }
    ENDPOINT("POST", "/v1/gas_detector/start", startGasDetector);

    ENDPOINT_INFO(stopGasDetector) {
        info->addTag(SWAGGER_TAG);
        info->summary = "停止气体检测仪轮询";
        info->description = "停止 GasDetectorPoll 工作线程并关闭串口。停止后再次启动前需要重新调用 init。";
        info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
        info->addResponse<oatpp::String>(Status::CODE_503, "application/json");
    }
    ENDPOINT("POST", "/v1/gas_detector/stop", stopGasDetector);

    // ==================== 查询接口 ====================

    ENDPOINT_INFO(getConfig) {
        info->addTag(SWAGGER_TAG);
        info->summary = "获取气体检测仪配置";
        info->description = "返回 GasDetectorPoll 当前规范化配置，所有波特率、时间、寄存器和地址均为 JSON 数字。";
        info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
        info->addResponse<oatpp::String>(Status::CODE_503, "application/json");
    }
    ENDPOINT("GET", "/v1/gas_detector/config", getConfig);

    ENDPOINT_INFO(getRealtime) {
        info->addTag(SWAGGER_TAG);
        info->summary = "获取气体检测仪实时传感器数值";
        info->description =
            "返回各 Modbus 从站最近一次轮询结果。concentration、low_alarm、high_alarm、range_value、"
            "temperature、humidity、ad_value 等数值字段均保持 JSON 数字类型，不使用格式化字符串代替数值。";
        info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
        info->addResponse<oatpp::String>(Status::CODE_503, "application/json");
    }
    ENDPOINT("GET", "/v1/gas_detector/realtime", getRealtime);

    ENDPOINT_INFO(simpleStatus) {
        info->addTag(SWAGGER_TAG);
        info->summary = "获取气体检测仪简单状态";
        info->description =
            "返回中英文两种字段格式的工作线程状态、串口通信状态，以及各传感器的当前浓度、低报警阈值、"
            "高报警阈值、气体量程、工作状态、实时 AD 值、环境温度、气体类型和环境湿度。"
            "所有测量值和编码字段均使用 JSON 数字，不使用数字字符串。";
        info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
    }
    ENDPOINT("GET", "/v1/gas_detector/simple_status", simpleStatus);

    ENDPOINT_INFO(getStatus) {
        info->addTag(SWAGGER_TAG);
        info->summary = "获取气体检测仪完整状态";
        info->description = "返回初始化状态、运行状态、通信统计、最近错误和最近传感器结果。";
        info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
    }
    ENDPOINT("GET", "/v1/gas_detector/status", getStatus);
};

#include OATPP_CODEGEN_END(ApiController)

} // namespace my_api::gas_detector_api
