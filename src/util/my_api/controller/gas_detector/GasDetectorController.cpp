#include "GasDetectorController.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "../../../my_gas_detector_poll/MyGasDetectorPoll.h"
#include "MyLog.h"

namespace my_api::gas_detector_api {

using namespace my_api::base;

namespace {

/**
 * @brief 去除文本首尾空白。
 *
 * 该函数只用于设备路径和协议枚举文本；任何数值字段都由 DTO 的数字类型承载，
 * 不在 controller 中通过字符串解析数字。
 */
std::string TrimCopy(const std::string& value) {
    const std::string::size_type begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return std::string();
    }
    const std::string::size_type end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string LowerCopy(const std::string& value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}

template <typename T>
bool RequireField(const T& field, const char* field_name, std::string* error) {
    if (field) {
        return true;
    }
    if (error != 0) {
        *error = std::string("缺少参数 ") + field_name;
    }
    return false;
}

bool IsSupportedParity(const std::string& parity) {
    return parity == "none" || parity == "odd" || parity == "even" ||
           parity == "mark" || parity == "space" || parity == "parity_none" ||
           parity == "parity_odd" || parity == "parity_even" ||
           parity == "parity_mark" || parity == "parity_space";
}

bool IsSupportedFlowcontrol(const std::string& flowcontrol) {
    return flowcontrol == "none" || flowcontrol == "software" ||
           flowcontrol == "hardware" || flowcontrol == "flowcontrol_none" ||
           flowcontrol == "flowcontrol_software" ||
           flowcontrol == "flowcontrol_hardware";
}

/**
 * @brief 把 DTO 转换为 GasDetectorPoll 使用的 JSON 配置。
 *
 * 这里显式写入每个数字字段，避免把波特率、时间、寄存器或地址转成文本。
 * 未提供的可选开关和串口枚举沿用 GasDetectorPollConfig 的默认值。
 */
bool BuildConfigJson(const oatpp::Object<my_api::dto::GasDetectorConfigDto>& configDto,
                     nlohmann::json* config,
                     std::string* error) {
    if (!configDto) {
        if (error != 0) {
            *error = "请求体不能为空，需要提供气体检测仪配置";
        }
        return false;
    }

    if (!RequireField(configDto->enabled, "enabled", error) ||
        !RequireField(configDto->baud_rate, "baud_rate", error) ||
        !RequireField(configDto->data_bits, "data_bits", error) ||
        !RequireField(configDto->stop_bits, "stop_bits", error) ||
        !RequireField(configDto->addresses, "addresses", error) ||
        !RequireField(configDto->interval_ms, "interval_ms", error) ||
        !RequireField(configDto->timeout_ms, "timeout_ms", error) ||
        !RequireField(configDto->start_register, "start_register", error) ||
        !RequireField(configDto->register_count, "register_count", error) ||
        !RequireField(configDto->read_slice_timeout_ms, "read_slice_timeout_ms", error)) {
        return false;
    }

    const bool enabled = *configDto->enabled;
    if (enabled && (!configDto->device ||
                    TrimCopy(configDto->device->c_str()).empty())) {
        if (error != 0) {
            *error = "enabled=true 时必须提供非空 device 参数";
        }
        return false;
    }

    const std::uint32_t baud_rate = *configDto->baud_rate;
    const int data_bits = *configDto->data_bits;
    const int stop_bits = *configDto->stop_bits;
    const std::uint32_t interval_ms = *configDto->interval_ms;
    const std::uint32_t timeout_ms = *configDto->timeout_ms;
    const std::uint32_t start_register = *configDto->start_register;
    const std::uint32_t register_count = *configDto->register_count;
    const std::uint32_t read_slice_timeout_ms = *configDto->read_slice_timeout_ms;

    if (baud_rate == 0) {
        if (error != 0) *error = "参数 baud_rate 必须大于 0";
        return false;
    }
    if (data_bits < 5 || data_bits > 8) {
        if (error != 0) *error = "参数 data_bits 必须在 5～8 范围内";
        return false;
    }
    if (stop_bits != 1 && stop_bits != 2) {
        if (error != 0) *error = "参数 stop_bits 必须是 1 或 2";
        return false;
    }
    if (interval_ms == 0) {
        if (error != 0) *error = "参数 interval_ms 必须大于 0，单位为毫秒";
        return false;
    }
    if (timeout_ms == 0) {
        if (error != 0) *error = "参数 timeout_ms 必须大于 0，单位为毫秒";
        return false;
    }
    if (start_register > 65535U) {
        if (error != 0) *error = "参数 start_register 必须在 0～65535 范围内";
        return false;
    }
    if (register_count != 10U) {
        if (error != 0) *error = "参数 register_count 当前必须是 10";
        return false;
    }
    if (read_slice_timeout_ms == 0 || read_slice_timeout_ms > timeout_ms) {
        if (error != 0) {
            *error = "参数 read_slice_timeout_ms 必须大于 0 且不大于 timeout_ms";
        }
        return false;
    }

    // 地址必须是数字数组，逐项校验范围并拒绝重复地址。
    if (configDto->addresses->empty()) {
        if (error != 0) *error = "参数 addresses 不能为空，至少配置一个数字地址";
        return false;
    }

    nlohmann::json addresses = nlohmann::json::array();
    std::set<int> address_set;
    for (auto it = configDto->addresses->begin();
         it != configDto->addresses->end(); ++it) {
        if (!*it) {
            if (error != 0) *error = "参数 addresses 中不能包含 null";
            return false;
        }
        const int address = **it;
        if (address < 1 || address > 255) {
            if (error != 0) *error = "参数 addresses 中每个地址必须在 1～255 范围内";
            return false;
        }
        if (!address_set.insert(address).second) {
            if (error != 0) *error = "参数 addresses 中不允许出现重复地址";
            return false;
        }
        addresses.push_back(address);
    }

    my_gas_detector_poll::GasDetectorPollConfig defaults;
    nlohmann::json result = defaults.ToJson();
    result["enabled"] = enabled;
    if (configDto->device) {
        result["device"] = TrimCopy(configDto->device->c_str());
    }
    result["baud_rate"] = baud_rate;
    result["data_bits"] = data_bits;
    result["stop_bits"] = stop_bits;
    result["addresses"] = addresses;
    result["interval_ms"] = interval_ms;
    result["timeout_ms"] = timeout_ms;
    result["start_register"] = start_register;
    result["register_count"] = register_count;
    result["read_slice_timeout_ms"] = read_slice_timeout_ms;

    if (configDto->parity) {
        const std::string parity = LowerCopy(TrimCopy(configDto->parity->c_str()));
        if (!IsSupportedParity(parity)) {
            if (error != 0) {
                *error = "参数 parity 不支持，允许值为 none、odd、even、mark、space";
            }
            return false;
        }
        result["parity"] = parity;
    }
    if (configDto->flowcontrol) {
        const std::string flowcontrol = LowerCopy(TrimCopy(configDto->flowcontrol->c_str()));
        if (!IsSupportedFlowcontrol(flowcontrol)) {
            if (error != 0) {
                *error = "参数 flowcontrol 不支持，允许值为 none、software、hardware";
            }
            return false;
        }
        result["flowcontrol"] = flowcontrol;
    }
    if (configDto->log_raw_frame) {
        result["log_raw_frame"] = *configDto->log_raw_frame;
    }
    if (configDto->debug_log_enabled) {
        result["debug_log_enabled"] = *configDto->debug_log_enabled;
    }
    if (configDto->simulate_mode) {
        result["simulate_mode"] = *configDto->simulate_mode;
    }

    if (config != 0) {
        *config = result;
    }
    return true;
}

/**
 * @brief 将实时结果转换为 API 数据。
 *
 * 只保留真正的数值字段和必要的状态描述字段；浓度、阈值、温湿度、AD 值、
 * 原始寄存器、CRC 等全部由 nlohmann::json 以数字输出，不使用 *_text 字段作为数值来源。
 */
nlohmann::json ToRealtimeJson(const my_gas_detector_poll::GasDetectorData& item) {
    return nlohmann::json{
        {"address", item.address},
        {"valid", item.valid},
        {"timeout", item.timeout},
        {"crc_ok", item.crc_ok},
        {"function_code", item.function_code},
        {"byte_count", item.byte_count},
        {"exception_code", item.exception_code},
        {"crc_received", item.crc_received},
        {"crc_calculated", item.crc_calculated},
        {"elapsed_ms", item.elapsed_ms},
        {"definition_register", item.definition_register},
        {"unit", item.unit},
        {"decimal_places", item.decimal_places},
        {"concentration_register", item.concentration_register},
        {"concentration", item.concentration},
        {"low_alarm_register", item.low_alarm_register},
        {"low_alarm", item.low_alarm},
        {"high_alarm_register", item.high_alarm_register},
        {"high_alarm", item.high_alarm},
        {"range_register", item.range_register},
        {"range_value", item.range_value},
        {"status_register", item.status_register},
        {"status_code", item.status_register},
        {"status", item.status},
        {"ad_register", item.ad_register},
        {"ad_value", item.ad_value},
        {"temperature_register", item.temperature_register},
        {"temperature", item.temperature},
        {"gas_code", item.gas_code},
        {"gas", item.gas},
        {"gas_register", item.gas_register},
        {"humidity_register", item.humidity_register},
        {"humidity", item.humidity},
        {"timestamp", item.timestamp},
        {"error", item.error},
        {"sequence", item.sequence}
    };
}

nlohmann::json BuildRealtimeData(
    my_gas_detector_poll::GasDetectorPoll& manager) {
    const std::vector<my_gas_detector_poll::GasDetectorData> results =
        manager.GetLatestResults();

    nlohmann::json result_array = nlohmann::json::array();
    for (std::vector<my_gas_detector_poll::GasDetectorData>::const_iterator it =
             results.begin();
         it != results.end(); ++it) {
        result_array.push_back(ToRealtimeJson(*it));
    }

    const nlohmann::json status = manager.Status();
    nlohmann::json data;
    data["initialized"] = status.value("initialized", false);
    data["running"] = status.value("running", false);
    data["communication_ok"] = status.value("communication_ok", false);
    data["count"] = result_array.size();
    data["results"] = result_array;
    return data;
}

} // namespace

GasDetectorController::GasDetectorController(
    const std::shared_ptr<ObjectMapper>& objectMapper)
    : BaseApiController(objectMapper) {}

std::shared_ptr<GasDetectorController> GasDetectorController::createShared(
    const std::shared_ptr<ObjectMapper>& objectMapper) {
    return std::make_shared<GasDetectorController>(objectMapper);
}

MyAPIResponsePtr GasDetectorController::initGasDetector(
    const oatpp::Object<my_api::dto::GasDetectorConfigDto>& configDto) {
    MYLOG_INFO("[API-气体检测仪] POST /v1/gas_detector/init 收到初始化请求");

    nlohmann::json config;
    std::string error;
    if (!BuildConfigJson(configDto, &config, &error)) {
        MYLOG_WARN("[API-气体检测仪] 初始化参数校验失败：{}", error);
        return jsonError(400, "气体检测仪初始化参数错误", {{"error", error}});
    }

    auto& manager = my_gas_detector_poll::GasDetectorPoll::GetInstance();
    if (!manager.Init(config, &error)) {
        MYLOG_ERROR("[API-气体检测仪] 初始化失败：{}", error);
        return jsonError(503, "气体检测仪初始化失败", {{"error", error}, {"status", manager.Status()}});
    }

    MYLOG_INFO("[API-气体检测仪] 初始化成功，当前未启动轮询线程");
    return jsonOk(manager.Status(), "气体检测仪初始化成功，请调用 start 开始轮询");
}

MyAPIResponsePtr GasDetectorController::startGasDetector() {
    MYLOG_INFO("[API-气体检测仪] POST /v1/gas_detector/start 请求启动轮询");

    auto& manager = my_gas_detector_poll::GasDetectorPoll::GetInstance();
    if (manager.IsRunning()) {
        MYLOG_WARN("[API-气体检测仪] 重复启动请求，轮询线程已经运行");
        return jsonOk({{"started", false}, {"already_running", true}, {"running", true}},
                      "气体检测仪轮询已经在运行");
    }

    if (!manager.Start()) {
        const nlohmann::json status = manager.Status();
        const std::string last_error = status.value("last_error", "未知启动错误");
        MYLOG_ERROR("[API-气体检测仪] 启动失败：{}", last_error);
        return jsonError(503, "气体检测仪启动失败",
                         {{"error", last_error}, {"status", status}});
    }

    MYLOG_INFO("[API-气体检测仪] 轮询线程启动成功");
    return jsonOk({{"started", true}, {"running", manager.IsRunning()}},
                  "气体检测仪轮询已启动");
}

MyAPIResponsePtr GasDetectorController::stopGasDetector() {
    MYLOG_INFO("[API-气体检测仪] POST /v1/gas_detector/stop 请求停止轮询");

    auto& manager = my_gas_detector_poll::GasDetectorPoll::GetInstance();
    const bool was_running = manager.IsRunning();
    if (!manager.Stop()) {
        const nlohmann::json status = manager.Status();
        MYLOG_ERROR("[API-气体检测仪] 停止失败");
        return jsonError(503, "气体检测仪停止失败", {{"status", status}});
    }

    MYLOG_INFO("[API-气体检测仪] 停止完成，停止前运行状态={}", was_running);
    return jsonOk({
        {"stopped", was_running},
        {"already_stopped", !was_running},
        {"running", manager.IsRunning()}
    }, was_running ? "气体检测仪轮询已停止" : "气体检测仪轮询已经停止");
}

MyAPIResponsePtr GasDetectorController::getConfig() {
    MYLOG_INFO("[API-气体检测仪] GET /v1/gas_detector/config 获取配置");

    auto& manager = my_gas_detector_poll::GasDetectorPoll::GetInstance();
    const nlohmann::json status = manager.Status();
    if (!status.value("initialized", false)) {
        MYLOG_WARN("[API-气体检测仪] 获取配置失败：模块尚未初始化");
        return jsonError(503, "气体检测仪尚未初始化，暂无当前配置", {{"status", status}});
    }

    return jsonOk(manager.GetConfig(), "气体检测仪配置获取成功");
}

MyAPIResponsePtr GasDetectorController::getRealtime() {
    MYLOG_INFO("[API-气体检测仪] GET /v1/gas_detector/realtime 获取实时传感器数值");

    auto& manager = my_gas_detector_poll::GasDetectorPoll::GetInstance();
    const nlohmann::json status = manager.Status();
    if (!status.value("initialized", false)) {
        MYLOG_WARN("[API-气体检测仪] 获取实时数值失败：模块尚未初始化");
        return jsonError(503, "气体检测仪尚未初始化，暂无实时传感器数值", {{"status", status}});
    }

    const nlohmann::json data = BuildRealtimeData(manager);
    MYLOG_INFO("[API-气体检测仪] 实时数值获取成功：传感器数量={}，running={}，communication_ok={}",
               data.value("count", 0),
               data.value("running", false),
               data.value("communication_ok", false));
    return jsonOk(data, "气体检测仪实时传感器数值获取成功");
}

MyAPIResponsePtr GasDetectorController::simpleStatus() {
    MYLOG_INFO("[API-气体检测仪] GET /v1/gas_detector/simple_status 获取简单状态");

    auto& manager = my_gas_detector_poll::GasDetectorPoll::GetInstance();
    const nlohmann::json cn_data = manager.StatusSimpleCN();
    const nlohmann::json en_data = manager.StatusSimpleEN();
    const nlohmann::json data = {
        {"cn", cn_data},
        {"en", en_data}
    };

    MYLOG_INFO("[API-气体检测仪] 简单状态获取成功：工作线程运行={}，串口通信正常={}，传感器数量={}",
               en_data.value("worker_running", false),
               en_data.value("serial_connected", false),
               en_data["sensors"].size());
    return jsonOk(data, "气体检测仪简单状态获取成功");
}

MyAPIResponsePtr GasDetectorController::getStatus() {
    MYLOG_INFO("[API-气体检测仪] GET /v1/gas_detector/status 获取完整状态");

    auto& manager = my_gas_detector_poll::GasDetectorPoll::GetInstance();
    return jsonOk(manager.Status(), "气体检测仪状态获取成功");
}

} // namespace my_api::gas_detector_api
