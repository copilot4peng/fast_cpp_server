#include <unistd.h> // for access function
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <exception>
#include "FreeFunc.h"
#include "MyINIConfig.h"
#include "MyJSONConfig.h"
#include "MyJSONConfigV2.h"
#include "MyYAMLConfig.h"
#include "MyLog.h"

namespace tools {
namespace free_func {  


    void logWelcomeMessage() {
        MYLOG_INFO("==========================================");
        MYLOG_INFO("      Welcome to Fast C++ Server         ");
        MYLOG_INFO("        Version: 1.0.0                    ");
        MYLOG_INFO("==========================================");
    }
    
    
    bool loadLogConfigFromINIConfig(
        std::vector<std::string>& logInfos,
        std::string& logDirPath,
        std::string& logFilePath,
        std::string& appName) 
    {
        std::cout << "[LogConfig] 尝试从INI配置获取日志目录..." << std::endl;
        try {

            if (MyINIConfig::GetInstance().GetString("app_name", appName, appName)) {
                logInfos.emplace_back("[LogConfig] 从INI配置获取应用名称成功: " + appName);
            } else {
                logInfos.emplace_back("[LogConfig] 从INI配置获取应用名称失败，使用默认值: " + appName);
            } 
            if (MyINIConfig::GetInstance().GetString("logger_dir", logDirPath, logDirPath)) {
                logInfos.emplace_back("[LogConfig] 从INI配置获取日志目录成功: " + logDirPath);
            } else {
                logInfos.emplace_back("[LogConfig] 从INI配置获取日志目录失败，使用默认值: " + logDirPath);
            }
            logFilePath = logDirPath + appName + ".log";
            logInfos.emplace_back("[LogConfig] 从INI配置获取日志目录成功: " + logDirPath);
            logInfos.emplace_back("[LogConfig] 日志文件路径设置为: " + logFilePath);
            return true;
        } catch (const std::exception& e) {
            logInfos.emplace_back("[LogConfig] 从INI配置获取日志目录失败: " + std::string(e.what()));
        }
        return false;
    }
    
    bool checkConfigLoadStatus(bool load_ini_config_status, bool load_json_config_status, bool load_yaml_config_status) {
        bool loadNecessaryConfigSuccess = true;
        if (!load_ini_config_status)  { 
            MYLOG_ERROR("加载INI配置失败, 该配置必须成功加载");
            loadNecessaryConfigSuccess = false;
        } else {
            MYLOG_INFO("加载INI配置成功");
        }
        if (!load_json_config_status) {
            MYLOG_ERROR("加载JSON配置失败, 必须成功加载");    
            loadNecessaryConfigSuccess = false;
        } else {
            MYLOG_INFO("加载JSON配置成功");
        }
        if (!load_yaml_config_status) { 
            MYLOG_ERROR("加载YAML配置失败");    
        } else {
            MYLOG_INFO("加载YAML配置成功");
        }
        return loadNecessaryConfigSuccess;
    }
    
    bool showMyConfig(const std::string& type_) {
        std::string target_line = "======================================================================";
        if (type_ == "ini" || type_ == "INI") {
            MYLOG_INFO("INI Config:\n{}\n{}\{}", target_line, MyINIConfig::GetInstance().ShowConfig(), target_line);
            return true;
        } else if (type_ == "json" || type_ == "JSON") {
            MYLOG_INFO("JSON Config:\n{}\n{}\n{}", target_line, MyJSONConfig::GetInstance().ShowConfig(), target_line);
            return true;
        } else if (type_ == "jsonV2" || type_ == "JSONV2") {
            MYLOG_INFO("JSON V2 Config:\n{}\n{}\n{}", target_line, MyJSONConfigV2::GetInstance().ShowConfig(), target_line);
            return true;
        } else if (type_ == "yaml" || type_ == "YAML") {
            MYLOG_INFO("YAML Config:\n{}\n{}\n{}", target_line, MyYAMLConfig::GetInstance().ShowConfig(), target_line);
            return true;
        } else {
            MYLOG_ERROR("Unsupported config type: {}", type_);
            return false;
        }
    }

};
};