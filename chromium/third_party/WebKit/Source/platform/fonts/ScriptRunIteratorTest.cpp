// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/fonts/ScriptRunIterator.h"

#include <string>
#include "platform/wtf/Assertions.h"
#include "platform/wtf/Threading.h"
#include "platform/wtf/text/WTFString.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

struct TestRun {
  std::string text;
  UScriptCode code;
};

struct ExpectedRun {
  unsigned limit;
  UScriptCode code;

  ExpectedRun(unsigned the_limit, UScriptCode the_code)
      : limit(the_limit), code(the_code) {}
};

class MockScriptData : public ScriptData {
 public:
  ~MockScriptData() override {}

  static const MockScriptData* Instance() {
    DEFINE_THREAD_SAFE_STATIC_LOCAL(const MockScriptData, mock_script_data,
                                    (new MockScriptData()));

    return &mock_script_data;
  }

  void GetScripts(UChar32 ch, Vector<UScriptCode>& dst) const override {
    DCHECK_GE(ch, kMockCharMin);
    DCHECK_LT(ch, kMockCharLimit);

    int code = ch - kMockCharMin;
    dst.Clear();
    switch (code & kCodeSpecialMask) {
      case kCodeSpecialCommon:
        dst.push_back(USCRIPT_COMMON);
        break;
      case kCodeSpecialInherited:
        dst.push_back(USCRIPT_INHERITED);
        break;
      default:
        break;
    }
    int list_bits = kTable[code & kCodeListIndexMask];
    if (dst.IsEmpty() && list_bits == 0) {
      dst.push_back(USCRIPT_UNKNOWN);
      return;
    }
    while (list_bits) {
      switch (list_bits & kListMask) {
        case 0:
          break;
        case kLatin:
          dst.push_back(USCRIPT_LATIN);
          break;
        case kHan:
          dst.push_back(USCRIPT_HAN);
          break;
        case kGreek:
          dst.push_back(USCRIPT_GREEK);
          break;
      }
      list_bits >>= kListShift;
    }
  }

  UChar32 GetPairedBracket(UChar32 ch) const override {
    switch (GetPairedBracketType(ch)) {
      case PairedBracketType::kBracketTypeClose:
        return ch - kBracketDelta;
      case PairedBracketType::kBracketTypeOpen:
        return ch + kBracketDelta;
      default:
        return ch;
    }
  }

  PairedBracketType GetPairedBracketType(UChar32 ch) const override {
    DCHECK_GE(ch, kMockCharMin);
    DCHECK_LT(ch, kMockCharLimit);
    int code = ch - kMockCharMin;
    if ((code & kCodeBracketBit) == 0) {
      return PairedBracketType::kBracketTypeNone;
    }
    if (code & kCodeBracketCloseBit) {
      return PairedBracketType::kBracketTypeClose;
    }
    return PairedBracketType::kBracketTypeOpen;
  }

  static int TableLookup(int value) {
    for (int i = 0; i < 16; ++i) {
      if (kTable[i] == value) {
        return i;
      }
    }
    DLOG(ERROR) << "Table does not contain value 0x" << std::hex << value;
    return 0;
  }

