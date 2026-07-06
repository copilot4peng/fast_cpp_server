# IP 管理模块设计文档

## 1. 模块概述

本模块提供工业级的 Linux 系统 IP 地址管理能力，包含底层工具库 `MyIPTools` 和上层 RESTful API 接口 `MyIPController`。

**核心特性：**
- 基于 Linux **rtnetlink** 实现 IP 添加/删除，绕过 `system()` 调用，具备更好的安全性和性能
- 完整的 CIDR 格式校验与参数合法性检查
- 明确区分错误类型：非法格式、无权限、网卡不存在、IP 冲突等
- 符合 RESTful 规范的 API 设计，集成 Swagger 文档自动生成
- 全中文日志输出

## 2. 架构设计

```
┌─────────────────────────────────────────────┐
│               前端 / Swagger UI              │
│         (JSON 请求 ↔ JSON 响应)              │
└────────────────┬────────────────────────────┘
                 │ HTTP
┌────────────────▼────────────────────────────┐
│          MyIPController (oatpp)              │
│  - GET  /v1/ip/interfaces                   │
│  - GET  /v1/ip/addresses                    │
│  - POST /v1/ip/add                          │
│  - POST /v1/ip/delete                       │
├─────────────────────────────────────────────┤
│            IPModifyRequestDto               │
│  (interface_name, cidr)                     │
└────────────────┬────────────────────────────┘
                 │ 调用
┌────────────────▼────────────────────────────┐
│             MyIPTools (静态方法)              │
│  - GetAllInterfaces()                       │
│  - GetAllIPs()                              │
│  - AddIP() / DeleteIP()                     │
│  - IsValidCIDR() / InterfaceExists()        │
├─────────────────────────────────────────────┤
│          Linux 内核接口                      │
│  - getifaddrs()    (查询)                   │
│  - rtnetlink       (修改)                   │
│  - if_nametoindex  (网卡校验)               │
└─────────────────────────────────────────────┘
```

## 3. 核心数据结构

### 3.1 IPInfo

```cpp
struct IPInfo {
    std::string interface;  // 网卡名，如 "eth0"
    std::string ip;         // IP 地址，如 "192.168.1.100"
    int prefix_len = 0;     // CIDR 前缀长度，如 24
};
```

### 3.2 IPResult

```cpp
struct IPResult {
    IPErrorCode code;       // 错误码枚举
    std::string message;    // 中文说明信息
};
```

### 3.3 IPErrorCode 错误码

| 错误码                | 值  | 含义                      |
|-----------------------|-----|---------------------------|
| kSuccess              | 0   | 操作成功                  |
| kInvalidCIDR          | 1   | 非法的 CIDR 格式          |
| kInterfaceNotFound    | 2   | 指定的网卡不存在          |
| kPermissionDenied     | 3   | 无 Root 权限              |
| kSystemError          | 4   | 底层系统调用失败          |
| kIPAlreadyExists      | 5   | IP 已存在于目标网卡       |
| kIPNotFound           | 6   | 目标网卡上未找到该 IP     |

## 4. 底层实现方案

### 4.1 查询操作

使用 POSIX 标准的 `getifaddrs()` 遍历系统网络接口：

1. **GetAllInterfaces()**: 遍历 `ifaddrs` 链表，过滤 `IFF_UP` 且非 `IFF_LOOPBACK` 的接口，去重后返回
2. **GetAllIPs()**: 遍历 `ifaddrs` 链表，筛选 `AF_INET` 族地址，解析子网掩码计算前缀长度

### 4.2 修改操作

使用 Linux `rtnetlink` (Netlink Route) 协议：

1. 创建 `AF_NETLINK` 类型的 `SOCK_DGRAM` socket
2. 构建 `nlmsghdr` + `ifaddrmsg` 请求消息
3. 添加 `IFA_LOCAL` 和 `IFA_ADDRESS` 属性
4. 发送请求并等待内核 ACK 响应
5. 解析 `NLMSG_ERROR` 判断成功/失败及具体错误原因

