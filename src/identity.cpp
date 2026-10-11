// SPDX-License-Identifier: GPL-2.0-or-later
#include "identity.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <stdexcept>
namespace linkaudio {
static void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray protect(const QByteArray &plain) {
    DATA_BLOB in{DWORD(plain.size()), reinterpret_cast<BYTE *>(const_cast<char *>(plain.data()))}, out{};
    require(
        CryptProtectData(&in, L"Elka AudioLink", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out),
        "Cannot protect credentials for this Windows user.");
    QByteArray result(reinterpret_cast<char *>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return result;
}
QByteArray unprotect(const QByteArray &encrypted) {
    DATA_BLOB in{DWORD(encrypted.size()), reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.data()))},
        out{};
    require(CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out),
            "Cannot unlock AudioLink credentials for this Windows user.");
    QByteArray result(reinterpret_cast<char *>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return result;
}
QString Identity::fingerprint() const {
    mbedtls_x509_crt parsed;
    mbedtls_x509_crt_init(&parsed);
    if (mbedtls_x509_crt_parse(&parsed, reinterpret_cast<const unsigned char *>(cert.constData()),
                               size_t(cert.size() + 1)) != 0) {
        mbedtls_x509_crt_free(&parsed);
        throw std::runtime_error("Invalid receiver certificate.");
    }
    const auto result =
        QString::fromLatin1(QCryptographicHash::hash(QByteArray(reinterpret_cast<const char *>(parsed.raw.p),
                                                                qsizetype(parsed.raw.len)),
                                                     QCryptographicHash::Sha256)
                                .toHex());
    mbedtls_x509_crt_free(&parsed);
    return result;
}
QString Identity::pairing() const {
    return "ELKA1-" + fingerprint() + "-" + QString::fromLatin1(token.toHex());
}
bool decodePairing(const QString &code, QString &fingerprint, QByteArray &token) {
    const auto parts = code.trimmed().split('-');
    if (parts.size() != 3 || parts[0] != "ELKA1" || parts[1].size() != 64 || parts[2].size() != 48)
        return false;
    const auto fp = parts[1].toLatin1().toLower(), secret = parts[2].toLatin1().toLower();
    if (QByteArray::fromHex(fp).toHex() != fp || QByteArray::fromHex(secret).toHex() != secret)
        return false;
    fingerprint = QString::fromLatin1(fp);
    token = QByteArray::fromHex(secret);
    return true;
}
Identity loadIdentity(const QString &dir) {
    require(QDir().mkpath(dir), "Cannot create receiver settings folder.");
    const auto path = dir + "/identity.bin";
    QFile existing(path);
    if (existing.exists()) {
        require(existing.open(QIODevice::ReadOnly), "Cannot read receiver identity.");
        const auto o = QJsonDocument::fromJson(unprotect(existing.readAll())).object();
        Identity id{o["cert"].toString().toUtf8(), o["key"].toString().toUtf8(),
                    QByteArray::fromHex(o["token"].toString().toLatin1())};
        require(!id.cert.isEmpty() && !id.key.isEmpty() && id.token.size() == 24,
                "Receiver identity is invalid; restore its settings backup.");
        id.fingerprint();
        return id;
    }
    struct Crypto {
        mbedtls_pk_context key;
        mbedtls_entropy_context entropy;
        mbedtls_ctr_drbg_context random;
        mbedtls_x509write_cert cert;
        mbedtls_mpi serial;
        Crypto() {
            mbedtls_pk_init(&key);
            mbedtls_entropy_init(&entropy);
            mbedtls_ctr_drbg_init(&random);
            mbedtls_x509write_crt_init(&cert);
            mbedtls_mpi_init(&serial);
        }
        ~Crypto() {
            mbedtls_pk_free(&key);
            mbedtls_entropy_free(&entropy);
            mbedtls_ctr_drbg_free(&random);
            mbedtls_x509write_crt_free(&cert);
            mbedtls_mpi_free(&serial);
        }
    } c;
    auto ok = [](int rc) { require(rc == 0, "Could not create receiver TLS identity."); };
    const unsigned char personal[] = "Elka AudioLink receiver";
    ok(mbedtls_ctr_drbg_seed(&c.random, mbedtls_entropy_func, &c.entropy, personal, sizeof(personal)));
    ok(mbedtls_pk_setup(&c.key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)));
    ok(mbedtls_rsa_gen_key(mbedtls_pk_rsa(c.key), mbedtls_ctr_drbg_random, &c.random, 2048, 65537));
    mbedtls_x509write_crt_set_version(&c.cert, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&c.cert, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&c.cert, &c.key);
    mbedtls_x509write_crt_set_issuer_key(&c.cert, &c.key);
    ok(mbedtls_x509write_crt_set_subject_name(&c.cert, "CN=Elka AudioLink"));
    ok(mbedtls_x509write_crt_set_issuer_name(&c.cert, "CN=Elka AudioLink"));
    unsigned char serial[16]{};
    ok(mbedtls_ctr_drbg_random(&c.random, serial, sizeof(serial)));
    serial[0] &= 0x7f;
    serial[0] |= 1;
    ok(mbedtls_mpi_read_binary(&c.serial, serial, sizeof(serial)));
    ok(mbedtls_x509write_crt_set_serial(&c.cert, &c.serial));
    const auto from = QDateTime::currentDateTimeUtc().addDays(-1).toString("yyyyMMddhhmmss").toLatin1();
    const auto to = QDateTime::currentDateTimeUtc().addYears(10).toString("yyyyMMddhhmmss").toLatin1();
    ok(mbedtls_x509write_crt_set_validity(&c.cert, from.constData(), to.constData()));
    ok(mbedtls_x509write_crt_set_basic_constraints(&c.cert, 0, -1));
    ok(mbedtls_x509write_crt_set_key_usage(&c.cert, MBEDTLS_X509_KU_DIGITAL_SIGNATURE |
                                                        MBEDTLS_X509_KU_KEY_ENCIPHERMENT));
    unsigned char key[4096]{}, cert[4096]{};
    ok(mbedtls_pk_write_key_pem(&c.key, key, sizeof(key)));
    ok(mbedtls_x509write_crt_pem(&c.cert, cert, sizeof(cert), mbedtls_ctr_drbg_random, &c.random));
    QByteArray token(24, 0);
    ok(mbedtls_ctr_drbg_random(&c.random, reinterpret_cast<unsigned char *>(token.data()), 24));
    QJsonObject o{{"cert", QString::fromLatin1(reinterpret_cast<char *>(cert))},
                  {"key", QString::fromLatin1(reinterpret_cast<char *>(key))},
                  {"token", QString::fromLatin1(token.toHex())}};
    auto encrypted = protect(QJsonDocument(o).toJson(QJsonDocument::Compact));
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly), "Cannot save receiver identity.");
    require(file.write(encrypted) == encrypted.size() && file.commit(), "Cannot commit receiver identity.");
    return {QByteArray(reinterpret_cast<char *>(cert)), QByteArray(reinterpret_cast<char *>(key)), token};
}
} // namespace linkaudio
