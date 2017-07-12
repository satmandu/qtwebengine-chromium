/*
 * Copyright (C) 2011, 2012 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CSSValuePool_h
#define CSSValuePool_h

#include "core/CSSPropertyNames.h"
#include "core/CSSValueKeywords.h"
#include "core/CoreExport.h"
#include "core/css/CSSColorValue.h"
#include "core/css/CSSCustomIdentValue.h"
#include "core/css/CSSFontFamilyValue.h"
#include "core/css/CSSIdentifierValue.h"
#include "core/css/CSSInheritedValue.h"
#include "core/css/CSSInitialValue.h"
#include "core/css/CSSPrimitiveValue.h"
#include "core/css/CSSUnsetValue.h"
#include "core/css/CSSValueList.h"
#include "platform/wtf/HashMap.h"
#include "platform/wtf/RefPtr.h"
#include "platform/wtf/text/AtomicStringHash.h"

namespace blink {

class CORE_EXPORT CSSValuePool
    : public GarbageCollectedFinalized<CSSValuePool> {
  WTF_MAKE_NONCOPYABLE(CSSValuePool);

 public:
  // TODO(sashab): Make all the value pools store const CSSValues.
  static const int kMaximumCacheableIntegerValue = 255;
  using CSSColorValue = cssvalue::CSSColorValue;
  using ColorValueCache = HeapHashMap<unsigned, Member<CSSColorValue>>;
  static const unsigned kMaximumColorCacheSize = 512;
  using FontFaceValueCache =
      HeapHashMap<AtomicString, Member<const CSSValueList>>;
  static const unsigned kMaximumFontFaceCacheSize = 128;
  using FontFamilyValueCache = HeapHashMap<String, Member<CSSFontFamilyValue>>;

  // Cached individual values.
  CSSColorValue* TransparentColor() { return color_transparent_; }
  CSSColorValue* WhiteColor() { return color_white_; }
  CSSColorValue* BlackColor() { return color_black_; }
  CSSInheritedValue* InheritedValue() { return inherited_value_; }
  CSSInitialValue* InitialValue() { return initial_value_; }
  CSSUnsetValue* UnsetValue() { return unset_value_; }

  // Vector caches.
  CSSIdentifierValue* IdentifierCacheValue(CSSValueID ident) {
    return identifier_value_cache_[ident];
  }
  CSSIdentifierValue* SetIdentifierCacheValue(CSSValueID ident,
                                              CSSIdentifierValue* css_value) {
    return identifier_value_cache_[ident] = css_value;
  }
  CSSPrimitiveValue* PixelCacheValue(int int_value) {
    return pixel_value_cache_[int_value];
  }
  CSSPrimitiveValue* SetPixelCacheValue(int int_value,
                                        CSSPrimitiveValue* css_value) {
    return pixel_value_cache_[int_value] = css_value;
  }
  CSSPrimitiveValue* PercentCacheValue(int int_value) {
    return percent_value_cache_[int_value];
  }
  CSSPrimitiveValue* SetPercentCacheValue(int int_value,
                                          CSSPrimitiveValue* css_value) {
    return percent_value_cache_[int_value] = css_value;
  }
  CSSPrimitiveValue* NumberCacheValue(int int_value) {
    return number_value_cache_[int_value];
  }
  CSSPrimitiveValue* SetNumberCacheValue(int int_value,
                                         CSSPrimitiveValue* css_value) {
    return number_value_cache_[int_value] = css_value;
  }

  // Hash map caches.
  ColorValueCache::AddResult GetColorCacheEntry(RGBA32 rgb_value) {
    // Just wipe out the cache and start rebuilding if it gets too big.
    if (color_value_cache_.size() > kMaximumColorCacheSize)
      color_value_cache_.Clear();
    return color_value_cache_.insert(rgb_value, nullptr);
  }
  FontFamilyValueCache::AddResult GetFontFamilyCacheEntry(
      const String& family_name) {
    return font_family_value_cache_.insert(family_name, nullptr);
  }
  FontFaceValueCache::AddResult GetFontFaceCacheEntry(
      const AtomicString& string) {
    // Just wipe out the cache and start rebuilding if it gets too big.
    if (font_face_value_cache_.size() > kMaximumFontFaceCacheSize)
      font_face_value_cache_.Clear();
    return font_face_value_cache_.insert(string, nullptr);
  }

  DECLARE_TRACE();

 private:
  CSSValuePool();

  // Cached individual values.
  Member<CSSInheritedValue> inherited_value_;
  Member<CSSInitialValue> initial_value_;
  Member<CSSUnsetValue> unset_value_;
  Member<CSSColorValue> color_transparent_;
  Member<CSSColorValue> color_white_;
  Member<CSSColorValue> color_black_;

  // Vector caches.
  HeapVector<Member<CSSIdentifierValue>, numCSSValueKeywords>
      identifier_value_cache_;
  HeapVector<Member<CSSPrimitiveValue>, kMaximumCacheableIntegerValue + 1>
      pixel_value_cache_;
  HeapVector<Member<CSSPrimitiveValue>, kMaximumCacheableIntegerValue + 1>
      percent_value_cache_;
  HeapVector<Member<CSSPrimitiveValue>, kMaximumCacheableIntegerValue + 1>
      number_value_cache_;

  // Hash map caches.
  ColorValueCache color_value_cache_;
  FontFaceValueCache font_face_value_cache_;
  FontFamilyValueCache font_family_value_cache_;

  friend CORE_EXPORT CSSValuePool& CssValuePool();
};

CORE_EXPORT CSSValuePool& CssValuePool();

}  // namespace blink

#endif
