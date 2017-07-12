// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/DepthOrderedLayoutObjectList.h"

#include "core/frame/FrameView.h"
#include "core/layout/LayoutObject.h"
#include <algorithm>

namespace blink {

struct DepthOrderedLayoutObjectListData {
  // LayoutObjects sorted by depth (deepest first). This structure is only
  // populated at the beginning of enumerations. See ordered().
  Vector<DepthOrderedLayoutObjectList::LayoutObjectWithDepth> ordered_objects_;

  // Outside of layout, LayoutObjects can be added and removed as needed such
  // as when style was changed or destroyed. They're kept in this hashset to
  // keep those operations fast.
  HashSet<LayoutObject*> objects_;
};

DepthOrderedLayoutObjectList::DepthOrderedLayoutObjectList()
    : data_(new DepthOrderedLayoutObjectListData) {}

DepthOrderedLayoutObjectList::~DepthOrderedLayoutObjectList() {
  delete data_;
}

int DepthOrderedLayoutObjectList::size() const {
  return data_->objects_.size();
}

bool DepthOrderedLayoutObjectList::IsEmpty() const {
  return data_->objects_.IsEmpty();
}

void DepthOrderedLayoutObjectList::Add(LayoutObject& object) {
  DCHECK(!object.GetFrameView()->IsInPerformLayout());
  data_->objects_.insert(&object);
  data_->ordered_objects_.Clear();
}

void DepthOrderedLayoutObjectList::Remove(LayoutObject& object) {
  auto it = data_->objects_.Find(&object);
  if (it == data_->objects_.end())
    return;
  DCHECK(!object.GetFrameView()->IsInPerformLayout());
  data_->objects_.erase(it);
  data_->ordered_objects_.Clear();
}

void DepthOrderedLayoutObjectList::Clear() {
  data_->objects_.Clear();
  data_->ordered_objects_.Clear();
}

unsigned DepthOrderedLayoutObjectList::LayoutObjectWithDepth::DetermineDepth(
    LayoutObject* object) {
  unsigned depth = 1;
  for (LayoutObject* parent = object->Parent(); parent;
       parent = parent->Parent())
    ++depth;
  return depth;
}

const HashSet<LayoutObject*>& DepthOrderedLayoutObjectList::Unordered() const {
  return data_->objects_;
}

const Vector<DepthOrderedLayoutObjectList::LayoutObjectWithDepth>&
DepthOrderedLayoutObjectList::Ordered() {
  if (data_->objects_.IsEmpty() || !data_->ordered_objects_.IsEmpty())
    return data_->ordered_objects_;

  CopyToVector(data_->objects_, data_->ordered_objects_);
  std::sort(data_->ordered_objects_.begin(), data_->ordered_objects_.end());
  return data_->ordered_objects_;
}

}  // namespace blink
