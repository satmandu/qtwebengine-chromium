/*
 * Copyright (C) 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CSSSelectorList_h
#define CSSSelectorList_h

#include "core/CoreExport.h"
#include "core/css/CSSSelector.h"
#include <memory>

namespace blink {

class CSSParserSelector;

class CORE_EXPORT CSSSelectorList {
  USING_FAST_MALLOC(CSSSelectorList);

 public:
  CSSSelectorList() : selector_array_(nullptr) {}

  CSSSelectorList(CSSSelectorList&& o) : selector_array_(o.selector_array_) {
    o.selector_array_ = nullptr;
  }

  CSSSelectorList& operator=(CSSSelectorList&& o) {
    DeleteSelectorsIfNeeded();
    selector_array_ = o.selector_array_;
    o.selector_array_ = nullptr;
    return *this;
  }

  ~CSSSelectorList() { DeleteSelectorsIfNeeded(); }

  static CSSSelectorList AdoptSelectorVector(
      Vector<std::unique_ptr<CSSParserSelector>>& selector_vector);
  CSSSelectorList Copy() const;

  bool IsValid() const { return !!selector_array_; }
  const CSSSelector* First() const { return selector_array_; }
  static const CSSSelector* Next(const CSSSelector&);
  bool HasOneSelector() const {
    return selector_array_ && !Next(*selector_array_);
  }
  const CSSSelector& SelectorAt(size_t index) const {
    return selector_array_[index];
  }

  size_t SelectorIndex(const CSSSelector& selector) const {
    return &selector - selector_array_;
  }

  size_t IndexOfNextSelectorAfter(size_t index) const {
    const CSSSelector& current = SelectorAt(index);
    const CSSSelector* next = this->Next(current);
    if (!next)
      return kNotFound;
    return SelectorIndex(*next);
  }

  String SelectorsText() const;

  // Selector lists don't know their length, computing it is O(n) and should be
  // avoided when possible. Instead iterate from first() and using next().
  unsigned ComputeLength() const;

 private:
  void DeleteSelectorsIfNeeded() {
    if (selector_array_)
      DeleteSelectors();
  }
  void DeleteSelectors();

  CSSSelectorList(const CSSSelectorList&) = delete;
  CSSSelectorList& operator=(const CSSSelectorList&) = delete;

  // End of a multipart selector is indicated by is_last_in_tag_history_ bit in
  // the last item. End of the array is indicated by is_last_in_selector_list_
  // bit in the last item.
  CSSSelector* selector_array_;
};

inline const CSSSelector* CSSSelectorList::Next(const CSSSelector& current) {
  // Skip subparts of compound selectors.
  const CSSSelector* last = &current;
  while (!last->IsLastInTagHistory())
    last++;
  return last->IsLastInSelectorList() ? 0 : last + 1;
}

}  // namespace blink

#endif  // CSSSelectorList_h
