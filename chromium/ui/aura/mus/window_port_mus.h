// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_AURA_MUS_WINDOW_PORT_MUS_H_
#define UI_AURA_MUS_WINDOW_PORT_MUS_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/logging.h"
#include "base/macros.h"
#include "cc/surfaces/surface_info.h"
#include "services/ui/public/cpp/client_compositor_frame_sink.h"
#include "services/ui/public/interfaces/cursor/cursor.mojom.h"
#include "services/ui/public/interfaces/window_tree.mojom.h"
#include "services/ui/public/interfaces/window_tree_constants.mojom.h"
#include "ui/aura/aura_export.h"
#include "ui/aura/mus/mus_types.h"
#include "ui/aura/mus/window_mus.h"
#include "ui/aura/window_port.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/platform_window/mojo/text_input_state.mojom.h"

namespace aura {

class ClientSurfaceEmbedder;
class PropertyConverter;
class Window;
class WindowPortMusTestApi;
class WindowTreeClient;
class WindowTreeClientPrivate;
class WindowTreeHostMus;

// WindowPortMus is a WindowPort that forwards calls to WindowTreeClient
// so that changes are propagated to the server. All changes from
// WindowTreeClient to the underlying Window route through this class (by
// way of WindowMus) and are done in such a way that they don't result in
// calling back to WindowTreeClient.
class AURA_EXPORT WindowPortMus : public WindowPort, public WindowMus {
 public:
  // See WindowMus's constructor for details on |window_mus_type|.
  WindowPortMus(WindowTreeClient* client, WindowMusType window_mus_type);
  ~WindowPortMus() override;

  static WindowPortMus* Get(Window* window);

  Window* window() { return window_; }
  const Window* window() const { return window_; }

  ClientSurfaceEmbedder* client_surface_embedder() const {
    return client_surface_embedder_.get();
  }

  const cc::SurfaceInfo& PrimarySurfaceInfoForTesting() const {
    return primary_surface_info_;
  }

  void SetTextInputState(mojo::TextInputStatePtr state);
  void SetImeVisibility(bool visible, mojo::TextInputStatePtr state);

  ui::mojom::CursorType predefined_cursor() const { return predefined_cursor_; }
  void SetPredefinedCursor(ui::mojom::CursorType cursor_id);

  // Sets the EventTargetingPolicy, default is TARGET_AND_DESCENDANTS.
  void SetEventTargetingPolicy(ui::mojom::EventTargetingPolicy policy);

  // Sets whether this window can accept drops, defaults to false.
  void SetCanAcceptDrops(bool can_accept_drops);

  // Embeds a new client in this Window. See WindowTreeClient::Embed() for
  // details on arguments.
  void Embed(ui::mojom::WindowTreeClientPtr client,
             uint32_t flags,
             const ui::mojom::WindowTree::EmbedCallback& callback);

  using CompositorFrameSinkCallback =
      base::Callback<void(std::unique_ptr<cc::CompositorFrameSink>)>;
  void RequestCompositorFrameSink(
      scoped_refptr<cc::ContextProvider> context_provider,
      gpu::GpuMemoryBufferManager* gpu_memory_buffer_manager,
      const CompositorFrameSinkCallback& callback);

  void RequestCompositorFrameSinkInternal(
      scoped_refptr<cc::ContextProvider> context_provider,
      gpu::GpuMemoryBufferManager* gpu_memory_buffer_manager,
      const CompositorFrameSinkCallback& callback);

  void AttachCompositorFrameSink(
      std::unique_ptr<ui::ClientCompositorFrameSinkBinding>
          compositor_frame_sink_binding);

 private:
  friend class WindowPortMusTestApi;
  friend class WindowTreeClient;
  friend class WindowTreeClientPrivate;
  friend class WindowTreeHostMus;

  using ServerChangeIdType = uint8_t;

