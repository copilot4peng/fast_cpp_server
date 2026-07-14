#include "MySerial.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>

#include "MyLog.h"

namespace my_serial {

namespace {

// 配置中的枚举值允许使用大小写混合的字符串；统一转小写后再做匹配，
// 同时保留数字形式（例如数据位 8、停止位 1）由调用方传入的原始语义。
std::string NormalizeToken(const std::string& value) {
    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return normalized;
}

// 日志本身不能因为 JSON 序列化异常而掩盖真正的初始化错误，因此日志
// 专用的 dump 必须兜底。该函数只影响诊断文本，不影响配置解析结果。
std::string SafeJsonDump(const nlohmann::json& value) {
    try {
        return value.dump();
    } catch (const std::exception& ex) {
        return std::string("<json dump failed: ") + ex.what() + ">";
    } catch (...) {
        return "<json dump failed: unknown exception>";
    }
}

// 将 JSON 中的字符串、整数或浮点数转换为枚举解析所需的 token。这里
// 允许数字是为了兼容设备配置文件中的 data_bits=8、stop_bits=1 等写法；
// 布尔值、数组、对象和 null 会被明确拒绝。
std::string JsonValueToToken(const nlohmann::json& value, const char* field_name) {
    if (value.is_string()) {
        return NormalizeToken(value.get<std::string>());
    }
    if (value.is_number_unsigned()) {
        return std::to_string(value.get<std::uint64_t>());
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<std::int64_t>());
    }
    if (value.is_number_float()) {
        return NormalizeToken(value.dump());
    }
    throw std::invalid_argument(std::string("serial config field '") + field_name + "' must be a string or number");
}

// port 是模块的标准字段，device 是兼容已有设备配置的别名。两者同时
// 存在时优先使用 port；空字符串不会被当作有效设备路径。
std::string GetRequiredPortValue(const nlohmann::json& cfg) {
    for (const char* key : {"port", "device"}) {
        if (!cfg.contains(key) || cfg.at(key).is_null()) {
            continue;
        }

        if (!cfg.at(key).is_string()) {
            throw std::invalid_argument(std::string("serial config field '") + key + "' must be a string");
        }

        const std::string port = cfg.at(key).get<std::string>();
        if (!port.empty()) {
            return port;
        }
    }

    throw std::invalid_argument("serial config missing non-empty field: port/device");
}

// 读取无符号 32 位整数配置，并对 JSON 整数和字符串形式统一做范围校验。
// 负数、超出 uint32_t 范围的数值以及非整数类型都不会静默截断。
uint32_t GetUint32Value(const nlohmann::json& cfg,
                       std::initializer_list<const char*> keys,
                       uint32_t default_value) {
    for (const char* key : keys) {
        if (!cfg.contains(key) || cfg.at(key).is_null()) {
            continue;
        }

        const auto& value = cfg.at(key);
        if (value.is_number_unsigned()) {
            const auto parsed = value.get<std::uint64_t>();
            if (parsed > static_cast<std::uint64_t>(std::numeric_limits<uint32_t>::max())) {
                throw std::invalid_argument(std::string("serial config field '") + key + "' is out of uint32 range");
            }
            return static_cast<uint32_t>(parsed);
        }

        if (value.is_number_integer()) {
            const auto parsed = value.get<std::int64_t>();
            if (parsed < 0 || parsed > static_cast<std::int64_t>(std::numeric_limits<uint32_t>::max())) {
                throw std::invalid_argument(std::string("serial config field '") + key + "' is out of uint32 range");
            }
            return static_cast<uint32_t>(parsed);
        }

        if (value.is_string()) {
            const auto parsed = std::stoull(value.get<std::string>());
            if (parsed > static_cast<unsigned long long>(std::numeric_limits<uint32_t>::max())) {
                throw std::invalid_argument(std::string("serial config field '") + key + "' is out of uint32 range");
            }
            return static_cast<uint32_t>(parsed);
        }

        throw std::invalid_argument(std::string("serial config field '") + key + "' must be an unsigned integer");
    }

    return default_value;
}

// 按字段别名查找一个 token；第一个存在且非 null 的字段生效。该规则与
// 端口和波特率的兼容字段保持一致，便于迁移旧版设备配置。
std::string GetTokenValue(const nlohmann::json& cfg,
                          std::initializer_list<const char*> keys,
                          const std::string& default_value) {
    for (const char* key : keys) {
        if (!cfg.contains(key) || cfg.at(key).is_null()) {
            continue;
        }
        return JsonValueToToken(cfg.at(key), key);
    }
    return NormalizeToken(default_value);
}

// 当所有读写超时参数构成最常见的简单超时组合时，使用 serial 库提供的
// simpleTimeout；否则保留每一项参数，支持更细粒度的读写超时策略。
serial::Timeout BuildTimeout(const SerialInitOptions& options) {
    if (options.inter_byte_timeout_ms == 0 &&
        options.read_timeout_constant_ms == options.timeout_ms &&
        options.read_timeout_multiplier_ms == 0 &&
        options.write_timeout_constant_ms == options.timeout_ms &&
        options.write_timeout_multiplier_ms == 0) {
        return serial::Timeout::simpleTimeout(options.timeout_ms);
    }

    return serial::Timeout(
        options.inter_byte_timeout_ms,
        options.read_timeout_constant_ms,
        options.read_timeout_multiplier_ms,
        options.write_timeout_constant_ms,
        options.write_timeout_multiplier_ms);
}

// 统一封装底层 serial 库异常：接口不向业务层抛出常规运行时错误，而是
// 返回失败值，并同时更新调用方 err 和实例级 last_error_。成功时清除旧错。
template <typename Fn>
bool ExecuteWithError(std::string* err, std::string& last_error, Fn&& fn) {
    try {
        fn();
        last_error.clear();
        if (err != nullptr) {
            err->clear();
        }
        return true;
    } catch (const std::exception& ex) {
        last_error = ex.what();
        if (err != nullptr) {
            *err = last_error;
        }
        return false;
    }
}

} // namespace

