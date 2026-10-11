// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "network.hpp"
#include "monitor-return.hpp"
#include <QDialog>
#include <QThread>
#include <QSettings>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QProgressBar>
namespace linkaudio {
class SenderPanel : public QDialog {
  public:
    SenderPanel(const QString &configPath, QWidget *parent = nullptr);
    ~SenderPanel() override;
    void shutdown();

  private:
    AudioQueue audio;
    SharedStatus shared;
    vban::MonitorReturn monitor;
    QThread worker;
    Sender *sender = nullptr;
    QSettings settings;
    QLineEdit *host, *pairing;
    QSpinBox *port, *buffer;
    QComboBox *peers;
    QCheckBox *automatic;
    QLabel *state, *detail, *counts;
    QProgressBar *meter;
    QTimer *refresh;
    bool stopped = false;
    QString inputError;
    void apply(bool enabled);
    void updateStatus();
};
} // namespace linkaudio
