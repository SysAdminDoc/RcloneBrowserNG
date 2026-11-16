#pragma once

#include "pch.h"

namespace UiPolish {

void ApplyApplicationStyle(bool dark);

void Repolish(QWidget *widget);
void SetCard(QWidget *widget);
void SetToolbarSurface(QWidget *widget);
void SetActionBar(QWidget *widget);
void SetEmptyState(QLabel *label, const QString &title,
                   const QString &detail = QString());
void SetNotice(QLabel *label, const QString &text);
void SetValidationMessage(QLabel *label, const QString &state,
                          const QString &text);
void SetMuted(QWidget *widget);
void SetFieldState(QWidget *widget, const QString &state);
void SetNavigationView(QAbstractItemView *view,
                       const QString &accessibleName = QString());
void SetCompactToolButton(QAbstractButton *button,
                          const QString &accessibleName,
                          const QString &toolTip = QString());
void SetDisclosureButton(QToolButton *button, const QString &accessibleName,
                         const QString &toolTip = QString());
void SetPrimaryButton(QAbstractButton *button);
void SetDestructiveButton(QAbstractButton *button);
void SetStatus(QAbstractButton *button, const QString &status,
               const QString &text);
void SetPathField(QLineEdit *lineEdit, const QString &accessibleName);
void SetReadOnlyValue(QLineEdit *lineEdit,
                      const QString &accessibleName = QString());
void SetOutputView(QPlainTextEdit *output,
                   const QString &accessibleName = QString());
void SetTableView(QTableView *view,
                  const QString &accessibleName = QString());
void SetDialogButtonBox(QDialogButtonBox *buttons);
void SetWindowDefaults(QWidget *widget, QSize minimumSize = QSize());
// Wraps every page in a scroll area that scrolls vertically only, so a tall
// tab never pushes its dialog past the screen.
void MakeTabPagesScrollable(QTabWidget *tabs);
// Shows the tab that holds the widget (pages wrapped by
// MakeTabPagesScrollable included) and scrolls it into view.
bool ShowTabContaining(QTabWidget *tabs, QWidget *widget);

void SetAccessibleFormField(QWidget *widget, const QString &name,
                            const QString &description = QString());
bool IsHighContrastActive();
// A file glyph in the accent color, for buttons. The style's own file icon
// is a white page with a pale outline that nearly vanishes on a light button.
QIcon FileIcon();
QColor BestContrastingColor(const QColor &background,
                            const QList<QColor> &candidates);

} // namespace UiPolish
