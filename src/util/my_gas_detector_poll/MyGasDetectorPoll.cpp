#include "MyGasDetectorPoll.h"
#include "MyGasDetectorPollInternal.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>

#include "MyLog.h"

namespace my_gas_detector_poll {

using namespace detail;

GasDetectorPoll& GasDetectorPoll::GetInstance() {
    static GasDetectorPoll instance;
    return instance;
}

GasDetectorPoll::GasDetectorPoll()
    : lifecycle_mutex_(),
      mutex_(),
      wait_mutex_(),
      wait_condition_(),
      stop_requested_(false),
      worker_(),
      initialized_(false),
      running_(false),
      communication_ok_(false),
      debug_log_enabled_(false),
      simulate_mode_(false),
      state_(PollState::Uninitialized),
      config_(),
      init_config_(nlohmann::json::object()),
      latest_results_(),
      request_count_(0),
      success_count_(0),
      timeout_count_(0),
      invalid_frame_count_(0),
      crc_error_count_(0),
      cycle_count_(0),
      next_sequence_(1),
      last_error_(),
      last_frame_(),
            serial_() {
}

GasDetectorPoll::~GasDetectorPoll() {
    Stop();
}

bool GasDetectorPoll::Init(const nlohmann::json& config, std::string* err) {
    MYLOG_INFO("[气体检测仪] 开始初始化轮询模块，配置：{}", SafeJsonDump(config));

    GasDetectorPollConfig parsed;
    try {
        parsed = ParseConfig(config);
    } catch (const std::exception& ex) {
        if (err != 0) {
            *err = ex.what();
        }
        MYLOG_ERROR("[气体检测仪] 配置解析失败：{}", ex.what());
        return false;
    } catch (...) {
        if (err != 0) {
            *err = "配置解析失败：未知异常";
        }
        MYLOG_ERROR("[气体检测仪] 配置解析失败：未知异常");
        return false;
    }

    std::unique_lock<std::mutex> lifecycle_lock(lifecycle_mutex_);
    StopInternalLocked();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = parsed;
        debug_log_enabled_ = parsed.debug_log_enabled;
        simulate_mode_ = parsed.simulate_mode;
        init_config_ = config;
        latest_results_.clear();
        for (std::vector<std::uint8_t>::const_iterator it = config_.addresses.begin();
             it != config_.addresses.end(); ++it) {
            GasDetectorData initial;
            initial.address = static_cast<int>(*it);
            latest_results_.push_back(initial);
        }
        request_count_ = 0;
        success_count_ = 0;
        timeout_count_ = 0;
        invalid_frame_count_ = 0;
        crc_error_count_ = 0;
        cycle_count_ = 0;
        next_sequence_ = 1;
        last_error_.clear();
        last_frame_.clear();
        communication_ok_ = false;
        initialized_ = false;
        running_ = false;
        state_ = parsed.enabled ? PollState::Initialized : PollState::Disabled;
    }

    if (!parsed.enabled) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            initialized_ = true;
        }
        if (err != 0) {
            err->clear();
        }
        MYLOG_WARN("[气体检测仪] 配置 enabled=false，模块已初始化但保持禁用，不打开串口、不创建工作线程");
        return true;
    }

    nlohmann::json serial_config = {
        {"device", parsed.device},
        {"baud_rate", parsed.baud_rate},
        {"data_bits", parsed.data_bits},
        {"stop_bits", parsed.stop_bits},
        {"parity", parsed.parity},
        {"flowcontrol", parsed.flowcontrol},
        // 底层单次读操作只等待一个短时间片，整体响应超时由本模块
        // 使用 steady_clock 统一控制，避免“读报头+读正文”重复超时。
        {"timeout_ms", parsed.read_slice_timeout_ms},
        {"auto_open", true}
    };

    std::string serial_error;
    if (!serial_.Init(serial_config, &serial_error)) {
        const std::string message = "串口初始化失败：" + serial_error;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            initialized_ = false;
            state_ = PollState::Error;
            last_error_ = message;
        }
        if (err != 0) {
            *err = message;
        }
        MYLOG_ERROR("[气体检测仪] 初始化失败：设备={}，波特率={}，原因：{}",
                    parsed.device, parsed.baud_rate, serial_error);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        initialized_ = true;
        state_ = PollState::Initialized;
        last_error_.clear();
    }
    if (err != 0) {
        err->clear();
    }

    MYLOG_INFO("[气体检测仪] 初始化成功：串口={}，{} baud，{}{}{}，轮询地址={} 个，间隔={} ms，响应超时={} ms",
               parsed.device,
               parsed.baud_rate,
               parsed.data_bits,
               parsed.parity == "none" ? "N" : parsed.parity,
               parsed.stop_bits,
               parsed.addresses.size(),
               parsed.interval_ms,
               parsed.timeout_ms);
    return true;
}

