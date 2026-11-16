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
      // Measured on v1.76.0: `--exclude "[!].txt"` exits with CRITICAL
      // "empty character class" and nothing runs.
      if (i + 2 < pattern.size() &&
          (pattern.at(i + 1) == QChar('!') || pattern.at(i + 1) == QChar('^')) &&
          pattern.at(i + 2) == QChar(']')) {
        return QStringLiteral("empty character class");
      }
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

bool isNegation(QChar c) { return c == QChar('!') || c == QChar('^'); }

// Whether c falls in a class body such as "a-z0-9_".
bool classHas(const QString &set, QChar c) {
  for (int k = 0; k < set.size(); k++) {
    if (k + 2 < set.size() && set.at(k + 1) == QChar('-')) {
      if (c >= set.at(k) && c <= set.at(k + 2)) {
        return true;
      }
      k += 2;
      continue;
    }
    if (set.at(k) == c) {
      return true;
    }
  }
  return false;
}

// A character a negated class matches: one outside the set, and never "/".
// Glob syntax is skipped too, or the rest of the example would read it as
// a new class ("[!!-Z_a-z][ab].txt" came out as "[.txt"), and so is what
// Windows refuses in a file name.
QChar outsideOf(const QString &set) {
  for (const QChar c : QStringLiteral("xzq0_X")) {
    if (!classHas(set, c)) {
      return c;
    }
  }
  static const QString kNotInExamples = QStringLiteral("/[]*?{}\\:<>\"|");
  for (char16_t code = u'!'; code <= u'~'; code++) {
    const QChar c(code);
    if (!kNotInExamples.contains(c) && !classHas(set, c)) {
      return c;
    }
  }
  return QChar();
}

// The index of the "]" closing the class opened at text[open], or -1. The
// first character of the body (after any negation) can't close it.
qsizetype classEnd(const QString &text, qsizetype open) {
  qsizetype first = open + 1;
  if (first < text.size() && isNegation(text.at(first))) {
    first++;
  }
  for (qsizetype j = first + 1; j < text.size(); j++) {
    if (text.at(j) == QChar(']')) {
      return j;
    }
  }
  return -1;
}

// Since v1.76.0 a class opening with "!" or "^" is negated, and neither
// form matches "/". Measured on v1.76.0 over a.txt, b.txt, x.txt, !.txt,
// ^.txt and sub/a.txt: `--exclude "[!a].txt"` and `--exclude "[^a].txt"`
// each excluded every top-level file but a.txt, and `sub[!x]a.txt` left
// sub/a.txt alone.
bool expandFirstCharacterClass(QString &text) {
  for (qsizetype i = 0; i < text.size(); i++) {
    if (text.at(i) == QChar('\\')) {
      i++;
      continue;
    }
    if (text.at(i) != QChar('[')) {
      continue;
    }
    const qsizetype end = classEnd(text, i);
    if (end < 0) {
      return false;
    }
    const QString body = text.mid(i + 1, end - i - 1);
    QString pick = body.left(1);
    if (isNegation(body.at(0))) {
      const QChar outside = outsideOf(body.mid(1));
      if (outside.isNull()) {
        return false;
      }
      pick = QString(outside);
    }
    text.replace(i, end - i + 1, pick);
    return true;
  }
  return false;
}

// The first "[!...]" or "[^...]" in pattern, as written, or empty.
QString firstNegatedClass(const QString &pattern) {
  for (qsizetype i = 0; i + 1 < pattern.size(); i++) {
    if (pattern.at(i) == QChar('\\')) {
      i++;
      continue;
    }
    if (pattern.at(i) != QChar('[')) {
      continue;
    }
    const qsizetype end = classEnd(pattern, i);
    if (end < 0) {
      return QString();
    }
    if (isNegation(pattern.at(i + 1))) {
      return pattern.mid(i, end - i + 1);
    }
    i = end;
  }
  return QString();
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
  description.negatedClass = firstNegatedClass(body);
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
    if (!description.negatedClass.isEmpty()) {
      line += QString(". %1 matches one character that isn't listed, and "
                      "never a /")
                  .arg(description.negatedClass);
    }
    lines << line;
  }
  return lines;
}

} // namespace FilterPattern
