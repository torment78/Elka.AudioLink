// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "network.hpp"
#include "style.hpp"
#include "level-meter.hpp"
#include "callback-settings.hpp"
#include <QTabWidget>
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
#include <QSystemTrayIcon>
#include <QMenu>
#include <QCloseEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QChronoTimer>
#include <QPainter>
#include <optional>
#include <QMessageBox>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QTemporaryDir>
#include <QScrollArea>
#include <QFile>
namespace linkaudio {
class ReceiverWindow : public QWidget {
    SharedStatus shared;
    QThread worker;
    Receiver *receiver;
    QSettings settings;
    QLineEdit *name, *stream;
    QSpinBox *port, *vbanPort, *buffer;
    QLabel *state, *detail, *counts, *addresses;
    LevelMeter *meter;
    QChronoTimer *meterTimer = nullptr;
    QElapsedTimer meterClock;
    QSystemTrayIcon *tray = nullptr;
    std::optional<ReceiverConfig> applied;
    QString pairingCode;
    QString startupError;
    QTabWidget *outputTabs;
    CallbackSettings *callbackSettings;
    void detectVoiceMeeter() {
        VmInfo info;
        QMetaObject::invokeMethod(
            receiver, [&] { info = receiver->detectVoiceMeeter(); }, Qt::BlockingQueuedConnection);
        callbackSettings->showInfo(info);
    }

    static QIcon applicationIcon() {
        QIcon icon;
        for (int size : {16, 20, 24, 32, 48, 64}) {
            QPixmap pix(size, size);
            pix.fill(Qt::transparent);
            QPainter p(&pix);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(size / 32.0, size / 32.0);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#166b7c"));
            p.drawRoundedRect(QRectF(1, 1, 30, 30), 8, 8);
            p.setPen(QPen(QColor("#e8fff9"), 3, Qt::SolidLine, Qt::RoundCap));
            for (int i = 0; i < 4; ++i) {
                const double height = i == 1 ? 17 : i == 2 ? 11 : 6;
                p.drawLine(QPointF(8.5 + i * 5, 16 - height / 2), QPointF(8.5 + i * 5, 16 + height / 2));
            }
            p.end();
            icon.addPixmap(pix);
        }
        return icon;
    }
    void updateMeterActivity() {
        if (!meterTimer)
            return;
        const bool active = isVisible() && !isMinimized();
        if (active && !meterTimer->isActive()) {
            shared.takeMeterPeak(); // Discard peaks accumulated while hidden.
            meter->reset();
            meterClock.start();
            meterTimer->start();
        } else if (!active) {
            meterTimer->stop();
        }
    }
    void updateAddresses(const ReceiverConfig &c) {
        QStringList ips;
        for (const auto &a : QNetworkInterface::allAddresses())
            if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback())
                ips.append(a.toString() + ":" + QString::number(c.port));
        addresses->setText(ips.isEmpty() ? "No LAN address available" : ips.join("   "));
    }

