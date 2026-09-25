#pragma once

#include <QIcon>
#include <QImage>

namespace ptd {
namespace ui {
namespace branding {

// The approved product icon has ONE authority: the IDI_ICON1 icon resource
// linked into the running executable (resources/windows/protrail.rc.in).
// Qt's Windows platform plugin already uses that resource as the default
// window/taskbar icon; these helpers read the same resource for Qt surfaces
// and the tray, so no second image source can disagree with the PE icon.

// Shell sizes read from the resource. Windows picks the nearest entry of the
// multi-resolution .ico for each request.
inline constexpr int kProductIconSizes[] = {16, 20, 24, 32, 40, 48, 64, 128, 256};

// True when the running module carries the product icon resource.
bool has_product_icon();

// Product icon at every kProductIconSizes entry; null when the resource is
// absent (developer build without the approved asset).
QIcon product_icon();

// Tray icon for the master enabled state. With the product icon present the
// enabled state is the product icon itself and the disabled state is its
// deterministic disabled treatment; without it both states use the generated
// development fallback, which is never final branding.
QIcon tray_icon(bool enabled);

// Disabled-state treatment: full desaturation plus reduced opacity, so the
// state reads at 16x16 by colour AND by weight, never by a subtle tint.
QImage disabled_treatment(const QImage& image);

// Generated QPainter cursor glyph (gold enabled / grey disabled). Development
// fallback only; used when the product icon resource is absent.
QIcon development_fallback_icon(bool enabled);

} // namespace branding
} // namespace ui
} // namespace ptd
