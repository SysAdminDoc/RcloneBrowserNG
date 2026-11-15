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

// The Windows headers have to be included explicitly for the shell/COM/process APIs
#if defined(Q_OS_WIN32)
#include <qt_windows.h>
#include <objbase.h>
#include <shobjidl.h>
#include <shellapi.h>
#endif

#ifdef _MSC_VER
// Without the parentheses MSVC ignores this line, and its complaint about it
// is silenced by the push above, so every file built at warning level 0.
#pragma warning(pop)
#endif
