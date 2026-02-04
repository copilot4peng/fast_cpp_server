cmake_minimum_required(VERSION 3.10)

message("CMAKE_VERSION: ${CMAKE_VERSION}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (BUILD_MY_SCRIPT)
    message("BUILD_MY_SCRIPT is ON")
    print_colored_message("------------------------------" COLOR magenta)
    print_colored_message("Building my_script library..." COLOR yellow)

    set(MY_SCRIPT_INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/src/util/my_script)
    file(GLOB_RECURSE MY_SCRIPT_SOURCES ${PROJECT_SOURCE_DIR}/src/util/my_script/*.cpp)

    pretty_print_list("MY_SCRIPT_INCLUDE_DIRECTORIES List" MY_SCRIPT_INCLUDE_DIRECTORIES)
    pretty_print_list("MY_SCRIPT_SOURCES List" MY_SCRIPT_SOURCES)

    add_library(my_script STATIC ${MY_SCRIPT_SOURCES})
    target_include_directories(my_script PUBLIC ${MY_SCRIPT_INCLUDE_DIRECTORIES})

    target_link_libraries(my_script PUBLIC pthread)
    target_link_libraries(my_script PUBLIC mylog)
    target_link_libraries(my_script PUBLIC myconfig)
    target_link_libraries(my_script PUBLIC my_data)
    target_link_libraries(my_script PUBLIC nlohmann_json::nlohmann_json)

    # 关键：链接 sqlite amalgamation 静态库 target
    target_link_libraries(my_script PUBLIC sqlite3_static)

    print_colored_message("Building my_script library over." COLOR yellow)
    print_colored_message("------------------------------" COLOR magenta)
else()
    message("BUILD_MY_SCRIPT is OFF, skipping my_script library build")
    return()
endif()