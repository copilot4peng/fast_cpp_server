cmake_minimum_required(VERSION 3.10)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (BUILD_MY_AIRDROP_LOCK)
    message("BUILD_MY_AIRDROP_LOCK is ON")
    print_colored_message("------------------------------" COLOR magenta)
    print_colored_message("Building my_airdrop_lock library..." COLOR yellow)

    set(MY_AIRDROP_LOCK_INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/src/util/my_airdrop_lock)
    file(GLOB_RECURSE MY_AIRDROP_LOCK_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/util/my_airdrop_lock/*.cpp)

    pretty_print_list("MY_AIRDROP_LOCK_INCLUDE_DIRECTORIES List" MY_AIRDROP_LOCK_INCLUDE_DIRECTORIES)
    pretty_print_list("MY_AIRDROP_LOCK_SOURCES List" MY_AIRDROP_LOCK_SOURCES)

    add_library(my_airdrop_lock STATIC ${MY_AIRDROP_LOCK_SOURCES})
    target_include_directories(my_airdrop_lock PUBLIC ${MY_AIRDROP_LOCK_INCLUDE_DIRECTORIES})
    target_link_libraries(my_airdrop_lock PUBLIC pthread)
    target_link_libraries(my_airdrop_lock PUBLIC mylog)
    target_link_libraries(my_airdrop_lock PUBLIC my_serial)
    target_link_libraries(my_airdrop_lock PUBLIC nlohmann_json::nlohmann_json)

    print_colored_message("Building my_airdrop_lock library over." COLOR yellow)
    print_colored_message("------------------------------" COLOR magenta)
else()
    message("BUILD_MY_AIRDROP_LOCK is OFF, skipping my_airdrop_lock library build")
    return()
endif()