MySerial::MySerial() = default;

MySerial::~MySerial() {
    // Close() 自身带锁，析构时复用统一的关闭路径，避免遗漏底层资源释放。
    Close();
}

bool MySerial::Init(const nlohmann::json& cfg, std::string* err) {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::string cfg_dump = SafeJsonDump(cfg);
    const bool had_previous_serial = static_cast<bool>(serial_);
    const bool previous_open = serial_ && serial_->isOpen();
    const std::string previous_port = options_.port;

    SerialInitOptions parsed_options;
    bool options_parsed = false;
    std::string current_step = "start";

    try {
        // 初始化采用“先准备新对象、成功后提交”的结构：配置解析和底层
        // 参数设置失败时不会留下半初始化的 serial_；但重新初始化前的
        // 旧端口会先关闭，失败后不会自动回滚到旧实例。
        MYLOG_INFO("[MySerial] Begin Init, cfg={}", cfg.dump(4));

        if (serial_ && serial_->isOpen()) {
            current_step = "close previous port";
            MYLOG_WARN("[MySerial] Closing previously opened port before re-init: {}", options_.port);
            serial_->close();
        }

        initialized_ = false;

        current_step = "parse config";
        parsed_options = ParseOptions(cfg);
        options_parsed = true;
        MYLOG_INFO(
            "[MySerial] Parsed config: port={}, baudrate={}, timeout_ms={}, auto_open={}, bytesize={}, parity={}, stopbits={}, flowcontrol={}, inter_byte_timeout_ms={}, read_timeout_constant_ms={}, read_timeout_multiplier_ms={}, write_timeout_constant_ms={}, write_timeout_multiplier_ms={}",
            parsed_options.port,
            parsed_options.baudrate,
            parsed_options.timeout_ms,
            parsed_options.auto_open ? "true" : "false",
            parsed_options.bytesize,
            parsed_options.parity,
            parsed_options.stopbits,
            parsed_options.flowcontrol,
            parsed_options.inter_byte_timeout_ms,
            parsed_options.read_timeout_constant_ms,
            parsed_options.read_timeout_multiplier_ms,
            parsed_options.write_timeout_constant_ms,
            parsed_options.write_timeout_multiplier_ms);

        current_step = "create serial instance";
        auto new_serial = std::make_unique<serial::Serial>();
        MYLOG_INFO("[MySerial] serial::Serial instance created, preparing low-level configuration");

        current_step = "apply basic port settings";
        new_serial->setPort(parsed_options.port);
        new_serial->setBaudrate(parsed_options.baudrate);

        current_step = "apply timeout settings";
        serial::Timeout timeout = BuildTimeout(parsed_options);
        new_serial->setTimeout(timeout);

        current_step = "apply line settings";
        new_serial->setBytesize(ParseBytesize(parsed_options.bytesize));
        new_serial->setParity(ParseParity(parsed_options.parity));
        new_serial->setStopbits(ParseStopbits(parsed_options.stopbits));
        new_serial->setFlowcontrol(ParseFlowcontrol(parsed_options.flowcontrol));

        if (parsed_options.auto_open) {
            // open() 失败会直接进入统一错误处理，因而 Init() 返回 false，
            // 调用方可通过 err 或 GetSnapshotJson() 获取失败原因。
            current_step = "auto open port";
            MYLOG_INFO("[MySerial] auto_open=true, trying to open port: {}", parsed_options.port);
            new_serial->open();
            MYLOG_INFO("[MySerial] Port opened during Init: {}", parsed_options.port);
        } else {
            MYLOG_INFO("[MySerial] auto_open=false, skip opening port during Init: {}", parsed_options.port);
        }

        current_step = "commit initialized state";
        serial_ = std::move(new_serial);
        options_ = parsed_options;
        initialized_ = true;
        last_error_.clear();

        if (err != nullptr) {
            err->clear();
        }

        MYLOG_INFO("[MySerial] Init success: port={}, baudrate={}, auto_open={}, initialized=true",
                   options_.port,
                   options_.baudrate,
                   options_.auto_open ? "true" : "false");
        return true;
    } catch (const std::exception& ex) {
        initialized_ = false;
        serial_.reset();
        options_ = SerialInitOptions{};
        last_error_ = ex.what();
        if (err != nullptr) {
            *err = last_error_;
        }

        MYLOG_ERROR(
            "[MySerial] Init failed at step='{}', error='{}', cfg={}, had_previous_serial={}, previous_open={}, previous_port={}, parsed_ok={}, parsed_port={}, parsed_baudrate={}, parsed_auto_open={}",
            current_step,
            ex.what(),
            cfg_dump,
            had_previous_serial ? "true" : "false",
            previous_open ? "true" : "false",
            previous_port.empty() ? "<empty>" : previous_port,
            options_parsed ? "true" : "false",
            options_parsed ? parsed_options.port : std::string("<unparsed>"),
            options_parsed ? std::to_string(parsed_options.baudrate) : std::string("<unparsed>"),
            options_parsed ? (parsed_options.auto_open ? "true" : "false") : std::string("<unparsed>"));
        return false;
    } catch (...) {
        initialized_ = false;
        serial_.reset();
        options_ = SerialInitOptions{};
        last_error_ = "unknown exception";
        if (err != nullptr) {
            *err = last_error_;
        }

        MYLOG_ERROR(
            "[MySerial] Init failed at step='{}' with unknown exception, cfg={}, had_previous_serial={}, previous_open={}, previous_port={}, parsed_ok={}, parsed_port={}",
            current_step,
            cfg_dump,
            had_previous_serial ? "true" : "false",
            previous_open ? "true" : "false",
            previous_port.empty() ? "<empty>" : previous_port,
            options_parsed ? "true" : "false",
            options_parsed ? parsed_options.port : std::string("<unparsed>"));
        return false;
    }
}