**选择 rtnetlink 而非 `system("ip addr add ...")` 的原因：**
- 避免命令注入安全风险
- 无需依赖外部命令行工具
- 可精确获取内核返回的错误码
- 性能更优（无 fork/exec 开销）

### 4.3 参数校验流程

```
AddIP(interface, cidr)
  │
  ├── ParseCIDR(cidr) ─── 失败 → 返回 kInvalidCIDR
  │
  ├── InterfaceExists(interface) ─── 失败 → 返回 kInterfaceNotFound
  │
  └── ModifyIP(interface, ip, prefix, true)
        │
        ├── socket() 失败 + EPERM → 返回 kPermissionDenied
        │
        ├── send() 请求
        │
        └── recv() ACK
              ├── error=0 → 返回 kSuccess
              ├── EEXIST  → 返回 kIPAlreadyExists
              ├── EPERM   → 返回 kPermissionDenied
              └── 其他    → 返回 kSystemError
```

## 5. API 设计

### 5.1 统一响应格式

```json
{
  "success": true,
  "code": 200,
  "message": "获取网卡列表成功",
  "data": [...]
}
```

错误响应：
```json
{
  "success": false,
  "code": 400,
  "message": "非法的 CIDR 格式: abc/99"
}
```

### 5.2 接口列表

| 方法 | 路径                | 说明                                |
|------|---------------------|-------------------------------------|
| GET  | /v1/ip/interfaces   | 获取所有可用网卡名称                |
| GET  | /v1/ip/addresses    | 获取所有 IPv4 地址及网卡映射        |
| POST | /v1/ip/add          | 向指定网卡添加 CIDR 格式 IP         |
| POST | /v1/ip/delete       | 从指定网卡删除 CIDR 格式 IP         |
| GET  | /v1/ip/scan         | 扫描局域网内所有活跃设备            |
| POST | /v1/ip/scan/config  | 配置扫描器参数（线程数、超时）      |

## 6. 局域网活跃设备探测

### 6.1 概述

基于原生 ICMP Raw Socket 实现局域网活跃设备探测，手动构造 RFC 792 标准的 ICMP Echo Request 报文并解析 Echo Reply。

### 6.2 ICMP 报文构造

#### ICMPHeader 结构（RFC 792）

```
 0       7 8     15 16                31
+--------+--------+------------------+
|  type  |  code  |    checksum      |
+--------+--------+------------------+
|       id        |    sequence      |
+-----------------+------------------+
```

```cpp
struct ICMPHeader {
    uint8_t  type;        // Echo Request=8, Echo Reply=0
    uint8_t  code;        // 固定为 0
    uint16_t checksum;    // RFC 1071 校验和
    uint16_t id;          // 进程 PID 低 16 位
    uint16_t sequence;    // 序列号
};
```

#### Echo Request 构造流程

1. 填充 `type=8, code=0, checksum=0, id=getpid()&0xFFFF, sequence=1`
2. 调用 `ComputeICMPChecksum()` 计算校验和并回填 `checksum` 字段
3. 通过 `sendto()` 发送至目标 IP

#### 校验和算法（RFC 1071）

```
1. 将数据视为 16-bit 字序列
2. 逐字相加（32-bit 累加器）
3. 处理末尾奇数字节
4. 将高 16-bit 进位折叠加回低 16-bit
5. 取反得到校验和
```

#### Echo Reply 解析

1. 接收数据包含 IP 头部（通常 20 字节）+ ICMP 头部
2. 从 IP 头部第一个字节的低 4 位（IHL 字段）计算实际 IP 头长度
3. 跳过 IP 头部后读取 ICMP 头部
4. 验证 `type=0`（Reply）且 `id` 与发送时匹配

### 6.3 多线程并发方案

#### 线程模型：即时线程 + 信号量控制

