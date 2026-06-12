#include "interface_polish.h"

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

Tone tones(bool dark) {
  if (dark) {
    return {"#1f2329", "#252a31", "#2d333c", "#171b20", "#3b4450",
            "#566273", "#f2f5f8", "#aab4c0", "#7fb4ff", "#9bc5ff",
            "#24354d", "#72d38f", "#203c2a", "#f1b85f", "#44351f",
            "#ff8a8a", "#442829", "#09111d"};
  }
  return {"#f5f7fb", "#ffffff", "#f9fafc", "#ffffff", "#d8dee8",
          "#b8c2d0", "#202733", "#667181", "#2f6fdd", "#245fc2",
          "#eaf2ff", "#208a4f", "#e8f6ee", "#a86600", "#fff3dc",
          "#c83f45", "#fdecee", "#ffffff"};
}

QString styleFor(const Tone &t) {
  return QString(R"(
QMainWindow, QDialog {
  background: %1;
  color: %7;
}
QWidget {
  color: %7;
  selection-background-color: %9;
  selection-color: %18;
}
QMenuBar {
  background: %1;
  border-bottom: 1px solid %5;
  padding: 2px 4px;
}
QMenuBar::item {
  background: transparent;
  border-radius: 4px;
  padding: 5px 10px;
}
QMenuBar::item:selected {
  background: %11;
}
QMenuBar::item:pressed {
  background: %3;
}
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
  padding: 7px 14px;
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
  background: %17;
}
QScrollArea {
  border: 0;
  background: transparent;
}
QGroupBox {
  border: 1px solid %5;
  border-radius: 8px;
  margin-top: 16px;
  padding: 12px 10px 10px 10px;
  background: %2;
}
QGroupBox::title {
  subcontrol-origin: margin;
  left: 10px;
  padding: 0 5px;
  color: %8;
  font-weight: 600;
}
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox {
  background: %4;
  color: %7;
  border: 1px solid %5;
  border-radius: 6px;
  padding: 5px 7px;
  min-height: 22px;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus,
QSpinBox:focus, QListView:focus, QTreeView:focus {
  border: 1px solid %9;
}
QLineEdit:read-only {
  background: %3;
  color: %8;
}
QLineEdit[metricValue="true"] {
  color: %7;
  font-weight: 600;
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
QComboBox::drop-down, QSpinBox::up-button, QSpinBox::down-button {
  border: 0;
  width: 24px;
}
QCheckBox, QRadioButton {
  spacing: 8px;
  min-height: 24px;
}
QPushButton, QToolButton {
  background: %3;
  border: 1px solid %5;
  border-radius: 6px;
  padding: 6px 12px;
  min-height: 26px;
  color: %7;
}
QPushButton:focus, QToolButton:focus {
  border-color: %9;
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
}
QLabel[emptyState="true"] {
  color: %8;
  padding: 24px;
}
QLabel[notice="true"] {
  background: %11;
  border: 1px solid %5;
  border-radius: 6px;
  color: %7;
  padding: 8px 10px;
}
QLabel[validationState="error"] {
  background: %17;
  border: 1px solid %16;
  border-radius: 6px;
  color: %16;
  padding: 6px 8px;
}
QLabel[validationState="success"] {
  background: %13;
  border: 1px solid %12;
  border-radius: 6px;
  color: %12;
  padding: 6px 8px;
}
QLabel[muted="true"] {
  color: %8;
}
QListView, QTreeView {
  background: %2;
  alternate-background-color: %3;
  border: 1px solid %5;
  border-radius: 6px;
  show-decoration-selected: 1;
  outline: 0;
}
QListView::item, QTreeView::item {
  min-height: 28px;
  padding: 4px 8px;
}
QListView::item:selected, QTreeView::item:selected {
  background: %11;
  color: %7;
}
QListView::item:selected:active, QTreeView::item:selected:active {
  background: %9;
  color: %18;
}
QListView::item:hover, QTreeView::item:hover {
  background: %3;
}
QHeaderView::section {
  background: %3;
  color: %8;
  border: 0;
  border-bottom: 1px solid %5;
  padding: 6px 8px;
  font-weight: 600;
}
QSplitter::handle {
  background: %1;
}
QSplitter::handle:hover {
  background: %11;
}
QProgressBar {
  border: 1px solid %5;
  border-radius: 5px;
  background: %3;
  text-align: center;
  min-height: 18px;
}
QProgressBar::chunk {
  background: %9;
  border-radius: 4px;
}
QStatusBar {
  background: %3;
  color: %8;
  border-top: 1px solid %5;
}
QStatusBar::item {
  border: 0;
}
QMenu {
  background: %2;
  border: 1px solid %5;
}
QMenu::item {
  padding: 6px 24px 6px 18px;
}
QMenu::item:selected {
  background: %11;
}
QDialogButtonBox QPushButton {
  min-width: 86px;
}
QScrollBar:vertical {
  background: %2;
  border: 0;
  width: 12px;
  margin: 0;
}
QScrollBar::handle:vertical {
  background: %5;
  border-radius: 4px;
  min-height: 28px;
  margin: 2px;
}
QScrollBar::handle:vertical:hover {
  background: %6;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
  height: 0;
}
QScrollBar:horizontal {
  background: %2;
  border: 0;
  height: 12px;
  margin: 0;
}
QScrollBar::handle:horizontal {
  background: %5;
  border-radius: 4px;
  min-width: 28px;
  margin: 2px;
}
QScrollBar::handle:horizontal:hover {
  background: %6;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
  width: 0;
}
QToolTip {
  color: %7;
  background-color: %2;
  border: 1px solid %6;
  padding: 5px;
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
  label->setText(detail.isEmpty()
                     ? escapedTitle
                     : QString("<b>%1</b><br><span>%2</span>")
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

} // namespace UiPolish
