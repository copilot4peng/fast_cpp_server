#pragma once

#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/core/Types.hpp"
#include "demo/Task.h"

namespace my_api::dto {
#include OATPP_CODEGEN_BEGIN(DTO)


class TaskDataDto : public oatpp::DTO {
    DTO_INIT(TaskDataDto, DTO)

    // identity
    DTO_FIELD(String, taskId);
    DTO_FIELD(String, commandId);

    // tracing
    DTO_FIELD(String, traceId);
    DTO_FIELD(String, spanId);

    // routing
    DTO_FIELD(String, edgeId);
    DTO_FIELD(String, deviceId);

    // capability & action
    DTO_FIELD(String, capability);
    DTO_FIELD(String, action);
    // params/output/policy stored as JSON strings (nlohmann::json serialized)
    DTO_FIELD(String, paramsJson);
    DTO_FIELD(String, policyJson);
    DTO_FIELD(String, resultJson);

    // idempotency (reserved)
    DTO_FIELD(String, idempotencyKey);
    DTO_FIELD(Int64, dedupWindowMs);

    // scheduling (reserved)
    DTO_FIELD(Int32, priority);
    DTO_FIELD(Int64, createdAtMs);
    DTO_FIELD(Int64, deadlineAtMs);

    // runtime
    // represent enum state as integer
    DTO_FIELD(Int32, state);

    void toTask(my_data::Task& task) const {
        task.task_id = taskId ? taskId->c_str() : "";
        task.command_id = commandId ? commandId->c_str() : "";
        task.trace_id = traceId ? traceId->c_str() : "";
        task.span_id = spanId ? spanId->c_str() : "";
        task.edge_id = edgeId ? edgeId->c_str() : "";
        task.device_id = deviceId ? deviceId->c_str() : "";
        task.capability = capability ? capability->c_str() : "";
        task.action = action ? action->c_str() : "";
        if (paramsJson && !paramsJson->empty()) {
          task.params = nlohmann::json::parse(paramsJson->c_str());
        } else {
          task.params = nlohmann::json::object();
        }
        task.idempotency_key = idempotencyKey ? idempotencyKey->c_str() : "";
        task.dedup_window_ms = dedupWindowMs ? *dedupWindowMs : 0;
        task.priority = priority ? *priority : 0;
        task.created_at_ms = createdAtMs ? *createdAtMs : 0;
        task.deadline_at_ms = deadlineAtMs ? *deadlineAtMs : 0;   
        task.state = state ? static_cast<my_data::TaskState>(*state) : my_data::TaskState::Pending;
        if (policyJson && !policyJson->empty()) {
            task.policy = nlohmann::json::parse(policyJson->c_str());
        } else {
            task.policy = nlohmann::json::object();
        }
        if (resultJson && !resultJson->empty()) {
            task.result = my_data::TaskResult::fromJson(nlohmann::json::parse(resultJson->c_str()));
        } else {
            task.result = my_data::TaskResult{};
        }
    }
};

/**
 * TaskDto
 * - edgeId: 必填，目标边缘设备 ID
 * - taskType: 可选，任务类型
 * - paramsJson: 可选，任务参数（JSON 字符串），用于传递任意可变结构
 *
 * 说明：为了兼顾 Swagger 文档与可扩展性，采用了固定字段 + 可变 JSON 字符串的折衷方案。
 */
class TaskDto : public oatpp::DTO {
  DTO_INIT(TaskDto, DTO)

  DTO_FIELD(String, edgeId);
  DTO_FIELD(String, taskType);
  // 可变参数以 JSON 字符串形式传递，业务层再解析为 nlohmann::json
  DTO_FIELD(String, paramsJson);
  DTO_FIELD(Object<TaskDataDto>, taskData);
};

#include OATPP_CODEGEN_END(DTO)
} // namespace my_api::dto