bool GasDetectorPoll::Start() {
    std::unique_lock<std::mutex> lifecycle_lock(lifecycle_mutex_);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            last_error_ = "模块尚未初始化，不能启动";
            state_ = PollState::Error;
            MYLOG_ERROR("[气体检测仪] 启动失败：模块尚未初始化，请先调用 Init()");
            return false;
        }
        if (!config_.enabled) {
            state_ = PollState::Disabled;
            MYLOG_WARN("[气体检测仪] Start() 被调用，但模块配置为 disabled，不创建工作线程");
            return true;
        }
        if (running_ || worker_.joinable()) {
            MYLOG_WARN("[气体检测仪] 工作线程已经运行，重复 Start() 被忽略");
            return true;
        }
    }

    if (!serial_.IsOpen()) {
        const std::string message = "串口未打开，不能启动工作线程";
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = message;
        state_ = PollState::Error;
        MYLOG_ERROR("[气体检测仪] 启动失败：{}，设备={}", message, config_.device);
        return false;
    }

    stop_requested_.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = true;
        communication_ok_ = false;
        state_ = PollState::Running;
        last_error_.clear();
    }

    try {
        worker_ = std::thread(&GasDetectorPoll::WorkerLoop, this);
    } catch (const std::exception& ex) {
        stop_requested_.store(true);
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        state_ = PollState::Error;
        last_error_ = std::string("创建工作线程失败：") + ex.what();
        MYLOG_ERROR("[气体检测仪] 创建工作线程失败：{}", ex.what());
        return false;
    } catch (...) {
        stop_requested_.store(true);
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        state_ = PollState::Error;
        last_error_ = "创建工作线程失败：未知异常";
        MYLOG_ERROR("[气体检测仪] 创建工作线程失败：未知异常");
        return false;
    }

    MYLOG_INFO("[气体检测仪] 工作线程启动成功，开始持续轮询和解析串口数据");
    return true;
}

bool GasDetectorPoll::Stop() {
    std::unique_lock<std::mutex> lifecycle_lock(lifecycle_mutex_);
    StopInternalLocked();
    return true;
}

void GasDetectorPoll::StopInternalLocked() {
    const bool had_worker = worker_.joinable();
    const bool had_serial = serial_.IsOpen();
    if (had_worker || had_serial) {
        MYLOG_INFO("[气体检测仪] 开始停止模块：通知工作线程退出，等待线程结束并关闭串口");
    }

    stop_requested_.store(true);
    wait_condition_.notify_all();

    std::thread old_worker;
    old_worker.swap(worker_);
    if (old_worker.joinable()) {
        old_worker.join();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        serial_.Close();
        running_ = false;
        initialized_ = false;
        communication_ok_ = false;
        state_ = PollState::Uninitialized;
        last_error_.clear();
    }
    stop_requested_.store(false);

    if (had_worker || had_serial) {
        MYLOG_INFO("[气体检测仪] 模块已停止，工作线程已退出，串口已关闭");
    }
}

nlohmann::json GasDetectorPoll::Status() const {
    std::lock_guard<std::mutex> lock(mutex_);

    nlohmann::json results = nlohmann::json::array();
    for (std::vector<GasDetectorData>::const_iterator it = latest_results_.begin();
         it != latest_results_.end(); ++it) {
        results.push_back(it->ToJson());
    }

    nlohmann::json status;
    status["initialized"] = initialized_;
    status["enabled"] = config_.enabled;
    status["running"] = running_;
    status["state"] = StateToString(state_);
    status["communication_ok"] = communication_ok_;
    status["serial_open"] = serial_.IsOpen();
    status["device"] = config_.device;
    status["config"] = config_.ToJson();
    status["init_config"] = init_config_;
    status["statistics"] = {
        {"request_count", request_count_},
        {"success_count", success_count_},
        {"timeout_count", timeout_count_},
        {"invalid_frame_count", invalid_frame_count_},
        {"crc_error_count", crc_error_count_},
        {"cycle_count", cycle_count_}
    };
    status["last_error"] = last_error_;
    status["last_frame"] = last_frame_;
    status["results"] = results;
    return status;
}

nlohmann::json GasDetectorPoll::StatusSimpleCN() const {
    std::lock_guard<std::mutex> lock(mutex_);

    nlohmann::json sensors = nlohmann::json::array();
    for (std::vector<GasDetectorData>::const_iterator it = latest_results_.begin();
         it != latest_results_.end(); ++it) {
        sensors.push_back(it->ToSimpleCNJson());
    }

    nlohmann::json status;
    status["工作线程运行"] = running_;
    status["串口联通"] = serial_.IsOpen() && communication_ok_;
    status["传感器"] = sensors;
    return status;
}

nlohmann::json GasDetectorPoll::StatusSimpleEN() const {
    std::lock_guard<std::mutex> lock(mutex_);

    nlohmann::json sensors = nlohmann::json::array();
    for (std::vector<GasDetectorData>::const_iterator it = latest_results_.begin();
         it != latest_results_.end(); ++it) {
        sensors.push_back(it->ToSimpleENJson());
    }

    nlohmann::json status;
    status["worker_running"] = running_;
    status["serial_connected"] = serial_.IsOpen() && communication_ok_;
    status["sensors"] = sensors;
    return status;
}