  // Changes to the underlying Window originating from the server must be done
  // in such a way that the same change is not applied back to the server. To
  // accomplish this every changes from the server is associated with at least
  // one ServerChange. If the underlying Window ends up calling back to this
  // class and the change is expected then the change is ignored and not sent to
  // the server. For example, here's the flow when the server changes the
  // bounds:
  // . WindowTreeClient calls SetBoundsFromServer().
  // . A ServerChange is added of type BOUNDS and the matching bounds.
  // . Window::SetBounds() is called.
  // . Window::SetBounds() calls WindowPortMus::OnDidChangeBounds().
  // . A ServerChange of type BOUNDS is found, and the request is ignored.
  //   Additionally the ServerChange is removed at this point so that if another
  //   bounds change is made it will be propagated. This is important as changes
  //   to underyling window may generate more changes.
  //
  // The typical pattern in implementing a call from the server looks like:
  //   // Create and configure the data as appropriate to the change:
  //   ServerChangeData data;
  //   data.foo = window->bar();
  //   ScopedServerChange change(this, ServerChangeType::FOO, data);
  //   window_->SetFoo(...);
  //
  // And the call from the Window (by way of WindowPort interface) looks like:
  //   ServerChangeData change_data;
  //   change_data.foo = ...;
  //   if (!RemoveChangeByTypeAndData(ServerChangeType::FOO, change_data))
  //     window_tree_client_->OnFooChanged(this, ...);
  enum ServerChangeType {
    ADD,
    ADD_TRANSIENT,
    BOUNDS,
    DESTROY,
    PROPERTY,
    REMOVE,
    REMOVE_TRANSIENT,
    REORDER,
    // This is used when a REORDER *may* occur as the result of a transient
    // child being added or removed. As there is no guarantee the move will
    // actually happen (the window may be in place already) this change is not
    // automatically removed. Instead the change is explicitly removed.
    TRANSIENT_REORDER,
    VISIBLE,
  };

  // Contains data needed to identify a change from the server.
  struct ServerChangeData {
    // Applies to ADD, ADD_TRANSIENT, REMOVE, REMOVE_TRANSIENT, REORDER and
    // TRANSIENT_REORDER.
    Id child_id;
    // Applies to BOUNDS. This should be in dip.
    gfx::Rect bounds_in_dip;
    // Applies to VISIBLE.
    bool visible;
    // Applies to PROPERTY.
    std::string property_name;
  };

  // Used to identify a change the server.
  struct ServerChange {
    ServerChangeType type;
    // A unique id assigned to the change and used later on to identify it for
    // removal.
    ServerChangeIdType server_change_id;
    ServerChangeData data;
  };

  using ServerChanges = std::vector<ServerChange>;

  // Convenience for adding/removing a ScopedChange.
  class ScopedServerChange {
   public:
    ScopedServerChange(WindowPortMus* window_impl,
                       const ServerChangeType type,
                       const ServerChangeData& data)
        : window_impl_(window_impl),
          server_change_id_(window_impl->ScheduleChange(type, data)) {}

    ~ScopedServerChange() { window_impl_->RemoveChangeById(server_change_id_); }

   private:
    WindowPortMus* window_impl_;
    const ServerChangeIdType server_change_id_;

    DISALLOW_COPY_AND_ASSIGN(ScopedServerChange);
  };

  struct WindowMusChangeDataImpl : public WindowMusChangeData {
    WindowMusChangeDataImpl();
    ~WindowMusChangeDataImpl() override;

    std::unique_ptr<ScopedServerChange> change;
  };

  // Creates and adds a ServerChange to |server_changes_|. Returns the id
  // assigned to the ServerChange.
  ServerChangeIdType ScheduleChange(const ServerChangeType type,
                                    const ServerChangeData& data);

  // Removes a ServerChange by id.
  void RemoveChangeById(ServerChangeIdType change_id);

  // If there is a schedule change matching |type| and |data| it is removed and
  // true is returned. If no matching change is scheduled returns false.
  bool RemoveChangeByTypeAndData(const ServerChangeType type,
                                 const ServerChangeData& data);

