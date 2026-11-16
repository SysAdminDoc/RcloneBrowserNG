#include "interface_polish.h"

#include <algorithm>
#include <cmath>

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#include <QAccessibilityHints>
#endif

namespace {

struct Tone {
  const char *window;
  const char *surface;
  const char *surfaceRaised;
  const char *field;
  const char *border;
  const char *borderStrong;
  const char *text;
  const char *muted;
  const char *accent;
  const char *accentHover;
  const char *accentSoft;
  const char *success;
  const char *successSoft;
  const char *warning;
  const char *warningSoft;
  const char *danger;
  const char *dangerSoft;
  const char *selectionText;
  // Boundary of inputs, check boxes and radios: 3:1 against field, window and
  // surface, which WCAG 1.4.11 asks of a control's only visible edge.
  const char *control;
  // Selected rows in a focused view, and the text drawn on them.
  const char *selection;
  const char *selectionInk;
};

// Measured against WCAG 2.2 AA: text pairs (muted text and the status badges
// on their soft fills included) reach 4.5:1, and control boundaries and the
// focus ring reach 3:1 against every surface they sit on.
const Tone kLight = {"#f4f6fa", "#ffffff", "#f2f5f9", "#ffffff", "#dce2eb",
                     "#bcc5d2", "#1d2530", "#5f6b7a", "#2563d0", "#1f56b8",
                     "#e9f1ff", "#18794a", "#e7f5ed", "#8f5700", "#fff3db",
                     "#b4323a", "#fcebee", "#ffffff", "#838d9c", "#d6e4fc",
                     "#1d2530"};
const Tone kDark = {"#1e2228", "#24292f", "#2b3139", "#191d23", "#384150",
                    "#536070", "#f0f3f7", "#9aa5b4", "#7fb4ff", "#99c4ff",
                    "#243550", "#6dcf8a", "#1f3828", "#edb55c", "#3f321d",
                    "#ff8585", "#3e2527", "#0a1220", "#677386", "#2d4a78",
                    "#f0f3f7"};

// The tone the stylesheet was last built from, for the few places that put
// colors into rich text, where the stylesheet doesn't reach.
Tone gCurrentTone = kLight;

double srgbChannel(double value) {
  value /= 255.0;
  return value <= 0.03928 ? value / 12.92
                          : std::pow((value + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor &color) {
  return 0.2126 * srgbChannel(color.red()) +
         0.7152 * srgbChannel(color.green()) +
         0.0722 * srgbChannel(color.blue());
}

double contrastRatio(const QColor &a, const QColor &b) {
  const double aLum = relativeLuminance(a);
  const double bLum = relativeLuminance(b);
  const double light = qMax(aLum, bLum);
  const double dark = qMin(aLum, bLum);
  return (light + 0.05) / (dark + 0.05);
}

QColor bestContrastingColor(const QColor &background,
                            const QList<QColor> &candidates) {
  QColor best(Qt::white);
  double bestRatio = 0.0;
  for (const QColor &candidate : candidates) {
    if (!candidate.isValid())
      continue;
    const double ratio = contrastRatio(candidate, background);
    if (ratio > bestRatio) {
      best = candidate;
      bestRatio = ratio;
    }
  }

  if (bestRatio >= 4.5) {
    return best;
  }

  const QColor black(Qt::black);
  const QColor white(Qt::white);
  const double blackRatio = contrastRatio(black, background);
  if (blackRatio > bestRatio) {
    best = black;
    bestRatio = blackRatio;
  }
  if (contrastRatio(white, background) > bestRatio) {
    best = white;
  }
  return best;
}

// High contrast is a system setting, so ask the system. Guessing it from the
// palette called every ordinary light theme high contrast (dark text on a
// light window is a big ratio), which swapped the designed light tones for
// raw palette colors and left the light "Running" badge unreadable.
bool isHighContrast() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
  if (QGuiApplication::styleHints()->accessibility()->contrastPreference() ==
      Qt::ContrastPreference::HighContrast) {
    return true;
  }
#endif
#if defined(Q_OS_WIN)
  HIGHCONTRASTW contrast{};
  contrast.cbSize = static_cast<UINT>(sizeof(contrast));
  if (SystemParametersInfoW(SPI_GETHIGHCONTRAST,
                            static_cast<UINT>(sizeof(contrast)), &contrast,
                            0)) {
    return (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
  }
#endif
  // Older Qt off Windows: only a palette at the very top of the scale (pure
  // black on white or the reverse is 21:1) reads as a high contrast theme.
  const QPalette pal = QGuiApplication::palette();
  return contrastRatio(pal.color(QPalette::Window),
                       pal.color(QPalette::WindowText)) > 20.0;
}

Tone highContrastTone() {
  QPalette pal = QApplication::palette();
  static QByteArray windowBuf, surfaceBuf, fieldBuf, borderBuf, textBuf,
      mutedBuf, accentBuf, selBuf, successBuf, warningBuf, dangerBuf;
  const QColor window = pal.color(QPalette::Window);
  const QColor surface = pal.color(QPalette::Base);
  const QColor text = pal.color(QPalette::WindowText);
  const QColor highlightedText = pal.color(QPalette::HighlightedText);
  const QColor accent = pal.color(QPalette::Highlight);
  windowBuf = window.name().toLatin1();
  surfaceBuf = surface.name().toLatin1();
  fieldBuf = surface.name().toLatin1();
  borderBuf = pal.color(QPalette::Mid).name().toLatin1();
  textBuf = text.name().toLatin1();
  mutedBuf =
      pal.color(QPalette::Disabled, QPalette::WindowText).name().toLatin1();
  accentBuf = accent.name().toLatin1();
  selBuf = highlightedText.name().toLatin1();
  successBuf = bestContrastingColor(surface, QList<QColor>()
                                                 << QColor("#008000")
                                                 << QColor("#00ff66")
                                                 << QColor("#004d1a") << text
                                                 << accent << highlightedText)
                   .name()
                   .toLatin1();
  warningBuf = bestContrastingColor(surface, QList<QColor>()
                                                 << QColor("#b8860b")
                                                 << QColor("#ffd166")
                                                 << QColor("#6b4500") << text
                                                 << accent << highlightedText)
                   .name()
                   .toLatin1();
  dangerBuf = bestContrastingColor(surface, QList<QColor>()
                                                << QColor("#cc0000")
                                                << QColor("#ff5f5f")
                                                << QColor("#7a0000") << text
                                                << accent << highlightedText)
                  .name()
                  .toLatin1();
  return {windowBuf.constData(),  surfaceBuf.constData(),
          surfaceBuf.constData(), fieldBuf.constData(),
          borderBuf.constData(),  borderBuf.constData(),
          textBuf.constData(),    mutedBuf.constData(),
          accentBuf.constData(),  accentBuf.constData(),
          accentBuf.constData(),  successBuf.constData(),
          surfaceBuf.constData(), warningBuf.constData(),
          surfaceBuf.constData(), dangerBuf.constData(),
          surfaceBuf.constData(), selBuf.constData(),
          textBuf.constData(),    accentBuf.constData(),
          selBuf.constData()};
}

// Fusion paints frame lines, tab bar bases and bevels from the palette, so a
// palette that doesn't match the stylesheet shows through as gray panels and
// white lines in dark mode. Build it from the same tokens.
QPalette paletteFor(const Tone &t) {
  const QColor window(t.window);
  const QColor surface(t.surface);
  const QColor raised(t.surfaceRaised);
  const QColor field(t.field);
  const QColor border(t.border);
  const QColor strong(t.borderStrong);
  const QColor text(t.text);
  const QColor muted(t.muted);
  const QColor accent(t.accent);
  const QColor accentHover(t.accentHover);
  const QColor selectionText(t.selectionText);

  QPalette pal;
  const QPalette::ColorGroup groups[] = {QPalette::Active, QPalette::Inactive,
                                         QPalette::Disabled};
  for (const QPalette::ColorGroup group : groups) {
    const bool disabled = group == QPalette::Disabled;
    pal.setColor(group, QPalette::Window, window);
    pal.setColor(group, QPalette::WindowText, disabled ? muted : text);
    pal.setColor(group, QPalette::Base, disabled ? raised : field);
    pal.setColor(group, QPalette::AlternateBase, raised);
    pal.setColor(group, QPalette::ToolTipBase, surface);
    pal.setColor(group, QPalette::ToolTipText, text);
    pal.setColor(group, QPalette::PlaceholderText, muted);
    pal.setColor(group, QPalette::Text, disabled ? muted : text);
    pal.setColor(group, QPalette::Button, raised);
    pal.setColor(group, QPalette::ButtonText, disabled ? muted : text);
    pal.setColor(group, QPalette::BrightText, selectionText);
    pal.setColor(group, QPalette::Light, border);
    pal.setColor(group, QPalette::Midlight, border);
    pal.setColor(group, QPalette::Mid, border);
    pal.setColor(group, QPalette::Dark, strong);
    pal.setColor(group, QPalette::Shadow, strong);
    pal.setColor(group, QPalette::Highlight, disabled ? border : accent);
    pal.setColor(group, QPalette::HighlightedText,
                 disabled ? muted : selectionText);
    pal.setColor(group, QPalette::Link, accent);
    pal.setColor(group, QPalette::LinkVisited, accentHover);
  }
  return pal;
}

// Fusion outlines check boxes and radios with window.darker(140), which all
// but disappears on a dark field. Draw them from the theme tokens instead.
class PolishStyle : public QProxyStyle {
public:
  explicit PolishStyle(const Tone &tone)
      : QProxyStyle(QStyleFactory::create("Fusion")), mPalette(paletteFor(tone)),
        mField(tone.field), mRaised(tone.surfaceRaised), mBorder(tone.border),
        mControl(tone.control), mAccent(tone.accent),
        mAccentHover(tone.accentHover), mMark(tone.selectionText),
        mMuted(tone.muted) {}

  QPalette standardPalette() const override { return mPalette; }

  void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                     QPainter *painter,
                     const QWidget *widget = nullptr) const override {
    switch (element) {
    case PE_IndicatorCheckBox:
    case PE_IndicatorItemViewItemCheck:
      drawCheckIndicator(option, painter);
      return;
    case PE_IndicatorRadioButton:
      drawRadioIndicator(option, painter);
      return;
    default:
      break;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
  }

private:
  static QRectF squareIn(const QRect &rect) {
    const qreal side = qMin(rect.width(), rect.height()) - 1.0;
    const QPointF center = QRectF(rect).center();
    return QRectF(center.x() - side / 2.0, center.y() - side / 2.0, side,
                  side);
  }

  void drawCheckIndicator(const QStyleOption *option,
                          QPainter *painter) const {
    const bool enabled = option->state.testFlag(State_Enabled);
    const bool on = option->state.testFlag(State_On);
    const bool partial = option->state.testFlag(State_NoChange);
    const bool hover = enabled && option->state.testFlag(State_MouseOver);
    const QRectF box = squareIn(option->rect);
    const qreal side = box.width();

    QColor fill = enabled ? mField : mRaised;
    QColor edge = enabled ? (hover ? mAccent : mControl) : mBorder;
    if (on || partial) {
      fill = enabled ? (hover ? mAccentHover : mAccent) : mBorder;
      edge = fill;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(edge, 1.0));
    painter->setBrush(fill);
    painter->drawRoundedRect(box, 4.0, 4.0);
    if (on || partial) {
      QPen pen(enabled ? mMark : mMuted, qMax<qreal>(1.6, side / 8.0));
      pen.setCapStyle(Qt::RoundCap);
      pen.setJoinStyle(Qt::RoundJoin);
      painter->setPen(pen);
      painter->setBrush(Qt::NoBrush);
      if (partial && !on) {
        painter->drawLine(
            QPointF(box.left() + side * 0.28, box.center().y()),
            QPointF(box.right() - side * 0.28, box.center().y()));
      } else {
        QPainterPath mark;
        mark.moveTo(box.left() + side * 0.27, box.top() + side * 0.52);
        mark.lineTo(box.left() + side * 0.43, box.top() + side * 0.68);
        mark.lineTo(box.left() + side * 0.74, box.top() + side * 0.34);
        painter->drawPath(mark);
      }
    }
    painter->restore();
  }

  void drawRadioIndicator(const QStyleOption *option,
                          QPainter *painter) const {
    const bool enabled = option->state.testFlag(State_Enabled);
    const bool on = option->state.testFlag(State_On);
    const bool hover = enabled && option->state.testFlag(State_MouseOver);
    const QRectF box = squareIn(option->rect);

    QColor edge = enabled ? (hover || on ? mAccent : mControl) : mBorder;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(edge, on ? 1.5 : 1.0));
    painter->setBrush(enabled ? mField : mRaised);
    painter->drawEllipse(box);
    if (on) {
      const qreal dot = box.width() * 0.5;
      painter->setPen(Qt::NoPen);
      painter->setBrush(enabled ? (hover ? mAccentHover : mAccent) : mMuted);
      painter->drawEllipse(QRectF(box.center().x() - dot / 2.0,
                                  box.center().y() - dot / 2.0, dot, dot));
    }
    painter->restore();
  }

  QPalette mPalette;
  QColor mField;
  QColor mRaised;
  QColor mBorder;
  QColor mControl;
  QColor mAccent;
  QColor mAccentHover;
  QColor mMark;
  QColor mMuted;
};

// Once ::drop-down or ::up-button is styled, Qt's stylesheet code draws the
// arrow only from an image file, so without one every combo and spin box lost
// its arrow. They're painted here in the theme's colors, once per color, into
// a folder that lasts as long as the app.
QString arrowImage(bool up, const QString &color) {
  static QTemporaryDir dir(QDir::tempPath() +
                           QStringLiteral("/rclone-browser-ng-XXXXXX"));
  if (!dir.isValid()) {
    return QString();
  }
  const QString path = dir.filePath(
      QStringLiteral("%1-%2.png")
          .arg(up ? QStringLiteral("up") : QStringLiteral("down"),
               QColor(color).name(QColor::HexRgb).mid(1)));
  if (QFileInfo::exists(path)) {
    return path;
  }
  // A chevron on a 12-unit grid, drawn at 4x so it stays crisp when Qt
  // scales it to the arrow's size on any display.
  constexpr int kScale = 4;
  QImage image(12 * kScale, 12 * kScale, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  {
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(kScale, kScale);
    painter.setPen(QPen(QColor(color), 1.6, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    const qreal tip = up ? 4.5 : 7.5;
    const qreal ends = up ? 7.5 : 4.5;
    const QPointF points[] = {QPointF(3.0, ends), QPointF(6.0, tip),
                              QPointF(9.0, ends)};
    painter.drawPolyline(points, 3);
  }
  return image.save(path, "PNG") ? path : QString();
}

QString arrowStyleFor(const Tone &t) {
  const QString down = arrowImage(false, t.muted);
  const QString downOff = arrowImage(false, t.borderStrong);
  const QString up = arrowImage(true, t.muted);
  const QString upOff = arrowImage(true, t.borderStrong);
  if (down.isEmpty() || downOff.isEmpty() || up.isEmpty() || upOff.isEmpty()) {
    return QString();
  }
  // A separate arg() pass, so a % in the temp path can't reach the tokens.
  return QString(R"(
QComboBox::down-arrow {
  image: url("%1");
  width: 10px;
  height: 10px;
}
QComboBox::down-arrow:disabled {
  image: url("%2");
}
QToolButton::menu-arrow {
  image: url("%1");
  width: 10px;
  height: 10px;
}
QToolButton::menu-arrow:disabled {
  image: url("%2");
}
QAbstractSpinBox::up-arrow {
  image: url("%3");
  width: 9px;
  height: 9px;
}
QAbstractSpinBox::up-arrow:disabled, QAbstractSpinBox::up-arrow:off {
  image: url("%4");
}
QAbstractSpinBox::down-arrow {
  image: url("%1");
  width: 9px;
  height: 9px;
}
QAbstractSpinBox::down-arrow:disabled, QAbstractSpinBox::down-arrow:off {
  image: url("%2");
}
)")
      .arg(down, downOff, up, upOff);
}

QString styleFor(const Tone &t) {
  return QString(R"(
/* ── Base ─────────────────────────────────────────────── */
QMainWindow, QDialog {
  background: %1;
  color: %7;
}
QWidget {
  color: %7;
  selection-background-color: %9;
  selection-color: %18;
}

/* ── Menu bar ─────────────────────────────────────────── */
QMenuBar {
  background: %1;
  border-bottom: 1px solid %5;
  padding: 2px 6px;
}
QMenuBar::item {
  background: transparent;
  border-radius: 4px;
  padding: 5px 10px;
}
QMenuBar::item:selected {
  background: %11;
  color: %21;
}
QMenuBar::item:pressed {
  background: %3;
}

/* ── Tabs ─────────────────────────────────────────────── */
QTabWidget::pane {
  border: 1px solid %5;
  border-top: 0;
  background: %2;
}
QTabBar::tab {
  background: %3;
  border: 1px solid %5;
  border-bottom: 0;
  border-top-left-radius: 6px;
  border-top-right-radius: 6px;
  padding: 7px 16px;
  margin-right: 2px;
  min-height: 20px;
}
QTabBar::tab:selected {
  background: %2;
  color: %7;
  border-color: %6;
}
/* Only the open tab gets a card. The others are flat labels, which also drops
   the stray corner marks their faint borders left between tabs. */
QTabBar::tab:!selected {
  color: %8;
  background: transparent;
  border-color: transparent;
}
QTabBar::tab:!selected:hover {
  background: %11;
  color: %21;
}
QTabBar::close-button {
  border-radius: 4px;
  margin: 2px;
}
QTabBar::close-button:hover {
  background: %5;
}
/* The main window has no outer margin, so its first tab sat on the edge. */
QTabWidget#tabs::tab-bar {
  left: 8px;
}

/* ── Scroll areas ─────────────────────────────────────── */
QScrollArea {
  border: 0;
  background: transparent;
}

/* ── Group boxes ──────────────────────────────────────── */
QGroupBox {
  border: 1px solid %5;
  border-radius: 8px;
  margin-top: 16px;
  padding: 14px 12px 12px 12px;
  background: %2;
}
QGroupBox::title {
  subcontrol-origin: margin;
  left: 12px;
  padding: 0 6px;
  color: %8;
  font-weight: 600;
}

/* ── Inputs ───────────────────────────────────────────── */
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QAbstractSpinBox {
  background: %4;
  color: %7;
  border: 1px solid %19;
  border-radius: 6px;
  padding: 5px 8px;
}
/* Single-line fields only. A stylesheet min-height replaces a widget's own
   minimum, so on the multi-line editors it threw away the 200 px the
   progress output asks for and that view opened one line tall. */
QLineEdit, QComboBox, QAbstractSpinBox {
  min-height: 22px;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus,
QAbstractSpinBox:focus {
  border: 2px solid %9;
  padding: 4px 7px;
}
QListView:focus, QTreeView:focus, QTableView:focus {
  border: 1px solid %9;
}
QLineEdit:read-only, QPlainTextEdit:read-only, QTextEdit:read-only {
  background: %3;
  color: %8;
  border-color: %5;
}
QLineEdit[metricValue="true"] {
  color: %7;
  font-weight: 600;
}
QLineEdit[fieldState="error"], QComboBox[fieldState="error"] {
  border: 1px solid %16;
  background: %17;
}
QLineEdit[fieldState="error"]:focus, QComboBox[fieldState="error"]:focus {
  border: 2px solid %16;
  padding: 4px 7px;
}
QLineEdit:disabled, QPlainTextEdit:disabled, QTextEdit:disabled,
QComboBox:disabled, QAbstractSpinBox:disabled {
  background: %3;
  color: %8;
  border-color: %5;
}
QComboBox::drop-down {
  border: 0;
  width: 26px;
}
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {
  border: 0;
  width: 24px;
}
QAbstractSpinBox::up-button:hover {
  background: %3;
  border-top-right-radius: 5px;
}
QAbstractSpinBox::down-button:hover {
  background: %3;
  border-bottom-right-radius: 5px;
}
QAbstractSpinBox::up-button:pressed, QAbstractSpinBox::down-button:pressed {
  background: %11;
}
/* The text's left inset comes from SpinBoxTextInset, since Qt won't pad a
   spin box's own line edit. */
QAbstractSpinBox, QAbstractSpinBox:focus {
  padding-left: 0;
}

/* ── Checkboxes and radios ────────────────────────────── */
QCheckBox, QRadioButton {
  spacing: 8px;
  min-height: 26px;
}
QCheckBox::indicator, QRadioButton::indicator {
  width: 16px;
  height: 16px;
}
QCheckBox:disabled, QRadioButton:disabled {
  color: %8;
}

/* ── Buttons ──────────────────────────────────────────── */
QPushButton, QToolButton {
  background: %3;
  border: 1px solid %5;
  border-radius: 6px;
  padding: 6px 14px;
  min-height: 26px;
  color: %7;
}
/* Back, forward and the breadcrumb stay flat until the pointer is over them. */
QToolButton[autoRaise="true"] {
  background: transparent;
  border-color: transparent;
}
/* The breadcrumb's folder list. Left unstyled, Qt draws it as a separate
   bevelled box glued to the button. */
QToolButton[popupMode="1"] {
  padding-right: 22px;
}
QToolButton::menu-button {
  border: 0;
  width: 18px;
  border-top-right-radius: 5px;
  border-bottom-right-radius: 5px;
}
QToolButton::menu-button:hover {
  background: %11;
}
QPushButton:hover, QToolButton:hover {
  background: %11;
  border-color: %6;
}
QPushButton:pressed, QToolButton:pressed {
  background: %9;
  color: %18;
}
QPushButton:focus, QToolButton:focus {
  border: 2px solid %9;
  padding: 5px 13px;
  outline: none;
}
/* No fill, so a disabled button sits back instead of looking brighter than
   the ones that work. */
QPushButton:disabled, QToolButton:disabled {
  color: %8;
  background: transparent;
  border-color: %5;
}
QToolButton[autoRaise="true"]:disabled {
  border-color: transparent;
}
QPushButton[primary="true"], QToolButton[primary="true"] {
  background: %9;
  border-color: %9;
  color: %18;
  font-weight: 600;
}
QPushButton[primary="true"]:hover, QToolButton[primary="true"]:hover {
  background: %10;
  border-color: %10;
}
QPushButton[primary="true"]:pressed, QToolButton[primary="true"]:pressed {
  background: %10;
  border-color: %6;
}
QPushButton[primary="true"]:focus, QToolButton[primary="true"]:focus {
  border-color: %7;
}
QPushButton[primary="true"]:disabled, QToolButton[primary="true"]:disabled {
  background: transparent;
  border-color: %5;
  color: %8;
}
QPushButton[destructive="true"], QToolButton[destructive="true"] {
  color: %16;
  border-color: %16;
  background: %17;
}
QPushButton[destructive="true"]:hover, QToolButton[destructive="true"]:hover {
  background: %16;
  border-color: %16;
  color: %18;
}
QPushButton[destructive="true"]:focus, QToolButton[destructive="true"]:focus {
  border-color: %16;
}
QPushButton[destructive="true"]:disabled,
QToolButton[destructive="true"]:disabled {
  background: transparent;
  border-color: %5;
  color: %8;
}
QToolButton[compact="true"] {
  padding: 4px;
  min-width: 30px;
  min-height: 30px;
}
QToolButton[compact="true"]:focus {
  padding: 3px;
}
QToolButton[compact="true"]:checked {
  background: %11;
  border-color: %9;
  color: %7;
}
QToolButton[disclosure="true"] {
  background: transparent;
  border: 1px solid transparent;
  padding: 4px 8px;
  min-height: 24px;
  font-weight: 600;
}
QToolButton[disclosure="true"]:hover {
  background: %11;
  border-color: %5;
}
QToolButton[disclosure="true"]:focus {
  border: 2px solid %9;
  padding: 3px 7px;
}
/* Buttons that live inside other controls (the clear button of a search
   field, the scroll arrows of a crowded tab bar) keep their native size. */
QLineEdit QToolButton, QAbstractSpinBox QToolButton, QMenuBar QToolButton {
  background: transparent;
  border: 0;
  padding: 0;
  margin: 0;
  min-width: 0;
  min-height: 0;
}
QTabBar QToolButton {
  background: %3;
  border: 1px solid %5;
  border-radius: 4px;
  padding: 0;
  margin: 1px;
  min-width: 0;
  min-height: 0;
}

/* ── Status badges ────────────────────────────────────── */
QToolButton[status="running"] {
  color: %9;
  background: %11;
  border-color: %9;
  font-weight: 600;
}
QToolButton[status="success"] {
  color: %12;
  background: %13;
  border-color: %12;
  font-weight: 600;
}
QToolButton[status="warning"] {
  color: %14;
  background: %15;
  border-color: %14;
  font-weight: 600;
}
QToolButton[status="error"] {
  color: %16;
  background: %17;
  border-color: %16;
  font-weight: 600;
}
QToolButton[status="idle"] {
  color: %8;
  background: transparent;
  border-color: %5;
  font-weight: 600;
}

/* ── Cards and surfaces ───────────────────────────────── */
QWidget[polishCard="true"] {
  background: %2;
  border: 1px solid %5;
  border-radius: 8px;
}
QWidget[toolbarSurface="true"] {
  background: %3;
  border-bottom: 1px solid %5;
  border-top-left-radius: 8px;
  border-top-right-radius: 8px;
}
QWidget[actionBar="true"] {
  background: %3;
  border: 1px solid %5;
  border-radius: 8px;
  padding: 4px;
}

/* ── Labels ───────────────────────────────────────────── */
QLabel[emptyState="true"] {
  background: %2;
  border: 1px solid %5;
  border-radius: 8px;
  color: %8;
  padding: 28px 24px;
  min-height: 72px;
}
QLabel[notice="true"] {
  background: %11;
  border: 1px solid %5;
  border-radius: 6px;
  color: %7;
  padding: 10px 12px;
}
QLabel[validationState="error"] {
  background: %17;
  border: 1px solid %16;
  border-radius: 6px;
  color: %16;
  padding: 8px 10px;
}
QLabel[validationState="warning"] {
  background: %15;
  border: 1px solid %14;
  border-radius: 6px;
  color: %7;
  padding: 8px 10px;
}
QLabel[validationState="success"] {
  background: %13;
  border: 1px solid %12;
  border-radius: 6px;
  color: %12;
  padding: 8px 10px;
}
QLabel[muted="true"] {
  color: %8;
}
QLabel:disabled {
  color: %8;
}

/* ── List and tree views ──────────────────────────────── */
QListView, QTreeView, QTableView {
  background: %2;
  alternate-background-color: %3;
  border: 1px solid %5;
  border-radius: 6px;
  show-decoration-selected: 1;
  outline: 0;
  selection-background-color: %20;
  selection-color: %21;
}
QListView:!active, QTreeView:!active, QTableView:!active {
  selection-background-color: %11;
}
QListView::item, QTreeView::item, QTableView::item {
  min-height: 28px;
  padding: 4px 8px;
}
QListView::item {
  border-radius: 4px;
}
QListView::item:hover, QTreeView::item:hover, QTableView::item:hover {
  background: %3;
}
QListView::item:selected, QTreeView::item:selected, QTableView::item:selected {
  background: %11;
  color: %21;
}
QListView::item:selected:active, QTreeView::item:selected:active,
QTableView::item:selected:active {
  background: %20;
  color: %21;
}
QListView::item:disabled, QTreeView::item:disabled,
QTableView::item:disabled {
  color: %8;
}
QTableView {
  gridline-color: %5;
}
QTableCornerButton::section {
  background: %3;
  border: 0;
  border-bottom: 1px solid %5;
}

/* ── Header view ──────────────────────────────────────── */
QHeaderView::section {
  background: %3;
  color: %8;
  border: 0;
  border-bottom: 1px solid %5;
  padding: 6px 10px;
  font-weight: 600;
}

/* ── Splitter ─────────────────────────────────────────── */
QSplitter::handle {
  background: %1;
}
QSplitter::handle:hover {
  background: %11;
}

/* ── Progress bar ─────────────────────────────────────── */
QProgressBar {
  border: 1px solid %5;
  border-radius: 6px;
  background: %3;
  text-align: center;
  min-height: 18px;
}
QProgressBar::chunk {
  background: %9;
  border-radius: 4px;
}

/* ── Status bar ───────────────────────────────────────── */
QStatusBar {
  background: %3;
  color: %8;
  border-top: 1px solid %5;
  padding: 2px 4px;
}
QStatusBar::item {
  border: 0;
}

/* ── Menus ─────────────────────────────────────────────── */
QMenu {
  background: %2;
  border: 1px solid %5;
  border-radius: 6px;
  padding: 4px;
}
QMenu::item {
  padding: 7px 28px 7px 16px;
  border-radius: 4px;
  margin: 1px 2px;
}
QMenu::item:selected {
  background: %11;
  color: %21;
}
QMenu::item:disabled {
  color: %8;
}
QMenu::separator {
  height: 1px;
  background: %5;
  margin: 4px 8px;
}

/* ── Dialog buttons ───────────────────────────────────── */
QDialogButtonBox QPushButton {
  min-width: 88px;
}

/* ── Scrollbars ───────────────────────────────────────── */
/* The thumb stays wider than twice its corner radius so it never turns
   into a pill. */
QScrollBar:vertical {
  background: transparent;
  border: 0;
  width: 12px;
  margin: 0;
}
QScrollBar::handle:vertical {
  background: %5;
  border-radius: 4px;
  min-height: 32px;
  margin: 2px 1px;
}
QScrollBar::handle:vertical:hover {
  background: %6;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
  background: transparent;
  height: 0;
}
QScrollBar:horizontal {
  background: transparent;
  border: 0;
  height: 12px;
  margin: 0;
}
QScrollBar::handle:horizontal {
  background: %5;
  border-radius: 4px;
  min-width: 32px;
  margin: 1px 2px;
}
QScrollBar::handle:horizontal:hover {
  background: %6;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
  background: transparent;
  width: 0;
}
QAbstractScrollArea::corner {
  background: transparent;
}

/* ── Tooltips ─────────────────────────────────────────── */
QToolTip {
  color: %7;
  background-color: %2;
  border: 1px solid %6;
  border-radius: 6px;
  padding: 6px 8px;
}
)")
      .arg(t.window, t.surface, t.surfaceRaised, t.field, t.border,
           t.borderStrong, t.text, t.muted, t.accent, t.accentHover,
           t.accentSoft, t.success, t.successSoft, t.warning, t.warningSoft,
           t.danger, t.dangerSoft, t.selectionText, t.control, t.selection,
           t.selectionInk) +
         arrowStyleFor(t);
}

void setBoolProperty(QWidget *widget, const char *name, bool value) {
  if (!widget) {
    return;
  }
  widget->setProperty(name, value);
  UiPolish::Repolish(widget);
}

// An explicit minimum size switches off the minimum a layout would give its
// window, so a dialog could open smaller than its own rows and draw them on
// top of each other (issue #13 and its follow-ups). Keep the requested size
// as a floor and grow it to whatever the layout needs whenever that changes.
// No Q_OBJECT here, so it's found by object name (findChild<T> needs moc).
const char kMinimumSizeKeeperName[] = "uiPolishMinimumSizeKeeper";

class MinimumSizeKeeper : public QObject {
public:
  MinimumSizeKeeper(QWidget *widget, QSize requested)
      : QObject(widget), mRequested(requested) {
    setObjectName(QLatin1String(kMinimumSizeKeeperName));
  }

  void setRequested(QSize requested) { mRequested = requested; }

  void apply(QWidget *widget) const {
    QSize target = mRequested.expandedTo(widget->minimumSizeHint());
    if (const QScreen *screen = widget->screen()) {
      target = target.boundedTo(screen->availableGeometry().size());
    }
    if (target != widget->minimumSize()) {
      widget->setMinimumSize(target);
    }
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    // The layout handles LayoutRequest before object filters see it, so the
    // hint read here is already the new one.
    if ((event->type() == QEvent::Show ||
         event->type() == QEvent::LayoutRequest) &&
        watched == parent()) {
      apply(static_cast<QWidget *>(watched));
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QSize mRequested;
};

// A spin box's text sits in a line edit that Qt's stylesheet code won't
// style, and Qt 6.12 also starts that line edit at the spin box's left edge
// whatever its padding says, so the number touched the border. The
// stylesheet gives spin boxes no left padding and this text margin stands
// in for it (border plus padding of a plain field), the same on every Qt.
constexpr int kSpinBoxTextInset = 9;

class SpinBoxTextInset : public QObject {
public:
  using QObject::QObject;

  static void apply(QAbstractSpinBox *spinBox) {
    auto *edit =
        spinBox->findChild<QLineEdit *>(QString(), Qt::FindDirectChildrenOnly);
    if (!edit) {
      return;
    }
    const QMargins margins = edit->textMargins();
    if (margins.left() != kSpinBoxTextInset) {
      edit->setTextMargins(kSpinBoxTextInset, margins.top(), margins.right(),
                           margins.bottom());
    }
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::Polish) {
      if (auto *spinBox = qobject_cast<QAbstractSpinBox *>(watched)) {
        apply(spinBox);
      }
    }
    return QObject::eventFilter(watched, event);
  }
};

} // namespace

namespace UiPolish {

QColor BestContrastingColor(const QColor &background,
                            const QList<QColor> &candidates) {
  return bestContrastingColor(background, candidates);
}

void ApplyApplicationStyle(bool dark) {
  const bool highContrast = isHighContrast();
  const Tone tone = highContrast ? highContrastTone() : (dark ? kDark : kLight);
  gCurrentTone = tone;
#if !defined(Q_OS_MACOS)
  // High contrast keeps the platform style and colors; everything else gets
  // Fusion with a matching palette, so both themes draw the same way on
  // Windows and Linux. macOS keeps its native look.
  if (!highContrast) {
    QApplication::setStyle(new PolishStyle(tone));
    QApplication::setPalette(paletteFor(tone));
  }
#endif
  qApp->setStyleSheet(styleFor(tone));

  static SpinBoxTextInset *spinBoxInset = nullptr;
  if (!spinBoxInset) {
    spinBoxInset = new SpinBoxTextInset(qApp);
    qApp->installEventFilter(spinBoxInset);
  }
  const QWidgetList widgets = QApplication::allWidgets();
  for (QWidget *widget : widgets) {
    if (auto *spinBox = qobject_cast<QAbstractSpinBox *>(widget)) {
      SpinBoxTextInset::apply(spinBox);
    }
  }
}

void Repolish(QWidget *widget) {
  if (!widget || !widget->style()) {
    return;
  }
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
  widget->update();
}

void SetCard(QWidget *widget) {
  if (widget) {
    widget->setAttribute(Qt::WA_StyledBackground, true);
  }
  setBoolProperty(widget, "polishCard", true);
}

void SetToolbarSurface(QWidget *widget) {
  if (widget) {
    widget->setAttribute(Qt::WA_StyledBackground, true);
  }
  setBoolProperty(widget, "toolbarSurface", true);
}

void SetActionBar(QWidget *widget) {
  if (widget) {
    widget->setAttribute(Qt::WA_StyledBackground, true);
  }
  setBoolProperty(widget, "actionBar", true);
}

void SetEmptyState(QLabel *label, const QString &title, const QString &detail) {
  if (!label) {
    return;
  }
  label->setProperty("emptyState", true);
  label->setAccessibleName(title);
  label->setAccessibleDescription(detail);
  label->setAlignment(Qt::AlignCenter);
  label->setWordWrap(true);
  label->setTextFormat(Qt::RichText);
  // Qt's rich text ignores CSS opacity, so the two lines get their own
  // colors: the title in body text, the detail in the muted tone.
  QString html =
      QString("<div style='font-size:13px; font-weight:600; color:%1;'>%2"
              "</div>")
          .arg(QString::fromLatin1(gCurrentTone.text), title.toHtmlEscaped());
  if (!detail.isEmpty()) {
    html += QString("<div style='margin-top:4px; font-size:12px; color:%1;'>"
                    "%2</div>")
                .arg(QString::fromLatin1(gCurrentTone.muted),
                     detail.toHtmlEscaped());
  }
  label->setText(html);
  Repolish(label);
}

void SetNotice(QLabel *label, const QString &text) {
  if (!label) {
    return;
  }
  label->setProperty("notice", true);
  label->setAccessibleName("Information");
  label->setWordWrap(true);
  label->setTextFormat(Qt::PlainText);
  label->setText(text);
  Repolish(label);
}

void SetValidationMessage(QLabel *label, const QString &state,
                          const QString &text) {
  if (!label) {
    return;
  }
  label->setProperty("validationState", state);
  label->setAccessibleName(text.isEmpty() ? QString("Validation message")
                                          : text);
  label->setWordWrap(true);
  label->setText(text);
  label->setVisible(!text.isEmpty());
  Repolish(label);
}

void SetMuted(QWidget *widget) { setBoolProperty(widget, "muted", true); }

void SetFieldState(QWidget *widget, const QString &state) {
  if (!widget) {
    return;
  }
  widget->setProperty("fieldState", state);
  Repolish(widget);
}

void SetNavigationView(QAbstractItemView *view, const QString &accessibleName) {
  if (!view) {
    return;
  }
  if (!accessibleName.isEmpty()) {
    view->setAccessibleName(accessibleName);
  }
  view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  view->setProperty("navigationView", true);
  Repolish(view);
}

void SetCompactToolButton(QAbstractButton *button,
                          const QString &accessibleName,
                          const QString &toolTip) {
  if (!button) {
    return;
  }
  button->setProperty("compact", true);
  button->setAccessibleName(accessibleName);
  button->setToolTip(toolTip.isEmpty() ? accessibleName : toolTip);
  Repolish(button);
}

void SetDisclosureButton(QToolButton *button, const QString &accessibleName,
                         const QString &toolTip) {
  if (!button) {
    return;
  }
  button->setProperty("disclosure", true);
  button->setAccessibleName(accessibleName);
  button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  button->setToolTip(toolTip.isEmpty() ? accessibleName : toolTip);
  Repolish(button);
}

void SetPrimaryButton(QAbstractButton *button) {
  if (!button) {
    return;
  }
  button->setProperty("primary", true);
  Repolish(button);
}

void SetDestructiveButton(QAbstractButton *button) {
  if (!button) {
    return;
  }
  button->setProperty("destructive", true);
  Repolish(button);
}

void SetStatus(QAbstractButton *button, const QString &status,
               const QString &text) {
  if (!button) {
    return;
  }
  button->setProperty("status", status);
  button->setText(text);
  button->setAccessibleName(QString("Status: %1").arg(text));
  button->setToolTip(QString("%1 (click for details)").arg(text));
  Repolish(button);
}

void SetPathField(QLineEdit *lineEdit, const QString &accessibleName) {
  if (!lineEdit) {
    return;
  }
  lineEdit->setAccessibleName(accessibleName);
  lineEdit->setMinimumWidth(0);
  lineEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void SetReadOnlyValue(QLineEdit *lineEdit, const QString &accessibleName) {
  if (!lineEdit) {
    return;
  }
  lineEdit->setProperty("metricValue", true);
  lineEdit->setReadOnly(true);
  if (!accessibleName.isEmpty()) {
    lineEdit->setAccessibleName(accessibleName);
  }
  lineEdit->setPlaceholderText("-");
  lineEdit->setMinimumWidth(qMax(lineEdit->minimumWidth(), 110));
  Repolish(lineEdit);
}

void SetOutputView(QPlainTextEdit *output, const QString &accessibleName) {
  if (!output) {
    return;
  }
  output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  output->setAccessibleName(accessibleName.isEmpty() ? "Command output"
                                                     : accessibleName);
}

void SetTableView(QTableView *view, const QString &accessibleName) {
  if (!view) {
    return;
  }
  if (!accessibleName.isEmpty()) {
    view->setAccessibleName(accessibleName);
  }
  view->setAlternatingRowColors(true);
  view->setShowGrid(false);
  view->setWordWrap(false);
  view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  view->setSelectionBehavior(QAbstractItemView::SelectRows);
  view->setTextElideMode(Qt::ElideMiddle);
  if (view->verticalHeader()) {
    view->verticalHeader()->setVisible(false);
    view->verticalHeader()->setDefaultSectionSize(30);
  }
  if (view->horizontalHeader()) {
    view->horizontalHeader()->setHighlightSections(false);
    view->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft |
                                                  Qt::AlignVCenter);
  }
  Repolish(view);
}

void SetDialogButtonBox(QDialogButtonBox *buttons) {
  if (!buttons) {
    return;
  }
  buttons->setCenterButtons(false);
  const auto buttonList = buttons->buttons();
  for (QAbstractButton *button : buttonList) {
    button->setMinimumHeight(qMax(button->minimumHeight(), 30));
    button->setFocusPolicy(Qt::StrongFocus);
    if (button->accessibleName().isEmpty()) {
      button->setAccessibleName(button->text().remove('&'));
    }
  }
}

void SetWindowDefaults(QWidget *widget, QSize minimumSize) {
  if (!widget) {
    return;
  }
  if (auto dialog = qobject_cast<QDialog *>(widget)) {
    dialog->setSizeGripEnabled(true);
  }
  if (!minimumSize.isValid()) {
    return;
  }
  widget->setMinimumSize(minimumSize);
  if (QObject *existing = widget->findChild<QObject *>(
          QLatin1String(kMinimumSizeKeeperName), Qt::FindDirectChildrenOnly)) {
    static_cast<MinimumSizeKeeper *>(existing)->setRequested(minimumSize);
  } else {
    widget->installEventFilter(new MinimumSizeKeeper(widget, minimumSize));
  }
}

void MakeTabPagesScrollable(QTabWidget *tabs) {
  if (!tabs) {
    return;
  }
  const QSignalBlocker blocker(tabs);
  const int current = tabs->currentIndex();
  for (int i = 0; i < tabs->count(); ++i) {
    QWidget *page = tabs->widget(i);
    if (!page || qobject_cast<QScrollArea *>(page)) {
      continue;
    }
    const QString text = tabs->tabText(i);
    const QIcon icon = tabs->tabIcon(i);
    const QString toolTip = tabs->tabToolTip(i);
    const bool enabled = tabs->isTabEnabled(i);

    auto *scroll = new QScrollArea;
    scroll->setObjectName(page->objectName() + "Scroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFocusPolicy(Qt::NoFocus);
    tabs->removeTab(i);
    page->setAutoFillBackground(false);
    scroll->setWidget(page);
    scroll->viewport()->setAutoFillBackground(false);
    // Only the height scrolls; the page keeps the width its rows need.
    scroll->setMinimumWidth(page->minimumSizeHint().width() +
                            scroll->verticalScrollBar()->sizeHint().width());
    tabs->insertTab(i, scroll, icon, text);
    tabs->setTabToolTip(i, toolTip);
    tabs->setTabEnabled(i, enabled);
  }
  tabs->setCurrentIndex(current);
}

bool ShowTabContaining(QTabWidget *tabs, QWidget *widget) {
  if (!tabs || !widget) {
    return false;
  }
  for (int i = 0; i < tabs->count(); ++i) {
    QWidget *page = tabs->widget(i);
    if (page && (page == widget || page->isAncestorOf(widget))) {
      tabs->setCurrentIndex(i);
      if (auto *scroll = qobject_cast<QScrollArea *>(page)) {
        if (scroll->widget() != widget) {
          scroll->ensureWidgetVisible(widget);
        }
      }
      return true;
    }
  }
  return false;
}

void SetAccessibleFormField(QWidget *widget, const QString &name,
                            const QString &description) {
  if (!widget)
    return;
  widget->setAccessibleName(name);
  if (!description.isEmpty())
    widget->setAccessibleDescription(description);
}

bool IsHighContrastActive() { return isHighContrast(); }

QIcon FileIcon() {
  const auto draw = [](int size, const QColor &ink, const QColor &fill) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal extent = size;
    const qreal stroke = (std::max)(1.0, extent / 14.0);
    const qreal left = extent * 0.2;
    const qreal right = extent * 0.8;
    const qreal top = extent * 0.1;
    const qreal bottom = extent * 0.9;
    const qreal ear = extent * 0.22;
    QPainterPath page;
    page.moveTo(left, top);
    page.lineTo(right - ear, top);
    page.lineTo(right, top + ear);
    page.lineTo(right, bottom);
    page.lineTo(left, bottom);
    page.closeSubpath();
    painter.setPen(
        QPen(ink, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(fill);
    painter.drawPath(page);
    QPainterPath fold;
    fold.moveTo(right - ear, top);
    fold.lineTo(right - ear, top + ear);
    fold.lineTo(right, top + ear);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(fold);
    return pixmap;
  };
  const QColor ink(QString::fromLatin1(gCurrentTone.accent));
  const QColor fill(QString::fromLatin1(gCurrentTone.accentSoft));
  const QColor off(QString::fromLatin1(gCurrentTone.borderStrong));
  QIcon icon;
  // Up to 64 px so a 2x or 3x display gets a sharp copy of an 18 px icon.
  for (int size : {16, 18, 20, 24, 32, 36, 48, 54, 64}) {
    icon.addPixmap(draw(size, ink, fill), QIcon::Normal);
    icon.addPixmap(draw(size, off, QColor(Qt::transparent)), QIcon::Disabled);
  }
  return icon;
}

} // namespace UiPolish
