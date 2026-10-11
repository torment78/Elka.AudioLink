// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "return-audio.hpp"
#include <QByteArray>
#include <QString>
#include <QJsonObject>
#include <functional>
#include <mutex>
namespace linkaudio {
constexpr quint16 defaultPort = 45991, discoveryPort = 45992;
constexpr int maxMessage = 4096;
enum class Message : quint8 { hello = 1, ready = 2, audio = 3, ack = 4, error = 5 };
QByteArray frame(Message type, const QByteArray &payload);
class Parser {
  public:
    bool feed(const QByteArray &, const std::function<bool(Message, const QByteArray &)> &);
    void clear() {
        pending.clear();
    }

  private:
    QByteArray pending;
};
struct AudioBlock {
    std::array<float, 256> samples{};
    uint32_t rate = 48000;
};
struct AudioQueue {
    vban::AudioRing<AudioBlock, 256> ring;
    std::atomic<uint64_t> dropped{0};
    void push(const float *samples, size_t frames, uint32_t rate);
};
struct Peer {
    QString name, host, fingerprint;
    quint16 port = defaultPort;
    qint64 seen = 0;
};
struct Status {
    QString state = "Stopped", detail;
    uint64_t received = 0, sent = 0, dropped = 0, udpErrors = 0, underruns = 0;
    uint32_t rate = 0;
    double peak = 0;
    int queuedMs = 0;
    bool connected = false;
};
struct SharedStatus {
    mutable std::mutex mutex;
    Status value;
    QList<Peer> peers;
    Status get() const {
        std::lock_guard lock(mutex);
        return value;
    }
    void set(const Status &s) {
        std::lock_guard lock(mutex);
        value = s;
    }
    QList<Peer> found() const {
        std::lock_guard lock(mutex);
        return peers;
    }
};
struct SenderConfig {
    QString host = "", pairing;
    quint16 port = defaultPort;
    bool enabled = false;
    int monitorBuffer = 60;
};
struct ReceiverConfig {
    QString name, stream = "AudioLink";
    quint16 port = defaultPort, vbanPort = 6980;
    int bufferMs = 40;
};
QByteArray json(const QJsonObject &);
bool validLocalConfig(const ReceiverConfig &, QString &error);
} // namespace linkaudio
