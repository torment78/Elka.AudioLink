// SPDX-License-Identifier: GPL-2.0-or-later
#include "secure-socket.hpp"
#include <QCryptographicHash>
#include <QTimer>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#include <mbedtls/x509_crt.h>
#include <cstring>
#include <algorithm>
namespace linkaudio {
struct SecureSocket::Impl {
    SecureSocket &owner;
    QTcpSocket *wire;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context rng;
    mbedtls_x509_crt certificate;
    mbedtls_pk_context key;
    QByteArray certPem, keyPem, in, out, activeWrite;
    QString expected, peer, error;
    bool server = false, ready = false, secure = false, pumping = false, active = false;
    explicit Impl(SecureSocket &o) : owner(o), wire(new QTcpSocket(&o)) {
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&config);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&rng);
        mbedtls_x509_crt_init(&certificate);
        mbedtls_pk_init(&key);
        wire->setReadBufferSize(65536);
        QObject::connect(wire, &QTcpSocket::connected, &o, [this] {
            if (!server && prepare())
                pump();
        });
        QObject::connect(wire, &QTcpSocket::readyRead, &o, [this] { pump(); });
        QObject::connect(wire, &QTcpSocket::bytesWritten, &o, [this](qint64) { pump(); });
        QObject::connect(wire, &QTcpSocket::disconnected, &o, [this] {
            active = false;
            secure = false;
            Q_EMIT owner.disconnected();
        });
        QObject::connect(wire, &QTcpSocket::errorOccurred, &o, [this](QAbstractSocket::SocketError e) {
            error = wire->errorString();
            Q_EMIT owner.errorOccurred(e);
        });
    }
    ~Impl() {
        wire->disconnect(&owner);
        wire->abort();
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&config);
        mbedtls_entropy_free(&entropy);
        mbedtls_ctr_drbg_free(&rng);
        mbedtls_x509_crt_free(&certificate);
        mbedtls_pk_free(&key);
    }
    void fail(int rc, const QString &reason = {}) {
        char text[256]{};
        mbedtls_strerror(rc, text, sizeof(text));
        error = reason.isEmpty() ? QString::fromLatin1(text) : reason;
        active = false;
        secure = false;
        in.clear();
        out.clear();
        activeWrite.clear();
        Q_EMIT owner.errorOccurred(QAbstractSocket::SslHandshakeFailedError);
        wire->abort();
    }
    static QString hash(const mbedtls_x509_crt *c) {
        return c ? QString::fromLatin1(
                       QCryptographicHash::hash(
                           QByteArray(reinterpret_cast<const char *>(c->raw.p), qsizetype(c->raw.len)),
                           QCryptographicHash::Sha256)
                           .toHex())
                 : QString();
    }
    static int verify(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
        auto &self = *static_cast<Impl *>(ctx);
        if (depth == 0 && !self.expected.isEmpty() && hash(crt) == self.expected)
            *flags &= ~(MBEDTLS_X509_BADCERT_NOT_TRUSTED | MBEDTLS_X509_BADCERT_CN_MISMATCH);
        else
            *flags |= MBEDTLS_X509_BADCERT_NOT_TRUSTED;
        return 0;
    }
    static int send(void *ctx, const unsigned char *data, size_t size) {
        auto &self = *static_cast<Impl *>(ctx);
        if (!self.active)
            return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
        if (self.wire->bytesToWrite() > 65536)
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        auto n = self.wire->write(reinterpret_cast<const char *>(data), qint64(size));
        return n < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : int(n);
    }
    static int receive(void *ctx, unsigned char *data, size_t size) {
        auto &self = *static_cast<Impl *>(ctx);
        if (!self.active)
            return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
        if (!self.wire->bytesAvailable())
            return MBEDTLS_ERR_SSL_WANT_READ;
        auto n = self.wire->read(reinterpret_cast<char *>(data), qint64(size));
        return n < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : int(n);
    }
    bool prepare() {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_free(&config);
        mbedtls_ssl_config_init(&config);
        mbedtls_x509_crt_free(&certificate);
        mbedtls_x509_crt_init(&certificate);
        mbedtls_pk_free(&key);
        mbedtls_pk_init(&key);
        if (!ready) {
            const unsigned char label[] = "Elka AudioLink TLS";
            const int rc = mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &entropy, label, sizeof(label));
            if (rc) {
                fail(rc);
                return false;
            }
            ready = true;
        }
        int rc = mbedtls_ssl_config_defaults(&config, server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
                                             MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        if (rc) {
            fail(rc);
            return false;
        }
        mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &rng);
        mbedtls_ssl_conf_min_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_2);
        // Pin verification is mandatory after handshake and before any application data.
        mbedtls_ssl_conf_authmode(&config, server ? MBEDTLS_SSL_VERIFY_NONE : MBEDTLS_SSL_VERIFY_OPTIONAL);
        if (server) {
            rc = mbedtls_x509_crt_parse(&certificate,
                                        reinterpret_cast<const unsigned char *>(certPem.constData()),
                                        size_t(certPem.size() + 1));
            if (!rc)
                rc = mbedtls_pk_parse_key(&key, reinterpret_cast<const unsigned char *>(keyPem.constData()),
                                          size_t(keyPem.size() + 1), nullptr, 0, mbedtls_ctr_drbg_random,
                                          &rng);
            if (!rc)
                rc = mbedtls_ssl_conf_own_cert(&config, &certificate, &key);
            if (rc) {
                fail(rc);
                return false;
            }
        } else
            mbedtls_ssl_conf_verify(&config, verify, this);
        rc = mbedtls_ssl_setup(&ssl, &config);
        if (rc) {
            fail(rc);
            return false;
        }
        mbedtls_ssl_set_bio(&ssl, this, send, receive, nullptr);
        active = true;
        secure = false;
        in.clear();
        out.clear();
        activeWrite.clear();
        error.clear();
        return true;
    }
    void pump() {
        if (pumping || !active)
            return;
        pumping = true;
        struct Exit {
            bool &p;
            ~Exit() {
                p = false;
            }
        } exit{pumping};
        if (!secure) {
            auto rc = mbedtls_ssl_handshake(&ssl);
            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
                return;
            if (rc) {
                fail(rc);
                return;
            }
            peer = hash(mbedtls_ssl_get_peer_cert(&ssl));
            if (!server &&
                (peer != expected || expected.isEmpty() || mbedtls_ssl_get_verify_result(&ssl) != 0)) {
                fail(-1, "Receiver certificate does not match the pairing code, or has expired.");
                return;
            }
            secure = true;
            Q_EMIT owner.encrypted();
            if (!active)
                return;
        }
        for (int count = 0; count < 32 && active; ++count) {
            if (activeWrite.isEmpty() && !out.isEmpty()) {
                activeWrite = out.left(4096);
                out.remove(0, activeWrite.size());
            }
            if (activeWrite.isEmpty())
                break;
            auto rc =
                mbedtls_ssl_write(&ssl, reinterpret_cast<const unsigned char *>(activeWrite.constData()),
                                  size_t(activeWrite.size()));
            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
                break;
            if (rc < 0) {
                fail(rc);
                return;
            }
            if (!rc)
                break;
            activeWrite.remove(0, rc);
        }
        bool produced = false;
        for (int count = 0; count < 32 && active; ++count) {
            unsigned char buffer[4096];
            auto rc = mbedtls_ssl_read(&ssl, buffer, sizeof(buffer));
            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
                break;
            if (rc <= 0) {
                if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                    owner.abort();
                    return;
                }
                fail(rc);
                return;
            }
            if (in.size() + rc > 65536) {
                fail(-1, "TLS receive queue exceeded its limit.");
                return;
            }
            in.append(reinterpret_cast<char *>(buffer), rc);
            produced = true;
        }
        if (produced)
            Q_EMIT owner.readyRead();
        if (active && (!out.isEmpty() || (!activeWrite.isEmpty() && wire->bytesToWrite() < 32768) ||
                       wire->bytesAvailable() || mbedtls_ssl_get_bytes_avail(&ssl)))
            QTimer::singleShot(0, &owner, [this] { pump(); });
    }
};
SecureSocket::SecureSocket(QObject *p) : QObject(p), impl(std::make_unique<Impl>(*this)) {}
SecureSocket::~SecureSocket() = default;
void SecureSocket::setExpectedFingerprint(const QString &s) {
    impl->expected = s;
}
void SecureSocket::setLocalIdentity(const QByteArray &cert, const QByteArray &key) {
    impl->server = true;
    impl->certPem = cert;
    impl->keyPem = key;
}
bool SecureSocket::setSocketDescriptor(qintptr d) {
    return impl->wire->setSocketDescriptor(d);
}
void SecureSocket::startServerEncryption() {
    if (impl->prepare())
        impl->pump();
}
void SecureSocket::connectToHostEncrypted(const QString &host, quint16 port) {
    impl->server = false;
    impl->active = false;
    impl->secure = false;
    impl->error.clear();
    impl->wire->connectToHost(host, port);
}
void SecureSocket::setReadBufferSize(qint64 n) {
    impl->wire->setReadBufferSize(n);
}
void SecureSocket::setSocketOption(QAbstractSocket::SocketOption o, const QVariant &v) {
    impl->wire->setSocketOption(o, v);
}
QAbstractSocket::SocketState SecureSocket::state() const {
    return impl->wire->state();
}
bool SecureSocket::isEncrypted() const {
    return impl->secure;
}
QString SecureSocket::peerFingerprint() const {
    return impl->peer;
}
QString SecureSocket::errorString() const {
    return impl->error.isEmpty() ? impl->wire->errorString() : impl->error;
}
qint64 SecureSocket::bytesAvailable() const {
    return impl->in.size();
}
qint64 SecureSocket::bytesToWrite() const {
    return impl->out.size() + impl->activeWrite.size();
}
qint64 SecureSocket::encryptedBytesToWrite() const {
    return impl->wire->bytesToWrite();
}
QByteArray SecureSocket::read(qint64 n) {
    auto result = impl->in.left(n);
    impl->in.remove(0, result.size());
    return result;
}
qint64 SecureSocket::write(const QByteArray &data) {
    if (!impl->secure)
        return -1;
    if (impl->out.size() + impl->activeWrite.size() + data.size() > 65536) {
        impl->fail(-1, "TLS send queue exceeded its limit.");
        return -1;
    }
    impl->out += data;
    impl->pump();
    return data.size();
}
void SecureSocket::abort() {
    impl->active = false;
    impl->secure = false;
    impl->in.clear();
    impl->out.clear();
    impl->activeWrite.clear();
    impl->wire->abort();
}
} // namespace linkaudio
