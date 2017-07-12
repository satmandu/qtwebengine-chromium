// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/trees/layer_tree_host_common.h"

#include <stddef.h>

#include <algorithm>

#include "base/containers/adapters.h"
#include "base/trace_event/trace_event.h"
#include "cc/base/math_util.h"
#include "cc/layers/heads_up_display_layer_impl.h"
#include "cc/layers/layer.h"
#include "cc/layers/layer_impl.h"
#include "cc/trees/draw_property_utils.h"
#include "cc/trees/effect_node.h"
#include "cc/trees/layer_tree_host.h"
#include "cc/trees/layer_tree_impl.h"
#include "cc/trees/property_tree_builder.h"
#include "cc/trees/scroll_node.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/vector2d_conversions.h"
#include "ui/gfx/transform.h"
#include "ui/gfx/transform_util.h"

namespace cc {

LayerTreeHostCommon::CalcDrawPropsMainInputsForTesting::
    CalcDrawPropsMainInputsForTesting(Layer* root_layer,
                                      const gfx::Size& device_viewport_size,
                                      const gfx::Transform& device_transform,
                                      float device_scale_factor,
                                      float page_scale_factor,
                                      const Layer* page_scale_layer,
                                      const Layer* inner_viewport_scroll_layer,
                                      const Layer* outer_viewport_scroll_layer)
    : root_layer(root_layer),
      device_viewport_size(device_viewport_size),
      device_transform(device_transform),
      device_scale_factor(device_scale_factor),
      page_scale_factor(page_scale_factor),
      page_scale_layer(page_scale_layer),
      inner_viewport_scroll_layer(inner_viewport_scroll_layer),
      outer_viewport_scroll_layer(outer_viewport_scroll_layer) {}

LayerTreeHostCommon::CalcDrawPropsMainInputsForTesting::
    CalcDrawPropsMainInputsForTesting(Layer* root_layer,
                                      const gfx::Size& device_viewport_size,
                                      const gfx::Transform& device_transform)
    : CalcDrawPropsMainInputsForTesting(root_layer,
                                        device_viewport_size,
                                        device_transform,
                                        1.f,
                                        1.f,
                                        NULL,
                                        NULL,
                                        NULL) {}

LayerTreeHostCommon::CalcDrawPropsMainInputsForTesting::
    CalcDrawPropsMainInputsForTesting(Layer* root_layer,
                                      const gfx::Size& device_viewport_size)
    : CalcDrawPropsMainInputsForTesting(root_layer,
                                        device_viewport_size,
                                        gfx::Transform()) {}

LayerTreeHostCommon::CalcDrawPropsImplInputs::CalcDrawPropsImplInputs(
    LayerImpl* root_layer,
    const gfx::Size& device_viewport_size,
    const gfx::Transform& device_transform,
    float device_scale_factor,
    float page_scale_factor,
    const LayerImpl* page_scale_layer,
    const LayerImpl* inner_viewport_scroll_layer,
    const LayerImpl* outer_viewport_scroll_layer,
    const gfx::Vector2dF& elastic_overscroll,
    const LayerImpl* elastic_overscroll_application_layer,
    int max_texture_size,
    bool can_render_to_separate_surface,
    bool can_adjust_raster_scales,
    bool use_layer_lists,
    LayerImplList* render_surface_layer_list,
    PropertyTrees* property_trees)
    : root_layer(root_layer),
      device_viewport_size(device_viewport_size),
      device_transform(device_transform),
      device_scale_factor(device_scale_factor),
      page_scale_factor(page_scale_factor),
      page_scale_layer(page_scale_layer),
      inner_viewport_scroll_layer(inner_viewport_scroll_layer),
      outer_viewport_scroll_layer(outer_viewport_scroll_layer),
      elastic_overscroll(elastic_overscroll),
      elastic_overscroll_application_layer(
          elastic_overscroll_application_layer),
      max_texture_size(max_texture_size),
      can_render_to_separate_surface(can_render_to_separate_surface),
      can_adjust_raster_scales(can_adjust_raster_scales),
      use_layer_lists(use_layer_lists),
      render_surface_layer_list(render_surface_layer_list),
      property_trees(property_trees) {}

LayerTreeHostCommon::CalcDrawPropsImplInputsForTesting::
    CalcDrawPropsImplInputsForTesting(LayerImpl* root_layer,
                                      const gfx::Size& device_viewport_size,
                                      const gfx::Transform& device_transform,
                                      float device_scale_factor,
                                      LayerImplList* render_surface_layer_list)
    : CalcDrawPropsImplInputs(root_layer,
                              device_viewport_size,
                              device_transform,
                              device_scale_factor,
                              1.f,
                              NULL,
                              NULL,
                              NULL,
                              gfx::Vector2dF(),
                              NULL,
                              std::numeric_limits<int>::max() / 2,
                              true,
                              false,
                              false,
                              render_surface_layer_list,
                              GetPropertyTrees(root_layer)) {
  DCHECK(root_layer);
  DCHECK(render_surface_layer_list);
}

LayerTreeHostCommon::CalcDrawPropsImplInputsForTesting::
    CalcDrawPropsImplInputsForTesting(LayerImpl* root_layer,
                                      const gfx::Size& device_viewport_size,
                                      const gfx::Transform& device_transform,
                                      LayerImplList* render_surface_layer_list)
    : CalcDrawPropsImplInputsForTesting(root_layer,
                                        device_viewport_size,
                                        device_transform,
                                        1.f,
                                        render_surface_layer_list) {}

LayerTreeHostCommon::CalcDrawPropsImplInputsForTesting::
    CalcDrawPropsImplInputsForTesting(LayerImpl* root_layer,
                                      const gfx::Size& device_viewport_size,
                                      LayerImplList* render_surface_layer_list)
    : CalcDrawPropsImplInputsForTesting(root_layer,
                                        device_viewport_size,
                                        gfx::Transform(),
                                        1.f,
                                        render_surface_layer_list) {}

LayerTreeHostCommon::CalcDrawPropsImplInputsForTesting::
    CalcDrawPropsImplInputsForTesting(LayerImpl* root_layer,
                                      const gfx::Size& device_viewport_size,
                                      float device_scale_factor,
                                      LayerImplList* render_surface_layer_list)
    : CalcDrawPropsImplInputsForTesting(root_layer,
                                        device_viewport_size,
                                        gfx::Transform(),
                                        device_scale_factor,
                                        render_surface_layer_list) {}

LayerTreeHostCommon::ScrollUpdateInfo::ScrollUpdateInfo()
    : layer_id(Layer::INVALID_ID) {}

bool LayerTreeHostCommon::ScrollUpdateInfo::operator==(
    const LayerTreeHostCommon::ScrollUpdateInfo& other) const {
  return layer_id == other.layer_id && scroll_delta == other.scroll_delta;
}

LayerTreeHostCommon::ScrollbarsUpdateInfo::ScrollbarsUpdateInfo()
    : layer_id(Layer::INVALID_ID), hidden(true) {}

LayerTreeHostCommon::ScrollbarsUpdateInfo::ScrollbarsUpdateInfo(int layer_id,
                                                                bool hidden)
    : layer_id(layer_id), hidden(hidden) {}

bool LayerTreeHostCommon::ScrollbarsUpdateInfo::operator==(
    const LayerTreeHostCommon::ScrollbarsUpdateInfo& other) const {
  return layer_id == other.layer_id && hidden == other.hidden;
}

ScrollAndScaleSet::ScrollAndScaleSet()
    : page_scale_delta(1.f),
      top_controls_delta(0.f),
      has_scrolled_by_wheel(false),
      has_scrolled_by_touch(false) {}

ScrollAndScaleSet::~ScrollAndScaleSet() {}

static inline void SetMaskLayersAreDrawnRenderSurfaceLayerListMembers(
    RenderSurfaceImpl* surface,
    PropertyTrees* property_trees) {
  LayerImpl* mask_layer = surface->MaskLayer();
  if (mask_layer) {
    mask_layer->set_is_drawn_render_surface_layer_list_member(true);
    draw_property_utils::ComputeMaskDrawProperties(mask_layer, property_trees);
  }
}

static inline void ClearMaskLayersAreDrawnRenderSurfaceLayerListMembers(
    RenderSurfaceImpl* surface) {
  LayerImpl* mask_layer = surface->MaskLayer();
  if (mask_layer)
    mask_layer->set_is_drawn_render_surface_layer_list_member(false);
}

static inline void ClearIsDrawnRenderSurfaceLayerListMember(
    LayerImplList* layer_list,
    ScrollTree* scroll_tree) {
  for (LayerImpl* layer : *layer_list)
    layer->set_is_drawn_render_surface_layer_list_member(false);
}

static bool CdpPerfTracingEnabled() {
  bool tracing_enabled;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED("cdp.perf", &tracing_enabled);
  return tracing_enabled;
}

static float TranslationFromActiveTreeLayerScreenSpaceTransform(
    LayerImpl* pending_tree_layer) {
  LayerTreeImpl* layer_tree_impl = pending_tree_layer->layer_tree_impl();
  if (layer_tree_impl) {
    LayerImpl* active_tree_layer =
        layer_tree_impl->FindActiveTreeLayerById(pending_tree_layer->id());
    if (active_tree_layer) {
      gfx::Transform active_tree_screen_space_transform =
          active_tree_layer->draw_properties().screen_space_transform;
      if (active_tree_screen_space_transform.IsIdentity())
        return 0.f;
      if (active_tree_screen_space_transform.ApproximatelyEqual(
              pending_tree_layer->draw_properties().screen_space_transform))
        return 0.f;
      return (active_tree_layer->draw_properties()
                  .screen_space_transform.To2dTranslation() -
              pending_tree_layer->draw_properties()
                  .screen_space_transform.To2dTranslation())
          .Length();
    }
  }
  return 0.f;
}

// A layer jitters if its screen space transform is same on two successive
// commits, but has changed in between the commits. CalculateLayerJitter
// computes the jitter for the layer.
int LayerTreeHostCommon::CalculateLayerJitter(LayerImpl* layer) {
  float jitter = 0.f;
  layer->performance_properties().translation_from_last_frame = 0.f;
  layer->performance_properties().last_commit_screen_space_transform =
      layer->draw_properties().screen_space_transform;

  if (!layer->visible_layer_rect().IsEmpty()) {
    if (layer->draw_properties().screen_space_transform.ApproximatelyEqual(
            layer->performance_properties()
                .last_commit_screen_space_transform)) {
      float translation_from_last_commit =
          TranslationFromActiveTreeLayerScreenSpaceTransform(layer);
      if (translation_from_last_commit > 0.f) {
        layer->performance_properties().num_fixed_point_hits++;
        layer->performance_properties().translation_from_last_frame =
            translation_from_last_commit;
        if (layer->performance_properties().num_fixed_point_hits >
            layer->layer_tree_impl()->kFixedPointHitsThreshold) {
          // Jitter = Translation from fixed point * sqrt(Area of the layer).
          // The square root of the area is used instead of the area to match
          // the dimensions of both terms on the rhs.
          jitter += translation_from_last_commit *
                    sqrt(layer->visible_layer_rect().size().GetArea());
        }
      } else {
        layer->performance_properties().num_fixed_point_hits = 0;
      }
    }
  }
  return jitter;
}

enum PropertyTreeOption {
  BUILD_PROPERTY_TREES_IF_NEEDED,
  DONT_BUILD_PROPERTY_TREES
};

static void ComputeInitialRenderSurfaceLayerList(
    LayerTreeImpl* layer_tree_impl,
    PropertyTrees* property_trees,
    LayerImplList* render_surface_layer_list,
    bool can_render_to_separate_surface,
    bool use_layer_lists) {
  // Add all non-skipped surfaces to the initial render surface layer list. Add
  // all non-skipped layers to the layer list of their target surface, and
  // add their content rect to their target surface's accumulated content rect.
  for (LayerImpl* layer : *layer_tree_impl) {
    DCHECK(layer);

    // TODO(crbug.com/726423): LayerImpls should never have invalid PropertyTree
    // indices.
    if (!layer)
      continue;

    layer->set_is_drawn_render_surface_layer_list_member(false);
    if (!layer->HasValidPropertyTreeIndices())
      continue;

    RenderSurfaceImpl* render_surface = layer->GetRenderSurface();
    if (render_surface) {
      render_surface->ClearLayerLists();
      ClearMaskLayersAreDrawnRenderSurfaceLayerListMembers(render_surface);
    }
    layer->set_is_drawn_render_surface_layer_list_member(false);

    bool is_root = layer_tree_impl->IsRootLayer(layer);
    bool skip_layer = !is_root && draw_property_utils::LayerShouldBeSkipped(
                                      layer, property_trees->transform_tree,
                                      property_trees->effect_tree);
    if (skip_layer)
      continue;

    bool render_to_separate_surface =
        is_root || (can_render_to_separate_surface && render_surface);

    if (render_to_separate_surface) {
      DCHECK(render_surface);
      DCHECK(layer->render_target() == render_surface);
      render_surface->ClearAccumulatedContentRect();
      render_surface_layer_list->push_back(layer);
      if (is_root) {
        // The root surface does not contribute to any other surface, it has no
        // target.
        render_surface->set_contributes_to_drawn_surface(false);
      } else {
        render_surface->render_target()->layer_list().push_back(layer);
        bool contributes_to_drawn_surface =
            property_trees->effect_tree.ContributesToDrawnSurface(
                layer->effect_tree_index());
        render_surface->set_contributes_to_drawn_surface(
            contributes_to_drawn_surface);
      }

      draw_property_utils::ComputeSurfaceDrawProperties(
          property_trees, render_surface, use_layer_lists);

      // Ignore occlusion from outside the surface when surface contents need to
      // be fully drawn. Layers with copy-request need to be complete.  We could
      // be smarter about layers with filters that move pixels and exclude
      // regions where both layers and the filters are occluded, but this seems
      // like overkill.
      // TODO(senorblanco): make this smarter for the SkImageFilter case (check
      // for pixel-moving filters)
      const FilterOperations& filters = render_surface->Filters();
      bool is_occlusion_immune = render_surface->HasCopyRequest() ||
                                 filters.HasReferenceFilter() ||
                                 filters.HasFilterThatMovesPixels();
      if (is_occlusion_immune) {
        render_surface->SetNearestOcclusionImmuneAncestor(render_surface);
      } else if (is_root) {
        render_surface->SetNearestOcclusionImmuneAncestor(nullptr);
      } else {
        render_surface->SetNearestOcclusionImmuneAncestor(
            render_surface->render_target()
                ->nearest_occlusion_immune_ancestor());
      }
    }
    bool layer_is_drawn =
        property_trees->effect_tree.Node(layer->effect_tree_index())->is_drawn;
    bool layer_should_be_drawn = draw_property_utils::LayerNeedsUpdate(
        layer, layer_is_drawn, property_trees);
    if (!layer_should_be_drawn)
      continue;

    layer->set_is_drawn_render_surface_layer_list_member(true);
    layer->render_target()->layer_list().push_back(layer);

    // The layer contributes its drawable content rect to its render target.
    layer->render_target()->AccumulateContentRectFromContributingLayer(layer);
  }
}

static void ComputeSurfaceContentRects(LayerTreeImpl* layer_tree_impl,
                                       PropertyTrees* property_trees,
                                       LayerImplList* render_surface_layer_list,
                                       int max_texture_size) {
  // Walk the list backwards, accumulating each surface's content rect into its
  // target's content rect.
  for (LayerImpl* layer : base::Reversed(*render_surface_layer_list)) {
    RenderSurfaceImpl* render_surface = layer->GetRenderSurface();
    if (layer_tree_impl->IsRootLayer(layer)) {
      // The root layer's surface content rect is always the entire viewport.
      render_surface->SetContentRectToViewport();
      continue;
    }

    // Now all contributing drawable content rect has been accumulated to this
    // render surface, calculate the content rect.
    render_surface->CalculateContentRectFromAccumulatedContentRect(
        max_texture_size);

    // Now the render surface's content rect is calculated correctly, it could
    // contribute to its render target.
    render_surface->render_target()
        ->AccumulateContentRectFromContributingRenderSurface(render_surface);
  }
}

static void ComputeListOfNonEmptySurfaces(LayerTreeImpl* layer_tree_impl,
                                          PropertyTrees* property_trees,
                                          LayerImplList* initial_surface_list,
                                          LayerImplList* final_surface_list) {
  // Walk the initial surface list forwards. The root surface and each
  // surface with a non-empty content rect go into the final render surface
  // layer list. Surfaces with empty content rects or whose target isn't in
  // the final list do not get added to the final list.
  for (LayerImpl* layer : *initial_surface_list) {
    bool is_root = layer_tree_impl->IsRootLayer(layer);
    RenderSurfaceImpl* surface = layer->GetRenderSurface();
    RenderSurfaceImpl* target_surface = surface->render_target();
    if (!is_root && (surface->content_rect().IsEmpty() ||
                     target_surface->layer_list().empty())) {
      ClearIsDrawnRenderSurfaceLayerListMember(&surface->layer_list(),
                                               &property_trees->scroll_tree);
      surface->ClearLayerLists();
      if (!is_root) {
        LayerImplList& target_list = target_surface->layer_list();
        auto it = std::find(target_list.begin(), target_list.end(), layer);
        if (it != target_list.end()) {
          target_list.erase(it);
          // This surface has an empty content rect. If its target's layer list
          // had no other layers, then its target would also have had an empty
          // content rect, meaning it would have been removed and had its layer
          // list cleared when we visited it, unless the target surface is the
          // root surface.
          DCHECK(!target_surface->layer_list().empty() ||
                 target_surface->render_target() == target_surface);
        } else {
          // This layer was removed when the target itself was cleared.
          DCHECK(target_surface->layer_list().empty());
        }
      }
      continue;
    }
    SetMaskLayersAreDrawnRenderSurfaceLayerListMembers(surface, property_trees);
    final_surface_list->push_back(layer);
  }
}

static void CalculateRenderSurfaceLayerList(
    LayerTreeImpl* layer_tree_impl,
    PropertyTrees* property_trees,
    LayerImplList* render_surface_layer_list,
    const bool can_render_to_separate_surface,
    const bool use_layer_lists,
    const int max_texture_size) {
  // This calculates top level Render Surface Layer List, and Layer List for all
  // Render Surfaces.
  // |render_surface_layer_list| is the top level RenderSurfaceLayerList.

  LayerImplList initial_render_surface_list;

  // First compute an RSLL that might include surfaces that later turn out to
  // have an empty content rect. After surface content rects are computed,
  // produce a final RSLL that omits empty surfaces.
  ComputeInitialRenderSurfaceLayerList(
      layer_tree_impl, property_trees, &initial_render_surface_list,
      can_render_to_separate_surface, use_layer_lists);
  ComputeSurfaceContentRects(layer_tree_impl, property_trees,
                             &initial_render_surface_list, max_texture_size);
  ComputeListOfNonEmptySurfaces(layer_tree_impl, property_trees,
                                &initial_render_surface_list,
                                render_surface_layer_list);
}

void CalculateDrawPropertiesInternal(
    LayerTreeHostCommon::CalcDrawPropsImplInputs* inputs,
    PropertyTreeOption property_tree_option) {
  inputs->render_surface_layer_list->clear();

  const bool should_measure_property_tree_performance =
      property_tree_option == BUILD_PROPERTY_TREES_IF_NEEDED;

  LayerImplList visible_layer_list;
  switch (property_tree_option) {
    case BUILD_PROPERTY_TREES_IF_NEEDED: {
      // The translation from layer to property trees is an intermediate
      // state. We will eventually get these data passed directly to the
      // compositor.
      if (should_measure_property_tree_performance) {
        TRACE_EVENT_BEGIN0(
            TRACE_DISABLED_BY_DEFAULT("cc.debug.cdp-perf"),
            "LayerTreeHostCommon::ComputeVisibleRectsWithPropertyTrees");
      }

      PropertyTreeBuilder::BuildPropertyTrees(
          inputs->root_layer, inputs->page_scale_layer,
          inputs->inner_viewport_scroll_layer,
          inputs->outer_viewport_scroll_layer,
          inputs->elastic_overscroll_application_layer,
          inputs->elastic_overscroll, inputs->page_scale_factor,
          inputs->device_scale_factor, gfx::Rect(inputs->device_viewport_size),
          inputs->device_transform, inputs->property_trees);
      draw_property_utils::UpdatePropertyTreesAndRenderSurfaces(
          inputs->root_layer, inputs->property_trees,
          inputs->can_render_to_separate_surface,
          inputs->can_adjust_raster_scales);

      // Property trees are normally constructed on the main thread and
      // passed to compositor thread. Source to parent updates on them are not
      // allowed in the compositor thread. Some tests build them on the
      // compositor thread, so we need to explicitly disallow source to parent
      // updates when they are built on compositor thread.
      inputs->property_trees->transform_tree
          .set_source_to_parent_updates_allowed(false);
      if (should_measure_property_tree_performance) {
        TRACE_EVENT_END0(
            TRACE_DISABLED_BY_DEFAULT("cc.debug.cdp-perf"),
            "LayerTreeHostCommon::ComputeVisibleRectsWithPropertyTrees");
      }

      break;
    }
    case DONT_BUILD_PROPERTY_TREES: {
      TRACE_EVENT0(
          TRACE_DISABLED_BY_DEFAULT("cc.debug.cdp-perf"),
          "LayerTreeHostCommon::ComputeJustVisibleRectsWithPropertyTrees");
      // Since page scale and elastic overscroll are SyncedProperties, changes
      // on the active tree immediately affect the pending tree, so instead of
      // trying to update property trees whenever these values change, we
      // update property trees before using them.
      draw_property_utils::UpdatePageScaleFactor(
          inputs->property_trees, inputs->page_scale_layer,
          inputs->page_scale_factor, inputs->device_scale_factor,
          inputs->device_transform);
      draw_property_utils::UpdateElasticOverscroll(
          inputs->property_trees, inputs->elastic_overscroll_application_layer,
          inputs->elastic_overscroll);
      // Similarly, the device viewport and device transform are shared
      // by both trees.
      PropertyTrees* property_trees = inputs->property_trees;
      property_trees->clip_tree.SetViewportClip(
          gfx::RectF(gfx::SizeF(inputs->device_viewport_size)));
      float page_scale_factor_for_root =
          inputs->page_scale_layer == inputs->root_layer
              ? inputs->page_scale_factor
              : 1.f;
      property_trees->transform_tree.SetRootTransformsAndScales(
          inputs->device_scale_factor, page_scale_factor_for_root,
          inputs->device_transform, inputs->root_layer->position());
      draw_property_utils::UpdatePropertyTreesAndRenderSurfaces(
          inputs->root_layer, inputs->property_trees,
          inputs->can_render_to_separate_surface,
          inputs->can_adjust_raster_scales);
      break;
    }
  }

  if (should_measure_property_tree_performance) {
    TRACE_EVENT_BEGIN0(TRACE_DISABLED_BY_DEFAULT("cc.debug.cdp-perf"),
                       "LayerTreeHostCommon::CalculateDrawProperties");
  }

  draw_property_utils::FindLayersThatNeedUpdates(
      inputs->root_layer->layer_tree_impl(), inputs->property_trees,
      &visible_layer_list);
  DCHECK(inputs->can_render_to_separate_surface ==
         inputs->property_trees->non_root_surfaces_enabled);
  draw_property_utils::ComputeDrawPropertiesOfVisibleLayers(
      &visible_layer_list, inputs->property_trees);

  CalculateRenderSurfaceLayerList(
      inputs->root_layer->layer_tree_impl(), inputs->property_trees,
      inputs->render_surface_layer_list, inputs->can_render_to_separate_surface,
      inputs->use_layer_lists, inputs->max_texture_size);

  if (should_measure_property_tree_performance) {
    TRACE_EVENT_END0(TRACE_DISABLED_BY_DEFAULT("cc.debug.cdp-perf"),
                     "LayerTreeHostCommon::CalculateDrawProperties");
  }

  // A root layer render_surface should always exist after
  // CalculateDrawProperties.
  DCHECK(inputs->root_layer->GetRenderSurface());
}

void LayerTreeHostCommon::CalculateDrawPropertiesForTesting(
    CalcDrawPropsMainInputsForTesting* inputs) {
  LayerList update_layer_list;
  bool can_render_to_separate_surface = true;
  PropertyTrees* property_trees =
      inputs->root_layer->layer_tree_host()->property_trees();
  Layer* overscroll_elasticity_layer = nullptr;
  gfx::Vector2dF elastic_overscroll;
  PropertyTreeBuilder::BuildPropertyTrees(
      inputs->root_layer, inputs->page_scale_layer,
      inputs->inner_viewport_scroll_layer, inputs->outer_viewport_scroll_layer,
      overscroll_elasticity_layer, elastic_overscroll,
      inputs->page_scale_factor, inputs->device_scale_factor,
      gfx::Rect(inputs->device_viewport_size), inputs->device_transform,
      property_trees);
  draw_property_utils::UpdatePropertyTrees(
      inputs->root_layer->layer_tree_host(), property_trees,
      can_render_to_separate_surface);
  draw_property_utils::FindLayersThatNeedUpdates(
      inputs->root_layer->layer_tree_host(), property_trees,
      &update_layer_list);
}

void LayerTreeHostCommon::CalculateDrawProperties(
    CalcDrawPropsImplInputs* inputs) {
  CalculateDrawPropertiesInternal(inputs, DONT_BUILD_PROPERTY_TREES);

  if (CdpPerfTracingEnabled()) {
    LayerTreeImpl* layer_tree_impl = inputs->root_layer->layer_tree_impl();
    if (layer_tree_impl->IsPendingTree() &&
        layer_tree_impl->is_first_frame_after_commit()) {
      LayerImpl* active_tree_root =
          layer_tree_impl->FindActiveTreeLayerById(inputs->root_layer->id());
      float jitter = 0.f;
      if (active_tree_root) {
        int last_scrolled_node_index =
            active_tree_root->layer_tree_impl()->LastScrolledScrollNodeIndex();
        if (last_scrolled_node_index != ScrollTree::kInvalidNodeId) {
          std::unordered_set<int> jitter_nodes;
          for (auto* layer : *layer_tree_impl) {
            // Layers that have the same scroll tree index jitter together. So,
            // it is enough to calculate jitter on one of these layers. So,
            // after we find a jittering layer, we need not consider other
            // layers with the same scroll tree index.
            int scroll_tree_index = layer->scroll_tree_index();
            if (last_scrolled_node_index <= scroll_tree_index &&
                jitter_nodes.find(scroll_tree_index) == jitter_nodes.end()) {
              float layer_jitter = CalculateLayerJitter(layer);
              if (layer_jitter > 0.f) {
                jitter_nodes.insert(layer->scroll_tree_index());
                jitter += layer_jitter;
              }
            }
          }
        }
      }
      TRACE_EVENT_ASYNC_BEGIN1(
          "cdp.perf", "jitter",
          inputs->root_layer->layer_tree_impl()->source_frame_number(), "value",
          jitter);
      inputs->root_layer->layer_tree_impl()->set_is_first_frame_after_commit(
          false);
      TRACE_EVENT_ASYNC_END1(
          "cdp.perf", "jitter",
          inputs->root_layer->layer_tree_impl()->source_frame_number(), "value",
          jitter);
    }
  }
}

void LayerTreeHostCommon::CalculateDrawPropertiesForTesting(
    CalcDrawPropsImplInputsForTesting* inputs) {
  CalculateDrawPropertiesInternal(inputs, BUILD_PROPERTY_TREES_IF_NEEDED);
}

PropertyTrees* GetPropertyTrees(Layer* layer) {
  return layer->layer_tree_host()->property_trees();
}

PropertyTrees* GetPropertyTrees(LayerImpl* layer) {
  return layer->layer_tree_impl()->property_trees();
}

}  // namespace cc
