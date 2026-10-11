// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QString>
namespace linkaudio {
struct Identity {
    QByteArray cert, key, token;
    QString fingerprint() const;
    QString pairing() const;
};
Identity loadIdentity(const QString &directory);
bool decodePairing(const QString &, QString &fingerprint, QByteArray &token);
QByteArray protect(const QByteArray &);
QByteArray unprotect(const QByteArray &);
} // namespace linkaudio