```
ScanActiveDevices()
  │
  ├── 权限校验: HasRawSocketPermission()
  │     └── 检测 geteuid()==0 或尝试创建 SOCK_RAW
  │
  ├── 获取候选 IP 列表:
  │     GetAllIPs() → GenerateSubnetIPs() → 去重、排除本机
  │
  └── 并发扫描:
        │
        │  std::counting_semaphore<1024> sem(max_threads)
        │
        │  for each candidate_ip:
        │    sem.acquire()          // 阻塞等待空闲槽位
        │    std::thread([&] {
        │      PingHost(ip, timeout)
        │      if (alive) → 加锁写入结果
        │      sem.release()        // 释放槽位
        │    })
        │
        └── join 所有线程 → 排序结果 → 返回
```

#### 关键设计决策

1. **禁止线程池**：遵循需求，每个 IP 启动独立 `std::thread`
2. **信号量限流**：`std::counting_semaphore` 限制同时运行的线程数（默认 64）
3. **线程安全**：活跃 IP 写入使用 `std::mutex` 保护
4. **资源释放**：所有线程在扫描结束后通过 `join()` 确保回收
5. **进度汇报**：每扫描 50 个 IP 或扫描完成时输出日志

#### 超时控制

- 每个 ICMP raw socket 通过 `SO_RCVTIMEO` 设置接收超时
- 默认 800ms，可通过 `InitScanner()` 或 API 配置
- 超时后 `recvfrom()` 返回 -1，线程自然结束

### 6.4 网段扫描逻辑

```
GenerateSubnetIPs(ip="192.168.1.10", prefix_len=24)
  │
  ├── 计算子网掩码: mask = 0xFFFFFF00
  ├── 网络地址:     192.168.1.0
  ├── 广播地址:     192.168.1.255
  ├── 首个主机IP:   192.168.1.1
  ├── 最后主机IP:   192.168.1.254
  └── 生成 254 个候选 IP（排除网络地址和广播地址）
```

**安全限制：** 前缀长度必须在 [16, 30] 范围内：
- `< 16`：网段过大（>65K 地址），防止资源耗尽
- `> 30`：网段过小（如 /31 点对点链路），无意义

### 6.5 ScanResult 数据结构

```cpp
struct ScanResult {
    int total_scanned;                // 总扫描 IP 数
    int active_count;                 // 活跃设备数
    std::vector<std::string> active_ips;  // 活跃 IP 列表（已排序）
    double scan_duration_ms;          // 扫描耗时（毫秒）
    std::string error;                // 错误信息（空=无错误）
};
```

### 6.6 权限要求

创建 `SOCK_RAW` + `IPPROTO_ICMP` 套接字需要：
- **方式一**：以 Root 身份运行
- **方式二**：通过 `setcap` 赋予可执行文件 `CAP_NET_RAW` 能力

```bash
# 赋予 CAP_NET_RAW 能力（无需 Root 运行）
sudo setcap cap_net_raw+ep ./bin/fast_cpp_server
sudo setcap cap_net_raw+ep ./bin/fast_cpp_server_Test
```

## 7. 文件结构

```
src/util/my_tools/
  ├── MyIPTools.h          # 底层库头文件（IPInfo, IPResult, IPErrorCode, ICMPHeader, ScanResult, MyIPTools）
  └── MyIPTools.cpp        # 底层库实现（getifaddrs + rtnetlink + ICMP 扫描）

src/util/my_api/
  ├── dto/ip/IPDto.hpp     # 请求 DTO（IPModifyRequestDto, ScanConfigRequestDto）
  └── controller/ip/
      ├── MyIPController.h   # 控制器头文件（Swagger 注解）
      └── MyIPController.cpp # 控制器实现

test/util/my_tools/
  └── TestMyIPTools.cpp    # GTest 单元测试

docs/desgin_document/
  └── IP_Tools_Design.md   # 本文档
```

## 8. 安全考量

