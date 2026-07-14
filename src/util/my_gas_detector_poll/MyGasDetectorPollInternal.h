#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace my_gas_detector_poll {
namespace detail {

const std::size_t kMaxResponseSize = 260;
const std::uint32_t kDefaultReadSliceTimeoutMs = 50;

inline std::string TrimCopy(const std::string& value) {
    const std::string::size_type begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return std::string();
    }
    const std::string::size_type end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

inline std::string LowerCopy(const std::string& value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}

inline const nlohmann::json* FindValue(const nlohmann::json& object,
                                       std::initializer_list<const char*> keys) {
    if (!object.is_object()) {
        return 0;
    }

    std::initializer_list<const char*>::const_iterator it = keys.begin();
    for (; it != keys.end(); ++it) {
        if (object.contains(*it) && !object.at(*it).is_null()) {
            return &object.at(*it);
        }
    }
    return 0;
}

inline const nlohmann::json* FindConfigValue(const nlohmann::json& config,
                                             const nlohmann::json& serial_config,
                                             std::initializer_list<const char*> keys) {
    const nlohmann::json* value = FindValue(config, keys);
    if (value != 0) {
        return value;
    }
    if (&config != &serial_config) {
        return FindValue(serial_config, keys);
    }
    return 0;
}

inline std::string JsonString(const nlohmann::json& value,
                              const std::string& field_name,
                              bool allow_number = false) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (allow_number && (value.is_number_integer() ||
                         value.is_number_unsigned() ||
                         value.is_number_float())) {
        return value.dump();
    }
    throw std::invalid_argument("字段 " + field_name + " 必须是字符串" +
                                (allow_number ? "或数字" : ""));
}

inline std::uint64_t ParseUnsignedText(const std::string& text,
                                       const std::string& field_name) {
    const std::string normalized = TrimCopy(text);
    if (normalized.empty()) {
        throw std::invalid_argument("字段 " + field_name + " 不能为空");
    }
    if (normalized[0] == '-') {
        throw std::invalid_argument("字段 " + field_name + " 不能为负数");
    }

    errno = 0;
    std::size_t consumed = 0;
    const unsigned long long parsed = std::stoull(normalized, &consumed, 10);
    if (errno == ERANGE || consumed != normalized.size()) {
        throw std::invalid_argument("字段 " + field_name + " 必须是十进制整数");
    }
    return static_cast<std::uint64_t>(parsed);
}

inline std::uint64_t JsonUnsigned(const nlohmann::json& value,
                                  const std::string& field_name,
                                  std::uint64_t default_value) {
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    if (value.is_number_integer()) {
        const std::int64_t parsed = value.get<std::int64_t>();
        if (parsed < 0) {
            throw std::invalid_argument("字段 " + field_name + " 不能为负数");
        }
        return static_cast<std::uint64_t>(parsed);
    }
    if (value.is_string()) {
        return ParseUnsignedText(value.get<std::string>(), field_name);
    }
    if (value.is_null()) {
        return default_value;
    }
    throw std::invalid_argument("字段 " + field_name + " 必须是非负整数");
}

inline std::uint32_t ToUint32(std::uint64_t value, const std::string& field_name) {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::invalid_argument("字段 " + field_name + " 超出 uint32 范围");
    }
    return static_cast<std::uint32_t>(value);
}

inline std::uint16_t ToUint16(std::uint64_t value, const std::string& field_name) {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max())) {
        throw std::invalid_argument("字段 " + field_name + " 超出 uint16 范围");
    }
    return static_cast<std::uint16_t>(value);
}

