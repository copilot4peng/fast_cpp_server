cmake_minimum_required(VERSION 3.10)

message("CMAKE_VERSION: ${CMAKE_VERSION}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (BUILD_MY_MATH)
    message("BUILD_MY_MATH is ON")
    print_colored_message("------------------------------" COLOR magenta)
    print_colored_message("Building my_math library..." COLOR yellow)

    set(MY_MATH_INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/src/util/my_math)
    file(GLOB_RECURSE MY_MATH_SOURCES ${PROJECT_SOURCE_DIR}/src/util/my_math/*.cpp)

    pretty_print_list("MY_MATH_INCLUDE_DIRECTORIES List" MY_MATH_INCLUDE_DIRECTORIES)
    pretty_print_list("MY_MATH_SOURCES List" MY_MATH_SOURCES)

    add_library(my_math STATIC ${MY_MATH_SOURCES})
    target_include_directories(my_math PUBLIC ${MY_MATH_INCLUDE_DIRECTORIES})

    target_link_libraries(my_math PUBLIC pthread)
    target_link_libraries(my_math PUBLIC mylog)
    target_link_libraries(my_math PUBLIC myconfig)

    print_colored_message("Building my_math library over." COLOR yellow)
    print_colored_message("------------------------------" COLOR magenta)
else()
    message("BUILD_MY_MATH is OFF, skipping my_math library build")
    return()
endif()