nlohmann::json GasDetectorPoll::GetConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.ToJson();
}

bool GasDetectorPoll::IsRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

std::vector<GasDetectorData> GasDetectorPoll::GetLatestResults() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_results_;
}

std::uint16_t GasDetectorPoll::Crc16Modbus(const std::vector<std::uint8_t>& data) {
    std::uint16_t crc = 0xFFFF;
    for (std::vector<std::uint8_t>::const_iterator it = data.begin(); it != data.end(); ++it) {
        crc = static_cast<std::uint16_t>(crc ^ *it);
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x0001U) != 0U) {
                crc = static_cast<std::uint16_t>((crc >> 1U) ^ 0xA001U);
            } else {
                crc = static_cast<std::uint16_t>(crc >> 1U);
            }
        }
    }
    return crc;
}

std::vector<std::uint8_t> GasDetectorPoll::BuildRequest(std::uint8_t address,
                                                         std::uint16_t start_register,
                                                         std::uint16_t count) {
    std::vector<std::uint8_t> body;
    body.push_back(address);
    body.push_back(0x03);
    body.push_back(static_cast<std::uint8_t>((start_register >> 8) & 0xFF));
    body.push_back(static_cast<std::uint8_t>(start_register & 0xFF));
    body.push_back(static_cast<std::uint8_t>((count >> 8) & 0xFF));
    body.push_back(static_cast<std::uint8_t>(count & 0xFF));

    const std::uint16_t crc = Crc16Modbus(body);
    body.push_back(static_cast<std::uint8_t>(crc & 0xFF));
    body.push_back(static_cast<std::uint8_t>((crc >> 8) & 0xFF));
    return body;
}

std::string GasDetectorPoll::HexFrame(const std::vector<std::uint8_t>& data) {
    std::ostringstream stream;
    for (std::vector<std::uint8_t>::const_iterator it = data.begin(); it != data.end(); ++it) {
        if (it != data.begin()) {
            stream << ' ';
        }
        stream << std::uppercase << std::hex << std::setfill('0') << std::setw(2)
               << static_cast<unsigned int>(*it);
    }
    return stream.str();
}

GasDetectorPollConfig GasDetectorPoll::ParseConfig(const nlohmann::json& config) {
    if (!config.is_object()) {
        throw std::invalid_argument("气体检测仪配置必须是 JSON 对象");
    }

    const nlohmann::json* serial_node = 0;
    if (config.contains("serial") && config.at("serial").is_object()) {
        serial_node = &config.at("serial");
    }
    const nlohmann::json& serial_config = serial_node == 0 ? config : *serial_node;

    GasDetectorPollConfig parsed;
    parsed.enabled = GetBooleanValue(config, serial_config, {"enabled"}, parsed.enabled);
    parsed.device = TrimCopy(GetStringValue(config, serial_config, {"device", "port"}, parsed.device));
    parsed.baud_rate = ToUint32(GetUnsignedValue(
        config, serial_config, {"baud_rate", "baudrate"}, parsed.baud_rate), "baud_rate");
    parsed.data_bits = GetIntegerValue(config, serial_config, {"data_bits", "bytesize"}, parsed.data_bits);
    parsed.stop_bits = GetIntegerValue(config, serial_config, {"stop_bits", "stopbits"}, parsed.stop_bits);
    parsed.parity = LowerCopy(TrimCopy(GetStringValue(
        config, serial_config, {"parity"}, parsed.parity)));
    parsed.flowcontrol = LowerCopy(TrimCopy(GetStringValue(
        config, serial_config, {"flowcontrol", "flow_control"}, parsed.flowcontrol)));

    const nlohmann::json* addresses = FindConfigValue(config, serial_config, {"addresses", "address"});
    if (addresses != 0) {
        parsed.addresses = ParseAddresses(*addresses);
    }

    parsed.interval_ms = ToUint32(GetUnsignedValue(
        config, serial_config, {"interval_ms", "poll_interval_ms"}, parsed.interval_ms), "interval_ms");
    parsed.timeout_ms = ToUint32(GetUnsignedValue(
        config, serial_config, {"timeout_ms", "response_timeout_ms"}, parsed.timeout_ms), "timeout_ms");
    parsed.start_register = ToUint16(GetUnsignedValue(
        config, serial_config, {"start_register"}, parsed.start_register), "start_register");
    parsed.register_count = ToUint16(GetUnsignedValue(
        config, serial_config, {"register_count", "count"}, parsed.register_count), "register_count");

    const nlohmann::json* slice_timeout = FindConfigValue(
        config, serial_config, {"read_slice_timeout_ms"});
    if (slice_timeout != 0) {
        parsed.read_slice_timeout_ms = ToUint32(
            JsonUnsigned(*slice_timeout, "read_slice_timeout_ms", parsed.read_slice_timeout_ms),
            "read_slice_timeout_ms");
    } else {
        parsed.read_slice_timeout_ms = std::min(parsed.timeout_ms, kDefaultReadSliceTimeoutMs);
    }
    parsed.log_raw_frame = GetBooleanValue(
        config, serial_config, {"log_raw_frame", "verbose_log"}, parsed.log_raw_frame);
        parsed.debug_log_enabled = GetBooleanValue(
            config, serial_config, {"debug_log_enabled"}, parsed.debug_log_enabled);
        parsed.simulate_mode = GetBooleanValue(
            config, serial_config, {"simulate_mode"}, parsed.simulate_mode);

    if (parsed.enabled && parsed.device.empty()) {
        throw std::invalid_argument("enabled=true 时 device/port 不能为空");
    }
    if (parsed.baud_rate == 0) {
        throw std::invalid_argument("baud_rate 必须大于 0");
    }
    if (parsed.data_bits < 5 || parsed.data_bits > 8) {
        throw std::invalid_argument("data_bits 必须在 5～8 范围内");
    }
    if (parsed.stop_bits != 1 && parsed.stop_bits != 2) {
        throw std::invalid_argument("stop_bits 当前仅支持 1 或 2");
    }
    if (parsed.interval_ms == 0) {
        throw std::invalid_argument("interval_ms 必须大于 0");
    }
    if (parsed.timeout_ms == 0) {
        throw std::invalid_argument("timeout_ms 必须大于 0");
    }
    if (parsed.register_count != 10) {
        throw std::invalid_argument("当前气体检测仪协议固定读取 10 个寄存器，register_count 必须为 10");
    }
    if (parsed.read_slice_timeout_ms == 0) {
        throw std::invalid_argument("read_slice_timeout_ms 必须大于 0");
    }
    if (parsed.read_slice_timeout_ms > parsed.timeout_ms) {
        parsed.read_slice_timeout_ms = parsed.timeout_ms;
    }
    if (parsed.addresses.empty()) {
        throw std::invalid_argument("至少需要配置一个设备地址");
    }

    MYLOG_INFO("[气体检测仪] 配置解析完成：enabled={}，device={}，baud_rate={}，data_bits={}，stop_bits={}，addresses={}，interval_ms={}，timeout_ms={}",
               parsed.enabled ? "true" : "false",
               parsed.device,
               parsed.baud_rate,
               parsed.data_bits,
               parsed.stop_bits,
               parsed.addresses.size(),
               parsed.interval_ms,
               parsed.timeout_ms);
    return parsed;
}

