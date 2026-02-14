// #include <gtest/gtest.h>
// #include "SoftHealthMonitorManager.h"
// #include "SoftHealthMonitorConfig.h"
// #include "SoftHealthSnapshot.h"

// #include <thread>
// #include <chrono>

// TEST(SoftHealthMonitorManagerTest, RunAndGetSnapshotAfter5Seconds) {
//     SoftHealthMonitorConfig config;
//     config.setIntervalSeconds(1);             // 每秒采集一次
//     config.setOutputToConsole(false);         // 测试时关闭输出
//     config.setEnableThreadTree(true);

//     SoftHealthMonitorManager monitor;
//     monitor.init(config);
//     monitor.start();

//     // 睡眠 5 秒
//     std::this_thread::sleep_for(std::chrono::seconds(5));

//     // 第 5 秒后拿快照
//     HealthSnapshot snapshot = monitor.getLastSnapshot();

//     // 基本断言验证
//     EXPECT_GT(snapshot.getUptimeSeconds(), 4.0);
//     EXPECT_LT(snapshot.getUptimeSeconds(), 6.5);

//     const ProcessInfo& proc = snapshot.getProcessInfo();
//     // EXPECT_GT(proc.pid, 0);
//     EXPECT_FALSE(proc.name.empty());

//     const auto& threads = snapshot.getThreads();
//     EXPECT_GE(threads.size(), 1);  // 至少应有主线程

//     monitor.stop();

//     // 睡 5 秒后再验证停止效果（快照不应继续刷新）
//     auto old_snapshot_time = snapshot.getUptimeSeconds();
//     std::this_thread::sleep_for(std::chrono::seconds(5));
//     auto new_snapshot = monitor.getLastSnapshot();

//     EXPECT_LE(new_snapshot.getUptimeSeconds(), old_snapshot_time + 2.0);  // 不再更新
// }
