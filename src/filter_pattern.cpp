#include "filter_pattern.h"

#include <string>

namespace FilterPattern {

namespace {

using Runes = std::u32string;

QString Text(const Runes &runes) {
  return QString::fromUcs4(runes.data(), static_cast<qsizetype>(runes.size()));
}

void Append(QString &text, char32_t c) { text += QString::fromUcs4(&c, 1); }

// What rclone makes of an exclude pattern before Go's regexp package sees it.
struct Translation {
  // The regular expression rclone compiles, when problem is empty.
  Runes regexp;
  // Why rclone refuses the glob itself.
  QString problem;
  // A {{regexp}} section, or a trailing backslash that escapes the closing
  // $, which the explainer doesn't try to describe.
  bool opaque = false;
  // The first "[!...]" or "[^...]" class as written, and where it starts
  // and ends in the regexp. Go can close a class at a different ] than the
  // glob does ("[!a[b]c]" is the class [^/a[b] then c]), so the class is
  // only named when the two agree.
  QString negatedClass;
  size_t negatedOpen = Runes::npos;
  size_t negatedEnd = Runes::npos;
};

Translation Refused(Translation translation, const char *problem) {
  translation.problem = QString::fromLatin1(problem);
  translation.regexp.clear();
  return translation;
}

// rclone 1.76.0's globToRegexp (fs/filter/glob.go) for an exclude, followed
// step by step so the explainer refuses what rclone refuses and reads a
// class the way it does. Inside [...] every character goes through to the
// regexp as written: a backslash is a regexp escape there ("[!\x]" is
// refused), and "[" nests ("[[:alpha:]]" is a POSIX class). Outside a
// class a backslash passes the next character through too, so "\d" is a
// digit and "\x" is refused.
Translation Translate(const Runes &glob) {
  Translation out;
  Runes &re = out.regexp;
  size_t i = 0;
  if (!glob.empty() && glob[0] == U'/') {
    i = 1;
    re += U'^';
  } else {
    re += U"(^|/)";
  }
  int stars = 0;
  const auto insertStars = [&re, &stars]() {
    if (stars > 2) {
      return false;
    }
    if (stars == 1) {
      re += U"[^/]*";
    } else if (stars == 2) {
      re += U".*";
    }
    stars = 0;
    return true;
  };
  int braceDepth = 0;
  int inBrackets = 0;
  bool bracketStart = false;
  bool bracketNegated = false;
  bool slashed = false;
  bool inRegexp = false;
  bool inRegexpEnd = false;
  size_t classOpen = 0;
  size_t classReOpen = 0;
  bool classNegated = false;
  char32_t next = 0;
  char32_t last = 0;
  for (; i < glob.size(); i++) {
    const char32_t c = glob[i];
    last = next;
    next = c;
    if (slashed) {
      re += c;
      slashed = false;
      continue;
    }
    if (inRegexpEnd) {
      if (c == U'}') {
        re.back() = U'}';
        re += U')';
        continue;
      }
      inRegexpEnd = false;
    }
    if (inRegexp) {
      if (c == U'}' && last == U'}') {
        inRegexp = false;
        inRegexpEnd = true;
        re.back() = U')';
      } else {
        re += c;
      }
      continue;
    }
    if (c != U'*' && !insertStars()) {
      return Refused(out, "too many stars");
    }
    if (inBrackets > 0) {
      if (bracketStart && (c == U'!' || c == U'^')) {
        bracketStart = false;
        bracketNegated = true;
        classNegated = true;
        re += U"^/";
        continue;
      }
      if (bracketNegated && c == U']') {
        return Refused(out, "empty character class");
      }
      bracketStart = false;
      bracketNegated = false;
      re += c;
      if (c == U'[') {
        inBrackets++;
      }
      if (c == U']') {
        inBrackets--;
        if (inBrackets == 0 && classNegated && out.negatedClass.isEmpty()) {
          out.negatedClass = Text(glob.substr(classOpen, i - classOpen + 1));
          out.negatedOpen = classReOpen;
          out.negatedEnd = re.size();
        }
      }
      continue;
    }
    switch (c) {
    case U'\\':
      re += c;
      slashed = true;
      break;
    case U'*':
      stars++;
      break;
    case U'?':
      re += U"[^/]";
      break;
    case U'[':
      classReOpen = re.size();
      re += c;
      inBrackets++;
      bracketStart = true;
      classOpen = i;
      classNegated = false;
      break;
    case U']':
      return Refused(out, "mismatched ']'");
    case U'{':
      if (braceDepth > 0 && last == U'{') {
        inRegexp = true;
        braceDepth--;
        out.opaque = true;
      } else {
        braceDepth++;
        re += U'(';
      }
      break;
    case U'}':
      if (braceDepth <= 0) {
        return Refused(out, "mismatched '{' and '}'");
      }
      re += U')';
      braceDepth--;
      break;
    case U',':
      re += braceDepth > 0 ? U'|' : U',';
      break;
    case U'.':
    case U'+':
    case U'(':
    case U')':
    case U'|':
    case U'^':
    case U'$':
      re += U'\\';
      re += c;
      break;
    default:
      re += c;
      break;
    }
  }
  if (!insertStars()) {
    return Refused(out, "too many stars");
  }
  if (inBrackets > 0) {
    return Refused(out, "mismatched '[' and ']'");
  }
  if (braceDepth != 0) {
    return Refused(out, "mismatched '{' and '}'");
  }
  if (inRegexp) {
    return Refused(out, "mismatched '{{' and '}}'");
  }
  if (slashed) {
    out.opaque = true;
  }
  re += U'$';
  return out;
}

struct Range {
  char32_t lo;
  char32_t hi;
};
using Ranges = QList<Range>;

bool InRanges(const Ranges &ranges, char32_t c) {
  for (const Range &range : ranges) {
    if (c >= range.lo && c <= range.hi) {
      return true;
    }
  }
  return false;
}

// Go's regexp/syntax perl_groups.go.
Ranges PerlGroup(char32_t letter) {
  switch (letter) {
  case U'd':
    return {{U'0', U'9'}};
  case U's':
    return {{U'\t', U'\n'}, {U'\f', U'\r'}, {U' ', U' '}};
  case U'w':
    return {{U'0', U'9'}, {U'A', U'Z'}, {U'_', U'_'}, {U'a', U'z'}};
  default:
    return {};
  }
}

bool IsPerlLetter(char32_t c) {
  return c == U'd' || c == U'D' || c == U's' || c == U'S' || c == U'w' ||
         c == U'W';
}

bool PosixGroup(const QString &name, Ranges *ranges) {
  static const QHash<QString, Ranges> kGroups{
      {"alnum", {{U'0', U'9'}, {U'A', U'Z'}, {U'a', U'z'}}},
      {"alpha", {{U'A', U'Z'}, {U'a', U'z'}}},
      {"ascii", {{0, 0x7F}}},
      {"blank", {{U'\t', U'\t'}, {U' ', U' '}}},
      {"cntrl", {{0, 0x1F}, {0x7F, 0x7F}}},
      {"digit", {{U'0', U'9'}}},
      {"graph", {{U'!', U'~'}}},
      {"lower", {{U'a', U'z'}}},
      {"print", {{U' ', U'~'}}},
      {"punct", {{U'!', U'/'}, {U':', U'@'}, {U'[', U'`'}, {U'{', U'~'}}},
      {"space", {{U'\t', U'\r'}, {U' ', U' '}}},
      {"upper", {{U'A', U'Z'}}},
      {"word", {{U'0', U'9'}, {U'A', U'Z'}, {U'_', U'_'}, {U'a', U'z'}}},
      {"xdigit", {{U'0', U'9'}, {U'A', U'F'}, {U'a', U'f'}}},
  };
  const auto found = kGroups.constFind(name);
  if (found == kGroups.constEnd()) {
    return false;
  }
  *ranges = found.value();
  return true;
}

// The one-letter Unicode category a character is in, as Go's unicode.C, L,
// M, N, P, S and Z tables have it, or 0 for one that isn't assigned.
char MajorCategory(char32_t c) {
  switch (QChar::category(c)) {
  case QChar::Letter_Uppercase:
  case QChar::Letter_Lowercase:
  case QChar::Letter_Titlecase:
  case QChar::Letter_Modifier:
  case QChar::Letter_Other:
    return 'L';
  case QChar::Mark_NonSpacing:
  case QChar::Mark_SpacingCombining:
  case QChar::Mark_Enclosing:
    return 'M';
  case QChar::Number_DecimalDigit:
  case QChar::Number_Letter:
  case QChar::Number_Other:
    return 'N';
  case QChar::Punctuation_Connector:
  case QChar::Punctuation_Dash:
  case QChar::Punctuation_Open:
  case QChar::Punctuation_Close:
  case QChar::Punctuation_InitialQuote:
  case QChar::Punctuation_FinalQuote:
  case QChar::Punctuation_Other:
    return 'P';
  case QChar::Symbol_Math:
  case QChar::Symbol_Currency:
  case QChar::Symbol_Modifier:
  case QChar::Symbol_Other:
    return 'S';
  case QChar::Separator_Space:
  case QChar::Separator_Line:
  case QChar::Separator_Paragraph:
    return 'Z';
  case QChar::Other_Control:
  case QChar::Other_Format:
  case QChar::Other_Surrogate:
  case QChar::Other_PrivateUse:
    return 'C';
  case QChar::Other_NotAssigned:
    break;
  }
  return 0;
}

// A regexp character class, or the class a \d or \pL escape stands for.
struct CharClass {
  Ranges ranges;
  // [:^alpha:], \D, \S and \W: everything outside one of these.
  QList<Ranges> outside;
  // \pL and \PL by their one-letter category.
  QString categories;
  QString outsideCategories;
  bool negated = false;
  // \p{Greek}: Go has a table for it, but the explainer doesn't.
  bool uncertain = false;
  bool hasFirst = false;
  char32_t first = 0;
};

bool Has(const CharClass &cls, char32_t c) {
  bool in = InRanges(cls.ranges, c);
  for (const Ranges &group : cls.outside) {
    if (!InRanges(group, c)) {
      in = true;
    }
  }
  const char major = MajorCategory(c);
  if (major != 0 && cls.categories.contains(QLatin1Char(major))) {
    in = true;
  }
  for (const QChar letter : cls.outsideCategories) {
    if (letter.toLatin1() != major) {
      in = true;
    }
  }
  return in != cls.negated;
}

// A space is left out: an example that is or ends in one can't be read.
bool Visible(char32_t c) {
  const char major = MajorCategory(c);
  return major == 'L' || major == 'N' || major == 'P' || major == 'S';
}

// A character the class matches, for the example. A plain class gives its
// first member ("[0-9]" is 0). Otherwise an ordinary letter or digit is
// tried first, then other ASCII, then a few accented letters, passing over
// glob syntax, which reads like a pattern in the example, and what Windows
// refuses in a file name. Those only come up when nothing else is in the
// class ("[*]" matches only *).
bool Pick(const CharClass &cls, char32_t *pick) {
  if (cls.uncertain) {
    return false;
  }
  static const QString kNotInExamples = QStringLiteral("/[]*?{}\\:<>\"|");
  const auto readable = [&cls](char32_t c) {
    return Visible(c) &&
           !(c < 0x80 && kNotInExamples.contains(QChar(char16_t(c)))) &&
           Has(cls, c);
  };
  if (!cls.negated && cls.hasFirst && readable(cls.first)) {
    *pick = cls.first;
    return true;
  }
  static const char32_t kPreferred[] = {U'x', U'z', U'q', U'0', U'_', U'X'};
  for (const char32_t c : kPreferred) {
    if (readable(c)) {
      *pick = c;
      return true;
    }
  }
  for (char32_t c = U'!'; c <= U'~'; c++) {
    if (readable(c)) {
      *pick = c;
      return true;
    }
  }
  // e with acute, sharp s, o with stroke, n with tilde, u with diaeresis,
  // c with cedilla, a with ring.
  static const char32_t kAccented[] = {0xE9, 0xDF, 0xF8, 0xF1, 0xFC, 0xE7, 0xE5};
  for (const char32_t c : kAccented) {
    if (readable(c)) {
      *pick = c;
      return true;
    }
  }
  for (char32_t c = 0x20; c < 0x30000; c++) {
    if ((c < 0xD800 || c > 0xDFFF) && Visible(c) && Has(cls, c)) {
      *pick = c;
      return true;
    }
  }
  return false;
}

bool IsAsciiAlnum(char32_t c) {
  return (c >= U'0' && c <= U'9') || (c >= U'A' && c <= U'Z') ||
         (c >= U'a' && c <= U'z');
}

int Unhex(char32_t c) {
  if (c >= U'0' && c <= U'9') {
    return static_cast<int>(c - U'0');
  }
  if (c >= U'a' && c <= U'f') {
    return static_cast<int>(c - U'a') + 10;
  }
  if (c >= U'A' && c <= U'F') {
    return static_cast<int>(c - U'A') + 10;
  }
  return -1;
}

// Go's regexp/syntax parser, for the parts a translated glob can hold, and
// the example: the first branch of each group, a class's pick, "name" for
// * and "sub/file" for **.
struct Regexp {
  explicit Regexp(const Runes &text) : re(text) {}

