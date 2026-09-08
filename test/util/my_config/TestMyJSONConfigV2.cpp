#include "MyJSONConfig.h"
#include "MyJSONConfigV2.h"
#include "gtest/gtest.h"

#include <filesystem>

namespace {

std::filesystem::path FindProjectRoot() {
    // 测试可能从项目根目录、build 目录或 build/bin 目录启动，逐级向上查找配置目录。
    std::filesystem::path current = std::filesystem::current_path();
    while (!current.empty()) {
        if (std::filesystem::exists(current / "config/config.json") &&
            std::filesystem::exists(current / "config/v2/config.json")) {
            return current;
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return {};
}

}  // namespace

TEST(MyJSONConfigV2Test, V1AndV2PipelineOutputAreEqual) {
    const std::filesystem::path project_root = FindProjectRoot();
    ASSERT_FALSE(project_root.empty()) << "找不到项目根目录配置文件";

    const std::filesystem::path v1_path = project_root / "config/config.json";
    const std::filesystem::path v2_path = project_root / "config/v2/config.json";

    // V1 和 V2 各自只初始化一次，后面直接读取最终快照进行比较。
    MyJSONConfig::Init(v1_path.string());
    MyJSONConfigV2::Init(v2_path.string());

    const auto& v1 = MyJSONConfig::GetInstance().Raw();
    const auto& v2 = MyJSONConfigV2::GetInstance().Raw();

    // A 和 pipeline1 是 V1 中的示例配置，V2 索引没有声明它们，因此不属于本次聚合结果。
    // 去掉这两个旧示例后，完整比较两版最终 JSON，避免只比较几个字段而漏掉差异。
    nlohmann::json v1_runtime_config = v1;
    v1_runtime_config.erase("A");
    v1_runtime_config.erase("pipeline1");
    EXPECT_EQ(v1_runtime_config, v2);
}
