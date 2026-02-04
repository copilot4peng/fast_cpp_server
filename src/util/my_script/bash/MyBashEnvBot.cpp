// MyBashEnvBot.cpp
#include "MyBashEnvBot.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>

#include <errno.h>
#include <string.h>

#include <chrono>
#include <sstream>

namespace bash {

static std::vector<char*> make_argv(const std::vector<std::string>& parts) {
    std::vector<char*> argv;
    argv.reserve(parts.size() + 1);
    for (const auto& p : parts) argv.push_back(const_cast<char*>(p.c_str()));
    argv.push_back(nullptr);
    return argv;
}

my_script::ExecResult MyBashEnvBot::runBash(const std::string& script,
                                            int timeout_seconds,
                                            size_t memory_limit_bytes,
                                            int cpu_time_limit_seconds) {
    my_script::ExecResult res;
    MYLOG_INFO("[MyBashEnvBot] runBash start, timeout={} mem={} cpu={}", timeout_seconds, memory_limit_bytes, cpu_time_limit_seconds);

    int outpipe[2];
    int errpipe[2];
    if (pipe(outpipe) != 0) {
        MYLOG_ERROR("[MyBashEnvBot] pipe stdout failed: {}", strerror(errno));
        return res;
    }
    if (pipe(errpipe) != 0) {
        close(outpipe[0]); close(outpipe[1]);
        MYLOG_ERROR("[MyBashEnvBot] pipe stderr failed: {}", strerror(errno));
        return res;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(outpipe[0]); close(outpipe[1]);
        close(errpipe[0]); close(errpipe[1]);
        MYLOG_ERROR("[MyBashEnvBot] fork failed: {}", strerror(errno));
        return res;
    }

    if (pid == 0) {
        dup2(outpipe[1], STDOUT_FILENO);
        dup2(errpipe[1], STDERR_FILENO);
        close(outpipe[0]); close(outpipe[1]);
        close(errpipe[0]); close(errpipe[1]);

        if (memory_limit_bytes > 0) {
            struct rlimit rl;
            rl.rlim_cur = rl.rlim_max = memory_limit_bytes;
            setrlimit(RLIMIT_AS, &rl);
        }
        if (cpu_time_limit_seconds > 0) {
            struct rlimit rl;
            rl.rlim_cur = rl.rlim_max = cpu_time_limit_seconds;
            setrlimit(RLIMIT_CPU, &rl);
        }

        // 用 /bin/bash -lc 执行传入脚本
        std::vector<std::string> parts = {"/bin/bash", "-lc", script};
        auto av = make_argv(parts);
        execvp(av[0], av.data());
        _exit(127);
    }

    close(outpipe[1]); close(errpipe[1]);

    int flags = fcntl(outpipe[0], F_GETFL, 0);
    fcntl(outpipe[0], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(errpipe[0], F_GETFL, 0);
    fcntl(errpipe[0], F_SETFL, flags | O_NONBLOCK);

    auto start = std::chrono::steady_clock::now();
    bool done = false;
    const int BUF = 4096;
    char buf[BUF];

    while (!done) {
        struct pollfd fds[2];
        fds[0].fd = outpipe[0]; fds[0].events = POLLIN;
        fds[1].fd = errpipe[0]; fds[1].events = POLLIN;

        int timeout_ms = 100;
        if (timeout_seconds > 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
            if (elapsed >= timeout_seconds) {
                kill(pid, SIGKILL);
                res.timed_out = true;
                MYLOG_WARN("[MyBashEnvBot] process timed out and was killed");
                break;
            }
        }

        int rv = poll(fds, 2, timeout_ms);
        if (rv > 0) {
            if (fds[0].revents & POLLIN) {
                ssize_t n = read(outpipe[0], buf, BUF);
                if (n > 0) res.stdout_str.append(buf, buf + n);
            }
            if (fds[1].revents & POLLIN) {
                ssize_t n = read(errpipe[0], buf, BUF);
                if (n > 0) res.stderr_str.append(buf, buf + n);
            }
        }

        int status = 0;
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            if (WIFEXITED(status)) res.exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) res.exit_code = 128 + WTERMSIG(status);
            done = true;
        }
    }

    // drain
    ssize_t n;
    while ((n = read(outpipe[0], buf, BUF)) > 0) res.stdout_str.append(buf, buf + n);
    while ((n = read(errpipe[0], buf, BUF)) > 0) res.stderr_str.append(buf, buf + n);

    close(outpipe[0]); close(errpipe[0]);

    if (res.timed_out) {
        int status = 0;
        waitpid(pid, &status, 0);
    }

    MYLOG_INFO("[MyBashEnvBot] runBash finished exit={} timed_out={}", res.exit_code, res.timed_out);
    return res;
}

} // namespace bash
