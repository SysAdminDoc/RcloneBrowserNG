#include "filter_pattern.h"

namespace FilterPattern {

namespace {

// Turn a glob into a concrete path so the example reads like a real file
// rather than the pattern repeated back at the user.
QString instantiate(const QString &pattern) {
  QString out = pattern;
  // ** crosses directory separators; * does not.
  out.replace(QStringLiteral("**"), QStringLiteral("sub/file"));
  out.replace(QChar('*'), QStringLiteral("name"));
  out.replace(QChar('?'), QChar('x'));

  // {a,b} alternation: show the first alternative.
  static const QRegularExpression alternation(QStringLiteral("\\{([^,}]*)[^}]*\\}"));
  QRegularExpressionMatch match = alternation.match(out);
  while (match.hasMatch()) {
    out.replace(match.capturedStart(), match.capturedLength(),
                match.captured(1));
    match = alternation.match(out);
  }

  // [abc] character class: show the first character.
  static const QRegularExpression charClass(QStringLiteral("\\[(.)[^\\]]*\\]"));
  match = charClass.match(out);
  while (match.hasMatch()) {
    out.replace(match.capturedStart(), match.capturedLength(),
                match.captured(1));
    match = charClass.match(out);
  }

  return out;
}

} // namespace

Description Describe(const QString &pattern) {
  Description description;
  const QString trimmed = pattern.trimmed();
  if (trimmed.isEmpty()) {
    return description;
  }
  description.valid = true;
  description.anchored = trimmed.startsWith(QChar('/'));
  description.directoryOnly = trimmed.endsWith(QChar('/'));

  QString body = trimmed;
  if (description.anchored) {
    body.remove(0, 1);
  }
  QString example = instantiate(body);
  if (example.endsWith(QChar('/'))) {
    // A directory pattern is clearer shown with something inside it.
    example += QStringLiteral("file");
  }
  if (example.isEmpty()) {
    description.valid = false;
    return description;
  }

  description.matchesExample = example;
  if (description.anchored) {
    // Verified against rclone v1.75.0: `--exclude /notes.tmp` removed
    // notes.tmp and left sub/notes.tmp alone.
    description.scope = QStringLiteral("only at the top level of the transfer");
    description.missesExample = QStringLiteral("sub/") + example;
  } else {
    // Verified against rclone v1.75.0: `--exclude *.tmp` removed both
    // notes.tmp and sub/notes.tmp.
    description.scope = QStringLiteral("at any depth");
    description.missesExample.clear();
  }

  return description;
}

QStringList DescribeAll(const QString &patternsText) {
  QStringList lines;
  const QStringList patterns =
      patternsText.split(QChar('\n'), Qt::SkipEmptyParts);
  for (const QString &pattern : patterns) {
    const Description description = Describe(pattern);
    if (!description.valid) {
      continue;
    }

    QString line = QString("%1: %2, matches %3")
                       .arg(pattern.trimmed(), description.scope,
                            description.matchesExample);
    if (!description.missesExample.isEmpty()) {
      line += QString(" but not %1").arg(description.missesExample);
    } else {
      line += QString(" and sub/%1").arg(description.matchesExample);
    }
    if (description.directoryOnly) {
      line += QStringLiteral(" (the whole directory)");
    }
    lines << line;
  }
  return lines;
}

} // namespace FilterPattern
