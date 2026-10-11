// SPDX-License-Identifier: GPL-2.0-or-later
#include "network.hpp"
#include "vban-transmitter.hpp"
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QDateTime>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QThread>
#include <cmath>
#include <cstring>
#include <chrono>
namespace linkaudio {
static qint64 now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
static QJsonObject object(const QByteArray &bytes) {
    return QJsonDocument::fromJson(bytes).object();
}
Sender::Sender(AudioQueue &q, SharedStatus &s) : queue(q), shared(s) {}
void Sender::start() {
    qInfo("[AudioLink] Sender network worker started");
    if (timer)
        return;
    socket = new SecureSocket(this);
    discovery = new QUdpSocket(this);
    timer = new QTimer(this);
    socket->setReadBufferSize(65536);
    connect(socket, &SecureSocket::encrypted, this, [this] {
        if (socket->peerFingerprint() != fingerprint) {
            fail("Receiver identity mismatch.");
            return;
        }
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        status.state = "Pairing";
        connectedAt = now();
        socket->write(
            frame(Message::hello, json({{"version", 1}, {"token", QString::fromLatin1(token.toHex())}})));
    });
    connect(socket, &SecureSocket::readyRead, this, [this] { read(); });
    connect(socket, &SecureSocket::disconnected, this, [this] {
        accepted = false;
        status.connected = false;
        status.peak = 0;
        parser.clear();
        if (config.enabled)
            status.state = "Reconnecting";
        shared.set(status);
    });
    connect(socket, &SecureSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        status.detail = socket->errorString();
        shared.set(status);
    });
    discovery->bind(QHostAddress::AnyIPv4, 0);
    connect(discovery, &QUdpSocket::readyRead, this, [this] { readDiscovery(); });
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(5);
    connect(timer, &QTimer::timeout, this, [this] { tick(); });
    timer->start();
    search();
}
void Sender::configure(const SenderConfig &c) {
    qInfo("[AudioLink] Sender configuration applied (enabled=%d)", int(c.enabled));
    config = c;
    accepted = false;
    fingerprint.clear();
    token.clear();
    lastAttempt = 0;
    parser.clear();
    if (socket)
        socket->abort();
    status = {};
    status.state = c.enabled ? "Connecting" : "Stopped";
    if (c.enabled && !decodePairing(c.pairing, fingerprint, token)) {
        config.enabled = false;
        status.state = "Pairing needed";
        status.detail = "Copy the pairing code from the receiver once.";
    }
    shared.set(status);
}
void Sender::stop() {
    config.enabled = false;
    if (timer)
        timer->stop();
    if (socket)
        socket->abort();
    if (discovery)
        discovery->close();
    status.state = "Stopped";
    status.connected = false;
    status.peak = 0;
    shared.set(status);
}
void Sender::fail(const QString &why) {
    accepted = false;
    socket->abort();
    status.state = "Reconnecting";
    status.detail = why;
    status.connected = false;
    shared.set(status);
}
void Sender::connectPeer() {
    lastAttempt = now();
    if (config.host.trimmed().isEmpty()) {
        status.state = "Searching";
        return;
    }
    parser.clear();
    status.state = "Connecting";
    status.detail = config.host + ":" + QString::number(config.port);
    connectedAt = now();
    socket->setExpectedFingerprint(fingerprint);
    socket->connectToHostEncrypted(config.host, config.port);
}
void Sender::search() {
    lastSearch = now();
    nonce = QByteArray::number(QRandomGenerator::system()->generate64(), 16);
    const QByteArray query = "ELKA_AUDIOLINK_DISCOVER_V1:" + nonce;
    discovery->writeDatagram(query, QHostAddress::LocalHost, discoveryPort);
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || !(iface.flags() & QNetworkInterface::CanBroadcast))
            continue;
        for (const auto &a : iface.addressEntries())
            if (a.ip().protocol() == QAbstractSocket::IPv4Protocol && !a.broadcast().isNull())
                discovery->writeDatagram(query, a.broadcast(), discoveryPort);
    }
    std::lock_guard lock(shared.mutex);
    for (auto it = shared.peers.begin(); it != shared.peers.end();)
        if (now() - it->seen > 8000)
            it = shared.peers.erase(it);
        else
            ++it;
}
void Sender::readDiscovery() {
    int budget = 64;
    while (discovery->hasPendingDatagrams() && budget--) {
        auto packet = discovery->receiveDatagram(1024);
        auto o = object(packet.data());
        if (o["service"] != "ElkaAudioLink/1" || o["nonce"].toString().toLatin1() != nonce)
            continue;
        Peer p{o["name"].toString().left(64), packet.senderAddress().toString(), o["fingerprint"].toString(),
               quint16(o["port"].toInt()), now()};
        if (p.fingerprint.size() != 64 || !p.port)
            continue;
        {
            std::lock_guard lock(shared.mutex);
            bool updated = false;
            for (auto &old : shared.peers)
                if (old.fingerprint == p.fingerprint) {
                    old = p;
                    updated = true;
                    break;
                }
            if (!updated && shared.peers.size() < 64)
                shared.peers.append(p);
        }
        if (config.enabled && !accepted && p.fingerprint == fingerprint &&
            socket->state() == QAbstractSocket::UnconnectedState) {
            config.host = p.host;
            config.port = p.port;
        }
    }
}
void Sender::read() {
    while (socket->bytesAvailable() > 0) {
        if (!parser.feed(socket->read(8192), [this](Message m, const QByteArray &data) {
                auto o = object(data);
                if (m == Message::ready && !accepted && o["version"].toInt() == 1) {
                    accepted = true;
                    status.connected = true;
                    status.state = "Connected";
                    status.detail = "Receiver authenticated; waiting for audio acknowledgements.";
                    lastAck = now();
                    return true;
                }
                if (m == Message::ack && accepted) {
                    status.received = o["received"].toVariant().toULongLong();
                    status.sent = o["sent"].toVariant().toULongLong();
                    status.udpErrors = o["udpErrors"].toVariant().toULongLong();
                    status.queuedMs = o["queueMs"].toInt();
                    status.underruns = o["underruns"].toVariant().toULongLong();
                    status.state = "Streaming";
                    status.detail =
                        "Receiver confirms audio; local VBAN delivery is not VoiceMeeter confirmation.";
                    lastAck = now();
                    return true;
                }
                if (m == Message::error) {
                    fail(o["message"].toString().left(256));
                    return false;
                }
                return false;
            })) {
            if (socket->state() != QAbstractSocket::UnconnectedState)
                fail("Invalid receiver response.");
            break;
        }
    }
}
void Sender::tick() {
    if (now() - lastSearch >= 2000)
        search();
    if (config.enabled && socket->state() == QAbstractSocket::UnconnectedState && now() - lastAttempt > 2000)
        connectPeer();
    if (config.enabled && socket->state() != QAbstractSocket::UnconnectedState && !accepted &&
        now() - connectedAt > 5000)
        fail("Connection or pairing timed out.");
    if (accepted && now() - lastAck > 2000)
        fail("Receiver stopped acknowledging audio; reconnecting to discard stale audio.");
    // Drop stale capture blocks, including while disconnected. Never replay a backlog on reconnect.
    while (queue.ring.size() > 32) {
        queue.ring.pop();
        ++queue.dropped;
    }
    int budget = 64;
    while (const auto *block = queue.ring.front()) {
        if (--budget < 0)
            break;
        if (accepted) {
            if (socket->bytesToWrite() > 32768 || socket->encryptedBytesToWrite() > 32768) {
                fail("Connection congested; reconnecting to discard queued audio.");
            } else {
                std::array<uint8_t, vban::max_datagram> packet{};
                const auto n = vban::encode_stereo_pcm(packet.data(), block->rate, "AudioLink", sequence++,
                                                       block->samples.data(), 128, 24);
                if (n)
                    socket->write(frame(Message::audio,
                                        QByteArray(reinterpret_cast<char *>(packet.data()), qsizetype(n))));
                status.rate = block->rate;
                double peak = 0;
                for (float f : block->samples)
                    if (std::isfinite(f))
                        peak = std::max(peak, double(std::abs(f)));
                status.peak = peak;
            }
        }
        queue.ring.pop();
    }
    status.dropped = queue.dropped.load();
    shared.set(status);
}
Receiver::Receiver(Identity id, SharedStatus &s, std::unique_ptr<VmApi> api)
    : identity(std::move(id)), shared(s), callback(std::move(api)) {}
