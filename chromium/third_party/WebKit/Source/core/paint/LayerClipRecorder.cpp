// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/paint/LayerClipRecorder.h"

#include "core/layout/LayoutView.h"
#include "core/paint/ClipRect.h"
#include "core/paint/PaintLayer.h"
#include "platform/geometry/IntRect.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/graphics/paint/ClipRecorder.h"
#include "platform/graphics/paint/PaintController.h"

namespace blink {

LayerClipRecorder::LayerClipRecorder(GraphicsContext& graphics_context,
                                     const LayoutBoxModelObject& layout_object,
                                     DisplayItem::Type clip_type,
                                     const ClipRect& clip_rect,
                                     const PaintLayer* clip_root,
                                     const LayoutPoint& fragment_offset,
                                     PaintLayerFlags paint_flags,
                                     BorderRadiusClippingRule rule)
    : graphics_context_(graphics_context),
      layout_object_(layout_object),
      clip_type_(clip_type) {
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled())
    return;
  IntRect snapped_clip_rect = PixelSnappedIntRect(clip_rect.Rect());
  Vector<FloatRoundedRect> rounded_rects;
  if (clip_root && clip_rect.HasRadius()) {
    CollectRoundedRectClips(*layout_object.Layer(), clip_root, graphics_context,
                            fragment_offset, paint_flags, rule, rounded_rects);
  }

  graphics_context_.GetPaintController().CreateAndAppend<ClipDisplayItem>(
      layout_object, clip_type_, snapped_clip_rect, rounded_rects);
}

static bool InContainingBlockChain(PaintLayer* start_layer,
                                   PaintLayer* end_layer) {
  if (start_layer == end_layer)
    return true;

  LayoutView* view = start_layer->GetLayoutObject().View();
  for (const LayoutBlock* current_block =
           start_layer->GetLayoutObject().ContainingBlock();
       current_block && current_block != view;
       current_block = current_block->ContainingBlock()) {
    if (current_block->Layer() == end_layer)
      return true;
  }

  return false;
}

void LayerClipRecorder::CollectRoundedRectClips(
    PaintLayer& paint_layer,
    const PaintLayer* clip_root,
    GraphicsContext& context,
    const LayoutPoint& fragment_offset,
    PaintLayerFlags paint_flags,
    BorderRadiusClippingRule rule,
    Vector<FloatRoundedRect>& rounded_rect_clips) {
  // If the clip rect has been tainted by a border radius, then we have to walk
  // up our layer chain applying the clips from any layers with overflow. The
  // condition for being able to apply these clips is that the overflow object
  // be in our containing block chain so we check that also.
  for (PaintLayer* layer = rule == kIncludeSelfForBorderRadius
                               ? &paint_layer
                               : paint_layer.Parent();
       layer; layer = layer->Parent()) {
    // Composited scrolling layers handle border-radius clip in the compositor
    // via a mask layer. We do not want to apply a border-radius clip to the
    // layer contents itself, because that would require re-rastering every
    // frame to update the clip. We only want to make sure that the mask layer
    // is properly clipped so that it can in turn clip the scrolled contents in
    // the compositor.
    if (layer->NeedsCompositedScrolling() &&
        !(paint_flags & kPaintLayerPaintingChildClippingMaskPhase ||
          paint_flags & kPaintLayerPaintingAncestorClippingMaskPhase))
      break;

    if (layer->GetLayoutObject().HasOverflowClip() &&
        layer->GetLayoutObject().Style()->HasBorderRadius() &&
        InContainingBlockChain(&paint_layer, layer)) {
      LayoutPoint delta(fragment_offset);
      layer->ConvertToLayerCoords(clip_root, delta);

      // The PaintLayer's size is pixel-snapped if it is a LayoutBox. We can't
      // use a pre-snapped border rect for clipping, since
      // getRoundedInnerBorderFor assumes it has not been snapped yet.
      LayoutSize size(layer->GetLayoutBox()
                          ? ToLayoutBox(layer->GetLayoutObject()).Size()
                          : LayoutSize(layer->size()));
      rounded_rect_clips.push_back(
          layer->GetLayoutObject().Style()->GetRoundedInnerBorderFor(
              LayoutRect(delta, size)));
    }

    if (layer == clip_root)
      break;
  }
}

LayerClipRecorder::~LayerClipRecorder() {
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled())
    return;
  graphics_context_.GetPaintController().EndItem<EndClipDisplayItem>(
      layout_object_, DisplayItem::ClipTypeToEndClipType(clip_type_));
}

}  // namespace blink
