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
void SetMuted(QWidget *widget);
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
void SetWindowDefaults(QWidget *widget, QSize minimumSize = QSize());

} // namespace UiPolish
