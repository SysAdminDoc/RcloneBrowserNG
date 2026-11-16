#pragma once

#include "pch.h"

// What a finished task tells its heartbeat and webhook URLs. The app and a
// scheduled --run-task both send these, so a monitor sees the same thing
// whichever of them ran the task.
namespace TaskNotify {

// The heartbeat URL itself on success, its /fail path otherwise.
QUrl HeartbeatEndpoint(QUrl endpoint, bool success);

// The webhook's JSON body. "content" repeats it as one sentence so a Discord
// webhook shows it as a message.
QByteArray WebhookBody(const QString &taskName, bool success,
                       const QString &error);

// For --run-task, which exits as soon as this returns: sends whichever of
// the two URLs is set and waits until both are answered or 15 seconds pass.
// Problems go to the app log, since nobody is watching a window.
void SendAndWait(const QString &heartbeatUrl, const QString &webhookUrl,
                 const QString &taskName, bool success, const QString &error);

} // namespace TaskNotify
