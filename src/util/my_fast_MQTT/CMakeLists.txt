cmake_minimum_required(VERSION 3.10)

# =============================================================================
# FastMQTT 核心通信模块（协议无关）
#
# 说明：
#   本模块仅负责 MQTT 传输层，严格不依赖 protobuf / 业务类型 / 数据库 / HTTP。
#   协议编解码（CS-Y2536）由独立的 my_csy2536_protocol 模块负责，实现“协议与模块分离”。
# =============================================================================

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (BUILD_MY_FAST_MQTT)
    print_colored_message("------------------------------" COLOR magenta)
    print_colored_message("Building my_fast_MQTT library..." COLOR yellow)

    set(MY_FAST_MQTT_INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/src/util/my_fast_MQTT)
    file(GLOB_RECURSE MY_FAST_MQTT_SOURCES ${PROJECT_SOURCE_DIR}/src/util/my_fast_MQTT/*.cpp)

    pretty_print_list("MY_FAST_MQTT_SOURCES List" MY_FAST_MQTT_SOURCES)

    add_library(my_fast_MQTT STATIC ${MY_FAST_MQTT_SOURCES})
    target_include_directories(my_fast_MQTT PUBLIC ${MY_FAST_MQTT_INCLUDE_DIRECTORIES})

    # 优先使用工程内的 libmosquitto_static target（不依赖系统开发包）。
    if (TARGET libmosquitto_static)
        target_link_libraries(my_fast_MQTT PUBLIC
            libmosquitto_static
            pthread
            mylog
            myconfig
            nlohmann_json::nlohmann_json
        )
        target_include_directories(my_fast_MQTT PUBLIC ${CMAKE_SOURCE_DIR}/external/mosquitto/include)
    else()
        message(FATAL_ERROR "libmosquitto_static not found. Ensure mosquitto is configured before my_fast_MQTT.")
    endif()

    set_target_properties(my_fast_MQTT PROPERTIES POSITION_INDEPENDENT_CODE ON)

    print_colored_message("Building my_fast_MQTT library over." COLOR yellow)
    print_colored_message("------------------------------" COLOR magenta)
else()
    message("BUILD_MY_FAST_MQTT is OFF, skipping my_fast_MQTT library build")
    return()
endif()