  protected:
    void closeEvent(QCloseEvent *event) override {
        if (tray && QSystemTrayIcon::isSystemTrayAvailable()) {
            event->ignore();
            hide();
        } else {
            event->accept();
            qApp->quit();
        }
    }
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event);
        updateMeterActivity();
    }
    void hideEvent(QHideEvent *event) override {
        QWidget::hideEvent(event);
        updateMeterActivity();
    }
    void changeEvent(QEvent *event) override {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::WindowStateChange)
            updateMeterActivity();
    }

  public:
    explicit ReceiverWindow(const QString &root, bool trayEnabled = true,
                            std::unique_ptr<VmApi> api = makeVmApi())
        : settings(root + "/receiver.ini", QSettings::IniFormat) {
        auto identity = loadIdentity(root);
        pairingCode = identity.pairing();
        setWindowTitle("Elka AudioLink Receiver");
        setWindowIcon(applicationIcon());
        setStyleSheet(linkaudio::style());
        resize(680, 850);
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
        auto *sub = new QLabel("RECEIVER  /  Deliver the OBS monitor mix to VoiceMeeter");
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
        auto *outputLayout = new QVBoxLayout(output);
        outputTabs = new QTabWidget;
        outputTabs->setObjectName("outputTabs");
        auto *vbanPage = new QWidget;
        auto *of = new QFormLayout(vbanPage);
        stream = new QLineEdit(settings.value("stream", "AudioLink").toString());
        stream->setMaxLength(16);
        of->addRow("VBAN stream name", stream);
        vbanPort = new QSpinBox;
        vbanPort->setRange(1, 65535);
        vbanPort->setValue(settings.value("vbanPort", 6980).toInt());
        of->addRow("VBAN UDP port", vbanPort);
        auto *hint =
            new QLabel("In VoiceMeeter, enable incoming VBAN with the same stream name, sender IP 127.0.0.1, "
                       "and the port above. AudioLink sends only to this PC's loopback address.");
        hint->setWordWrap(true);
        of->addRow(hint);
        outputTabs->addTab(vbanPage, "VBAN");
        callbackSettings = new CallbackSettings(settings);
        outputTabs->addTab(callbackSettings, "Callback");
        outputTabs->setCurrentIndex(settings.value("outputMode", 0).toInt() == 1 ? 1 : 0);
        outputLayout->addWidget(outputTabs);
        auto *bufferForm = new QFormLayout;
        buffer = new QSpinBox;
        buffer->setRange(20, 200);
        buffer->setSuffix(" ms");
        buffer->setValue(settings.value("buffer", 40).toInt());
        bufferForm->addRow("Receive buffer", buffer);
        outputLayout->addLayout(bufferForm);
        auto *applyHint = new QLabel(
            "Choose a tab and destinations, then press Start / apply to change the active output.");
        applyHint->setWordWrap(true);
        outputLayout->addWidget(applyHint);
        layout->addWidget(output);
        auto *buttons = new QHBoxLayout;
        auto *start = new QPushButton("Start / apply");
        start->setObjectName("primary");
        auto *stop = new QPushButton("Stop");
        buttons->addWidget(start);
        buttons->addWidget(stop);
        buttons->setContentsMargins(24, 8, 24, 16);
        outer->addLayout(buttons);
        auto *sg = new QGroupBox("Audio arriving here");
        auto *sl = new QVBoxLayout(sg);
        state = new QLabel("Starting");
        meter = new LevelMeter;
        meter->setObjectName("receivedLevel");
        detail = new QLabel;
        detail->setWordWrap(true);
        counts = new QLabel;
        counts->setWordWrap(true);
        sl->addWidget(state);
        sl->addWidget(meter);
        sl->addWidget(detail);
        sl->addWidget(counts);
        layout->insertWidget(2, sg);
        auto *note = new QLabel(
            "Close this window to keep receiving in the system tray. Right-click the AudioLink "
            "tray icon to reopen settings, refresh reception, or shut down. Check VoiceMeeter's meters "
            "and listen to confirm the final output.");
        note->setWordWrap(true);
        note->setObjectName("subtitle");
        layout->addWidget(note);
        receiver = new Receiver(std::move(identity), shared, std::move(api));
        receiver->moveToThread(&worker);
        connect(&worker, &QThread::finished, receiver, &QObject::deleteLater);
        worker.start();
        connect(callbackSettings->detect, &QPushButton::clicked, this, [this] { detectVoiceMeeter(); });
        connect(outputTabs, &QTabWidget::currentChanged, this, [this](int tab) {
            if (tab == 1)
                detectVoiceMeeter();
        });
        connect(start, &QPushButton::clicked, this, [this] { begin(); });
        connect(stop, &QPushButton::clicked, this, [this] {
            startupError.clear();
            QMetaObject::invokeMethod(receiver, [this] { receiver->stop(); }, Qt::QueuedConnection);
        });
        if (trayEnabled) {
            tray = new QSystemTrayIcon(windowIcon(), this);
            tray->setObjectName("receiverTray");
            tray->setToolTip("Elka AudioLink Receiver");
            auto *menu = new QMenu(this);
            auto *open = menu->addAction("Open settings");
            open->setObjectName("trayOpen");
            menu->setDefaultAction(open);
            connect(open, &QAction::triggered, this, [this] { showSettings(); });
            auto *refresh = menu->addAction("Refresh / reconnect");
            refresh->setObjectName("trayRefresh");
            refresh->setToolTip("Restart reception using the last applied settings.");
            connect(refresh, &QAction::triggered, this, [this] { restartReception(); });
            menu->addSeparator();
            auto *exit = menu->addAction("Shut down");
            exit->setObjectName("trayShutdown");
            connect(exit, &QAction::triggered, this, [this] { shutdown(); });
            tray->setContextMenu(menu);
            connect(tray, &QSystemTrayIcon::activated, this,
                    [this](QSystemTrayIcon::ActivationReason reason) {
                        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                            showSettings();
                    });
            tray->show();
        }
        meterTimer = new QChronoTimer(this);
        meterTimer->setObjectName("meterRefresh");
        meterTimer->setTimerType(Qt::PreciseTimer);
        meterTimer->setInterval(std::chrono::nanoseconds(16666667));
        connect(meterTimer, &QChronoTimer::timeout, this, [this] {
            const double seconds = meterClock.nsecsElapsed() / 1e9;
            meterClock.restart();
            const auto peak = shared.takeMeterPeak();
            if (shared.get().connected)
                meter->advance(peak, seconds);
            else
                meter->reset();
        });
        auto *timer = new QTimer(this);
        timer->setInterval(250);
        connect(timer, &QTimer::timeout, this, [this] {
            auto s = shared.get();
            if (tray)
                tray->setToolTip("Elka AudioLink — " + s.state);
            if (!isVisible() || isMinimized())
                return;
            state->setText((s.outputMode == OutputMode::callback ? "CALLBACK  /  " : "VBAN  /  ") + s.state);
            detail->setText(startupError.isEmpty() ? s.detail : startupError);
            if (s.outputMode == OutputMode::callback) {
                counts->setText(QString("Audio received: %1 packets   •   Callback delivered: %2 frames\n"
                                        "Buffer: %3 ms   •   Underruns: %4   •   Discarded: %5 frames\nOBS: "
                                        "%6 Hz   •   VoiceMeeter: %7 Hz")
                                    .arg(s.received)
                                    .arg(s.callbackFrames)
                                    .arg(s.queuedMs)
                                    .arg(s.underruns)
                                    .arg(s.callbackDropped)
                                    .arg(s.rate)
                                    .arg(s.callbackRate));
            } else
                counts->setText(
                    QString(
                        "Audio received: %1 packets   •   Local VBAN sent: %2\nUDP errors: %3   •   Buffer: "
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
        begin(false);
    }
    ~ReceiverWindow() override {
        if (tray)
            tray->hide();
        QMetaObject::invokeMethod(receiver, [this] { receiver->stop(); }, Qt::BlockingQueuedConnection);
        worker.quit();
        worker.wait();
    }
    void showSettings() {
        showNormal();
        raise();
        activateWindow();
    }
    void shutdown() {
        meterTimer->stop();
        QMetaObject::invokeMethod(receiver, [this] { receiver->stop(); }, Qt::BlockingQueuedConnection);
        if (tray)
            tray->hide();
        qApp->quit();
    }
    void restartReception() {
        if (!applied)
            return;
        QString error;
        bool ok = false;
        QMetaObject::invokeMethod(
            receiver, [&] { ok = receiver->begin(*applied, error); }, Qt::BlockingQueuedConnection);
        meter->reset();
        updateAddresses(*applied);
        if (!ok) {
            showSettings();
            QMessageBox::warning(this, "Cannot restart receiver", error);
        }
    }
    void begin(bool interactive = true) {
        ReceiverConfig c{name->text().trimmed(), stream->text(), quint16(port->value()),
                         quint16(vbanPort->value()), buffer->value()};
        c.mode = outputTabs->currentIndex() == 1 ? OutputMode::callback : OutputMode::vban;
        if (c.mode == OutputMode::callback) {
            detectVoiceMeeter();
            callbackSettings->configure(c);
        }
        QString error;
        if (!validLocalConfig(c, error)) {
            if (interactive)
                QMessageBox::warning(this, "Receiver settings", error);
            else {
                startupError = error;
                state->setText("Stopped — check output settings");
                detail->setText(error);
            }
            return;
        }
        bool ok = false;
        QMetaObject::invokeMethod(
            receiver, [&] { ok = receiver->begin(c, error); }, Qt::BlockingQueuedConnection);
        if (!ok) {
            if (interactive)
                QMessageBox::warning(this, "Cannot start receiver", error);
            else {
                startupError = error;
                state->setText("Stopped — cannot start receiver");
                detail->setText(error);
            }
            return;
        }
        settings.setValue("name", c.name);
        startupError.clear();
        applied = c;
        settings.setValue("port", c.port);
        settings.setValue("stream", c.stream);
        settings.setValue("vbanPort", c.vbanPort);
        settings.setValue("buffer", c.bufferMs);
        settings.setValue("outputMode", int(c.mode));
        callbackSettings->save(settings);
        settings.sync();
        if (settings.status() != QSettings::NoError)
            QMessageBox::warning(this, "Settings",
                                 "Receiver is running, but its settings could not be saved.");
        updateAddresses(c);
    }
};
} // namespace linkaudio