bool MySerial::Open(std::string* err) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Init(auto_open=false) 只完成参数配置，不占用设备；需要实际收发时
    // 必须显式 Open()。重复 Open() 被视为成功，不会重复调用底层 open。
    return ExecuteWithError(err, last_error_, [this]() {
        if (!initialized_ || !serial_) {
            throw std::runtime_error("MySerial is not initialized");
        }
        if (!serial_->isOpen()) {
            serial_->open();
            MYLOG_INFO("[MySerial] Port opened: {}", options_.port);
        }
    });
}

void MySerial::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Close() 是幂等的：未创建、未打开或已经关闭的实例都可以安全调用。
    if (serial_ && serial_->isOpen()) {
        serial_->close();
        MYLOG_INFO("[MySerial] Port closed: {}", options_.port);
    }
}

bool MySerial::IsOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return serial_ && serial_->isOpen();
}

size_t MySerial::Write(const std::string& data, std::string* err) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t written = 0;
    // 字符串按字节写出，不附加换行符、不做编码转换；文本协议需要的
    // \r、\n 必须由调用方显式放入 data。
    ExecuteWithError(err, last_error_, [this, &data, &written]() {
        if (!initialized_ || !serial_) {
            throw std::runtime_error("MySerial is not initialized");
        }
        if (!serial_->isOpen()) {
            throw std::runtime_error("Serial port is not open");
        }
        written = serial_->write(data);
    });
    return written;
}

