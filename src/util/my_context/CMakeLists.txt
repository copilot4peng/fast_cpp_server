cmake_minimum_required(VERSION 3.10)

message("CMAKE_VERSION: ${CMAKE_VERSION}")



set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (BUILD_MY_CONTEXT)
    message("BUILD_MY_CONTEXT is ON")
    print_colored_message("------------------------------" COLOR magenta)
    print_colored_message("Building my_context library..." COLOR yellow)
    
    set(MY_CONTEXT_INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/src/util/my_context)
    file(GLOB_RECURSE MY_CONTEXT_SOURCES ${PROJECT_SOURCE_DIR}/src/util/my_context/*.cpp)

    pretty_print_list("MY_CONTEXT_INCLUDE_DIRECTORIES List" MY_CONTEXT_INCLUDE_DIRECTORIES)
    pretty_print_list("MY_CONTEXT_SOURCES List" MY_CONTEXT_SOURCES)

    add_library(my_context STATIC ${MY_CONTEXT_SOURCES})
    target_include_directories(my_context PUBLIC ${MY_CONTEXT_INCLUDE_DIRECTORIES})
    target_link_libraries(my_context PUBLIC pthread)
    target_link_libraries(my_context PUBLIC mylog)
    target_link_libraries(my_context PUBLIC myconfig)
    print_colored_message("Building my_context library over." COLOR yellow)
    print_colored_message("------------------------------" COLOR magenta)
else()
    message("BUILD_MY_CONTEXT is OFF, skipping my_context library build")
    return()
endif()