inline int JsonInteger(const nlohmann::json& value,
                       const std::string& field_name,
                       int default_value) {
    if (value.is_number_integer()) {
        const std::int64_t parsed = value.get<std::int64_t>();
        if (parsed < static_cast<std::int64_t>(std::numeric_limits<int>::min()) ||
            parsed > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
            throw std::invalid_argument("字段 " + field_name + " 超出 int 范围");
        }
        return static_cast<int>(parsed);
    }
    if (value.is_number_unsigned()) {
        const std::uint64_t parsed = value.get<std::uint64_t>();
        if (parsed > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            throw std::invalid_argument("字段 " + field_name + " 超出 int 范围");
        }
        return static_cast<int>(parsed);
    }
    if (value.is_string()) {
        const std::uint64_t parsed = ParseUnsignedText(value.get<std::string>(), field_name);
        if (parsed > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            throw std::invalid_argument("字段 " + field_name + " 超出 int 范围");
        }
        return static_cast<int>(parsed);
    }
    if (value.is_null()) {
        return default_value;
    }
    throw std::invalid_argument("字段 " + field_name + " 必须是整数");
}

inline bool JsonBoolean(const nlohmann::json& value,
                        const std::string& field_name,
                        bool default_value) {
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_number_integer() || value.is_number_unsigned()) {
        return JsonUnsigned(value, field_name, default_value ? 1 : 0) != 0;
    }
    if (value.is_string()) {
        const std::string normalized = LowerCopy(TrimCopy(value.get<std::string>()));
        if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") {
            return true;
        }
        if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off") {
            return false;
        }
    }
    throw std::invalid_argument("字段 " + field_name + " 必须是布尔值");
}

inline std::string GetStringValue(const nlohmann::json& config,
                                  const nlohmann::json& serial_config,
                                  std::initializer_list<const char*> keys,
                                  const std::string& default_value,
                                  bool allow_number = false) {
    const nlohmann::json* value = FindConfigValue(config, serial_config, keys);
    return value == 0 ? default_value : JsonString(*value, *keys.begin(), allow_number);
}

inline std::uint64_t GetUnsignedValue(const nlohmann::json& config,
                                      const nlohmann::json& serial_config,
                                      std::initializer_list<const char*> keys,
                                      std::uint64_t default_value) {
    const nlohmann::json* value = FindConfigValue(config, serial_config, keys);
    return value == 0 ? default_value : JsonUnsigned(*value, *keys.begin(), default_value);
}

inline int GetIntegerValue(const nlohmann::json& config,
                           const nlohmann::json& serial_config,
                           std::initializer_list<const char*> keys,
                           int default_value) {
    const nlohmann::json* value = FindConfigValue(config, serial_config, keys);
    return value == 0 ? default_value : JsonInteger(*value, *keys.begin(), default_value);
}

inline bool GetBooleanValue(const nlohmann::json& config,
                            const nlohmann::json& serial_config,
                            std::initializer_list<const char*> keys,
                            bool default_value) {
    const nlohmann::json* value = FindConfigValue(config, serial_config, keys);
    return value == 0 ? default_value : JsonBoolean(*value, *keys.begin(), default_value);
}

inline void AppendAddress(int address,
                          std::vector<std::uint8_t>* addresses,
                          std::set<int>* seen) {
    if (address < 1 || address > 255) {
        throw std::invalid_argument("设备地址必须在 1～255 之间");
    }
    if (seen->insert(address).second) {
        addresses->push_back(static_cast<std::uint8_t>(address));
    }
}

inline void ParseAddressText(const std::string& text,
                             std::vector<std::uint8_t>* addresses,
                             std::set<int>* seen) {
    std::string normalized = TrimCopy(text);
    if (normalized.empty()) {
        throw std::invalid_argument("addresses 不能为空");
    }

    std::size_t begin = 0;
    while (begin < normalized.size()) {
        const std::size_t comma = normalized.find(',', begin);
        const std::string item = TrimCopy(normalized.substr(
            begin, comma == std::string::npos ? std::string::npos : comma - begin));
        if (item.empty()) {
            throw std::invalid_argument("addresses 存在空地址项");
        }

        const std::size_t dash = item.find('-');
        if (dash == std::string::npos) {
            const std::uint64_t address = ParseUnsignedText(item, "addresses");
            if (address > 255) {
                throw std::invalid_argument("设备地址必须在 1～255 之间");
            }
            AppendAddress(static_cast<int>(address), addresses, seen);
        } else {
            if (item.find('-', dash + 1) != std::string::npos) {
                throw std::invalid_argument("addresses 范围格式错误");
            }
            const std::uint64_t start = ParseUnsignedText(
                TrimCopy(item.substr(0, dash)), "addresses");
            const std::uint64_t end = ParseUnsignedText(
                TrimCopy(item.substr(dash + 1)), "addresses");
            if (start > 255 || end > 255) {
                throw std::invalid_argument("设备地址必须在 1～255 之间");
            }
            if (start > end) {
                throw std::invalid_argument("addresses 范围的起始地址不能大于结束地址");
            }
            for (std::uint64_t address = start; address <= end; ++address) {
                AppendAddress(static_cast<int>(address), addresses, seen);
            }
        }

        if (comma == std::string::npos) {
            break;
        }
        begin = comma + 1;
    }
}

