// SPDX-License-Identifier: GPL-2.0-or-later
#include "protocol.hpp"
#include <QJsonDocument>
#include <QtEndian>
#include <cstring>
namespace linkaudio {
QByteArray json(const QJsonObject &o) {
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
QByteArray frame(Message type, const QByteArray &payload) {
    if (payload.size() + 1 > maxMessage)
        return {};
    QByteArray result(5, 0);
    qToBigEndian<quint32>(quint32(payload.size() + 1), result.data());
    result[4] = char(type);
    result += payload;
    return result;
}
bool Parser::feed(const QByteArray &bytes, const std::function<bool(Message, const QByteArray &)> &take) {
    if (bytes.size() > 65536 || pending.size() + bytes.size() > 65536)
        return false;
    pending += bytes;
    while (pending.size() >= 4) {
        auto length = qFromBigEndian<quint32>(pending.constData());
        if (length < 1 || length > maxMessage)
            return false;
        if (pending.size() < qsizetype(length + 4))
            return true;
        auto type = quint8(pending[4]);
        if (type < 1 || type > 5)
            return false;
        auto payload = pending.mid(5, length - 1);
        pending.remove(0, length + 4);
        if (!take(Message(type), payload))
            return false;
    }
    return true;
}
void AudioQueue::push(const float *samples, size_t frames, uint32_t rate) {
    if (frames != 128) {
        ++dropped;
        return;
    }
    auto *block = ring.write_slot();
    if (!block) {
        ++dropped;
        return;
    }
    std::memcpy(block->samples.data(), samples, 256 * sizeof(float));
    block->rate = rate;
    ring.publish();
}
bool validLocalConfig(const ReceiverConfig &c, QString &error) {
    if (c.name.trimmed().isEmpty() || c.name.size() > 64)
        error = "Enter a receiver name (1-64 characters).";
    else if (!c.port || (c.mode == OutputMode::vban && !c.vbanPort))
        error = "Ports must be between 1 and 65535.";
    else if (c.mode == OutputMode::vban && !vban::valid_stream_name(c.stream.toStdString()))
        error = "VBAN stream name must be 1-16 printable ASCII characters.";
    else if (c.bufferMs < 20 || c.bufferMs > 200)
        error = "Receive buffer must be 20-200 ms.";
    else if (c.mode != OutputMode::vban && c.mode != OutputMode::callback)
        error = "Choose VBAN or Callback output.";
    else if (c.mode == OutputMode::callback &&
             ((c.target != CallbackTarget::strips && c.target != CallbackTarget::buses) ||
              !VmLayout::forType(c.vmType).count(c.target) || !c.destinations ||
              (c.destinations >> VmLayout::forType(c.vmType).count(c.target))))
        error = "Detect VoiceMeeter and select at least one available destination.";
    else {
        error.clear();
        return true;
    }
    return false;
}
} // namespace linkaudio
