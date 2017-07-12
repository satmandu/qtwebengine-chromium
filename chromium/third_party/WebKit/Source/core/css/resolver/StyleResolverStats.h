/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef StyleResolverStats_h
#define StyleResolverStats_h

#include <memory>
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/instrumentation/tracing/TracedValue.h"
#include "platform/wtf/PtrUtil.h"

namespace blink {

class StyleResolverStats {
  USING_FAST_MALLOC(StyleResolverStats);

 public:
  static std::unique_ptr<StyleResolverStats> Create() {
    return WTF::WrapUnique(new StyleResolverStats);
  }

  void Reset();
  bool AllCountersEnabled() const;
  std::unique_ptr<TracedValue> ToTracedValue() const;

  unsigned shared_style_lookups;
  unsigned shared_style_candidates;
  unsigned shared_style_found;
  unsigned shared_style_missed;
  unsigned shared_style_rejected_by_uncommon_attribute_rules;
  unsigned shared_style_rejected_by_sibling_rules;
  unsigned shared_style_rejected_by_parent;
  unsigned matched_property_apply;
  unsigned matched_property_cache_hit;
  unsigned matched_property_cache_inherited_hit;
  unsigned matched_property_cache_added;
  unsigned rules_fast_rejected;
  unsigned rules_rejected;
  unsigned rules_matched;
  unsigned styles_changed;
  unsigned styles_unchanged;
  unsigned styles_animated;
  unsigned elements_styled;
  unsigned pseudo_elements_styled;
  unsigned base_styles_used;
  unsigned independent_inherited_styles_propagated;
  unsigned custom_properties_applied;

 private:
  StyleResolverStats() { Reset(); }
};

#define INCREMENT_STYLE_STATS_COUNTER(styleEngine, counter, n) \
  ((styleEngine).Stats() && ((styleEngine).Stats()->counter += n));

}  // namespace blink

#endif  // StyleResolverStats_h
