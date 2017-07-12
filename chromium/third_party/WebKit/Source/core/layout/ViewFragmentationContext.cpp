// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ViewFragmentationContext.h"

#include "core/layout/LayoutView.h"

namespace blink {

bool ViewFragmentationContext::IsFragmentainerLogicalHeightKnown() {
  DCHECK(view_.PageLogicalHeight());
  return true;
}

LayoutUnit ViewFragmentationContext::FragmentainerLogicalHeightAt(LayoutUnit) {
  DCHECK(view_.PageLogicalHeight());
  return view_.PageLogicalHeight();
}

LayoutUnit ViewFragmentationContext::RemainingLogicalHeightAt(
    LayoutUnit block_offset) {
  LayoutUnit page_logical_height = view_.PageLogicalHeight();
  return page_logical_height - IntMod(block_offset, page_logical_height);
}

}  // namespace blink
