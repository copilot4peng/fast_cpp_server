cmake_minimum_required(VERSION 3.10)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (BUILD_MY_GAS_DETECTOR_POLL)
    message("BUILD_MY_GAS_DETECTOR_POLL is ON")
    print_colored_message("------------------------------" COLOR magenta)
    print_colored_message("Building my_gas_detector_poll library..." COLOR yellow)

    set(MY_GAS_DETECTOR_POLL_INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/src/util/my_gas_detector_poll)
    file(GLOB_RECURSE MY_GAS_DETECTOR_POLL_SOURCES CONFIGURE_DEPENDS
        ${PROJECT_SOURCE_DIR}/src/util/my_gas_detector_poll/*.cpp)

    pretty_print_list("MY_GAS_DETECTOR_POLL_INCLUDE_DIRECTORIES List" MY_GAS_DETECTOR_POLL_INCLUDE_DIRECTORIES)
    pretty_print_list("MY_GAS_DETECTOR_POLL_SOURCES List" MY_GAS_DETECTOR_POLL_SOURCES)

    add_library(my_gas_detector_poll STATIC ${MY_GAS_DETECTOR_POLL_SOURCES})
    target_include_directories(my_gas_detector_poll PUBLIC ${MY_GAS_DETECTOR_POLL_INCLUDE_DIRECTORIES})

    # 源码只使用 C++11 特性，显式设置目标标准，便于将该模块移植到
    # 仍然使用 C++11 的独立程序中；项目主工程使用更高标准时也不受影响。
    set_target_properties(my_gas_detector_poll PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)

    target_link_libraries(my_gas_detector_poll PUBLIC pthread)
    target_link_libraries(my_gas_detector_poll PUBLIC mylog)
    target_link_libraries(my_gas_detector_poll PUBLIC my_serial)
    target_link_libraries(my_gas_detector_poll PUBLIC nlohmann_json::nlohmann_json)

    print_colored_message("Building my_gas_detector_poll library over." COLOR yellow)
    print_colored_message("------------------------------" COLOR magenta)
else()
    message("BUILD_MY_GAS_DETECTOR_POLL is OFF, skipping my_gas_detector_poll library build")
    return()
endif()
