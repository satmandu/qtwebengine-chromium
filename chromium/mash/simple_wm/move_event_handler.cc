// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mash/simple_wm/move_event_handler.h"

#include "mash/simple_wm/move_loop.h"
#include "services/ui/public/interfaces/cursor/cursor.mojom.h"
#include "ui/aura/mus/window_port_mus.h"
#include "ui/aura/window.h"
#include "ui/aura/window_delegate.h"
#include "ui/base/hit_test.h"
#include "ui/events/event.h"

namespace simple_wm {
namespace {

ui::mojom::CursorType CursorForWindowComponent(int window_component) {
  switch (window_component) {
    case HTBOTTOM:
      return ui::mojom::CursorType::SOUTH_RESIZE;
    case HTBOTTOMLEFT:
      return ui::mojom::CursorType::SOUTH_WEST_RESIZE;
    case HTBOTTOMRIGHT:
      return ui::mojom::CursorType::SOUTH_EAST_RESIZE;
    case HTLEFT:
      return ui::mojom::CursorType::WEST_RESIZE;
    case HTRIGHT:
      return ui::mojom::CursorType::EAST_RESIZE;
    case HTTOP:
      return ui::mojom::CursorType::NORTH_RESIZE;
    case HTTOPLEFT:
      return ui::mojom::CursorType::NORTH_WEST_RESIZE;
    case HTTOPRIGHT:
      return ui::mojom::CursorType::NORTH_EAST_RESIZE;
    default:
      return ui::mojom::CursorType::CURSOR_NULL;
  }
}

}  // namespace

MoveEventHandler::MoveEventHandler(aura::Window* window)
    : window_(window) {
  window_->AddObserver(this);
  window_->AddPreTargetHandler(this);
}

MoveEventHandler::~MoveEventHandler() {
  Detach();
}

void MoveEventHandler::ProcessLocatedEvent(ui::LocatedEvent* event) {
  const bool had_move_loop = move_loop_.get() != nullptr;
  DCHECK(event->IsMouseEvent() || event->IsTouchEvent());

  // This event handler can receive mouse events like ET_MOUSE_CAPTURE_CHANGED
  // that cannot be converted to PointerEvents. Ignore them because they aren't
  // needed for move handling.
  if (!ui::PointerEvent::CanConvertFrom(*event))
    return;

  // TODO(moshayedi): no need for this once MoveEventHandler directly receives
  // pointer events.
  std::unique_ptr<ui::PointerEvent> pointer_event;
  if (event->IsMouseEvent())
    pointer_event.reset(new ui::PointerEvent(*event->AsMouseEvent()));
  else
    pointer_event.reset(new ui::PointerEvent(*event->AsTouchEvent()));

  if (move_loop_) {
    if (move_loop_->Move(*pointer_event.get()) == MoveLoop::DONE)
      move_loop_.reset();
  } else if (pointer_event->type() == ui::ET_POINTER_DOWN) {
    const int ht_location =
        GetNonClientComponentForEvent(pointer_event.get());
    if (ht_location != HTNOWHERE)
      move_loop_ = MoveLoop::Create(window_, ht_location, *pointer_event.get());
  } else if (pointer_event->type() == ui::ET_POINTER_MOVED) {
    const int ht_location = GetNonClientComponentForEvent(pointer_event.get());
    aura::WindowPortMus::Get(window_)->SetPredefinedCursor(
        CursorForWindowComponent(ht_location));
  }
  if (had_move_loop || move_loop_)
    event->SetHandled();
}

int MoveEventHandler::GetNonClientComponentForEvent(
    const ui::LocatedEvent* event) {
  return window_->delegate()->GetNonClientComponent(event->location());
}

void MoveEventHandler::Detach() {
  window_->RemoveObserver(this);
  window_->RemovePreTargetHandler(this);
  window_ = nullptr;
}

void MoveEventHandler::OnMouseEvent(ui::MouseEvent* event) {
  ProcessLocatedEvent(event);
}

void MoveEventHandler::OnTouchEvent(ui::TouchEvent* event) {
  ProcessLocatedEvent(event);
}

void MoveEventHandler::OnCancelMode(ui::CancelModeEvent* event) {
  if (!move_loop_)
    return;

  move_loop_->Revert();
  move_loop_.reset();
  event->SetHandled();
}

void MoveEventHandler::OnWindowDestroying(aura::Window* window) {
  DCHECK_EQ(window_, window);
  Detach();
}

}  // namespace simple_wm
