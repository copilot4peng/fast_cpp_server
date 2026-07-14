#include "MyGasDetectorData.h"
#include "MyGasDetectorPollInternal.h"

namespace my_gas_detector_poll {

using namespace detail;


GasDetectorData::GasDetectorData()
    : address(0),
      valid(false),
      timeout(false),
      crc_ok(false),
      function_code(0),
      byte_count(0),
      exception_code(0),
      crc_received(0),
      crc_calculated(0),
      elapsed_ms(0.0),
      definition_register(0),
      unit("未知"),
      decimal_places(0),
      concentration_register(0),
      concentration(0.0),
      concentration_text("0"),
      low_alarm_register(0),
      low_alarm(0.0),
      low_alarm_text("0"),
      high_alarm_register(0),
      high_alarm(0.0),
      high_alarm_text("0"),
      range_register(0),
      range_value(0.0),
      range_text("0"),
      status_register(0),
      status("未查询"),
      ad_register(0),
      ad_value(0),
      temperature_register(0),
      temperature(0.0),
      gas_code(0),
      gas("NULL"),
      gas_register(0),
      humidity_register(0),
      humidity(0.0),
      timestamp(),
      error("尚未查询"),
      sequence(0) {
}

nlohmann::json GasDetectorData::ToJson() const {
    return nlohmann::json{
        {"address", address},
        {"valid", valid},
        {"timeout", timeout},
        {"crc_ok", crc_ok},
        {"function_code", function_code},
        {"byte_count", byte_count},
        {"exception_code", exception_code},
        {"crc_received", crc_received},
        {"crc_calculated", crc_calculated},
        {"elapsed_ms", elapsed_ms},
        {"definition_register", definition_register},
        {"unit", unit},
        {"decimal_places", decimal_places},
        {"concentration_register", concentration_register},
        {"concentration", concentration},
        {"concentration_text", concentration_text},
        {"low_alarm_register", low_alarm_register},
        {"low_alarm", low_alarm},
        {"low_alarm_text", low_alarm_text},
        {"high_alarm_register", high_alarm_register},
        {"high_alarm", high_alarm},
        {"high_alarm_text", high_alarm_text},
        {"range_register", range_register},
        {"range_value", range_value},
        {"range_text", range_text},
        {"status_register", status_register},
        {"status_code", status_register},
        {"status", status},
        {"ad_register", ad_register},
        {"ad_value", ad_value},
        {"temperature_register", temperature_register},
        {"temperature", temperature},
        {"gas_code", gas_code},
        {"gas", gas},
        {"gas_register", gas_register},
        {"humidity_register", humidity_register},
        {"humidity", humidity},
        {"timestamp", timestamp},
        {"error", error},
        {"sequence", sequence}
    };
}

};