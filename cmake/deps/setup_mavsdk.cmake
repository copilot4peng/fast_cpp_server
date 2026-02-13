# 1. 定义需要校验和导入的库列表（注意顺序：基础库放在后面）
set(MAVSDK_LIBS 
    mavsdk
    mav              # V3 核心处理库
    events           # 对应 libevents.a
    jsoncpp
    tinyxml2         # libmav 的依赖
    curl             # 网络支持
    ssl              # 加密支持
    crypto           # 加密支持
    lzma             # 压缩支持
    z                # libz.a
)

list(APPEND THIRD_INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/build/mavsdk_dist/include)
list(APPEND THIRD_INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/build/mavsdk_dist/include/mavsdk)
list(APPEND THIRD_INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/build/mavsdk_dist/include/mavsdk/mavlink)
list(APPEND THIRD_INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/build/mavsdk_dist/include/mavsdk/plugins)

set(CMAKE_PREFIX_PATH "${PROJECT_SOURCE_DIR}/build/mavsdk_dist")

# 2. 批量校验文件并创建 IMPORTED 目标
foreach(lib_name ${MAVSDK_LIBS})
    set(LIB_FILE "${PROJECT_SOURCE_DIR}/build/mavsdk_dist/lib/lib${lib_name}.a")
    
    if(NOT EXISTS ${LIB_FILE})
        message(FATAL_ERROR "Missing: ${LIB_FILE}")
    endif()

    add_library(${lib_name} STATIC IMPORTED)
    set_target_properties(${lib_name} PROPERTIES IMPORTED_LOCATION ${LIB_FILE})
endforeach()

# 3. 组装接口目标
add_library(my_mavsdk INTERFACE)

target_include_directories(my_mavsdk INTERFACE
    ${PROJECT_SOURCE_DIR}/build/mavsdk_dist/include
    ${PROJECT_SOURCE_DIR}/build/mavsdk_dist/include/mavsdk
    ${PROJECT_SOURCE_DIR}/build/mavsdk_dist/include/mavsdk/mavlink
    ${PROJECT_SOURCE_DIR}/build/mavsdk_dist/include/mavsdk/plugins
)

find_package(Threads REQUIRED)

# 4. 关键：链接顺序！
# 必须先链接 MAVSDK，再链接它依赖的 mav，最后是底层系统库
target_link_libraries(my_mavsdk INTERFACE
    mavsdk
    mav
    events
    jsoncpp
    tinyxml2
    curl
    ldap
    lber
    ssl
    crypto
    lzma
    z
    nlohmann_json::nlohmann_json
    Threads::Threads
    dl    # 动态链接库支持，通常是 curl/ssl 需要
)