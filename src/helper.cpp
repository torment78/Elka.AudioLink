// SPDX-License-Identifier: GPL-2.0-or-later
#include "network.hpp"
#include "style.hpp"
#include <QApplication>
#include <QClipboard>
#include <QStandardPaths>
#include <QLockFile>
#include <QSettings>
#include <QDir>
#include <QThread>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QProgressBar>
#include <QMessageBox>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QTemporaryDir>
#include <QScrollArea>
#include <QFile>
using namespace linkaudio;
class Window : public QWidget {
    SharedStatus shared;
    QThread worker;
    Receiver *receiver;
    QSettings settings;
    QLineEdit *name, *stream;
    QSpinBox *port, *vbanPort, *buffer;
    QLabel *state, *detail, *counts, *addresses;
    QProgressBar *meter;
    QString pairingCode;

  public:
    explicit Window(const QString &root) : settings(root + "/receiver.ini", QSettings::IniFormat) {
        auto identity = loadIdentity(root);
        pairingCode = identity.pairing();
        setWindowTitle("Elka AudioLink Receiver");
        setStyleSheet(linkaudio::style());
        resize(680, 730);
        setMinimumWidth(580);
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *body = new QWidget;
        scroll->setWidget(body);
        outer->addWidget(scroll);
        auto *layout = new QVBoxLayout(body);
        layout->setContentsMargins(24, 20, 24, 20);
        layout->setSpacing(12);
        auto *title = new QLabel("Elka AudioLink");
        title->setObjectName("title");
        layout->addWidget(title);
        auto *sub = new QLabel("RECEIVER  /  Deliver the OBS monitor mix to local VBAN");
        sub->setObjectName("subtitle");
        layout->addWidget(sub);
        auto *g = new QGroupBox("This receiver");
        auto *f = new QFormLayout(g);
        f->setSpacing(10);
        name = new QLineEdit(settings.value("name", QHostInfo::localHostName()).toString());
        name->setMaxLength(64);
        f->addRow("Name", name);
        port = new QSpinBox;
        port->setRange(1, 65535);
        port->setValue(settings.value("port", defaultPort).toInt());
        f->addRow("TCP port", port);
        auto *copy = new QPushButton("Copy pairing code");
        f->addRow("Pair once in OBS", copy);
        connect(copy, &QPushButton::clicked, this, [this, copy] {
            QApplication::clipboard()->setText(pairingCode);
            copy->setText("Copied — paste into OBS");
            QTimer::singleShot(3000, copy, [copy] { copy->setText("Copy pairing code"); });
        });
        addresses = new QLabel;
        addresses->setWordWrap(true);
        addresses->setTextInteractionFlags(Qt::TextSelectableByMouse);
        f->addRow("Local addresses", addresses);
        layout->addWidget(g);
        auto *output = new QGroupBox("VoiceMeeter on this PC");
        auto *of = new QFormLayout(output);
        stream = new QLineEdit(settings.value("stream", "AudioLink").toString());
        stream->setMaxLength(16);
        of->addRow("VBAN stream name", stream);
        vbanPort = new QSpinBox;
        vbanPort->setRange(1, 65535);
        vbanPort->setValue(settings.value("vbanPort", 6980).toInt());
        of->addRow("VBAN UDP port", vbanPort);
        buffer = new QSpinBox;
        buffer->setRange(20, 200);
        buffer->setSuffix(" ms");
        buffer->setValue(settings.value("buffer", 40).toInt());
        of->addRow("Receive buffer", buffer);
        auto *hint =
            new QLabel("In VoiceMeeter, enable incoming VBAN with the same stream name, sender IP 127.0.0.1, "
                       "and the port above. AudioLink sends only to this PC's loopback address.");
        hint->setWordWrap(true);
        of->addRow(hint);
        layout->addWidget(output);
        auto *buttons = new QHBoxLayout;
        auto *start = new QPushButton("Start / apply");
        start->setObjectName("primary");
        auto *stop = new QPushButton("Stop");
        buttons->addWidget(start);
        buttons->addWidget(stop);
        layout->addLayout(buttons);
        auto *sg = new QGroupBox("Audio arriving here");
        auto *sl = new QVBoxLayout(sg);
        state = new QLabel("Starting");
        meter = new QProgressBar;
        meter->setRange(0, 100);
        meter->setFormat("Received audio level");
        detail = new QLabel;
        detail->setWordWrap(true);
        counts = new QLabel;
        counts->setWordWrap(true);
        sl->addWidget(state);
        sl->addWidget(meter);
        sl->addWidget(detail);
        sl->addWidget(counts);
        layout->addWidget(sg);
        auto *note =
            new QLabel("Keep this helper running while receiving. Local VBAN sent confirms Windows accepted "
                       "the packets; check VoiceMeeter's meter and listen to confirm reception.");
        note->setWordWrap(true);
        note->setObjectName("subtitle");
        layout->addWidget(note);
        receiver = new Receiver(std::move(identity), shared);
        receiver->moveToThread(&worker);
        connect(&worker, &QThread::finished, receiver, &QObject::deleteLater);
        worker.start();
        connect(start, &QPushButton::clicked, this, [this] { begin(); });
        connect(stop, &QPushButton::clicked, this, [this] {
            QMetaObject::invokeMethod(receiver, [this] { receiver->stop(); }, Qt::QueuedConnection);
        });
        auto *timer = new QTimer(this);
        timer->setInterval(250);
        connect(timer, &QTimer::timeout, this, [this] {
            auto s = shared.get();
            state->setText(s.state);
            detail->setText(s.detail);
            meter->setValue(int(std::clamp(s.peak, 0.0, 1.0) * 100));
            counts->setText(
                QString("Audio received: %1 packets   •   Local VBAN sent: %2\nUDP errors: %3   •   Buffer: "
                        "%4 ms   •   Discarded: %5\nBuffer underruns: %6   •   Sample rate: %7 Hz")
                    .arg(s.received)
                    .arg(s.sent)
                    .arg(s.udpErrors)
                    .arg(s.queuedMs)
                    .arg(s.dropped)
                    .arg(s.underruns)
                    .arg(s.rate));
        });
        timer->start();
        begin();
    }
    ~Window() override {
        QMetaObject::invokeMethod(receiver, [this] { receiver->stop(); }, Qt::BlockingQueuedConnection);
        worker.quit();
        worker.wait();
    }
    void begin() {
        ReceiverConfig c{name->text().trimmed(), stream->text(), quint16(port->value()),
                         quint16(vbanPort->value()), buffer->value()};
        QString error;
        if (!validLocalConfig(c, error)) {
            QMessageBox::warning(this, "Receiver settings", error);
            return;
        }
        bool ok = false;
        QMetaObject::invokeMethod(
            receiver, [&] { ok = receiver->begin(c, error); }, Qt::BlockingQueuedConnection);
        if (!ok) {
            QMessageBox::warning(this, "Cannot start receiver", error);
            return;
        }
        settings.setValue("name", c.name);
        settings.setValue("port", c.port);
        settings.setValue("stream", c.stream);
        settings.setValue("vbanPort", c.vbanPort);
        settings.setValue("buffer", c.bufferMs);
        settings.sync();
        if (settings.status() != QSettings::NoError)
            QMessageBox::warning(this, "Settings",
                                 "Receiver is running, but its settings could not be saved.");
        QStringList ips;
        for (const auto &a : QNetworkInterface::allAddresses())
            if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback())
                ips.append(a.toString() + ":" + QString::number(c.port));
        addresses->setText(ips.isEmpty() ? "No LAN address available" : ips.join("   "));
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("ElkaSoft");
    app.setApplicationName("Elka AudioLink");
    app.setApplicationVersion("0.1.0");
    const auto args = app.arguments();
    const int preview = args.indexOf("--preview");
    QTemporaryDir previewRoot;
    const auto root = preview >= 0 ? previewRoot.path()
                                   : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(root);
    QLockFile lock(root + "/receiver.lock");
    if (!lock.tryLock(100)) {
        QMessageBox::information(nullptr, "Elka AudioLink",
                                 "The receiver is already running for this Windows user.");
        return 0;
    }
    try {
        Window window(root);
        window.show();
        if (preview >= 0 && preview + 1 < args.size())
            QTimer::singleShot(1000, &window, [&] {
                window.grab().save(args[preview + 1]);
                app.quit();
            });
        return app.exec();
    } catch (const std::exception &e) {
        QMessageBox::critical(nullptr, "Elka AudioLink", QString::fromUtf8(e.what()));
        return 1;
    }
}
