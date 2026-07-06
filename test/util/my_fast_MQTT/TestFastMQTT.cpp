// =============================================================================
// 文件：TestFastMQTT.cpp
// 说明：FastMQTT 单例通信模块单元测试。
//
// 注意：
//   - FastMQTT 为进程级单例，且 Initialize 只能成功一次。
//   - 这些测试不依赖真实 Broker：仅验证配置解析、单例、生命周期状态机、
//     未运行时的接口行为与健康状态结构。
//   - 涉及生命周期的用例统一在末尾以 Destroy() 复位，避免污染其它用例。
// =============================================================================

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "FastMQTT.hpp"
#include "FastMQTTTypes.hpp"

using fast_mqtt::FastMQTT;
using fast_mqtt::FastMQTTConfig;
using json = nlohmann::json;

// -------------------- 配置解析 --------------------

// 从含 mqtt 键的顶层对象解析。
TEST(FastMQTTConfigTest, ParseFromTopLevel) {
    json j = {
        {"mqtt",
         {{"enable", true},
          {"broker",
           {{"host", "192.168.1.10"},
            {"port", 1884},
            {"client_id", "launcher_001"},
            {"keep_alive", 30},
            {"auto_reconnect", false}}},
          {"thread", {{"send_queue_size", 500}, {"recv_queue_size", 800}}},
          {"default", {{"qos", 2}, {"retain", true}}}}}};

    auto cfg = FastMQTTConfig::FromJson(j);
    EXPECT_TRUE(cfg.enable);
    EXPECT_EQ(cfg.broker.host, "192.168.1.10");
    EXPECT_EQ(cfg.broker.port, 1884);
    EXPECT_EQ(cfg.broker.client_id, "launcher_001");
    EXPECT_EQ(cfg.broker.keep_alive, 30);
    EXPECT_FALSE(cfg.broker.auto_reconnect);
    EXPECT_EQ(cfg.thread.send_queue_size, 500u);
    EXPECT_EQ(cfg.thread.recv_queue_size, 800u);
    EXPECT_EQ(cfg.def.qos, 2);
    EXPECT_TRUE(cfg.def.retain);
}

// 直接传入 mqtt 节点内容也应可解析。
TEST(FastMQTTConfigTest, ParseFromInnerObject) {
    json j = {{"enable", false}, {"broker", {{"host", "10.0.0.1"}}}};
    auto cfg = FastMQTTConfig::FromJson(j);
    EXPECT_FALSE(cfg.enable);
    EXPECT_EQ(cfg.broker.host, "10.0.0.1");
}

// 缺失字段应使用默认值，保证健壮性。
TEST(FastMQTTConfigTest, DefaultsWhenMissing) {
    json j = json::object();
    auto cfg = FastMQTTConfig::FromJson(j);
    EXPECT_TRUE(cfg.enable);
    EXPECT_EQ(cfg.broker.host, "127.0.0.1");
    EXPECT_EQ(cfg.broker.port, 1883);
    EXPECT_EQ(cfg.broker.keep_alive, 60);
    EXPECT_TRUE(cfg.broker.clean_session);
    EXPECT_TRUE(cfg.broker.auto_reconnect);
    EXPECT_EQ(cfg.def.qos, 1);
}

// -------------------- 单例 --------------------

TEST(FastMQTTTest, SingletonIdentity) {
    auto& a = FastMQTT::GetInstance();
    auto& b = FastMQTT::GetInstance();
    EXPECT_EQ(&a, &b);
}

// -------------------- 未运行时的接口行为 --------------------

TEST(FastMQTTTest, PublishBeforeStartFails) {
    auto& m = FastMQTT::GetInstance();
    // 未初始化/未运行时发布应失败，不崩溃。
    EXPECT_FALSE(m.Publish("/test", "hello"));
    EXPECT_FALSE(m.IsReady());
    EXPECT_FALSE(m.IsConnected());
}

TEST(FastMQTTTest, HealthStatusHasExpectedKeys) {
    auto& m = FastMQTT::GetInstance();
    json h = m.GetHealthStatus();
    // 健康状态应包含关键字段，供 Heartbeat 模块读取。
    EXPECT_TRUE(h.contains("enable"));
    EXPECT_TRUE(h.contains("ready"));
    EXPECT_TRUE(h.contains("broker_connected"));
    EXPECT_TRUE(h.contains("ip_alive"));
    EXPECT_TRUE(h.contains("reconnect_count"));
    EXPECT_TRUE(h.contains("send_queue_size"));
    EXPECT_TRUE(h.contains("recv_queue_size"));
    EXPECT_TRUE(h.contains("send_success"));
    EXPECT_TRUE(h.contains("recv_count"));
    EXPECT_TRUE(h.contains("callback_failed"));
}

TEST(FastMQTTTest, StatisticsHasExpectedKeys) {
    auto& m = FastMQTT::GetInstance();
    json s = m.GetStatistics();
    EXPECT_TRUE(s.contains("send_success"));
    EXPECT_TRUE(s.contains("send_failed"));
    EXPECT_TRUE(s.contains("recv_count"));
    EXPECT_TRUE(s.contains("callback_failed"));
}

// -------------------- 生命周期（enable=false 路径，不依赖 Broker） --------------------

// enable=false 时可初始化但 Start 返回 false；随后 Destroy 复位。
TEST(FastMQTTTest, LifecycleDisabled) {
    auto& m = FastMQTT::GetInstance();
    json j = {{"mqtt", {{"enable", false}, {"broker", {{"host", "127.0.0.1"}}}}}};

    ASSERT_TRUE(m.Initialize(j));
    EXPECT_FALSE(m.IsEnabled());
    // 未启用时 Start 应返回 false。
    EXPECT_FALSE(m.Start());

    // 重复初始化应被拒绝。
    EXPECT_FALSE(m.Initialize(j));

    // 复位，避免污染其它用例。
    m.Destroy();
    EXPECT_FALSE(m.IsReady());
}
