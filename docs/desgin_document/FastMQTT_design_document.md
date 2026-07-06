# FastMQTT 模块设计手册

**版本：** V1.0
**语言标准：** C++11（兼容 C++17 工程）
**所属工程：** fast_cpp_server
**模块路径：** `src/util/my_fast_MQTT/`（传输层）、`src/util/my_csy2536_protocol/`（协议层）

---

## 一、设计目标

FastMQTT 是整个系统**唯一**的 MQTT 通信模块，负责：

- 与 MQTT Broker 建立连接、断线自动重连；
- MQTT 消息发送 / 接收；
- Topic 回调管理；
- 线程安全消息队列；
- 连接状态监控与健康状态统计；
- 为 Heartbeat 模块提供状态信息。

**FastMQTT 不负责：**

- protobuf 编解码（由 `my_csy2536_protocol` 协议层负责）；
- 业务逻辑、Topic 内容解释、Controller 调度。

> **协议与模块分离**：`FastMQTT` 只搬运字节，完全不认识 protobuf；
> CS-Y2536 的 `MsgInfo` 序列化 / 反序列化由独立的协议层 `CSY2536Codec` 完成。

业务层仅需：

```cpp
fast_mqtt::FastMQTT::GetInstance().Publish(...);
fast_mqtt::FastMQTT::GetInstance().RegisterCallback(...);
fast_mqtt::FastMQTT::GetInstance().IsReady();
```

整个程序仅允许存在**一个** FastMQTT 实例（单例）。

---

## 二、总体架构

```
                    +----------------------+
                    |      Business        |
                    +----------+-----------+
                               |
                    +----------v-----------+
                    |      FastMQTT        |
                    +----------+-----------+
        +----------------------+---------------------+
        |                      |                     |
+-------v------+      +--------v------+     +--------v-------+
| Connection   |      |  Receiver     |     |    Sender      |
|  Manager     |      |    Thread     |     |    Thread      |
+-------+------+      +--------+------+     +--------+-------+
        |                      |                     |
        |              Receive Queue          Send Queue
        |                      |                     |
        +-----------+----------+---------------------+
                    |
            Dispatcher Thread
                    |
            Callback Manager
                    |
              Business Callback
```

---

## 三、生命周期

```
Uninitialized → Initialized → Starting → Running → Stopping → Stopped
```

- `Initialize()` 只能成功调用一次；
- `Start()` 只能成功调用一次；
- `Stop()` 可安全退出所有线程，且幂等；
- `Destroy()` 释放资源并回到 `Uninitialized`。

状态由 `std::atomic<LifecycleState>` 维护，见
[FastMQTTTypes.hpp](../../src/util/my_fast_MQTT/FastMQTTTypes.hpp)。

---

## 四、配置文件

采用 JSON 初始化，支持传入 `mqtt` 节点或含 `mqtt` 键的顶层对象：

```json
{
    "mqtt": {
        "enable": true,
        "broker": {
            "host": "127.0.0.1",
            "port": 1883,
            "client_id": "launcher_001",
            "username": "",
            "password": "",
            "keep_alive": 60,
            "clean_session": true,
            "auto_reconnect": true,
            "connect_timeout_ms": 5000
        },
        "thread": {
            "send_queue_size": 1000,
            "recv_queue_size": 1000
        },
        "default": {
            "qos": 1,
            "retain": false
        }
    }
}
```

所有字段均有默认值，缺失时使用默认值，保证健壮性。

---

## 五、单例模式

```cpp
fast_mqtt::FastMQTT& mqtt = fast_mqtt::FastMQTT::GetInstance();
```

- 使用函数内 `static` 局部变量实现线程安全的懒汉单例；
- 禁止拷贝 / 移动构造与赋值；
- 禁止重复初始化。

---

## 六、线程设计（四个后台线程）

| 线程                        | 职责                                                                                          | 是否触碰 mosquitto 网络 |
| --------------------------- | --------------------------------------------------------------------------------------------- | ----------------------- |
| **ConnectionManager** | 通过 TCP connect 探测目标 IP 连通性，聚合并更新状态                                           | 否                      |
| **Receiver**          | **网络所有权线程**：负责 connect / `mosquitto_loop` / reconnect；收到消息压入接收队列 | 是（唯一）              |
| **Dispatcher**        | 消费接收队列，按 Topic 匹配并派发业务回调                                                     | 否                      |
| **Sender**            | 消费发送队列，统一执行`mosquitto_publish`                                                   | 仅 publish              |

> **线程安全关键约定**：mosquitto 句柄的“网络状态”（连接、循环、重连）
> **只由 Receiver 线程操作**，从而避免多线程竞争。`ConnectionManager`
> 只做 TCP 连通性探测，不直接触碰句柄网络状态。`mosquitto_publish`
> 允许在网络循环运行时从 Sender 线程调用，并额外用 `pub_mutex_` 保护。

### Receiver 主循环

```
发起 connect / reconnect
  ├─ 失败 → 指数退避等待 → 重试
  └─ 成功 → 进入 mosquitto_loop 网络循环
              └─ 循环返回错误（断开）→ 指数退避等待 → 重连
```

---

## 七、线程同步

- 所有队列均为阻塞队列（`BlockingQueue`），基于
  `std::mutex` + `std::condition_variable`，**禁止 Busy Loop**；
- 队列支持 `Push / Pop / Pop(timeout) / Shutdown / Clear / Size`；
- 停止时调用 `Shutdown()` 唤醒阻塞线程，保证可安全 `join`。

---

## 八、状态设计

