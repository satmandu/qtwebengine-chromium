// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_INPUT_INPUT_HANDLER_H_
#define CC_INPUT_INPUT_HANDLER_H_

#include <memory>

#include "base/macros.h"
#include "base/time/time.h"
#include "cc/cc_export.h"
#include "cc/input/event_listener_properties.h"
#include "cc/input/main_thread_scrolling_reason.h"
#include "cc/input/scroll_state.h"
#include "cc/input/scrollbar.h"
#include "cc/trees/swap_promise_monitor.h"

namespace gfx {
class Point;
class ScrollOffset;
class SizeF;
class Vector2dF;
}

namespace ui {
class LatencyInfo;
}

namespace cc {

class ScrollElasticityHelper;

struct CC_EXPORT InputHandlerScrollResult {
  InputHandlerScrollResult();
  // Did any layer scroll as a result this ScrollBy call?
  bool did_scroll;
  // Was any of the scroll delta argument to this ScrollBy call not used?
  bool did_overscroll_root;
  // The total overscroll that has been accumulated by all ScrollBy calls that
  // have had overscroll since the last ScrollBegin call. This resets upon a
  // ScrollBy with no overscroll.
  gfx::Vector2dF accumulated_root_overscroll;
  // The amount of the scroll delta argument to this ScrollBy call that was not
  // used for scrolling.
  gfx::Vector2dF unused_scroll_delta;
};

class CC_EXPORT InputHandlerClient {
 public:
  virtual ~InputHandlerClient() {}

  virtual void WillShutdown() = 0;
  virtual void Animate(base::TimeTicks time) = 0;
  virtual void MainThreadHasStoppedFlinging() = 0;
  virtual void ReconcileElasticOverscrollAndRootScroll() = 0;
  virtual void UpdateRootLayerStateForSynchronousInputHandler(
      const gfx::ScrollOffset& total_scroll_offset,
      const gfx::ScrollOffset& max_scroll_offset,
      const gfx::SizeF& scrollable_size,
      float page_scale_factor,
      float min_page_scale_factor,
      float max_page_scale_factor) = 0;
  virtual void DeliverInputForBeginFrame() = 0;

 protected:
  InputHandlerClient() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(InputHandlerClient);
};

// The InputHandler is a way for the embedders to interact with the impl thread
// side of the compositor implementation. There is one InputHandler per
// LayerTreeHost. To use the input handler, implement the InputHanderClient
// interface and bind it to the handler on the compositor thread.
class CC_EXPORT InputHandler {
 public:
  // Note these are used in a histogram. Do not reorder or delete existing
  // entries.
  enum ScrollThread {
    SCROLL_ON_MAIN_THREAD = 0,
    SCROLL_ON_IMPL_THREAD,
    SCROLL_IGNORED,
    SCROLL_UNKNOWN,
    LAST_SCROLL_STATUS = SCROLL_UNKNOWN
  };

  struct ScrollStatus {
    ScrollStatus()
        : thread(SCROLL_ON_IMPL_THREAD),
          main_thread_scrolling_reasons(
              MainThreadScrollingReason::kNotScrollingOnMain) {}
    ScrollStatus(ScrollThread thread, uint32_t main_thread_scrolling_reasons)
        : thread(thread),
          main_thread_scrolling_reasons(main_thread_scrolling_reasons) {}
    ScrollThread thread;
    uint32_t main_thread_scrolling_reasons;
  };

  enum ScrollInputType {
    TOUCHSCREEN,
    WHEEL,
    NON_BUBBLING_GESTURE
  };

  enum class TouchStartEventListenerType {
    NO_HANDLER,
    HANDLER,
    HANDLER_ON_SCROLLING_LAYER
  };

  // Binds a client to this handler to receive notifications. Only one client
  // can be bound to an InputHandler. The client must live at least until the
  // handler calls WillShutdown() on the client.
  virtual void BindToClient(InputHandlerClient* client,
                            bool wheel_scroll_latching_enabled) = 0;

  // Selects a layer to be scrolled using the |scroll_state| start position.
  // Returns SCROLL_STARTED if the layer at the coordinates can be scrolled,
  // SCROLL_ON_MAIN_THREAD if the scroll event should instead be delegated to
  // the main thread, or SCROLL_IGNORED if there is nothing to be scrolled at
  // the given coordinates.
  virtual ScrollStatus ScrollBegin(ScrollState* scroll_state,
                                   ScrollInputType type) = 0;

  // Similar to ScrollBegin, except the hit test is skipped and scroll always
  // targets at the root layer.
  virtual ScrollStatus RootScrollBegin(ScrollState* scroll_state,
                                       ScrollInputType type) = 0;

