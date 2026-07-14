#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <serial/serial.h>

namespace my_serial {

/**
 * @brief MySerial 的串口初始化参数。
 *
 * 该结构体是 JSON 配置解析后的内部标准形式。业务层通常不需要直接
 * 构造它，而是将 JSON 交给 MySerial::Init()，由 Init() 负责完成字段
 * 兼容、范围检查和枚举值转换。
 *
 * 所有 timeout 字段的单位均为毫秒。read/write 的 constant 和 multiplier
 * 会传递给底层 serial::Timeout；当它们保持默认组合时，模块使用简单
 * 超时模式，以便与 timeout_ms 的直观语义保持一致。
 */
struct SerialInitOptions {
    std::string port;
    uint32_t baudrate{9600};
    uint32_t timeout_ms{1000};
    uint32_t inter_byte_timeout_ms{0};
    uint32_t read_timeout_constant_ms{1000};
    uint32_t read_timeout_multiplier_ms{0};
    uint32_t write_timeout_constant_ms{1000};
    uint32_t write_timeout_multiplier_ms{0};
    std::string bytesize{"eightbits"};
    std::string parity{"none"};
    std::string stopbits{"one"};
    std::string flowcontrol{"none"};
    bool auto_open{true};
};

/**
 * @brief 串口当前状态的只读快照。
 *
 * 快照是一次性复制出来的状态，不代表后续时刻的实时状态。调用方可
 * 将其用于日志、健康检查或转换为 JSON 后返回给管理接口。
 */
struct SerialPortSnapshot {
    bool initialized{false};
    bool open{false};
    std::string port;
    uint32_t baudrate{0};
    uint32_t timeout_ms{0};
    size_t available_bytes{0};
    std::string bytesize;
    std::string parity;
    std::string stopbits;
    std::string flowcontrol;
    std::string last_error;
};

/**
 * @brief 串口自检或回环自测的统一返回结果。
 *
 * success 表示检查结论；summary 是适合日志展示的简短描述；detail
 * 保存快照、检查项以及测试收发数据等结构化诊断信息。
 */
struct SerialSelfCheckResult {
    bool success{false};
    std::string summary;
    nlohmann::json detail;
};

/**
 * @brief 面向业务代码的串口封装。
 *
 * MySerial 在项目的 serial::Serial 底层库之上统一提供：
 * 1. JSON 配置初始化；
 * 2. 串口生命周期管理；
 * 3. 文本和二进制数据收发；
 * 4. 状态快照、端口枚举和回环自测；
 * 5. 基于互斥锁的实例级串行访问。
 *
 * 使用顺序通常为 Init() ->（auto_open=false 时调用 Open()）->
 * Read()/Write()/ReadLine() -> Close()。所有串口对象操作都由 mutex_
 * 保护，因此同一个 MySerial 实例可以被多个线程安全调用，但一次读写
 * 或查询仍然会独占该实例，长时间阻塞的读操作也会持有这把锁。
 */
class MySerial {
public:
    /** @brief 创建一个尚未初始化、未打开串口的实例。 */
    MySerial();

    /** @brief 析构实例并关闭仍处于打开状态的串口。 */
    ~MySerial();

    /**
     * @brief 解析配置、创建底层串口对象并按配置决定是否打开。
     * @param cfg JSON 对象，至少提供非空的 port 或 device。
     * @param err 可选错误输出；成功时清空，失败时写入原因。
     * @return 成功返回 true；配置非法、底层创建失败或打开失败返回 false。
     *
     * 重新 Init() 时会先关闭已有端口。新的初始化若失败，实例会回到
     * 未初始化状态，之前的串口配置不会自动恢复。
     */
    bool Init(const nlohmann::json& cfg, std::string* err = nullptr);

    /**
     * @brief 打开已初始化但尚未打开的串口。
     * @param err 可选错误输出。
     * @return 打开成功或本来已经打开时返回 true。
     */
    bool Open(std::string* err = nullptr);

