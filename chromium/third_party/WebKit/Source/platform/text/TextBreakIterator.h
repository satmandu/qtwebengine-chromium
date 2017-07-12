/*
 * Copyright (C) 2006 Lars Knoll <lars@trolltech.com>
 * Copyright (C) 2007, 2011, 2012 Apple Inc. All rights reserved.
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

#ifndef TextBreakIterator_h
#define TextBreakIterator_h

#include "platform/PlatformExport.h"
#include "platform/wtf/text/AtomicString.h"
#include "platform/wtf/text/Unicode.h"

#include <unicode/brkiter.h>

namespace blink {

typedef icu::BreakIterator TextBreakIterator;

// Note: The returned iterator is good only until you get another iterator, with
// the exception of acquireLineBreakIterator.

// This is similar to character break iterator in most cases, but is subject to
// platform UI conventions. One notable example where this can be different
// from character break iterator is Thai prepend characters, see bug 24342.
// Use this for insertion point and selection manipulations.
PLATFORM_EXPORT TextBreakIterator* CursorMovementIterator(const UChar*,
                                                          int length);

PLATFORM_EXPORT TextBreakIterator* WordBreakIterator(const String&,
                                                     int start,
                                                     int length);
PLATFORM_EXPORT TextBreakIterator* WordBreakIterator(const UChar*, int length);
PLATFORM_EXPORT TextBreakIterator* AcquireLineBreakIterator(
    const LChar*,
    int length,
    const AtomicString& locale,
    const UChar* prior_context,
    unsigned prior_context_length);
PLATFORM_EXPORT TextBreakIterator* AcquireLineBreakIterator(
    const UChar*,
    int length,
    const AtomicString& locale,
    const UChar* prior_context,
    unsigned prior_context_length);
PLATFORM_EXPORT void ReleaseLineBreakIterator(TextBreakIterator*);
PLATFORM_EXPORT TextBreakIterator* SentenceBreakIterator(const UChar*,
                                                         int length);

PLATFORM_EXPORT bool IsWordTextBreak(TextBreakIterator*);

const int kTextBreakDone = -1;

enum class LineBreakType {
  kNormal,
  kBreakAll,  // word-break:break-all allows breaks between letters/numbers
  kKeepAll,   // word-break:keep-all doesn't allow breaks between all kind of
              // letters/numbers except some south east asians'.
};

class PLATFORM_EXPORT LazyLineBreakIterator final {
  STACK_ALLOCATED();

 public:
  LazyLineBreakIterator()
      : iterator_(0),
        cached_prior_context_(0),
        cached_prior_context_length_(0) {
    ResetPriorContext();
  }

  LazyLineBreakIterator(String string,
                        const AtomicString& locale = AtomicString())
      : string_(string),
        locale_(locale),
        iterator_(0),
        cached_prior_context_(0),
        cached_prior_context_length_(0) {
    ResetPriorContext();
  }

  ~LazyLineBreakIterator() {
    if (iterator_)
      ReleaseLineBreakIterator(iterator_);
  }

  String GetString() const { return string_; }

  UChar LastCharacter() const {
    static_assert(WTF_ARRAY_LENGTH(prior_context_) == 2,
                  "TextBreakIterator has unexpected prior context length");
    return prior_context_[1];
  }

  UChar SecondToLastCharacter() const {
    static_assert(WTF_ARRAY_LENGTH(prior_context_) == 2,
                  "TextBreakIterator has unexpected prior context length");
    return prior_context_[0];
  }

  void SetPriorContext(UChar last, UChar second_to_last) {
    static_assert(WTF_ARRAY_LENGTH(prior_context_) == 2,
                  "TextBreakIterator has unexpected prior context length");
    prior_context_[0] = second_to_last;
    prior_context_[1] = last;
  }

  void UpdatePriorContext(UChar last) {
    static_assert(WTF_ARRAY_LENGTH(prior_context_) == 2,
                  "TextBreakIterator has unexpected prior context length");
    prior_context_[0] = prior_context_[1];
    prior_context_[1] = last;
  }

  void ResetPriorContext() {
    static_assert(WTF_ARRAY_LENGTH(prior_context_) == 2,
                  "TextBreakIterator has unexpected prior context length");
    prior_context_[0] = 0;
    prior_context_[1] = 0;
  }

  unsigned PriorContextLength() const {
    unsigned prior_context_length = 0;
    static_assert(WTF_ARRAY_LENGTH(prior_context_) == 2,
                  "TextBreakIterator has unexpected prior context length");
    if (prior_context_[1]) {
      ++prior_context_length;
      if (prior_context_[0])
        ++prior_context_length;
    }
    return prior_context_length;
  }

  // Obtain text break iterator, possibly previously cached, where this iterator
  // is (or has been) initialized to use the previously stored string as the
  // primary breaking context and using previously stored prior context if
  // non-empty.
  TextBreakIterator* Get(unsigned prior_context_length) {
    DCHECK(prior_context_length <= kPriorContextCapacity);
    const UChar* prior_context =
        prior_context_length
            ? &prior_context_[kPriorContextCapacity - prior_context_length]
            : 0;
    if (!iterator_) {
      if (string_.Is8Bit())
        iterator_ = AcquireLineBreakIterator(
            string_.Characters8(), string_.length(), locale_, prior_context,
            prior_context_length);
      else
        iterator_ = AcquireLineBreakIterator(
            string_.Characters16(), string_.length(), locale_, prior_context,
            prior_context_length);
      cached_prior_context_ = prior_context;
      cached_prior_context_length_ = prior_context_length;
    } else if (prior_context != cached_prior_context_ ||
               prior_context_length != cached_prior_context_length_) {
      this->ResetStringAndReleaseIterator(string_, locale_);
      return this->Get(prior_context_length);
    }
    return iterator_;
  }

  void ResetStringAndReleaseIterator(String string,
                                     const AtomicString& locale) {
    if (iterator_)
      ReleaseLineBreakIterator(iterator_);

    string_ = string;
    locale_ = locale;
    iterator_ = 0;
    cached_prior_context_ = 0;
    cached_prior_context_length_ = 0;
  }

  inline bool IsBreakable(
      int pos,
      int& next_breakable,
      LineBreakType line_break_type = LineBreakType::kNormal) {
    if (pos > next_breakable) {
      switch (line_break_type) {
        case LineBreakType::kBreakAll:
          next_breakable = NextBreakablePositionBreakAll(pos);
          break;
        case LineBreakType::kKeepAll:
          next_breakable = NextBreakablePositionKeepAll(pos);
          break;
        default:
          next_breakable = NextBreakablePositionIgnoringNBSP(pos);
      }
    }
    return pos == next_breakable;
  }

 private:
  int NextBreakablePositionIgnoringNBSP(int pos);
  int NextBreakablePositionBreakAll(int pos);
  int NextBreakablePositionKeepAll(int pos);

  static const unsigned kPriorContextCapacity = 2;
  String string_;
  AtomicString locale_;
  TextBreakIterator* iterator_;
  UChar prior_context_[kPriorContextCapacity];
  const UChar* cached_prior_context_;
  unsigned cached_prior_context_length_;
};

// Iterates over "extended grapheme clusters", as defined in UAX #29.
// Note that platform implementations may be less sophisticated - e.g. ICU prior
// to version 4.0 only supports "legacy grapheme clusters".  Use this for
// general text processing, e.g. string truncation.

class PLATFORM_EXPORT NonSharedCharacterBreakIterator final {
  STACK_ALLOCATED();
  WTF_MAKE_NONCOPYABLE(NonSharedCharacterBreakIterator);

 public:
  explicit NonSharedCharacterBreakIterator(const String&);
  NonSharedCharacterBreakIterator(const UChar*, unsigned length);
  ~NonSharedCharacterBreakIterator();

  int Next();
  int Current();

  bool IsBreak(int offset) const;
  int Preceding(int offset) const;
  int Following(int offset) const;

  bool operator!() const { return !is8_bit_ && !iterator_; }

 private:
  void CreateIteratorForBuffer(const UChar*, unsigned length);

  unsigned ClusterLengthStartingAt(unsigned offset) const {
    DCHECK(is8_bit_);
    // The only Latin-1 Extended Grapheme Cluster is CR LF
    return IsCRBeforeLF(offset) ? 2 : 1;
  }

  bool IsCRBeforeLF(unsigned offset) const {
    DCHECK(is8_bit_);
    return charaters8_[offset] == '\r' && offset + 1 < length_ &&
           charaters8_[offset + 1] == '\n';
  }

  bool IsLFAfterCR(unsigned offset) const {
    DCHECK(is8_bit_);
    return charaters8_[offset] == '\n' && offset >= 1 &&
           charaters8_[offset - 1] == '\r';
  }

  bool is8_bit_;

  // For 8 bit strings, we implement the iterator ourselves.
  const LChar* charaters8_;
  unsigned offset_;
  unsigned length_;

  // For 16 bit strings, we use a TextBreakIterator.
  TextBreakIterator* iterator_;
};

// Counts the number of grapheme clusters. A surrogate pair or a sequence
// of a non-combining character and following combining characters is
// counted as 1 grapheme cluster.
PLATFORM_EXPORT unsigned NumGraphemeClusters(const String&);

}  // namespace blink

#endif