  const Runes &re;
  size_t at = 0;
  QString problem;
  QString example;
  // Go might take it or refuse it, and the explainer can't tell which.
  bool uncertain = false;
  // Valid, but the example would need a character that can't be shown.
  bool noExample = false;
  // Where the class starting at watchClass ends.
  size_t watchClass = Runes::npos;
  size_t watchedClassEnd = Runes::npos;

  void Parse() {
    Alternation(true);
    if (problem.isEmpty() && at < re.size()) {
      uncertain = true;
    }
  }

  bool Fail(const QString &why) {
    problem = why;
    return false;
  }

  void Alternation(bool output) {
    bool firstBranch = true;
    for (;;) {
      Sequence(output && firstBranch);
      if (!problem.isEmpty() || at >= re.size() || re[at] != U'|') {
        return;
      }
      at++;
      firstBranch = false;
    }
  }

  void Sequence(bool output) {
    qsizetype atomStart = -1;
    bool atomAny = false;
    while (at < re.size() && problem.isEmpty()) {
      const char32_t c = re[at];
      if (c == U'|' || c == U')') {
        return;
      }
      if (c == U'*') {
        at++;
        if (atomStart < 0) {
          uncertain = true;
          continue;
        }
        if (output) {
          example.truncate(atomStart);
          example += atomAny ? QStringLiteral("sub/file") : QStringLiteral("name");
        }
        atomStart = -1;
        continue;
      }
      atomStart = -1;
      atomAny = false;
      if (c == U'(') {
        at++;
        Alternation(output);
        if (!problem.isEmpty()) {
          return;
        }
        if (at >= re.size()) {
          uncertain = true;
          return;
        }
        at++;
        continue;
      }
      if (c == U'^' || c == U'$') {
        at++;
        continue;
      }
      const qsizetype start = example.size();
      if (c == U'.') {
        at++;
        atomAny = true;
        if (output && (at >= re.size() || re[at] != U'*')) {
          noExample = true;
        }
      } else if (c == U'[') {
        CharClass cls;
        if (!Class(&cls)) {
          return;
        }
        EmitClass(output, cls);
      } else if (c == U'\\') {
        if (!EscapeAtom(output)) {
          return;
        }
      } else {
        at++;
        if (output) {
          EmitRune(c);
        }
      }
      atomStart = start;
    }
  }

