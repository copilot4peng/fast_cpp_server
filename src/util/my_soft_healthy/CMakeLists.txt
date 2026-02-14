cmake_minimum_required(VERSION 3.10)

message("CMAKE_VERSION: ${CMAKE_VERSION}")



set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (BUILD_MY_SOFT_HEALTHY)
    message("BUILD_MY_SOFT_HEALTHY is ON")
    print_colored_message("------------------------------" COLOR magenta)
    print_colored_message("Building my_soft_healthy library..." COLOR yellow)
    
    set(MY_SOFT_HEALTHY_INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/src/util/my_soft_healthy)
    file(GLOB_RECURSE MY_SOFT_HEALTHY_SOURCES ${PROJECT_SOURCE_DIR}/src/util/my_soft_healthy/*.cpp)

    pretty_print_list("MY_SOFT_HEALTHY_INCLUDE_DIRECTORIES List" MY_SOFT_HEALTHY_INCLUDE_DIRECTORIES)
    pretty_print_list("MY_SOFT_HEALTHY_SOURCES List" MY_SOFT_HEALTHY_SOURCES)

    add_library(my_soft_healthy STATIC ${MY_SOFT_HEALTHY_SOURCES})
    target_include_directories(my_soft_healthy PUBLIC ${MY_SOFT_HEALTHY_INCLUDE_DIRECTORIES})
    target_link_libraries(my_soft_healthy PUBLIC pthread)
    target_link_libraries(my_soft_healthy PUBLIC mylog)
    target_link_libraries(my_soft_healthy PUBLIC nlohmann_json::nlohmann_json)
    print_colored_message("Building my_soft_healthy library over." COLOR yellow)
    print_colored_message("------------------------------" COLOR magenta)
else()
    message("BUILD my_soft_healthy is OFF, skipping my_soft_healthy library build")
    return()
endif()

