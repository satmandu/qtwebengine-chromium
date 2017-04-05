// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/frame_owner_properties.h"

namespace content {

FrameOwnerProperties::FrameOwnerProperties()
    : scrolling_mode(blink::WebFrameOwnerProperties::ScrollingMode::Auto),
      margin_width(-1),
      margin_height(-1),
      allow_fullscreen(false),
      allow_payment_request(false) {}

FrameOwnerProperties::FrameOwnerProperties(const FrameOwnerProperties& other) =
    default;

FrameOwnerProperties::~FrameOwnerProperties() {}

bool FrameOwnerProperties::operator==(const FrameOwnerProperties& other) const {
  return name == other.name && scrolling_mode == other.scrolling_mode &&
         margin_width == other.margin_width &&
         margin_height == other.margin_height &&
         allow_fullscreen == other.allow_fullscreen &&
         allow_payment_request == other.allow_payment_request &&
         required_csp == other.required_csp &&
         std::equal(delegated_permissions.begin(), delegated_permissions.end(),
                    other.delegated_permissions.begin()) &&
         std::equal(allowed_features.begin(), allowed_features.end(),
                    other.allowed_features.begin());
}

}  // namespace content
