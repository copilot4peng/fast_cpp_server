#pragma once

#include <cstdint>
#include <string>

/**
 * @brief Wait for MAVLink heartbeat using MAVSDK
 *
 * @param connection_url  e.g. "udp://0.0.0.0:14550"
 * @param out_system_id   returned MAVLink system id
 * @param out_component_id returned MAVLink component id
 * @param timeout_sec     <0: block forever; >=0: timeout in seconds
 *
 * @return true if heartbeat received, false on timeout or error
 */
bool wait_mavsdk_heartbeat(
    const std::string& connection_url,
    uint8_t& out_system_id,
    uint8_t& out_component_id,
    int timeout_sec = -1
);
