# 空投锁模块设计文档与使用手册

## 1. 模块定位

空投锁模块用于统一管理空投锁设备的串口连接、初始化保护动作、开锁、关锁、占空比/频率设置和状态查询。

代码位置：

1. `src/util/my_airdrop_lock/my_airdrop_lock.h`
2. `src/util/my_airdrop_lock/my_airdrop_lock.cpp`
3. `src/util/my_airdrop_lock/CMakeLists.txt`
4. `src/util/my_api/controller/airdrop_lock/AirdropLockController.h`
5. `src/util/my_api/controller/airdrop_lock/AirdropLockController.cpp`
6. `src/util/my_api/dto/airdrop_lock/AirdropLockDto.hpp`

## 2. 设计目标

1. 使用单例管理空投锁，保证进程内只有一个设备状态入口。
2. 使用 JSON 初始化，和项目内其他模块保持一致。
3. 初始化后立即执行保护动作：先设置默认占空比/频率，再关闭空投锁。
4. REST API 暴露给前端，便于页面或上层服务调用。
5. 日志全部使用中文，便于现场排障。

## 3. 初始化参数

示例：

```json
{
  "enabled": true,
  "device": "/dev/ttyUSB0",
  "baud_rate": 9600,
  "data_bits": 8,
  "stop_bits": 1,
  "default_PWM_frequency": "50",
  "open_cmd": "D005",
  "lock_cmd": "D010"
}
```

字段说明：

1. `enabled`：是否启用空投锁。为 `false` 时不会打开串口，也不会下发命令。
2. `device`：串口设备路径，例如 `/dev/ttyUSB0`。
3. `baud_rate`：波特率，默认 `9600`。
4. `data_bits`：数据位，默认 `8`。
5. `stop_bits`：停止位，当前支持 `1` 或 `2`。
6. `default_PWM_frequency`：默认占空比/频率值。配置 `50` 时实际下发 ASCII 命令 `F050`。
7. `open_cmd`：开锁命令，默认 `D005`。
8. `lock_cmd`：关锁命令，默认 `D010`。

## 4. 初始化行为

调用 `AirdropLockManager::Init(config)` 后，模块会按以下顺序执行：

1. 校验 JSON 配置。
2. 初始化并打开串口。
3. 将 `default_PWM_frequency` 规范化为 `Fxxx` 命令，例如 `50` 转为 `F050`。
4. 下发默认占空比/频率命令。
5. 下发 `lock_cmd` 关闭空投锁。
6. 状态置为 `closed`。

这样可以保证设备上电或服务重启后，默认处于关闭锁状态，避免误开锁。

## 5. C++ 使用方式

```cpp
#include "my_airdrop_lock.h"

nlohmann::json config = {
    {"enabled", true},
    {"device", "/dev/ttyUSB0"},
    {"baud_rate", 9600},
    {"data_bits", 8},
    {"stop_bits", 1},
    {"default_PWM_frequency", "50"},
    {"open_cmd", "D005"},
    {"lock_cmd", "D010"}
};

std::string error;
auto& manager = my_airdrop_lock::AirdropLockManager::GetInstance();
if (!manager.Init(config, &error)) {
    MYLOG_ERROR("空投锁初始化失败: {}", error);
    return;
}

manager.SetDutyCycle("50", &error);
manager.OpenDropper(&error);
manager.CloseDropper(&error);
auto status = manager.GetStatus();
```

## 6. Pipeline 配置方式

在 `config.json` 的 `executes` 中添加：

```json
"airdrop_lock": {
  "model_name": "airdrop_lock",
  "enable": true,
  "model_args": {
    "enabled": true,
    "device": "/dev/ttyUSB0",
    "baud_rate": 9600,
    "data_bits": 8,
    "stop_bits": 1,
    "default_PWM_frequency": "50",
    "open_cmd": "D005",
    "lock_cmd": "D010"
  }
}
```

当 `rest_api` 启动时，`airdrop_lock` 也会被 `MyAPI` 识别为可加载 API 模块。

## 7. REST API

### 7.1 初始化

```bash
curl -X POST http://127.0.0.1:8080/v1/airdrop_lock/init \
  -H 'Content-Type: application/json' \
  -d '{
    "enabled": true,
    "device": "/dev/ttyUSB0",
    "baud_rate": 9600,
    "data_bits": 8,
    "stop_bits": 1,
    "default_PWM_frequency": "50",
    "open_cmd": "D005",
    "lock_cmd": "D010"
  }'
```

### 7.2 设置占空比/频率

```bash
curl -X POST http://127.0.0.1:8080/v1/airdrop_lock/duty_cycle \
  -H 'Content-Type: application/json' \
  -d '{"duty_cycle":"50"}'
```

也可以传入 `F050`，模块会统一规范化。

### 7.3 打开空投锁

```bash
curl -X POST http://127.0.0.1:8080/v1/airdrop_lock/open
```

### 7.4 关闭空投锁

```bash
curl -X POST http://127.0.0.1:8080/v1/airdrop_lock/close
```

### 7.5 获取状态

```bash
curl http://127.0.0.1:8080/v1/airdrop_lock/status
```

返回数据中的关键字段：

1. `initialized`：管理器是否初始化。
2. `enabled`：模块是否启用。
3. `state`：`uninitialized`、`disabled`、`closed`、`open` 或 `error`。
4. `serial_open`：串口是否打开。
5. `current_duty_cycle`：最近一次设置的占空比/频率输入值。
6. `last_command`：最近一次成功下发的 ASCII 命令。
7. `last_error`：最近一次错误信息。

## 8. 命令协议说明

当前模块按 ASCII 字符串下发命令：

1. 设置占空比/频率：`F050`。
2. 打开空投锁：`D005`。
3. 关闭空投锁：`D010`。

如果现场设备协议变化，只需要修改初始化 JSON 中的 `open_cmd` 和 `lock_cmd`。占空比/频率命令当前固定为 `Fxxx` 格式。

## 9. 排障建议

1. `serial_open=false`：检查设备路径是否存在、权限是否允许当前进程访问串口。
2. 初始化失败：查看日志中 `[空投锁] 初始化失败` 后面的中文错误。
3. API 返回 503：通常表示管理器未初始化、模块被禁用、串口未打开或命令发送失败。
4. 设备无动作：确认设备是否使用 ASCII 命令，确认 `open_cmd`、`lock_cmd` 是否与硬件协议一致。