  void EmitRune(char32_t c) {
    if (c < 0x20 || c == 0x7F) {
      noExample = true;
      return;
    }
    Append(example, c);
  }

  void EmitClass(bool output, const CharClass &cls) {
    if (cls.uncertain) {
      uncertain = true;
    }
    if (!output) {
      return;
    }
    char32_t pick = 0;
    if (Pick(cls, &pick)) {
      Append(example, pick);
    } else {
      noExample = true;
    }
  }

  static void AddPerl(CharClass *cls, char32_t letter) {
    const bool outside = letter == U'D' || letter == U'S' || letter == U'W';
    const Ranges group =
        PerlGroup(outside ? static_cast<char32_t>(letter + 0x20) : letter);
    if (outside) {
      cls->outside.append(group);
    } else {
      cls->ranges += group;
    }
  }

  // A backslash outside a class.
  bool EscapeAtom(bool output) {
    if (at + 1 >= re.size()) {
      return Fail(QStringLiteral("trailing backslash"));
    }
    const char32_t letter = re[at + 1];
    switch (letter) {
    case U'A':
    case U'b':
    case U'B':
    case U'z':
      at += 2;
      if (output) {
        noExample = true;
      }
      return true;
    case U'C':
      return Fail(QStringLiteral("invalid escape sequence \\C"));
    case U'Q': {
      // \Q...\E is literal text. It can swallow the closing $, so it's left
      // undescribed.
      const size_t end = re.find(U"\\E", at + 2);
      at = end == Runes::npos ? re.size() : end + 2;
      if (output) {
        noExample = true;
      }
      return true;
    }
    case U'p':
    case U'P': {
      CharClass cls;
      if (!UnicodeClass(&at, &cls)) {
        return false;
      }
      EmitClass(output, cls);
      return true;
    }
    default:
      break;
    }
    if (IsPerlLetter(letter)) {
      CharClass cls;
      AddPerl(&cls, letter);
      at += 2;
      EmitClass(output, cls);
      return true;
    }
    char32_t rune = 0;
    if (!Escape(&at, &rune)) {
      return false;
    }
    if (output) {
      EmitRune(rune);
    }
    return true;
  }

