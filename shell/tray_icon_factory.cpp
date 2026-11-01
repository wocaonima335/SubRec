#include "shell/tray_icon_factory.h"

#include <QPainter>
#include <QPixmap>

namespace subrec::shell {
namespace {
struct IconSize {
    int size, inset, block, radius, font, badge, badgePosition, barWidth, barHeight, gap;
};
constexpr IconSize sizes[] = {
    {16, 1, 14, 3, 11, 6, 10, 2, 7, 1},
    {20, 1, 18, 4, 13, 8, 12, 3, 9, 1},
    {24, 2, 20, 4, 15, 9, 15, 3, 10, 2},
    {32, 2, 28, 6, 20, 12, 19, 4, 14, 2},
    {40, 2, 36, 8, 25, 15, 24, 5, 18, 2},
    {48, 3, 42, 9, 30, 18, 29, 6, 21, 3}
};
}

QIcon makeTrayIcon(TrayIconKind kind)
{
    QIcon icon;
    for (const auto &s : sizes) {
        QPixmap pixels(s.size, s.size);
        pixels.fill(Qt::transparent);
        QPainter p(&pixels);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(kind == TrayIconKind::Recording ? "#EF4444"
            : kind == TrayIconKind::Paused ? "#F59E0B" : "#5B6475"));
        p.drawRoundedRect(QRectF(s.inset, s.inset, s.block, s.block), s.radius, s.radius);
        if (kind == TrayIconKind::Paused) {
            p.setBrush(Qt::white);
            const qreal x = (s.size - 3 * s.barWidth) / 2.0;
            const qreal y = (s.size - s.barHeight) / 2.0;
            p.drawRoundedRect(QRectF(x, y, s.barWidth, s.barHeight), 1, 1);
            p.drawRoundedRect(QRectF(x + 2 * s.barWidth, y, s.barWidth, s.barHeight), 1, 1);
        } else {
            QFont font(QStringLiteral("Microsoft YaHei UI"));
            font.setPixelSize(s.font);
            font.setBold(true);
            p.setFont(font);
            p.setPen(Qt::white);
            p.drawText(QRect(0, 0, s.size, s.size), Qt::AlignCenter, QStringLiteral("字"));
            p.setPen(Qt::NoPen);
        }
        if (kind == TrayIconKind::Transition || kind == TrayIconKind::Error) {
            const QRectF badge(s.badgePosition, s.badgePosition, s.badge, s.badge);
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            p.setBrush(Qt::transparent);
            p.drawEllipse(badge.adjusted(-s.gap, -s.gap, s.gap, s.gap));
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.setBrush(QColor(kind == TrayIconKind::Error ? "#EF4444" : "#38BDF8"));
            p.drawEllipse(badge);
            if (kind == TrayIconKind::Error && s.size > 16) {
                p.setBrush(Qt::white);
                const qreal scale = s.badge / 12.0;
                const qreal x = badge.center().x() - scale;
                const qreal y = badge.center().y() - 4 * scale;
                p.drawRect(QRectF(x, y, 2 * scale, 5 * scale));
                p.drawRect(QRectF(x, y + 6 * scale, 2 * scale, 2 * scale));
            }
        }
        p.end();
        icon.addPixmap(pixels);
    }
    return icon;
}

} // namespace subrec::shell
