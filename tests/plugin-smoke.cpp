// SPDX-License-Identifier: GPL-2.0-or-later
#include "network.hpp"
#include "mock-vm.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QAction>
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QThread>
#include <QNetworkDatagram>
#include <QDir>
#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>
#include <iostream>
#include <cstdio>
#include "frontend-stub.hpp"
using namespace linkaudio;
static void check(bool ok, const char *m) {
    if (!ok)
        throw std::runtime_error(m);
}
static const char *name(void *) {
    return "AudioLink test audio";
}
static void *create(obs_data_t *, obs_source_t *s) {
    return s;
}
static void destroy(void *) {}
int main(int argc, char **argv) {
    try {
        qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &m) {
            fprintf(stderr, "Qt: %s\n", m.toUtf8().constData());
            fflush(stderr);
        });
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "minimal:enable_fonts");
        qputenv("QT_STYLE_OVERRIDE", "Fusion");
        QApplication app(argc, argv);
        check(argc == 4 || (argc == 5 && QByteArray(argv[4]) == "--callback"),
              "DLL, data, screenshot, optional --callback arguments");
        const bool direct = argc == 5;
        QTemporaryDir root;
        QWidget mainWindow;
        auto *frontend = new TestFrontend(mainWindow);
        obs_frontend_set_callbacks_internal(frontend);
        check(obs_startup("en-US", root.path().toUtf8().constData(), nullptr), "OBS starts");
        obs_audio_info ai{};
        ai.samples_per_sec = 48000;
        ai.speakers = SPEAKERS_STEREO;
        check(obs_reset_audio(&ai), "OBS audio starts");
        const auto dll = QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[1])).toUtf8(),
                   dataPath = QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[2])).toUtf8();
        obs_module_t *module = nullptr;
        check(obs_open_module(&module, dll.constData(), dataPath.constData()) == MODULE_SUCCESS,
              "Plugin DLL opens");
        check(obs_init_module(module), "Plugin DLL initializes");
        check(frontend->action, "Tools menu exists");
        frontend->action->trigger();
        app.processEvents();
        QDialog *panel = nullptr;
        for (QWidget *w : app.topLevelWidgets())
            if (w->windowTitle() == "Elka AudioLink - OBS Sender")
                panel = qobject_cast<QDialog *>(w);
        check(panel, "Sender panel opens");
        auto identity = loadIdentity(root.path() + "/receiver");
        SharedStatus shared;
        auto fake = std::make_unique<MockVm>();
        auto *vm = fake.get();
        Receiver receiver(identity, shared, std::move(fake));
        QUdpSocket sink;
        check(sink.bind(QHostAddress::LocalHost, 0), "VBAN test sink");
        QTcpServer temp;
        check(temp.listen(QHostAddress::LocalHost, 0), "TCP test port");
        auto port = temp.serverPort();
        temp.close();
        ReceiverConfig config{"Plugin test receiver", "PluginReturn", port, sink.localPort(), 30};
        if (direct) {
            config.mode = OutputMode::callback;
            config.vmType = 3;
            config.target = CallbackTarget::buses;
            config.destinations = 1;
        }
        QString error;
        check(receiver.begin(config, error, false), qPrintable(error));
        panel->findChild<QLineEdit *>("host")->setText("127.0.0.1");
        panel->findChild<QLineEdit *>("pairing")->setText(identity.pairing());
        panel->findChild<QSpinBox *>("port")->setValue(port);
        panel->findChild<QPushButton *>("primary")->click();
        obs_source_info si{};
        si.id = "link_test";
        si.type = OBS_SOURCE_TYPE_INPUT;
        si.output_flags = OBS_SOURCE_AUDIO;
        si.get_name = name;
        si.create = create;
        si.destroy = destroy;
        obs_register_source(&si);
        auto *source = obs_source_create("link_test", "Monitored test", nullptr, nullptr);
        check(source, "test source");
        obs_source_inc_active(source);
        obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_MONITOR_ONLY);
        obs_source_set_volume(source, 0.5f);
        std::array<float, 960> data;
        data.fill(0.4f);
        auto base = os_gettime_ns();
        QElapsedTimer timer;
        timer.start();
        int produced = 0, matching = 0;
        qint64 rendered = 0;
        while (timer.elapsed() < 2400) {
            while (produced * 10 <= timer.elapsed()) {
                obs_source_audio audio{};
                audio.data[0] = reinterpret_cast<uint8_t *>(data.data());
                audio.frames = 480;
                audio.format = AUDIO_FORMAT_FLOAT;
                audio.speakers = SPEAKERS_STEREO;
                audio.samples_per_sec = 48000;
                audio.timestamp = base + uint64_t(produced++) * 10000000;
                obs_source_output_audio(source, &audio);
            }
            app.processEvents();
            if (direct)
                while (rendered + 256 <= timer.nsecsElapsed() * 48000 / 1000000000) {
                    vm->render();
                    rendered += 256;
                    if (std::abs(vm->output[0][255] - .21f) < .002f &&
                        std::abs(vm->output[1][255] - .22f) < .002f)
                        ++matching;
                }
            while (sink.hasPendingDatagrams()) {
                auto dg = sink.receiveDatagram();
                vban::Packet p;
                check(vban::decode(reinterpret_cast<const uint8_t *>(dg.data().constData()),
                                   size_t(dg.data().size()), p) == vban::ParseError::none,
                      "plugin VBAN packet valid");
                if (std::abs(p.samples[0] - 0.2f) < 0.001f)
                    ++matching;
            }
            QThread::msleep(1);
        }
        auto observed = shared.get();
        std::cerr << "Receiver: " << observed.state.toStdString() << "; packets=" << observed.received
                  << "; sent=" << observed.sent << "; level=" << observed.peak << "; matching=" << matching
                  << "; " << observed.detail.toStdString() << '\n';
        check(panel->grab().save(QString::fromLocal8Bit(argv[3])), "sender UI screenshot saved");
        check(matching > 100, "actual plugin monitored audio crosses TLS and selected output");
        if (direct)
            check(observed.sent == 0 && observed.callbackFrames > 48000,
                  "callback path active with no local VBAN");
        check(shared.get().received > 100, "receiver acknowledges plugin audio");
        obs_source_dec_active(source);
        obs_source_release(source);
        obs_wait_for_destroy_queue();
        receiver.stop();
        frontend->on_event(OBS_FRONTEND_EVENT_EXIT);
        obs_shutdown();
        app.processEvents();
        obs_frontend_set_callbacks_internal(nullptr);
        std::cout << "PASS: actual OBS DLL load, Tools UI, settings apply, monitored PCM through TLS/"
                  << (direct ? "callback" : "local VBAN") << " (" << matching
                  << " matching blocks), shutdown\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
