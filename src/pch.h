#pragma once

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#include <memory>

#include <QtCore>
#include <QtDebug>
#include <QtGui>
#include <QtNetwork>
#include <QtWidgets>
#include <QRegularExpression>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#if defined(Q_OS_WIN32)
#include <QtWinExtras>
#endif
#ifdef Q_OS_MACOS
#include <QtMacExtras>
#endif
#endif

// Qt 5's QtWinExtras pulled the Windows headers in transitively; with
// Qt 6 they have to be included explicitly for the shell/COM/process APIs
#if defined(Q_OS_WIN32)
#include <qt_windows.h>
#include <objbase.h>
#include <shellapi.h>
#endif

#ifdef _MSC_VER
#pragma warning pop
#endif
