// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QWidget>
#include <QPainter>
#include <QLinearGradient>
#include <array>
#include <algorithm>
#include <cmath>
namespace linkaudio {
class LevelMeter : public QWidget {
    std::array<double, 2> level{-60, -60}, hold{-60, -60}, remaining{};

  public:
    explicit LevelMeter(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(84);
        setAccessibleName("Received audio level, left and right, dBFS");
        setToolTip("Received sample peaks. Instant attack, 24 dB/second fall, one-second peak hold.");
    }
    const std::array<double, 2> &levels() const {
        return level;
    }
    void reset() {
        level = hold = {-60, -60};
        remaining = {};
        update();
    }
    void advance(const std::array<double, 2> &peak, double seconds) {
        seconds = std::clamp(seconds, 0.0, 1.0);
        for (size_t c = 0; c < 2; ++c) {
            const double db = peak[c] > 0 && std::isfinite(peak[c])
                                  ? std::clamp(20.0 * std::log10(peak[c]), -60.0, 0.0)
                                  : -60.0;
            level[c] = std::max(db, level[c] - 24.0 * seconds);
            if (db >= hold[c] && db > -60) {
                hold[c] = db;
                remaining[c] = 1.0;
            } else {
                const double fallTime = std::max(0.0, seconds - remaining[c]);
                remaining[c] = std::max(0.0, remaining[c] - seconds);
                hold[c] = std::max(level[c], hold[c] - 24.0 * fallTime);
            }
        }
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double left = 22, width = std::max(1, this->width() - 90);
        const auto x = [&](double db) { return left + (db + 60.0) / 60.0 * width; };
        QLinearGradient fill(left, 0, left + width, 0);
        fill.setColorAt(0, QColor("#39a58f"));
        fill.setColorAt(.79, QColor("#51d3ac"));
        fill.setColorAt(.81, QColor("#e3bf60"));
        fill.setColorAt(.94, QColor("#e3bf60"));
        fill.setColorAt(.96, QColor("#ef7676"));
        fill.setColorAt(1, QColor("#ef7676"));
        for (size_t c = 0; c < 2; ++c) {
            const double y = 8 + double(c) * 26;
            p.setPen(QColor("#a2afbe"));
            p.drawText(QRectF(0, y, 18, 18), Qt::AlignVCenter, c == 0 ? "L" : "R");
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#222a35"));
            p.drawRoundedRect(QRectF(left, y, width, 18), 3, 3);
            p.setBrush(fill);
            if (level[c] > -60)
                p.drawRoundedRect(QRectF(left, y, x(level[c]) - left, 18), 3, 3);
            if (hold[c] > -60) {
                p.setPen(QPen(hold[c] >= -.1 ? QColor("#ff8585") : QColor("#f5f8fc"), 2));
                p.drawLine(QPointF(std::min(x(hold[c]), left + width - 1), y + 1),
                           QPointF(std::min(x(hold[c]), left + width - 1), y + 17));
            }
            p.setPen(QColor("#e8edf4"));
            p.drawText(QRectF(left + width + 6, y, 60, 18), Qt::AlignVCenter | Qt::AlignRight,
                       level[c] <= -60 ? QString::fromUtf8("−∞") : QString::number(level[c], 'f', 1));
        }
        p.setPen(QColor("#a2afbe"));
        auto small = font();
        small.setPointSizeF(8);
        p.setFont(small);
        for (int db : {-60, -48, -36, -24, -12, -6, 0})
            p.drawText(QRectF(x(db) - 13, 59, 26, 18), Qt::AlignHCenter, QString::number(db));
        p.drawText(QRectF(left + width + 6, 59, 60, 18), Qt::AlignRight, "dBFS");
    }
};
} // namespace linkaudio