  static String ToTestString(const std::string& input) {
    String result(g_empty_string16_bit);
    bool in_set = false;
    int seen = 0;
    int code = 0;
    int list = 0;
    int current_shift = 0;
    for (char c : input) {
      if (in_set) {
        switch (c) {
          case '(':
            DCHECK_EQ(seen, 0);
            seen |= kSawBracket;
            code |= kCodeBracketBit;
            break;
          case '[':
            DCHECK_EQ(seen, 0);
            seen |= kSawBracket;
            code |= kCodeBracketBit | kCodeSquareBracketBit;
            break;
          case ')':
            DCHECK_EQ(seen, 0);
            seen |= kSawBracket;
            code |= kCodeBracketBit | kCodeBracketCloseBit;
            break;
          case ']':
            DCHECK_EQ(seen, 0);
            seen |= kSawBracket;
            code |=
                kCodeBracketBit | kCodeSquareBracketBit | kCodeBracketCloseBit;
            break;
          case 'i':
            DCHECK_EQ(seen, 0);  // brackets can't be inherited
            seen |= kSawSpecial;
            code |= kCodeSpecialInherited;
            break;
          case 'c':
            DCHECK_EQ((seen & ~kSawBracket), 0);
            seen |= kSawSpecial;
            code |= kCodeSpecialCommon;
            break;
          case 'l':
            DCHECK_EQ((seen & kSawLatin), 0);
            DCHECK_LT(current_shift, 3);
            seen |= kSawLatin;
            list |= kLatin << (2 * current_shift++);
            break;
          case 'h':
            DCHECK_EQ((seen & kSawHan), 0);
            DCHECK_LT(current_shift, 3);
            seen |= kSawHan;
            list |= kHan << (2 * current_shift++);
            break;
          case 'g':
            DCHECK_EQ((seen & kSawGreek), 0);
            DCHECK_LT(current_shift, 3);
            seen |= kSawGreek;
            list |= kGreek << (2 * current_shift++);
            break;
          case '>':
            DCHECK_NE(seen, 0);
            code |= TableLookup(list);
            result.Append(static_cast<UChar>(kMockCharMin + code));
            in_set = false;
            break;
          default:
            DLOG(ERROR) << "Illegal mock string set char: '" << c << "'";
            break;
        }
        continue;
      }
      // not in set
      switch (c) {
        case '<':
          seen = 0;
          code = 0;
          list = 0;
          current_shift = 0;
          in_set = true;
          break;
        case '(':
          code = kCodeBracketBit | kCodeSpecialCommon;
          break;
        case '[':
          code = kCodeBracketBit | kCodeSquareBracketBit | kCodeSpecialCommon;
          break;
        case ')':
          code = kCodeBracketBit | kCodeBracketCloseBit | kCodeSpecialCommon;
          break;
        case ']':
          code = kCodeBracketBit | kCodeSquareBracketBit |
                 kCodeBracketCloseBit | kCodeSpecialCommon;
          break;
        case 'i':
          code = kCodeSpecialInherited;
          break;
        case 'c':
          code = kCodeSpecialCommon;
          break;
        case 'l':
          code = kLatin;
          break;
        case 'h':
          code = kHan;
          break;
        case 'g':
          code = kGreek;
          break;
        case '?':
          code = 0;  // unknown
          break;
        default:
          DLOG(ERROR) << "Illegal mock string set char: '" << c << "'";
      }
      if (!in_set) {
        result.Append(static_cast<UChar>(kMockCharMin + code));
      }
    }
    return result;
  }

  // We determine properties based on the offset from kMockCharMin:
  // bits 0-3 represent the list of l, h, c scripts (index into table)
  // bit 4-5 means: 0 plain, 1 common, 2 inherited, 3 illegal
  // bit 6 clear means non-bracket, open means bracket
  // bit 7 clear means open bracket, set means close bracket
  // bit 8 clear means paren, set means bracket
  // if it's a bracket, the matching bracket is 64 code points away
  static const UChar32 kMockCharMin = 0xe000;
  static const UChar32 kMockCharLimit = kMockCharMin + 0x200;
  static const int kLatin = 1;
  static const int kHan = 2;
  static const int kGreek = 3;
  static const int kCodeListIndexMask = 0xf;
  static const int kCodeSpecialMask = 0x30;
  static const int kCodeSpecialCommon = 0x10;
  static const int kCodeSpecialInherited = 0x20;
  static const int kCodeBracketCloseBit = 0x40;
  static const int kCodeBracketBit = 0x80;
  static const int kCodeSquareBracketBit = 0x100;
  static const int kListShift = 2;
  static const int kListMask = 0x3;
  static const int kBracketDelta = kCodeBracketCloseBit;
  static const int kTable[16];

  static const int kSawBracket = 0x1;
  static const int kSawSpecial = 0x2;
  static const int kSawLatin = 0x4;
  static const int kSawHan = 0x8;
  static const int kSawGreek = 0x10;
};

