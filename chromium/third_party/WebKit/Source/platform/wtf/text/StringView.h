// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WTF_StringView_h
#define WTF_StringView_h

#include "platform/wtf/Allocator.h"
#include "platform/wtf/GetPtr.h"
#if DCHECK_IS_ON()
#include "platform/wtf/RefPtr.h"
#endif
#include "platform/wtf/text/StringImpl.h"
#include "platform/wtf/text/Unicode.h"
#include <cstring>

namespace WTF {

class AtomicString;
class String;

// A string like object that wraps either an 8bit or 16bit byte sequence
// and keeps track of the length and the type, it does NOT own the bytes.
//
// Since StringView does not own the bytes creating a StringView from a String,
// then calling clear() on the String will result in a use-after-free. Asserts
// in ~StringView attempt to enforce this for most common cases.
//
// See base/strings/string_piece.h for more details.
class WTF_EXPORT StringView {
  DISALLOW_NEW_EXCEPT_PLACEMENT_NEW();

 public:
  // Null string.
  StringView() { Clear(); }

  // From a StringView:
  StringView(const StringView&, unsigned offset, unsigned length);
  StringView(const StringView& view, unsigned offset)
      : StringView(view, offset, view.length_ - offset) {}

  // From a StringImpl:
  StringView(const StringImpl*);
  StringView(const StringImpl*, unsigned offset);
  StringView(const StringImpl*, unsigned offset, unsigned length);

  // From a non-null StringImpl.
  StringView(const StringImpl& impl)
      : impl_(const_cast<StringImpl*>(&impl)),
        bytes_(impl.Bytes()),
        length_(impl.length()) {}

  // From a non-null StringImpl, avoids the null check.
  StringView(StringImpl& impl)
      : impl_(&impl), bytes_(impl.Bytes()), length_(impl.length()) {}
  StringView(StringImpl&, unsigned offset);
  StringView(StringImpl&, unsigned offset, unsigned length);

  // From an String, implemented in String.h
  inline StringView(const String&, unsigned offset, unsigned length);
  inline StringView(const String&, unsigned offset);
  inline StringView(const String&);

  // From an AtomicString, implemented in AtomicString.h
  inline StringView(const AtomicString&, unsigned offset, unsigned length);
  inline StringView(const AtomicString&, unsigned offset);
  inline StringView(const AtomicString&);

  // From a literal string or LChar buffer:
  StringView(const LChar* chars, unsigned length)
      : impl_(StringImpl::empty_), characters8_(chars), length_(length) {}
  StringView(const char* chars, unsigned length)
      : StringView(reinterpret_cast<const LChar*>(chars), length) {}
  StringView(const LChar* chars)
      : StringView(chars,
                   chars ? strlen(reinterpret_cast<const char*>(chars)) : 0) {}
  StringView(const char* chars)
      : StringView(reinterpret_cast<const LChar*>(chars)) {}

  // From a wide literal string or UChar buffer.
  StringView(const UChar* chars, unsigned length)
      : impl_(StringImpl::empty16_bit_),
        characters16_(chars),
        length_(length) {}
  StringView(const UChar* chars);
  StringView(const char16_t* chars)
      : StringView(reinterpret_cast<const UChar*>(chars)) {}

#if DCHECK_IS_ON()
  ~StringView();
#endif

  bool IsNull() const { return !bytes_; }
  bool IsEmpty() const { return !length_; }

  unsigned length() const { return length_; }

  bool Is8Bit() const {
    DCHECK(impl_);
    return impl_->Is8Bit();
  }

  void Clear();

  UChar operator[](unsigned i) const {
    SECURITY_DCHECK(i < length());
    if (Is8Bit())
      return Characters8()[i];
    return Characters16()[i];
  }

  const LChar* Characters8() const {
    DCHECK(Is8Bit());
    return characters8_;
  }

  const UChar* Characters16() const {
    DCHECK(!Is8Bit());
    return characters16_;
  }

  const void* Bytes() const { return bytes_; }

  // This is not named impl() like String because it has different semantics.
  // String::impl() is never null if String::isNull() is false. For StringView
  // sharedImpl() can be null if the StringView was created with a non-zero
  // offset, or a length that made it shorter than the underlying impl.
  StringImpl* SharedImpl() const {
    // If this StringView is backed by a StringImpl, and was constructed
    // with a zero offset and the same length we can just access the impl
    // directly since this == StringView(m_impl).
    if (impl_->Bytes() == Bytes() && length_ == impl_->length())
      return GetPtr(impl_);
    return nullptr;
  }

