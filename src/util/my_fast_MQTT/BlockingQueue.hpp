#pragma once

// =============================================================================
// 文件：BlockingQueue.hpp
// 模块：FastMQTT
// 说明：线程安全的阻塞队列（Blocking Queue）。
//
// 设计要点：
//   1. 使用 std::mutex + std::condition_variable 实现，禁止 Busy Loop（忙等待）。
//   2. 支持最大容量限制，超过容量时 push 直接失败，避免内存无限增长（OOM）。
//   3. 支持超时出队 pop(timeout)，方便后台线程周期性检查退出标志。
//   4. 支持 shutdown()，唤醒所有阻塞在 pop 上的线程，用于安全退出。
//
// 该组件不依赖任何业务类型，可被 SendQueue / ReceiveQueue 复用。
// =============================================================================

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace fast_mqtt {

/**
 * @brief 线程安全阻塞队列。
 *
 * @tparam T 队列中存放的元素类型（要求可移动构造）。
 */
template <typename T>
class BlockingQueue {
public:
    /**
     * @brief 构造阻塞队列。
     * @param max_size 队列最大容量，0 表示不限制容量。
     */
    explicit BlockingQueue(std::size_t max_size = 0)
        : max_size_(max_size) {}

    // 禁止拷贝与移动，队列应作为单一持有者存在。
    BlockingQueue(const BlockingQueue&) = delete;
    BlockingQueue& operator=(const BlockingQueue&) = delete;

    /**
     * @brief 入队一个元素（拷贝语义）。
     * @param value 待入队元素。
     * @return true 入队成功；false 队列已满或已 shutdown 被拒绝。
     */
    bool Push(const T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (shutdown_) {
            return false;
        }
        // 容量限制：max_size_ 为 0 表示不限容量。
        if (max_size_ != 0 && queue_.size() >= max_size_) {
            return false;  // 队列已满，直接拒绝，交由上层记录日志。
        }
        queue_.push_back(value);
        // 只唤醒一个等待线程即可，避免惊群。
        not_empty_.notify_one();
        return true;
    }

    /**
     * @brief 入队一个元素（移动语义）。
     * @param value 待入队元素（右值）。
     * @return true 入队成功；false 队列已满或已 shutdown 被拒绝。
     */
    bool Push(T&& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (shutdown_) {
            return false;
        }
        if (max_size_ != 0 && queue_.size() >= max_size_) {
            return false;
        }
        queue_.push_back(std::move(value));
        not_empty_.notify_one();
        return true;
    }

    /**
     * @brief 阻塞出队。若队列为空则一直等待，直到有元素或 shutdown。
     * @param out 出参，成功时写入队首元素。
     * @return true 成功取出元素；false 队列已 shutdown 且为空。
     */
    bool Pop(T& out) {
        std::unique_lock<std::mutex> lock(mutex_);
        // 使用条件变量等待，条件：队列非空 或 已 shutdown。
        not_empty_.wait(lock, [this] { return !queue_.empty() || shutdown_; });
        if (queue_.empty()) {
            // 只有在 shutdown 且队列已空时才会走到这里。
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    /**
     * @brief 带超时的阻塞出队。
     * @param out 出参，成功时写入队首元素。
     * @param timeout_ms 最大等待毫秒数。
     * @return true 成功取出元素；false 超时或已 shutdown 且为空。
     */
    bool Pop(T& out, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ready = not_empty_.wait_for(
            lock, std::chrono::milliseconds(timeout_ms),
            [this] { return !queue_.empty() || shutdown_; });
        if (!ready || queue_.empty()) {
            return false;  // 超时，或 shutdown 后队列为空。
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    /**
     * @brief 关闭队列。唤醒所有等待线程，后续 Push 全部失败。
     *
     * 用于程序退出时让阻塞在 Pop 的线程能够返回，从而安全 join。
     */
    void Shutdown() {
        std::unique_lock<std::mutex> lock(mutex_);
        shutdown_ = true;
        not_empty_.notify_all();
    }

    /**
     * @brief 清空队列中的全部元素（不改变 shutdown 状态）。
     */
    void Clear() {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.clear();
    }

    /**
     * @brief 获取当前队列元素个数。
     * @return 队列长度。
     */
    std::size_t Size() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.size();
    }

    /**
     * @brief 判断队列是否为空。
     * @return true 空；false 非空。
     */
    bool Empty() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    /**
     * @brief 队列是否已被 shutdown。
     */
    bool IsShutdown() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return shutdown_;
    }

private:
    mutable std::mutex          mutex_;        ///< 保护队列内部状态的互斥锁。
    std::condition_variable     not_empty_;    ///< “非空”条件变量。
    std::deque<T>               queue_;        ///< 底层双端队列。
    std::size_t                 max_size_{0};  ///< 最大容量，0 表示不限。
    bool                        shutdown_{false};  ///< 关闭标志。
};

}  // namespace fast_mqtt
