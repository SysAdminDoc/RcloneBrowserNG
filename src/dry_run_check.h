#pragma once

#include "pch.h"

// A dry run rclone runs as a separate process can still change files: a
// section's global.dry_run in rclone.conf, or an RCLONE_CONFIG_ variable,
// turns --dry-run off when the path opens (addConfigToContext in
// fs/newfs.go, v1.76.0). The daemon's sync/* calls skip that, and
// RcloneRcEngine checks its own core/command route, so this covers the
// rclone processes the app starts itself and the remote rclone's
// core/command.
namespace DryRunCheck {

// Empty when the dry run stays dry, else why it doesn't start.
using Callback = std::function<void(const QString &refusal)>;

// Runs `rclone config dump` the way args would run, through the remote
// rclone when one is set up and on this PC otherwise, and calls back with
// what in rclone.conf or the environment would turn the dry run off.
// Arguments that aren't a dry run call back empty right away. environment
// is what the rclone on this PC gets; empty means this PC's own.
void Run(const QStringList &args, const QProcessEnvironment &environment,
         QObject *context, const Callback &callback);

// The refusal for `rclone config dump` output: exitCode and output from
// the dump, environment as NAME=value.
QString RefusalFor(int exitCode, const QByteArray &output,
                   const QStringList &environment);

} // namespace DryRunCheck
