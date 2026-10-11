// SPDX-License-Identifier: GPL-2.0-or-later
#include "vm-api.hpp"
#include <windows.h>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
namespace linkaudio {
class NativeVmApi final : public VmApi {
    HMODULE library = nullptr;
    using Simple = long(__stdcall *)();
    using Get = long(__stdcall *)(long *);
    using Register = long(__stdcall *)(long, VmCallback, void *, char *);
    Simple loginFn = nullptr, logoutFn = nullptr, startFn = nullptr, stopFn = nullptr, unregisterFn = nullptr;
    Get typeFn = nullptr, versionFn = nullptr;
    Register registerFn = nullptr;

  public:
    ~NativeVmApi() override {
        if (library)
            FreeLibrary(library);
    }
    bool load(QString &error) override {
        if (library)
            return true;
        const QString key = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VB:"
                            "Voicemeeter {17359A74-1236-5467}";
        QString path;
        for (auto format : {QSettings::Registry32Format, QSettings::Registry64Format}) {
            QSettings registry(key, format);
            auto uninstall = registry.value("UninstallString").toString().trimmed();
            if (uninstall.startsWith('"'))
                uninstall = uninstall.section('"', 1, 1);
            // The official installer stores an executable path. Do not execute it.
            if (!uninstall.isEmpty()) {
                auto candidate = QFileInfo(uninstall).dir().absoluteFilePath("VoicemeeterRemote64.dll");
                if (QFileInfo::exists(candidate)) {
                    path = candidate;
                    break;
                }
            }
        }
        if (path.isEmpty()) {
            const auto candidate = QDir(qEnvironmentVariable("ProgramFiles(x86)"))
                                       .absoluteFilePath("VB/Voicemeeter/VoicemeeterRemote64.dll");
            if (QFileInfo::exists(candidate))
                path = candidate;
        }
        if (path.isEmpty()) {
            error = "VoiceMeeter is not installed. Install it, start it, then detect again.";
            return false;
        }
        library = LoadLibraryExW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library) {
            error = "Cannot load the installed VoiceMeeter API (Windows " + QString::number(GetLastError()) +
                    ").";
            return false;
        }
        loginFn = reinterpret_cast<Simple>(GetProcAddress(library, "VBVMR_Login"));
        logoutFn = reinterpret_cast<Simple>(GetProcAddress(library, "VBVMR_Logout"));
        typeFn = reinterpret_cast<Get>(GetProcAddress(library, "VBVMR_GetVoicemeeterType"));
        versionFn = reinterpret_cast<Get>(GetProcAddress(library, "VBVMR_GetVoicemeeterVersion"));
        registerFn = reinterpret_cast<Register>(GetProcAddress(library, "VBVMR_AudioCallbackRegister"));
        startFn = reinterpret_cast<Simple>(GetProcAddress(library, "VBVMR_AudioCallbackStart"));
        stopFn = reinterpret_cast<Simple>(GetProcAddress(library, "VBVMR_AudioCallbackStop"));
        unregisterFn = reinterpret_cast<Simple>(GetProcAddress(library, "VBVMR_AudioCallbackUnregister"));
        if (!loginFn || !logoutFn || !typeFn || !versionFn || !registerFn || !startFn || !stopFn ||
            !unregisterFn) {
            FreeLibrary(library);
            library = nullptr;
            error = "The installed VoiceMeeter API does not support audio callbacks. Update VoiceMeeter.";
            return false;
        }
        return true;
    }
    long login() override {
        return loginFn();
    }
    long logout() override {
        return logoutFn();
    }
    long type(long *v) override {
        return typeFn(v);
    }
    long version(long *v) override {
        return versionFn(v);
    }
    long registerCallback(long mode, VmCallback callback, void *user, char *name) override {
        return registerFn(mode, callback, user, name);
    }
    long start() override {
        return startFn();
    }
    long stop() override {
        return stopFn();
    }
    long unregisterCallback() override {
        return unregisterFn();
    }
};
std::unique_ptr<VmApi> makeVmApi() {
    return std::make_unique<NativeVmApi>();
}
} // namespace linkaudio
