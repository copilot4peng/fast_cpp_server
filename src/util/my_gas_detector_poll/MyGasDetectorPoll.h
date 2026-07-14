#pragma once

/**
 * @file gas_detector_poll.h
 * @brief 基于 Modbus-RTU 的气体检测仪轮询模块。
 *
 * Python 版本的 gas_detector_poll.py 负责完成以下工作：
 * 1. 按从站地址发送 Modbus-RTU 功能码 03 请求；
 * 2. 读取可变长度的正常响应或异常响应；
 * 3. 校验 Modbus CRC-16；
 * 4. 解析 10 个保持寄存器中的浓度、报警阈值、状态、温度、气体类型和湿度；
 * 5. 持续轮询并输出中文诊断日志。
 *
 * 本头文件提供等价的 C++11 单例实现接口。真正的串口读写放在一个
 * 工作线程中执行，业务线程只需要调用 Init()/Start()/Status() 等接口，
 * 不会因为串口阻塞而被卡住。
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "MyGasDetectorData.h"
#include "MyGasDetectorPollConfig.h"
#include "MySerial.h"

namespace my_gas_detector_poll {

/**
 * @brief 轮询模块的生命周期状态。
 */
enum class PollState {
    Uninitialized,
    Disabled,
    Initialized,
    Running,
    Error
};

/**
 * @brief 气体检测仪 Modbus-RTU 轮询单例。
 *
 * 典型使用方式：
 *
 * @code
 * nlohmann::json config = {
 *     {"enabled", true},
 *     {"device", "/dev/ttyUSB0"},
 *     {"baud_rate", 9600},
 *     {"addresses", "1-6"}
 * };
 * std::string error;
 * auto& poll = my_gas_detector_poll::GasDetectorPoll::GetInstance();
 * poll.Init(config, &error);
 * poll.Start();
 * nlohmann::json status = poll.Status();
 * poll.Stop();
 * @endcode
 *
 * Init() 成功后只完成配置解析和串口打开，Start() 才创建工作线程。
 * Stop() 会通知线程退出、等待线程结束并关闭串口，因此析构时不会
 * 遗留后台线程或占用设备文件。
 */
class GasDetectorPoll {
public:
    /** @brief 获取进程内唯一的气体检测仪轮询实例。 */
    static GasDetectorPoll& GetInstance();

    GasDetectorPoll(const GasDetectorPoll&) = delete;
    GasDetectorPoll& operator=(const GasDetectorPoll&) = delete;
    GasDetectorPoll(GasDetectorPoll&&) = delete;
    GasDetectorPoll& operator=(GasDetectorPoll&&) = delete;

    /**
     * @brief 解析配置并打开串口。
     * @param config 轮询配置 JSON。
     * @param err 可选错误输出；成功时清空，失败时写入中文原因。
     * @return 配置和串口都准备成功返回 true；否则返回 false。
     *
     * 重复 Init() 会先安全停止旧的工作线程，再关闭旧串口并应用新配置。
     * 配置解析失败时不会影响当前已经运行的实例。
     */
    bool Init(const nlohmann::json& config, std::string* err = 0);

    /**
     * @brief 启动唯一工作线程，持续发送请求并解析返回帧。
     * @return 启动成功、已经运行或模块被禁用时返回 true。
     */
    bool Start();

    /**
     * @brief 停止工作线程并关闭串口。
     * @return 停止操作总是幂等，成功返回 true。
     */
    bool Stop();

    /**
     * @brief 获取当前运行状态和最近一次查询结果的 JSON 快照。
     * @return 快照不会暴露内部引用，调用方可以在任意线程安全使用。
     */
    nlohmann::json Status() const;

    nlohmann::json StatusSimpleCN() const;

    nlohmann::json StatusSimpleEN() const;

    /**
     * @brief 获取规范化后的当前配置。
     * @return 返回配置副本；不会返回内部对象引用。
     */
    nlohmann::json GetConfig() const;

    /** @brief 返回当前是否已经创建并运行工作线程。 */
    bool IsRunning() const;

    /** @brief 返回每个配置地址对应的最近一次结果副本。 */
    std::vector<GasDetectorData> GetLatestResults() const;

    /** @brief 计算 Modbus-RTU CRC-16，报文发送顺序为低字节在前。 */
    static std::uint16_t Crc16Modbus(const std::vector<std::uint8_t>& data);

    /**
     * @brief 构造功能码 03 的读保持寄存器请求帧。
     * @param address 从站地址。
     * @param start_register 起始寄存器地址，默认 0。
     * @param count 读取寄存器数量，默认 10。
     */
    static std::vector<std::uint8_t> BuildRequest(
        std::uint8_t address,
        std::uint16_t start_register = 0,
        std::uint16_t count = 10);

    /** @brief 将二进制帧格式化为大写十六进制文本，便于中文日志展示。 */
    static std::string HexFrame(const std::vector<std::uint8_t>& data);

private:
    GasDetectorPoll();
    ~GasDetectorPoll();

    static GasDetectorPollConfig ParseConfig(const nlohmann::json& config);
    static std::string StateToString(PollState state);
    static bool ParseResponse(const std::vector<std::uint8_t>& response,
                              std::uint8_t expected_address,
                              std::uint16_t expected_register_count,
                              double elapsed_ms,
                              GasDetectorData* result,
                              std::string* error);

    void WorkerLoop();
    void StopInternalLocked();
    bool WaitUntil(const std::chrono::steady_clock::time_point& deadline);
    bool DiscardInputBuffer();
    std::vector<std::uint8_t> ReadResponse(
        const std::chrono::steady_clock::time_point& deadline,
        std::string* error);
    std::vector<std::uint8_t> ReadUpTo(
        std::size_t size,
        const std::chrono::steady_clock::time_point& deadline,
        std::string* error);

    static GasDetectorData MakeFailureResult(int address,
                                             double elapsed_ms,
                                             const std::string& error,
                                             bool timeout);
    void StoreResult(const GasDetectorData& result,
                     const std::vector<std::uint8_t>& response);
    void LogSummary(bool print_log=false) const;

private:
    // lifecycle_mutex_ 只保护 Start/Stop/Init 之间的生命周期并发，避免
    // 在等待工作线程退出时阻塞工作线程需要使用的状态互斥锁。
    mutable std::mutex lifecycle_mutex_;
    mutable std::mutex mutex_;
    mutable std::mutex wait_mutex_;
    std::condition_variable wait_condition_;

    std::atomic<bool> stop_requested_;
    std::thread worker_;

    bool initialized_;
    bool running_;
    bool communication_ok_;
    bool debug_log_enabled_;
    bool simulate_mode_;
    PollState state_;
    GasDetectorPollConfig config_;
    nlohmann::json init_config_;
    std::vector<GasDetectorData> latest_results_;

    std::uint64_t request_count_;
    std::uint64_t success_count_;
    std::uint64_t timeout_count_;
    std::uint64_t invalid_frame_count_;
    std::uint64_t crc_error_count_;
    std::uint64_t cycle_count_;
    std::uint64_t next_sequence_;

    std::string last_error_;
    std::string last_frame_;

    my_serial::MySerial serial_;
};

// 兼容“Manager”命名习惯：两个名称指向完全相同的单例类型。
typedef GasDetectorPoll GasDetectorPollManager;

} // namespace my_gas_detector_poll
