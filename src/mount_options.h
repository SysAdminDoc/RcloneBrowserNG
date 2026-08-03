#pragma once

#include "pch.h"

struct MountPreset {
  QString id;
  QString label;
  QString flags;
};

struct MountOptionState {
  QString presetId;
  QString expertOptions;
};

struct MountOptionValidation {
  bool valid = true;
  QString error;
};

QVector<MountPreset> MountPresets();
MountOptionState LoadMountOptionState(const QSettings &settings);
QStringList MountPresetArguments(const QString &presetId);
QString MountPresetFlags(const QString &presetId);
MountOptionValidation ValidateMountOptions(const QString &presetId,
                                           const QString &expertOptions,
                                           bool readOnly, bool driveShared);
QStringList BuildMountOptions(const QString &presetId,
                              const QString &expertOptions, bool readOnly,
                              bool driveShared, QString *error = nullptr);