  // Go's parseEscape, from the backslash at *from.
  bool Escape(size_t *from, char32_t *rune) {
    const size_t start = *from;
    size_t t = start + 1;
    if (t >= re.size()) {
      return Fail(QStringLiteral("trailing backslash"));
    }
    const char32_t c = re[t++];
    const auto invalid = [this, start, &t]() {
      return Fail(QStringLiteral("invalid escape sequence ") +
                  Text(re.substr(start, t - start)));
    };
    if (c < 0x80 && !IsAsciiAlnum(c)) {
      *rune = c;
      *from = t;
      return true;
    }
    if (c >= U'1' && c <= U'7' &&
        (t >= re.size() || re[t] < U'0' || re[t] > U'7')) {
      // A single digit would be a backreference, which Go doesn't have.
      return invalid();
    }
    if (c >= U'0' && c <= U'7') {
      char32_t value = c - U'0';
      for (int k = 1; k < 3 && t < re.size() && re[t] >= U'0' && re[t] <= U'7';
           k++) {
        value = value * 8 + (re[t] - U'0');
        t++;
      }
      *rune = value;
      *from = t;
      return true;
    }
    if (c == U'x') {
      if (t >= re.size()) {
        return invalid();
      }
      const char32_t c2 = re[t++];
      if (c2 == U'{') {
        int digits = 0;
        char32_t value = 0;
        for (;;) {
          if (t >= re.size()) {
            return invalid();
          }
          const char32_t h = re[t++];
          if (h == U'}') {
            break;
          }
          const int v = Unhex(h);
          if (v < 0) {
            return invalid();
          }
          value = value * 16 + static_cast<char32_t>(v);
          if (value > 0x10FFFF) {
            return invalid();
          }
          digits++;
        }
        if (digits == 0) {
          return invalid();
        }
        *rune = value;
        *from = t;
        return true;
      }
      if (t >= re.size()) {
        return invalid();
      }
      const int x = Unhex(c2);
      const int y = Unhex(re[t++]);
      if (x < 0 || y < 0) {
        return invalid();
      }
      *rune = static_cast<char32_t>(x * 16 + y);
      *from = t;
      return true;
    }
    switch (c) {
    case U'a':
      *rune = 0x07;
      break;
    case U'f':
      *rune = 0x0C;
      break;
    case U'n':
      *rune = 0x0A;
      break;
    case U'r':
      *rune = 0x0D;
      break;
    case U't':
      *rune = 0x09;
      break;
    case U'v':
      *rune = 0x0B;
      break;
    default:
      return invalid();
    }
    *from = t;
    return true;
  }

