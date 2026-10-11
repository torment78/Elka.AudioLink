// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
namespace linkaudio {
inline QString style() {
    return QStringLiteral(
        "QWidget { background:#171b22; color:#e8edf4; font-family:'Segoe UI'; font-size:10pt; }"
        "QLabel#title { font-size:23pt; font-weight:600; color:#f5f8fc; }"
        "QLabel#subtitle { color:#a2afbe; } QGroupBox { border:1px solid #343e4d; border-radius:8px; "
        "margin-top:14px; padding:14px; }"
        "QGroupBox::title { subcontrol-origin:margin; left:12px; padding:0 5px; color:#94cfda; }"
        "QLineEdit,QSpinBox,QComboBox { background:#222a35; border:1px solid #425064; border-radius:5px; "
        "padding:7px; min-height:22px; selection-background-color:#236979; }"
        "QPushButton { background:#2b3645; border:1px solid #4b5b71; border-radius:5px; padding:9px 16px; "
        "min-height:22px; }"
        "QPushButton:hover { background:#384b5e; } QPushButton#primary { background:#166b7c; "
        "border-color:#2b92a3; font-weight:600; }"
        "QPushButton:disabled { color:#83909f; background:#242a34; }"
        "QProgressBar { border:1px solid #394659; border-radius:4px; background:#222a35; min-height:14px; "
        "text-align:center; }"
        "QProgressBar::chunk { background:#48c4ad; border-radius:3px; }"
        "QCheckBox { spacing:8px; } QCheckBox::indicator { width:18px; height:18px; }"
        "QTabWidget::pane { border:1px solid #343e4d; border-radius:5px; }"
        "QTabBar::tab { background:#222a35; color:#a2afbe; padding:9px 24px; border:1px solid #343e4d; }"
        "QTabBar::tab:selected { background:#166b7c; color:#f5f8fc; }"
        "QRadioButton { spacing:8px; padding:6px 0; } QRadioButton::indicator { width:18px; height:18px; }"
        "QToolTip { background:#222a35; color:#eef3fa; border:1px solid #52647d; }");
}
} // namespace linkaudio
