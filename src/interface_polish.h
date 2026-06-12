#pragma once

#include "pch.h"

namespace UiPolish {

void ApplyApplicationStyle(bool dark);

void Repolish(QWidget *widget);
void SetCard(QWidget *widget);
void SetToolbarSurface(QWidget *widget);
void SetEmptyState(QLabel *label, const QString &title,
                   const QString &detail = QString());
void SetMuted(QWidget *widget);
void SetCompactToolButton(QAbstractButton *button,
                          const QString &accessibleName,
                          const QString &toolTip = QString());
void SetPrimaryButton(QAbstractButton *button);
void SetDestructiveButton(QAbstractButton *button);
void SetStatus(QAbstractButton *button, const QString &status,
               const QString &text);
void SetPathField(QLineEdit *lineEdit, const QString &accessibleName);
void SetOutputView(QPlainTextEdit *output);
void SetWindowDefaults(QWidget *widget, QSize minimumSize = QSize());

} // namespace UiPolish
