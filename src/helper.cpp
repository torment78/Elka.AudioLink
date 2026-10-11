// SPDX-License-Identifier: GPL-2.0-or-later
#include "receiver-window.hpp"
#include <QLocalServer>
#include <QLocalSocket>
#include <QCryptographicHash>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("ElkaSoft");
    app.setApplicationName("Elka AudioLink");
    app.setApplicationVersion("0.2.1");
    app.setQuitOnLastWindowClosed(false);
    const auto args = app.arguments();
    const int preview = args.indexOf("--preview");
    QTemporaryDir previewRoot;
    const auto root = preview >= 0 ? previewRoot.path()
                                   : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(root);
    const auto instanceName =
        "ElkaAudioLink-" +
        QString::fromLatin1(
            QCryptographicHash::hash(root.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
    QLockFile lock(root + "/receiver.lock");
    if (!lock.tryLock(100)) {
        QLocalSocket existing;
        existing.connectToServer(instanceName);
        if (existing.waitForConnected(1000)) {
            existing.write("show\n");
            existing.waitForBytesWritten(1000);
            existing.disconnectFromServer();
        } else {
            QMessageBox::information(
                nullptr, "Elka AudioLink",
                "The receiver is already running. Open it from the AudioLink system tray icon.");
        }
        return 0;
    }
    try {
        linkaudio::ReceiverWindow window(root, preview < 0);
        QLocalServer activation;
        activation.setSocketOptions(QLocalServer::UserAccessOption);
        if (preview < 0 && activation.listen(instanceName)) {
            QObject::connect(&activation, &QLocalServer::newConnection, &window, [&] {
                while (auto *client = activation.nextPendingConnection()) {
                    window.showSettings();
                    client->close();
                    client->deleteLater();
                }
            });
        }
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