std::string GasDetectorPoll::StateToString(PollState state) {
    switch (state) {
        case PollState::Uninitialized: return "uninitialized";
        case PollState::Disabled: return "disabled";
        case PollState::Initialized: return "initialized";
        case PollState::Running: return "running";
        case PollState::Error: return "error";
        default: return "unknown";
    }
}

bool GasDetectorPoll::ParseResponse(const std::vector<std::uint8_t>& response,
                                    std::uint8_t expected_address,
                                    std::uint16_t expected_register_count,
                                    double elapsed_ms,
                                    GasDetectorData* result,
                                    std::string* error) {
    if (result == 0) {
        if (error != 0) {
            *error = "解析输出对象为空";
        }
        return false;
    }

    *result = GasDetectorData();
    result->address = static_cast<int>(expected_address);
    result->elapsed_ms = elapsed_ms;
    result->timestamp = TimestampNow();

    if (response.size() < 5) {
        result->error = "报文长度不足：至少需要 5 字节，实际收到 " +
                        std::to_string(response.size()) + " 字节";
        if (error != 0) {
            *error = result->error;
        }
        return false;
    }

    result->crc_received = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(response[response.size() - 2]) |
        static_cast<std::uint16_t>(response[response.size() - 1] << 8));
    std::vector<std::uint8_t> crc_body(response.begin(), response.end() - 2);
    result->crc_calculated = Crc16Modbus(crc_body);
    result->crc_ok = result->crc_received == result->crc_calculated;
    if (!result->crc_ok) {
        std::ostringstream stream;
        stream << "CRC 校验失败：接收 CRC=0x" << std::uppercase << std::hex
               << std::setw(4) << std::setfill('0') << result->crc_received
               << "，计算 CRC=0x" << std::setw(4) << result->crc_calculated;
        result->error = stream.str();
        if (error != 0) {
            *error = result->error;
        }
        return false;
    }

    result->function_code = static_cast<int>(response[1]);
    if (response[0] != expected_address) {
        std::ostringstream stream;
        stream << "从站地址错误：期望 " << static_cast<int>(expected_address)
               << "，实际 " << static_cast<int>(response[0]);
        result->error = stream.str();
        if (error != 0) {
            *error = result->error;
        }
        return false;
    }

    if ((response[1] & 0x80U) != 0U) {
        result->exception_code = response[2];
        result->error = "Modbus 异常响应：异常码 0x";
        std::ostringstream stream;
        stream << "Modbus 异常响应：异常码 0x" << std::uppercase << std::hex
               << std::setw(2) << std::setfill('0') << result->exception_code
               << "（" << ExceptionName(result->exception_code) << "）";
        result->error = stream.str();
        if (response.size() != 5) {
            result->error += "，异常帧长度错误，期望 5 字节，实际 " +
                             std::to_string(response.size()) + " 字节";
        }
        if (error != 0) {
            *error = result->error;
        }
        return false;
    }

    if (response[1] != 0x03) {
        std::ostringstream stream;
        stream << "功能码错误：期望 0x03，实际 0x" << std::uppercase << std::hex
               << std::setw(2) << std::setfill('0') << static_cast<int>(response[1]);
        result->error = stream.str();
        if (error != 0) {
            *error = result->error;
        }
        return false;
    }

    result->byte_count = response[2];
    const int expected_byte_count = static_cast<int>(expected_register_count) * 2;
    const std::size_t expected_frame_size = static_cast<std::size_t>(expected_byte_count + 5);
    if (result->byte_count != expected_byte_count || response.size() != expected_frame_size) {
        std::ostringstream stream;
        stream << "响应长度错误：期望数据区 " << expected_byte_count
               << " 字节、完整帧 " << expected_frame_size
               << " 字节，实际声明 " << result->byte_count
               << " 字节、收到 " << response.size() << " 字节";
        result->error = stream.str();
        if (error != 0) {
            *error = result->error;
        }
        return false;
    }

    std::uint16_t registers[10] = {0};
    for (int index = 0; index < 10; ++index) {
        const std::size_t offset = 3 + static_cast<std::size_t>(index) * 2;
        registers[index] = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(response[offset]) << 8) |
            static_cast<std::uint16_t>(response[offset + 1]));
    }

    // 第 0 个寄存器是“定义寄存器”，高 4 位表示单位编码，次高 4 位表示小数位数。
    result->definition_register     = registers[0];
    const int unit_code             = (registers[0] >> 12) & 0x0F;
    const int decimal_code          = (registers[0] >> 8) & 0x0F;
    result->unit                    = UnitName(unit_code);
    result->decimal_places          = DecimalPlaces(decimal_code);

    // 第 1 个寄存器表示当前检测浓度，需要按定义寄存器中的小数位换算。
    // 第 2～4 个寄存器表示低报警阀值、高报警阀值和气体量程，直接保留原始值，不做小数位换算。
    result->concentration_register  = registers[1];
    result->concentration           = static_cast<double>(registers[1]);
    result->concentration_text      = FormatScaled(registers[1], result->decimal_places);

    result->low_alarm_register      = registers[2];
    result->low_alarm               = static_cast<double>(registers[2]);
    result->low_alarm_text          = std::to_string(static_cast<unsigned int>(registers[2]));

    result->high_alarm_register     = registers[3];
    result->high_alarm              = static_cast<double>(registers[3]);
    result->high_alarm_text         = std::to_string(static_cast<unsigned int>(registers[3]));

    result->range_register          = registers[4];
    result->range_value             = static_cast<double>(registers[4]);
    result->range_text              = std::to_string(static_cast<unsigned int>(registers[4]));

    // 第 5～9 个寄存器保存传感器状态、实时 AD 值、温度、气体编码和湿度。
    result->status_register         = registers[5];
    result->status                  = StatusName(registers[5]);

    result->ad_register             = registers[6];
    result->ad_value                = static_cast<int>(registers[6]);

    result->temperature_register    = registers[7];
    result->temperature             = (static_cast<double>(registers[7]) - 500.0) / 10.0;

    result->gas_register            = registers[8];
    result->gas_code                = (registers[8] >> 8) & 0xFF;
    result->gas                     = GasName(result->gas_code);

    result->humidity_register       = registers[9];
    result->humidity                = static_cast<double>(registers[9]) / 10.0;

    result->valid = true;
    result->error.clear();
    if (error != 0) {
        error->clear();
    }
    return true;
}

