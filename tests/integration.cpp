// SPDX-License-Identifier: GPL-2.0-or-later
#include "network.hpp"
#include "vban-transmitter.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QNetworkDatagram>
#include <QThread>
#include <iostream>
#include <stdexcept>
using namespace linkaudio;
static void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> static bool until(F f, int ms = 4000) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents();
        if (f())
            return true;
        QThread::msleep(1);
    }
    return f();
}
static quint16 freePort() {
    QTcpServer s;
    check(s.listen(QHostAddress::LocalHost, 0), "test TCP port");
    return s.serverPort();
}
int main(int argc, char **argv) {
    try {
        QCoreApplication app(argc, argv);
        Parser parser;
        int messages = 0;
        auto message = frame(Message::hello, "test");
        for (char b : message)
            check(parser.feed(QByteArray(1, b),
                              [&](Message m, const QByteArray &p) {
                                  check(m == Message::hello && p == "test", "split frame content");
                                  ++messages;
                                  return true;
                              }),
                  "split frame parse");
        check(messages == 1, "split frame delivered once");
        check(parser.feed(message + message,
                          [&](Message, const QByteArray &) {
                              ++messages;
                              return true;
                          }),
              "coalesced frames");
        check(messages == 3, "coalesced frame count");
        check(!parser.feed(QByteArray::fromHex("ffffffff"), [](Message, const QByteArray &) { return true; }),
              "oversized frame rejected");
        parser.clear();
        check(
            !parser.feed(QByteArray::fromHex("0000000100"), [](Message, const QByteArray &) { return true; }),
            "unknown type rejected");
        QTemporaryDir root;
        check(root.isValid(), "temporary identity folder");
        const auto id = loadIdentity(root.path());
        const auto restored = loadIdentity(root.path());
        check(id.pairing() == restored.pairing(), "identity persists and DPAPI decrypts");
        QString fp;
        QByteArray token;
        check(decodePairing(id.pairing(), fp, token), "pairing code parses");
        check(!decodePairing("ELKA1-not-a-code", fp, token), "invalid pairing code rejected");
        QUdpSocket sink;
        check(sink.bind(QHostAddress::LocalHost, 0), "local VBAN sink");
        SharedStatus rx, tx;
        Receiver receiver(id, rx);
        ReceiverConfig rc{"Integration receiver", "TestReturn", freePort(), sink.localPort(), 30};
        QString error;
        check(receiver.begin(rc, error), qPrintable(error));
        AudioQueue queue;
        Sender sender(queue, tx);
        sender.start();
        SenderConfig sc{"127.0.0.1", id.pairing(), rc.port, true, 60};
        sender.configure(sc);
        check(until([&] { return tx.get().connected; }), "TLS pin and pairing authenticate");
        check(until([&] { return !tx.found().isEmpty(); }),
              "LAN discovery finds receiver without carrying audio");
        std::array<float, 256> samples;
        samples.fill(0.25f);
        QElapsedTimer streamClock;
        streamClock.start();
        qint64 produced = 0;
        int received = 0;
        quint32 lastSeq = 0;
        bool previous = false;
        check(until(
                  [&] {
                      const auto target = streamClock.nsecsElapsed() * 48000 / 1000000000;
                      while (produced + 128 <= target) {
                          queue.push(samples.data(), 128, 48000);
                          produced += 128;
                      }
                      while (sink.hasPendingDatagrams()) {
                          auto datagram = sink.receiveDatagram();
                          vban::Packet packet;
                          check(datagram.senderAddress().isLoopback(), "VBAN stays on loopback");
                          check(vban::decode(reinterpret_cast<const uint8_t *>(datagram.data().constData()),
                                             size_t(datagram.data().size()),
                                             packet) == vban::ParseError::none,
                                "valid local VBAN");
                          check(packet.name == "TestReturn" && packet.format.channels == 2 &&
                                    packet.format.rate == 48000,
                                "local VBAN format and name");
                          check(std::abs(packet.samples[0] - 0.25f) < 0.000001f,
                                "PCM preserved across TLS and local VBAN");
                          if (previous)
                              check(packet.sequence == lastSeq + 1, "local packet sequence continuous");
                          lastSeq = packet.sequence;
                          previous = true;
                          ++received;
                      }
                      return received >= 250 && tx.get().received >= 100;
                  },
                  5000),
              "audio transfer and receiver acknowledgement");
        check(rx.get().udpErrors == 0, "no local UDP errors");
        std::cout << "TLS -> local VBAN: " << received << " packets; acknowledged " << tx.get().received
                  << "\n";
        sc.enabled = false;
        sender.configure(sc);
        check(until([&] { return !rx.get().connected; }), "disconnect clears receiver session");
        while (sink.hasPendingDatagrams())
            sink.receiveDatagram();
        until([&] { return false; }, 120);
        check(!sink.hasPendingDatagrams(), "no stale audio after disconnect");
        sc.enabled = true;
        sc.pairing = id.pairing();
        sc.pairing[sc.pairing.size() - 1] = sc.pairing.endsWith('0') ? '1' : '0';
        sender.configure(sc);
        until([&] { return false; }, 1200);
        check(!rx.get().connected && !tx.get().connected, "wrong pairing token rejected");
        sc.pairing = id.pairing();
        sc.pairing[6] = sc.pairing[6] == '0' ? '1' : '0';
        sender.configure(sc);
        until([&] { return false; }, 1200);
        check(!rx.get().connected && !tx.get().connected, "wrong certificate pin rejected");
        sc.pairing = id.pairing();
        sender.configure(sc);
        check(until([&] { return tx.get().connected; }), "reconnect with correct identity");
        check(until([&] { return !rx.get().connected; }, 3500),
              "idle audio timeout clears receiver before reconnect");
        sender.stop();
        receiver.stop();
        std::cout << "PASS: framing, validation, protected identity, discovery, TLS pinning, token "
                     "rejection, PCM, acknowledgements, reconnect, idle cleanup\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