static const int kLatin2 = MockScriptData::kLatin << 2;
static const int kHan2 = MockScriptData::kHan << 2;
static const int kGreek2 = MockScriptData::kGreek << 2;
static const int kLatin3 = MockScriptData::kLatin << 4;
static const int kHan3 = MockScriptData::kHan << 4;
static const int kGreek3 = MockScriptData::kGreek << 4;
const int MockScriptData::kTable[] = {
    0,
    kLatin,
    kHan,
    kGreek,
    kLatin2 + kHan,
    kLatin2 + kGreek,
    kHan2 + kLatin,
    kHan2 + kGreek,
    kGreek2 + kLatin,
    kGreek2 + kHan,
    kLatin3 + kHan2 + kGreek,
    kLatin3 + kGreek2 + kHan,
    kHan3 + kLatin2 + kGreek,
    kHan3 + kGreek2 + kLatin,
    kGreek3 + kLatin2 + kHan,
    kGreek3 + kHan2 + kLatin,
};

class ScriptRunIteratorTest : public testing::Test {
 protected:
  void CheckRuns(const Vector<TestRun>& runs) {
    String text(g_empty_string16_bit);
    Vector<ExpectedRun> expect;
    for (auto& run : runs) {
      text.Append(String::FromUTF8(run.text.c_str()));
      expect.push_back(ExpectedRun(text.length(), run.code));
    }
    ScriptRunIterator script_run_iterator(text.Characters16(), text.length());
    VerifyRuns(&script_run_iterator, expect);
  }

  // FIXME crbug.com/527329 - CheckMockRuns should be replaced by finding
  // suitable equivalent real codepoint sequences instead.
  void CheckMockRuns(const Vector<TestRun>& runs) {
    String text(g_empty_string16_bit);
    Vector<ExpectedRun> expect;
    for (const TestRun& run : runs) {
      text.Append(MockScriptData::ToTestString(run.text));
      expect.push_back(ExpectedRun(text.length(), run.code));
    }

    ScriptRunIterator script_run_iterator(text.Characters16(), text.length(),
                                          MockScriptData::Instance());
    VerifyRuns(&script_run_iterator, expect);
  }

  void VerifyRuns(ScriptRunIterator* script_run_iterator,
                  const Vector<ExpectedRun>& expect) {
    unsigned limit;
    UScriptCode code;
    unsigned long run_count = 0;
    while (script_run_iterator->Consume(limit, code)) {
      ASSERT_LT(run_count, expect.size());
      ASSERT_EQ(expect[run_count].limit, limit);
      ASSERT_EQ(expect[run_count].code, code);
      ++run_count;
    }
    ASSERT_EQ(expect.size(), run_count);
  }
};

TEST_F(ScriptRunIteratorTest, Empty) {
  String empty(g_empty_string16_bit);
  ScriptRunIterator script_run_iterator(empty.Characters16(), empty.length());
  unsigned limit = 0;
  UScriptCode code = USCRIPT_INVALID_CODE;
  DCHECK(!script_run_iterator.Consume(limit, code));
  ASSERT_EQ(limit, 0u);
  ASSERT_EQ(code, USCRIPT_INVALID_CODE);
}

// Some of our compilers cannot initialize a vector from an array yet.
#define DECLARE_RUNSVECTOR(...)                    \
  static const TestRun kRunsArray[] = __VA_ARGS__; \
  Vector<TestRun> runs;                            \
  runs.Append(kRunsArray, sizeof(kRunsArray) / sizeof(*kRunsArray));

#define CHECK_RUNS(...)            \
  DECLARE_RUNSVECTOR(__VA_ARGS__); \
  CheckRuns(runs);

#define CHECK_MOCK_RUNS(...)       \
  DECLARE_RUNSVECTOR(__VA_ARGS__); \
  CheckMockRuns(runs);

TEST_F(ScriptRunIteratorTest, Whitespace) {
  CHECK_RUNS({{" \t ", USCRIPT_COMMON}});
}

