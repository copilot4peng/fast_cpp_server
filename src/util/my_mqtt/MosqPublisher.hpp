#pragma once

#include "IMqttPublisher.hpp"
#include <mosquitto.h>
#include <string>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace my_mqtt {

class MosqPublisher : public IMqttPublisher {
public:
  // host e.g. "127.0.0.1", port e.g. 1883, client_id unique for client
  MosqPublisher(const std::string& host,
               int port,
               const std::string& client_id,
               int keepalive = 60,
               bool clean_session = true);

  ~MosqPublisher() override;

  bool Connect() override;
  void Disconnect() override;
  bool Publish(const std::string& topic,
               const std::string& payload,
               int qos = 0,
               bool retain = false) override;

  void SetLastWill(const std::string& topic,
                   const std::string& payload,
                   int qos = 1,
                   bool retain = true) override;

  // 可选：阻塞等待连接成功的最大超时（秒）
  void SetConnectTimeout(int seconds) { connect_timeout_seconds_ = seconds; }

private:
  struct mosquitto *mosq_{nullptr};
  std::string host_;
  int port_;
  std::string client_id_;
  int keepalive_;
  bool clean_session_;

  // LWT buffer
  std::string will_topic_;
  std::string will_payload_;
  int will_qos_{1};
  bool will_retain_{true};
  bool will_set_{false};

  std::mutex conn_mtx_;
  std::condition_variable conn_cv_;
  std::atomic<bool> connected_{false};
  std::atomic<bool> stopping_{false};
  int connect_timeout_seconds_{10};

  // static C callbacks to forward to instance
  static void on_connect(struct mosquitto *mosq, void *obj, int rc);
  static void on_disconnect(struct mosquitto *mosq, void *obj, int rc);
  static void on_publish(struct mosquitto *mosq, void *obj, int mid);

  // helper
  bool wait_for_connect();
};

} // namespace my_mqtt