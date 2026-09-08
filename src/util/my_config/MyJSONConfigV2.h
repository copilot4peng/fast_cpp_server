#pragma once
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>

class MyJSONConfigV2 {
public:
    // 读取 V2 索引文件，并加载索引中 pipeline.executes 指向的配置文件。
    static void Init(const std::string& path);

    static MyJSONConfigV2& GetInstance();

    // 与 MyJSONConfig 保持相同的访问方式，方便旧调用方逐步迁移。
    bool Get(const std::string& key, const nlohmann::json& default_value, nlohmann::json& out) const;

    const nlohmann::json& Raw() const;
    std::string ShowConfig() const;

    nlohmann::json& GetMutableConfig();
private:
    MyJSONConfigV2() = default;

    bool Load(const std::string& path);

    static MyJSONConfigV2* instance_;
    static std::once_flag init_flag_;

    nlohmann::json config_;
};
