// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_WIDGET_DESKTOP_AURA_DESKTOP_WINDOW_TREE_HOST_MUS_H_
#define UI_VIEWS_WIDGET_DESKTOP_AURA_DESKTOP_WINDOW_TREE_HOST_MUS_H_

#include <memory>
#include <set>

#include "base/macros.h"
#include "ui/aura/mus/focus_synchronizer_observer.h"
#include "ui/aura/mus/window_tree_host_mus.h"
#include "ui/aura/window_observer.h"
#include "ui/views/mus/mus_client_observer.h"
#include "ui/views/mus/mus_export.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace wm {
class CursorManager;
}

namespace views {

class VIEWS_MUS_EXPORT DesktopWindowTreeHostMus
    : public DesktopWindowTreeHost,
      public MusClientObserver,
      public WidgetObserver,
      public aura::FocusSynchronizerObserver,
      public aura::WindowObserver,
      public aura::WindowTreeHostMus {
 public:
  DesktopWindowTreeHostMus(
      internal::NativeWidgetDelegate* native_widget_delegate,
      DesktopNativeWidgetAura* desktop_native_widget_aura,
      const cc::FrameSinkId& frame_sink_id,
      const std::map<std::string, std::vector<uint8_t>>* mus_properties);
  ~DesktopWindowTreeHostMus() override;

  // Called when the window was deleted on the server.
  void ServerDestroyedWindow() { CloseNow(); }

  // Controls whether the client area is automatically updated as necessary.
  void set_auto_update_client_area(bool value) {
    auto_update_client_area_ = value;
  }

 private:
  void SendClientAreaToServer();
  void SendHitTestMaskToServer();

  // Helper function to get the scale factor.
  float GetScaleFactor() const;

  void SetBoundsInDIP(const gfx::Rect& bounds_in_dip);

  // Returns true if the client area should be set on this.
  bool ShouldSendClientAreaToServer() const;

  // DesktopWindowTreeHost:
  void Init(aura::Window* content_window,
            const Widget::InitParams& params) override;
  void OnNativeWidgetCreated(const Widget::InitParams& params) override;
  void OnNativeWidgetActivationChanged(bool active) override;
  void OnWidgetInitDone() override;
  std::unique_ptr<corewm::Tooltip> CreateTooltip() override;
  std::unique_ptr<aura::client::DragDropClient> CreateDragDropClient(
      DesktopNativeCursorManager* cursor_manager) override;
  void Close() override;
  void CloseNow() override;
  aura::WindowTreeHost* AsWindowTreeHost() override;
  void ShowWindowWithState(ui::WindowShowState state) override;
  void ShowMaximizedWithBounds(const gfx::Rect& restored_bounds) override;
  bool IsVisible() const override;
  void SetSize(const gfx::Size& size) override;
  void StackAbove(aura::Window* window) override;
  void StackAtTop() override;
  void CenterWindow(const gfx::Size& size) override;
  void GetWindowPlacement(gfx::Rect* bounds,
                          ui::WindowShowState* show_state) const override;
  gfx::Rect GetWindowBoundsInScreen() const override;
  gfx::Rect GetClientAreaBoundsInScreen() const override;
  gfx::Rect GetRestoredBounds() const override;
  std::string GetWorkspace() const override;
  gfx::Rect GetWorkAreaBoundsInScreen() const override;
  void SetShape(std::unique_ptr<SkRegion> native_region) override;
  void Activate() override;
  void Deactivate() override;
  bool IsActive() const override;
  void Maximize() override;
  void Minimize() override;
  void Restore() override;
  bool IsMaximized() const override;
  bool IsMinimized() const override;
  bool HasCapture() const override;
  void SetAlwaysOnTop(bool always_on_top) override;
  bool IsAlwaysOnTop() const override;
  void SetVisibleOnAllWorkspaces(bool always_visible) override;
  bool IsVisibleOnAllWorkspaces() const override;
  bool SetWindowTitle(const base::string16& title) override;
  void ClearNativeFocus() override;
  Widget::MoveLoopResult RunMoveLoop(
      const gfx::Vector2d& drag_offset,
      Widget::MoveLoopSource source,
      Widget::MoveLoopEscapeBehavior escape_behavior) override;
  void EndMoveLoop() override;
  void SetVisibilityChangedAnimationsEnabled(bool value) override;
  NonClientFrameView* CreateNonClientFrameView() override;
  bool ShouldUseNativeFrame() const override;
  bool ShouldWindowContentsBeTransparent() const override;
  void FrameTypeChanged() override;
  void SetFullscreen(bool fullscreen) override;
  bool IsFullscreen() const override;
  void SetOpacity(float opacity) override;
  void SetWindowIcons(const gfx::ImageSkia& window_icon,
                      const gfx::ImageSkia& app_icon) override;
  void InitModalType(ui::ModalType modal_type) override;
  void FlashFrame(bool flash_frame) override;
  bool IsAnimatingClosed() const override;
  bool IsTranslucentWindowOpacitySupported() const override;
  void SizeConstraintsChanged() override;
  bool ShouldUpdateWindowTransparency() const override;
  bool ShouldUseDesktopNativeCursorManager() const override;
  bool ShouldCreateVisibilityController() const override;

  // MusClientObserver:
  void OnWindowManagerFrameValuesChanged() override;

  // WidgetObserver:
  void OnWidgetActivationChanged(Widget* widget, bool active) override;

  // aura::FocusSynchronizerObserver:
  void OnActiveFocusClientChanged(aura::client::FocusClient* focus_client,
                                  aura::Window* focus_client_root) override;

  // aura::WindowObserver:
  void OnWindowPropertyChanged(aura::Window* window,
                               const void* key,
                               intptr_t old) override;

  // aura::WindowTreeHostMus:
  void ShowImpl() override;
  void HideImpl() override;
  void SetBoundsInPixels(const gfx::Rect& bounds_in_pixels) override;

  internal::NativeWidgetDelegate* native_widget_delegate_;

  DesktopNativeWidgetAura* desktop_native_widget_aura_;

  // We can optionally have a parent which can order us to close, or own
  // children who we're responsible for closing when we CloseNow().
  DesktopWindowTreeHostMus* parent_ = nullptr;
  std::set<DesktopWindowTreeHostMus*> children_;

  bool is_active_ = false;

  std::unique_ptr<wm::CursorManager> cursor_manager_;

  bool auto_update_client_area_ = true;

  // Used so that Close() isn't immediate.
  base::WeakPtrFactory<DesktopWindowTreeHostMus> close_widget_factory_;

  DISALLOW_COPY_AND_ASSIGN(DesktopWindowTreeHostMus);
};

}  // namespace views

#endif  // UI_VIEWS_WIDGET_DESKTOP_AURA_DESKTOP_WINDOW_TREE_HOST_MUS_H_
