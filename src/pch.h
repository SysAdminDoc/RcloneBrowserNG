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

#ifdef _MSC_VER
#pragma warning pop
#endif
