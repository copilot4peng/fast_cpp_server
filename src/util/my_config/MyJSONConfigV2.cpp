#include "MyJSONConfigV2.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

MyJSONConfigV2* MyJSONConfigV2::instance_ = nullptr;
std::once_flag MyJSONConfigV2::init_flag_;

void MyJSONConfigV2::Init(const std::string& path) {
    // 加载失败时抛出异常，call_once 不会完成初始化，后续可以重新尝试。
    std::call_once(init_flag_, [&]() {
        std::unique_ptr<MyJSONConfigV2> instance(new MyJSONConfigV2());
        if (!instance->Load(path)) {
            throw std::runtime_error("V2 JSON 配置加载失败: " + path);
        }
        instance_ = instance.release();
    });
}

MyJSONConfigV2& MyJSONConfigV2::GetInstance() {
    // 未初始化时返回空对象，便于启动日志和测试代码安全查询。
    static MyJSONConfigV2 empty_instance;
    return instance_ == nullptr ? empty_instance : *instance_;
}

bool MyJSONConfigV2::Load(const std::string& path) {
    try {
        const std::filesystem::path index_path = std::filesystem::absolute(path);
        std::ifstream index_file(index_path);
        if (!index_file.is_open()) {
            std::cerr << "[MyJSONConfigV2] 无法打开索引文件: " << index_path << std::endl;
            return false;
        }

        nlohmann::json loaded_config;
        index_file >> loaded_config;

        if (!loaded_config.is_object() || !loaded_config.contains("pipeline")) {
            std::cerr << "[MyJSONConfigV2] 索引文件缺少 pipeline 配置: " << index_path << std::endl;
            return false;
        }

        auto& pipeline = loaded_config["pipeline"];
        if (!pipeline.is_object() || !pipeline.contains("executes") ||
            !pipeline["executes"].is_object()) {
            std::cerr << "[MyJSONConfigV2] 索引文件中的 pipeline.executes 格式错误" << std::endl;
            return false;
        }

        const std::filesystem::path config_directory = index_path.parent_path();
        auto& executes = pipeline["executes"];

        // 每个节点文件本身就是旧版的完整节点对象，读取后直接替换索引节点。
        for (auto& item : executes.items()) {
            auto& index_item = item.value();
            if (!index_item.is_object()) {
                std::cerr << "[MyJSONConfigV2] Pipeline 节点不是对象，序号: "
                          << item.key() << std::endl;
                return false;
            }

            const bool read_config = index_item.value("read_config", true);
            const std::string config_name = index_item.value("config_name", item.key());
            if (!read_config) {
                // 不读取的节点明确禁用，避免索引字段被 Pipeline 当作业务配置使用。
                index_item = {
                    {"model_name", config_name},
                    {"model_args", nlohmann::json::object()},
                    {"enable", false}
                };
                std::cerr << "[MyJSONConfigV2] 节点配置标记为不读取，已禁用: "
                          << config_name << std::endl;
                continue;
            }

            const std::string config_path = index_item.value("config_path", "");
            if (config_path.empty()) {
                std::cerr << "[MyJSONConfigV2] 节点缺少 config_path: " << config_name << std::endl;
                return false;
            }

            const std::filesystem::path item_path =
                std::filesystem::path(config_path).is_absolute()
                    ? std::filesystem::path(config_path)
                    : config_directory / config_path;

            std::ifstream item_file(item_path);
            if (!item_file.is_open()) {
                std::cerr << "[MyJSONConfigV2] 无法打开节点配置文件: " << item_path << std::endl;
                return false;
            }

            nlohmann::json item_config;
            item_file >> item_config;
            if (!item_config.is_object()) {
                std::cerr << "[MyJSONConfigV2] 节点配置根节点不是对象: " << item_path << std::endl;
                return false;
            }

            index_item = std::move(item_config);
            std::cerr << "[MyJSONConfigV2] 节点加载成功: " << config_name
                      << " -> " << item_path << std::endl;
        }

        config_ = std::move(loaded_config);
        std::cerr << "[MyJSONConfigV2] 配置聚合完成: " << index_path << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[MyJSONConfigV2] 读取配置异常: " << e.what() << std::endl;
        return false;
    }
}

bool MyJSONConfigV2::Get(const std::string& key,
                         const nlohmann::json& default_value,
                         nlohmann::json& out) const {
    try {
        const auto it = config_.find(key);
        if (it == config_.end()) {
            out = default_value;
            return false;
        }
        out = *it;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[MyJSONConfigV2] 读取配置项异常: " << key
                  << ", " << e.what() << std::endl;
        out = default_value;
        return false;
    }
}

const nlohmann::json& MyJSONConfigV2::Raw() const {
    return config_;
}

std::string MyJSONConfigV2::ShowConfig() const {
    return config_.dump(2);
}

nlohmann::json& MyJSONConfigV2::GetMutableConfig() {
    return config_;
}