// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_LAYERS_LAYER_STICKY_POSITION_CONSTRAINT_H_
#define CC_LAYERS_LAYER_STICKY_POSITION_CONSTRAINT_H_

#include "cc/base/cc_export.h"

#include "ui/gfx/geometry/rect.h"

namespace cc {

struct CC_EXPORT LayerStickyPositionConstraint {
  LayerStickyPositionConstraint();
  LayerStickyPositionConstraint(const LayerStickyPositionConstraint& other);

  bool is_sticky : 1;
  bool is_anchored_left : 1;
  bool is_anchored_right : 1;
  bool is_anchored_top : 1;
  bool is_anchored_bottom : 1;

  // The offset from each edge of the ancestor scroller (or the viewport) to
  // try to maintain to the sticky box as we scroll.
  float left_offset;
  float right_offset;
  float top_offset;
  float bottom_offset;

  // The layout offset of the sticky box relative to its containing layer.
  // This is used to detect the sticky offset the main thread has applied
  // to the layer.
  gfx::Point parent_relative_sticky_box_offset;

  // The rectangle corresponding to original layout position of the sticky box
  // relative to the scroll ancestor. The sticky box is only offset once the
  // scroll has passed its initial position (e.g. top_offset will only push
  // the element down from its original position).
  gfx::Rect scroll_container_relative_sticky_box_rect;

  // The layout rectangle of the sticky box's containing block relative to the
  // scroll ancestor. The sticky box is only moved as far as its containing
  // block boundary.
  gfx::Rect scroll_container_relative_containing_block_rect;

  bool operator==(const LayerStickyPositionConstraint&) const;
  bool operator!=(const LayerStickyPositionConstraint&) const;
};

}  // namespace cc

#endif  // CC_LAYERS_LAYER_STICKY_POSITION_CONSTRAINT_H_
