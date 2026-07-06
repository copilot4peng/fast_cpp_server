// =============================================================================
// 文件：MyComm.cpp
// 模块：my_comm
// 说明：通信模块统一接入门面（Facade）的实现。
//
// 当前把 JSON 子模块（json_comm::JsonComm）接入系统；
// 后续如需接入 2536pb / HTTP，只要在此文件内追加对应委托即可。
// =============================================================================

#include "MyComm.h"
#include "JsonComm.h"
#include "CSY2536Comm.h"
#include "MyLog.h"

namespace my_comm {

namespace {
/**
 * @brief 从顶层配置里取出 JSON 子模块的配置节点。
 *
 * 兼容两种写法：
 *   1. 顶层含 "json" 键：{ "json": { ... } } —— 取内部节点；
 *   2. 直接就是子模块配置：{ "enable": ..., "topics": ... } —— 原样透传。
 */
const nlohmann::json PickJsonConfig(const nlohmann::json& config) {
    if (config.contains("json") && config["json"].is_object()) {
        return config["json"];
    }
    return config;
}
}  // namespace

MyComm& MyComm::GetInstance() {
    static MyComm instance;
    return instance;
}

bool MyComm::Init(const nlohmann::json& config) {
    MYLOG_INFO("【MyComm】开始初始化通信门面");

    // init json
    // init 2536

    nlohmann::json json_cfg = {};
    nlohmann::json csy2536_cfg = {};
    
    // try {
        MYLOG_INFO("【MyComm】解析 JSON 子模块配置");
        if (!json_comm::JsonComm::GetInstance().Init(config)) {
            MYLOG_ERROR("【MyComm】JSON 子模块初始化失败");
        } else {
            MYLOG_INFO("【MyComm】JSON 子模块初始化成功");
        }
    // } catch (const std::exception& e) {
    //     MYLOG_ERROR("【MyComm】JSON 子模块初始化异常: {}", e.what());
    //     return false;
    // }

    // try {
        MYLOG_INFO("【MyComm】解析 2536 子模块配置");
        if (!csy2536::CSY2536Comm::GetInstance().Initialize(config)) {
            MYLOG_ERROR("【MyComm】2536 子模块初始化失败");
        } else {
            MYLOG_INFO("【MyComm】2536 子模块初始化成功");
        }
    // } catch (const std::exception& e) {
    //     MYLOG_ERROR("【MyComm】2536 子模块初始化异常: {}", e.what());
    //     return false;
    // }

    MYLOG_INFO("【MyComm】通信门面初始化完成");
    return true;
}

bool MyComm::Start() {
    MYLOG_INFO("【MyComm】开始启动通信门面");
    
    // start json
    try {
        if (!json_comm::JsonComm::GetInstance().Start()) {
            MYLOG_ERROR("【MyComm】JSON 子模块启动失败");
        } else {
            MYLOG_INFO("【MyComm】JSON 子模块启动成功");
        }
    } catch (const std::exception& e) { 
        MYLOG_ERROR("【MyComm】JSON 子模块启动异常: {}", e.what());
        return false;
    }

    // start 2536
    try {
        if (!csy2536::CSY2536Comm::GetInstance().Start()) {
            MYLOG_ERROR("【MyComm】2536 子模块启动失败");
        } else {
            MYLOG_INFO("【MyComm】2536 子模块启动成功");
        }
    } catch (const std::exception& e) {
        MYLOG_ERROR("【MyComm】2536 子模块启动异常: {}", e.what());
        return false;
    }

    MYLOG_INFO("【MyComm】通信门面启动完成");
    return true;
}

void MyComm::Stop() {
    MYLOG_INFO("【MyComm】开始停止通信门面");
    json_comm::JsonComm::GetInstance().Stop();
    csy2536::CSY2536Comm::GetInstance().Stop();
    MYLOG_INFO("【MyComm】通信门面已停止");
}

nlohmann::json MyComm::Status() const {
    nlohmann::json status;
    // 聚合各子模块状态，键名与子模块一一对应，便于上层区分来源。
    status["json"] = json_comm::JsonComm::GetInstance().Status();
    return status;
}

}  // namespace my_comm
