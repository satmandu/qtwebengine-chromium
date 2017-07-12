// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/frame/BrowserControls.h"

#include <algorithm>  // for std::min and std::max

#include "core/frame/VisualViewport.h"
#include "core/page/ChromeClient.h"
#include "core/page/Page.h"
#include "platform/geometry/FloatSize.h"

namespace blink {

BrowserControls::BrowserControls(const Page& page)
    : page_(&page),
      height_(0),
      shown_ratio_(0),
      baseline_content_offset_(0),
      accumulated_scroll_delta_(0),
      shrink_viewport_(false),
      permitted_state_(kWebBrowserControlsBoth) {}

DEFINE_TRACE(BrowserControls) {
  visitor->Trace(page_);
}

void BrowserControls::ScrollBegin() {
  ResetBaseline();
}

FloatSize BrowserControls::ScrollBy(FloatSize pending_delta) {
  if ((permitted_state_ == kWebBrowserControlsShown &&
       pending_delta.Height() > 0) ||
      (permitted_state_ == kWebBrowserControlsHidden &&
       pending_delta.Height() < 0))
    return pending_delta;

  if (height_ == 0)
    return pending_delta;

  float old_offset = ContentOffset();
  float page_scale = page_->GetVisualViewport().Scale();

  // Update accumulated vertical scroll and apply it to browser controls
  // Compute scroll delta in viewport space by applying page scale
  accumulated_scroll_delta_ += pending_delta.Height() * page_scale;

  float new_content_offset =
      baseline_content_offset_ - accumulated_scroll_delta_;

  SetShownRatio(new_content_offset / height_);

  // Reset baseline when controls are fully visible
  if (shown_ratio_ == 1)
    ResetBaseline();

  // Clamp and use the expected content offset so that we don't return
  // spurrious remaining scrolls due to the imprecision of the shownRatio.
  new_content_offset = std::min(new_content_offset, height_);
  new_content_offset = std::max(new_content_offset, 0.f);

  // We negate the difference because scrolling down (positive delta) causes
  // browser controls to hide (negative offset difference).
  FloatSize applied_delta(0, (old_offset - new_content_offset) / page_scale);
  return pending_delta - applied_delta;
}

void BrowserControls::ResetBaseline() {
  accumulated_scroll_delta_ = 0;
  baseline_content_offset_ = ContentOffset();
}

float BrowserControls::LayoutHeight() {
  return shrink_viewport_ ? height_ : 0;
}

float BrowserControls::ContentOffset() {
  return shown_ratio_ * height_;
}

void BrowserControls::SetShownRatio(float shown_ratio) {
  shown_ratio = std::min(shown_ratio, 1.f);
  shown_ratio = std::max(shown_ratio, 0.f);

  if (shown_ratio_ == shown_ratio)
    return;

  shown_ratio_ = shown_ratio;
  page_->GetChromeClient().DidUpdateBrowserControls();
}

void BrowserControls::UpdateConstraintsAndState(
    WebBrowserControlsState constraints,
    WebBrowserControlsState current,
    bool animate) {
  permitted_state_ = constraints;

  DCHECK(!(constraints == kWebBrowserControlsShown &&
           current == kWebBrowserControlsHidden));
  DCHECK(!(constraints == kWebBrowserControlsHidden &&
           current == kWebBrowserControlsShown));

  // If the change should be animated, let the impl thread drive the change.
  // Otherwise, immediately set the shown ratio so we don't have to wait for
  // a commit from the impl thread.
  if (animate)
    return;

  if (constraints == kWebBrowserControlsBoth &&
      current == kWebBrowserControlsBoth)
    return;

  if (constraints == kWebBrowserControlsHidden ||
      current == kWebBrowserControlsHidden)
    SetShownRatio(0.f);
  else
    SetShownRatio(1.f);
}

void BrowserControls::SetHeight(float height, bool shrink_viewport) {
  if (height_ == height && shrink_viewport_ == shrink_viewport)
    return;

  height_ = height;
  shrink_viewport_ = shrink_viewport;
  page_->GetChromeClient().DidUpdateBrowserControls();
}

}  // namespace blink
