#include "MyGasDetectorPollConfig.h"
#include "MyGasDetectorPollInternal.h"

namespace my_gas_detector_poll {

using namespace detail;

GasDetectorPollConfig::GasDetectorPollConfig()
    : enabled(true),
      device("/dev/ttyUSB0"),
      baud_rate(9600),
      data_bits(8),
      stop_bits(1),
      parity("none"),
      flowcontrol("none"),
      addresses(),
      interval_ms(500),
      timeout_ms(400),
      start_register(0),
      register_count(10),
      read_slice_timeout_ms(kDefaultReadSliceTimeoutMs),
      log_raw_frame(true),
      debug_log_enabled(false),
      simulate_mode(false) {
    for (int address = 1; address <= 6; ++address) {
        addresses.push_back(static_cast<std::uint8_t>(address));
    }
}

nlohmann::json GasDetectorPollConfig::ToJson() const {
    nlohmann::json address_array = nlohmann::json::array();
    for (std::vector<std::uint8_t>::const_iterator it = addresses.begin();
         it != addresses.end(); ++it) {
        address_array.push_back(static_cast<int>(*it));
    }

    return nlohmann::json{
        {"enabled", enabled},
        {"device", device},
        {"baud_rate", baud_rate},
        {"data_bits", data_bits},
        {"stop_bits", stop_bits},
        {"parity", parity},
        {"flowcontrol", flowcontrol},
        {"addresses", address_array},
        {"interval_ms", interval_ms},
        {"timeout_ms", timeout_ms},
        {"start_register", start_register},
        {"register_count", register_count},
        {"read_slice_timeout_ms", read_slice_timeout_ms},
        {"log_raw_frame", log_raw_frame},
        {"debug_log_enabled", debug_log_enabled},
        {"simulate_mode", simulate_mode}
    };
}
};