#include "theme.h"

#include <QFont>

namespace ptd {
namespace theme {

namespace {

QString qcolor(const QColor& c) {
    return QStringLiteral("#%1%2%3")
        .arg(c.red(), 2, 16, QLatin1Char('0'))
        .arg(c.green(), 2, 16, QLatin1Char('0'))
        .arg(c.blue(), 2, 16, QLatin1Char('0'));
}

} // namespace

QString font_css(int px) {
    return QStringLiteral("font-size: %1px;").arg(px);
}

QString golden_stylesheet() {
    const QString bg          = qcolor(background());
    const QString surface_c   = qcolor(surface());
    const QString raised      = qcolor(surfaceRaised());
    const QString surface_alt = qcolor(surfaceAlt());
    const QString bd_dark     = qcolor(borderDark());
    const QString bd_hl       = qcolor(borderHighlight());
    const QString bev_light   = qcolor(bevelLight());
    const QString bd_muted    = qcolor(borderMuted());
    const QString text        = qcolor(textPrimary());
    const QString text_sec    = qcolor(textSecondary());
    const QString text_mut    = qcolor(textMuted());
    const QString teal        = qcolor(accentTeal());
    const QString compare     = qcolor(compareBack());
    const QString sel         = qcolor(selection());
    const QString danger_txt  = qcolor(dangerText());

    // UI.md base CSS, ported to Qt widgets:
    //   raised  -> border: bevelLight borderDark borderDark bevelLight
    //              (top right bottom left) on surfaceRaised
    //   sunken  -> border: borderDark bevelLight bevelLight borderDark
    //              on surface (inputs use compareBack per base CSS)
    // Square corners, 2px bevels, token-only colors, zero animation.
    // Focus: borderHighlight recolor -- instant, no motion (predictability
    // rule 6); same 2px width so focused controls never shift layout.
    // Disabled: label drops to textMuted ON THE SAME raised/sunken
    // surface (never opacity -- UI.md iron law 2 + button disabled rule).
    return QStringLiteral(R"(
* {
    font-family: Verdana;
    font-size: %1px;
    color: %11;
    outline: none;
}
QWidget {
    background: %2;
}
QMainWindow, QDialog {
    background: %2;
}
QLabel {
    background: transparent;
}
QLabel#titleLabel {
    font-size: %17px;
}
QLabel#sectionLabel {
    font-size: %3px;
    color: %11;
}
QLabel#hintLabel {
    font-size: 10px;
    color: %12;
}

/* ---- Buttons: raised bevel ---- */
QPushButton {
    background: %5;
    border: 2px solid;
    border-top-color: %7;
    border-left-color: %7;
    border-bottom-color: %8;
    border-right-color: %8;
    padding: 2px 6px;
    min-height: 16px;
    min-width: 24px;
}
QPushButton:hover {
    background: %6;
}
QPushButton:pressed {
    background: %4;
    border-top-color: %8;
    border-left-color: %8;
    border-bottom-color: %7;
    border-right-color: %7;
    padding-left: 7px;
    padding-top: 3px;
    padding-right: 5px;
    padding-bottom: 1px;
}
/* ---- T-020R1 selector contract (explicit; never the generic :checked) ----
   These buttons are mutually exclusive selectors, so all three of their
   states are declared here instead of being inherited from the ordinary
   button rules. The first T-020 pass let them fall through to a generic
   checked rule, which read inside-out: the selected button looked like an
   ordinary button somebody had accidentally pressed. The contract is:

     UNSELECTED neutral raised Golden button, standard bevel, no highlight.
     SELECTED   stable lighter Golden surface + borderHighlight on the lit
                (top/left) edges + borderDark on the shadow (bottom/right)
                edges. The bevel DIRECTION is unchanged, so a selected
                button still reads raised -- never pressed.
     FOCUS      additive and secondary: only the label color changes. Focus
                never touches the four bevel colors or the surface, so both
                the bevel geometry and the selected/unselected distinction
                survive focus. (No font-weight change either: a bold label
                re-measures and would reflow the row, and at 520 px that can
                clip. Text emphasis is colour-only.)

   Hover must never impersonate selection: unselected hover only lifts the
   surface, it never lights the bevel. Press keeps the sanctioned instant 1px
   shift from the shared QPushButton:pressed padding rule; the bevel inverts
   only for the duration of the press, then the resting reading returns. */
QPushButton[selector="true"] {
    background: %5;
    color: %11;
    border-top-color: %7;
    border-left-color: %7;
    border-bottom-color: %8;
    border-right-color: %8;
}
QPushButton[selector="true"]:hover {
    background: %6;
}
QPushButton[selector="true"]:checked,
QPushButton[selector="true"]:checked:hover {
    background: %6;
    border-top-color: %9;
    border-left-color: %9;
    border-bottom-color: %8;
    border-right-color: %8;
}
/* Focus last: the selected surface must win over hover, but the focus cue
   must survive on top of a selected button too. */
QPushButton[selector="true"]:focus {
    color: %9;
}
/* A real press is still a press: momentary sunken feedback, exactly like an
   ordinary button, then back to the resting selected/unselected reading. */
QPushButton[selector="true"]:pressed {
    background: %4;
    border-top-color: %8;
    border-left-color: %8;
    border-bottom-color: %7;
    border-right-color: %7;
}
QPushButton:disabled {
    color: %13;
    background: %5;
}

/* ---- Inputs: sunken bevel on compareBack ---- */
QLineEdit, QSpinBox, QDoubleSpinBox,
QLineEdit:focus, QSpinBox:focus,
QDoubleSpinBox:focus {
    background: %10;
    border: 2px solid;
    border-top-color: %7;
    border-left-color: %7;
    border-bottom-color: %8;
    border-right-color: %8;
    padding: 1px 3px;
    min-height: 14px;
    selection-background-color: %14;
    selection-color: %11;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus {
    border-top-color: %9;
    border-left-color: %9;
    border-bottom-color: %9;
    border-right-color: %9;
}
QSpinBox::up-button, QDoubleSpinBox::up-button,
QSpinBox::down-button, QDoubleSpinBox::down-button {
    background: %5;
    border: 1px solid %7;
    width: 12px;
}
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
    background: %6;
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
    image: none;
    border-left: 3px solid transparent;
    border-right: 3px solid transparent;
    border-bottom: 3px solid %11;
    width: 0; height: 0;
}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
    image: none;
    border-left: 3px solid transparent;
    border-right: 3px solid transparent;
    border-top: 3px solid %11;
    width: 0; height: 0;
}
/* ---- Checkbox: sunken indicator; checked = teal fill + dark tick ----
   T-010R Repair 4: checked state must not depend on accent color alone.
   The checked indicator draws the embedded tick mark (:/ui/check.xpm,
   drawn in borderDark on transparent) over the teal fill: a shape cue
   that survives grayscale. Unchecked stays the empty sunken box.
   Square geometry, no animation, no decorative iconography; the mark
   uses the existing borderDark token only. */
