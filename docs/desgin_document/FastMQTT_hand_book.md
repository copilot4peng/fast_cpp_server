# FastMQTT 使用手册

**版本：** V1.0
**模块：** `my_fast_MQTT`（传输层） + `my_csy2536_protocol`（协议层）

---

## 1. 快速开始

### 1.1 编译集成

工程默认已开启构建开关（见 [src/CMakeLists.txt](../../src/CMakeLists.txt)）：

```cmake
set(BUILD_MY_FAST_MQTT        ON CACHE BOOL "Build my_fast_MQTT library")
set(BUILD_MY_CSY2536_PROTOCOL ON CACHE BOOL "Build my_csy2536_protocol library")
add_subdirectory(util/my_fast_MQTT)
add_subdirectory(util/my_csy2536_protocol)
```

在你的目标中链接：

```cmake
# 仅需纯字节收发（协议无关）
target_link_libraries(your_app PRIVATE my_fast_MQTT)

# 需要收发 CS-Y2536 的 MsgInfo（含 protobuf）
target_link_libraries(your_app PRIVATE my_csy2536_protocol)
```

### 1.2 头文件

```cpp
#include "FastMQTT.hpp"       // 传输层
#include "CSY2536Codec.hpp"   // 协议层（可选）
```

---

## 2. 传输层用法（协议无关）

### 2.1 初始化与启动

```cpp
#include "FastMQTT.hpp"
#include <nlohmann/json.hpp>

using fast_mqtt::FastMQTT;

nlohmann::json cfg = {
    {"mqtt", {
        {"enable", true},
        {"broker", {
            {"host", "127.0.0.1"},
            {"port", 1883},
            {"client_id", "launcher_001"},
            {"keep_alive", 60},
            {"auto_reconnect", true}
        }},
        {"thread", {{"send_queue_size", 1000}, {"recv_queue_size", 1000}}},
        {"default", {{"qos", 1}, {"retain", false}}}
    }}
};

auto& mqtt = FastMQTT::GetInstance();
mqtt.Initialize(cfg);   // 只能成功一次
mqtt.Start();           // 只能成功一次，启动四个后台线程
```

### 2.2 订阅与回调

```cpp
// 一个 Topic 可注册多个回调，返回句柄用于注销
uint64_t h = mqtt.RegisterCallback("/status", [](const fast_mqtt::Message& msg){
    // 注意：回调在 Dispatcher 线程执行，避免长时间阻塞
    printf("收到 Topic=%s 长度=%zu\n", msg.topic.c_str(), msg.payload.size());
}, /*qos=*/1);

// 支持通配符
mqtt.RegisterCallback("sensor/+/temp", [](const fast_mqtt::Message&){ /*...*/ });

// 注销
mqtt.UnregisterCallback(h);
mqtt.ClearCallback("/status");
mqtt.ClearAllCallbacks();
```

### 2.3 发布

```cpp
mqtt.Publish("/heartbeat", "ping");            // 使用默认 QoS / retain
mqtt.Publish("/heartbeat", "ping", 1);         // 指定 QoS
mqtt.Publish("/heartbeat", "ping", 1, false);  // 指定 QoS 与 retain
```

> 业务线程调用 `Publish` 只是**入队**，真正发送由 Sender 线程完成，不会阻塞业务。

### 2.4 状态查询

```cpp
if (mqtt.IsReady()) { /* 可进行业务通信 */ }
bool connected = mqtt.IsConnected();
bool ip_ok     = mqtt.IsIPAlive();

nlohmann::json health = mqtt.GetHealthStatus();   // Heartbeat 模块直接读取
nlohmann::json stats  = mqtt.GetStatistics();
size_t sq = mqtt.GetSendQueueSize();
size_t rq = mqtt.GetReceiveQueueSize();
```

### 2.5 停止与销毁

```cpp
mqtt.Stop();     // 安全退出所有线程，幂等
mqtt.Destroy();  // 释放资源，状态回到 Uninitialized
```

---

## 3. 协议层用法（收发 CS-Y2536 的 MsgInfo）

协议层让你直接以 protobuf 对象收发，无需手动序列化：