  // \pL, \PL, and inside a class \p{Name}, from the backslash at *from. A
  // glob turns { into ( outside a class, so there only a one-letter name
  // can reach Go, and Go has those for C, L, M, N, P, S and Z alone.
  bool UnicodeClass(size_t *from, CharClass *cls) {
    const size_t start = *from;
    bool include = re[start + 1] == U'p';
    size_t t = start + 2;
    Runes name;
    if (t < re.size() && re[t] == U'{') {
      const size_t end = re.find(U'}', start);
      if (end == Runes::npos) {
        return Fail(QStringLiteral("invalid character class range ") +
                    Text(re.substr(start)));
      }
      name = re.substr(start + 3, end - start - 3);
      t = end + 1;
    } else if (t < re.size()) {
      name = re.substr(t, 1);
      t++;
    }
    const QString written = Text(re.substr(start, t - start));
    *from = t;
    if (!name.empty() && name[0] == U'^') {
      include = !include;
      name.erase(0, 1);
    }
    static const Runes kMajor = U"CLMNPSZ";
    if (name.size() == 1 && kMajor.find(name[0]) != Runes::npos) {
      (include ? cls->categories : cls->outsideCategories) +=
          QChar(char16_t(name[0]));
      return true;
    }
    if (name.size() <= 1) {
      return Fail(QStringLiteral("invalid character class range ") + written);
    }
    cls->uncertain = true;
    return true;
  }

