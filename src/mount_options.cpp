#include "mount_options.h"

#include "utils.h"

namespace {

const QString kBalanced = QStringLiteral("balanced");
const QString kStreaming = QStringLiteral("streaming");
const QString kOffline = QStringLiteral("offline");
const QString kCustom = QStringLiteral("custom");

const QVector<MountPreset> &presets() {
  static const QVector<MountPreset> values = {
      {kBalanced, QStringLiteral("Balanced (recommended)"),
       QStringLiteral("--vfs-cache-mode writes --dir-cache-time 5m "
                      "--poll-interval 1m")},
      {kStreaming, QStringLiteral("Streaming / low disk use"),
       QStringLiteral("--vfs-cache-mode off --dir-cache-time 5m "
                      "--poll-interval 1m")},
      {kOffline, QStringLiteral("Offline-friendly / full cache"),
       QStringLiteral("--vfs-cache-mode full --vfs-cache-max-age 24h "
                      "--vfs-cache-max-size 10G --dir-cache-time 5m "
                      "--poll-interval 1m")},
      {kCustom, QStringLiteral("Custom (expert)"), QString()},
  };
  return values;
}

const MountPreset *findPreset(const QString &id) {
  for (const MountPreset &preset : presets()) {
    if (preset.id == id) {
      return &preset;
    }
  }
  return nullptr;
}

QString normalizedFlags(const QStringList &arguments) {
  return arguments.join(QChar('\x1f'));
}

bool hasFlag(const QStringList &arguments, const QString &flag) {
  for (const QString &argument : arguments) {
    if (argument == flag || argument.startsWith(flag + '=')) {
      return true;
    }
  }
  return false;
}

bool hasOptionFamily(const QStringList &arguments, const QString &prefix) {
  for (const QString &argument : arguments) {
    if (argument == prefix || argument.startsWith(prefix + '=')) {
      return true;
    }
  }
  return false;
}

MountOptionValidation invalid(const QString &message) {
  MountOptionValidation result;
  result.valid = false;
  result.error = message;
  return result;
}

} // namespace

QVector<MountPreset> MountPresets() { return presets(); }

MountOptionState LoadMountOptionState(const QSettings &settings) {
  const QString savedPreset =
      settings.value("Settings/mountPreset").toString().trimmed();
  const QString legacyOptions =
      settings.value("Settings/mount").toString().trimmed();

  if (findPreset(savedPreset)) {
    return {savedPreset, legacyOptions};
  }

  if (legacyOptions.isEmpty() ||
      legacyOptions == QStringLiteral("--vfs-cache-mode writes")) {
    return {kBalanced, QString()};
  }

  const QStringList legacyArguments = SplitRcloneOptions(legacyOptions);
  for (const MountPreset &preset : presets()) {
    if (preset.id == kCustom) {
      continue;
    }
    if (normalizedFlags(legacyArguments) ==
        normalizedFlags(MountPresetArguments(preset.id))) {
      return {preset.id, QString()};
    }
  }

  // A pre-preset installation may have arbitrary mount flags here. Keep them
  // as expert options rather than silently replacing or duplicating them.
  return {kCustom, legacyOptions};
}

QStringList MountPresetArguments(const QString &presetId) {
  const MountPreset *preset = findPreset(presetId);
  return preset ? SplitRcloneOptions(preset->flags) : QStringList();
}

QString MountPresetFlags(const QString &presetId) {
  const MountPreset *preset = findPreset(presetId);
  return preset ? preset->flags : QString();
}

MountOptionValidation ValidateMountOptions(const QString &presetId,
                                           const QString &expertOptions,
                                           bool readOnly, bool driveShared) {
  Q_UNUSED(readOnly);
  const MountPreset *preset = findPreset(presetId);
  if (!preset) {
    return invalid(QStringLiteral("Choose a valid mount preset."));
  }

  const QStringList presetArguments = MountPresetArguments(presetId);
  const QStringList expertArguments = SplitRcloneOptions(expertOptions);
  const QStringList allArguments = presetArguments + expertArguments;

  if (presetId != kCustom &&
      hasOptionFamily(expertArguments, QStringLiteral("--vfs-cache-mode"))) {
    return invalid(
        QStringLiteral("Expert options repeat --vfs-cache-mode from the "
                       "selected preset. Choose Custom (expert) or remove "
                       "the duplicate flag."));
  }

  if (driveShared &&
      hasFlag(expertArguments, QStringLiteral("--drive-shared-with-me"))) {
    return invalid(QStringLiteral(
        "Expert options repeat --drive-shared-with-me, which is managed by "
        "the selected shared-drive remote."));
  }

  const bool cacheOff = hasOptionFamily(allArguments,
                                        QStringLiteral("--vfs-cache-mode=off")) ||
                        (allArguments.contains(QStringLiteral("--vfs-cache-mode")) &&
                         allArguments.indexOf(QStringLiteral("--vfs-cache-mode")) +
                                 1 <
                             allArguments.size() &&
                         allArguments.at(allArguments.indexOf(
                                             QStringLiteral("--vfs-cache-mode")) +
                                         1) == QStringLiteral("off"));
  if (cacheOff) {
    const QStringList incompatible = {
        QStringLiteral("--vfs-cache-max-age"),
        QStringLiteral("--vfs-cache-max-size"),
        QStringLiteral("--vfs-cache-min-free-space"),
        QStringLiteral("--vfs-cache-poll-interval"),
    };
    for (const QString &flag : incompatible) {
      if (hasOptionFamily(allArguments, flag)) {
        return invalid(QStringLiteral("%1 is incompatible with "
                                      "--vfs-cache-mode off.")
                           .arg(flag));
      }
    }
  }

  return {};
}

QStringList BuildMountOptions(const QString &presetId,
                              const QString &expertOptions, bool readOnly,
                              bool driveShared, QString *error) {
  const MountOptionValidation validation =
      ValidateMountOptions(presetId, expertOptions, readOnly, driveShared);
  if (error) {
    *error = validation.error;
  }
  if (!validation.valid) {
    return {};
  }

  QStringList result = MountPresetArguments(presetId);
  result.append(SplitRcloneOptions(expertOptions));
  if (driveShared) {
    result << QStringLiteral("--drive-shared-with-me");
    if (!hasFlag(result, QStringLiteral("--read-only"))) {
      result << QStringLiteral("--read-only");
    }
  } else if (readOnly &&
             !hasFlag(result, QStringLiteral("--read-only"))) {
    result << QStringLiteral("--read-only");
  }
  return result;
}