GasDetectorData GasDetectorPoll::MakeFailureResult(int address,
                                                    double elapsed_ms,
                                                    const std::string& error,
                                                    bool timeout) {
    GasDetectorData result;
    result.address = address;
    result.elapsed_ms = elapsed_ms;
    result.timeout = timeout;
    result.timestamp = TimestampNow();
    result.error = error;
    return result;
}

bool GasDetectorPoll::WaitUntil(const std::chrono::steady_clock::time_point& deadline) {
    std::unique_lock<std::mutex> lock(wait_mutex_);
    if (stop_requested_.load()) {
        return false;
    }
    const bool stopped = wait_condition_.wait_until(
        lock, deadline, [this]() { return stop_requested_.load(); });
    return !stopped;
}

bool GasDetectorPoll::DiscardInputBuffer() {
    // Python 版本在每次发送请求前调用 reset_input_buffer()。MySerial 对外
    // 提供的是 Available()+ReadBytes()，因此这里循环读取并丢弃旧数据，
    // 防止上一次残帧被误当成当前从站的响应。
    while (!stop_requested_.load()) {
        std::string available_error;
        const std::size_t available = serial_.Available(&available_error);
        if (!available_error.empty()) {
            MYLOG_WARN("[气体检测仪] 清理串口接收缓存失败：{}", available_error);
            return false;
        }
        if (available == 0) {
            return true;
        }

        const std::size_t chunk_size = std::min<std::size_t>(available, 512);
        std::string read_error;
        const std::vector<std::uint8_t> discarded = serial_.ReadBytes(chunk_size, &read_error);
        if (!read_error.empty()) {
            MYLOG_WARN("[气体检测仪] 丢弃串口残留数据失败：{}", read_error);
            return false;
        }
        if (discarded.empty()) {
            MYLOG_WARN("[气体检测仪] 串口缓存报告有 {} 字节，但实际没有读到数据", available);
            return false;
        }
        MYLOG_DEBUG("[气体检测仪] 已丢弃串口残留数据 {} 字节：{}",
                    discarded.size(), HexFrame(discarded));
    }
    return false;
}

