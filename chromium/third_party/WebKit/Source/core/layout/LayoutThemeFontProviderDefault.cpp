/*
 * Copyright (C) 2012 Google Inc.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/layout/LayoutThemeFontProvider.h"

#include "core/CSSValueKeywords.h"
#include "platform/fonts/FontDescription.h"
#include "platform/wtf/StdLibExtras.h"
#include "platform/wtf/text/WTFString.h"

namespace blink {

// static
void LayoutThemeFontProvider::SetDefaultFontSize(int font_size) {
  default_font_size_ = static_cast<float>(font_size);
}

// static
void LayoutThemeFontProvider::SystemFont(CSSValueID system_font_id,
                                         FontStyle& font_style,
                                         FontWeight& font_weight,
                                         float& font_size,
                                         AtomicString& font_family) {
  font_weight = kFontWeightNormal;
  font_style = kFontStyleNormal;
  font_size = default_font_size_;
  font_family = DefaultGUIFont();

  switch (system_font_id) {
    case CSSValueWebkitMiniControl:
    case CSSValueWebkitSmallControl:
    case CSSValueWebkitControl:
      // Why 2 points smaller? Because that's what Gecko does. Note that we
      // are assuming a 96dpi screen, which is the default that we use on
      // Windows.
      static const float kPointsPerInch = 72.0f;
      static const float kPixelsPerInch = 96.0f;
      font_size -= (2.0f / kPointsPerInch) * kPixelsPerInch;
      break;
    default:
      break;
  }
}

}  // namespace blink
