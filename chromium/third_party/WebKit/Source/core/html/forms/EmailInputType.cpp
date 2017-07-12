/*
 * This file is part of the WebKit project.
 *
 * Copyright (C) 2009 Michelangelo De Simone <micdesim@gmail.com>
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "core/html/forms/EmailInputType.h"

#include <unicode/idna.h>
#include <unicode/unistr.h>
#include <unicode/uvernum.h>
#include "bindings/core/v8/ScriptRegexp.h"
#include "core/InputTypeNames.h"
#include "core/html/HTMLInputElement.h"
#include "core/html/parser/HTMLParserIdioms.h"
#include "core/page/ChromeClient.h"
#include "platform/text/PlatformLocale.h"
#include "platform/wtf/text/StringBuilder.h"
#include "public/platform/Platform.h"

#if U_ICU_VERSION_MAJOR_NUM >= 59
#include <unicode/char16ptr.h>
#endif

namespace blink {

using blink::WebLocalizedString;

// http://www.whatwg.org/specs/web-apps/current-work/multipage/states-of-the-type-attribute.html#valid-e-mail-address
static const char kLocalPartCharacters[] =
    "abcdefghijklmnopqrstuvwxyz0123456789!#$%&'*+/=?^_`{|}~.-";
static const char kEmailPattern[] =
    "[a-z0-9!#$%&'*+/=?^_`{|}~.-]+"  // local part
    "@"
    "[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?"  // domain part
    "(?:\\.[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?)*";

// RFC5321 says the maximum total length of a domain name is 255 octets.
static const int32_t kMaximumDomainNameLength = 255;
// Use the same option as in url/url_canon_icu.cc
static const int32_t kIdnaConversionOption = UIDNA_CHECK_BIDI;

std::unique_ptr<ScriptRegexp> EmailInputType::CreateEmailRegexp() {
  return std::unique_ptr<ScriptRegexp>(
      new ScriptRegexp(kEmailPattern, kTextCaseUnicodeInsensitive));
}

String EmailInputType::ConvertEmailAddressToASCII(const ScriptRegexp& regexp,
                                                  const String& address) {
  if (address.ContainsOnlyASCII())
    return address;

  size_t at_position = address.Find('@');
  if (at_position == kNotFound)
    return address;
  String host = address.Substring(at_position + 1);

  // UnicodeString ctor for copy-on-write does not work reliably (in debug
  // build.) TODO(jshin): In an unlikely case this is a perf-issue, treat
  // 8bit and non-8bit strings separately.
  host.Ensure16Bit();
  icu::UnicodeString idn_domain_name(host.Characters16(), host.length());
  icu::UnicodeString domain_name;

  // Leak |idna| at the end.
  UErrorCode error_code = U_ZERO_ERROR;
  static icu::IDNA* idna =
      icu::IDNA::createUTS46Instance(kIdnaConversionOption, error_code);
  DCHECK(idna);
  icu::IDNAInfo idna_info;
  idna->nameToASCII(idn_domain_name, domain_name, idna_info, error_code);
  if (U_FAILURE(error_code) || idna_info.hasErrors() ||
      domain_name.length() > kMaximumDomainNameLength)
    return address;

  StringBuilder builder;
  builder.Append(address, 0, at_position + 1);
#if U_ICU_VERSION_MAJOR_NUM >= 59
  builder.append(icu::toUCharPtr(domainName.getBuffer()), domainName.length());
#else
  builder.Append(domain_name.getBuffer(), domain_name.length());
#endif
  String ascii_email = builder.ToString();
  return IsValidEmailAddress(regexp, ascii_email) ? ascii_email : address;
}

String EmailInputType::ConvertEmailAddressToUnicode(
    const String& address) const {
  if (!address.ContainsOnlyASCII())
    return address;

  size_t at_position = address.Find('@');
  if (at_position == kNotFound)
    return address;

  if (address.Find("xn--", at_position + 1) == kNotFound)
    return address;

  String unicode_host = Platform::Current()->ConvertIDNToUnicode(
      address.Substring(at_position + 1));
  StringBuilder builder;
  builder.Append(address, 0, at_position + 1);
  builder.Append(unicode_host);
  return builder.ToString();
}

static bool IsInvalidLocalPartCharacter(UChar ch) {
  if (!IsASCII(ch))
    return true;
  DEFINE_STATIC_LOCAL(const String, valid_characters, (kLocalPartCharacters));
  return valid_characters.Find(ToASCIILower(ch)) == kNotFound;
}

static bool IsInvalidDomainCharacter(UChar ch) {
  if (!IsASCII(ch))
    return true;
  return !IsASCIILower(ch) && !IsASCIIUpper(ch) && !IsASCIIDigit(ch) &&
         ch != '.' && ch != '-';
}

static bool CheckValidDotUsage(const String& domain) {
  if (domain.IsEmpty())
    return true;
  if (domain[0] == '.' || domain[domain.length() - 1] == '.')
    return false;
  return domain.Find("..") == kNotFound;
}

bool EmailInputType::IsValidEmailAddress(const ScriptRegexp& regexp,
                                         const String& address) {
  int address_length = address.length();
  if (!address_length)
    return false;

  int match_length;
  int match_offset = regexp.Match(address, 0, &match_length);

  return !match_offset && match_length == address_length;
}

EmailInputType::EmailInputType(HTMLInputElement& element)
    : BaseTextInputType(element) {}

InputType* EmailInputType::Create(HTMLInputElement& element) {
  return new EmailInputType(element);
}

void EmailInputType::CountUsage() {
  CountUsageIfVisible(UseCounter::kInputTypeEmail);
  bool has_max_length = GetElement().FastHasAttribute(HTMLNames::maxlengthAttr);
  if (has_max_length)
    CountUsageIfVisible(UseCounter::kInputTypeEmailMaxLength);
  if (GetElement().Multiple()) {
    CountUsageIfVisible(UseCounter::kInputTypeEmailMultiple);
    if (has_max_length)
      CountUsageIfVisible(UseCounter::kInputTypeEmailMultipleMaxLength);
  }
}

const AtomicString& EmailInputType::FormControlType() const {
  return InputTypeNames::email;
}

ScriptRegexp& EmailInputType::EnsureEmailRegexp() const {
  if (!email_regexp_)
    email_regexp_ = CreateEmailRegexp();
  return *email_regexp_;
}

// The return value is an invalid email address string if the specified string
// contains an invalid email address. Otherwise, null string is returned.
// If an empty string is returned, it means empty address is specified.
// e.g. "foo@example.com,,bar@example.com" for multiple case.
String EmailInputType::FindInvalidAddress(const String& value) const {
  if (value.IsEmpty())
    return String();
  if (!GetElement().Multiple())
    return IsValidEmailAddress(EnsureEmailRegexp(), value) ? String() : value;
  Vector<String> addresses;
  value.Split(',', true, addresses);
  for (const auto& address : addresses) {
    String stripped = StripLeadingAndTrailingHTMLSpaces(address);
    if (!IsValidEmailAddress(EnsureEmailRegexp(), stripped))
      return stripped;
  }
  return String();
}

bool EmailInputType::TypeMismatchFor(const String& value) const {
  return !FindInvalidAddress(value).IsNull();
}

bool EmailInputType::TypeMismatch() const {
  return TypeMismatchFor(GetElement().value());
}

String EmailInputType::TypeMismatchText() const {
  String invalid_address = FindInvalidAddress(GetElement().value());
  DCHECK(!invalid_address.IsNull());
  if (invalid_address.IsEmpty())
    return GetLocale().QueryString(
        WebLocalizedString::kValidationTypeMismatchForEmailEmpty);
  String at_sign = String("@");
  size_t at_index = invalid_address.Find('@');
  if (at_index == kNotFound)
    return GetLocale().QueryString(
        WebLocalizedString::kValidationTypeMismatchForEmailNoAtSign, at_sign,
        invalid_address);
  // We check validity against an ASCII value because of difficulty to check
  // invalid characters. However we should show Unicode value.
  String unicode_address = ConvertEmailAddressToUnicode(invalid_address);
  String local_part = invalid_address.Left(at_index);
  String domain = invalid_address.Substring(at_index + 1);
  if (local_part.IsEmpty())
    return GetLocale().QueryString(
        WebLocalizedString::kValidationTypeMismatchForEmailEmptyLocal, at_sign,
        unicode_address);
  if (domain.IsEmpty())
    return GetLocale().QueryString(
        WebLocalizedString::kValidationTypeMismatchForEmailEmptyDomain, at_sign,
        unicode_address);
  size_t invalid_char_index = local_part.Find(IsInvalidLocalPartCharacter);
  if (invalid_char_index != kNotFound) {
    unsigned char_length = U_IS_LEAD(local_part[invalid_char_index]) ? 2 : 1;
    return GetLocale().QueryString(
        WebLocalizedString::kValidationTypeMismatchForEmailInvalidLocal,
        at_sign, local_part.Substring(invalid_char_index, char_length));
  }
  invalid_char_index = domain.Find(IsInvalidDomainCharacter);
  if (invalid_char_index != kNotFound) {
    unsigned char_length = U_IS_LEAD(domain[invalid_char_index]) ? 2 : 1;
    return GetLocale().QueryString(
        WebLocalizedString::kValidationTypeMismatchForEmailInvalidDomain,
        at_sign, domain.Substring(invalid_char_index, char_length));
  }
  if (!CheckValidDotUsage(domain)) {
    size_t at_index_in_unicode = unicode_address.Find('@');
    DCHECK_NE(at_index_in_unicode, kNotFound);
    return GetLocale().QueryString(
        WebLocalizedString::kValidationTypeMismatchForEmailInvalidDots,
        String("."), unicode_address.Substring(at_index_in_unicode + 1));
  }
  if (GetElement().Multiple())
    return GetLocale().QueryString(
        WebLocalizedString::kValidationTypeMismatchForMultipleEmail);
  return GetLocale().QueryString(
      WebLocalizedString::kValidationTypeMismatchForEmail);
}

bool EmailInputType::SupportsSelectionAPI() const {
  return false;
}

String EmailInputType::SanitizeValue(const String& proposed_value) const {
  String no_line_break_value = proposed_value.RemoveCharacters(IsHTMLLineBreak);
  if (!GetElement().Multiple())
    return StripLeadingAndTrailingHTMLSpaces(no_line_break_value);
  Vector<String> addresses;
  no_line_break_value.Split(',', true, addresses);
  StringBuilder stripped_value;
  for (size_t i = 0; i < addresses.size(); ++i) {
    if (i > 0)
      stripped_value.Append(',');
    stripped_value.Append(StripLeadingAndTrailingHTMLSpaces(addresses[i]));
  }
  return stripped_value.ToString();
}

String EmailInputType::ConvertFromVisibleValue(
    const String& visible_value) const {
  String sanitized_value = SanitizeValue(visible_value);
  if (!GetElement().Multiple())
    return ConvertEmailAddressToASCII(EnsureEmailRegexp(), sanitized_value);
  Vector<String> addresses;
  sanitized_value.Split(',', true, addresses);
  StringBuilder builder;
  builder.ReserveCapacity(sanitized_value.length());
  for (size_t i = 0; i < addresses.size(); ++i) {
    if (i > 0)
      builder.Append(',');
    builder.Append(
        ConvertEmailAddressToASCII(EnsureEmailRegexp(), addresses[i]));
  }
  return builder.ToString();
}

String EmailInputType::VisibleValue() const {
  String value = GetElement().value();
  if (!GetElement().Multiple())
    return ConvertEmailAddressToUnicode(value);

  Vector<String> addresses;
  value.Split(',', true, addresses);
  StringBuilder builder;
  builder.ReserveCapacity(value.length());
  for (size_t i = 0; i < addresses.size(); ++i) {
    if (i > 0)
      builder.Append(',');
    builder.Append(ConvertEmailAddressToUnicode(addresses[i]));
  }
  return builder.ToString();
}

}  // namespace blink
