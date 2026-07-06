cmake_minimum_required(VERSION 3.10)

message("CMAKE_VERSION: ${CMAKE_VERSION}")



set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (BUILD_MY_COMM)
    message("BUILD_MY_COMM is ON")
    print_colored_message("------------------------------" COLOR magenta)
    print_colored_message("Building my_comm library..." COLOR yellow)
    
    # 头文件搜索路径：顶层 my_comm，以及 JSON 子模块目录（方便直接 include 头文件名）。
    set(MY_COMM_INCLUDE_DIRECTORIES
        ${PROJECT_SOURCE_DIR}/src/util/my_comm
        ${PROJECT_SOURCE_DIR}/src/util/my_comm/mqtt/json
        ${PROJECT_SOURCE_DIR}/src/util/my_comm/mqtt/2536pb
    )

    # 收集源码。使用 CONFIGURE_DEPENDS，保证新增 *.cpp 能被自动识别。
    file(GLOB_RECURSE MY_COMM_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/util/my_comm/*.cpp)

    # 排除 mqtt/2536pb 下的拷贝：该协议由独立库 my_csy2536_protocol 负责，
    # 若在此重复编译会与其产生重复符号，且缺少 protobuf(myproto) 依赖。
    # list(FILTER MY_COMM_SOURCES EXCLUDE REGEX ".*/mqtt/2536pb/.*")

    pretty_print_list("MY_COMM_INCLUDE_DIRECTORIES List" MY_COMM_INCLUDE_DIRECTORIES)
    pretty_print_list("MY_COMM_SOURCES List" MY_COMM_SOURCES)

    add_library(my_comm STATIC ${MY_COMM_SOURCES})
    target_include_directories(my_comm PUBLIC ${MY_COMM_INCLUDE_DIRECTORIES})
    target_link_libraries(my_comm PUBLIC pthread)
    target_link_libraries(my_comm PUBLIC mylog)
    target_link_libraries(my_comm PUBLIC myconfig)
    target_link_libraries(my_comm PUBLIC myproto)
    # 链接 FastMQTT 传输层：其 include 目录与 nlohmann_json 会以 PUBLIC 方式传递过来。
    target_link_libraries(my_comm PUBLIC my_fast_MQTT)
    print_colored_message("Building my_comm library over." COLOR yellow)
    print_colored_message("------------------------------" COLOR magenta)
else()
    message("BUILD_MY_COMM is OFF, skipping my_comm library build")
    return()
endif()