inline std::vector<std::uint8_t> ParseAddresses(const nlohmann::json& value) {
    std::vector<std::uint8_t> addresses;
    std::set<int> seen;

    if (value.is_string()) {
        ParseAddressText(value.get<std::string>(), &addresses, &seen);
    } else if (value.is_array()) {
        for (nlohmann::json::const_iterator it = value.begin(); it != value.end(); ++it) {
            if (it->is_string()) {
                ParseAddressText(it->get<std::string>(), &addresses, &seen);
            } else {
                const std::uint64_t address = JsonUnsigned(*it, "addresses", 0);
                if (address > 255) {
                    throw std::invalid_argument("设备地址必须在 1～255 之间");
                }
                AppendAddress(static_cast<int>(address), &addresses, &seen);
            }
        }
    } else {
        const std::uint64_t address = JsonUnsigned(value, "addresses", 0);
        if (address > 255) {
            throw std::invalid_argument("设备地址必须在 1～255 之间");
        }
        AppendAddress(static_cast<int>(address), &addresses, &seen);
    }

    if (addresses.empty()) {
        throw std::invalid_argument("addresses 不能为空");
    }
    return addresses;
}

inline std::string FormatScaled(std::uint16_t raw, int decimals) {
    if (decimals <= 0) {
        return std::to_string(static_cast<unsigned int>(raw));
    }

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(decimals)
           << (static_cast<double>(raw) / std::pow(10.0, decimals));
    return stream.str();
}

inline std::string FormatFixed1(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << value;
    return stream.str();
}

inline std::string TimestampNow() {
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::chrono::milliseconds milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    const std::time_t time_value = std::chrono::system_clock::to_time_t(now);

    std::tm local_time;
#if defined(_WIN32)
    localtime_s(&local_time, &time_value);
#else
    localtime_r(&time_value, &local_time);
#endif

    char date_buffer[32] = {0};
    std::strftime(date_buffer, sizeof(date_buffer), "%Y-%m-%d %H:%M:%S", &local_time);
    const long millisecond_part = static_cast<long>(milliseconds.count() % 1000);

    std::ostringstream stream;
    stream << date_buffer << '.' << std::setfill('0') << std::setw(3) << millisecond_part;
    return stream.str();
}

inline std::string UnitName(int code) {
    switch (code) {
        case 0x0: return "ppm";
        case 0x2: return "%LEL";
        case 0x4: return "%VOL";
        case 0x6: return "mg/m³";
        case 0x8: return "ppb";
        case 0xA: return "℃";
        default: {
            std::ostringstream stream;
            stream << "未知单位(编码0x" << std::uppercase << std::hex << code << ')';
            return stream.str();
        }
    }
}

inline int DecimalPlaces(int code) {
    switch (code) {
        case 0x0: return 0;
        case 0x4: return 1;
        case 0x8: return 2;
        case 0xC: return 3;
        default: return 0;
    }
}

inline std::string StatusName(int code) {
    switch (code) {
        case 0x00: return "预热";
        case 0x01: return "正常工作";
        case 0x02: return "数据错误";
        case 0x03: return "传感器故障";
        case 0x04: return "预警";
        case 0x05: return "低报";
        case 0x06: return "高报";
        case 0x07: return "访问故障";
        case 0x08: return "超量程";
        case 0x09: return "需要标定";
        case 0x0A: return "超时";
        case 0x0B: return "STEL报警";
        case 0x0C: return "TWA报警";
        case 0x0D: return "保留";
        case 0x0E: return "保留";
        case 0x0F: return "通信故障";
        default: return "未知状态";
    }
}

