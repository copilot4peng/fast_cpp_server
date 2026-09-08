#include "MyJSONConfigV1.h"
#include <iostream>
#include <fstream>

MyJSONConfigV1* MyJSONConfigV1::instance_ = nullptr;
std::once_flag MyJSONConfigV1::init_flag_;

void MyJSONConfigV1::Init(const std::string& path) {
    std::call_once(init_flag_, [&]() {
        instance_ = new MyJSONConfigV1();
        instance_->Load(path);
    });
}

MyJSONConfigV1& MyJSONConfigV1::GetInstance() {
    return *instance_;
}

bool MyJSONConfigV1::Load(const std::string& path) {
    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return false;
        ifs >> config_;
        return true;
    } catch (...) {
        return false;
    }
}

bool MyJSONConfigV1::Get(const std::string& key,
                       const nlohmann::json& def,
                       nlohmann::json& out) const {
    try {
        if (config_.is_null()) {
            out = def;
            return false;
        }
        out = config_[key];
    } catch (...) {
        std::cout << "[MyJSONConfigV1] Get key exception: " << key << std::endl;
        out = def;
        return false;
    }
    return true;
}

const nlohmann::json& MyJSONConfigV1::Raw() const {
    return config_;
}

std::string MyJSONConfigV1::ShowConfig() const {
    return config_.dump(2);
}

nlohmann::json& MyJSONConfigV1::GetMutableConfig() {
    return config_;
}