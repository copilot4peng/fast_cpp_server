#pragma once
#include <string>
#include <vector>
#include <map>
#include "MyLog.h"

namespace my_tools {

/** 
 * @brief 检测指定端口是否可用（即当前没有被占用，且本进程可以 bind）
 * @param port 待检测的端口号
 * @return true 端口可用，false 端口不可用或检测出错（视为占用）
 */
bool isPortAvailable(int port);



}