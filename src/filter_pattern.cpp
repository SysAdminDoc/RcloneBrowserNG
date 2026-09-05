#include "filter_pattern.h"

namespace FilterPattern {

namespace {

// rclone refuses the whole run on an unbalanced glob rather than skipping the
// rule, so this is a hard error and not a cosmetic one. Measured on v1.75.0:
// `--exclude "a{b"` and `--exclude "["` both exit 1 with CRITICAL and copy
// nothing.
QString braceProblem(const QString &pattern) {
  int braces = 0;
  int brackets = 0;
  for (int i = 0; i < pattern.size(); i++) {
    const QChar c = pattern.at(i);
    if (c == QChar('\\')) {
      i++; // escaped, so it is a literal and does not open anything
      continue;
    }
    if (c == QChar('{')) {
      braces++;
    } else if (c == QChar('}')) {
      braces--;
    } else if (c == QChar('[')) {
      brackets++;
    } else if (c == QChar(']')) {
      brackets--;
    }
    if (braces < 0) {
      return QStringLiteral("mismatched '{' and '}'");
    }
    if (brackets < 0) {
      return QStringLiteral("mismatched '[' and ']'");
    }
  }
  if (braces != 0) {
    return QStringLiteral("mismatched '{' and '}'");
  }
  if (brackets != 0) {
    return QStringLiteral("mismatched '[' and ']'");
  }
  return QString();
}

// Replace the first {..} group with its first top-level alternative. Written
// as a scanner rather than a regex because the alternatives can nest:
// "{a,{b,c}}" has to give "a", and a regex that stops at the first "}" gives
// the nonsense "a}".
bool expandFirstAlternation(QString &text) {
  int open = -1;
  for (int i = 0; i < text.size(); i++) {
    if (text.at(i) == QChar('\\')) {
      i++;
      continue;
    }
    if (text.at(i) == QChar('{')) {
      open = i;
      break;
    }
  }
  if (open < 0) {
    return false;
  }

  int depth = 0;
  int firstComma = -1;
  for (int i = open; i < text.size(); i++) {
    const QChar c = text.at(i);
    if (c == QChar('\\')) {
      i++;
      continue;
    }
    if (c == QChar('{')) {
      depth++;
    } else if (c == QChar('}')) {
      depth--;
      if (depth == 0) {
        const int end = firstComma >= 0 ? firstComma : i;
        const QString first = text.mid(open + 1, end - open - 1);
        text.replace(open, i - open + 1, first);
        return true;
      }
    } else if (c == QChar(',') && depth == 1 && firstComma < 0) {
      firstComma = i;
    }
  }
  return false;
}

bool expandFirstCharacterClass(QString &text) {
  for (int i = 0; i < text.size(); i++) {
    if (text.at(i) == QChar('\\')) {
      i++;
      continue;
    }
    if (text.at(i) != QChar('[')) {
      continue;
    }
    for (int j = i + 1; j < text.size(); j++) {
      if (text.at(j) == QChar(']') && j > i + 1) {
        text.replace(i, j - i + 1, text.mid(i + 1, 1));
        return true;
      }
    }
    return false;
  }
  return false;
}

// Turn a glob into a concrete path so the example reads like a real file
// rather than the pattern repeated back at the user.
QString instantiate(const QString &pattern) {
  QString out = pattern;
  // ** crosses directory separators; * does not.
  out.replace(QStringLiteral("**"), QStringLiteral("sub/file"));
  out.replace(QChar('*'), QStringLiteral("name"));
  out.replace(QChar('?'), QChar('x'));

  while (expandFirstAlternation(out)) {
  }
  while (expandFirstCharacterClass(out)) {
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

  const QString problem = braceProblem(trimmed);
  if (!problem.isEmpty()) {
    description.wellFormed = false;
    description.problem = problem;
    return description;
  }

  description.anchored = trimmed.startsWith(QChar('/'));
  description.directoryOnly = trimmed.endsWith(QChar('/'));

  QString body = trimmed;
  if (description.anchored) {
    body.remove(0, 1);
  }
  // "**" matches across separators, so anchoring does not confine it to the
  // top level. Verified on rclone v1.75.0: `--exclude "/**"` removed every
  // file in the tree and `--exclude "/**.tmp"` removed sub/sub/file.tmp.
  description.crossesDirectories = body.startsWith(QStringLiteral("**"));

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
  if (description.anchored && !description.crossesDirectories) {
    // Verified against rclone v1.75.0: `--exclude /file.tmp` removed
    // file.tmp and left sub/file.tmp alone.
    description.scope = QStringLiteral("only at the top level of the transfer");
    description.missesExample = QStringLiteral("sub/") + example;
  } else if (description.anchored) {
    description.scope = QStringLiteral("everywhere below the transfer root");
    description.missesExample.clear();
  } else {
    // Verified against rclone v1.75.0: `--exclude *.tmp` removed both
    // file.tmp and sub/file.tmp.
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

    if (!description.wellFormed) {
      lines << QString("%1: rclone will refuse this filter and the transfer "
                       "will not run (%2)")
                   .arg(pattern.trimmed(), description.problem);
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