维护四个独立状态，而非单一 `connected`：

| 状态                  | 含义                                              |
| --------------------- | ------------------------------------------------- |
| `ip_alive`          | 目标主机 IP 是否连通（由 ConnectionManager 探测） |
| `broker_connected`  | Broker（TCP + CONNACK）是否已连接                 |
| `session_connected` | MQTT 会话是否建立                                 |
| `ready`             | 是否真正可进行业务通信（`enable && 已连接`）    |

---

## 九、消息模型

发送与接收共用统一结构 `Message`：

```cpp
struct Message {
    std::string  topic;      // 主题
    std::string  payload;    // 负载（二进制安全，可容纳 protobuf 字节）
    int          qos;        // QoS
    bool         retain;     // retain
    int64_t      timestamp;  // 发送=入队时间；接收=收到时间
};
```

- 发送：业务线程 `Publish()` → **SendQueue** → Sender 线程 → `mosquitto_publish`；
- 接收：Receiver 线程 → **ReceiveQueue** → Dispatcher 线程 → 业务回调。

业务线程**永远不会**直接调用 `mosquitto_publish`，避免阻塞网络线程。

---

## 十、Callback 管理

- 一个 Topic 支持注册**多个**回调（`map<string, vector<CallbackEntry>>`）；
- 支持 `RegisterCallback / UnregisterCallback / ClearCallback / ClearAllCallbacks`；
- 每个回调有唯一句柄，便于精确注销；
- 派发时**拷贝命中回调再执行**，避免持锁执行业务；
- 单个回调失败不影响其它回调（各自 `try/catch` 包裹）；
- 连接 / 重连成功后自动重新订阅所有已注册主题（`ResubscribeAll`）。
- Topic 通配符匹配使用 mosquitto 官方 `mosquitto_topic_matches_sub`。

---

## 十一、Reconnect 策略

采用指数退避，避免 Broker 异常时 CPU 持续 100%：

```
1s → 2s → 5s → 10s → 20s → 30s → 30s → ...
```

连接成功后退避档位重置为 0。

---

## 十二、消息队列容量

- SendQueue / ReceiveQueue 均有最大长度限制；
- 超过容量时 `Push` 直接失败，记录日志并计入 `send_dropped` / `recv_dropped`，避免 OOM。

---

## 十三、日志设计

统一使用工程日志宏（`MYLOG_INFO / WARN / ERROR / DEBUG`），日志全部中文，例如：

```
【MQTT】初始化成功
【MQTT】开始连接 Broker
【MQTT】Broker连接成功
【MQTT】Broker连接断开
【MQTT】开始自动重连
【MQTT】发送队列已满，消息被丢弃
【MQTT】MQTT模块停止完成
```

---

## 十四、健康状态

`GetHealthStatus()` 返回 JSON，供 Heartbeat 模块直接读取（无需 MQTT 主动上报）：

```json
{
    "enable": true,
    "state": "Running",
    "ready": true,
    "broker_connected": true,
    "ip_alive": true,
    "reconnect_count": 2,
    "last_connect_time": 1751234567,
    "last_send_time": 1751234570,
    "last_recv_time": 1751234571,
    "send_queue_size": 0,
    "recv_queue_size": 2,
    "send_success": 123,
    "send_failed": 1,
    "recv_count": 89,
    "callback_failed": 0
}
```

---

## 十五、对外接口

| 分类     | 接口                                                                                |
| -------- | ----------------------------------------------------------------------------------- |
| 生命周期 | `Initialize / Start / Stop / Destroy`                                             |
| 发布     | `Publish(topic,payload[,qos[,retain]])`                                           |
| 订阅     | `RegisterCallback / UnregisterCallback / ClearCallback / ClearAllCallbacks`       |
| 状态     | `IsReady / IsConnected / IsIPAlive / IsEnabled / GetStatistics / GetHealthStatus` |
| 队列     | `GetSendQueueSize / GetReceiveQueueSize`                                          |

---

## 十六、异常处理原则

四个后台线程主循环均以

```cpp
try { ... }
catch (const std::exception& e) { ... }
catch (...) { ... }
```

包裹，**禁止线程因异常退出**，避免整个 MQTT 功能失效。

---

## 十七、模块依赖关系

`FastMQTT` 传输层**仅**依赖：`MyLog`、Mosquitto、`BlockingQueue`、`nlohmann::json`。
**不依赖**：HTTP / Controller / 业务模块 / protobuf / 数据库 / UI。

协议层 `my_csy2536_protocol` 依赖 `myproto` + `my_fast_MQTT`，实现 protobuf 桥接，
从而将协议与传输层彻底解耦。

---

## 十八、设计原则

- **单一职责**：连接、发送、接收、派发、回调各自独立；
- **线程隔离**：网络 IO 与业务处理完全解耦；
- **队列解耦**：所有收发均经阻塞队列；
- **生命周期明确**：不允许重复初始化；
- **高可观测**：状态 / 统计 / 队列长度均可查询；
- **高可靠**：线程异常保护 + 自动重连 + 队列容量限制；
- **低耦合**：不依赖业务对象与 protobuf，便于协议替换与扩展。

---

## 十九、未来扩展方向（接口预留，本版不实现）

1. TLS/SSL 安全连接；
2. MQTT v5 特性；
3. 离线消息缓存；
4. 消息优先级队列；
5. 消息过滤器（Filter/拦截器）；
6. 运行时动态订阅 / 取消订阅；
7. 线程池 Dispatcher；
8. 统一通信抽象接口（MessageBus，支持 WebSocket / DDS / TCP 等实现）。