size_t MySerial::Write(const std::vector<uint8_t>& data, std::string* err) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t written = 0;
    // vector<uint8_t> 路径用于二进制帧，数据中的 0x00 等字节不会被当作
    // C 字符串结束符处理。
    ExecuteWithError(err, last_error_, [this, &data, &written]() {
        if (!initialized_ || !serial_) {
            throw std::runtime_error("MySerial is not initialized");
        }
        if (!serial_->isOpen()) {
            throw std::runtime_error("Serial port is not open");
        }
        written = serial_->write(data);
    });
    return written;
}

std::string MySerial::Read(size_t size, std::string* err) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string result;
    // Read() 的长度参数是字节数而不是字符数；实际返回长度取决于底层
    // 超时和当前接收数据量，协议层如需严格帧长应自行检查 result.size()。
    ExecuteWithError(err, last_error_, [this, size, &result]() {
        if (!initialized_ || !serial_) {
            throw std::runtime_error("MySerial is not initialized");
        }
        if (!serial_->isOpen()) {
            throw std::runtime_error("Serial port is not open");
        }
        result = serial_->read(size);
    });
    return result;
}

std::vector<uint8_t> MySerial::ReadBytes(size_t size, std::string* err) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint8_t> result;
    // 先按期望长度分配，再按底层实际读取长度收缩结果，避免调用方误把
    // 未填充的尾部字节当成有效协议内容。
    ExecuteWithError(err, last_error_, [this, size, &result]() {
        if (!initialized_ || !serial_) {
            throw std::runtime_error("MySerial is not initialized");
        }
        if (!serial_->isOpen()) {
            throw std::runtime_error("Serial port is not open");
        }
        result.resize(size);
        const size_t read_size = serial_->read(result.data(), size);
        result.resize(read_size);
    });
    return result;
}

std::string MySerial::ReadLine(size_t max_size, const std::string& eol, std::string* err) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string result;
    // ReadLine() 的结束符由调用方指定，适合文本设备的 \n、\r\n 等行协议。
    ExecuteWithError(err, last_error_, [this, max_size, &eol, &result]() {
        if (!initialized_ || !serial_) {
            throw std::runtime_error("MySerial is not initialized");
        }
        if (!serial_->isOpen()) {
            throw std::runtime_error("Serial port is not open");
        }
        result = serial_->readline(max_size, eol);
    });
    return result;
}

size_t MySerial::Available(std::string* err) const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t available = 0;
    // available() 只查询当前缓冲区，不等待新数据；它不能替代 Read() 的
    // 超时机制，也不能保证查询后数据仍然未被其他线程读取。
    ExecuteWithError(err, last_error_, [this, &available]() {
        if (!initialized_ || !serial_) {
            throw std::runtime_error("MySerial is not initialized");
        }
        if (!serial_->isOpen()) {
            throw std::runtime_error("Serial port is not open");
        }
        available = serial_->available();
    });
    return available;
}

std::vector<nlohmann::json> MySerial::ListAvailablePorts() const {
    std::vector<nlohmann::json> ports_json;
    // 端口枚举不依赖本实例状态，失败行为由底层 serial::list_ports() 决定；
    // 这里将底层 PortInfo 转成稳定的 JSON 结构，便于直接对外提供。
    for (const auto& port_info : serial::list_ports()) {
        ports_json.push_back({
            {"port", port_info.port},
            {"description", port_info.description},
            {"hardware_id", port_info.hardware_id}
        });
    }
    return ports_json;
}

SerialPortSnapshot MySerial::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);

    // 先复制状态，再在端口打开时尝试查询 available。查询失败只影响快照
    // 中的 last_error，不让诊断接口本身抛出异常。
    SerialPortSnapshot snapshot;
    snapshot.initialized = initialized_;
    snapshot.last_error = last_error_;

    if (!initialized_ || !serial_) {
        return snapshot;
    }

    snapshot.open = serial_->isOpen();
    snapshot.port = options_.port;
    snapshot.baudrate = options_.baudrate;
    snapshot.timeout_ms = options_.timeout_ms;
    snapshot.bytesize = options_.bytesize;
    snapshot.parity = options_.parity;
    snapshot.stopbits = options_.stopbits;
    snapshot.flowcontrol = options_.flowcontrol;

    if (snapshot.open) {
        try {
            snapshot.available_bytes = serial_->available();
        } catch (const std::exception& ex) {
            snapshot.last_error = ex.what();
        }
    }

    return snapshot;
}

