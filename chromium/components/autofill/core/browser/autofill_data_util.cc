// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/autofill_data_util.h"

#include <algorithm>
#include <vector>

#include "base/i18n/char_iterator.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/grit/components_scaled_resources.h"
#include "third_party/icu/source/common/unicode/uscript.h"

namespace autofill {
namespace data_util {

namespace {
// Mappings from Chrome card types to Payment Request API basic card payment
// spec types and icons. Note that "generic" is not in the spec.
// https://w3c.github.io/webpayments-methods-card/#method-id
const PaymentRequestData kPaymentRequestData[]{
    {"americanExpressCC", "amex", IDR_AUTOFILL_PR_AMEX},
    {"dinersCC", "diners", IDR_AUTOFILL_PR_DINERS},
    {"discoverCC", "discover", IDR_AUTOFILL_PR_DISCOVER},
    {"jcbCC", "jcb", IDR_AUTOFILL_PR_JCB},
    {"masterCardCC", "mastercard", IDR_AUTOFILL_PR_MASTERCARD},
    {"mirCC", "mir", IDR_AUTOFILL_PR_MIR},
    {"unionPayCC", "unionpay", IDR_AUTOFILL_PR_UNIONPAY},
    {"visaCC", "visa", IDR_AUTOFILL_PR_VISA},
};
const PaymentRequestData kGenericPaymentRequestData = {"genericCC", "generic",
                                                       IDR_AUTOFILL_PR_GENERIC};

const char* const name_prefixes[] = {
    "1lt",     "1st", "2lt", "2nd",    "3rd",  "admiral", "capt",
    "captain", "col", "cpt", "dr",     "gen",  "general", "lcdr",
    "lt",      "ltc", "ltg", "ltjg",   "maj",  "major",   "mg",
    "mr",      "mrs", "ms",  "pastor", "prof", "rep",     "reverend",
    "rev",     "sen", "st"};

const char* const name_suffixes[] = {"b.a", "ba", "d.d.s", "dds",  "i",   "ii",
                                     "iii", "iv", "ix",    "jr",   "m.a", "m.d",
                                     "ma",  "md", "ms",    "ph.d", "phd", "sr",
                                     "v",   "vi", "vii",   "viii", "x"};

const char* const family_name_prefixes[] = {"d'",  "de",  "del", "der", "di",
                                            "la",  "le",  "mc",  "san", "st",
                                            "ter", "van", "von"};

// The common and non-ambiguous CJK surnames (last names) that have more than
// one character.
const char* common_cjk_multi_char_surnames[] = {
  // Korean, taken from the list of surnames:
  // https://ko.wikipedia.org/wiki/%ED%95%9C%EA%B5%AD%EC%9D%98_%EC%84%B1%EC%94%A8_%EB%AA%A9%EB%A1%9D
  "남궁", "사공", "서문", "선우", "제갈", "황보", "독고", "망절",

  // Chinese, taken from the top 10 Chinese 2-character surnames:
  // https://zh.wikipedia.org/wiki/%E8%A4%87%E5%A7%93#.E5.B8.B8.E8.A6.8B.E7.9A.84.E8.A4.87.E5.A7.93
  // Simplified Chinese (mostly mainland China)
  "欧阳", "令狐", "皇甫", "上官", "司徒", "诸葛", "司马", "宇文", "呼延", "端木",
  // Traditional Chinese (mostly Taiwan)
  "張簡", "歐陽", "諸葛", "申屠", "尉遲", "司馬", "軒轅", "夏侯"
};

// All Korean surnames that have more than one character, even the
// rare/ambiguous ones.
const char* korean_multi_char_surnames[] = {
  "강전", "남궁", "독고", "동방", "망절", "사공", "서문", "선우",
  "소봉", "어금", "장곡", "제갈", "황목", "황보"
};

// Returns true if |set| contains |element|, modulo a final period.
bool ContainsString(const char* const set[],
                    size_t set_size,
                    base::StringPiece16 element) {
  if (!base::IsStringASCII(element))
    return false;

  base::StringPiece16 trimmed_element =
      base::TrimString(element, base::ASCIIToUTF16("."), base::TRIM_ALL);

  for (size_t i = 0; i < set_size; ++i) {
    if (base::LowerCaseEqualsASCII(trimmed_element, set[i]))
      return true;
  }

  return false;
}

// Removes common name prefixes from |name_tokens|.
void StripPrefixes(std::vector<base::StringPiece16>* name_tokens) {
  std::vector<base::StringPiece16>::iterator iter = name_tokens->begin();
  while (iter != name_tokens->end()) {
    if (!ContainsString(name_prefixes, arraysize(name_prefixes), *iter))
      break;
    ++iter;
  }

  std::vector<base::StringPiece16> copy_vector;
  copy_vector.assign(iter, name_tokens->end());
  *name_tokens = copy_vector;
}

// Removes common name suffixes from |name_tokens|.
void StripSuffixes(std::vector<base::StringPiece16>* name_tokens) {
  while (!name_tokens->empty()) {
    if (!ContainsString(name_suffixes, arraysize(name_suffixes),
                        name_tokens->back())) {
      break;
    }
    name_tokens->pop_back();
  }
}

// Find whether |name| starts with any of the strings from the array
// |prefixes|. The returned value is the length of the prefix found, or 0 if
// none is found.
size_t StartsWithAny(base::StringPiece16 name, const char** prefixes,
                     size_t prefix_count) {
   base::string16 buffer;
   for (size_t i = 0; i < prefix_count; i++) {
     buffer.clear();
     base::UTF8ToUTF16(prefixes[i], strlen(prefixes[i]), &buffer);
     if (base::StartsWith(name, buffer, base::CompareCase::SENSITIVE)) {
       return buffer.size();
     }
   }
   return 0;
}

// Returns true if |c| is a CJK (Chinese, Japanese, Korean) character, for any
// of the CJK alphabets.
bool IsCJKCharacter(UChar32 c) {
  UErrorCode error = U_ZERO_ERROR;
  switch (uscript_getScript(c, &error)) {
    case USCRIPT_HAN:  // CJK logographs, used by all 3 (but rarely for Korean)
    case USCRIPT_HANGUL:    // Korean alphabet
    case USCRIPT_KATAKANA:  // A Japanese syllabary
    case USCRIPT_HIRAGANA:  // A Japanese syllabary
    case USCRIPT_BOPOMOFO:  // Chinese semisyllabary, rarely used
      return true;
    default:
      return false;
  }
}

// Returns true if |c| is a Korean Hangul character.
bool IsHangulCharacter(UChar32 c) {
  UErrorCode error = U_ZERO_ERROR;
  return uscript_getScript(c, &error) == USCRIPT_HANGUL;
}

// Returns true if |name| looks like a Korean name, made up entirely of Hangul
// characters or spaces. |name| should already be confirmed to be a CJK name, as
// per |IsCJKName()|.
bool IsHangulName(base::StringPiece16 name) {
  for (base::i18n::UTF16CharIterator iter(name.data(), name.length());
       !iter.end(); iter.Advance()) {
    UChar32 c = iter.get();
    if (!IsHangulCharacter(c) && !base::IsUnicodeWhitespace(c)) {
      return false;
    }
  }
  return true;
}

// Tries to split a Chinese, Japanese, or Korean name into its given name &
// surname parts, and puts the result in |parts|. If splitting did not work for
// whatever reason, returns false.
bool SplitCJKName(const std::vector<base::StringPiece16>& name_tokens,
                  NameParts* parts) {
  // The convention for CJK languages is to put the surname (last name) first,
  // and the given name (first name) second. In a continuous text, there is
  // normally no space between the two parts of the name. When entering their
  // name into a field, though, some people add a space to disambiguate. CJK
  // names (almost) never have a middle name.
  if (name_tokens.size() == 1) {
    // There is no space between the surname and given name. Try to infer where
    // to separate between the two. Most Chinese and Korean surnames have only
    // one character, but there are a few that have 2. If the name does not
    // start with a surname from a known list, default to 1 character.
    //
    // TODO(crbug.com/89111): Japanese names with no space will be mis-split,
    // since we don't have a list of Japanese last names. In the Han alphabet,
    // it might also be difficult for us to differentiate between Chinese &
    // Japanese names.
    const base::StringPiece16& name = name_tokens.front();
    const bool is_korean = IsHangulName(name);
    size_t surname_length = 0;
    if (is_korean && name.size() > 3) {
      // 4-character Korean names are more likely to be 2/2 than 1/3, so use
      // the full list of Korean 2-char surnames. (instead of only the common
      // ones)
      surname_length = std::max<size_t>(
          1, StartsWithAny(name, korean_multi_char_surnames,
                           arraysize(korean_multi_char_surnames)));
    } else {
      // Default to 1 character if the surname is not in
      // |common_cjk_multi_char_surnames|.
      surname_length = std::max<size_t>(
          1, StartsWithAny(name, common_cjk_multi_char_surnames,
                           arraysize(common_cjk_multi_char_surnames)));
    }
    parts->family = name.substr(0, surname_length).as_string();
    parts->given = name.substr(surname_length).as_string();
    return true;
  }
  if (name_tokens.size() == 2) {
    // The user entered a space between the two name parts. This makes our job
    // easier. Family name first, given name second.
    parts->family = name_tokens[0].as_string();
    parts->given = name_tokens[1].as_string();
    return true;
  }
  // We don't know what to do if there are more than 2 tokens.
  return false;
}

}  // namespace

bool IsCJKName(base::StringPiece16 name) {
  // The name is considered to be a CJK name if it is only CJK characters,
  // spaces, and "middle dot" separators, with at least one CJK character, and
  // no more than 2 words.
  //
  // Chinese and Japanese names are usually spelled out using the Han characters
  // (logographs), which constitute the "CJK Unified Ideographs" block in
  // Unicode, also referred to as Unihan. Korean names are usually spelled out
  // in the Korean alphabet (Hangul), although they do have a Han equivalent as
  // well.
  //
  // The middle dot is used as a separator for foreign names in Japanese.
  static const base::char16 kKatakanaMiddleDot = u'\u30FB';
  // A (common?) typo for 'KATAKANA MIDDLE DOT' (U+30FB).
  static const base::char16 kMiddleDot = u'\u00B7';
  bool previous_was_cjk = false;
  size_t word_count = 0;
  for (base::i18n::UTF16CharIterator iter(name.data(), name.length());
       !iter.end(); iter.Advance()) {
    UChar32 c = iter.get();
    const bool is_cjk = IsCJKCharacter(c);
    if (!is_cjk && !base::IsUnicodeWhitespace(c) && c != kKatakanaMiddleDot &&
        c != kMiddleDot) {
      return false;
    }
    if (is_cjk && !previous_was_cjk) {
      word_count++;
    }
    previous_was_cjk = is_cjk;
  }
  return word_count > 0 && word_count < 3;
}

NameParts SplitName(base::StringPiece16 name) {
  static const base::char16 kWordSeparators[] = {
    u' ', // ASCII space.
    u',', // ASCII comma.
    u'\u3000', // 'IDEOGRAPHIC SPACE' (U+3000).
    u'\u30FB', // 'KATAKANA MIDDLE DOT' (U+30FB).
    u'\u00B7', // 'MIDDLE DOT' (U+00B7).
    u'\0' // End of string.
  };
  std::vector<base::StringPiece16> name_tokens = base::SplitStringPiece(
      name, kWordSeparators, base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  StripPrefixes(&name_tokens);

  NameParts parts;

  // TODO(crbug.com/89111): Hungarian, Tamil, Telugu, and Vietnamese also have
  // the given name before the surname, and should be treated as special cases
  // too.

  // Treat CJK names differently.
  if (IsCJKName(name) && SplitCJKName(name_tokens, &parts)) {
    return parts;
  }

  // Don't assume "Ma" is a suffix in John Ma.
  if (name_tokens.size() > 2)
    StripSuffixes(&name_tokens);

  if (name_tokens.empty()) {
    // Bad things have happened; just assume the whole thing is a given name.
    parts.given = name.as_string();
    return parts;
  }

  // Only one token, assume given name.
  if (name_tokens.size() == 1) {
    parts.given = name_tokens[0].as_string();
    return parts;
  }

  // 2 or more tokens. Grab the family, which is the last word plus any
  // recognizable family prefixes.
  std::vector<base::StringPiece16> reverse_family_tokens;
  reverse_family_tokens.push_back(name_tokens.back());
  name_tokens.pop_back();
  while (name_tokens.size() >= 1 &&
         ContainsString(family_name_prefixes, arraysize(family_name_prefixes),
                        name_tokens.back())) {
    reverse_family_tokens.push_back(name_tokens.back());
    name_tokens.pop_back();
  }

  std::vector<base::StringPiece16> family_tokens(reverse_family_tokens.rbegin(),
                                                 reverse_family_tokens.rend());
  parts.family = base::JoinString(family_tokens, base::ASCIIToUTF16(" "));

  // Take the last remaining token as the middle name (if there are at least 2
  // tokens).
  if (name_tokens.size() >= 2) {
    parts.middle = name_tokens.back().as_string();
    name_tokens.pop_back();
  }

  // Remainder is given name.
  parts.given = base::JoinString(name_tokens, base::ASCIIToUTF16(" "));

  return parts;
}

base::string16 JoinNameParts(base::StringPiece16 given,
                             base::StringPiece16 middle,
                             base::StringPiece16 family) {
  // First Middle Last
  std::vector<base::StringPiece16> full_name;
  if (!given.empty())
    full_name.push_back(given);

  if (!middle.empty())
    full_name.push_back(middle);

  if (!family.empty())
    full_name.push_back(family);

  const char* separator = " ";
  if (IsCJKName(given) && IsCJKName(family) && middle.empty()) {
    // LastFirst
    std::reverse(full_name.begin(), full_name.end());
    separator = "";
  }

  return base::JoinString(full_name, base::ASCIIToUTF16(separator));
}

bool ProfileMatchesFullName(base::StringPiece16 full_name,
                            const autofill::AutofillProfile& profile) {
  const base::string16 kSpace = base::ASCIIToUTF16(" ");
  const base::string16 kPeriodSpace = base::ASCIIToUTF16(". ");

  // First Last
  base::string16 candidate = profile.GetRawInfo(autofill::NAME_FIRST) + kSpace +
                             profile.GetRawInfo(autofill::NAME_LAST);
  if (!full_name.compare(candidate)) {
    return true;
  }

  // First Middle Last
  candidate = profile.GetRawInfo(autofill::NAME_FIRST) + kSpace +
              profile.GetRawInfo(autofill::NAME_MIDDLE) + kSpace +
              profile.GetRawInfo(autofill::NAME_LAST);
  if (!full_name.compare(candidate)) {
    return true;
  }

  // First M Last
  candidate = profile.GetRawInfo(autofill::NAME_FIRST) + kSpace +
              profile.GetRawInfo(autofill::NAME_MIDDLE_INITIAL) + kSpace +
              profile.GetRawInfo(autofill::NAME_LAST);
  if (!full_name.compare(candidate)) {
    return true;
  }

  // First M. Last
  candidate = profile.GetRawInfo(autofill::NAME_FIRST) + kSpace +
              profile.GetRawInfo(autofill::NAME_MIDDLE_INITIAL) + kPeriodSpace +
              profile.GetRawInfo(autofill::NAME_LAST);
  if (!full_name.compare(candidate)) {
    return true;
  }

  // Last First
  candidate = profile.GetRawInfo(autofill::NAME_LAST) + kSpace +
              profile.GetRawInfo(autofill::NAME_FIRST);
  if (!full_name.compare(candidate)) {
    return true;
  }

  // LastFirst
  candidate = profile.GetRawInfo(autofill::NAME_LAST) +
              profile.GetRawInfo(autofill::NAME_FIRST);
  if (!full_name.compare(candidate)) {
    return true;
  }

  return false;
}

const PaymentRequestData& GetPaymentRequestData(const std::string& type) {
  for (const PaymentRequestData& data : kPaymentRequestData) {
    if (type == data.card_type)
      return data;
  }
  return kGenericPaymentRequestData;
}

const char* GetCardTypeForBasicCardPaymentType(
    const std::string& basic_card_payment_type) {
  for (const PaymentRequestData& data : kPaymentRequestData) {
    if (basic_card_payment_type == data.basic_card_payment_type) {
      return data.card_type;
    }
  }
  return kGenericPaymentRequestData.card_type;
}

}  // namespace data_util
}  // namespace autofill
