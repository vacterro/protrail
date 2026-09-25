#include "branding.h"

#include <QColor>
#include <QPainter>
#include <QPixmap>
#include <QPointF>
#include <QPolygonF>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace ptd {
namespace ui {
namespace branding {

namespace {

constexpr const wchar_t* kIconResourceName = L"IDI_ICON1";

// Opacity kept by the disabled treatment. Together with full desaturation it
// keeps the disabled tray state obviously different at 16x16.
constexpr qreal kDisabledOpacity = 0.45;

QImage load_resource_icon(int size) {
    HICON handle = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr), kIconResourceName, IMAGE_ICON,
        size, size, LR_DEFAULTCOLOR));
    if (!handle) return {};
    QImage image = QImage::fromHICON(handle);
    DestroyIcon(handle);
    return image;
}

} // namespace

bool has_product_icon() {
    return FindResourceW(GetModuleHandleW(nullptr), kIconResourceName,
                         RT_GROUP_ICON) != nullptr;
}

QIcon product_icon() {
    QIcon icon;
    if (!has_product_icon()) return icon;
    for (int size : kProductIconSizes) {
        const QImage image = load_resource_icon(size);
        if (!image.isNull()) icon.addPixmap(QPixmap::fromImage(image));
    }
    return icon;
}

QImage disabled_treatment(const QImage& image) {
    QImage out = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < out.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const QRgb px = row[x];
            const int gray = qGray(px);
            const int alpha = static_cast<int>(qAlpha(px) * kDisabledOpacity + 0.5);
            row[x] = qRgba(gray, gray, gray, alpha);
        }
    }
    return out;
}

QIcon tray_icon(bool enabled) {
    if (!has_product_icon()) return development_fallback_icon(enabled);
    if (enabled) return product_icon();
    QIcon icon;
    for (int size : kProductIconSizes) {
        const QImage image = load_resource_icon(size);
        if (!image.isNull())
            icon.addPixmap(QPixmap::fromImage(disabled_treatment(image)));
    }
    return icon;
}

QIcon development_fallback_icon(bool enabled) {
    QIcon icon;
    for (int size : {16, 32}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);

        // Gold palette for enabled, neutral gray for disabled
        const QColor fill = enabled ? QColor(212, 160, 23) : QColor(120, 120, 120);
        const QColor border = enabled ? QColor(255, 223, 70) : QColor(160, 160, 160);

        const qreal s = size / 16.0;
        p.setPen(QPen(border, 1.0 * s));
        p.setBrush(fill);

        // Arrow cursor shape pointing top-left
        QPolygonF poly;
        poly << QPointF(3.0 * s, 2.0 * s)
             << QPointF(13.0 * s, 8.0 * s)
             << QPointF(8.5 * s, 9.5 * s)
             << QPointF(6.0 * s, 14.0 * s);
        p.drawPolygon(poly);

        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace branding
} // namespace ui
} // namespace ptd
