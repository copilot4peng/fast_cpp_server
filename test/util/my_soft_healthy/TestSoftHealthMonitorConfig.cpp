// #include <gtest/gtest.h>
// #include "SoftHealthMonitorConfig.h"

// TEST(SoftHealthMonitorConfigTest, DefaultConfigIsCorrect) {
//     SoftHealthMonitorConfig config = SoftHealthMonitorConfig::getDefaultConfig();

//     EXPECT_EQ(config.getIntervalSeconds(), 5);
//     EXPECT_TRUE(config.isThreadTreeEnabled());
//     EXPECT_TRUE(config.isConsoleOutputEnabled());
//     EXPECT_FALSE(config.isSubprocessCheckEnabled());
// }

// TEST(SoftHealthMonitorConfigTest, SettersWorkCorrectly) {
//     SoftHealthMonitorConfig config;

//     config.setIntervalSeconds(10);
//     config.setEnableThreadTree(false);
//     config.setOutputToConsole(false);
//     config.setEnableSubprocessCheck(true);

//     EXPECT_EQ(config.getIntervalSeconds(), 10);
//     EXPECT_FALSE(config.isThreadTreeEnabled());
//     EXPECT_FALSE(config.isConsoleOutputEnabled());
//     EXPECT_TRUE(config.isSubprocessCheckEnabled());
// }

// TEST(SoftHealthMonitorConfigTest, TesttoJsonString) {
//     SoftHealthMonitorConfig config;
//     config.setIntervalSeconds(10);
//     config.setEnableThreadTree(false);
//     config.setOutputToConsole(false);
//     config.setEnableSubprocessCheck(true);

//     std::string jsonStr = config.toJsonString();
//     std::cout << "Config JSON: " << jsonStr << std::endl;

//     EXPECT_TRUE(jsonStr.find("\"interval_seconds\": 10") != std::string::npos);
//     EXPECT_TRUE(jsonStr.find("\"enable_thread_tree\": false") != std::string::npos);
//     EXPECT_TRUE(jsonStr.find("\"output_to_console\": false") != std::string::npos);
//     EXPECT_TRUE(jsonStr.find("\"enable_subprocess_check\": true") != std::string::npos);
// }