// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "protocol.hpp"
#include <QWidget>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
namespace linkaudio {
class CallbackSettings : public QWidget {
    QLabel *detected;
    QRadioButton *strips, *buses;
    std::array<QCheckBox *, 8> destinations{};
    uint32_t masks[2]{};
    int edition = 0, rememberedEdition = 0;
    bool updating = false;
    CallbackTarget target() const {
        return buses->isChecked() ? CallbackTarget::buses : CallbackTarget::strips;
    }
    void refresh() {
        updating = true;
        const auto category = target();
        const int count = VmLayout::forType(edition).count(category);
        for (int i = 0; i < 8; ++i) {
            destinations[i]->setText(category == CallbackTarget::strips ? QString("In %1").arg(i + 1)
                                     : edition == 1                     ? "A (A1 / A2)"
                                                                        : QString("A%1").arg(i + 1));
            destinations[i]->setVisible(i < count);
            destinations[i]->setChecked(i < count && (masks[int(category)] & (1u << i)));
        }
        updating = false;
    }

  public:
    QPushButton *detect;
    explicit CallbackSettings(QSettings &settings) {
        rememberedEdition = settings.value("callback/edition", 0).toInt();
        masks[0] = settings.value("callback/strips", 0).toUInt();
        masks[1] = settings.value("callback/buses", 0).toUInt();
        auto *layout = new QVBoxLayout(this);
        auto *heading = new QHBoxLayout;
        detected = new QLabel("Click Detect to find the running VoiceMeeter edition.");
        detected->setObjectName("vmDetected");
        detected->setWordWrap(true);
        heading->addWidget(detected, 1);
        detect = new QPushButton("Detect");
        detect->setObjectName("vmDetect");
        heading->addWidget(detect);
        layout->addLayout(heading);
        auto *category = new QHBoxLayout;
        strips = new QRadioButton("Input strips");
        strips->setObjectName("callbackStrips");
        buses = new QRadioButton("Output buses (A)");
        buses->setObjectName("callbackBuses");
        auto *group = new QButtonGroup(this);
        group->addButton(strips);
        group->addButton(buses);
        category->addWidget(strips);
        category->addWidget(buses);
        category->addStretch();
        layout->addLayout(category);
        if (settings.value("callback/target", 0).toInt() == 1)
            buses->setChecked(true);
        else
            strips->setChecked(true);
        auto *grid = new QGridLayout;
        for (int i = 0; i < 8; ++i) {
            destinations[i] = new QCheckBox;
            destinations[i]->setObjectName(QString("callbackDestination%1").arg(i + 1));
            grid->addWidget(destinations[i], i / 4, i % 4);
            connect(destinations[i], &QCheckBox::toggled, this, [this, i](bool on) {
                if (updating)
                    return;
                if (on)
                    masks[int(target())] |= 1u << i;
                else
                    masks[int(target())] &= ~(1u << i);
            });
        }
        layout->addLayout(grid);
        auto *selection = new QHBoxLayout;
        auto *all = new QPushButton("Select all");
        all->setObjectName("callbackAll");
        auto *none = new QPushButton("Clear");
        none->setObjectName("callbackNone");
        selection->addWidget(all);
        selection->addWidget(none);
        selection->addStretch();
        layout->addLayout(selection);
        connect(all, &QPushButton::clicked, this, [this] {
            masks[int(target())] = (1u << VmLayout::forType(edition).count(target())) - 1;
            refresh();
        });
        connect(none, &QPushButton::clicked, this, [this] {
            masks[int(target())] = 0;
            refresh();
        });
        connect(strips, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked)
                refresh();
        });
        connect(buses, &QRadioButton::toggled, this, [this](bool checked) {
            if (checked)
                refresh();
        });
        auto *hint = new QLabel(
            "Pick one or more destinations. The stereo return is added to each selected strip or A bus. "
            "Strip audio follows its normal routing; bus audio goes directly to that bus. B buses are not "
            "selectable.");
        hint->setWordWrap(true);
        layout->addWidget(hint);
        refresh();
    }
    void showInfo(const VmInfo &info) {
        bool changed = info.running() && rememberedEdition && rememberedEdition != info.type;
        if (changed)
            masks[0] = masks[1] = 0;
        edition = info.type;
        if (info.running())
            rememberedEdition = info.type;
        detected->setText(info.message + (changed ? " — Edition changed; select destinations again." : ""));
        refresh();
    }
    void configure(ReceiverConfig &config) const {
        config.vmType = edition;
        config.target = target();
        config.destinations = masks[int(target())];
    }
    void save(QSettings &settings) const {
        settings.setValue("callback/edition", rememberedEdition);
        settings.setValue("callback/target", int(target()));
        settings.setValue("callback/strips", masks[0]);
        settings.setValue("callback/buses", masks[1]);
    }
};
} // namespace linkaudio
