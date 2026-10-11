// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "protocol.hpp"
#include "identity.hpp"
#include "voicemeeter-output.hpp"
#include <QObject>
#include <QTcpServer>
#include "secure-socket.hpp"
#include <QUdpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <deque>
namespace linkaudio {
class Sender : public QObject {
  public:
    Sender(AudioQueue &, SharedStatus &);
    void configure(const SenderConfig &);
    void start();
    void stop();

  private:
    AudioQueue &queue;
    SharedStatus &shared;
    SenderConfig config;
    Status status;
    SecureSocket *socket = nullptr;
    QUdpSocket *discovery = nullptr;
    QTimer *timer = nullptr;
    Parser parser;
    QString fingerprint;
    QByteArray token, nonce;
    bool accepted = false;
    quint32 sequence = 0;
    qint64 lastAttempt = 0, lastSearch = 0, lastAck = 0, connectedAt = 0;
    void tick();
    void connectPeer();
    void search();
    void readDiscovery();
    void read();
    void fail(const QString &);
};
class Receiver : public QTcpServer {
  public:
    Receiver(Identity, SharedStatus &, std::unique_ptr<VmApi> = makeVmApi());
    VmInfo detectVoiceMeeter() {
        return callback.detect();
    }
    bool begin(const ReceiverConfig &, QString &error, bool discover = true);
    void stop();
    QString pairing() const {
        return identity.pairing();
    }
    quint16 listeningPort() const {
        return serverPort();
    }

  protected:
    void incomingConnection(qintptr descriptor) override;

  private:
    Identity identity;
    SharedStatus &shared;
    ReceiverConfig config;
    VoiceMeeterOutput callback;
    Status status;
    SecureSocket *socket = nullptr;
    QUdpSocket *discovery = nullptr, *local = nullptr;
    QTimer *timer = nullptr;
    Parser parser;
    bool authenticated = false, primed = false;
    std::deque<QByteArray> packets;
    QElapsedTimer clock;
    qint64 due = 0, lastAudio = 0, lastAck = 0, connectionAt = 0;
    quint32 sequence = 0, lastSequence = 0;
    bool haveSequence = false;
    void read();
    bool message(Message, const QByteArray &);
    void tick();
    void advertise();
    void disconnectPeer(const QString &reason);
};
} // namespace linkaudio