- **命令注入防护**：使用 rtnetlink 系统调用而非 `system()` ，从根本上杜绝命令注入
- **ICMP 注入防护**：手动构造 ICMP 报文，使用 PID 作为 ID 字段校验响应合法性
- **权限检查**：`HasRawSocketPermission()` 在扫描前主动探测权限，避免大量线程创建后才发现无权限
- **资源限制**：信号量上限 1024 线程，前缀长度限制 [16,30]，防止扫描 /8 级大网段导致资源耗尽
- **fd 泄漏防护**：每个 `PingHost()` 调用在任何退出路径上都确保关闭 raw socket
- **输入校验**：所有外部输入经过 `ParseCIDR()` + `InterfaceExists()` 严格校验后才执行操作


# IP 管理模块使用手册

## 1. 编译

本模块已集成到项目的 CMake 构建系统中，无需额外配置。

```bash
# 标准编译（包含 IP 管理模块）
cd /home/cs/DockerRoot/fast_cpp_server
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

模块依赖以下 CMake 构建标志（默认已开启）：
- `BUILD_MY_TOOLS` — 编译底层 `my_tools` 库（含 `MyIPTools`）
- `BUILD_MY_API` — 编译 API 层（含 `MyIPController`）

## 2. 底层库使用 (MyIPTools)

### 2.1 头文件引入

```cpp
#include "MyIPTools.h"
```

### 2.2 获取所有网卡

```cpp
auto interfaces = my_tools::MyIPTools::GetAllInterfaces();
for (const auto& iface : interfaces) {
    std::cout << "网卡: " << iface << std::endl;
}
// 输出示例：
// 网卡: eth0
// 网卡: wlan0
```

### 2.3 获取所有 IP 地址

```cpp
auto ips = my_tools::MyIPTools::GetAllIPs();
for (const auto& info : ips) {
    std::cout << info.interface << ": "
              << info.ip << "/" << info.prefix_len << std::endl;
}
// 输出示例：
// eth0: 192.168.1.10/24
// docker0: 172.17.0.1/16
```

### 2.4 添加 IP 地址

```cpp
// 需要 Root 权限
auto result = my_tools::MyIPTools::AddIP("eth0", "192.168.1.100/24");
if (result.code == my_tools::IPErrorCode::kSuccess) {
    std::cout << "添加成功: " << result.message << std::endl;
} else {
    std::cerr << "添加失败: " << result.message << std::endl;
}
```

### 2.5 删除 IP 地址

```cpp
// 需要 Root 权限
auto result = my_tools::MyIPTools::DeleteIP("eth0", "192.168.1.100/24");
if (result.code == my_tools::IPErrorCode::kSuccess) {
    std::cout << "删除成功" << std::endl;
}
```

### 2.6 校验 CIDR 格式

```cpp
bool valid = my_tools::MyIPTools::IsValidCIDR("192.168.1.100/24"); // true
bool invalid = my_tools::MyIPTools::IsValidCIDR("abc/99");          // false
```

### 2.7 错误码处理

```cpp
auto result = my_tools::MyIPTools::AddIP("eth0", "10.0.0.1/8");
switch (result.code) {
    case my_tools::IPErrorCode::kSuccess:
        // 操作成功
        break;
    case my_tools::IPErrorCode::kInvalidCIDR:
        // CIDR 格式非法
        break;
    case my_tools::IPErrorCode::kInterfaceNotFound:
        // 网卡不存在
        break;
    case my_tools::IPErrorCode::kPermissionDenied:
        // 需要 Root 权限
        break;
    case my_tools::IPErrorCode::kIPAlreadyExists:
        // IP 已存在
        break;
    case my_tools::IPErrorCode::kIPNotFound:
        // IP 未找到（删除时）
        break;
    case my_tools::IPErrorCode::kSystemError:
        // 系统调用出错
        break;
}
```

## 3. REST API 使用

### 3.1 获取所有网卡

```bash
curl -X GET http://localhost:8080/v1/ip/interfaces
```

响应示例：
```json
{
  "success": true,
  "code": 200,
  "message": "获取网卡列表成功",
  "data": ["eth0", "docker0", "wlan0"]
}
```

### 3.2 获取所有 IP 地址

```bash
curl -X GET http://localhost:8080/v1/ip/addresses
```

响应示例：
```json
{
  "success": true,
  "code": 200,
  "message": "获取 IP 地址列表成功",
  "data": [
    {"interface": "eth0", "ip": "192.168.1.10", "prefix_len": 24},
    {"interface": "docker0", "ip": "172.17.0.1", "prefix_len": 16}
  ]
}
```

### 3.3 添加 IP 地址

```bash
curl -X POST http://localhost:8080/v1/ip/add \
  -H "Content-Type: application/json" \
  -d '{"interface_name": "eth0", "cidr": "192.168.1.100/24"}'
