// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/bmem.h>
#include <QAction>
#include <QPointer>
#include "sender-ui.hpp"
OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("elka-audiolink", "en-US")
MODULE_EXPORT const char *obs_module_description(void) {
    return "Elka AudioLink: encrypted monitor return to a local VBAN receiver.";
}
MODULE_EXPORT const char *obs_module_name(void) {
    return "Elka AudioLink";
}
MODULE_EXPORT const char *obs_module_author(void) {
    return "ElkaSoft";
}
namespace {
QPointer<linkaudio::SenderPanel> panel;
QPointer<QAction> action;
void ensurePanel() {
    if (panel)
        return;
    char *path = obs_module_config_path("sender.ini");
    const QString config = QString::fromUtf8(path);
    bfree(path);
    try {
        panel = new linkaudio::SenderPanel(config, static_cast<QWidget *>(obs_frontend_get_main_window()));
    } catch (const std::exception &e) {
        blog(LOG_ERROR, "[Elka AudioLink] Cannot start: %s", e.what());
    }
}
void event(enum obs_frontend_event e, void *) {
    if (e == OBS_FRONTEND_EVENT_FINISHED_LOADING)
        ensurePanel();
    if (e == OBS_FRONTEND_EVENT_EXIT && panel)
        panel->shutdown();
}
} // namespace
bool obs_module_load(void) {
    action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("Elka AudioLink"));
    if (action)
        QObject::connect(action, &QAction::triggered, action, [] {
            ensurePanel();
            if (panel) {
                panel->show();
                panel->raise();
                panel->activateWindow();
            }
        });
    obs_frontend_add_event_callback(event, nullptr);
    blog(LOG_INFO, "[Elka AudioLink] 0.1.1 loaded");
    return true;
}
void obs_module_unload(void) {
    obs_frontend_remove_event_callback(event, nullptr);
    if (panel) {
        panel->shutdown();
        delete panel;
    }
    if (action) {
        action->disconnect();
        delete action;
    }
}