bool Receiver::begin(const ReceiverConfig &c, QString &error, bool discover) {
    if (!validLocalConfig(c, error))
        return false;
    stop();
    // A failed callback release must block switching outputs, never leave two paths active.
    if (!callback.stop(error))
        return false;
    config = c;
    status = {};
    status.outputMode = c.mode;
    if (!listen(QHostAddress::AnyIPv4, c.port)) {
        error = errorString();
        return false;
    }
    if (c.mode == OutputMode::callback) {
        if (!callback.start(c.vmType, c.target, c.destinations, c.bufferMs, error)) {
            stop();
            return false;
        }
    } else {
        local = new QUdpSocket(this);
        if (!local->bind(QHostAddress::LocalHost, 0)) {
            error = local->errorString();
            stop();
            return false;
        }
    }
    if (discover) {
        discovery = new QUdpSocket(this);
        if (!discovery->bind(QHostAddress::AnyIPv4, discoveryPort,
                             QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
            status.detail = "Discovery unavailable; connect by IP and port.";
        else
            connect(discovery, &QUdpSocket::readyRead, this, [this] { advertise(); });
    }
    timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(1);
    connect(timer, &QTimer::timeout, this, [this] { tick(); });
    timer->start();
    clock.start();
    status.state = "Listening";
    shared.set(status);
    return true;
}
void Receiver::stop() {
    close();
    if (timer) {
        delete timer;
        timer = nullptr;
    }
    if (discovery) {
        delete discovery;
        discovery = nullptr;
    }
    if (local) {
        delete local;
        local = nullptr;
    }
    if (socket) {
        socket->disconnect(this);
        socket->abort();
        delete socket;
        socket = nullptr;
    }
    authenticated = false;
    primed = false;
    packets.clear();
    parser.clear();
    haveSequence = false;
    status.connected = false;
    status.state = "Stopped";
    status.peak = 0;
    status.queuedMs = 0;
    QString callbackError;
    if (!callback.stop(callbackError))
        status.detail = callbackError;
    shared.set(status);
}
void Receiver::incomingConnection(qintptr descriptor) {
    if (socket) {
        QTcpSocket rejected;
        rejected.setSocketDescriptor(descriptor);
        rejected.abort();
        return;
    }
    status = {};
    status.outputMode = config.mode;
    socket = new SecureSocket(this);
    socket->setReadBufferSize(65536);
    socket->setLocalIdentity(identity.cert, identity.key);
    if (!socket->setSocketDescriptor(descriptor)) {
        disconnectPeer("Could not accept sender.");
        return;
    }
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connectionAt = now();
    lastAudio = now();
    lastAck = 0;
    status.state = "Authenticating";
    connect(socket, &SecureSocket::readyRead, this, [this] { read(); });
    connect(socket, &SecureSocket::disconnected, this,
            [this] { disconnectPeer("Sender disconnected; waiting for reconnect."); });
    socket->startServerEncryption();
    shared.set(status);
}
void Receiver::disconnectPeer(const QString &why) {
    if (socket) {
        auto *old = socket;
        socket = nullptr;
        old->disconnect(this);
        old->abort();
        old->deleteLater();
    }
    authenticated = false;
    primed = false;
    packets.clear();
    parser.clear();
    haveSequence = false;
    status.state = "Listening";
    callback.flush();
    status.detail = why;
    status.connected = false;
    status.peak = 0;
    status.queuedMs = 0;
    shared.set(status);
}
void Receiver::read() {
    while (socket && socket->bytesAvailable() > 0) {
        if (!parser.feed(socket->read(8192),
                         [this](Message m, const QByteArray &d) { return message(m, d); })) {
            disconnectPeer("Sender protocol or pairing rejected.");
            break;
        }
    }
}
bool Receiver::message(Message type, const QByteArray &data) {
    if (!authenticated) {
        if (type != Message::hello || !socket->isEncrypted())
            return false;
        auto o = object(data);
        auto receivedToken = QByteArray::fromHex(o["token"].toString().toLatin1());
        if (o["version"].toInt() != 1 || receivedToken.size() != identity.token.size())
            return false;
        unsigned difference = 0;
        for (int i = 0; i < receivedToken.size(); ++i)
            difference |= quint8(receivedToken[i]) ^ quint8(identity.token[i]);
        if (difference)
            return false;
        authenticated = true;
        status.connected = true;
        status.state = "Connected";
        status.detail = "Waiting for audio.";
        socket->write(frame(Message::ready, json({{"version", 1}})));
        return true;
    }
    if (type != Message::audio)
        return false;
    vban::Packet decoded;
    if (vban::decode(reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size()), decoded) !=
            vban::ParseError::none ||
        decoded.format.channels != 2 || decoded.frames != 128 || decoded.format.type != 2)
        return false;
    if (haveSequence && decoded.sequence != lastSequence + 1) {
        callback.flush();
        ++status.dropped;
        packets.clear();
        primed = false;
    }
    haveSequence = true;
    lastSequence = decoded.sequence;
    if (status.rate && status.rate != decoded.format.rate) {
        callback.flush();
        packets.clear();
        primed = false;
    }
    status.rate = decoded.format.rate;
    lastAudio = now();
    ++status.received;
    status.peak = 0;
    std::array<double, 2> peak{};
    for (size_t i = 0; i < decoded.samples.size(); ++i)
        peak[i % 2] = std::max(peak[i % 2], double(std::abs(decoded.samples[i])));
    status.peak = std::max(peak[0], peak[1]);
    shared.recordMeterPeak(peak);
    if (config.mode == OutputMode::callback) {
        callback.feed(decoded.samples.data(), decoded.frames, decoded.format.rate);
        return true;
    }
    auto packet = data;
    std::memset(packet.data() + 8, 0, 16);
    auto name = config.stream.toLatin1();
    std::memcpy(packet.data() + 8, name.constData(), size_t(name.size()));
    packets.push_back(std::move(packet));
    const auto maximum = std::max<size_t>(8, size_t(status.rate) * size_t(config.bufferMs + 100) / 128000);
    if (packets.size() > maximum) {
        const size_t target = std::max<size_t>(1, size_t(status.rate) * config.bufferMs / 128000);
        while (packets.size() > target) {
            packets.pop_front();
            ++status.dropped;
        }
        primed = false;
    }
    return true;
}
void Receiver::tick() {
    if (config.mode == OutputMode::callback) {
        const auto output = callback.poll();
        status.callbackCalls = output.calls;
        status.callbackFrames = output.frames;
        status.callbackRate = output.rate;
        status.callbackDropped = output.dropped;
        status.underruns = output.underruns;
        status.queuedMs = output.queuedMs;
        status.detail = output.detail;
        shared.set(status);
    }
    if (socket && !authenticated && now() - connectionAt > 5000) {
        disconnectPeer("Pairing timed out.");
        return;
    }
    if (!authenticated)
        return;
    if (now() - lastAudio > 2000) {
        disconnectPeer("Audio stopped; discarded queued audio.");
        return;
    }
    if (config.mode == OutputMode::vban) {
        const auto t = clock.nsecsElapsed();
        const size_t target = std::max<size_t>(1, size_t(status.rate) * config.bufferMs / 128000);
        if (!primed && status.rate && packets.size() >= target) {
            primed = true;
            due = t;
        }
        if (primed && status.rate) {
            const qint64 interval = 128000000000LL / status.rate;
            if (t - due > 20000000) {
                due = t;
                ++status.underruns;
            }
            int budget = 8;
            while (t >= due && budget--) {
                if (packets.empty()) {
                    primed = false;
                    ++status.underruns;
                    break;
                }
                auto packet = std::move(packets.front());
                packets.pop_front();
                for (size_t b = 0; b < 4; ++b)
                    packet[qsizetype(24 + b)] = char(sequence >> (b * 8));
                ++sequence;
                if (local->writeDatagram(packet, QHostAddress::LocalHost, config.vbanPort) == packet.size())
                    ++status.sent;
                else
                    ++status.udpErrors;
                const auto adjustment = std::clamp(
                    (double(packets.size()) - double(target)) / double(target) * 0.001, -0.002, 0.002);
                due += qint64(double(interval) * (1.0 - adjustment));
            }
        }
        status.queuedMs = status.rate ? int(packets.size() * 128000 / status.rate) : 0;
        status.state = primed ? "Streaming" : "Buffering";
        status.detail = "Local VBAN source 127.0.0.1; VoiceMeeter reception not confirmed.";
    } else {
        status.state = "Receiving — Callback";
    }
    if (now() - lastAck >= 250 && socket) {
        if (socket->bytesToWrite() > 8192) {
            disconnectPeer("Sender stopped reading status.");
            return;
        }
        socket->write(frame(Message::ack, json({{"received", qint64(status.received)},
                                                {"sent", qint64(status.sent)},
                                                {"udpErrors", qint64(status.udpErrors)},
                                                {"queueMs", status.queuedMs},
                                                {"underruns", qint64(status.underruns)}})));
        lastAck = now();
    }
    shared.set(status);
}
void Receiver::advertise() {
    int budget = 32;
    while (discovery->hasPendingDatagrams() && budget--) {
        auto p = discovery->receiveDatagram(128);
        const QByteArray prefix = "ELKA_AUDIOLINK_DISCOVER_V1:";
        if (!p.data().startsWith(prefix) || p.data().size() > prefix.size() + 32)
            continue;
        auto response = json({{"service", "ElkaAudioLink/1"},
                              {"name", config.name},
                              {"fingerprint", identity.fingerprint()},
                              {"port", int(serverPort())},
                              {"nonce", QString::fromLatin1(p.data().mid(prefix.size()))}});
        discovery->writeDatagram(response, p.senderAddress(), p.senderPort());
    }
}
} // namespace linkaudio
