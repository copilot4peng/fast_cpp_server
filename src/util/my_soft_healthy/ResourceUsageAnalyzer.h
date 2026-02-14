#pragma once
// 资源使用分析器（计算 CPU%、IO 速率）
// 将 prev snapshot 与 curr snapshot 做 diff，并把结果写回 curr snapshot（非破坏 prev）
#include "SoftHealthSnapshot.h"
#include <memory>

namespace MySoftHealthy {
class ResourceUsageAnalyzer {
public:
  ResourceUsageAnalyzer() = default;
  ~ResourceUsageAnalyzer() = default;

  // 计算差分并填充 curr（要求 prev 可能为空（首次采样））
  // interval_seconds 用于计算 io 速率（若需要）
  void compute_deltas(const std::shared_ptr<SoftHealthSnapshot>& prev,
                      const std::shared_ptr<SoftHealthSnapshot>& curr,
                      int interval_seconds);
};

} // namespace MySoftHealthy