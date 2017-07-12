// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TREES_CLIP_NODE_H_
#define CC_TREES_CLIP_NODE_H_

#include <memory>

#include "cc/cc_export.h"
#include "cc/trees/clip_expander.h"
#include "ui/gfx/geometry/rect_f.h"

namespace base {
namespace trace_event {
class TracedValue;
}  // namespace trace_event
}  // namespace base

namespace cc {

struct CC_EXPORT ClipNode {
  ClipNode();
  ClipNode(const ClipNode& other);

  ClipNode& operator=(const ClipNode& other);

  ~ClipNode();

  static const int defaultCachedClipsSize = 1;

  // The node index of this node in the clip tree node vector.
  int id;
  // The node index of the parent node in the clip tree node vector.
  int parent_id;
  // The layer id of the layer that owns this node.
  int owning_layer_id;

  enum class ClipType {
    // The node contributes a new clip (that is, |clip| needs to be applied).
    APPLIES_LOCAL_CLIP,

    // This node represents a space expansion. When computing visible rects,
    // the accumulated clip inherited by this node gets expanded. Similarly,
    // when mapping a rect in descendant space to the rect in ancestor space
    // that depends on the descendant rect's contents, this node expands the
    // descendant rect. This is used for effects like pixel-moving filters,
    // where clipped-out content can affect visible output.
    EXPANDS_CLIP
  };

  ClipType clip_type;

  // The clip rect that this node contributes, expressed in the space of its
  // transform node.
  gfx::RectF clip;

  // Each element of this cache stores the accumulated clip from this clip
  // node to a particular target.
  mutable std::vector<ClipRectData> cached_clip_rects;

  // This rect accumulates all clips from this node to the root in screen space.
  // It is used in the computation of layer's visible rect.
  gfx::RectF cached_accumulated_rect_in_screen_space;

  // For nodes that expand, this represents the amount of expansion.
  std::unique_ptr<ClipExpander> clip_expander;

  // The id of the transform node that defines the clip node's local space.
  int transform_id;

  bool operator==(const ClipNode& other) const;

  void AsValueInto(base::trace_event::TracedValue* value) const;
};

}  // namespace cc

#endif  // CC_TREES_CLIP_NODE_H_
