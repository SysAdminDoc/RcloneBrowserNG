#include "selection_arguments.h"

namespace {

// Leaves headroom under the 32767-character Windows command line for the
// rclone binary, the source and destination paths, and the transfer flags.
constexpr int kMaxFilterCharacters = 24000;

} // namespace

namespace SelectionArguments {

QString EscapeFilterPattern(const QString &name) {
  QString escaped;
  escaped.reserve(name.size() + 8);
  for (const QChar c : name) {
    switch (c.unicode()) {
    case u'\\':
    case u'*':
    case u'?':
    case u'[':
    case u']':
    case u'{':
    case u'}':
      escaped.append(QChar(u'\\'));
      break;
    default:
      break;
    }
    escaped.append(c);
  }
  return escaped;
}

FilterRules BuildSelectionFilter(const QVector<SelectionEntry> &entries) {
  FilterRules result;

  QStringList rules;
  int characters = 0;
  for (const SelectionEntry &entry : entries) {
    if (entry.name.isEmpty()) {
      continue;
    }
    // Filter files and the --filter flag are line oriented, so a name holding
    // a line break cannot be expressed as a rule. Refuse rather than transfer
    // something the user did not select.
    if (entry.name.contains(QChar(u'\n')) || entry.name.contains(QChar(u'\r'))) {
      result.error =
          QString("\"%1\" contains a line break, which rclone filter rules "
                  "cannot express. Transfer it on its own instead.")
              .arg(entry.name.simplified());
      return result;
    }

    QString rule = "+ /" + EscapeFilterPattern(entry.name);
    if (entry.isDirectory) {
      rule += "/**";
    }
    characters += rule.size() + 10; // "--filter " plus quoting
    rules << rule;
  }

  if (rules.isEmpty()) {
    result.error = "Nothing in the selection could be transferred.";
    return result;
  }

  if (characters > kMaxFilterCharacters) {
    result.error =
        QString("The selection needs %1 characters of rclone filter rules, "
                "which will not fit on one command line. Select fewer items, "
                "or transfer the whole folder instead.")
            .arg(characters);
    return result;
  }

  for (const QString &rule : rules) {
    result.arguments << "--filter" << rule;
  }
  // Anything the rules above did not name stays behind.
  result.arguments << "--filter" << "- *";
  result.valid = true;
  return result;
}

} // namespace SelectionArguments
