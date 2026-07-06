// =============================================================================
// 文件：TestBlockingQueue.cpp
// 说明：FastMQTT 阻塞队列（BlockingQueue）单元测试。
// =============================================================================

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "BlockingQueue.hpp"

using fast_mqtt::BlockingQueue;

// 基本入队 / 出队。
TEST(BlockingQueueTest, PushPopBasic) {
    BlockingQueue<int> q(0);  // 不限容量
    EXPECT_TRUE(q.Push(1));
    EXPECT_TRUE(q.Push(2));
    EXPECT_EQ(q.Size(), 2u);

    int v = 0;
    EXPECT_TRUE(q.Pop(v));
    EXPECT_EQ(v, 1);
    EXPECT_TRUE(q.Pop(v));
    EXPECT_EQ(v, 2);
    EXPECT_TRUE(q.Empty());
}

// 容量限制：超过最大容量应拒绝入队。
TEST(BlockingQueueTest, CapacityLimitRejects) {
    BlockingQueue<int> q(2);
    EXPECT_TRUE(q.Push(1));
    EXPECT_TRUE(q.Push(2));
    EXPECT_FALSE(q.Push(3));  // 已满，拒绝
    EXPECT_EQ(q.Size(), 2u);
}

// 带超时出队：空队列应超时返回 false。
TEST(BlockingQueueTest, PopTimeout) {
    BlockingQueue<int> q(0);
    int v = 0;
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(q.Pop(v, 100));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    EXPECT_GE(elapsed, 80);  // 允许少量误差
}

// shutdown 应唤醒阻塞的 Pop 并返回 false。
TEST(BlockingQueueTest, ShutdownWakesPop) {
    BlockingQueue<int> q(0);
    std::atomic<bool> returned{false};
    std::thread t([&] {
        int v = 0;
        bool ok = q.Pop(v);  // 阻塞等待
        EXPECT_FALSE(ok);
        returned.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(returned.load());
    q.Shutdown();
    t.join();
    EXPECT_TRUE(returned.load());
    EXPECT_TRUE(q.IsShutdown());
}

// shutdown 之后 Push 应全部失败。
TEST(BlockingQueueTest, PushAfterShutdownFails) {
    BlockingQueue<int> q(0);
    q.Shutdown();
    EXPECT_FALSE(q.Push(1));
}

// Clear 应清空所有元素。
TEST(BlockingQueueTest, ClearEmptiesQueue) {
    BlockingQueue<int> q(0);
    q.Push(1);
    q.Push(2);
    q.Clear();
    EXPECT_EQ(q.Size(), 0u);
    EXPECT_TRUE(q.Empty());
}

// 多线程生产者/消费者压力：所有元素都应被正确消费。
TEST(BlockingQueueTest, MultiThreadProducerConsumer) {
    BlockingQueue<int> q(0);
    const int kCount = 10000;
    std::atomic<int> consumed{0};

    std::thread consumer([&] {
        int v = 0;
        while (consumed.load() < kCount) {
            if (q.Pop(v, 100)) {
                consumed.fetch_add(1);
            }
        }
    });

    std::thread producer([&] {
        for (int i = 0; i < kCount; ++i) {
            while (!q.Push(i)) {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();
    EXPECT_EQ(consumed.load(), kCount);
}
