#pragma once
#include <string>
#include <mutex>
#include <nlohmann/json.hpp>

class MyJSONConfigV1 {
public:
    static void Init(const std::string& path);
    static MyJSONConfigV1& GetInstance();

    bool Get(const std::string& key,
             const nlohmann::json& def,
             nlohmann::json& out) const;

    const nlohmann::json& Raw() const;
    std::string ShowConfig() const;

    nlohmann::json& GetMutableConfig();

private:
    MyJSONConfigV1() = default;
    bool Load(const std::string& path);

    static MyJSONConfigV1* instance_;
    static std::once_flag init_flag_;

    nlohmann::json config_;
};