std::vector<std::uint8_t> GasDetectorPoll::ReadUpTo(
    std::size_t size,
    const std::chrono::steady_clock::time_point& deadline,
    std::string* error) {
    std::vector<std::uint8_t> result;
    while (result.size() < size && !stop_requested_.load()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }

        std::string read_error;
        const std::vector<std::uint8_t> chunk = serial_.ReadBytes(size - result.size(), &read_error);
        if (!read_error.empty()) {
            if (error != 0) {
                *error = read_error;
            }
            return result;
        }
        if (!chunk.empty()) {
            result.insert(result.end(), chunk.begin(), chunk.end());
        }
    }
    return result;
}

std::vector<std::uint8_t> GasDetectorPoll::ReadResponse(
    const std::chrono::steady_clock::time_point& deadline,
    std::string* error) {
    if (error != 0) {
        error->clear();
    }

    std::vector<std::uint8_t> header = ReadUpTo(3, deadline, error);
    if (header.size() < 3) {
        if (error != 0 && error->empty() && !header.empty()) {
            *error = "响应头不完整：期望 3 字节，实际收到 " +
                     std::to_string(header.size()) + " 字节";
        }
        return header;
    }

    const std::size_t remaining = (header[1] & 0x80U) != 0U
                                      ? 2U
                                      : static_cast<std::size_t>(header[2]) + 2U;
    if (header.size() + remaining > kMaxResponseSize) {
        if (error != 0) {
            *error = "响应声明长度超过 Modbus RTU 最大帧长度";
        }
        return header;
    }

    std::vector<std::uint8_t> body = ReadUpTo(remaining, deadline, error);
    header.insert(header.end(), body.begin(), body.end());
    if (header.size() < 3 + remaining && error != 0 && error->empty()) {
        *error = "响应帧未读取完整：期望 " + std::to_string(3 + remaining) +
                 " 字节，实际收到 " + std::to_string(header.size()) + " 字节";
    }
    return header;
}

void GasDetectorPoll::StoreResult(const GasDetectorData& result,
                                   const std::vector<std::uint8_t>& response) {
    std::lock_guard<std::mutex> lock(mutex_);

    GasDetectorData stored = result;
    stored.sequence = next_sequence_++;
    bool replaced = false;
    for (std::vector<GasDetectorData>::iterator it = latest_results_.begin();
         it != latest_results_.end(); ++it) {
        if (it->address == stored.address) {
            *it = stored;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        latest_results_.push_back(stored);
    }

    last_frame_ = response.empty() ? std::string() : HexFrame(response);
    communication_ok_ = stored.valid;
    if (stored.valid) {
        ++success_count_;
        last_error_.clear();
    } else {
        ++invalid_frame_count_;
        if (stored.timeout) {
            ++timeout_count_;
        }
        if (!stored.crc_ok && response.size() >= 5) {
            ++crc_error_count_;
        }
        last_error_ = stored.error;
    }
}

void GasDetectorPoll::LogSummary(bool print_log) const {
    std::vector<GasDetectorData> results;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        results = latest_results_;
    }
    if (!print_log) {
        return;
    }

    MYLOG_INFO("[气体检测仪] 【最新查询结果简表】");
    MYLOG_INFO("+------+-------------+------------------+------------+------------+----------+----------------+----------------+------------+---------------+------------+----------+");
    MYLOG_INFO("| 序号 | 数值单位    | 当前检测气体浓度 | 低报警阀值 | 高报警阀值 | 气体量程 | 传感器工作状态 | 传感器实时AD值 | 环境温度值 | 气体类型表    | 环境湿度值 | 校验结果 |");
    MYLOG_INFO("+------+-------------+------------------+------------+------------+----------+----------------+----------------+------------+---------------+------------+----------+");

    std::size_t sequence = 0;
    for (std::vector<GasDetectorData>::const_iterator it = results.begin();
         it != results.end(); ++it) {
        ++sequence;

        const std::string unit_text = it->unit + "/" + std::to_string(it->decimal_places) + "位小数";
        const std::string concentration = it->valid ? it->concentration_text : "-";
        const std::string low_alarm = it->valid ? it->low_alarm_text : "-";
        const std::string high_alarm = it->valid ? it->high_alarm_text : "-";
        const std::string range_value = it->valid ? it->range_text : "-";
        const std::string status = it->valid ? it->status : "无有效数据";
        const std::string ad_value = it->valid ? std::to_string(it->ad_value) : "-";
        const std::string temperature = it->valid ? FormatFixed1(it->temperature) + "℃" : "-";
        const std::string gas_type = it->valid ? it->gas : "-";
        const std::string humidity = it->valid ? FormatFixed1(it->humidity) + "%" : "-";
        const std::string check_result = it->valid ? "成功" : (it->error.empty() ? "失败" : "失败");

        MYLOG_INFO("| {:<4d} | {:<11} | {:<16} | {:<10} | {:<10} | {:<8} | {:<14} | {:<14} | {:<10} | {:<13} | {:<10} | {:<8} |",
                   static_cast<int>(sequence),
                   unit_text,
                   concentration,
                   low_alarm,
                   high_alarm,
                   range_value,
                   status,
                   ad_value,
                   temperature,
                   gas_type,
                   humidity,
                   check_result);
    }

    MYLOG_INFO("+------+-------------+------------------+------------+------------+----------+----------------+----------------+------------+---------------+------------+----------+");
}

