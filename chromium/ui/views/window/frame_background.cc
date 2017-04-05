// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/window/frame_background.h"

#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/theme_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/view.h"

namespace views {

FrameBackground::FrameBackground()
    : frame_color_(0),
      use_custom_frame_(true),
      is_active_(true),
      incognito_(false),
      top_area_height_(0),
      left_edge_(nullptr),
      top_edge_(nullptr),
      right_edge_(nullptr),
      bottom_edge_(nullptr),
      top_left_corner_(nullptr),
      top_right_corner_(nullptr),
      bottom_left_corner_(nullptr),
      bottom_right_corner_(nullptr),
      maximized_top_inset_(0) {}

FrameBackground::~FrameBackground() {
}

void FrameBackground::SetSideImages(const gfx::ImageSkia* left,
                                    const gfx::ImageSkia* top,
                                    const gfx::ImageSkia* right,
                                    const gfx::ImageSkia* bottom) {
  left_edge_ = left;
  top_edge_ = top;
  right_edge_ = right;
  bottom_edge_ = bottom;
}

void FrameBackground::SetCornerImages(const gfx::ImageSkia* top_left,
                                      const gfx::ImageSkia* top_right,
                                      const gfx::ImageSkia* bottom_left,
                                      const gfx::ImageSkia* bottom_right) {
  top_left_corner_ = top_left;
  top_right_corner_ = top_right;
  bottom_left_corner_ = bottom_left;
  bottom_right_corner_ = bottom_right;
}

void FrameBackground::PaintRestored(gfx::Canvas* canvas,
                                    const View* view) const {
  // Fill with the frame color first so we have a constant background for
  // areas not covered by the theme image.
  PaintFrameColor(canvas, view);

  // Draw the theme frame and overlay, if available.
  if (!theme_image_.isNull()) {
    canvas->TileImageInt(theme_image_, 0, 0, view->width(),
                         theme_image_.height());
  }
  if (!theme_overlay_image_.isNull())
    canvas->DrawImageInt(theme_overlay_image_, 0, 0);

  // Draw the top corners and edge, scaling the corner images down if they
  // are too big and relative to the vertical space available.
  int top_left_height =
      std::min(top_left_corner_->height(),
               view->height() - bottom_left_corner_->height());
  canvas->DrawImageInt(*top_left_corner_,
                       0, 0, top_left_corner_->width(), top_left_height,
                       0, 0, top_left_corner_->width(), top_left_height,
                       false);
  canvas->TileImageInt(*top_edge_,
      top_left_corner_->width(),
      0,
      view->width() - top_left_corner_->width() - top_right_corner_->width(),
      top_edge_->height());
  int top_right_height =
      std::min(top_right_corner_->height(),
               view->height() - bottom_right_corner_->height());
  canvas->DrawImageInt(*top_right_corner_,
                       0, 0,
                       top_right_corner_->width(), top_right_height,
                       view->width() - top_right_corner_->width(), 0,
                       top_right_corner_->width(), top_right_height,
                       false);

  // Right edge.
  int right_edge_height =
      view->height() - top_right_height - bottom_right_corner_->height();
  canvas->TileImageInt(*right_edge_,
                       view->width() - right_edge_->width(),
                       top_right_height,
                       right_edge_->width(),
                       right_edge_height);

  // Bottom corners and edge.
  canvas->DrawImageInt(*bottom_right_corner_,
                       view->width() - bottom_right_corner_->width(),
                       view->height() - bottom_right_corner_->height());
  canvas->TileImageInt(
      *bottom_edge_,
      bottom_left_corner_->width(),
      view->height() - bottom_edge_->height(),
      view->width() - bottom_left_corner_->width()
          - bottom_right_corner_->width(),
      bottom_edge_->height());
  canvas->DrawImageInt(*bottom_left_corner_, 0,
                       view->height() - bottom_left_corner_->height());

  // Left edge.
  int left_edge_height =
      view->height() - top_left_height - bottom_left_corner_->height();
  canvas->TileImageInt(*left_edge_,
                       0, top_left_height,
                       left_edge_->width(), left_edge_height);
}

void FrameBackground::PaintMaximized(gfx::Canvas* canvas,
                                     const View* view) const {
  // We will be painting from -|maximized_top_inset_| to
  // -|maximized_top_inset_| + |theme_image_|.height(). If this is less than
  // |top_area_height_|, we need to paint the frame color to fill in the area
  // beneath the image.
  int theme_frame_bottom = -maximized_top_inset_ +
                           (theme_image_.isNull() ? 0 : theme_image_.height());
  if (top_area_height_ > theme_frame_bottom)
    PaintFrameTopArea(canvas, view);

  // Draw the theme frame.
  if (!theme_image_.isNull()) {
    canvas->TileImageInt(theme_image_, 0, -maximized_top_inset_, view->width(),
                         theme_image_.height());
  }
  // Draw the theme frame overlay, if available.
  if (!theme_overlay_image_.isNull())
    canvas->DrawImageInt(theme_overlay_image_, 0, -maximized_top_inset_);
}

void FrameBackground::PaintFrameColor(gfx::Canvas* canvas,
                                      const View* view) const {
  PaintFrameTopArea(canvas, view);

  // If the window is very short, we're done.
  int remaining_height = view->height() - top_area_height_;
  if (remaining_height <= 0)
    return;

  // Fill down the sides.
  canvas->FillRect(gfx::Rect(0, top_area_height_, left_edge_->width(),
                             remaining_height), frame_color_);
  canvas->FillRect(gfx::Rect(view->width() - right_edge_->width(),
                             top_area_height_, right_edge_->width(),
                             remaining_height), frame_color_);

  // If the window is very narrow, we're done.
  int center_width =
      view->width() - left_edge_->width() - right_edge_->width();
  if (center_width <= 0)
    return;

  // Fill the bottom area.
  canvas->FillRect(gfx::Rect(left_edge_->width(),
                             view->height() - bottom_edge_->height(),
                             center_width, bottom_edge_->height()),
                             frame_color_);
}

void FrameBackground::PaintFrameTopArea(gfx::Canvas* canvas,
                                        const View* view) const {
#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
  auto* native_theme = view->GetNativeTheme();
  ui::NativeTheme::ExtraParams params;
  params.frame_top_area.use_custom_frame = use_custom_frame_;
  params.frame_top_area.is_active = is_active_;
  params.frame_top_area.incognito = incognito_;
  params.frame_top_area.default_background_color = frame_color_;
  native_theme->Paint(canvas->sk_canvas(), ui::NativeTheme::kFrameTopArea,
                      ui::NativeTheme::kNormal,
                      gfx::Rect(0, 0, view->width(), top_area_height_), params);
#else
  canvas->FillRect(gfx::Rect(0, 0, view->width(), top_area_height_),
                   frame_color_);
#endif
}

}  // namespace views
