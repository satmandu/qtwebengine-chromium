// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_FRAME_HOST_RENDER_WIDGET_HOST_VIEW_CHILD_FRAME_H_
#define CONTENT_BROWSER_FRAME_HOST_RENDER_WIDGET_HOST_VIEW_CHILD_FRAME_H_

#include <stddef.h>
#include <stdint.h>

#include <deque>
#include <memory>
#include <vector>

#include "base/callback.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "build/build_config.h"
#include "cc/resources/returned_resource.h"
#include "cc/scheduler/begin_frame_source.h"
#include "cc/surfaces/compositor_frame_sink_support_client.h"
#include "cc/surfaces/surface_info.h"
#include "cc/surfaces/surface_sequence.h"
#include "content/browser/compositor/image_transport_factory.h"
#include "content/browser/renderer_host/event_with_latency_info.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/common/content_export.h"
#include "content/common/input/input_event_ack_state.h"
#include "content/public/browser/readback_types.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_widget_types.h"

namespace cc {
class CompositorFrameSinkSupport;
}

namespace content {
class CrossProcessFrameConnector;
class RenderWidgetHost;
class RenderWidgetHostImpl;
class RenderWidgetHostViewChildFrameTest;
class RenderWidgetHostViewGuestSurfaceTest;

// RenderWidgetHostViewChildFrame implements the view for a RenderWidgetHost
// associated with content being rendered in a separate process from
// content that is embedding it. This is not a platform-specific class; rather,
// the embedding renderer process implements the platform containing the
// widget, and the top-level frame's RenderWidgetHostView will ultimately
// manage all native widget interaction.
//
// See comments in render_widget_host_view.h about this class and its members.
class CONTENT_EXPORT RenderWidgetHostViewChildFrame
    : public RenderWidgetHostViewBase,
      public NON_EXPORTED_BASE(cc::CompositorFrameSinkSupportClient) {
 public:
  static RenderWidgetHostViewChildFrame* Create(RenderWidgetHost* widget);
  ~RenderWidgetHostViewChildFrame() override;

  void SetCrossProcessFrameConnector(
      CrossProcessFrameConnector* frame_connector);

  // This functions registers single-use callbacks that want to be notified when
  // the next frame is swapped. The callback is triggered by
  // ProcessCompositorFrame, which is the appropriate time to request pixel
  // readback for the frame that is about to be drawn. Once called, the callback
  // pointer is released.
  // TODO(wjmaclean): We should consider making this available in other view
  // types, such as RenderWidgetHostViewAura.
  void RegisterFrameSwappedCallback(std::unique_ptr<base::Closure> callback);

  // RenderWidgetHostView implementation.
  void InitAsChild(gfx::NativeView parent_view) override;
  RenderWidgetHost* GetRenderWidgetHost() const override;
  void SetSize(const gfx::Size& size) override;
  void SetBounds(const gfx::Rect& rect) override;
  void Focus() override;
  bool HasFocus() const override;
  bool IsSurfaceAvailableForCopy() const override;
  void CopyFromSurface(const gfx::Rect& src_rect,
                       const gfx::Size& output_size,
                       const ReadbackRequestCallback& callback,
                       const SkColorType color_type) override;
  void Show() override;
  void Hide() override;
  bool IsShowing() override;
  gfx::Rect GetViewBounds() const override;
  gfx::Size GetVisibleViewportSize() const override;
  gfx::Vector2dF GetLastScrollOffset() const override;
  gfx::NativeView GetNativeView() const override;
  gfx::NativeViewAccessible GetNativeViewAccessible() override;
  void SetBackgroundColor(SkColor color) override;
  SkColor background_color() const override;
  gfx::Size GetPhysicalBackingSize() const override;
  bool IsMouseLocked() override;
  void SetNeedsBeginFrames(bool needs_begin_frames) override;

  // RenderWidgetHostViewBase implementation.
  void InitAsPopup(RenderWidgetHostView* parent_host_view,
                   const gfx::Rect& bounds) override;
  void InitAsFullscreen(RenderWidgetHostView* reference_host_view) override;
  void UpdateCursor(const WebCursor& cursor) override;
  void SetIsLoading(bool is_loading) override;
  void RenderProcessGone(base::TerminationStatus status,
                         int error_code) override;
  void Destroy() override;
  void SetTooltipText(const base::string16& tooltip_text) override;
  bool HasAcceleratedSurface(const gfx::Size& desired_size) override;
  void GestureEventAck(const blink::WebGestureEvent& event,
                       InputEventAckState ack_result) override;
  void DidCreateNewRendererCompositorFrameSink(
      cc::mojom::MojoCompositorFrameSinkClient* renderer_compositor_frame_sink)
      override;
  void SubmitCompositorFrame(const cc::LocalSurfaceId& local_surface_id,
                             cc::CompositorFrame frame) override;
  void OnBeginFrameDidNotSwap(const cc::BeginFrameAck& ack) override;
  void OnSurfaceChanged(const cc::SurfaceInfo& surface_info) override;
  // Since the URL of content rendered by this class is not displayed in
  // the URL bar, this method does not need an implementation.
  void ClearCompositorFrame() override {}
  gfx::Rect GetBoundsInRootWindow() override;
  void ProcessAckedTouchEvent(const TouchEventWithLatencyInfo& touch,
                              InputEventAckState ack_result) override;
  bool LockMouse() override;
  void UnlockMouse() override;
  cc::FrameSinkId GetFrameSinkId() override;
  void ProcessKeyboardEvent(const NativeWebKeyboardEvent& event) override;
  void ProcessMouseEvent(const blink::WebMouseEvent& event,
                         const ui::LatencyInfo& latency) override;
  void ProcessMouseWheelEvent(const blink::WebMouseWheelEvent& event,
                              const ui::LatencyInfo& latency) override;
  void ProcessTouchEvent(const blink::WebTouchEvent& event,
                         const ui::LatencyInfo& latency) override;
  void ProcessGestureEvent(const blink::WebGestureEvent& event,
                           const ui::LatencyInfo& latency) override;
  gfx::Point TransformPointToRootCoordSpace(const gfx::Point& point) override;
  bool TransformPointToLocalCoordSpace(const gfx::Point& point,
                                       const cc::SurfaceId& original_surface,
                                       gfx::Point* transformed_point) override;
  bool TransformPointToCoordSpaceForView(
      const gfx::Point& point,
      RenderWidgetHostViewBase* target_view,
      gfx::Point* transformed_point) override;

  bool IsRenderWidgetHostViewChildFrame() override;

#if defined(OS_MACOSX)
  // RenderWidgetHostView implementation.
  ui::AcceleratedWidgetMac* GetAcceleratedWidgetMac() const override;
  void SetActive(bool active) override;
  void ShowDefinitionForSelection() override;
  bool SupportsSpeech() const override;
  void SpeakSelection() override;
  bool IsSpeaking() const override;
  void StopSpeaking() override;
#endif  // defined(OS_MACOSX)

  InputEventAckState FilterInputEvent(
      const blink::WebInputEvent& input_event) override;
  BrowserAccessibilityManager* CreateBrowserAccessibilityManager(
      BrowserAccessibilityDelegate* delegate, bool for_root_frame) override;

  // cc::CompositorFrameSinkSupportClient implementation.
  void DidReceiveCompositorFrameAck(
      const cc::ReturnedResourceArray& resources) override;
  void OnBeginFrame(const cc::BeginFrameArgs& args) override;
  void ReclaimResources(const cc::ReturnedResourceArray& resources) override;
  void WillDrawSurface(const cc::LocalSurfaceId& id,
                       const gfx::Rect& damage_rect) override {}

  // Exposed for tests.
  bool IsChildFrameForTesting() const override;
  cc::SurfaceId SurfaceIdForTesting() const override;
  CrossProcessFrameConnector* FrameConnectorForTesting() const {
    return frame_connector_;
  }

  // Returns the current surface scale factor.
  float current_surface_scale_factor() { return current_surface_scale_factor_; }

  // Returns the view into which this view is directly embedded. This can
  // return nullptr when this view's associated child frame is not connected
  // to the frame tree.
  RenderWidgetHostViewBase* GetParentView();

  void RegisterFrameSinkId();
  void UnregisterFrameSinkId();

  void UpdateViewportIntersection(const gfx::Rect& viewport_intersection);

  bool has_frame() { return has_frame_; }

 protected:
  friend class RenderWidgetHostView;
  friend class RenderWidgetHostViewChildFrameTest;
  friend class RenderWidgetHostViewGuestSurfaceTest;
  FRIEND_TEST_ALL_PREFIXES(RenderWidgetHostViewChildFrameTest,
                           ForwardsBeginFrameAcks);

  explicit RenderWidgetHostViewChildFrame(RenderWidgetHost* widget);
  void Init();

  void ProcessCompositorFrame(const cc::LocalSurfaceId& local_surface_id,
                              cc::CompositorFrame frame);

  void SendSurfaceInfoToEmbedder();

  // Clears current compositor surface, if one is in use.
  void ClearCompositorSurfaceIfNecessary();

  void ProcessFrameSwappedCallbacks();

  // The last scroll offset of the view.
  gfx::Vector2dF last_scroll_offset_;

  // Members will become private when RenderWidgetHostViewGuest is removed.
  // The model object.
  RenderWidgetHostImpl* host_;

  // The ID for FrameSink associated with this view.
  cc::FrameSinkId frame_sink_id_;

  // Surface-related state.
  std::unique_ptr<cc::CompositorFrameSinkSupport> support_;
  cc::LocalSurfaceId local_surface_id_;
  uint32_t next_surface_sequence_;
  gfx::Size current_surface_size_;
  float current_surface_scale_factor_;
  gfx::Rect last_screen_rect_;

  // frame_connector_ provides a platform abstraction. Messages
  // sent through it are routed to the embedding renderer process.
  CrossProcessFrameConnector* frame_connector_;

  base::WeakPtr<RenderWidgetHostViewChildFrame> AsWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  virtual void SendSurfaceInfoToEmbedderImpl(
      const cc::SurfaceInfo& surface_info,
      const cc::SurfaceSequence& sequence);

  void SubmitSurfaceCopyRequest(const gfx::Rect& src_subrect,
                                const gfx::Size& dst_size,
                                const ReadbackRequestCallback& callback,
                                const SkColorType preferred_color_type);

  void CreateCompositorFrameSinkSupport();
  void ResetCompositorFrameSinkSupport();

  virtual bool HasEmbedderChanged();

  using FrameSwappedCallbackList = std::deque<std::unique_ptr<base::Closure>>;
  // Since frame-drawn callbacks are "fire once", we use std::deque to make
  // it convenient to swap() when processing the list.
  FrameSwappedCallbackList frame_swapped_callbacks_;

  // The surface client ID of the parent RenderWidgetHostView.  0 if none.
  cc::FrameSinkId parent_frame_sink_id_;

  bool has_frame_ = false;
  cc::mojom::MojoCompositorFrameSinkClient* renderer_compositor_frame_sink_ =
      nullptr;

  // The background color of the widget.
  SkColor background_color_;

  base::WeakPtrFactory<RenderWidgetHostViewChildFrame> weak_factory_;
  DISALLOW_COPY_AND_ASSIGN(RenderWidgetHostViewChildFrame);
};

}  // namespace content

#endif  // CONTENT_BROWSER_FRAME_HOST_RENDER_WIDGET_HOST_VIEW_CHILD_FRAME_H_