TEST_F(ScriptRunIteratorTest, Common) {
  CHECK_RUNS({{" ... !?", USCRIPT_COMMON}});
}

TEST_F(ScriptRunIteratorTest, CombiningCircle) {
  CHECK_RUNS({{"◌́◌̀◌̈◌̂◌̄◌̊", USCRIPT_COMMON}});
}

TEST_F(ScriptRunIteratorTest, Latin) {
  CHECK_RUNS({{"latin", USCRIPT_LATIN}});
}

TEST_F(ScriptRunIteratorTest, Chinese) {
  CHECK_RUNS({{"萬國碼", USCRIPT_HAN}});
}

// Close bracket without matching open is ignored
TEST_F(ScriptRunIteratorTest, UnbalancedParens1) {
  CHECK_RUNS({{"(萬", USCRIPT_HAN}, {"a]", USCRIPT_LATIN}, {")", USCRIPT_HAN}});
}

// Open bracket without matching close is popped when inside
// matching close brackets, so doesn't match later close.
TEST_F(ScriptRunIteratorTest, UnbalancedParens2) {
  CHECK_RUNS(
      {{"(萬", USCRIPT_HAN}, {"a[", USCRIPT_LATIN}, {")]", USCRIPT_HAN}});
}

// space goes with leading script
TEST_F(ScriptRunIteratorTest, LatinHan) {
  CHECK_RUNS({{"Unicode ", USCRIPT_LATIN}, {"萬國碼", USCRIPT_HAN}});
}

// space goes with leading script
TEST_F(ScriptRunIteratorTest, HanLatin) {
  CHECK_RUNS({{"萬國碼 ", USCRIPT_HAN}, {"Unicode", USCRIPT_LATIN}});
}

TEST_F(ScriptRunIteratorTest, ParenEmptyParen) {
  CHECK_RUNS({{"()", USCRIPT_COMMON}});
}

TEST_F(ScriptRunIteratorTest, ParenChineseParen) {
  CHECK_RUNS({{"(萬國碼)", USCRIPT_HAN}});
}

TEST_F(ScriptRunIteratorTest, ParenLatinParen) {
  CHECK_RUNS({{"(Unicode)", USCRIPT_LATIN}});
}

// open paren gets leading script
TEST_F(ScriptRunIteratorTest, LatinParenChineseParen) {
  CHECK_RUNS({{"Unicode (", USCRIPT_LATIN},
              {"萬國碼", USCRIPT_HAN},
              {")", USCRIPT_LATIN}});
}

// open paren gets first trailing script if no leading script
TEST_F(ScriptRunIteratorTest, ParenChineseParenLatin) {
  CHECK_RUNS({{"(萬國碼) ", USCRIPT_HAN}, {"Unicode", USCRIPT_LATIN}});
}

// leading common and open paren get first trailing script.
// TODO(dougfelt): we don't do quote matching, but probably should figure out
// something better then doing nothing.
TEST_F(ScriptRunIteratorTest, QuoteParenChineseParenLatinQuote) {
  CHECK_RUNS({{"\"(萬國碼) ", USCRIPT_HAN}, {"Unicode\"", USCRIPT_LATIN}});
}

// Emojies are resolved to the leading script.
TEST_F(ScriptRunIteratorTest, EmojiCommon) {
  CHECK_RUNS({{"百家姓🌱🌲🌳🌴", USCRIPT_HAN}});
}

// Unmatched close brace gets leading context
TEST_F(ScriptRunIteratorTest, UnmatchedClose) {
  CHECK_RUNS({{"Unicode (", USCRIPT_LATIN},
              {"萬國碼] ", USCRIPT_HAN},
              {") Unicode\"", USCRIPT_LATIN}});
}

// Match up to 32 bracket pairs
TEST_F(ScriptRunIteratorTest, Match32Brackets) {
  CHECK_RUNS({{"[萬國碼 ", USCRIPT_HAN},
              {"Unicode (((((((((((((((((((((((((((((((!"
               ")))))))))))))))))))))))))))))))",
               USCRIPT_LATIN},
              {"]", USCRIPT_HAN}});
}

