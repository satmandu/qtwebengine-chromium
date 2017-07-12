// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/LayoutLocale.h"

#include "platform/Language.h"
#include "platform/fonts/AcceptLanguagesResolver.h"
#include "platform/text/ICUError.h"
#include "platform/text/LocaleToScriptMapping.h"
#include "platform/wtf/HashMap.h"
#include "platform/wtf/text/AtomicStringHash.h"
#include "platform/wtf/text/StringHash.h"

#include <hb.h>
#include <unicode/locid.h>

namespace blink {

const LayoutLocale* LayoutLocale::default_ = nullptr;
const LayoutLocale* LayoutLocale::system_ = nullptr;
const LayoutLocale* LayoutLocale::default_for_han_ = nullptr;
bool LayoutLocale::default_for_han_computed_ = false;

static hb_language_t ToHarfbuzLanguage(const AtomicString& locale) {
  CString locale_as_latin1 = locale.Latin1();
  return hb_language_from_string(locale_as_latin1.Data(),
                                 locale_as_latin1.length());
}

// SkFontMgr requires script-based locale names, like "zh-Hant" and "zh-Hans",
// instead of "zh-CN" and "zh-TW".
static const char* ToSkFontMgrLocale(UScriptCode script) {
  switch (script) {
    case USCRIPT_KATAKANA_OR_HIRAGANA:
      return "ja-JP";
    case USCRIPT_HANGUL:
      return "ko-KR";
    case USCRIPT_SIMPLIFIED_HAN:
      return "zh-Hans";
    case USCRIPT_TRADITIONAL_HAN:
      return "zh-Hant";
    default:
      return nullptr;
  }
}

const char* LayoutLocale::LocaleForSkFontMgr() const {
  if (string_for_sk_font_mgr_.IsNull()) {
    string_for_sk_font_mgr_ = ToSkFontMgrLocale(script_);
    if (string_for_sk_font_mgr_.IsNull())
      string_for_sk_font_mgr_ = string_.Ascii();
  }
  return string_for_sk_font_mgr_.Data();
}

void LayoutLocale::ComputeScriptForHan() const {
  if (IsUnambiguousHanScript(script_)) {
    script_for_han_ = script_;
    has_script_for_han_ = true;
    return;
  }

  script_for_han_ = ScriptCodeForHanFromSubtags(string_);
  if (script_for_han_ == USCRIPT_COMMON)
    script_for_han_ = USCRIPT_SIMPLIFIED_HAN;
  else
    has_script_for_han_ = true;
  DCHECK(IsUnambiguousHanScript(script_for_han_));
}

UScriptCode LayoutLocale::GetScriptForHan() const {
  if (script_for_han_ == USCRIPT_COMMON)
    ComputeScriptForHan();
  return script_for_han_;
}

bool LayoutLocale::HasScriptForHan() const {
  if (script_for_han_ == USCRIPT_COMMON)
    ComputeScriptForHan();
  return has_script_for_han_;
}

const LayoutLocale* LayoutLocale::LocaleForHan(
    const LayoutLocale* content_locale) {
  if (content_locale && content_locale->HasScriptForHan())
    return content_locale;
  if (!default_for_han_computed_)
    ComputeLocaleForHan();
  return default_for_han_;
}

void LayoutLocale::ComputeLocaleForHan() {
  if (const LayoutLocale* locale = AcceptLanguagesResolver::LocaleForHan())
    default_for_han_ = locale;
  else if (GetDefault().HasScriptForHan())
    default_for_han_ = &GetDefault();
  else if (GetSystem().HasScriptForHan())
    default_for_han_ = &GetSystem();
  else
    default_for_han_ = nullptr;
  default_for_han_computed_ = true;
}

const char* LayoutLocale::LocaleForHanForSkFontMgr() const {
  const char* locale = ToSkFontMgrLocale(GetScriptForHan());
  DCHECK(locale);
  return locale;
}

LayoutLocale::LayoutLocale(const AtomicString& locale)
    : string_(locale),
      harfbuzz_language_(ToHarfbuzLanguage(locale)),
      script_(LocaleToScriptCodeForFontSelection(locale)),
      script_for_han_(USCRIPT_COMMON),
      has_script_for_han_(false),
      hyphenation_computed_(false) {}

using LayoutLocaleMap =
    HashMap<AtomicString, RefPtr<LayoutLocale>, CaseFoldingHash>;

static LayoutLocaleMap& GetLocaleMap() {
  DEFINE_STATIC_LOCAL(LayoutLocaleMap, locale_map, ());
  return locale_map;
}

const LayoutLocale* LayoutLocale::Get(const AtomicString& locale) {
  if (locale.IsNull())
    return nullptr;

  auto result = GetLocaleMap().insert(locale, nullptr);
  if (result.is_new_entry)
    result.stored_value->value = AdoptRef(new LayoutLocale(locale));
  return result.stored_value->value.Get();
}

const LayoutLocale& LayoutLocale::GetDefault() {
  if (default_)
    return *default_;

  AtomicString locale = DefaultLanguage();
  default_ = Get(!locale.IsEmpty() ? locale : "en");
  return *default_;
}

const LayoutLocale& LayoutLocale::GetSystem() {
  if (system_)
    return *system_;

  // Platforms such as Windows can give more information than the default
  // locale, such as "en-JP" for English speakers in Japan.
  String name = icu::Locale::getDefault().getName();
  system_ = Get(AtomicString(name.Replace('_', '-')));
  return *system_;
}

PassRefPtr<LayoutLocale> LayoutLocale::CreateForTesting(
    const AtomicString& locale) {
  return AdoptRef(new LayoutLocale(locale));
}

void LayoutLocale::ClearForTesting() {
  default_ = nullptr;
  system_ = nullptr;
  default_for_han_ = nullptr;
  default_for_han_computed_ = false;
  GetLocaleMap().Clear();
}

Hyphenation* LayoutLocale::GetHyphenation() const {
  if (hyphenation_computed_)
    return hyphenation_.Get();

  hyphenation_computed_ = true;
  hyphenation_ = Hyphenation::PlatformGetHyphenation(LocaleString());
  return hyphenation_.Get();
}

void LayoutLocale::SetHyphenationForTesting(
    const AtomicString& locale_string,
    PassRefPtr<Hyphenation> hyphenation) {
  const LayoutLocale& locale = ValueOrDefault(Get(locale_string));
  locale.hyphenation_computed_ = true;
  locale.hyphenation_ = std::move(hyphenation);
}

AtomicString LayoutLocale::LocaleWithBreakKeyword(
    LineBreakIteratorMode mode) const {
  if (string_.IsEmpty())
    return string_;

  // uloc_setKeywordValue_58 has a problem to handle "@" in the original
  // string. crbug.com/697859
  if (string_.Contains('@'))
    return string_;

  CString utf8_locale = string_.Utf8();
  Vector<char> buffer(utf8_locale.length() + 11, 0);
  memcpy(buffer.Data(), utf8_locale.Data(), utf8_locale.length());

  const char* keyword_value = nullptr;
  switch (mode) {
    default:
      NOTREACHED();
    // Fall through.
    case LineBreakIteratorMode::kDefault:
      // nullptr will cause any existing values to be removed.
      break;
    case LineBreakIteratorMode::kNormal:
      keyword_value = "normal";
      break;
    case LineBreakIteratorMode::kStrict:
      keyword_value = "strict";
      break;
    case LineBreakIteratorMode::kLoose:
      keyword_value = "loose";
      break;
  }

  ICUError status;
  int32_t length_needed = uloc_setKeywordValue(
      "lb", keyword_value, buffer.Data(), buffer.size(), &status);
  if (U_SUCCESS(status))
    return AtomicString::FromUTF8(buffer.Data(), length_needed);

  if (status == U_BUFFER_OVERFLOW_ERROR) {
    buffer.Grow(length_needed + 1);
    memset(buffer.Data() + utf8_locale.length(), 0,
           buffer.size() - utf8_locale.length());
    status = U_ZERO_ERROR;
    int32_t length_needed2 = uloc_setKeywordValue(
        "lb", keyword_value, buffer.Data(), buffer.size(), &status);
    DCHECK_EQ(length_needed, length_needed2);
    if (U_SUCCESS(status) && length_needed == length_needed2)
      return AtomicString::FromUTF8(buffer.Data(), length_needed);
  }

  NOTREACHED();
  return string_;
}

}  // namespace blink