  bool ClassChar(size_t *t, char32_t *rune) {
    if (*t >= re.size()) {
      return Fail(QStringLiteral("missing closing ]"));
    }
    if (re[*t] == U'\\') {
      return Escape(t, rune);
    }
    *rune = re[(*t)++];
    return true;
  }

  // Go's parseClass, from the [ at `at`.
  bool Class(CharClass *cls) {
    size_t t = at + 1;
    if (t < re.size() && re[t] == U'^') {
      cls->negated = true;
      t++;
    }
    bool firstItem = true;
    while (t >= re.size() || re[t] != U']' || firstItem) {
      firstItem = false;
      if (re.size() - t > 2 && re[t] == U'[' && re[t + 1] == U':') {
        const size_t close = re.find(U":]", t + 2);
        if (close != Runes::npos) {
          const Runes name = re.substr(t, close + 2 - t);
          QString group = Text(name.substr(2, name.size() - 4));
          const bool outside = group.startsWith(QLatin1Char('^'));
          if (outside) {
            group.remove(0, 1);
          }
          Ranges ranges;
          if (!PosixGroup(group, &ranges)) {
            return Fail(QStringLiteral("invalid character class range ") +
                        Text(name));
          }
          if (outside) {
            cls->outside.append(ranges);
          } else {
            cls->ranges += ranges;
          }
          t = close + 2;
          continue;
        }
      }
      if (re.size() - t >= 2 && re[t] == U'\\' &&
          (re[t + 1] == U'p' || re[t + 1] == U'P')) {
        if (!UnicodeClass(&t, cls)) {
          return false;
        }
        continue;
      }
      if (re.size() - t >= 2 && re[t] == U'\\' && IsPerlLetter(re[t + 1])) {
        AddPerl(cls, re[t + 1]);
        t += 2;
        continue;
      }
      const size_t rangeStart = t;
      char32_t lo = 0;
      if (!ClassChar(&t, &lo)) {
        return false;
      }
      char32_t hi = lo;
      if (re.size() - t >= 2 && re[t] == U'-' && re[t + 1] != U']') {
        t++;
        if (!ClassChar(&t, &hi)) {
          return false;
        }
        if (hi < lo) {
          return Fail(QStringLiteral("invalid character class range ") +
                      Text(re.substr(rangeStart, t - rangeStart)));
        }
      }
      cls->ranges.append(Range{lo, hi});
      if (!cls->hasFirst) {
        cls->hasFirst = true;
        cls->first = lo;
      }
    }
    if (at == watchClass) {
      watchedClassEnd = t + 1;
    }
    at = t + 1;
    return true;
  }
};

} // namespace

Description Describe(const QString &pattern) {
  Description description;
  const QString trimmed = pattern.trimmed();
  if (trimmed.isEmpty()) {
    return description;
  }
  description.valid = true;

  const QList<uint> ucs4 = trimmed.toUcs4();
  const Runes glob(ucs4.cbegin(), ucs4.cend());
  const Translation translation = Translate(glob);
  if (!translation.problem.isEmpty()) {
    description.wellFormed = false;
    description.problem = translation.problem;
    return description;
  }
  if (translation.opaque) {
    description.valid = false;
    return description;
  }
  Regexp regexp(translation.regexp);
  regexp.watchClass = translation.negatedOpen;
  regexp.Parse();
  if (!regexp.problem.isEmpty()) {
    // Measured on v1.76.0: `--exclude "[!z-a].txt"` exits 1 with "bad glob
    // pattern" and nothing runs.
    description.wellFormed = false;
    description.problem = regexp.problem;
    return description;
  }
  if (regexp.uncertain || regexp.noExample) {
    description.valid = false;
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

  QString example = regexp.example;
  if (example.endsWith(QChar('/'))) {
    // A directory pattern is clearer shown with something inside it.
    example += QStringLiteral("file");
  }
  if (example.isEmpty()) {
    description.valid = false;
    return description;
  }

  description.matchesExample = example;
  if (regexp.watchedClassEnd == translation.negatedEnd) {
    description.negatedClass = translation.negatedClass;
  }
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