  ServerChanges::iterator FindChangeByTypeAndData(const ServerChangeType type,
                                                  const ServerChangeData& data);

  PropertyConverter* GetPropertyConverter();

  // WindowMus:
  Window* GetWindow() override;
  void AddChildFromServer(WindowMus* window) override;
  void RemoveChildFromServer(WindowMus* child) override;
  void ReorderFromServer(WindowMus* child,
                         WindowMus* relative,
                         ui::mojom::OrderDirection) override;
  void SetBoundsFromServer(
      const gfx::Rect& bounds,
      const base::Optional<cc::LocalSurfaceId>& local_surface_id) override;
  void SetVisibleFromServer(bool visible) override;
  void SetOpacityFromServer(float opacity) override;
  void SetPredefinedCursorFromServer(ui::mojom::CursorType cursor) override;
  void SetPropertyFromServer(
      const std::string& property_name,
      const std::vector<uint8_t>* property_data) override;
  void SetFrameSinkIdFromServer(const cc::FrameSinkId& frame_sink_id) override;
  const cc::LocalSurfaceId& GetOrAllocateLocalSurfaceId(
      const gfx::Size& surface_size) override;
  void SetPrimarySurfaceInfo(const cc::SurfaceInfo& surface_info) override;
  void SetFallbackSurfaceInfo(const cc::SurfaceInfo& surface_info) override;
  void DestroyFromServer() override;
  void AddTransientChildFromServer(WindowMus* child) override;
  void RemoveTransientChildFromServer(WindowMus* child) override;
  ChangeSource OnTransientChildAdded(WindowMus* child) override;
  ChangeSource OnTransientChildRemoved(WindowMus* child) override;
  const cc::LocalSurfaceId& GetLocalSurfaceId() override;
  std::unique_ptr<WindowMusChangeData> PrepareForServerBoundsChange(
      const gfx::Rect& bounds) override;
  std::unique_ptr<WindowMusChangeData> PrepareForServerVisibilityChange(
      bool value) override;
  void PrepareForDestroy() override;
  void PrepareForTransientRestack(WindowMus* window) override;
  void OnTransientRestackDone(WindowMus* window) override;
  void NotifyEmbeddedAppDisconnected() override;

  // WindowPort:
  void OnPreInit(Window* window) override;
  void OnDeviceScaleFactorChanged(float device_scale_factor) override;
  void OnWillAddChild(Window* child) override;
  void OnWillRemoveChild(Window* child) override;
  void OnWillMoveChild(size_t current_index, size_t dest_index) override;
  void OnVisibilityChanged(bool visible) override;
  void OnDidChangeBounds(const gfx::Rect& old_bounds,
                         const gfx::Rect& new_bounds) override;
  std::unique_ptr<ui::PropertyData> OnWillChangeProperty(
      const void* key) override;
  void OnPropertyChanged(const void* key,
                         int64_t old_value,
                         std::unique_ptr<ui::PropertyData> data) override;

  void UpdatePrimarySurfaceInfo();
  void UpdateClientSurfaceEmbedder();

  WindowTreeClient* window_tree_client_;

  Window* window_ = nullptr;

  // Used when this window is embedding a client.
  std::unique_ptr<ClientSurfaceEmbedder> client_surface_embedder_;

  ServerChangeIdType next_server_change_id_ = 0;
  ServerChanges server_changes_;

  cc::FrameSinkId frame_sink_id_;
  base::Closure pending_compositor_frame_sink_request_;

  cc::SurfaceInfo primary_surface_info_;
  cc::SurfaceInfo fallback_surface_info_;

  cc::LocalSurfaceId local_surface_id_;
  cc::LocalSurfaceIdAllocator local_surface_id_allocator_;
  gfx::Size last_surface_size_;

  ui::mojom::CursorType predefined_cursor_ = ui::mojom::CursorType::CURSOR_NULL;

  DISALLOW_COPY_AND_ASSIGN(WindowPortMus);
};

}  // namespace aura

#endif  // UI_AURA_MUS_WINDOW_PORT_MUS_H_
