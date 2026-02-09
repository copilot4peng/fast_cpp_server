#include "MyTools.h"

#include <cstring>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

#include "MyLog.h"


// 返回 true 表示端口可用（可在本进程 bind），false 表示不可用或检测出错（视为占用）
bool my_tools::isPortAvailable(int port) {
    // 创建 IPv4 TCP socket
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        MYLOG_WARN("检测端口 {} 时创建 socket 失败: {}，视为不可用", port, std::strerror(errno));
        return false;
    }

    // 允许快速重用地址（不影响判断）
    int opt = 1;
    (void)setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // 0.0.0.0
    addr.sin_port = htons(static_cast<uint16_t>(port));

    // 尝试 bind
    int ret = ::bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (ret == 0) {
        // bind 成功，说明端口暂时可用；释放 socket 后返回 true
        ::close(sock);
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 短暂等待，确保系统更新端口状态
        return true;
    } else {
        // bind 失败，检查 errno
        int err = errno;
        if (err == EADDRINUSE) {
            // 明确被占用
            ::close(sock);
            return false;
        } else if (err == EACCES) {
            // 权限问题（端口 <1024 或权限限制），视为不可用
            MYLOG_WARN("检测端口 {} 时权限受限: {}，视为不可用", port, std::strerror(err));
            ::close(sock);
            return false;
        } else {
            // 其它错误，记录并视为不可用
            MYLOG_WARN("检测端口 {} 时 bind 出错: {}，视为不可用", port, std::strerror(err));
            ::close(sock);
            return false;
        }
    }
}