nlohmann::json MySerial::GetSnapshotJson() const {
    // 通过 C++ 快照统一生成 JSON，避免两个状态出口字段不一致。
    const SerialPortSnapshot snapshot = GetSnapshot();
    return {
        {"initialized", snapshot.initialized},
        {"open", snapshot.open},
        {"port", snapshot.port},
        {"baudrate", snapshot.baudrate},
        {"timeout_ms", snapshot.timeout_ms},
        {"available_bytes", snapshot.available_bytes},
        {"bytesize", snapshot.bytesize},
        {"parity", snapshot.parity},
        {"stopbits", snapshot.stopbits},
        {"flowcontrol", snapshot.flowcontrol},
        {"last_error", snapshot.last_error}
    };
}

SerialSelfCheckResult MySerial::SelfCheck() const {
    // SelfCheck 同时采集快照和系统端口列表，但 success 的核心判据是实例
    // 已打开。configured_port_found 作为诊断信息保留，不改变 success 判定。
    SerialSelfCheckResult result;
    result.detail["snapshot"] = GetSnapshotJson();
    result.detail["available_ports"] = ListAvailablePorts();

    const SerialPortSnapshot snapshot = GetSnapshot();
    if (!snapshot.initialized) {
        result.summary = "serial not initialized";
        result.detail["checks"] = {
            {"initialized", false},
            {"open", false}
        };
        return result;
    }

    bool port_found = false;
    for (const auto& port_item : result.detail["available_ports"]) {
        if (port_item.value("port", std::string()) == snapshot.port) {
            port_found = true;
            break;
        }
    }

    result.success = snapshot.open;
    result.summary = snapshot.open ? "serial self check passed" : "serial self check failed";
    result.detail["checks"] = {
        {"initialized", snapshot.initialized},
        {"open", snapshot.open},
        {"configured_port_found", port_found},
        {"available_bytes_non_negative", true}
    };
    return result;
}

SerialSelfCheckResult MySerial::SelfTestLoopback(const std::string& payload, size_t read_size) {
    // 回环测试复用公开的 Write/Read，因此会继承同样的初始化、打开和超时
    // 规则。read_size=0 表示按 payload 长度读取；对端必须实际回显数据。
    SerialSelfCheckResult result;
    std::string err;

    const size_t expected_size = read_size == 0 ? payload.size() : read_size;
    const size_t written = Write(payload, &err);
    if (!err.empty()) {
        result.summary = "write failed: " + err;
        result.detail["payload"] = payload;
        return result;
    }

    const std::string received = Read(expected_size, &err);
    if (!err.empty()) {
        result.summary = "read failed: " + err;
        result.detail["payload"] = payload;
        result.detail["written_size"] = written;
        return result;
    }

    result.success = (received == payload);
    result.summary = result.success ? "serial loopback self test passed" : "serial loopback self test mismatch";
    result.detail = {
        {"payload", payload},
        {"written_size", written},
        {"expected_size", expected_size},
        {"received", received},
        {"snapshot", GetSnapshotJson()}
    };
    return result;
}

SerialInitOptions MySerial::ParseOptions(const nlohmann::json& cfg) {
    if (!cfg.is_object()) {
        throw std::invalid_argument("serial config must be a json object");
    }

    // 先使用结构体默认值，再逐项覆盖 JSON 字段；未提供的可选项因此保持
    // eightbits/none/one/none 和 1000ms 等默认配置。
    SerialInitOptions options;
    options.port = GetRequiredPortValue(cfg);
    options.baudrate = GetUint32Value(cfg, {"baudrate", "baud_rate"}, options.baudrate);
    options.timeout_ms = GetUint32Value(cfg, {"timeout_ms"}, options.timeout_ms);
    options.inter_byte_timeout_ms = GetUint32Value(cfg, {"inter_byte_timeout_ms"}, options.inter_byte_timeout_ms);
    options.read_timeout_constant_ms = GetUint32Value(cfg, {"read_timeout_constant_ms"}, options.timeout_ms);
    options.read_timeout_multiplier_ms = GetUint32Value(cfg, {"read_timeout_multiplier_ms"}, options.read_timeout_multiplier_ms);
    options.write_timeout_constant_ms = GetUint32Value(cfg, {"write_timeout_constant_ms"}, options.timeout_ms);
    options.write_timeout_multiplier_ms = GetUint32Value(cfg, {"write_timeout_multiplier_ms"}, options.write_timeout_multiplier_ms);
    options.bytesize = ToString(ParseBytesize(GetTokenValue(cfg, {"bytesize", "data_bits"}, options.bytesize)));
    options.parity = ToString(ParseParity(GetTokenValue(cfg, {"parity"}, options.parity)));
    options.stopbits = ToString(ParseStopbits(GetTokenValue(cfg, {"stopbits", "stop_bits"}, options.stopbits)));
    options.flowcontrol = ToString(ParseFlowcontrol(GetTokenValue(cfg, {"flowcontrol", "flow_control"}, options.flowcontrol)));
    options.auto_open = cfg.value("auto_open", options.auto_open);
    return options;
}