  // Returns SCROLL_ON_IMPL_THREAD if a layer is actively being scrolled or
  // a subsequent call to ScrollAnimated can begin on the impl thread.
  virtual ScrollStatus ScrollAnimatedBegin(
      const gfx::Point& viewport_point) = 0;

  // Returns SCROLL_ON_IMPL_THREAD if an animation is initiated on the impl
  // thread. delayed_by is the delay that is taken into account when determining
  // the duration of the animation.
  virtual ScrollStatus ScrollAnimated(const gfx::Point& viewport_point,
                                      const gfx::Vector2dF& scroll_delta,
                                      base::TimeDelta delayed_by) = 0;

  // Scroll the layer selected by |ScrollBegin| by given |scroll_state| delta.
  // Internally, the delta is transformed to local layer's coordinate space for
  // scrolls gestures that are direct manipulation (e.g. touch). If there is no
  // room to move the layer in the requested direction, its first ancestor layer
  // that can be scrolled will be moved instead. The return value's |did_scroll|
  // field is set to false if no layer can be moved in the requested direction
  // at all, and set to true if any layer is moved. If the scroll delta hits the
  // root layer, and the layer can no longer move, the root overscroll
  // accumulated within this ScrollBegin() scope is reported in the return
  // value's |accumulated_overscroll| field. Should only be called if
  // ScrollBegin() returned SCROLL_STARTED.
  virtual InputHandlerScrollResult ScrollBy(ScrollState* scroll_state) = 0;

  // Returns SCROLL_STARTED if a layer was actively being scrolled,
  // SCROLL_IGNORED if not.
  virtual ScrollStatus FlingScrollBegin() = 0;

  virtual void MouseMoveAt(const gfx::Point& mouse_position) = 0;
  virtual void MouseDown() = 0;
  virtual void MouseUp() = 0;
  virtual void MouseLeave() = 0;

  // Stop scrolling the selected layer. Should only be called if ScrollBegin()
  // returned SCROLL_STARTED.
  virtual void ScrollEnd(ScrollState* scroll_state) = 0;

  // Requests a callback to UpdateRootLayerStateForSynchronousInputHandler()
  // giving the current root scroll and page scale information.
  virtual void RequestUpdateForSynchronousInputHandler() = 0;

  // Called when the root scroll offset has been changed in the synchronous
  // input handler by the application (outside of input event handling).
  virtual void SetSynchronousInputHandlerRootScrollOffset(
      const gfx::ScrollOffset& root_offset) = 0;

  virtual void PinchGestureBegin() = 0;
  virtual void PinchGestureUpdate(float magnify_delta,
                                  const gfx::Point& anchor) = 0;
  virtual void PinchGestureEnd() = 0;

  // Request another callback to InputHandlerClient::Animate().
  virtual void SetNeedsAnimateInput() = 0;

  // Returns true if there is an active scroll on the viewport.
  virtual bool IsCurrentlyScrollingViewport() const = 0;

  // Whether the layer under |viewport_point| is the currently scrolling layer.
  virtual bool IsCurrentlyScrollingLayerAt(const gfx::Point& viewport_point,
                                           ScrollInputType type) const = 0;

  virtual EventListenerProperties GetEventListenerProperties(
      EventListenerClass event_class) const = 0;

  // It returns the type of a touch start event listener at |viewport_point|.
  // Whether the page should be given the opportunity to suppress scrolling by
  // consuming touch events that started at |viewport_point|, and whether
  // |viewport_point| is on the currently scrolling layer.
  virtual TouchStartEventListenerType EventListenerTypeForTouchStartAt(
      const gfx::Point& viewport_point) = 0;

  // Calling CreateLatencyInfoSwapPromiseMonitor() to get a scoped
  // LatencyInfoSwapPromiseMonitor. During the life time of the
  // LatencyInfoSwapPromiseMonitor, if SetNeedsRedraw() or SetNeedsRedrawRect()
  // is called on LayerTreeHostImpl, the original latency info will be turned
  // into a LatencyInfoSwapPromise.
  virtual std::unique_ptr<SwapPromiseMonitor>
  CreateLatencyInfoSwapPromiseMonitor(ui::LatencyInfo* latency) = 0;

  virtual ScrollElasticityHelper* CreateScrollElasticityHelper() = 0;

  // Called by the single-threaded UI Compositor to get or set the scroll offset
  // on the impl side. Retruns false if |layer_id| isn't in the active tree.
  virtual bool GetScrollOffsetForLayer(int layer_id,
                                       gfx::ScrollOffset* offset) = 0;
  virtual bool ScrollLayerTo(int layer_id, const gfx::ScrollOffset& offset) = 0;

  virtual bool ScrollingShouldSwitchtoMainThread() = 0;

 protected:
  InputHandler() {}
  virtual ~InputHandler() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(InputHandler);
};

}  // namespace cc

#endif  // CC_INPUT_INPUT_HANDLER_H_