void GasDetectorPoll::WorkerLoop() {
    GasDetectorPollConfig config;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config = config_;
    }
    const bool debug_log_enabled = config.debug_log_enabled;

    if (debug_log_enabled) {
        MYLOG_INFO("[气体检测仪] 工作线程进入主循环：地址数量={}，请求间隔={} ms，单次响应总超时={} ms",
                   config.addresses.size(), config.interval_ms, config.timeout_ms);
    }

    std::chrono::steady_clock::time_point next_request_at =
        std::chrono::steady_clock::now();

    try {
        int print_log_countdown = 0;
        while (!stop_requested_.load()) {
            bool completed_cycle = true;
            for (std::vector<std::uint8_t>::const_iterator address_it = config.addresses.begin();
                 address_it != config.addresses.end(); ++address_it) {
                if (stop_requested_.load()) {
                    completed_cycle = false;
                    break;
                }
                if (!WaitUntil(next_request_at)) {
                    completed_cycle = false;
                    break;
                }

                const std::uint8_t address = *address_it;
                const std::vector<std::uint8_t> request = BuildRequest(
                    address, config.start_register, config.register_count);
                if (debug_log_enabled) {
                    MYLOG_INFO("================================================================================");
                    MYLOG_INFO("【{}】开始轮询 {} 号气体检测仪", TimestampNow(), static_cast<int>(address));
                    MYLOG_INFO("【发送报文】{}", config.log_raw_frame ? HexFrame(request) : "（原始报文日志已关闭）");
                    MYLOG_INFO("地址：{}，功能码：03（读取保持寄存器），起始寄存器：{}，寄存器数量：{}，发送 CRC：0x{:04X}",
                               static_cast<int>(address),
                               config.start_register,
                               config.register_count,
                               static_cast<unsigned int>(
                                   static_cast<std::uint16_t>(request[request.size() - 2]) |
                                   static_cast<std::uint16_t>(request[request.size() - 1] << 8)));
                    MYLOG_INFO("等待从站返回，整体响应超时：{} ms", config.timeout_ms);
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++request_count_;
                }

                GasDetectorData result;
                std::vector<std::uint8_t> response;
                const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

                if (!DiscardInputBuffer()) {
                    const double elapsed_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - started).count() / 1000.0;
                    result = MakeFailureResult(
                        static_cast<int>(address), elapsed_ms, "发送前清理串口接收缓存失败", false);
                } else {
                    std::string write_error;
                    const std::size_t written = serial_.Write(request, &write_error);
                    if (!write_error.empty() || written != request.size()) {
                        const double elapsed_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - started).count() / 1000.0;
                        std::string message = "请求发送失败：实际写入 " + std::to_string(written) +
                                              " 字节，期望 " + std::to_string(request.size()) + " 字节";
                        if (!write_error.empty()) {
                            message += "，原因：" + write_error;
                        }
                        result = MakeFailureResult(static_cast<int>(address), elapsed_ms, message, false);
                    } else {
                        const std::chrono::steady_clock::time_point deadline =
                            started + std::chrono::milliseconds(config.timeout_ms);
                        std::string read_error;
                        response = ReadResponse(deadline, &read_error);
                        const double elapsed_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - started).count() / 1000.0;

                        if (response.empty()) {
                            const std::string message = read_error.empty()
                                ? "通信超时：在 " + std::to_string(config.timeout_ms) + " ms 内未收到任何数据"
                                : "读取响应失败：" + read_error;
                            result = MakeFailureResult(
                                static_cast<int>(address), elapsed_ms, message, read_error.empty());
                        } else if (!read_error.empty()) {
                            result = MakeFailureResult(
                                static_cast<int>(address), elapsed_ms,
                                "读取响应失败：" + read_error, false);
                        } else {
                            std::string parse_error;
                            ParseResponse(response, address, config.register_count,
                                          elapsed_ms, &result, &parse_error);
                            if (!parse_error.empty()) {
                                result.error = parse_error;
                            }
                        }
                    }
                }

                if (debug_log_enabled) {
                    MYLOG_INFO("【收到报文】{}",
                               response.empty() ? "<无数据>" :
                               (config.log_raw_frame ? HexFrame(response) : "（原始报文日志已关闭）"));
                    const double log_elapsed_ms = result.elapsed_ms;
                    MYLOG_INFO("通信耗时：{:.1f} ms", log_elapsed_ms);

                    if (result.valid) {
                        MYLOG_INFO("【CRC校验】接收 CRC：0x{:04X}，计算 CRC：0x{:04X}，校验结果：√ 成功",
                                   static_cast<unsigned int>(result.crc_received),
                                   static_cast<unsigned int>(result.crc_calculated));
                        MYLOG_INFO("【Modbus解析】地址：{}，功能码：0x{:02X}，数据长度：{} Byte",
                                   result.address,
                                   result.function_code,
                                   result.byte_count);
                        MYLOG_INFO("单位：{}，小数位：{}，当前浓度：{} {}，低报警：{} {}，高报警：{} {}，量程：{} {}",
                                   result.unit,
                                   result.decimal_places,
                                   result.concentration_text,
                                   result.unit,
                                   result.low_alarm_text,
                                   result.unit,
                                   result.high_alarm_text,
                                   result.unit,
                                   result.range_text,
                                   result.unit);
                        MYLOG_INFO("原始寄存器：R0=0x{:04X}，R1=0x{:04X}，R2=0x{:04X}，R3=0x{:04X}，R4=0x{:04X}",
                                   static_cast<unsigned int>(result.definition_register),
                                   static_cast<unsigned int>(result.concentration_register),
                                   static_cast<unsigned int>(result.low_alarm_register),
                                   static_cast<unsigned int>(result.high_alarm_register),
                                   static_cast<unsigned int>(result.range_register));
                        MYLOG_INFO("状态：{}（原始值 0x{:04X}），AD值：{}，温度：{:.1f} ℃，气体：{}（编码 {}），湿度：{:.1f} %",
                                   result.status,
                                   static_cast<unsigned int>(result.status_register),
                                   result.ad_value,
                                   result.temperature,
                                   result.gas,
                                   result.gas_code,
                                   result.humidity);
                        MYLOG_INFO("原始寄存器：R5=0x{:04X}，R6=0x{:04X}，R7=0x{:04X}，R8=0x{:04X}，R9=0x{:04X}",
                                   static_cast<unsigned int>(result.status_register),
                                   static_cast<unsigned int>(result.ad_register),
                                   static_cast<unsigned int>(result.temperature_register),
                                   static_cast<unsigned int>(result.gas_register),
                                   static_cast<unsigned int>(result.humidity_register));
                        MYLOG_INFO("【最终结果】√ 当前状态：{}；√ 当前浓度：{} {}；√ 气体：{}；√ 温度：{:.1f} ℃；√ 湿度：{:.1f} %",
                                   result.status,
                                   result.concentration_text,
                                   result.unit,
                                   result.gas,
                                   result.temperature,
                                   result.humidity);
                    } else {
                        if (result.crc_received != 0 || result.crc_calculated != 0) {
                            MYLOG_ERROR("【CRC/协议校验失败】{}", result.error);
                        } else {
                            MYLOG_ERROR("【报文处理失败】{}", result.error);
                        }
                    }
                }

                StoreResult(result, response);
                
                if (12 == print_log_countdown) {
                    LogSummary(true);
                    print_log_countdown = 0;
                }
                print_log_countdown = print_log_countdown + 1;
                

                next_request_at += std::chrono::milliseconds(config.interval_ms);
                const std::chrono::steady_clock::time_point now =
                    std::chrono::steady_clock::now();
                if (next_request_at < now) {
                    next_request_at = now;
                }
            }

            if (completed_cycle) {
                std::lock_guard<std::mutex> lock(mutex_);
                ++cycle_count_;
            }
        }
    } catch (const std::exception& ex) {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        communication_ok_ = false;
        state_ = PollState::Error;
        last_error_ = std::string("工作线程异常退出：") + ex.what();
        if (debug_log_enabled) {
            MYLOG_ERROR("[气体检测仪] 工作线程异常退出：{}", ex.what());
        }
        return;
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        communication_ok_ = false;
        state_ = PollState::Error;
        last_error_ = "工作线程异常退出：未知异常";
        if (debug_log_enabled) {
            MYLOG_ERROR("[气体检测仪] 工作线程异常退出：未知异常");
        }
        return;
    }

    if (debug_log_enabled) {
        MYLOG_INFO("[气体检测仪] 收到停止通知，工作线程安全退出");
    }
}

} // namespace my_gas_detector_poll