// Matches 32 most recent bracket pairs. More than that, and we revert to
// surrounding script.
TEST_F(ScriptRunIteratorTest, Match32MostRecentBrackets) {
  CHECK_RUNS({{"((([萬國碼 ", USCRIPT_HAN},
              {"Unicode (((((((((((((((((((((((((((((((", USCRIPT_LATIN},
              {"萬國碼!", USCRIPT_HAN},
              {")))))))))))))))))))))))))))))))", USCRIPT_LATIN},
              {"]", USCRIPT_HAN},
              {"But )))", USCRIPT_LATIN}});
}

// A char with multiple scripts that match both leading and trailing context
// gets the leading context.
TEST_F(ScriptRunIteratorTest, ExtensionsPreferLeadingContext) {
  CHECK_MOCK_RUNS({{"h<lh>", USCRIPT_HAN}, {"l", USCRIPT_LATIN}});
}

// A char with multiple scripts that only match trailing context gets the
// trailing context.
TEST_F(ScriptRunIteratorTest, ExtensionsMatchTrailingContext) {
  CHECK_MOCK_RUNS({{"h", USCRIPT_HAN}, {"<gl>l", USCRIPT_LATIN}});
}

// Retain first established priority script.  <lhg><gh> produce the script <gh>
// with g as priority, because of the two priority scripts l and g, only g
// remains.  Then <gh><hgl> retains g as priority, because of the two priority
// scripts g and h that remain, g was encountered first.
TEST_F(ScriptRunIteratorTest, ExtensionsRetainFirstPriorityScript) {
  CHECK_MOCK_RUNS({{"<lhg><gh><hgl>", USCRIPT_GREEK}});
}

// Parens can have scripts that break script runs.
TEST_F(ScriptRunIteratorTest, ExtensionsParens) {
  CHECK_MOCK_RUNS({{"<gl><(lg>", USCRIPT_GREEK},
                   {"h<[hl>", USCRIPT_HAN},
                   {"l", USCRIPT_LATIN},
                   {"<]hl>", USCRIPT_HAN},
                   {"<)lg>", USCRIPT_GREEK}});
}

// The close paren might be encountered before we've established the open
// paren's script, but when this is the case the current set is still valid, so
// this doesn't affect it nor break the run.
TEST_F(ScriptRunIteratorTest, ExtensionsParens2) {
  CHECK_MOCK_RUNS({{"<(lhg><gh><)lhg>", USCRIPT_GREEK}});
}

// A common script with a single extension should be treated as common, but
// with the extended script as a default.  If we encounter anything other than
// common, that takes priority.  If we encounter other common scripts with a
// single extension, the current priority remains.
TEST_F(ScriptRunIteratorTest, CommonWithPriority) {
  CHECK_MOCK_RUNS({{"<ch>", USCRIPT_HAN}});
}

TEST_F(ScriptRunIteratorTest, CommonWithPriority2) {
  CHECK_MOCK_RUNS({{"<ch><lh>", USCRIPT_LATIN}});
}

TEST_F(ScriptRunIteratorTest, CommonWithPriority3) {
  CHECK_MOCK_RUNS({{"<ch><cl><cg>", USCRIPT_HAN}});
}

// UDatta (\xE0\xA5\x91) is inherited with LATIN, DEVANAGARI, BENGALI and
// other Indic scripts. Since it has LATIN, and the
// dotted circle U+25CC (\xE2\x97\x8C) is COMMON and has adopted the
// preceding LATIN, it gets the LATIN. This is standard.
TEST_F(ScriptRunIteratorTest, LatinDottedCircleUdatta) {
  CHECK_RUNS({{"Latin \xE2\x97\x8C\xE0\xA5\x91", USCRIPT_LATIN}});
}

