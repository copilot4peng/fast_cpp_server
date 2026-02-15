#pragma once

#include "BaseApiController.hpp"
#include "oatpp/web/server/api/ApiController.hpp"
#include "oatpp/core/macro/codegen.hpp"
#include "oatpp/core/macro/component.hpp"

namespace my_api::soft_healthy {

#include OATPP_CODEGEN_BEGIN(ApiController)

class SoftHealthyController : public base::BaseApiController {
public:
	explicit SoftHealthyController(const std::shared_ptr<ObjectMapper>& objectMapper);

	static std::shared_ptr<SoftHealthyController> createShared(const std::shared_ptr<ObjectMapper>& objectMapper);

	ENDPOINT_INFO(getSoftHealthyOnline) {
		info->summary = "软件健康检查接口";
		info->description = "检查服务是否存活，返回简单状态";
		info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
	}
	ENDPOINT("GET", "/v1/softhealthy/online", getSoftHealthyOnline);

	ENDPOINT_INFO(getSoftHealthyData) {
		info->summary = "获取软件健康快照";
		info->description = "返回当前软件健康快照的简要 JSON";
		info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
		info->addResponse<oatpp::String>(Status::CODE_204, "application/json");
	}
	ENDPOINT("GET", "/v1/softhealthy/getSoftHealthJsonData", getSoftHealthyData);

	ENDPOINT_INFO(refreshSoftHealthyNow) {
		info->summary = "立即触发一次软健康采样";
		info->description = "同步采样并返回结果";
		info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
	}
	ENDPOINT("GET", "/v1/softhealthy/refresh", refreshSoftHealthyNow);

	ENDPOINT_INFO(getSoftHealthyConfig) {
		info->summary = "获取/查看软健康监控配置";
		info->description = "返回当前 SoftHealthMonitor 的初始化/应用配置";
		info->addResponse<oatpp::String>(Status::CODE_200, "application/json");
	}
	ENDPOINT("GET", "/v1/softhealthy/config", getSoftHealthyConfig);

};

#include OATPP_CODEGEN_END(ApiController)

} // namespace my_api::soft_healthy

