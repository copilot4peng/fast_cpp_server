#include "MosqPublisher.hpp"
#include "MyLog.h" // 使用项目日志宏
#include <chrono>
#include <thread>
#include <cstring>

namespace my_mqtt {

MosqPublisher::MosqPublisher(const std::string& host,
                             int port,
                             const std::string& client_id,
                             int keepalive,
                             bool clean_session)
  : host_(host)
  , port_(port)
  , client_id_(client_id)
  , keepalive_(keepalive)
  , clean_session_(clean_session)
{
    mosquitto_lib_init();
    mosq_ = mosquitto_new(client_id_.c_str(), clean_session_, this);
    if (!mosq_) {
        MYLOG_ERROR("MosqPublisher: mosquitto_new failed");
        throw std::runtime_error("mosquitto_new failed");
    }
    mosquitto_connect_callback_set(mosq_, &MosqPublisher::on_connect);
    mosquitto_disconnect_callback_set(mosq_, &MosqPublisher::on_disconnect);
    mosquitto_publish_callback_set(mosq_, &MosqPublisher::on_publish);
}

MosqPublisher::~MosqPublisher() {
    Disconnect();
    if (mosq_) {
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
    }
    mosquitto_lib_cleanup();
}

void MosqPublisher::SetLastWill(const std::string& topic,
                                const std::string& payload,
                                int qos,
                                bool retain)
{
    will_topic_ = topic;
    will_payload_ = payload;
    will_qos_ = qos;
    will_retain_ = retain;
    will_set_ = true;
    if (mosq_) {
        // 设置 will (如果在 Connect 之前调用则会在连接时生效)
        mosquitto_will_set(mosq_,
                           will_topic_.c_str(),
                           static_cast<int>(will_payload_.size()),
                           will_payload_.c_str(),
                           will_qos_,
                           will_retain_);
    }
}

bool MosqPublisher::Connect() {
    if (!mosq_) {
        MYLOG_ERROR("MosqPublisher: invalid mosquitto handle");
        return false;
    }

    // 如果设置了 will 并且 mosq_ 已可用，先设置
    if (will_set_) {
        mosquitto_will_set(mosq_,
                           will_topic_.c_str(),
                           static_cast<int>(will_payload_.size()),
                           will_payload_.c_str(),
                           will_qos_,
                           will_retain_);
    }

    // 非阻塞连接
    int rc = mosquitto_connect_async(mosq_, host_.c_str(), port_, keepalive_);
    if (rc != MOSQ_ERR_SUCCESS) {
        MYLOG_ERROR("MosqPublisher: mosquitto_connect_async failed: {}", mosquitto_strerror(rc));
        return false;
    }

    rc = mosquitto_loop_start(mosq_);
    if (rc != MOSQ_ERR_SUCCESS) {
        MYLOG_ERROR("MosqPublisher: mosquitto_loop_start failed: {}", mosquitto_strerror(rc));
        return false;
    }

    // 等待 connect callback 设置 connected_
    if (!wait_for_connect()) {
        MYLOG_WARN("MosqPublisher: connect timeout");
        return false;
    }

    MYLOG_INFO("MosqPublisher connected to {}:{}", host_, port_);
    return true;
}

void MosqPublisher::Disconnect() {
    stopping_.store(true);

    if (mosq_) {
        // 请求断开
        int rc = mosquitto_disconnect(mosq_);
        if (rc != MOSQ_ERR_SUCCESS) {
            MYLOG_WARN("MosqPublisher: mosquitto_disconnect returned {}", mosquitto_strerror(rc));
        }
        // 停 loop
        mosquitto_loop_stop(mosq_, true);
        connected_.store(false);
    }
}

bool MosqPublisher::Publish(const std::string& topic,
                           const std::string& payload,
                           int qos,
                           bool retain)
{
    if (!mosq_) {
        MYLOG_ERROR("MosqPublisher: invalid handle");
        return false;
    }
    if (!connected_.load()) {
        MYLOG_WARN("MosqPublisher: not connected, publish will fail");
        return false;
    }

    int rc = mosquitto_publish(mosq_,
                               nullptr,
                               topic.c_str(),
                               static_cast<int>(payload.size()),
                               payload.data(),
                               qos,
                               retain);
    if (rc != MOSQ_ERR_SUCCESS) {
        MYLOG_ERROR("MosqPublisher: publish failed: {}", mosquitto_strerror(rc));
        return false;
    }
    return true;
}

bool MosqPublisher::wait_for_connect() {
    std::unique_lock<std::mutex> lk(conn_mtx_);
    // 等待 connect callback ，超时使用 connect_timeout_seconds_
    if (!conn_cv_.wait_for(lk, std::chrono::seconds(connect_timeout_seconds_), [this]{ return connected_.load(); })) {
        return false;
    }
    return true;
}

// static callbacks
void MosqPublisher::on_connect(struct mosquitto *mosq, void *obj, int rc) {
    MosqPublisher *self = static_cast<MosqPublisher*>(obj);
    if (!self) return;
    if (rc == 0) {
        self->connected_.store(true);
        {
            std::lock_guard<std::mutex> lk(self->conn_mtx_);
            // notify
        }
        self->conn_cv_.notify_all();
        MYLOG_INFO("MosqPublisher on_connect success");
    } else {
        self->connected_.store(false);
        MYLOG_WARN("MosqPublisher on_connect failed: rc={}", rc);
        self->conn_cv_.notify_all();
    }
}

void MosqPublisher::on_disconnect(struct mosquitto *mosq, void *obj, int rc) {
    MosqPublisher *self = static_cast<MosqPublisher*>(obj);
    if (!self) return;
    self->connected_.store(false);
    MYLOG_WARN("MosqPublisher on_disconnect rc={}", rc);
}

void MosqPublisher::on_publish(struct mosquitto *mosq, void *obj, int mid) {
    // 发布回调，可用于日志或追踪
    (void)mosq;
    (void)obj;
    (void)mid;
    // MYLOG_INFO("MosqPublisher on_publish mid={}", mid);
}

} // namespace my_mqtt