serial::bytesize_t MySerial::ParseBytesize(const std::string& value) {
    // serial 库枚举不能直接从 JSON 使用，集中在这里完成字符串/数字兼容。
    const std::string normalized = NormalizeToken(value);
    if (normalized == "fivebits" || normalized == "5") return serial::fivebits;
    if (normalized == "sixbits" || normalized == "6") return serial::sixbits;
    if (normalized == "sevenbits" || normalized == "7") return serial::sevenbits;
    if (normalized == "eightbits" || normalized == "8") return serial::eightbits;
    throw std::invalid_argument("unsupported bytesize: " + value);
}

serial::parity_t MySerial::ParseParity(const std::string& value) {
    const std::string normalized = NormalizeToken(value);
    if (normalized == "none" || normalized == "parity_none") return serial::parity_none;
    if (normalized == "odd" || normalized == "parity_odd") return serial::parity_odd;
    if (normalized == "even" || normalized == "parity_even") return serial::parity_even;
    if (normalized == "mark" || normalized == "parity_mark") return serial::parity_mark;
    if (normalized == "space" || normalized == "parity_space") return serial::parity_space;
    throw std::invalid_argument("unsupported parity: " + value);
}

serial::stopbits_t MySerial::ParseStopbits(const std::string& value) {
    const std::string normalized = NormalizeToken(value);
    if (normalized == "one" || normalized == "1") return serial::stopbits_one;
    if (normalized == "one_point_five" || normalized == "1.5") return serial::stopbits_one_point_five;
    if (normalized == "two" || normalized == "2") return serial::stopbits_two;
    throw std::invalid_argument("unsupported stopbits: " + value);
}

serial::flowcontrol_t MySerial::ParseFlowcontrol(const std::string& value) {
    const std::string normalized = NormalizeToken(value);
    if (normalized == "none" || normalized == "flowcontrol_none") return serial::flowcontrol_none;
    if (normalized == "software" || normalized == "flowcontrol_software") return serial::flowcontrol_software;
    if (normalized == "hardware" || normalized == "flowcontrol_hardware") return serial::flowcontrol_hardware;
    throw std::invalid_argument("unsupported flowcontrol: " + value);
}

std::string MySerial::ToString(serial::bytesize_t value) {
    // ToString() 用于把底层枚举还原为稳定、可序列化的配置文本。
    switch (value) {
        case serial::fivebits: return "fivebits";
        case serial::sixbits: return "sixbits";
        case serial::sevenbits: return "sevenbits";
        case serial::eightbits: return "eightbits";
        default: return "unknown";
    }
}

std::string MySerial::ToString(serial::parity_t value) {
    switch (value) {
        case serial::parity_none: return "none";
        case serial::parity_odd: return "odd";
        case serial::parity_even: return "even";
        case serial::parity_mark: return "mark";
        case serial::parity_space: return "space";
        default: return "unknown";
    }
}

std::string MySerial::ToString(serial::stopbits_t value) {
    switch (value) {
        case serial::stopbits_one: return "one";
        case serial::stopbits_one_point_five: return "one_point_five";
        case serial::stopbits_two: return "two";
        default: return "unknown";
    }
}

std::string MySerial::ToString(serial::flowcontrol_t value) {
    switch (value) {
        case serial::flowcontrol_none: return "none";
        case serial::flowcontrol_software: return "software";
        case serial::flowcontrol_hardware: return "hardware";
        default: return "unknown";
    }
}

} // namespace my_serial