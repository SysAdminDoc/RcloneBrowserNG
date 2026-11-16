#include "filter_pattern.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace FilterPattern {

namespace {

using Runes = std::u32string;
constexpr size_t kNone = Runes::npos;

// Longer patterns get no line. rclone takes them, but Go refuses deep
// nesting somewhere past this, and nobody types a filter this long.
constexpr size_t kMaxGlob = 1000;
// Nesting deeper than this gets no line either.
constexpr int kMaxHeight = 150;
// Candidate examples kept for each part of the pattern, and their length.
constexpr qsizetype kMaxCandidates = 8;
constexpr qsizetype kMaxExample = 160;

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
  size_t negatedOpen = kNone;
  size_t negatedEnd = kNone;
  // Where the ".*" for the "**" rclone adds to a directory pattern starts.
  size_t tailDot = kNone;
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
// digit and "\x" is refused. Stars from tailFrom on are the ones rclone
// added.
Translation Translate(const Runes &glob, size_t tailFrom) {
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
  size_t starStart = 0;
  const auto insertStars = [&re, &stars, &starStart, &out, tailFrom]() {
    if (stars > 2) {
      return false;
    }
    if (stars == 1) {
      re += U"[^/]*";
    } else if (stars == 2) {
      if (starStart >= tailFrom) {
        out.tailDot = re.size();
      }
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
      if (stars == 0) {
        starStart = i;
      }
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

// Unicode general categories as bits, one per QChar::Category.
constexpr quint32 Bit(QChar::Category category) {
  return 1u << static_cast<unsigned>(category);
}

constexpr quint32 kCasedLetter = Bit(QChar::Letter_Uppercase) |
                                 Bit(QChar::Letter_Lowercase) |
                                 Bit(QChar::Letter_Titlecase);
constexpr quint32 kLetter =
    kCasedLetter | Bit(QChar::Letter_Modifier) | Bit(QChar::Letter_Other);
constexpr quint32 kMark = Bit(QChar::Mark_NonSpacing) |
                          Bit(QChar::Mark_SpacingCombining) |
                          Bit(QChar::Mark_Enclosing);
constexpr quint32 kNumber = Bit(QChar::Number_DecimalDigit) |
                            Bit(QChar::Number_Letter) |
                            Bit(QChar::Number_Other);
constexpr quint32 kPunctuation =
    Bit(QChar::Punctuation_Connector) | Bit(QChar::Punctuation_Dash) |
    Bit(QChar::Punctuation_Open) | Bit(QChar::Punctuation_Close) |
    Bit(QChar::Punctuation_InitialQuote) | Bit(QChar::Punctuation_FinalQuote) |
    Bit(QChar::Punctuation_Other);
constexpr quint32 kSymbol = Bit(QChar::Symbol_Math) |
                            Bit(QChar::Symbol_Currency) |
                            Bit(QChar::Symbol_Modifier) |
                            Bit(QChar::Symbol_Other);
constexpr quint32 kSeparator = Bit(QChar::Separator_Space) |
                               Bit(QChar::Separator_Line) |
                               Bit(QChar::Separator_Paragraph);
// Go's unicode.C takes in unassigned code points as well.
constexpr quint32 kOther =
    Bit(QChar::Other_Control) | Bit(QChar::Other_Format) |
    Bit(QChar::Other_Surrogate) | Bit(QChar::Other_PrivateUse) |
    Bit(QChar::Other_NotAssigned);
constexpr quint32 kUnassigned = Bit(QChar::Other_NotAssigned);

quint32 CategoryBit(char32_t c) { return Bit(QChar::category(c)); }

// Go 1.27.3's unicode.Categories.
bool CategoryMask(const QString &name, quint32 *mask) {
  static const QHash<QString, quint32> kMasks{
      {"C", kOther},
      {"Cc", Bit(QChar::Other_Control)},
      {"Cf", Bit(QChar::Other_Format)},
      {"Cn", kUnassigned},
      {"Co", Bit(QChar::Other_PrivateUse)},
      {"Cs", Bit(QChar::Other_Surrogate)},
      {"L", kLetter},
      {"LC", kCasedLetter},
      {"Ll", Bit(QChar::Letter_Lowercase)},
      {"Lm", Bit(QChar::Letter_Modifier)},
      {"Lo", Bit(QChar::Letter_Other)},
      {"Lt", Bit(QChar::Letter_Titlecase)},
      {"Lu", Bit(QChar::Letter_Uppercase)},
      {"M", kMark},
      {"Mc", Bit(QChar::Mark_SpacingCombining)},
      {"Me", Bit(QChar::Mark_Enclosing)},
      {"Mn", Bit(QChar::Mark_NonSpacing)},
      {"N", kNumber},
      {"Nd", Bit(QChar::Number_DecimalDigit)},
      {"Nl", Bit(QChar::Number_Letter)},
      {"No", Bit(QChar::Number_Other)},
      {"P", kPunctuation},
      {"Pc", Bit(QChar::Punctuation_Connector)},
      {"Pd", Bit(QChar::Punctuation_Dash)},
      {"Pe", Bit(QChar::Punctuation_Close)},
      {"Pf", Bit(QChar::Punctuation_FinalQuote)},
      {"Pi", Bit(QChar::Punctuation_InitialQuote)},
      {"Po", Bit(QChar::Punctuation_Other)},
      {"Ps", Bit(QChar::Punctuation_Open)},
      {"S", kSymbol},
      {"Sc", Bit(QChar::Symbol_Currency)},
      {"Sk", Bit(QChar::Symbol_Modifier)},
      {"Sm", Bit(QChar::Symbol_Math)},
      {"So", Bit(QChar::Symbol_Other)},
      {"Z", kSeparator},
      {"Zl", Bit(QChar::Separator_Line)},
      {"Zp", Bit(QChar::Separator_Paragraph)},
      {"Zs", Bit(QChar::Separator_Space)},
  };
  const auto found = kMasks.constFind(name);
  if (found == kMasks.constEnd()) {
    return false;
  }
  *mask = found.value();
  return true;
}

// Go 1.27.3's unicode.CategoryAliases, keyed by regexp/syntax's
// canonicalName.
const QHash<QString, QString> &CategoryAliases() {
  static const QHash<QString, QString> kAliases{
      {"Casedletter", "LC"},
      {"Closepunctuation", "Pe"},
      {"Cntrl", "Cc"},
      {"Combiningmark", "M"},
      {"Connectorpunctuation", "Pc"},
      {"Control", "Cc"},
      {"Currencysymbol", "Sc"},
      {"Dashpunctuation", "Pd"},
      {"Decimalnumber", "Nd"},
      {"Digit", "Nd"},
      {"Enclosingmark", "Me"},
      {"Finalpunctuation", "Pf"},
      {"Format", "Cf"},
      {"Initialpunctuation", "Pi"},
      {"Letter", "L"},
      {"Letternumber", "Nl"},
      {"Lineseparator", "Zl"},
      {"Lowercaseletter", "Ll"},
      {"Mark", "M"},
      {"Mathsymbol", "Sm"},
      {"Modifierletter", "Lm"},
      {"Modifiersymbol", "Sk"},
      {"Nonspacingmark", "Mn"},
      {"Number", "N"},
      {"Openpunctuation", "Ps"},
      {"Other", "C"},
      {"Otherletter", "Lo"},
      {"Othernumber", "No"},
      {"Otherpunctuation", "Po"},
      {"Othersymbol", "So"},
      {"Paragraphseparator", "Zp"},
      {"Privateuse", "Co"},
      {"Punct", "P"},
      {"Punctuation", "P"},
      {"Separator", "Z"},
      {"Spaceseparator", "Zs"},
      {"Spacingmark", "Mc"},
      {"Surrogate", "Cs"},
      {"Symbol", "S"},
      {"Titlecaseletter", "Lt"},
      {"Unassigned", "Cn"},
      {"Uppercaseletter", "Lu"},
  };
  return kAliases;
}

// Go 1.27.3's unicode.Scripts, by canonicalName. Go takes these, but the
// explainer has no tables for them.
const QSet<QString> &ScriptNames() {
  static const QSet<QString> kScripts{
      "Adlam", "Ahom", "Anatolianhieroglyphs", "Arabic", "Armenian",
      "Avestan", "Balinese", "Bamum", "Bassavah", "Batak", "Bengali",
      "Beriaerfe", "Bhaiksuki", "Bopomofo", "Brahmi", "Braille", "Buginese",
      "Buhid", "Canadianaboriginal", "Carian", "Caucasianalbanian", "Chakma",
      "Cham", "Cherokee", "Chorasmian", "Common", "Coptic", "Cuneiform",
      "Cypriot", "Cyprominoan", "Cyrillic", "Deseret", "Devanagari",
      "Divesakuru", "Dogra", "Duployan", "Egyptianhieroglyphs", "Elbasan",
      "Elymaic", "Ethiopic", "Garay", "Georgian", "Glagolitic", "Gothic",
      "Grantha", "Greek", "Gujarati", "Gunjalagondi", "Gurmukhi",
      "Gurungkhema", "Han", "Hangul", "Hanifirohingya", "Hanunoo", "Hatran",
      "Hebrew", "Hiragana", "Imperialaramaic", "Inherited",
      "Inscriptionalpahlavi", "Inscriptionalparthian", "Javanese", "Kaithi",
      "Kannada", "Katakana", "Kawi", "Kayahli", "Kharoshthi",
      "Khitansmallscript", "Khmer", "Khojki", "Khudawadi", "Kiratrai", "Lao",
      "Latin", "Lepcha", "Limbu", "Lineara", "Linearb", "Lisu", "Lycian",
      "Lydian", "Mahajani", "Makasar", "Malayalam", "Mandaic", "Manichaean",
      "Marchen", "Masaramgondi", "Medefaidrin", "Meeteimayek", "Mendekikakui",
      "Meroiticcursive", "Meroitichieroglyphs", "Miao", "Modi", "Mongolian",
      "Mro", "Multani", "Myanmar", "Nabataean", "Nagmundari", "Nandinagari",
      "Newa", "Newtailue", "Nko", "Nushu", "Nyiakengpuachuehmong", "Ogham",
      "Olchiki", "Oldhungarian", "Olditalic", "Oldnortharabian", "Oldpermic",
      "Oldpersian", "Oldsogdian", "Oldsoutharabian", "Oldturkic", "Olduyghur",
      "Olonal", "Oriya", "Osage", "Osmanya", "Pahawhhmong", "Palmyrene",
      "Paucinhau", "Phagspa", "Phoenician", "Psalterpahlavi", "Rejang",
      "Runic", "Samaritan", "Saurashtra", "Sharada", "Shavian", "Siddham",
      "Sidetic", "Signwriting", "Sinhala", "Sogdian", "Sorasompeng",
      "Soyombo", "Sundanese", "Sunuwar", "Sylotinagri", "Syriac", "Tagalog",
      "Tagbanwa", "Taile", "Taitham", "Taiviet", "Taiyo", "Takri", "Tamil",
      "Tangsa", "Tangut", "Telugu", "Thaana", "Thai", "Tibetan", "Tifinagh",
      "Tirhuta", "Todhri", "Tolongsiki", "Toto", "Tulutigalari", "Ugaritic",
      "Vai", "Vithkuqi", "Wancho", "Warangciti", "Yezidi", "Yi",
      "Zanabazarsquare",
  };
  return kScripts;
}

// Go's canonicalName: separators dropped, the first letter upper case and
// the rest lower case, so \pl is \pL and \p{letter} is \p{Letter}.
QString CanonicalName(const Runes &name) {
  QString out;
  bool first = true;
  for (char32_t c : name) {
    if (c == U'_' || c == U'-' || c == U' ') {
      continue;
    }
    if (first) {
      if (c >= U'a' && c <= U'z') {
        c -= U'a' - U'A';
      }
      first = false;
    } else if (c >= U'A' && c <= U'Z') {
      c += U'a' - U'A';
    }
    Append(out, c);
  }
  return out;
}

// A regexp character class, or the class a \d or \pL escape stands for.
struct CharClass {
  Ranges ranges;
  // [:^alpha:], \D, \S and \W: everything outside one of these.
  QList<Ranges> outside;
  // \pL, \p{Lu} and the like by category, and \PL, \P{Lu} as outsides.
  quint32 categories = 0;
  QList<quint32> outsideCategories;
  bool negated = false;
  // \p{Greek}: Go has a table for it, but the explainer doesn't.
  bool uncertain = false;
  bool hasFirst = false;
  char32_t first = 0;
  // Where it's written in the regexp.
  size_t from = 0;
  size_t to = 0;
};

bool Has(const CharClass &cls, char32_t c) {
  bool in = InRanges(cls.ranges, c);
  for (const Ranges &group : cls.outside) {
    if (!InRanges(group, c)) {
      in = true;
    }
  }
  const quint32 bit = CategoryBit(c);
  if ((cls.categories & bit) != 0) {
    in = true;
  }
  for (const quint32 mask : cls.outsideCategories) {
    if ((mask & bit) == 0) {
      in = true;
    }
  }
  return in != cls.negated;
}

// A space is left out: an example that is or ends in one can't be read.
bool Visible(char32_t c) {
  return (CategoryBit(c) & (kLetter | kNumber | kPunctuation | kSymbol)) != 0;
}

// A character the class matches, other than `avoid`, for the example. A
// plain class gives its first member ("[0-9]" is 0). Otherwise an ordinary
// letter or digit is tried first, then other ASCII, then a few accented
// letters, passing over glob syntax, which reads like a pattern in the
// example, and what Windows refuses in a file name. Those only come up when
// nothing else is in the class ("[*]" matches only *).
bool Pick(const CharClass &cls, char32_t avoid, char32_t *pick) {
  if (cls.uncertain) {
    return false;
  }
  static const QString kNotInExamples = QStringLiteral("/[]*?{}\\:<>\"|");
  const auto readable = [&cls, avoid](char32_t c) {
    return c != avoid && Visible(c) &&
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
  // Far enough for Latin, Greek, Cyrillic and most other alphabets, and
  // short enough that a class of marks fails fast.
  for (char32_t c = 0x20; c < 0x3000; c++) {
    if (c != avoid && Visible(c) && Has(cls, c)) {
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

enum class Op {
  NoMatch,
  Empty,
  Literal,
  Class,
  AnyNotNL,
  BeginText,
  EndText,
  Capture,
  Concat,
  Alternate,
  Star,
  Plus,
  Quest,
  // Markers on the parse stack, as in Go.
  LeftParen,
  VerticalBar,
};

struct Node {
  Op op = Op::Empty;
  char32_t rune = 0;
  int cls = -1;
  // The "." of the ".*" rclone appended for a directory pattern.
  bool tail = false;
  int height = 1;
  std::vector<int> subs;
};

// Go 1.27's regexp/syntax parser (parse.go), with rclone's flags: ^ and $
// are the start and end of the path, and . is any character but a newline.
// It works the way Go's does, from a stack and without recursion, so it
// reaches Go's first error for any pattern and a long one can't overflow
// the stack. A {n,m} repeat, (?flags), \b and a Unicode script name are
// taken where Go takes them but make the pattern uncertain.
struct Parser {
  explicit Parser(const Runes &text) : re(text) {}

  const Runes &re;
  std::vector<Node> nodes;
  std::vector<CharClass> classes;
  std::vector<int> stack;
  int root = -1;
  QString problem;
  // Go takes it, or may refuse it further on, and the explainer can't say
  // what it matches.
  bool uncertain = false;
  // Where the class starting at watchClass ends, and whether Go reads its
  // first member as a range ("[^/-a]" is / to a).
  size_t watchClass = kNone;
  size_t watchedEnd = kNone;
  bool watchedRange = false;
  size_t tailDot = kNone;

  bool Fail(const char *code, const QString &expr = QString()) {
    problem = QString::fromLatin1(code);
    if (!expr.isEmpty()) {
      problem += QChar(' ') + expr;
    }
    return false;
  }

  int Add(Op op, std::vector<int> subs = {}) {
    Node node;
    node.op = op;
    for (const int sub : subs) {
      node.height = (std::max)(node.height, nodes[sub].height + 1);
    }
    node.subs = std::move(subs);
    nodes.push_back(std::move(node));
    return static_cast<int>(nodes.size()) - 1;
  }

  void Push(int id) { stack.push_back(id); }

  bool IsMarker(int id) const {
    return nodes[id].op == Op::LeftParen || nodes[id].op == Op::VerticalBar;
  }

  void Literal(char32_t c) {
    const int id = Add(Op::Literal);
    nodes[id].rune = c;
    Push(id);
  }

  void PushClass(CharClass cls, size_t from, size_t to) {
    cls.from = from;
    cls.to = to;
    if (cls.uncertain) {
      uncertain = true;
    }
    classes.push_back(std::move(cls));
    const int id = Add(Op::Class);
    nodes[id].cls = static_cast<int>(classes.size()) - 1;
    Push(id);
  }

  // Go's concat and alternate: everything above the nearest ( or | becomes
  // one node.
  void Collapse(Op op) {
    size_t i = stack.size();
    while (i > 0 && !IsMarker(stack[i - 1])) {
      i--;
    }
    std::vector<int> subs(stack.begin() + static_cast<std::ptrdiff_t>(i),
                          stack.end());
    stack.resize(i);
    if (subs.empty()) {
      Push(Add(op == Op::Concat ? Op::Empty : Op::NoMatch));
    } else if (subs.size() == 1) {
      Push(subs[0]);
    } else {
      Push(Add(op, std::move(subs)));
    }
  }

  // Go's swapVerticalBar, less its merging of single characters into one
  // class, which matches the same text.
  bool SwapVerticalBar() {
    const size_t n = stack.size();
    if (n >= 2 && nodes[stack[n - 2]].op == Op::VerticalBar) {
      std::swap(stack[n - 2], stack[n - 1]);
      return true;
    }
    return false;
  }

  void VerticalBar() {
    Collapse(Op::Concat);
    if (!SwapVerticalBar()) {
      Push(Add(Op::VerticalBar));
    }
  }

  bool RightParen() {
    Collapse(Op::Concat);
    if (SwapVerticalBar()) {
      stack.pop_back();
    }
    Collapse(Op::Alternate);
    const size_t n = stack.size();
    if (n < 2) {
      return Fail("unexpected )");
    }
    const int inner = stack[n - 1];
    const int open = stack[n - 2];
    stack.resize(n - 2);
    if (nodes[open].op != Op::LeftParen) {
      return Fail("unexpected )");
    }
    Push(Add(Op::Capture, {inner}));
    return true;
  }

  // Go's repeat, for the operator at `before`, with *after just past it. A
  // {n,m} repeat (counted) is checked the way Go checks it and then stops
  // the parse.
  bool Repeat(Op op, size_t before, size_t *after, size_t lastRepeat,
              bool counted) {
    if (*after < re.size() && re[*after] == U'?') {
      ++*after;
    }
    if (lastRepeat != kNone) {
      return Fail("invalid nested repetition operator",
                  Text(re.substr(lastRepeat, *after - lastRepeat)));
    }
    if (stack.empty() || IsMarker(stack.back())) {
      return Fail("missing argument to repetition operator",
                  Text(re.substr(before, *after - before)));
    }
    if (counted) {
      uncertain = true;
      return false;
    }
    stack.back() = Add(op, {stack.back()});
    return true;
  }

  // Go's parseInt: digits with no leading zero, and -1 when too big.
  bool ParseInt(size_t *at, int *value) const {
    size_t t = *at;
    const auto digit = [this](size_t k) {
      return k < re.size() && re[k] >= U'0' && re[k] <= U'9';
    };
    if (!digit(t) || (re[t] == U'0' && digit(t + 1))) {
      return false;
    }
    int n = 0;
    for (; digit(t); t++) {
      if (n >= 100000000) {
        n = -1;
      } else if (n >= 0) {
        n = n * 10 + static_cast<int>(re[t] - U'0');
      }
    }
    *at = t;
    *value = n;
    return true;
  }

  // Go's parseRepeat, from the { at t.
  bool ParseRepeat(size_t t, int *low, int *high, size_t *after) const {
    t++;
    if (!ParseInt(&t, low) || t >= re.size()) {
      return false;
    }
    if (re[t] != U',') {
      *high = *low;
    } else {
      t++;
      if (t >= re.size()) {
        return false;
      }
      if (re[t] == U'}') {
        *high = -1;
      } else if (!ParseInt(&t, high)) {
        return false;
      } else if (*high < 0) {
        *low = -1;
      }
    }
    if (t >= re.size() || re[t] != U'}') {
      return false;
    }
    *after = t + 1;
    return true;
  }

  void Parse() {
    size_t t = 0;
    size_t lastRepeat = kNone;
    while (t < re.size()) {
      size_t repeat = kNone;
      const char32_t c = re[t];
      switch (c) {
      case U'(':
        if (t + 1 < re.size() && re[t + 1] == U'?') {
          uncertain = true;
          return;
        }
        Push(Add(Op::LeftParen));
        t++;
        break;
      case U'|':
        VerticalBar();
        t++;
        break;
      case U')':
        if (!RightParen()) {
          return;
        }
        t++;
        break;
      case U'^':
        Push(Add(Op::BeginText));
        t++;
        break;
      case U'$':
        Push(Add(Op::EndText));
        t++;
        break;
      case U'.': {
        const int id = Add(Op::AnyNotNL);
        nodes[id].tail = t == tailDot;
        Push(id);
        t++;
        break;
      }
      case U'[':
        if (!Class(&t)) {
          return;
        }
        break;
      case U'*':
      case U'+':
      case U'?': {
        const Op op = c == U'*' ? Op::Star : c == U'+' ? Op::Plus : Op::Quest;
        size_t after = t + 1;
        if (!Repeat(op, t, &after, lastRepeat, false)) {
          return;
        }
        repeat = t;
        t = after;
        break;
      }
      case U'{': {
        int low = 0;
        int high = 0;
        size_t after = 0;
        if (!ParseRepeat(t, &low, &high, &after)) {
          Literal(c);
          t++;
          break;
        }
        if (low < 0 || low > 1000 || high > 1000 || (high >= 0 && low > high)) {
          Fail("invalid repeat count", Text(re.substr(t, after - t)));
          return;
        }
        Repeat(Op::Empty, t, &after, lastRepeat, true);
        return;
      }
      case U'\\':
        if (!Backslash(&t)) {
          return;
        }
        break;
      default:
        Literal(c);
        t++;
        break;
      }
      lastRepeat = repeat;
    }
    Collapse(Op::Concat);
    if (SwapVerticalBar()) {
      stack.pop_back();
    }
    Collapse(Op::Alternate);
    if (stack.size() != 1) {
      Fail("missing closing )");
      return;
    }
    root = stack[0];
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
  bool Backslash(size_t *t) {
    const size_t at = *t;
    if (at + 1 < re.size()) {
      switch (re[at + 1]) {
      case U'A':
        Push(Add(Op::BeginText));
        *t = at + 2;
        return true;
      case U'b':
      case U'B':
        Push(Add(Op::Empty));
        uncertain = true;
        *t = at + 2;
        return true;
      case U'C':
        return Fail("invalid escape sequence", QStringLiteral("\\C"));
      case U'Q': {
        // \Q...\E is literal text. It can swallow the closing $, so it's
        // left undescribed.
        const size_t end = re.find(U"\\E", at + 2);
        const size_t stop = end == kNone ? re.size() : end;
        for (size_t k = at + 2; k < stop; k++) {
          Literal(re[k]);
        }
        *t = end == kNone ? re.size() : end + 2;
        uncertain = true;
        return true;
      }
      case U'z':
        Push(Add(Op::EndText));
        *t = at + 2;
        return true;
      default:
        break;
      }
      if (re[at + 1] == U'p' || re[at + 1] == U'P') {
        CharClass cls;
        size_t from = at;
        if (!UnicodeClass(&from, &cls)) {
          return false;
        }
        PushClass(std::move(cls), at, from);
        *t = from;
        return true;
      }
      if (IsPerlLetter(re[at + 1])) {
        CharClass cls;
        AddPerl(&cls, re[at + 1]);
        PushClass(std::move(cls), at, at + 2);
        *t = at + 2;
        return true;
      }
    }
    size_t from = at;
    char32_t rune = 0;
    if (!Escape(&from, &rune)) {
      return false;
    }
    Literal(rune);
    *t = from;
    return true;
  }

  // Go's parseEscape, from the backslash at *from.
  bool Escape(size_t *from, char32_t *rune) {
    const size_t start = *from;
    size_t t = start + 1;
    if (t >= re.size()) {
      return Fail("trailing backslash at end of expression");
    }
    const char32_t c = re[t++];
    const auto invalid = [this, start, &t]() {
      return Fail("invalid escape sequence", Text(re.substr(start, t - start)));
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

  // Go's parseUnicodeClass and unicodeTable, from the backslash at *from.
  // Inside a class the name can be in braces; outside one a glob has
  // already turned { into (, so only a one-letter name reaches Go there.
  bool UnicodeClass(size_t *from, CharClass *cls) {
    const size_t start = *from;
    int sign = re[start + 1] == U'p' ? 1 : -1;
    const size_t t = start + 2;
    Runes name;
    size_t end = t;
    if (t < re.size() && re[t] != U'{') {
      name = re.substr(t, 1);
      end = t + 1;
    } else if (t < re.size()) {
      const size_t close = re.find(U'}', start);
      if (close == kNone) {
        return Fail("invalid character class range", Text(re.substr(start)));
      }
      name = re.substr(start + 3, close - start - 3);
      end = close + 1;
    }
    const QString seq = Text(re.substr(start, end - start));
    *from = end;
    if (!name.empty() && name[0] == U'^') {
      sign = -sign;
      name.erase(0, 1);
    }
    const QString canonical = CanonicalName(name);
    const auto addRanges = [cls, sign](const Ranges &ranges) {
      if (sign > 0) {
        cls->ranges += ranges;
      } else {
        cls->outside.append(ranges);
      }
    };
    const auto addMask = [cls](quint32 mask, int maskSign) {
      if (maskSign > 0) {
        cls->categories |= mask;
      } else {
        cls->outsideCategories.append(mask);
      }
    };
    if (canonical == QLatin1String("Any")) {
      addRanges({{0, 0x10FFFF}});
      return true;
    }
    if (canonical == QLatin1String("Assigned")) {
      addMask(kUnassigned, -sign);
      return true;
    }
    if (canonical == QLatin1String("Ascii")) {
      addRanges({{0, 0x7F}});
      return true;
    }
    quint32 mask = 0;
    if (canonical == QLatin1String("Lc")) {
      addMask(kCasedLetter, sign);
      return true;
    }
    if (CategoryMask(canonical, &mask)) {
      addMask(mask, sign);
      return true;
    }
    if (ScriptNames().contains(canonical)) {
      cls->uncertain = true;
      return true;
    }
    const auto alias = CategoryAliases().constFind(canonical);
    if (alias != CategoryAliases().constEnd() &&
        CategoryMask(alias.value(), &mask)) {
      addMask(mask, sign);
      return true;
    }
    return Fail("invalid character class range", seq);
  }

  bool ClassChar(size_t *t, char32_t *rune) {
    if (*t >= re.size()) {
      return Fail("missing closing ]");
    }
    if (re[*t] == U'\\') {
      return Escape(t, rune);
    }
    *rune = re[(*t)++];
    return true;
  }

  // Go's parseClass, from the [ at *at.
  bool Class(size_t *at) {
    const size_t start = *at;
    CharClass cls;
    size_t t = start + 1;
    if (t < re.size() && re[t] == U'^') {
      cls.negated = true;
      t++;
    }
    bool firstItem = true;
    bool firstIsRange = false;
    while (t >= re.size() || re[t] != U']' || firstItem) {
      const bool atFirst = firstItem;
      firstItem = false;
      if (re.size() - t > 2 && re[t] == U'[' && re[t + 1] == U':') {
        const size_t close = re.find(U":]", t + 2);
        if (close != kNone) {
          const Runes written = re.substr(t, close + 2 - t);
          QString group = Text(written.substr(2, written.size() - 4));
          const bool outside = group.startsWith(QLatin1Char('^'));
          if (outside) {
            group.remove(0, 1);
          }
          Ranges ranges;
          if (!PosixGroup(group, &ranges)) {
            return Fail("invalid character class range", Text(written));
          }
          if (outside) {
            cls.outside.append(ranges);
          } else {
            cls.ranges += ranges;
          }
          t = close + 2;
          continue;
        }
      }
      if (re.size() - t >= 2 && re[t] == U'\\' &&
          (re[t + 1] == U'p' || re[t + 1] == U'P')) {
        if (!UnicodeClass(&t, &cls)) {
          return false;
        }
        continue;
      }
      if (re.size() - t >= 2 && re[t] == U'\\' && IsPerlLetter(re[t + 1])) {
        AddPerl(&cls, re[t + 1]);
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
          return Fail("invalid character class range",
                      Text(re.substr(rangeStart, t - rangeStart)));
        }
        firstIsRange = atFirst;
      }
      cls.ranges.append(Range{lo, hi});
      if (!cls.hasFirst) {
        cls.hasFirst = true;
        cls.first = lo;
      }
    }
    t++;
    if (start == watchClass) {
      watchedEnd = t;
      watchedRange = firstIsRange;
    }
    PushClass(std::move(cls), start, t);
    *at = t;
    return true;
  }
};

bool IsLeaf(const Parser &parser, int id) {
  const Op op = parser.nodes[id].op;
  return op == Op::Literal || op == Op::Class || op == Op::AnyNotNL;
}

bool LeafAccepts(const Parser &parser, int id, char32_t c) {
  const Node &node = parser.nodes[id];
  switch (node.op) {
  case Op::Literal:
    return c == node.rune;
  case Op::Class:
    return Has(parser.classes[node.cls], c);
  case Op::AnyNotNL:
    return c != U'\n';
  default:
    return false;
  }
}

// The parsed regexp as a Thompson NFA, run the way Go's MatchString runs
// it: a match may start anywhere. Each run costs the pattern's size times
// the path's length.
class Matcher {
public:
  explicit Matcher(const Parser &parser) : p(parser) {
    const Fragment whole = Compile(p.root);
    const int match = Emit(Kind::Match, -1);
    Patch(whole, match);
    start = whole.start;
  }

  bool Matches(const Runes &s) const {
    std::vector<size_t> mark(insts.size(), kNone);
    std::vector<int> current;
    std::vector<int> following;
    std::vector<int> work;
    const size_t length = s.size();
    bool matched = false;
    const auto add = [&](std::vector<int> &list, int pc, size_t pos) {
      work.push_back(pc);
      while (!work.empty()) {
        const int at = work.back();
        work.pop_back();
        if (at < 0 || mark[at] == pos) {
          continue;
        }
        mark[at] = pos;
        const Inst &inst = insts[at];
        switch (inst.kind) {
        case Kind::Leaf:
          list.push_back(at);
          break;
        case Kind::Split:
          work.push_back(inst.y);
          work.push_back(inst.x);
          break;
        case Kind::Jump:
          work.push_back(inst.x);
          break;
        case Kind::Begin:
          if (pos == 0) {
            work.push_back(inst.x);
          }
          break;
        case Kind::End:
          if (pos == length) {
            work.push_back(inst.x);
          }
          break;
        case Kind::Match:
          matched = true;
          break;
        case Kind::Fail:
          break;
        }
      }
    };
    for (size_t pos = 0;; pos++) {
      add(current, start, pos);
      if (matched) {
        return true;
      }
      if (pos == length) {
        return false;
      }
      for (const int pc : current) {
        if (LeafAccepts(p, insts[pc].node, s[pos])) {
          add(following, insts[pc].x, pos + 1);
        }
      }
      if (matched) {
        return true;
      }
      current.swap(following);
      following.clear();
    }
  }

private:
  enum class Kind { Leaf, Split, Jump, Begin, End, Fail, Match };
  struct Inst {
    Kind kind;
    int node;
    int x = -1;
    int y = -1;
  };
  // A compiled piece and its loose ends: an instruction and whether its y
  // (rather than x) still has to be pointed somewhere.
  struct Fragment {
    int start;
    std::vector<std::pair<int, bool>> outs;
  };

  const Parser &p;
  std::vector<Inst> insts;
  int start = -1;

  int Emit(Kind kind, int node) {
    insts.push_back(Inst{kind, node});
    return static_cast<int>(insts.size()) - 1;
  }

  Fragment Single(Kind kind, int node) {
    const int i = Emit(kind, node);
    return Fragment{i, {{i, false}}};
  }

  void Patch(const Fragment &fragment, int target) {
    for (const auto &[inst, ySlot] : fragment.outs) {
      (ySlot ? insts[inst].y : insts[inst].x) = target;
    }
  }

  Fragment Compile(int id) {
    const Node &node = p.nodes[id];
    switch (node.op) {
    case Op::Literal:
    case Op::Class:
    case Op::AnyNotNL:
      return Single(Kind::Leaf, id);
    case Op::BeginText:
      return Single(Kind::Begin, id);
    case Op::EndText:
      return Single(Kind::End, id);
    case Op::NoMatch:
      return Fragment{Emit(Kind::Fail, id), {}};
    case Op::Capture:
      return Compile(node.subs[0]);
    case Op::Concat: {
      Fragment whole = Compile(node.subs[0]);
      for (size_t k = 1; k < node.subs.size(); k++) {
        Fragment part = Compile(node.subs[k]);
        Patch(whole, part.start);
        whole.outs = std::move(part.outs);
      }
      return whole;
    }
    case Op::Alternate: {
      Fragment rest = Compile(node.subs.back());
      for (size_t k = node.subs.size() - 1; k-- > 0;) {
        Fragment branch = Compile(node.subs[k]);
        const int split = Emit(Kind::Split, id);
        insts[split].x = branch.start;
        insts[split].y = rest.start;
        branch.outs.insert(branch.outs.end(), rest.outs.begin(),
                           rest.outs.end());
        rest = Fragment{split, std::move(branch.outs)};
      }
      return rest;
    }
    case Op::Star: {
      const Fragment body = Compile(node.subs[0]);
      const int split = Emit(Kind::Split, id);
      insts[split].x = body.start;
      Patch(body, split);
      return Fragment{split, {{split, true}}};
    }
    case Op::Plus: {
      const Fragment body = Compile(node.subs[0]);
      const int split = Emit(Kind::Split, id);
      insts[split].x = body.start;
      Patch(body, split);
      return Fragment{body.start, {{split, true}}};
    }
    case Op::Quest: {
      Fragment body = Compile(node.subs[0]);
      const int split = Emit(Kind::Split, id);
      insts[split].x = body.start;
      body.outs.push_back({split, true});
      return Fragment{split, std::move(body.outs)};
    }
    default:
      return Single(Kind::Jump, id);
    }
  }
};

// Every pairing of a left and a right candidate, the earliest choices first.
QStringList Join(const QStringList &left, const QStringList &right) {
  QStringList out;
  const qsizetype sums = left.size() + right.size() - 1;
  for (qsizetype sum = 0; sum < sums && out.size() < kMaxCandidates; sum++) {
    for (qsizetype i = 0; i <= sum && out.size() < kMaxCandidates; i++) {
      const qsizetype j = sum - i;
      if (i >= left.size() || j >= right.size()) {
        continue;
      }
      const QString joined = left[i] + right[j];
      if (joined.size() <= kMaxExample &&
          !joined.contains(QStringLiteral("//")) && !out.contains(joined)) {
        out << joined;
      }
    }
  }
  return out;
}

// Candidate example paths for each part of the pattern: the first branch
// of an alternation first, a class's pick, "name" for * and "sub/file" for
// "**" (just "file" first for the "**" rclone adds to a directory
// pattern). The caller keeps the first one rclone would really exclude.
class Examples {
public:
  explicit Examples(const Parser &parser) : p(parser) {}

  QStringList Of(int id) {
    const Node &node = p.nodes[id];
    switch (node.op) {
    case Op::Empty:
    case Op::BeginText:
    case Op::EndText:
      return {QString()};
    case Op::Literal: {
      const char32_t c = node.rune;
      if (c < 0x20 || c == 0x7F || (c >= 0xD800 && c <= 0xDFFF)) {
        return {};
      }
      QString text;
      Append(text, c);
      return {text};
    }
    case Op::AnyNotNL:
      return {QStringLiteral("x")};
    case Op::Class:
      return Picks(p.classes[node.cls]);
    case Op::Capture:
    case Op::Plus:
      return Of(node.subs[0]);
    case Op::Concat: {
      QStringList joined{QString()};
      for (const int sub : node.subs) {
        joined = Join(joined, Of(sub));
        if (joined.isEmpty()) {
          return {};
        }
      }
      return joined;
    }
    case Op::Alternate: {
      QList<QStringList> branches;
      for (const int sub : node.subs) {
        branches << Of(sub);
      }
      QStringList out;
      for (qsizetype k = 0; out.size() < kMaxCandidates; k++) {
        bool more = false;
        for (const QStringList &branch : branches) {
          if (k < branch.size()) {
            more = true;
            if (!out.contains(branch[k]) && out.size() < kMaxCandidates) {
              out << branch[k];
            }
          }
        }
        if (!more) {
          break;
        }
      }
      return out;
    }
    case Op::Star: {
      const int sub = node.subs[0];
      if (IsLeaf(p, sub) && AcceptsAll(sub, QStringLiteral("sub/file"))) {
        if (p.nodes[sub].tail) {
          return {QStringLiteral("file"), QStringLiteral("sub/file"),
                  QString()};
        }
        return {QStringLiteral("sub/file"), QStringLiteral("file"), QString()};
      }
      if (IsLeaf(p, sub) && AcceptsAll(sub, QStringLiteral("name"))) {
        return {QStringLiteral("name"), QString()};
      }
      QStringList out{QString()};
      for (const QString &one : Of(sub)) {
        if (out.size() < kMaxCandidates && !out.contains(one)) {
          out << one;
        }
      }
      return out;
    }
    case Op::Quest: {
      QStringList out = Of(node.subs[0]);
      if (!out.contains(QString())) {
        out << QString();
      }
      return out;
    }
    default:
      return {};
    }
  }

private:
  const Parser &p;
  // By the class as written, so a class repeated many times is only
  // searched once.
  QHash<QString, QStringList> picks;

  bool AcceptsAll(int leaf, const QString &text) const {
    for (const QChar c : text) {
      if (!LeafAccepts(p, leaf, c.unicode())) {
        return false;
      }
    }
    return true;
  }

  QStringList Picks(const CharClass &cls) {
    const QString key = Text(p.re.substr(cls.from, cls.to - cls.from));
    const auto found = picks.constFind(key);
    if (found != picks.constEnd()) {
      return found.value();
    }
    QStringList out;
    char32_t pick = 0;
    if (Pick(cls, 0, &pick)) {
      QString one;
      Append(one, pick);
      out << one;
      // "." alone, or "/", can't be a file name.
      char32_t other = 0;
      if ((pick == U'.' || pick == U'/') && Pick(cls, pick, &other)) {
        QString two;
        Append(two, other);
        out << two;
      }
    }
    picks.insert(key, out);
    return out;
  }
};

// A path rclone could list: no empty, "." or ".." part, and no control
// character.
bool ValidPath(const QString &path) {
  if (path.isEmpty()) {
    return false;
  }
  for (const QString &part : path.split(QChar('/'))) {
    if (part.isEmpty() || part == QLatin1String(".") ||
        part == QLatin1String("..")) {
      return false;
    }
  }
  for (const QChar c : path) {
    if (c.unicode() < 0x20 || c.unicode() == 0x7F) {
      return false;
    }
  }
  return true;
}

Runes ToRunes(const QString &text) {
  const QList<uint> ucs4 = text.toUcs4();
  return Runes(ucs4.cbegin(), ucs4.cend());
}

} // namespace

Description Describe(const QString &pattern) {
  Description description;
  const QString trimmed = pattern.trimmed();
  if (trimmed.isEmpty()) {
    return description;
  }
  description.valid = true;

  const Runes glob = ToRunes(trimmed);
  // rclone's Filter.Add: a pattern ending in "/" is a directory rule, and
  // for an exclude rclone adds "**" so it takes in everything below.
  // Measured on v1.76.0: `--exclude /` excluded every file. A pattern with
  // "**" anywhere in it is checked against directories and files both.
  const bool dirPattern = glob.back() == U'/';
  Runes effective = glob;
  if (dirPattern) {
    effective += U"**";
  }
  const bool doubleStar = effective.find(U"**") != kNone;
  const bool dirRule = dirPattern || doubleStar;
  const bool fileRule = !dirPattern || doubleStar;

  const Translation translation =
      Translate(effective, dirPattern ? glob.size() : kNone);
  if (!translation.problem.isEmpty()) {
    description.wellFormed = false;
    description.problem = translation.problem;
    return description;
  }
  if (translation.opaque || effective.size() > kMaxGlob) {
    description.valid = false;
    return description;
  }
  Parser parser(translation.regexp);
  parser.watchClass = translation.negatedOpen;
  parser.tailDot = translation.tailDot;
  parser.Parse();
  if (!parser.problem.isEmpty()) {
    // Measured on v1.76.0: `--exclude "[!z-a].txt"` exits 1 with "bad glob
    // pattern" and nothing runs.
    description.wellFormed = false;
    description.problem = parser.problem;
    return description;
  }
  if (parser.uncertain || parser.root < 0 ||
      parser.nodes[parser.root].height > kMaxHeight) {
    description.valid = false;
    return description;
  }

  // A file is excluded when a file rule matches its path or a directory
  // rule matches one of the directories above it, with a "/" on the end.
  const Matcher matcher(parser);
  const auto excluded = [&matcher, fileRule, dirRule](const QString &path) {
    const Runes runes = ToRunes(path);
    if (fileRule && matcher.Matches(runes)) {
      return true;
    }
    if (dirRule) {
      for (size_t k = 0; k < runes.size(); k++) {
        if (runes[k] == U'/' && matcher.Matches(runes.substr(0, k + 1))) {
          return true;
        }
      }
    }
    return false;
  };

  Examples examples(parser);
  QString example;
  for (const QString &candidate : examples.Of(parser.root)) {
    if (ValidPath(candidate) && excluded(candidate)) {
      example = candidate;
      break;
    }
  }
  if (example.isEmpty()) {
    // Nothing the explainer can show is excluded: "x{/}" only matches a
    // path ending in "/", and "//" one with an empty part.
    description.valid = false;
    return description;
  }

  description.anchored = trimmed.startsWith(QChar('/'));
  description.directoryOnly = dirPattern;
  description.matchesExample = example;
  if (parser.watchedEnd == translation.negatedEnd && !parser.watchedRange) {
    description.negatedClass = translation.negatedClass;
  }

  // The same path one directory down, under a directory the pattern
  // doesn't name. Verified against rclone v1.75.0: `--exclude /file.tmp`
  // removed file.tmp and left sub/file.tmp alone, `--exclude *.tmp` and
  // `--exclude /**.tmp` removed both.
  const QString deeper =
      (example.startsWith(QStringLiteral("sub/")) ? QStringLiteral("other/")
                                                  : QStringLiteral("sub/")) +
      example;
  if (excluded(deeper)) {
    description.crossesDirectories = true;
    description.alsoMatchesExample = deeper;
    description.scope = description.anchored
                            ? QStringLiteral("everywhere below the transfer root")
                            : QStringLiteral("at any depth");
  } else {
    description.scope = QStringLiteral("only at the top level of the transfer");
    description.missesExample = deeper;
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
    } else if (!description.alsoMatchesExample.isEmpty()) {
      line += QString(" and %1").arg(description.alsoMatchesExample);
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
