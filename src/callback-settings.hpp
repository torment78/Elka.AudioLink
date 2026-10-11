// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "protocol.hpp"
#include <QWidget>
#include <QSlider>
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
    std::array<QRadioButton *, 8> destinations{};
    QButtonGroup *destinationGroup;
    QWidget *gainBox;
    QSlider *gain;
    QLabel *gainValue, *selectionHint;
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
        destinationGroup->setExclusive(false);
        for (auto *button : destinations)
            button->setChecked(false);
        destinationGroup->setExclusive(true);
        for (int i = 0; i < 8; ++i) {
            destinations[i]->setText(category == CallbackTarget::strips ? QString("In %1").arg(i + 1)
                                     : edition == 1                     ? "A (A1 / A2)"
                                                                        : QString("A%1").arg(i + 1));
            destinations[i]->setVisible(i < count);
            destinations[i]->setChecked(i < count && (masks[int(category)] & (1u << i)));
        }
        gainBox->setVisible(category == CallbackTarget::buses);
        updating = false;
    }

  public:
    QPushButton *detect;
    std::function<void(int)> gainChanged;
    explicit CallbackSettings(QSettings &settings) {
        rememberedEdition = settings.value("callback/edition", 0).toInt();
        masks[0] = settings.value("callback/strips", 0).toUInt();
        masks[1] = settings.value("callback/buses", 0).toUInt();
        // Never silently choose a destination from an old multi-route setting.
        const bool migrated =
            (masks[0] && !singleDestination(masks[0])) || (masks[1] && !singleDestination(masks[1]));
        for (auto &mask : masks)
            if (!singleDestination(mask))
                mask = 0;
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
        selectionHint =
            new QLabel(migrated ? "Pick one channel only. Previous multiple selections were cleared."
                                : "Pick one channel only.");
        selectionHint->setObjectName("callbackSelectionHint");
        selectionHint->setWordWrap(true);
        layout->addWidget(selectionHint);
        auto *grid = new QGridLayout;
        destinationGroup = new QButtonGroup(this);
        for (int i = 0; i < 8; ++i) {
            destinations[i] = new QRadioButton;
            destinationGroup->addButton(destinations[i]);
            destinations[i]->setObjectName(QString("callbackDestination%1").arg(i + 1));
            grid->addWidget(destinations[i], i / 4, i % 4);
            connect(destinations[i], &QRadioButton::toggled, this, [this, i](bool on) {
                if (!updating && on) {
                    masks[int(target())] = 1u << i;
                    selectionHint->setText("Pick one channel only.");
                }
            });
        }
        layout->addLayout(grid);
        gainBox = new QWidget;
        auto *levelLayout = new QVBoxLayout(gainBox);
        levelLayout->setContentsMargins(0, 8, 0, 4);
        auto *levelTitle = new QHBoxLayout;
        levelTitle->addWidget(new QLabel("Bus return level"));
        gainValue = new QLabel;
        gainValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        gainValue->setObjectName("busReturnGainValue");
        levelTitle->addWidget(gainValue);
        levelLayout->addLayout(levelTitle);
        gain = new QSlider(Qt::Horizontal);
        gain->setObjectName("busReturnGain");
        gain->setAccessibleName("Bus return level");
        gain->setRange(-61, 0);
        gain->setPageStep(6);
        gain->setValue(settings.value("callback/busGainDb", 0).toInt());
        gainValue->setText(gain->value() == -61 ? "Mute" : QString("%1 dB").arg(gain->value()));
        levelLayout->addWidget(gain);
        auto *levelHint = new QLabel("Live control · 0 dB to mute · Changes only the AudioLink return.");
        levelHint->setWordWrap(true);
        levelLayout->addWidget(levelHint);
        layout->addWidget(gainBox);
        connect(gain, &QSlider::valueChanged, this, [this](int db) {
            gainValue->setText(db == -61 ? "Mute" : QString("%1 dB").arg(db));
            if (gainChanged)
                gainChanged(db);
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
            "The stereo return is added to the selected strip or A bus. "
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
        config.busGainDb = gain->value();
    }
    void save(QSettings &settings) const {
        settings.setValue("callback/edition", rememberedEdition);
        settings.setValue("callback/target", int(target()));
        settings.setValue("callback/strips", masks[0]);
        settings.setValue("callback/buses", masks[1]);
        settings.setValue("callback/busGainDb", gain->value());
    }
};
} // namespace linkaudio