```

成功响应：
```json
{
  "success": true,
  "code": 200,
  "message": "成功添加 IP: 192.168.1.100/24"
}
```

错误响应（无权限）：
```json
{
  "success": false,
  "code": 3,
  "message": "无 Root 权限，操作被拒绝"
}
```

### 3.4 删除 IP 地址

```bash
curl -X POST http://localhost:8080/v1/ip/delete \
  -H "Content-Type: application/json" \
  -d '{"interface_name": "eth0", "cidr": "192.168.1.100/24"}'
```

### 3.5 配置扫描参数

```bash
curl -X POST http://localhost:8080/v1/ip/scan/config \
  -H "Content-Type: application/json" \
  -d '{"max_threads": 128, "timeout_ms": 500}'
```

响应：
```json
{
  "success": true,
  "code": 200,
  "message": "扫描参数配置成功",
  "data": {"max_threads": 128, "timeout_ms": 500}
}
```

### 3.6 扫描局域网活跃设备

```bash
curl -X GET http://localhost:8080/v1/ip/scan
```

响应：
```json
{
  "success": true,
  "code": 200,
  "message": "局域网扫描完成",
  "data": {
    "total_scanned": 254,
    "active_count": 12,
    "scan_duration_ms": 3456.7,
    "active_ips": [
      "192.168.1.1",
      "192.168.1.5",
      "192.168.1.100"
    ]
  }
}
```

权限不足时的错误响应：
```json
{
  "success": false,
  "code": 403,
  "message": "无 CAP_NET_RAW 权限或非 Root 用户，无法创建原始套接字执行 ICMP 扫描"
}
```

### 3.7 Swagger 文档

启动服务后访问 Swagger UI 查看完整 API 文档：

```
http://localhost:8080/swagger/ui
```

所有接口附带中文描述、请求体示例和多种响应码说明。

## 4. 运行测试

```bash
cd build
# 运行全部测试
./bin/fast_cpp_server_Test

# 仅运行 IP 模块测试
./bin/fast_cpp_server_Test --gtest_filter="MyIPToolsTest.*"
```

**注意：** 部分测试需要 Root 权限才能执行实际的 IP 添加/删除操作，非 Root 环境下此类测试会自动跳过（`GTEST_SKIP`）。

## 5. 注意事项

1. **Root 权限**：`AddIP` 和 `DeleteIP` 操作修改系统网络配置，必须以 Root 身份运行
2. **CIDR 格式**：所有 IP 参数必须包含前缀长度，如 `192.168.1.100/24`，不接受裸 IP
3. **持久化**：通过 rtnetlink 添加的 IP 在系统重启后会丢失，如需持久化请配合 netplan 或 `/etc/network/interfaces`
4. **并发安全**：`MyIPTools` 所有方法均为无状态的静态方法，天然线程安全
5. **ICMP 扫描权限**：`ScanActiveDevices` 需要 `CAP_NET_RAW` 或 Root，可通过 `setcap cap_net_raw+ep <binary>` 赋权
6. **扫描范围限制**：仅扫描前缀长度在 [16, 30] 范围内的网段，避免 /8 级大网段导致资源耗尽
