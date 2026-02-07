#include "mavsdk_utils.h"

#include <mavsdk/mavsdk.h>

#include <chrono>
#include <thread>
#include "MyLog.h"

using namespace mavsdk;
using namespace std::chrono;

bool wait_mavsdk_heartbeat(
    const std::string& connection_url,
    uint8_t& out_system_id,
    uint8_t& out_component_id,
    int timeout_sec
)
{
    // ✅ 正确：ComponentType 在 mavsdk 命名空间
    Mavsdk mavsdk(
        Mavsdk::Configuration(
            mavsdk::ComponentType::GroundStation
        )
    );

    const ConnectionResult conn_result =
        mavsdk.add_any_connection(connection_url);

    if (conn_result != ConnectionResult::Success) {
        return false;
    }

    const auto start_time = steady_clock::now();

    while (true) {
        const auto& systems = mavsdk.systems();

        if (!systems.empty()) {
            auto system = systems.front();

            out_system_id = system->get_system_id();

            const auto component_ids = system->component_ids();
            out_component_id =
                component_ids.empty() ? 0 : component_ids.front();

            return true;
        }

        if (timeout_sec >= 0) {
            auto elapsed =
                duration_cast<seconds>(steady_clock::now() - start_time);
            if (elapsed.count() >= timeout_sec) {
                return false;
            }
        }
        MYLOG_INFO("Waiting for link {} heartbeat...", connection_url);
        std::this_thread::sleep_for(milliseconds(200));
    }
}
