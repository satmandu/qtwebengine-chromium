// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/fonts/AcceptLanguagesResolver.h"

#include "platform/LayoutLocale.h"

namespace blink {

static String& CurrentAcceptLanguages() {
  DEFINE_STATIC_LOCAL(String, current, ());
  return current;
}

void AcceptLanguagesResolver::AcceptLanguagesChanged(
    const String& accept_languages) {
  String& current_value = CurrentAcceptLanguages();
  if (current_value == accept_languages)
    return;

  current_value = accept_languages;
  LayoutLocale::InvalidateLocaleForHan();
}

const LayoutLocale* AcceptLanguagesResolver::LocaleForHan() {
  return LocaleForHanFromAcceptLanguages(CurrentAcceptLanguages());
}

const LayoutLocale* AcceptLanguagesResolver::LocaleForHanFromAcceptLanguages(
    const String& accept_languages) {
  // Use the first acceptLanguages that can disambiguate.
  Vector<String> languages;
  accept_languages.Split(',', languages);
  for (String token : languages) {
    token = token.StripWhiteSpace();
    const LayoutLocale* locale = LayoutLocale::Get(AtomicString(token));
    if (locale->HasScriptForHan())
      return locale;
  }

  return nullptr;
}

}  // namespace blink
