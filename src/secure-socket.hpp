// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QTcpSocket>
#include <memory>
namespace linkaudio {
// TLS is linked into AudioLink, independent of the host OBS Qt TLS plugins.
class SecureSocket : public QObject {
    Q_OBJECT
  public:
    explicit SecureSocket(QObject *parent = nullptr);
    ~SecureSocket() override;
    void setExpectedFingerprint(const QString &);
    void setLocalIdentity(const QByteArray &certificate, const QByteArray &key);
    bool setSocketDescriptor(qintptr);
    void startServerEncryption();
    void connectToHostEncrypted(const QString &, quint16);
    void setReadBufferSize(qint64 size);
    void setSocketOption(QAbstractSocket::SocketOption, const QVariant &);
    QAbstractSocket::SocketState state() const;
    bool isEncrypted() const;
    QString peerFingerprint() const;
    QString errorString() const;
    qint64 bytesAvailable() const;
    qint64 bytesToWrite() const;
    qint64 encryptedBytesToWrite() const;
    QByteArray read(qint64);
    qint64 write(const QByteArray &);
    void abort();
  Q_SIGNALS:
    void encrypted();
    void readyRead();
    void disconnected();
    void errorOccurred(QAbstractSocket::SocketError);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace linkaudio