```cpp
#include "CSY2536Codec.hpp"

using csy2536::CSY2536Codec;

// 3.1 发布一个 MsgInfo
CSY2536::MsgInfo info;
info.set_send_id(100);
info.set_seq(1);
// info.mutable_heartbeat()-> ... 填充具体 oneof 字段
CSY2536Codec::Publish("agent/heartbeat", info);          // 默认 QoS
CSY2536Codec::Publish("agent/heartbeat", info, 1, false);

// 3.2 订阅并自动解析为 MsgInfo
uint64_t h = CSY2536Codec::Subscribe("agent/#",
    [](const std::string& topic, const CSY2536::MsgInfo& msg){
        // 已解析好的 protobuf，解析失败的字节会被自动丢弃并记录日志
        printf("send_id=%u seq=%u\n", msg.send_id(), msg.seq());
    }, /*qos=*/1);

// 注销（复用 FastMQTT 的句柄机制）
fast_mqtt::FastMQTT::GetInstance().UnregisterCallback(h);
```

> **协议与模块分离**：`FastMQTT` 本身不认识 protobuf；`CSY2536Codec`
> 只是把 `SerializeToString` / `ParseFromString` 与 FastMQTT 的收发桥接起来。
> 如需接入其它协议，只需新增一个类似的协议层，无需改动 FastMQTT。

---

## 4. 完整最小示例

```cpp
#include "FastMQTT.hpp"
#include "CSY2536Codec.hpp"
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

int main() {
    auto& mqtt = fast_mqtt::FastMQTT::GetInstance();

    nlohmann::json cfg = {{"mqtt", {{"enable", true},
        {"broker", {{"host", "127.0.0.1"}, {"port", 1883}}}}}};

    if (!mqtt.Initialize(cfg)) return 1;

    csy2536::CSY2536Codec::Subscribe("agent/#",
        [](const std::string& t, const CSY2536::MsgInfo& m){
            printf("[%s] send_id=%u\n", t.c_str(), m.send_id());
        });

    mqtt.Start();

    for (int i = 0; i < 5; ++i) {
        CSY2536::MsgInfo info;
        info.set_send_id(100);
        info.set_seq(i);
        csy2536::CSY2536Codec::Publish("agent/heartbeat", info);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    mqtt.Stop();
    mqtt.Destroy();
    return 0;
}
```

---

## 5. 常见问题（FAQ）

**Q：`Publish` 返回 false？**
A：可能是模块未 `Start`（非 Running 状态）或发送队列已满。检查
`GetHealthStatus()` 中的 `state` 与 `send_queue_size`、`send_dropped`。

**Q：回调没有被触发？**
A：确认已 `RegisterCallback` 且连接成功；重连后会自动重新订阅。检查主题
通配符是否匹配，以及 `recv_dropped` 是否在增长（接收队列满）。

**Q：可以在回调里做耗时操作吗？**
A：不建议。回调运行在 Dispatcher 线程，耗时操作会拖慢整体派发；请将耗时
任务转交业务线程池处理。

**Q：能连续 `Initialize` 两次吗？**
A：不能。`Initialize` 只能成功一次，重复调用返回 false。需重来请先 `Destroy`。

---

## 6. 相关文件

- 传输层：[FastMQTT.hpp](../../src/util/my_fast_MQTT/FastMQTT.hpp)、[FastMQTT.cpp](../../src/util/my_fast_MQTT/FastMQTT.cpp)
- 队列：[BlockingQueue.hpp](../../src/util/my_fast_MQTT/BlockingQueue.hpp)
- 类型：[FastMQTTTypes.hpp](../../src/util/my_fast_MQTT/FastMQTTTypes.hpp)
- 协议层：[CSY2536Codec.hpp](../../src/util/my_csy2536_protocol/CSY2536Codec.hpp)、[CSY2536Codec.cpp](../../src/util/my_csy2536_protocol/CSY2536Codec.cpp)
- 测试：[TestFastMQTT.cpp](../../test/util/my_fast_MQTT/TestFastMQTT.cpp)、[TestBlockingQueue.cpp](../../test/util/my_fast_MQTT/TestBlockingQueue.cpp)
- 设计手册：[FastMQTT_design_document.md](FastMQTT_design_document.md)