// In this situation, UDatta U+0951 (\xE0\xA5\x91) doesn't share a script
// with the value inherited by the dotted circle U+25CC (\xE2\x97\x8C).
// It captures the preceding dotted circle and breaks it from the run it would
// normally have been in. U+0951 is used in multiple scripts (DEVA, BENG, LATN,
// etc) and has multiple values for Script_Extension property. At the moment,
// getScripts() treats the script with the lowest script code as 'true' primary,
// and BENG comes before DEVA in the script enum so that we get BENGALI.
// Taking into account a Unicode block and returning DEVANAGARI would be
// slightly better.
TEST_F(ScriptRunIteratorTest, HanDottedCircleUdatta) {
  CHECK_RUNS({{"萬國碼 ", USCRIPT_HAN},
              {"\xE2\x97\x8C\xE0\xA5\x91", USCRIPT_BENGALI}});
}

// Tatweel is \xD9\x80 Lm, Fathatan is \xD9\x8B Mn. The script of tatweel is
// common, that of Fathatan is inherited.  The script extensions for Fathatan
// are Arabic and Syriac. The Syriac script is 34 in ICU, Arabic is 2. So the
// preferred script for Fathatan is Arabic, according to Behdad's
// heuristic. This is exactly analogous to the Udatta tests above, except
// Tatweel is Lm. But we don't take properties into account, only scripts.
TEST_F(ScriptRunIteratorTest, LatinTatweelFathatan) {
  CHECK_RUNS({{"Latin ", USCRIPT_LATIN}, {"\xD9\x80\xD9\x8B", USCRIPT_ARABIC}});
}

// Another case where if the mark accepts a script that was inherited by the
// preceding common-script character, they both continue in that script.
// SYRIAC LETTER NUN \xDC\xA2
// ARABIC TATWEEL \xD9\x80
// ARABIC FATHATAN \xD9\x82
TEST_F(ScriptRunIteratorTest, SyriacTatweelFathatan) {
  CHECK_RUNS({{"\xDC\xA2\xD9\x80\xD9\x8B", USCRIPT_SYRIAC}});
}

// The Udatta (\xE0\xA5\x91) is inherited, so will share runs with anything that
// is not common.
TEST_F(ScriptRunIteratorTest, HanUdatta) {
  CHECK_RUNS({{"萬國碼\xE0\xA5\x91", USCRIPT_HAN}});
}

// The Udatta U+0951 (\xE0\xA5\x91) is inherited, and will capture the space
// and turn it into Bengali because SCRIPT_BENAGLI is 4 and SCRIPT_DEVANAGARI
// is 10. See TODO comment for |getScripts| and HanDottedCircleUdatta.
TEST_F(ScriptRunIteratorTest, HanSpaceUdatta) {
  CHECK_RUNS({{"萬國碼", USCRIPT_HAN}, {" \xE0\xA5\x91", USCRIPT_BENGALI}});
}

// Corresponds to one test in RunSegmenter, where orientation of the
// space character is sidesways in vertical.
TEST_F(ScriptRunIteratorTest, Hangul) {
  CHECK_RUNS({{"키스의 고유조건은", USCRIPT_HANGUL}});
}

// Corresponds to one test in RunSegmenter, which tests that the punctuation
// characters mixed in are actually sideways in vertical. The ScriptIterator
// should report one run, but the RunSegmenter should report three, with the
// middle one rotated sideways.
TEST_F(ScriptRunIteratorTest, HiraganaMixedPunctuation) {
  CHECK_RUNS({{"いろはに.…¡ほへと", USCRIPT_HIRAGANA}});
}

// Make sure Mock code works too.
TEST_F(ScriptRunIteratorTest, MockHanInheritedGL) {
  CHECK_MOCK_RUNS({{"h<igl>", USCRIPT_HAN}});
}

TEST_F(ScriptRunIteratorTest, MockHanCommonInheritedGL) {
  CHECK_MOCK_RUNS({{"h", USCRIPT_HAN}, {"c<igl>", USCRIPT_GREEK}});
}

// Leading inherited just act like common, except there's no preferred script.
TEST_F(ScriptRunIteratorTest, MockLeadingInherited) {
  CHECK_MOCK_RUNS({{"<igl>", USCRIPT_COMMON}});
}

