#pragma once

// =============================================================================
// 文件：MyComm.h
// 模块：my_comm
// 说明：通信模块统一接入门面（Facade）。
//
// 设计定位：
//   - my_comm 是系统通信抽象层的顶层入口；
//   - 底层按“传输协议 + 报文格式”拆分子模块：
//       · mqtt/2536pb : CS-Y2536 protobuf 协议接入（csy2536::CSY2536Comm）
//       · mqtt/json   : 通用 JSON 报文接入（json_comm::JsonComm）
//   - MyComm 负责把这些子模块统一收口，向系统上层暴露一致的
//     Init / Start / Status 生命周期接口。
//
// 当前版本：
//   - 顶层门面先接入 JSON 子模块（JsonComm）；
//   - 后续如需接入 2536pb 或 HTTP，只需在本类内追加委托即可，
//     上层调用方式保持不变。
// =============================================================================

#include <nlohmann/json.hpp>

namespace my_comm {

/**
 * @brief 通信模块统一门面（单例）。
 *
 * 使用方式：
 *   1. 先确保底层 FastMQTT 已经 Initialize + Start；
 *   2. MyComm::GetInstance().Init(config);
 *   3. MyComm::GetInstance().Start();
 *   4. 需要时通过 Status() 获取聚合状态。
 */
class MyComm final {
public:
    /// @brief 获取全局唯一实例（懒汉式单例，线程安全）。
    static MyComm& GetInstance();

    MyComm(const MyComm&) = delete;
    MyComm& operator=(const MyComm&) = delete;
    MyComm(MyComm&&) = delete;
    MyComm& operator=(MyComm&&) = delete;

    /**
     * @brief 初始化通信门面及其下属子模块。
     *
     * 约定配置格式（缺省字段全部走默认值）：
     * @code
     * {
     *   "json": {
     *     "enable": true,
     *     "topics": ["/status", "test"],
     *     "default_qos": 1
     *   }
     * }
     * @endcode
     * 若配置中不含 "json" 节点，则把整个 config 直接透传给 JsonComm。
     *
     * @param config 通信模块配置。
     * @return true 初始化成功；false 存在子模块初始化失败。
     */
    bool Init(const nlohmann::json& config = nlohmann::json::object());

    /**
     * @brief 启动通信门面下属所有子模块。
     * @return true 全部启动成功；false 存在子模块启动失败。
     */
    bool Start();

    /**
     * @brief 停止通信门面下属所有子模块。
     */
    void Stop();

    /**
     * @brief 获取聚合状态（包含各子模块状态）。
     * @return 形如 { "json": { ... } } 的 JSON。
     */
    nlohmann::json Status() const;

private:
    MyComm() = default;
    ~MyComm() { Stop(); };
};

}  // namespace my_comm