  String ToString() const;
  AtomicString ToAtomicString() const;

  template <bool isSpecialCharacter(UChar)>
  bool IsAllSpecialCharacters() const;

 private:
  void Set(const StringImpl&, unsigned offset, unsigned length);

// We use the StringImpl to mark for 8bit or 16bit, even for strings where
// we were constructed from a char pointer. So m_impl->bytes() might have
// nothing to do with this view's bytes().
#if DCHECK_IS_ON()
  RefPtr<StringImpl> impl_;
#else
  StringImpl* impl_;
#endif
  union {
    const LChar* characters8_;
    const UChar* characters16_;
    const void* bytes_;
  };
  unsigned length_;
};

inline StringView::StringView(const StringView& view,
                              unsigned offset,
                              unsigned length)
    : impl_(view.impl_), length_(length) {
  SECURITY_DCHECK(offset + length <= view.length());
  if (Is8Bit())
    characters8_ = view.Characters8() + offset;
  else
    characters16_ = view.Characters16() + offset;
}

inline StringView::StringView(const StringImpl* impl) {
  if (!impl) {
    Clear();
    return;
  }
  impl_ = const_cast<StringImpl*>(impl);
  length_ = impl->length();
  bytes_ = impl->Bytes();
}

inline StringView::StringView(const StringImpl* impl, unsigned offset) {
  impl ? Set(*impl, offset, impl->length() - offset) : Clear();
}

inline StringView::StringView(const StringImpl* impl,
                              unsigned offset,
                              unsigned length) {
  impl ? Set(*impl, offset, length) : Clear();
}

inline StringView::StringView(StringImpl& impl, unsigned offset) {
  Set(impl, offset, impl.length() - offset);
}

inline StringView::StringView(StringImpl& impl,
                              unsigned offset,
                              unsigned length) {
  Set(impl, offset, length);
}

inline void StringView::Clear() {
  length_ = 0;
  bytes_ = nullptr;
  impl_ = StringImpl::empty_;  // mark as 8 bit.
}

inline void StringView::Set(const StringImpl& impl,
                            unsigned offset,
                            unsigned length) {
  SECURITY_DCHECK(offset + length <= impl.length());
  length_ = length;
  impl_ = const_cast<StringImpl*>(&impl);
  if (impl.Is8Bit())
    characters8_ = impl.Characters8() + offset;
  else
    characters16_ = impl.Characters16() + offset;
}

// Unicode aware case insensitive string matching. Non-ASCII characters might
// match to ASCII characters. These functions are rarely used to implement web
// platform features.
// These functions are deprecated. Use EqualIgnoringASCIICase(), or introduce
// EqualIgnoringUnicodeCase(). See crbug.com/627682
WTF_EXPORT bool DeprecatedEqualIgnoringCase(const StringView&,
                                            const StringView&);
WTF_EXPORT bool DeprecatedEqualIgnoringCaseAndNullity(const StringView&,
                                                      const StringView&);

WTF_EXPORT bool EqualIgnoringASCIICase(const StringView&, const StringView&);

// TODO(esprehn): Can't make this an overload of WTF::equal since that makes
// calls to equal() that pass literal strings ambiguous. Figure out if we can
// replace all the callers with equalStringView and then rename it to equal().
WTF_EXPORT bool EqualStringView(const StringView&, const StringView&);

inline bool operator==(const StringView& a, const StringView& b) {
  return EqualStringView(a, b);
}

inline bool operator!=(const StringView& a, const StringView& b) {
  return !(a == b);
}

template <bool isSpecialCharacter(UChar), typename CharacterType>
inline bool IsAllSpecialCharacters(const CharacterType* characters,
                                   size_t length) {
  for (size_t i = 0; i < length; ++i) {
    if (!isSpecialCharacter(characters[i]))
      return false;
  }
  return true;
}

template <bool isSpecialCharacter(UChar)>
inline bool StringView::IsAllSpecialCharacters() const {
  size_t len = length();
  if (!len)
    return true;

  return Is8Bit() ? WTF::IsAllSpecialCharacters<isSpecialCharacter, LChar>(
                        Characters8(), len)
                  : WTF::IsAllSpecialCharacters<isSpecialCharacter, UChar>(
                        Characters16(), len);
}

}  // namespace WTF

using WTF::StringView;
using WTF::EqualIgnoringASCIICase;
using WTF::DeprecatedEqualIgnoringCase;
using WTF::IsAllSpecialCharacters;

#endif
