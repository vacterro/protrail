#pragma once

// MVP 05 Phase B: Golden Default theme (saipen UI.md canonical palette,
// Wintage themes/goldendefault.json). Every visible chrome color in the
// SettingsWindow must trace back to these tokens -- no ad hoc hex values,
// no second palette (UI.md closed colour set). User-selectable effect
// RGB values are DOMAIN data, not chrome: their preview swatches may show
// the chosen effect color, but no chrome color is ever derived from them.

#include <QColor>
#include <QString>

namespace ptd {
namespace theme {

// ---- Golden Default tokens (byte-for-byte from UI.md) ----

inline QColor background()       { return QColor(0x1A, 0x18, 0x10); } // #1A1810
inline QColor backgroundSoft()   { return QColor(0x23, 0x20, 0x18); } // #232018
inline QColor surface()          { return QColor(0x33, 0x2E, 0x22); } // #332E22
inline QColor surfaceRaised()    { return QColor(0x3D, 0x37, 0x2A); } // #3D372A
inline QColor surfaceAlt()       { return QColor(0x45, 0x3D, 0x30); } // #453D30
inline QColor borderDark()       { return QColor(0x10, 0x0E, 0x08); } // #100E08
inline QColor borderHighlight()  { return QColor(0xF0, 0xD0, 0x60); } // #F0D060
inline QColor bevelLight()       { return QColor(0x75, 0x66, 0x3D); } // #75663D
inline QColor borderMuted()      { return QColor(0x5A, 0x50, 0x40); } // #5A5040
inline QColor textPrimary()      { return QColor(0xD4, 0xC8, 0x9A); } // #D4C89A
inline QColor textSecondary()    { return QColor(0x9C, 0x93, 0x71); } // #9C9371
inline QColor textMuted()        { return QColor(0x6E, 0x67, 0x4E); } // #6E674E
inline QColor accentTeal()       { return QColor(0x00, 0x80, 0x80); } // #008080
inline QColor accentTealDeep()   { return QColor(0x00, 0x4C, 0x4C); } // #004C4C
inline QColor success()          { return QColor(0x4A, 0x7A, 0x20); } // #4A7A20
inline QColor warning()          { return QColor(0x7A, 0x7A, 0x20); } // #7A7A20
inline QColor danger()           { return QColor(0x7A, 0x20, 0x20); } // #7A2020
inline QColor dangerText()       { return QColor(0xD6, 0x64, 0x64); } // #D66464
inline QColor selection()        { return QColor(0x3D, 0x37, 0x2A); } // #3D372A
inline QColor compareBack()      { return QColor(0x14, 0x12, 0x0C); } // #14120C
inline QColor link()             { return QColor(0xF0, 0xD0, 0x60); } // #F0D060

// ---- Font: Verdana, non-antialiased (UI.md iron law 1) ----

inline QString fontFamily() { return QStringLiteral("Verdana"); }
inline int fontBodyPx()    { return 12; }   // default body size
inline int fontSmallPx()   { return 10; }   // secondary metadata only
inline int fontHeaderPx()  { return 14; }   // section headers
inline int fontTitlePx()   { return 16; }   // window title only

// ---- Geometry: 2px bevel, square corners, compact rhythm ----

inline int bevelPx()          { return 2; }  // the only depth language
inline int controlPadPx()     { return 1; }  // inside controls
inline int groupPadPx()       { return 4; }  // inside groups
inline int sectionGapPx()     { return 8; }  // between sections
inline int outerMarginPx()    { return 12; } // window margins (12-16 band)

// ---- Stylesheet builders ----

// One application-wide stylesheet implementing UI.md's base CSS for the
// Qt Widgets chrome: raised buttons, sunken inputs, square corners,
// 2px bevels, token-only colors, no transitions/animations/gradients.
QString golden_stylesheet();

// Verdana font at the requested UI.md size, without AA-friendly hinting.
// Qt applies the app font; this is used for per-class size overrides.
QString font_css(int px);

} // namespace theme
} // namespace ptd