QCheckBox {
    background: transparent;
    spacing: 6px;
}
QCheckBox::indicator {
    width: 14px;
    height: 14px;
    background: %10;
    border: 2px solid;
    border-top-color: %7;
    border-left-color: %7;
    border-bottom-color: %8;
    border-right-color: %8;
}
QCheckBox::indicator:checked {
    background: %15;
    image: url(:/ui/check.xpm);
}
QCheckBox::indicator:checked:disabled {
    background: %15;
    image: url(:/ui/check.xpm);
}
QCheckBox::indicator:hover {
    border-top-color: %9;
    border-left-color: %9;
    border-bottom-color: %9;
    border-right-color: %9;
}
QCheckBox::indicator:focus {
    border-top-color: %9;
    border-left-color: %9;
    border-bottom-color: %9;
    border-right-color: %9;
}
QCheckBox:disabled {
    color: %13;
}

/* ---- Tabs: inactive raised, active sunken, tabs touch ---- */
QTabWidget::pane {
    border: 2px solid;
    border-top-color: %7;
    border-left-color: %7;
    border-bottom-color: %8;
    border-right-color: %8;
    background: %2;
    top: -1px;
}
QTabBar::tab {
    background: %5;
    border: 2px solid;
    border-top-color: %8;
    border-left-color: %8;
    border-bottom-color: %7;
    border-right-color: %7;
    border-bottom: none;
    padding: 3px 10px;
    margin-right: 0px;
}
QTabBar::tab:selected {
    background: %2;
    border-top-color: %7;
    border-left-color: %7;
    border-bottom-color: %2;
    border-right-color: %7;
}
QTabBar::tab:focus {
    border-top-color: %9;
    border-left-color: %9;
}

/* ---- Group boxes: one shared 1px muted frame ---- */
QGroupBox {
    border: 1px solid %16;
    margin-top: 10px;
    padding: 6px 4px 4px 4px;
    background: %2;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 6px;
    padding: 0 3px;
    background: %2;
    font-size: %3px;
}

/* ---- Sliders: sunken groove, raised handle ---- */
QSlider::groove:horizontal {
    height: 4px;
    background: %10;
    border: 1px solid %7;
}
QSlider::sub-page:horizontal {
    background: %15;
    border: 1px solid %7;
}
QSlider::handle:horizontal {
    background: %5;
    border: 2px solid;
    border-top-color: %8;
    border-left-color: %8;
    border-bottom-color: %7;
    border-right-color: %7;
    width: 10px;
    margin: -7px 0;
}
QSlider::handle:horizontal:hover {
    background: %6;
}
QSlider::handle:horizontal:focus {
    border-top-color: %9;
    border-left-color: %9;
    border-bottom-color: %9;
    border-right-color: %9;
}
QSlider:disabled {
    color: %13;
}

/* ---- Scrollbars: compact, raised handle ---- */
QScrollBar:vertical {
    background: %2;
    width: 12px;
    border: none;
}
QScrollBar::handle:vertical {
    background: %5;
    border: 1px solid %7;
    min-height: 20px;
}
QScrollBar::handle:vertical:hover {
    background: %6;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: %4;
}

/* ---- Tooltips: surface panel ---- */
QToolTip {
    background: %4;
    color: %11;
    border: 1px solid %16;
    padding: 2px 4px;
}
)")
        .arg(fontBodyPx())      // %1
        .arg(bg)                // %2
        .arg(fontHeaderPx())    // %3
        .arg(surface_c)         // %4
        .arg(raised)            // %5
        .arg(surface_alt)       // %6
        .arg(bev_light)         // %7
        .arg(bd_dark)           // %8
        .arg(bd_hl)             // %9
        .arg(compare)           // %10
        .arg(text)              // %11
        .arg(text_sec)          // %12
        .arg(text_mut)          // %13
        .arg(sel)               // %14
        .arg(teal)              // %15
        .arg(bd_muted)          // %16 (danger_txt reserved for future error text)
        .arg(fontTitlePx());    // %17 (window title font size)
}

} // namespace theme
} // namespace ptd