    /** @brief 关闭当前串口；未初始化或已经关闭时不执行底层关闭操作。 */
    void Close();

    /** @brief 返回当前底层串口是否处于打开状态。 */
    bool IsOpen() const;

    /**
     * @brief 写入文本/字符串数据。
     * @return 成功时返回实际写入的字节数；失败时返回 0。
     */
    size_t Write(const std::string& data, std::string* err = nullptr);

    /**
     * @brief 写入原始字节数据，适合二进制协议。
     * @return 成功时返回实际写入的字节数；失败时返回 0。
     */
    size_t Write(const std::vector<uint8_t>& data, std::string* err = nullptr);

    /**
     * @brief 读取指定数量的字节并以字符串返回。
     * @param size 期望读取的字节数，实际返回长度可能受底层超时影响。
     */
    std::string Read(size_t size, std::string* err = nullptr);

    /**
     * @brief 读取指定数量的字节并以 uint8_t 数组返回。
     * @param size 期望读取的字节数，结果会收缩为实际读取长度。
     */
    std::vector<uint8_t> ReadBytes(size_t size, std::string* err = nullptr);

    /**
     * @brief 读取一行数据，直到遇到 eol 或达到 max_size/超时。
     * @param max_size 单次读取允许返回的最大长度。
     * @param eol 行结束符，默认为换行符 "\n"。
     */
    std::string ReadLine(size_t max_size = 65536, const std::string& eol = "\n", std::string* err = nullptr);

    /**
     * @brief 查询底层接收缓冲区当前可读取的字节数。
     * @return 可读取字节数；调用失败时返回 0，并通过 err 报告错误。
     */
    size_t Available(std::string* err = nullptr) const;

    /**
     * @brief 枚举操作系统当前可识别的串口。
     * @return 每项包含 port、description 和 hardware_id 字段的 JSON 数组。
     *
     * 该接口不要求当前实例已经 Init()，适合用于配置页面和启动前诊断。
     */
    std::vector<nlohmann::json> ListAvailablePorts() const;

    /** @brief 获取当前实例的 C++ 状态快照。 */
    SerialPortSnapshot GetSnapshot() const;

    /** @brief 获取与 SerialPortSnapshot 等价的 JSON 状态快照。 */
    nlohmann::json GetSnapshotJson() const;

    /**
     * @brief 检查初始化状态、打开状态及配置端口是否可枚举。
     *
     * SelfCheck() 只做状态检查，不向设备发送业务数据，因此不会验证
     * 设备协议或设备是否真正响应。
     */
    SerialSelfCheckResult SelfCheck() const;

    /**
     * @brief 执行一次写入后读取的回环测试。
     * @param payload 要发送并期望收到的字符串。
     * @param read_size 读取长度；为 0 时使用 payload.size()。
     * @return 包含收发数据、长度和状态快照的测试结果。
     *
     * 该接口要求对端回显数据，适用于回环线、伪终端和测试设备；普通
     * 不回显的传感器或执行器不能直接使用该接口判断链路是否正常。
     */
    SerialSelfCheckResult SelfTestLoopback(const std::string& payload, size_t read_size = 0);

private:
    static SerialInitOptions ParseOptions(const nlohmann::json& cfg);
    static serial::bytesize_t ParseBytesize(const std::string& value);
    static serial::parity_t ParseParity(const std::string& value);
    static serial::stopbits_t ParseStopbits(const std::string& value);
    static serial::flowcontrol_t ParseFlowcontrol(const std::string& value);

    static std::string ToString(serial::bytesize_t value);
    static std::string ToString(serial::parity_t value);
    static std::string ToString(serial::stopbits_t value);
    static std::string ToString(serial::flowcontrol_t value);

private:
    mutable std::mutex mutex_;
    bool initialized_{false};
    mutable std::string last_error_;
    SerialInitOptions options_;
    std::unique_ptr<serial::Serial> serial_;
};

} // namespace my_serial