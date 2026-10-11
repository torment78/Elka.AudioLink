// SPDX-License-Identifier: GPL-2.0-or-later
#include "receiver-window.hpp"
#include "mock-vm.hpp"
#include <QNetworkDatagram>
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
        QApplication::processEvents();
        if (f())
            return true;
        QThread::msleep(1);
    }
    return f();
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    int result = 1;
    QTimer::singleShot(0, &app, [&] {
        try {
            check(QSystemTrayIcon::isSystemTrayAvailable(), "Windows notification area available");
            SharedStatus peaks;
            peaks.recordMeterPeak({.9, .2});
            peaks.recordMeterPeak({0, .8});
            auto captured = peaks.takeMeterPeak();
            check(captured[0] == .9 && captured[1] == .8, "brief stereo peaks survive quieter blocks");
            check(peaks.takeMeterPeak() == std::array<double, 2>{}, "meter consumes each peak once");
            LevelMeter ballistic;
            ballistic.advance({1, .1}, 1.0 / 60);
            check(ballistic.levels()[0] == 0 && std::abs(ballistic.levels()[1] + 20) < .001,
                  "immediate attack and stereo dBFS scale");
            ballistic.advance({}, .5);
            check(std::abs(ballistic.levels()[0] + 12) < .001, "24 dB/second release");
            ballistic.reset();
            check(ballistic.levels()[0] == -60, "silence resets meter");

            QTemporaryDir root;
            check(root.isValid(), "isolated receiver settings");
            QTcpServer free;
            check(free.listen(QHostAddress::LocalHost, 0), "reserve test TCP port");
            const auto port = free.serverPort();
            free.close();
            QUdpSocket sink;
            check(sink.bind(QHostAddress::LocalHost, 0), "local VBAN test endpoint");
            QSettings settings(root.path() + "/receiver.ini", QSettings::IniFormat);
            settings.setValue("port", port);
            settings.setValue("vbanPort", sink.localPort());
            settings.setValue("name", "Receiver lifecycle test");
            settings.sync();
            auto fake = std::make_unique<MockVm>();
            auto *vm = fake.get();
            ReceiverWindow window(root.path(), true, std::move(fake));
            window.setAttribute(Qt::WA_DontShowOnScreen);
            window.resize(680, 970);
            window.show();
            auto *tabs = window.findChild<QTabWidget *>("outputTabs");
            auto *strips = window.findChild<QRadioButton *>("callbackStrips");
            auto *buses = window.findChild<QRadioButton *>("callbackBuses");
            auto *all = window.findChild<QPushButton *>("callbackAll");
            auto *detect = window.findChild<QPushButton *>("vmDetect");
            auto *routing = dynamic_cast<CallbackSettings *>(tabs->widget(1));
            tabs->setCurrentIndex(1);
            ReceiverConfig draft;
            strips->click();
            all->click();
            routing->configure(draft);
            check(draft.vmType == 3 && draft.destinations == 255, "Potato exposes all eight strips");
            buses->click();
            all->click();
            routing->configure(draft);
            check(draft.destinations == 31 && draft.target == CallbackTarget::buses,
                  "Potato exposes A1-A5 without B buses");
            vm->edition = 2;
            detect->click();
            routing->configure(draft);
            check(draft.destinations == 0, "edition change clears stale selection");
            all->click();
            routing->configure(draft);
            check(draft.destinations == 7, "Banana has three A buses");
            strips->click();
            all->click();
            routing->configure(draft);
            check(draft.destinations == 31, "Banana has five strips");
            vm->edition = 3;
            detect->click();
            buses->click();
            all->click();
            window.findChild<QCheckBox *>("callbackDestination2")->setChecked(false);
            routing->configure(draft);
            check(draft.destinations == 29 && !vm->callback,
                  "arbitrary selection; editing does not activate callback");
            tabs->setCurrentIndex(0);
            auto *tray = window.findChild<QSystemTrayIcon *>("receiverTray");
            auto *refresh = window.findChild<QAction *>("trayRefresh");
            auto *open = window.findChild<QAction *>("trayOpen");
            auto *exit = window.findChild<QAction *>("trayShutdown");
            auto *timer = window.findChild<QChronoTimer *>("meterRefresh");
            auto *meter = dynamic_cast<LevelMeter *>(window.findChild<QWidget *>("receivedLevel"));
            check(tray && refresh && open && exit && timer && meter, "receiver controls available");
            check(tray->isVisible() && timer->isActive(), "tray and visible meter running");
            AudioQueue audio;
            SharedStatus senderStatus;
            Sender sender(audio, senderStatus);
            sender.start();
            sender.configure({"127.0.0.1", loadIdentity(root.path()).pairing(), port, true, 60});
            check(until([&] { return senderStatus.get().connected; }),
                  "sender authenticates to real receiver window");
            std::array<float, 256> samples;
            for (size_t i = 0; i < samples.size(); ++i)
                samples[i] = i % 2 ? .5f : .125f;
            QElapsedTimer clock;
            clock.start();
            qint64 produced = 0;
            QTimer produce;
            produce.setTimerType(Qt::PreciseTimer);
            produce.setInterval(2);
            QObject::connect(&produce, &QTimer::timeout, &window, [&] {
                const auto target = clock.nsecsElapsed() * 48000 / 1000000000;
                while (produced + 128 <= target) {
                    audio.push(samples.data(), 128, 48000);
                    produced += 128;
                }
            });
            produce.start();
            int packets = 0;
            auto drain = [&] {
                while (sink.hasPendingDatagrams()) {
                    sink.receiveDatagram();
                    ++packets;
                }
            };
            check(until([&] {
                      drain();
                      return packets > 40 && meter->levels()[0] > -19 && meter->levels()[1] > -7;
                  }),
                  "received stereo peaks reach visible meter and local VBAN");
            int frames = 0;
            const auto counted = QObject::connect(timer, &QChronoTimer::timeout, &window, [&] { ++frames; });
            until(
                [&] {
                    drain();
                    return false;
                },
                500);
            QObject::disconnect(counted);
            check(frames >= 24 && frames <= 38, "visible meter refreshes near 60 Hz");
            std::cout << "Meter frames in 500 ms: " << frames << '\n';
            if (argc > 1) {
                check(window.grab().save(QString::fromLocal8Bit(argv[1])), "receiver preview saved");
                tabs->setCurrentIndex(1);
                check(window.grab().save(QString::fromLocal8Bit(argv[1]) + ".callback.png"),
                      "callback controls preview saved");
                tabs->setCurrentIndex(0);
                auto *menu = tray->contextMenu();
                menu->ensurePolished();
                menu->resize(menu->sizeHint());
                check(menu->grab().save(QString::fromLocal8Bit(argv[1]) + ".tray.png"),
                      "tray menu preview saved");
            }
            window.close();
            check(!window.isVisible() && tray->isVisible() && !timer->isActive(),
                  "close hides window and pauses meter only");
            const auto before = packets;
            check(until([&] {
                      drain();
                      return packets > before + 100;
                  }),
                  "audio continues while closed to tray");
            refresh->trigger();
            check(!window.isVisible(), "refresh keeps receiver in tray");
            const auto refreshed = packets;
            check(until(
                      [&] {
                          drain();
                          return packets > refreshed + 100;
                      },
                      6000),
                  "tray refresh reconnects and resumes VBAN");
            open->trigger();
            check(window.isVisible() && timer->isActive(), "tray settings reopens window and meter");
            window.showMinimized();
            check(!timer->isActive(), "minimized meter does not run");
            open->trigger();
            check(!window.isMinimized() && timer->isActive(), "settings restores minimized window");
            sender.stop();
            check(until([&] { return meter->levels()[0] == -60; }), "disconnect clears displayed level");
            exit->trigger();
            check(!tray->isVisible() && !timer->isActive(), "shutdown removes tray and stops meter");
            QTcpServer released;
            check(released.listen(QHostAddress::AnyIPv4, port), "shutdown releases receiver TCP port");
            std::cout << "PASS: stereo meter, 60 Hz, close-to-tray audio continuity, reconnect, reopen, "
                         "minimize and shutdown\n";
            result = 0;
        } catch (const std::exception &e) {
            std::cerr << "FAIL: " << e.what() << '\n';
        }
        app.quit();
    });
    app.exec();
    return result;
}