inline std::string GasName(int code) {
    static const char* const names[] = {
        "NULL", "AR", "ASH3", "B2H6", "BR2", "CO", "CO2", "COCL2", "CH2O",
        "CH2O2", "CH3BR", "CH4", "CH4O", "CH4S", "CH5N", "CH6O", "CIC",
        "CL2", "CLO2", "C2CL4", "C2HCL3", "C2H2", "C2H3CL", "C2H", "C2H4O",
        "C2H6O", "C3H3N", "C3H4", "C3H6", "C3H8", "C4H8O2", "C4H8S",
        "C4H10", "C4H10O", "C5H12", "C6H6", "C6H6S", "C6H12", "C6H14",
        "C7H8", "C7H16", "C8H8", "C8H10", "C8H18", "CS2", "EX", "ETO", "F2",
        "FX", "GEH4", "H2", "H2O2", "H2S", "HCL", "HCN", "HBR", "HE", "HF",
        "KR", "N2", "NO2", "NOX", "NF3", "NH3", "N2", "N2O", "N2H4", "O2",
        "O3", "PH3", "PID", "P2O5", "SO2", "SO2F2", "SIH4", "SIF4", "SF6",
        "THT", "TVOC", "VOC", "VOCS", "SO3", "NMHC", "温度", "湿度", "风速", "风向"
    };
    static const std::size_t name_count = sizeof(names) / sizeof(names[0]);

    std::string symbol = code >= 0 && static_cast<std::size_t>(code) < name_count
                             ? names[code]
                             : "未知";
    std::string chinese;
    switch (code) {
        case 1: chinese = "氩气"; break;
        case 2: chinese = "砷化氢"; break;
        case 3: chinese = "乙硼烷"; break;
        case 4: chinese = "溴气"; break;
        case 5: chinese = "一氧化碳"; break;
        case 6: chinese = "二氧化碳"; break;
        case 7: chinese = "光气"; break;
        case 8: chinese = "甲醛"; break;
        case 11: chinese = "甲烷"; break;
        case 17: chinese = "氯气"; break;
        case 18: chinese = "二氧化氯"; break;
        case 21: chinese = "乙炔"; break;
        case 29: chinese = "丙烷"; break;
        case 35: chinese = "苯"; break;
        case 39: chinese = "甲苯"; break;
        case 44: chinese = "二硫化碳"; break;
        case 47: chinese = "氟气"; break;
        case 50: chinese = "氢气"; break;
        case 52: chinese = "硫化氢"; break;
        case 53: chinese = "氯化氢"; break;
        case 54: chinese = "氰化氢"; break;
        case 56: chinese = "氦气"; break;
        case 57: chinese = "氟化氢"; break;
        case 60: chinese = "二氧化氮"; break;
        case 63: chinese = "氨气"; break;
        case 67: chinese = "氧气"; break;
        case 68: chinese = "臭氧"; break;
        case 69: chinese = "磷化氢"; break;
        case 72: chinese = "二氧化硫"; break;
        case 76: chinese = "六氟化硫"; break;
        default: break;
    }
    return chinese.empty() ? symbol : symbol + "(" + chinese + ")";
}

inline std::string ExceptionName(int code) {
    switch (code) {
        case 1: return "非法功能";
        case 2: return "非法数据地址";
        case 3: return "非法数据值";
        case 4: return "从站设备故障";
        default: return "未知异常";
    }
}

inline std::string SafeJsonDump(const nlohmann::json& value) {
    try {
        return value.dump(4);
    } catch (const std::exception& ex) {
        return std::string("<JSON序列化失败：") + ex.what() + ">";
    } catch (...) {
        return "<JSON序列化失败：未知异常>";
    }
}

} // namespace detail
} // namespace my_gas_detector_poll
