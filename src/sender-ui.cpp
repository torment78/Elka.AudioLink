// SPDX-License-Identifier: GPL-2.0-or-later
#include "sender-ui.hpp"
#include "style.hpp"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QAbstractItemView>
#include <QScrollArea>
namespace linkaudio {
SenderPanel::SenderPanel(const QString &path, QWidget *parent)
    : QDialog(parent), monitor([this](const float *s, size_t n, uint32_t rate) { audio.push(s, n, rate); }),
      settings(path, QSettings::IniFormat) {
    setWindowTitle("Elka AudioLink - OBS Sender");
    setStyleSheet(linkaudio::style());
    resize(690, 720);
    setMinimumWidth(580);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        qWarning("[AudioLink] Cannot create sender settings directory");
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
    auto *subtitle = new QLabel("OBS monitor mix  →  encrypted TCP  →  local VBAN");
    subtitle->setObjectName("subtitle");
    layout->addWidget(subtitle);
    auto *instructions =
        new QLabel("Choose Monitor Only or Monitor and Output in OBS Advanced Audio Properties.\nThe same "
                   "headphone / monitoring selections feed AudioLink.");
    instructions->setWordWrap(true);
    layout->addWidget(instructions);
    auto *group = new QGroupBox("Receiver");
    auto *form = new QFormLayout(group);
    form->setSpacing(12);
    peers = new QComboBox;
    peers->setObjectName("receivers");
    peers->addItem("Searching the local network…");
    form->addRow("Nearby receivers", peers);
    auto *choose = new QPushButton("Use selected receiver");
    form->addRow("", choose);
    host = new QLineEdit(settings.value("host").toString());
    host->setPlaceholderText("Receiver IPv4 address or hostname");
    host->setObjectName("host");
    form->addRow("Address / fallback", host);
    port = new QSpinBox;
    port->setObjectName("port");
    port->setRange(1, 65535);
    port->setValue(settings.value("port", defaultPort).toInt());
    form->addRow("TCP port", port);
    pairing = new QLineEdit;
    pairing->setObjectName("pairing");
    pairing->setEchoMode(QLineEdit::Password);
    pairing->setPlaceholderText("Paste the receiver's pairing code once");
    form->addRow("Pairing code", pairing);
    try {
        auto secret = settings.value("pairingProtected").toByteArray();
        if (!secret.isEmpty())
            pairing->setText(QString::fromUtf8(unprotect(QByteArray::fromBase64(secret))));
    } catch (const std::exception &) {
        pairing->clear();
    }
    buffer = new QSpinBox;
    buffer->setRange(20, 200);
    buffer->setSuffix(" ms");
    buffer->setValue(settings.value("monitorBuffer", 60).toInt());
    form->addRow("Monitor mix buffer", buffer);
    automatic = new QCheckBox("Connect automatically when OBS opens");
    automatic->setChecked(settings.value("automatic", false).toBool());
    form->addRow("", automatic);
    layout->addWidget(group);
    auto *buttons = new QHBoxLayout;
    auto *start = new QPushButton("Connect and save");
    start->setObjectName("primary");
    auto *stop = new QPushButton("Disconnect");
    buttons->addWidget(start);
    buttons->addWidget(stop);
    layout->addLayout(buttons);
    auto *statusGroup = new QGroupBox("Connection and audio");
    auto *sl = new QVBoxLayout(statusGroup);
    state = new QLabel("Stopped");
    detail = new QLabel;
    detail->setWordWrap(true);
    counts = new QLabel;
    counts->setWordWrap(true);
    meter = new QProgressBar;
    meter->setRange(0, 100);
    meter->setFormat("Monitor mix level");
    sl->addWidget(state);
    sl->addWidget(meter);
    sl->addWidget(detail);
    sl->addWidget(counts);
    layout->addWidget(statusGroup);
    auto *footer =
        new QLabel("Discovery works on the local subnet. Manual addressing also works across a reachable VPN "
                   "or routed network. Avoid routing the received return back into this mix.");
    footer->setWordWrap(true);
    footer->setObjectName("subtitle");
    layout->addWidget(footer);
    monitor.start();
    sender = new Sender(audio, shared);
    sender->moveToThread(&worker);
    connect(&worker, &QThread::finished, sender, &QObject::deleteLater);
    worker.start();
    QMetaObject::invokeMethod(sender, [this] { sender->start(); }, Qt::QueuedConnection);
    connect(choose, &QPushButton::clicked, this, [this] {
        const auto data = peers->currentData().toMap();
        if (data.isEmpty())
            return;
        host->setText(data["host"].toString());
        port->setValue(data["port"].toInt());
    });
    connect(start, &QPushButton::clicked, this, [this] { apply(true); });
    connect(stop, &QPushButton::clicked, this, [this] { apply(false); });
    refresh = new QTimer(this);
    refresh->setInterval(250);
    connect(refresh, &QTimer::timeout, this, [this] { updateStatus(); });
    refresh->start();
    if (automatic->isChecked())
        apply(true);
}
SenderPanel::~SenderPanel() {
    shutdown();
}
void SenderPanel::shutdown() {
    if (stopped)
        return;
    stopped = true;
    refresh->stop();
    monitor.shutdown();
    QMetaObject::invokeMethod(sender, [this] { sender->stop(); }, Qt::BlockingQueuedConnection);
    worker.quit();
    worker.wait();
}
void SenderPanel::apply(bool enabled) {
    qInfo("[AudioLink] Sender settings requested (enabled=%d)", int(enabled));
    SenderConfig c{host->text().trimmed(), pairing->text().trimmed(), quint16(port->value()), enabled,
                   buffer->value()};
    QString fingerprint;
    QByteArray token;
    inputError.clear();
    if (enabled && !decodePairing(c.pairing, fingerprint, token)) {
        inputError = "Copy the pairing code from Elka AudioLink Receiver and paste it here once.";
        updateStatus();
        return;
    }
    try {
        settings.setValue("host", c.host);
        settings.setValue("port", c.port);
        settings.setValue("monitorBuffer", c.monitorBuffer);
        settings.setValue("automatic", automatic->isChecked());
        settings.setValue("pairingProtected", protect(c.pairing.toUtf8()).toBase64());
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            qWarning("[AudioLink] Settings error %d at %s", int(settings.status()),
                     qPrintable(settings.fileName()));
            throw std::runtime_error("Could not save sender settings.");
        }
        monitor.enable_tunnel(enabled, uint32_t(c.monitorBuffer));
        QMetaObject::invokeMethod(sender, [this, c] { sender->configure(c); }, Qt::QueuedConnection);
    } catch (const std::exception &e) {
        inputError = QString::fromUtf8(e.what());
        updateStatus();
        qWarning("[AudioLink] %s", e.what());
    }
}
void SenderPanel::updateStatus() {
    auto s = shared.get();
    state->setText(inputError.isEmpty() ? s.state : "Check settings");
    detail->setText(inputError.isEmpty() ? s.detail : inputError);
    meter->setValue(int(std::clamp(s.peak, 0.0, 1.0) * 100));
    const auto capture = monitor.status(0);
    counts->setText(
        QString("Receiver acknowledged: %1 packets   •   Local VBAN sent: %2\nReceiver UDP errors: %3   •   "
                "Queue: %4 ms   •   Sender drops: %5\nCapture drops: %6   •   Late source frames: %7")
            .arg(s.received)
            .arg(s.sent)
            .arg(s.udpErrors)
            .arg(s.queuedMs)
            .arg(s.dropped)
            .arg(capture.capture_drops)
            .arg(capture.late_audio_frames));
    const auto list = shared.found();
    const auto selected = peers->currentData().toMap()["fingerprint"].toString();
    if (peers->view()->isVisible())
        return;
    peers->clear();
    for (const auto &p : list) {
        QVariantMap data{{"host", p.host}, {"port", p.port}, {"fingerprint", p.fingerprint}};
        peers->addItem(p.name + " — " + p.host + ":" + QString::number(p.port), data);
        if (p.fingerprint == selected)
            peers->setCurrentIndex(peers->count() - 1);
    }
    if (list.isEmpty())
        peers->addItem("No receivers found — use the address above");
}
} // namespace linkaudio
