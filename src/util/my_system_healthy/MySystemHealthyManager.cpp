#include <chrono>
#include <pthread.h>

#include "MySystemHealthyManager.h"
#include "CPUInfoTools.h"
#include "MemInfoTools.h"
#include "DiskInfoTools.h"
#include "NetInfoTools.h"
#include "GPUInfoTools.h"
#include "ProcessInfoTools.h"

// using namespace SystemHealthyTools;

namespace {

constexpr char kWorkerThreadName[] = "sys_health_mgr";

void SetCurrentThreadName(const char* name) {
#if defined(__linux__)
  pthread_setname_np(pthread_self(), name);
#else
  (void)name;
#endif
}

}  // namespace


namespace MySystemHealthy {

MySystemHealthyManager::MySystemHealthyManager() : running_(false) {}
MySystemHealthyManager::~MySystemHealthyManager() {
  Shutdown();
}

MySystemHealthyManager& MySystemHealthyManager::GetInstance() {
  static MySystemHealthyManager instance;
  return instance;
}

void MySystemHealthyManager::Init(int update_interval_sec) {
  interval_sec_ = update_interval_sec;
  if (running_) return;
  running_ = true;
  worker_ = std::thread(&MySystemHealthyManager::WorkerLoop, this);
  MYLOG_INFO("MySystemHealthyManager started with interval: {} sec", interval_sec_);
}

void MySystemHealthyManager::Shutdown() {
  if (!running_) return;
  running_ = false;
  if (worker_.joinable()) worker_.join();
  MYLOG_INFO("MySystemHealthyManager stopped.");
}

void MySystemHealthyManager::WorkerLoop() {
  SetCurrentThreadName(kWorkerThreadName);

  while (running_) {
    SystemHealthy::SystemInfo info;
    info.set_os_name("Linux"); // todo
    info.set_platform("x86_64"); // todo
    info.set_uptime_seconds(static_cast<uint64_t>(time(nullptr))); // todo

    CPUInfoTools::CollectCPUInfo();
    MemInfoTools::CollectMemInfo();
    DiskInfoTools::CollectDiskInfo();
    NetInfoTools::CollectNetInfo();
    GPUInfoTools::CollectGPUInfo();

    // *info.mutable_cpu_info() = CPUInfoTools::CollectCPUInfo();
    // *info.mutable_mem_info() = MemInfoTools::CollectMemInfo();
    // *info.mutable_disk_info() = DiskInfoTools::CollectDiskInfo();
    // *info.mutable_net_info() = NetInfoTools::CollectNetInfo();
    // for (auto& gpu : GPUInfoTools::CollectGPUInfo()) {
    //   *info.add_gpu_infos() = gpu;
    // }
    // for (auto& proc : ProcessInfoTools::CollectProcessInfo()) {
    //   *info.add_processes() = proc;
    // }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      current_info_ = info;
    }
    std::this_thread::sleep_for(std::chrono::seconds(interval_sec_));
  }
}

const SystemHealthy::SystemInfo MySystemHealthyManager::GetSystemInfo() {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_info_;
}

};