// Leading inherited just act like common, except there's no preferred script.
TEST_F(ScriptRunIteratorTest, MockLeadingInherited2) {
  CHECK_MOCK_RUNS({{"<igl><ih>", USCRIPT_COMMON}});
}

TEST_F(ScriptRunIteratorTest, LeadingInheritedHan) {
  // DEVANAGARI STRESS SIGN UDATTA \xE0\xA5\x91
  CHECK_RUNS({{"\xE0\xA5\x91萬國碼", USCRIPT_HAN}});
}

TEST_F(ScriptRunIteratorTest, LeadingInheritedHan2) {
  // DEVANAGARI STRESS SIGN UDATTA \xE0\xA5\x91
  // ARABIC FATHATAN \xD9\x8B
  CHECK_RUNS({{"\xE0\xA5\x91\xD9\x8B萬國碼", USCRIPT_HAN}});
}

TEST_F(ScriptRunIteratorTest, OddLatinString) {
  CHECK_RUNS({{"ç̈", USCRIPT_LATIN}});
}

TEST_F(ScriptRunIteratorTest, CommonMalayalam) {
  CHECK_RUNS({{"100-ാം", USCRIPT_MALAYALAM}});
}

class ScriptRunIteratorICUDataTest : public testing::Test {
 public:
  ScriptRunIteratorICUDataTest()
      : max_extensions_(0), max_extensions_codepoint_(0xffff) {
    int max_extensions = 0;
    UChar32 max_extensionscp = 0;
    for (UChar32 cp = 0; cp < 0x11000; ++cp) {
      UErrorCode status = U_ZERO_ERROR;
      int count = uscript_getScriptExtensions(cp, 0, 0, &status);
      if (count > max_extensions) {
        max_extensions = count;
        max_extensionscp = cp;
      }
    }
    max_extensions_ = max_extensions;
    max_extensions_codepoint_ = max_extensionscp;
  }

 protected:
  UChar32 GetACharWithMaxExtensions(int* num_extensions) {
    if (num_extensions) {
      *num_extensions = max_extensions_;
    }
    return max_extensions_codepoint_;
  }

 private:
  int max_extensions_;
  UChar32 max_extensions_codepoint_;
};

// Validate that ICU never returns more than our maximum expected number of
// script extensions.
TEST_F(ScriptRunIteratorICUDataTest, ValidateICUMaxScriptExtensions) {
  int max_extensions;
  UChar32 cp = GetACharWithMaxExtensions(&max_extensions);
  ASSERT_LE(max_extensions, ScriptData::kMaxScriptCount)
      << "char " << std::hex << cp << std::dec;
}

// Check that ICUScriptData returns all of a character's scripts.
// This only checks one likely character, but doesn't check all cases.
TEST_F(ScriptRunIteratorICUDataTest, ICUDataGetScriptsReturnsAllExtensions) {
  int max_extensions;
  UChar32 cp = GetACharWithMaxExtensions(&max_extensions);
  Vector<UScriptCode> extensions;
  ICUScriptData::Instance()->GetScripts(cp, extensions);

  // It's possible that GetScripts adds the primary script to the list of
  // extensions, resulting in one more script than the raw extension count.
  ASSERT_GE(static_cast<int>(extensions.size()), max_extensions)
      << "char " << std::hex << cp << std::dec;
}

TEST_F(ScriptRunIteratorICUDataTest, CommonHaveNoMoreThanOneExtension) {
  Vector<UScriptCode> extensions;
  for (UChar32 cp = 0; cp < 0x110000; ++cp) {
    ICUScriptData::Instance()->GetScripts(cp, extensions);
    UScriptCode primary = extensions.at(0);
    if (primary == USCRIPT_COMMON) {
      ASSERT_LE(extensions.size(), 2ul) << "cp: " << std::hex << cp << std::dec;
    }
  }
}

// ZWJ is \u200D Cf (Format, other) and its script is inherited.  I'm going to
// ignore this for now, as I think it shouldn't matter which run it ends up
// in. HarfBuzz needs to be able to use it as context and shape each
// neighboring character appropriately no matter what run it got assigned to.

}  // namespace blink
