#include "interface_polish.h"

#include <cmath>

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
};

bool isHighContrast() {
  QPalette pal = QApplication::palette();
  QColor bg = pal.color(QPalette::Window);
  QColor fg = pal.color(QPalette::WindowText);
  int bgLum = qGray(bg.rgb());
  int fgLum = qGray(fg.rgb());
  double contrast = (qMax(bgLum, fgLum) + 0.05) /
                    (qMin(bgLum, fgLum) + 0.05);
  return contrast > 12.0;
}

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

Tone tones(bool dark) {
  if (isHighContrast()) {
    QPalette pal = QApplication::palette();
    static QByteArray windowBuf, surfaceBuf, fieldBuf, borderBuf,
        textBuf, mutedBuf, accentBuf, selBuf, successBuf, warningBuf,
        dangerBuf;
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
    successBuf =
        bestContrastingColor(surface, QList<QColor>()
                                          << QColor("#008000")
                                          << QColor("#00ff66")
                                          << QColor("#004d1a") << text
                                          << accent << highlightedText)
            .name()
            .toLatin1();
    warningBuf =
        bestContrastingColor(surface, QList<QColor>()
                                          << QColor("#b8860b")
                                          << QColor("#ffd166")
                                          << QColor("#6b4500") << text
                                          << accent << highlightedText)
            .name()
            .toLatin1();
    dangerBuf =
        bestContrastingColor(surface, QList<QColor>()
                                          << QColor("#cc0000")
                                          << QColor("#ff5f5f")
                                          << QColor("#7a0000") << text
                                          << accent << highlightedText)
            .name()
            .toLatin1();
    return {windowBuf.constData(), surfaceBuf.constData(),
            surfaceBuf.constData(), fieldBuf.constData(),
            borderBuf.constData(), borderBuf.constData(),
            textBuf.constData(), mutedBuf.constData(),
            accentBuf.constData(), accentBuf.constData(),
            accentBuf.constData(),
            successBuf.constData(), surfaceBuf.constData(),
            warningBuf.constData(), surfaceBuf.constData(),
            dangerBuf.constData(), surfaceBuf.constData(),
            selBuf.constData()};
  }
  if (dark) {
    return {"#1e2228", "#24292f", "#2b3139", "#191d23", "#384150",
            "#536070", "#f0f3f7", "#9aa5b4", "#7fb4ff", "#99c4ff",
            "#243550", "#6dcf8a", "#1f3828", "#edb55c", "#3f321d",
            "#ff8585", "#3e2527", "#0a1220"};
  }
  return {"#f4f6fa", "#ffffff", "#f8f9fc", "#ffffff", "#dce2eb",
          "#bcc5d2", "#1d2530", "#64707f", "#2e6edb", "#2460c0",
          "#e9f1ff", "#1f8a4e", "#e7f5ed", "#a56500", "#fff3db",
          "#c63e44", "#fcebee", "#ffffff"};
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
  border-radius: 5px;
  padding: 5px 10px;
}
QMenuBar::item:selected {
  background: %11;
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
QTabBar::tab:!selected {
  color: %8;
}
QTabBar::tab:!selected:hover {
  background: %11;
  color: %7;
}
QTabBar::close-button {
  border-radius: 4px;
  margin: 2px;
}
QTabBar::close-button:hover {
  background: %5;
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
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox {
  background: %4;
  color: %7;
  border: 1px solid %5;
  border-radius: 6px;
  padding: 5px 8px;
  min-height: 22px;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus,
QSpinBox:focus, QListView:focus, QTreeView:focus, QTableView:focus {
  border: 1px solid %9;
}
QLineEdit:read-only {
  background: %3;
  color: %8;
}
QLineEdit[metricValue="true"] {
  color: %7;
  font-weight: 600;
  font-variant-numeric: tabular-nums;
}
QLineEdit[fieldState="error"], QComboBox[fieldState="error"] {
  border: 1px solid %16;
  background: %17;
}
QLineEdit:disabled, QPlainTextEdit:disabled, QTextEdit:disabled,
QComboBox:disabled, QSpinBox:disabled {
  background: %3;
  color: %8;
  border-color: %5;
}
QComboBox::drop-down {
  border: 0;
  width: 26px;
}
QSpinBox::up-button, QSpinBox::down-button {
  border: 0;
  width: 24px;
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

/* ── Buttons ──────────────────────────────────────────── */
QPushButton, QToolButton {
  background: %3;
  border: 1px solid %5;
  border-radius: 6px;
  padding: 6px 14px;
  min-height: 26px;
  color: %7;
}
QPushButton:focus, QToolButton:focus {
  border-color: %9;
  outline: none;
}
QPushButton:hover, QToolButton:hover {
  background: %11;
  border-color: %6;
}
QPushButton:pressed, QToolButton:pressed {
  background: %9;
  color: %18;
}
QPushButton:disabled, QToolButton:disabled {
  color: %8;
  background: %2;
  border-color: %5;
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
QToolButton[compact="true"] {
  padding: 4px;
  min-width: 30px;
  min-height: 30px;
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
  color: %8;
  padding: 32px 24px;
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

/* ── List and tree views ──────────────────────────────── */
QListView, QTreeView, QTableView {
  background: %2;
  alternate-background-color: %3;
  border: 1px solid %5;
  border-radius: 6px;
  show-decoration-selected: 1;
  outline: 0;
}
QListView::item, QTreeView::item, QTableView::item {
  min-height: 28px;
  padding: 4px 8px;
  border-radius: 4px;
}
QListView::item:selected, QTreeView::item:selected, QTableView::item:selected {
  background: %11;
  color: %7;
}
QListView::item:selected:active, QTreeView::item:selected:active,
QTableView::item:selected:active {
  background: %9;
  color: %18;
}
QListView::item:hover, QTreeView::item:hover, QTableView::item:hover {
  background: %3;
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
  border-radius: 5px;
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
QScrollBar:vertical {
  background: transparent;
  border: 0;
  width: 10px;
  margin: 0;
}
QScrollBar::handle:vertical {
  background: %5;
  border-radius: 4px;
  min-height: 32px;
  margin: 2px;
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
  height: 10px;
  margin: 0;
}
QScrollBar::handle:horizontal {
  background: %5;
  border-radius: 4px;
  min-width: 32px;
  margin: 2px;
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
  border-radius: 5px;
  padding: 6px 8px;
}
)")
      .arg(t.window, t.surface, t.surfaceRaised, t.field, t.border,
           t.borderStrong, t.text, t.muted, t.accent, t.accentHover,
           t.accentSoft, t.success, t.successSoft, t.warning, t.warningSoft,
           t.danger, t.dangerSoft, t.selectionText);
}

void setBoolProperty(QWidget *widget, const char *name, bool value) {
  if (!widget) {
    return;
  }
  widget->setProperty(name, value);
  UiPolish::Repolish(widget);
}

} // namespace

namespace UiPolish {

QColor BestContrastingColor(const QColor &background,
                            const QList<QColor> &candidates) {
  return bestContrastingColor(background, candidates);
}

void ApplyApplicationStyle(bool dark) { qApp->setStyleSheet(styleFor(tones(dark))); }

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
  label->setAlignment(Qt::AlignCenter);
  label->setWordWrap(true);
  label->setTextFormat(Qt::RichText);
  const QString escapedTitle = title.toHtmlEscaped();
  const QString escapedDetail = detail.toHtmlEscaped();
  label->setText(
      detail.isEmpty()
          ? QString("<span style='font-size:13px; font-weight:600;'>%1</span>")
                .arg(escapedTitle)
          : QString("<span style='font-size:13px; font-weight:600;'>%1</span>"
                    "<br><span style='font-size:12px; opacity:0.7;'>%2</span>")
                .arg(escapedTitle, escapedDetail));
  Repolish(label);
}

void SetNotice(QLabel *label, const QString &text) {
  if (!label) {
    return;
  }
  label->setProperty("notice", true);
  label->setAccessibleName("Information");
  label->setWordWrap(true);
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
  button->setToolTip(QString("%1 - show details").arg(text));
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
  if (minimumSize.isValid()) {
    widget->setMinimumSize(minimumSize);
  }
}

void SetAccessibleFormField(QWidget *widget, const QString &name,
                            const QString &description) {
  if (!widget)
    return;
  widget->setAccessibleName(name);
  if (!description.isEmpty())
    widget->setAccessibleDescription(description);
}

bool IsHighContrastActive() {
  QPalette pal = QApplication::palette();
  QColor bg = pal.color(QPalette::Window);
  QColor fg = pal.color(QPalette::WindowText);
  int bgLum = qGray(bg.rgb());
  int fgLum = qGray(fg.rgb());
  double contrast = (qMax(bgLum, fgLum) + 0.05) /
                    (qMin(bgLum, fgLum) + 0.05);
  return contrast > 12.0;
}

} // namespace UiPolish
