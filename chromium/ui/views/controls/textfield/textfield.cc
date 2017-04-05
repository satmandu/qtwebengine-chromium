// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/textfield/textfield.h"

#include <string>
#include <utility>

#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/default_style.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/drag_utils.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/ime/text_edit_commands.h"
#include "ui/base/material_design/material_design_controller.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_switches_util.h"
#include "ui/compositor/canvas_painter.h"
#include "ui/compositor/scoped_animation_duration_scale_mode.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/selection_bound.h"
#include "ui/native_theme/native_theme.h"
#include "ui/strings/grit/ui_strings.h"
#include "ui/views/background.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/focusable_border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/drag_utils.h"
#include "ui/views/native_cursor.h"
#include "ui/views/painter.h"
#include "ui/views/style/platform_style.h"
#include "ui/views/views_delegate.h"
#include "ui/views/widget/widget.h"

#if defined(OS_WIN)
#include "base/win/win_util.h"
#include "ui/base/win/osk_display_manager.h"
#endif

#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
#include "base/strings/utf_string_conversions.h"
#include "ui/base/ime/linux/text_edit_command_auralinux.h"
#include "ui/base/ime/linux/text_edit_key_bindings_delegate_auralinux.h"
#endif

namespace views {

namespace {

#if defined(OS_MACOSX)
const ui::EventFlags kPlatformModifier = ui::EF_COMMAND_DOWN;
#else
const ui::EventFlags kPlatformModifier = ui::EF_CONTROL_DOWN;
#endif  // OS_MACOSX

#if defined(OS_MACOSX)
const gfx::SelectionBehavior kLineSelectionBehavior = gfx::SELECTION_EXTEND;
const gfx::SelectionBehavior kWordSelectionBehavior = gfx::SELECTION_CARET;
const gfx::SelectionBehavior kMoveParagraphSelectionBehavior =
    gfx::SELECTION_CARET;
#else
const gfx::SelectionBehavior kLineSelectionBehavior = gfx::SELECTION_RETAIN;
const gfx::SelectionBehavior kWordSelectionBehavior = gfx::SELECTION_RETAIN;
const gfx::SelectionBehavior kMoveParagraphSelectionBehavior =
    gfx::SELECTION_RETAIN;
#endif

// Default placeholder text color.
const SkColor kDefaultPlaceholderTextColor = SK_ColorLTGRAY;

void ConvertRectToScreen(const View* src, gfx::Rect* r) {
  DCHECK(src);

  gfx::Point new_origin = r->origin();
  View::ConvertPointToScreen(src, &new_origin);
  r->set_origin(new_origin);
}

// Get the default command for a given key |event|.
ui::TextEditCommand GetCommandForKeyEvent(const ui::KeyEvent& event) {
  if (event.type() != ui::ET_KEY_PRESSED || event.IsUnicodeKeyCode())
    return ui::TextEditCommand::INVALID_COMMAND;

  const bool shift = event.IsShiftDown();
  const bool control = event.IsControlDown() || event.IsCommandDown();
  const bool alt = event.IsAltDown() || event.IsAltGrDown();
  switch (event.key_code()) {
    case ui::VKEY_Z:
      if (control && !shift && !alt)
        return ui::TextEditCommand::UNDO;
      return (control && shift && !alt) ? ui::TextEditCommand::REDO
                                        : ui::TextEditCommand::INVALID_COMMAND;
    case ui::VKEY_Y:
      return (control && !alt) ? ui::TextEditCommand::REDO
                               : ui::TextEditCommand::INVALID_COMMAND;
    case ui::VKEY_A:
      return (control && !alt) ? ui::TextEditCommand::SELECT_ALL
                               : ui::TextEditCommand::INVALID_COMMAND;
    case ui::VKEY_X:
      return (control && !alt) ? ui::TextEditCommand::CUT
                               : ui::TextEditCommand::INVALID_COMMAND;
    case ui::VKEY_C:
      return (control && !alt) ? ui::TextEditCommand::COPY
                               : ui::TextEditCommand::INVALID_COMMAND;
    case ui::VKEY_V:
      return (control && !alt) ? ui::TextEditCommand::PASTE
                               : ui::TextEditCommand::INVALID_COMMAND;
    case ui::VKEY_RIGHT:
      // Ignore alt+right, which may be a browser navigation shortcut.
      if (alt)
        return ui::TextEditCommand::INVALID_COMMAND;
      if (!shift) {
        return control ? ui::TextEditCommand::MOVE_WORD_RIGHT
                       : ui::TextEditCommand::MOVE_RIGHT;
      }
      return control ? ui::TextEditCommand::MOVE_WORD_RIGHT_AND_MODIFY_SELECTION
                     : ui::TextEditCommand::MOVE_RIGHT_AND_MODIFY_SELECTION;
    case ui::VKEY_LEFT:
      // Ignore alt+left, which may be a browser navigation shortcut.
      if (alt)
        return ui::TextEditCommand::INVALID_COMMAND;
      if (!shift) {
        return control ? ui::TextEditCommand::MOVE_WORD_LEFT
                       : ui::TextEditCommand::MOVE_LEFT;
      }
      return control ? ui::TextEditCommand::MOVE_WORD_LEFT_AND_MODIFY_SELECTION
                     : ui::TextEditCommand::MOVE_LEFT_AND_MODIFY_SELECTION;
    case ui::VKEY_HOME:
      return shift ? ui::TextEditCommand::
                         MOVE_TO_BEGINNING_OF_LINE_AND_MODIFY_SELECTION
                   : ui::TextEditCommand::MOVE_TO_BEGINNING_OF_LINE;
    case ui::VKEY_END:
      return shift
                 ? ui::TextEditCommand::MOVE_TO_END_OF_LINE_AND_MODIFY_SELECTION
                 : ui::TextEditCommand::MOVE_TO_END_OF_LINE;
    case ui::VKEY_BACK:
      if (!control)
        return ui::TextEditCommand::DELETE_BACKWARD;
#if defined(OS_LINUX)
      // Only erase by line break on Linux and ChromeOS.
      if (shift)
        return ui::TextEditCommand::DELETE_TO_BEGINNING_OF_LINE;
#endif
      return ui::TextEditCommand::DELETE_WORD_BACKWARD;
    case ui::VKEY_DELETE:
#if defined(OS_LINUX)
      // Only erase by line break on Linux and ChromeOS.
      if (shift && control)
        return ui::TextEditCommand::DELETE_TO_END_OF_LINE;
#endif
      if (control)
        return ui::TextEditCommand::DELETE_WORD_FORWARD;
      return shift ? ui::TextEditCommand::CUT
                   : ui::TextEditCommand::DELETE_FORWARD;
    case ui::VKEY_INSERT:
      if (control && !shift)
        return ui::TextEditCommand::COPY;
      return (shift && !control) ? ui::TextEditCommand::PASTE
                                 : ui::TextEditCommand::INVALID_COMMAND;
    default:
      return ui::TextEditCommand::INVALID_COMMAND;
  }
}

const gfx::FontList& GetDefaultFontList() {
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  return rb.GetFontListWithDelta(ui::kLabelFontSizeDelta);
}

// Returns the ui::TextEditCommand corresponding to the |command_id| menu
// action. |has_selection| is true if the textfield has an active selection.
// Keep in sync with UpdateContextMenu.
ui::TextEditCommand GetTextEditCommandFromMenuCommand(int command_id,
                                                      bool has_selection) {
  switch (command_id) {
    case IDS_APP_UNDO:
      return ui::TextEditCommand::UNDO;
    case IDS_APP_CUT:
      return ui::TextEditCommand::CUT;
    case IDS_APP_COPY:
      return ui::TextEditCommand::COPY;
    case IDS_APP_PASTE:
      return ui::TextEditCommand::PASTE;
    case IDS_APP_DELETE:
      // The DELETE menu action only works in case of an active selection.
      if (has_selection)
        return ui::TextEditCommand::DELETE_FORWARD;
      break;
    case IDS_APP_SELECT_ALL:
      return ui::TextEditCommand::SELECT_ALL;
  }
  return ui::TextEditCommand::INVALID_COMMAND;
}

base::TimeDelta GetPasswordRevealDuration() {
  return ViewsDelegate::GetInstance()
             ? ViewsDelegate::GetInstance()
                   ->GetTextfieldPasswordRevealDuration()
             : base::TimeDelta();
}

bool IsControlKeyModifier(int flags) {
// XKB layout doesn't natively generate printable characters from a
// Control-modified key combination, but we cannot extend it to other platforms
// as Control has different meanings and behaviors.
// https://crrev.com/2580483002/#msg46
#if defined(OS_LINUX)
  return flags & ui::EF_CONTROL_DOWN;
#else
  return false;
#endif
}

}  // namespace

// static
const char Textfield::kViewClassName[] = "Textfield";
const int Textfield::kTextPadding = 3;

// static
size_t Textfield::GetCaretBlinkMs() {
  static const size_t default_value = 500;
#if defined(OS_WIN)
  static const size_t system_value = ::GetCaretBlinkTime();
  if (system_value != 0)
    return (system_value == INFINITE) ? 0 : system_value;
#endif
  return default_value;
}

Textfield::Textfield()
    : model_(new TextfieldModel(this)),
      controller_(NULL),
      scheduled_text_edit_command_(ui::TextEditCommand::INVALID_COMMAND),
      read_only_(false),
      default_width_in_chars_(0),
      use_default_text_color_(true),
      use_default_background_color_(true),
      use_default_selection_text_color_(true),
      use_default_selection_background_color_(true),
      text_color_(SK_ColorBLACK),
      background_color_(SK_ColorWHITE),
      selection_text_color_(SK_ColorWHITE),
      selection_background_color_(SK_ColorBLUE),
      placeholder_text_color_(kDefaultPlaceholderTextColor),
      invalid_(false),
      text_input_type_(ui::TEXT_INPUT_TYPE_TEXT),
      text_input_flags_(0),
      performing_user_action_(false),
      skip_input_method_cancel_composition_(false),
      drop_cursor_visible_(false),
      initiating_drag_(false),
      selection_controller_(this),
      drag_start_display_offset_(0),
      touch_handles_hidden_due_to_scroll_(false),
      use_focus_ring_(ui::MaterialDesignController::IsSecondaryUiMaterial()),
      weak_ptr_factory_(this) {
  set_context_menu_controller(this);
  set_drag_controller(this);
  cursor_view_.SetPaintToLayer(ui::LAYER_SOLID_COLOR);
  cursor_view_.layer()->SetColor(GetTextColor());
  // |cursor_view_| is owned by Textfield view.
  cursor_view_.set_owned_by_client();
  AddChildView(&cursor_view_);
  GetRenderText()->SetFontList(GetDefaultFontList());
  View::SetBorder(std::unique_ptr<Border>(new FocusableBorder()));
  SetFocusBehavior(FocusBehavior::ALWAYS);

  // These allow BrowserView to pass edit commands from the Chrome menu to us
  // when we're focused by simply asking the FocusManager to
  // ProcessAccelerator() with the relevant accelerators.
  AddAccelerator(ui::Accelerator(ui::VKEY_X, ui::EF_CONTROL_DOWN));
  AddAccelerator(ui::Accelerator(ui::VKEY_C, ui::EF_CONTROL_DOWN));
  AddAccelerator(ui::Accelerator(ui::VKEY_V, ui::EF_CONTROL_DOWN));
}

Textfield::~Textfield() {
  if (GetInputMethod()) {
    // The textfield should have been blurred before destroy.
    DCHECK(this != GetInputMethod()->GetTextInputClient());
  }
}

void Textfield::SetReadOnly(bool read_only) {
  // Update read-only without changing the focusable state (or active, etc.).
  read_only_ = read_only;
  if (GetInputMethod())
    GetInputMethod()->OnTextInputTypeChanged(this);
  SetColor(GetTextColor());
  UpdateBackgroundColor();
}

void Textfield::SetTextInputType(ui::TextInputType type) {
  GetRenderText()->SetObscured(type == ui::TEXT_INPUT_TYPE_PASSWORD);
  text_input_type_ = type;
  OnCaretBoundsChanged();
  if (GetInputMethod())
    GetInputMethod()->OnTextInputTypeChanged(this);
  SchedulePaint();
}

void Textfield::SetTextInputFlags(int flags) {
  text_input_flags_ = flags;
}

void Textfield::SetText(const base::string16& new_text) {
  model_->SetText(new_text);
  OnCaretBoundsChanged();
  SchedulePaint();
  NotifyAccessibilityEvent(ui::AX_EVENT_TEXT_CHANGED, true);
}

void Textfield::AppendText(const base::string16& new_text) {
  if (new_text.empty())
    return;
  model_->Append(new_text);
  OnCaretBoundsChanged();
  SchedulePaint();
}

void Textfield::InsertOrReplaceText(const base::string16& new_text) {
  if (new_text.empty())
    return;
  model_->InsertText(new_text);
  UpdateAfterChange(true, true);
}

base::string16 Textfield::GetSelectedText() const {
  return model_->GetSelectedText();
}

void Textfield::SelectAll(bool reversed) {
  model_->SelectAll(reversed);
  if (HasSelection() && performing_user_action_)
    UpdateSelectionClipboard();
  UpdateAfterChange(false, true);
}

void Textfield::SelectWordAt(const gfx::Point& point) {
  model_->MoveCursorTo(point, false);
  model_->SelectWord();
  UpdateAfterChange(false, true);
}

void Textfield::ClearSelection() {
  model_->ClearSelection();
  UpdateAfterChange(false, true);
}

bool Textfield::HasSelection() const {
  return !GetSelectedRange().is_empty();
}

SkColor Textfield::GetTextColor() const {
  if (!use_default_text_color_)
    return text_color_;

  return GetNativeTheme()->GetSystemColor(read_only() || !enabled() ?
      ui::NativeTheme::kColorId_TextfieldReadOnlyColor :
      ui::NativeTheme::kColorId_TextfieldDefaultColor);
}

void Textfield::SetTextColor(SkColor color) {
  text_color_ = color;
  use_default_text_color_ = false;
  SetColor(color);
}

void Textfield::UseDefaultTextColor() {
  use_default_text_color_ = true;
  SetColor(GetTextColor());
}

SkColor Textfield::GetBackgroundColor() const {
  if (!use_default_background_color_)
    return background_color_;

  return GetNativeTheme()->GetSystemColor(read_only() || !enabled() ?
      ui::NativeTheme::kColorId_TextfieldReadOnlyBackground :
      ui::NativeTheme::kColorId_TextfieldDefaultBackground);
}

void Textfield::SetBackgroundColor(SkColor color) {
  background_color_ = color;
  use_default_background_color_ = false;
  UpdateBackgroundColor();
}

void Textfield::UseDefaultBackgroundColor() {
  use_default_background_color_ = true;
  UpdateBackgroundColor();
}

SkColor Textfield::GetSelectionTextColor() const {
  return use_default_selection_text_color_ ?
      GetNativeTheme()->GetSystemColor(
          ui::NativeTheme::kColorId_TextfieldSelectionColor) :
      selection_text_color_;
}

void Textfield::SetSelectionTextColor(SkColor color) {
  selection_text_color_ = color;
  use_default_selection_text_color_ = false;
  GetRenderText()->set_selection_color(GetSelectionTextColor());
  SchedulePaint();
}

void Textfield::UseDefaultSelectionTextColor() {
  use_default_selection_text_color_ = true;
  GetRenderText()->set_selection_color(GetSelectionTextColor());
  SchedulePaint();
}

SkColor Textfield::GetSelectionBackgroundColor() const {
  return use_default_selection_background_color_ ?
      GetNativeTheme()->GetSystemColor(
          ui::NativeTheme::kColorId_TextfieldSelectionBackgroundFocused) :
      selection_background_color_;
}

void Textfield::SetSelectionBackgroundColor(SkColor color) {
  selection_background_color_ = color;
  use_default_selection_background_color_ = false;
  GetRenderText()->set_selection_background_focused_color(
      GetSelectionBackgroundColor());
  SchedulePaint();
}

void Textfield::UseDefaultSelectionBackgroundColor() {
  use_default_selection_background_color_ = true;
  GetRenderText()->set_selection_background_focused_color(
      GetSelectionBackgroundColor());
  SchedulePaint();
}

bool Textfield::GetCursorEnabled() const {
  return GetRenderText()->cursor_enabled();
}

void Textfield::SetCursorEnabled(bool enabled) {
  if (GetRenderText()->cursor_enabled() == enabled)
    return;

  GetRenderText()->SetCursorEnabled(enabled);
  UpdateCursorViewPosition();
  UpdateCursorVisibility();
}

const gfx::FontList& Textfield::GetFontList() const {
  return GetRenderText()->font_list();
}

void Textfield::SetFontList(const gfx::FontList& font_list) {
  GetRenderText()->SetFontList(font_list);
  OnCaretBoundsChanged();
  PreferredSizeChanged();
}

base::string16 Textfield::GetPlaceholderText() const {
  return placeholder_text_;
}

gfx::HorizontalAlignment Textfield::GetHorizontalAlignment() const {
  return GetRenderText()->horizontal_alignment();
}

void Textfield::SetHorizontalAlignment(gfx::HorizontalAlignment alignment) {
  GetRenderText()->SetHorizontalAlignment(alignment);
}

void Textfield::ShowImeIfNeeded() {
  if (enabled() && !read_only())
    GetInputMethod()->ShowImeIfNeeded();
}

bool Textfield::IsIMEComposing() const {
  return model_->HasCompositionText();
}

const gfx::Range& Textfield::GetSelectedRange() const {
  return GetRenderText()->selection();
}

void Textfield::SelectRange(const gfx::Range& range) {
  model_->SelectRange(range);
  UpdateAfterChange(false, true);
}

const gfx::SelectionModel& Textfield::GetSelectionModel() const {
  return GetRenderText()->selection_model();
}

void Textfield::SelectSelectionModel(const gfx::SelectionModel& sel) {
  model_->SelectSelectionModel(sel);
  UpdateAfterChange(false, true);
}

size_t Textfield::GetCursorPosition() const {
  return model_->GetCursorPosition();
}

void Textfield::SetColor(SkColor value) {
  GetRenderText()->SetColor(value);
  SchedulePaint();
}

void Textfield::ApplyColor(SkColor value, const gfx::Range& range) {
  GetRenderText()->ApplyColor(value, range);
  SchedulePaint();
}

void Textfield::SetStyle(gfx::TextStyle style, bool value) {
  GetRenderText()->SetStyle(style, value);
  SchedulePaint();
}

void Textfield::ApplyStyle(gfx::TextStyle style,
                           bool value,
                           const gfx::Range& range) {
  GetRenderText()->ApplyStyle(style, value, range);
  SchedulePaint();
}

void Textfield::SetInvalid(bool invalid) {
  if (invalid == invalid_)
    return;
  invalid_ = invalid;
  UpdateBorder();

  if (HasFocus() && use_focus_ring_) {
    FocusRing::Install(this, invalid_
                                 ? ui::NativeTheme::kColorId_AlertSeverityHigh
                                 : ui::NativeTheme::kColorId_NumColors);
  }
}

void Textfield::ClearEditHistory() {
  model_->ClearEditHistory();
}

void Textfield::SetAccessibleName(const base::string16& name) {
  accessible_name_ = name;
}

////////////////////////////////////////////////////////////////////////////////
// Textfield, View overrides:

gfx::Insets Textfield::GetInsets() const {
  gfx::Insets insets = View::GetInsets();
  insets += gfx::Insets(kTextPadding, kTextPadding, kTextPadding, kTextPadding);
  return insets;
}

int Textfield::GetBaseline() const {
  return GetInsets().top() + GetRenderText()->GetBaseline();
}

gfx::Size Textfield::GetPreferredSize() const {
  const gfx::Insets& insets = GetInsets();
  return gfx::Size(GetFontList().GetExpectedTextWidth(default_width_in_chars_) +
                   insets.width(), GetFontList().GetHeight() + insets.height());
}

const char* Textfield::GetClassName() const {
  return kViewClassName;
}

void Textfield::SetBorder(std::unique_ptr<Border> b) {
  if (use_focus_ring_ && HasFocus())
    FocusRing::Uninstall(this);
  use_focus_ring_ = false;
  View::SetBorder(std::move(b));
}

gfx::NativeCursor Textfield::GetCursor(const ui::MouseEvent& event) {
  bool in_selection = GetRenderText()->IsPointInSelection(event.location());
  bool drag_event = event.type() == ui::ET_MOUSE_DRAGGED;
  bool text_cursor = !initiating_drag_ && (drag_event || !in_selection);
  return text_cursor ? GetNativeIBeamCursor() : gfx::kNullCursor;
}

bool Textfield::OnMousePressed(const ui::MouseEvent& event) {
  const bool had_focus = HasFocus();
  bool handled = controller_ && controller_->HandleMouseEvent(this, event);
  if (!handled &&
      (event.IsOnlyLeftMouseButton() || event.IsOnlyRightMouseButton())) {
    if (!had_focus)
      RequestFocus();
    ShowImeIfNeeded();
  }

#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
  if (!handled && !had_focus && event.IsOnlyMiddleMouseButton())
    RequestFocus();
#endif

  return selection_controller_.OnMousePressed(
      event, handled, had_focus ? SelectionController::FOCUSED
                                : SelectionController::UNFOCUSED);
}

bool Textfield::OnMouseDragged(const ui::MouseEvent& event) {
  return selection_controller_.OnMouseDragged(event);
}

void Textfield::OnMouseReleased(const ui::MouseEvent& event) {
  selection_controller_.OnMouseReleased(event);
}

void Textfield::OnMouseCaptureLost() {
  selection_controller_.OnMouseCaptureLost();
}

WordLookupClient* Textfield::GetWordLookupClient() {
  return this;
}

bool Textfield::OnKeyPressed(const ui::KeyEvent& event) {
  ui::TextEditCommand edit_command = scheduled_text_edit_command_;
  scheduled_text_edit_command_ = ui::TextEditCommand::INVALID_COMMAND;

  // Since HandleKeyEvent() might destroy |this|, get a weak pointer and verify
  // it isn't null before proceeding.
  base::WeakPtr<Textfield> textfield(weak_ptr_factory_.GetWeakPtr());

  bool handled = controller_ && controller_->HandleKeyEvent(this, event);

  if (!textfield)
    return handled;

#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
  ui::TextEditKeyBindingsDelegateAuraLinux* delegate =
      ui::GetTextEditKeyBindingsDelegate();
  std::vector<ui::TextEditCommandAuraLinux> commands;
  if (!handled && delegate && delegate->MatchEvent(event, &commands)) {
    for (size_t i = 0; i < commands.size(); ++i) {
      if (IsTextEditCommandEnabled(commands[i].command())) {
        ExecuteTextEditCommand(commands[i].command());
        handled = true;
      }
    }
    return handled;
  }
#endif

  if (edit_command == ui::TextEditCommand::INVALID_COMMAND)
    edit_command = GetCommandForKeyEvent(event);

  if (!handled && IsTextEditCommandEnabled(edit_command)) {
    ExecuteTextEditCommand(edit_command);
    handled = true;
  }
  return handled;
}

bool Textfield::OnKeyReleased(const ui::KeyEvent& event) {
  return controller_ && controller_->HandleKeyEvent(this, event);
}

void Textfield::OnGestureEvent(ui::GestureEvent* event) {
  switch (event->type()) {
    case ui::ET_GESTURE_TAP_DOWN:
      RequestFocus();
      ShowImeIfNeeded();
      event->SetHandled();
      break;
    case ui::ET_GESTURE_TAP:
      if (event->details().tap_count() == 1) {
        // If tap is on the selection and touch handles are not present, handles
        // should be shown without changing selection. Otherwise, cursor should
        // be moved to the tap location.
        if (touch_selection_controller_ ||
            !GetRenderText()->IsPointInSelection(event->location())) {
          OnBeforeUserAction();
          MoveCursorTo(event->location(), false);
          OnAfterUserAction();
        }
      } else if (event->details().tap_count() == 2) {
        OnBeforeUserAction();
        SelectWordAt(event->location());
        OnAfterUserAction();
      } else {
        OnBeforeUserAction();
        SelectAll(false);
        OnAfterUserAction();
      }
      CreateTouchSelectionControllerAndNotifyIt();
#if defined(OS_WIN)
      if (!read_only()) {
        DCHECK(ui::OnScreenKeyboardDisplayManager::GetInstance());
        ui::OnScreenKeyboardDisplayManager::GetInstance()
            ->DisplayVirtualKeyboard(nullptr);
      }
#endif
      event->SetHandled();
      break;
    case ui::ET_GESTURE_LONG_PRESS:
      if (!GetRenderText()->IsPointInSelection(event->location())) {
        // If long-press happens outside selection, select word and try to
        // activate touch selection.
        OnBeforeUserAction();
        SelectWordAt(event->location());
        OnAfterUserAction();
        CreateTouchSelectionControllerAndNotifyIt();
        // If touch selection activated successfully, mark event as handled so
        // that the regular context menu is not shown.
        if (touch_selection_controller_)
          event->SetHandled();
      } else {
        // If long-press happens on the selection, deactivate touch selection
        // and try to initiate drag-drop. If drag-drop is not enabled, context
        // menu will be shown. Event is not marked as handled to let Views
        // handle drag-drop or context menu.
        DestroyTouchSelection();
        initiating_drag_ = switches::IsTouchDragDropEnabled();
      }
      break;
    case ui::ET_GESTURE_LONG_TAP:
      // If touch selection is enabled, the context menu on long tap will be
      // shown by the |touch_selection_controller_|, hence we mark the event
      // handled so Views does not try to show context menu on it.
      if (touch_selection_controller_)
        event->SetHandled();
      break;
    case ui::ET_GESTURE_SCROLL_BEGIN:
      touch_handles_hidden_due_to_scroll_ = touch_selection_controller_ != NULL;
      DestroyTouchSelection();
      drag_start_location_ = event->location();
      drag_start_display_offset_ =
          GetRenderText()->GetUpdatedDisplayOffset().x();
      event->SetHandled();
      break;
    case ui::ET_GESTURE_SCROLL_UPDATE: {
      int new_offset = drag_start_display_offset_ + event->location().x() -
          drag_start_location_.x();
      GetRenderText()->SetDisplayOffset(new_offset);
      SchedulePaint();
      event->SetHandled();
      break;
    }
    case ui::ET_GESTURE_SCROLL_END:
    case ui::ET_SCROLL_FLING_START:
      if (touch_handles_hidden_due_to_scroll_) {
        CreateTouchSelectionControllerAndNotifyIt();
        touch_handles_hidden_due_to_scroll_ = false;
      }
      event->SetHandled();
      break;
    default:
      return;
  }
}

// This function is called by BrowserView to execute clipboard commands.
bool Textfield::AcceleratorPressed(const ui::Accelerator& accelerator) {
  ui::KeyEvent event(accelerator.type(), accelerator.key_code(),
                     accelerator.modifiers());
  ExecuteTextEditCommand(GetCommandForKeyEvent(event));
  return true;
}

bool Textfield::CanHandleAccelerators() const {
  return GetRenderText()->focused() && View::CanHandleAccelerators();
}

void Textfield::AboutToRequestFocusFromTabTraversal(bool reverse) {
  SelectAll(false);
}

bool Textfield::SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) {
#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
  // Skip any accelerator handling that conflicts with custom keybindings.
  ui::TextEditKeyBindingsDelegateAuraLinux* delegate =
      ui::GetTextEditKeyBindingsDelegate();
  std::vector<ui::TextEditCommandAuraLinux> commands;
  if (delegate && delegate->MatchEvent(event, &commands)) {
    for (size_t i = 0; i < commands.size(); ++i)
      if (IsTextEditCommandEnabled(commands[i].command()))
        return true;
  }
#endif

  // Skip backspace accelerator handling; editable textfields handle this key.
  // Also skip processing Windows [Alt]+<num-pad digit> Unicode alt-codes.
  const bool is_backspace = event.key_code() == ui::VKEY_BACK;
  return (is_backspace && !read_only()) || event.IsUnicodeKeyCode();
}

bool Textfield::GetDropFormats(
    int* formats,
    std::set<ui::Clipboard::FormatType>* format_types) {
  if (!enabled() || read_only())
    return false;
  // TODO(msw): Can we support URL, FILENAME, etc.?
  *formats = ui::OSExchangeData::STRING;
  if (controller_)
    controller_->AppendDropFormats(formats, format_types);
  return true;
}

bool Textfield::CanDrop(const OSExchangeData& data) {
  int formats;
  std::set<ui::Clipboard::FormatType> format_types;
  GetDropFormats(&formats, &format_types);
  return enabled() && !read_only() && data.HasAnyFormat(formats, format_types);
}

int Textfield::OnDragUpdated(const ui::DropTargetEvent& event) {
  DCHECK(CanDrop(event.data()));
  gfx::RenderText* render_text = GetRenderText();
  const gfx::Range& selection = render_text->selection();
  drop_cursor_position_ = render_text->FindCursorPosition(event.location());
  bool in_selection = !selection.is_empty() &&
      selection.Contains(gfx::Range(drop_cursor_position_.caret_pos()));
  drop_cursor_visible_ = !in_selection;
  // TODO(msw): Pan over text when the user drags to the visible text edge.
  OnCaretBoundsChanged();
  SchedulePaint();

  StopBlinkingCursor();

  if (initiating_drag_) {
    if (in_selection)
      return ui::DragDropTypes::DRAG_NONE;
    return event.IsControlDown() ? ui::DragDropTypes::DRAG_COPY :
                                   ui::DragDropTypes::DRAG_MOVE;
  }
  return ui::DragDropTypes::DRAG_COPY | ui::DragDropTypes::DRAG_MOVE;
}

void Textfield::OnDragExited() {
  drop_cursor_visible_ = false;
  if (ShouldBlinkCursor())
    StartBlinkingCursor();
  SchedulePaint();
}

int Textfield::OnPerformDrop(const ui::DropTargetEvent& event) {
  DCHECK(CanDrop(event.data()));
  drop_cursor_visible_ = false;

  if (controller_) {
    int drag_operation = controller_->OnDrop(event.data());
    if (drag_operation != ui::DragDropTypes::DRAG_NONE)
      return drag_operation;
  }

  gfx::RenderText* render_text = GetRenderText();
  DCHECK(!initiating_drag_ ||
         !render_text->IsPointInSelection(event.location()));
  OnBeforeUserAction();
  skip_input_method_cancel_composition_ = true;

  gfx::SelectionModel drop_destination_model =
      render_text->FindCursorPosition(event.location());
  base::string16 new_text;
  event.data().GetString(&new_text);

  // Delete the current selection for a drag and drop within this view.
  const bool move = initiating_drag_ && !event.IsControlDown() &&
                    event.source_operations() & ui::DragDropTypes::DRAG_MOVE;
  if (move) {
    // Adjust the drop destination if it is on or after the current selection.
    size_t pos = drop_destination_model.caret_pos();
    pos -= render_text->selection().Intersect(gfx::Range(0, pos)).length();
    model_->DeleteSelectionAndInsertTextAt(new_text, pos);
  } else {
    model_->MoveCursorTo(drop_destination_model);
    // Drop always inserts text even if the textfield is not in insert mode.
    model_->InsertText(new_text);
  }
  skip_input_method_cancel_composition_ = false;
  UpdateAfterChange(true, true);
  OnAfterUserAction();
  return move ? ui::DragDropTypes::DRAG_MOVE : ui::DragDropTypes::DRAG_COPY;
}

void Textfield::OnDragDone() {
  initiating_drag_ = false;
  drop_cursor_visible_ = false;
}

void Textfield::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  node_data->role = ui::AX_ROLE_TEXT_FIELD;
  node_data->SetName(accessible_name_);
  if (enabled()) {
    node_data->AddIntAttribute(ui::AX_ATTR_ACTION,
                               ui::AX_SUPPORTED_ACTION_ACTIVATE);
  }
  if (read_only())
    node_data->AddStateFlag(ui::AX_STATE_READ_ONLY);
  else
    node_data->AddStateFlag(ui::AX_STATE_EDITABLE);
  if (text_input_type_ == ui::TEXT_INPUT_TYPE_PASSWORD) {
    node_data->AddStateFlag(ui::AX_STATE_PROTECTED);
    node_data->SetValue(base::string16(text().size(), '*'));
  } else {
    node_data->SetValue(text());
  }
  node_data->AddStringAttribute(ui::AX_ATTR_PLACEHOLDER,
                                base::UTF16ToUTF8(GetPlaceholderText()));

  const gfx::Range range = GetSelectedRange();
  node_data->AddIntAttribute(ui::AX_ATTR_TEXT_SEL_START, range.start());
  node_data->AddIntAttribute(ui::AX_ATTR_TEXT_SEL_END, range.end());
}

bool Textfield::HandleAccessibleAction(const ui::AXActionData& action_data) {
  if (action_data.action == ui::AX_ACTION_SET_SELECTION) {
    if (action_data.anchor_node_id != action_data.focus_node_id)
      return false;
    // TODO(nektar): Check that the focus_node_id matches the ID of this node.
    const gfx::Range range(action_data.anchor_offset, action_data.focus_offset);
    return SetSelectionRange(range);
  }

  // Remaining actions cannot be performed on readonly fields.
  if (read_only())
    return View::HandleAccessibleAction(action_data);

  if (action_data.action == ui::AX_ACTION_SET_VALUE) {
    SetText(action_data.value);
    ClearSelection();
    return true;
  } else if (action_data.action == ui::AX_ACTION_REPLACE_SELECTED_TEXT) {
    InsertOrReplaceText(action_data.value);
    ClearSelection();
    return true;
  }

  return View::HandleAccessibleAction(action_data);
}

void Textfield::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  // Textfield insets include a reasonable amount of whitespace on all sides of
  // the default font list. Fallback fonts with larger heights may paint over
  // the vertical whitespace as needed. Alternate solutions involve undesirable
  // behavior like changing the default font size, shrinking some fallback fonts
  // beyond their legibility, or enlarging controls dynamically with content.
  gfx::Rect bounds = GetContentsBounds();
  // GetContentsBounds() does not actually use the local GetInsets() override.
  bounds.Inset(gfx::Insets(0, kTextPadding, 0, kTextPadding));
  GetRenderText()->SetDisplayRect(bounds);
  OnCaretBoundsChanged();
}

bool Textfield::GetNeedsNotificationWhenVisibleBoundsChange() const {
  return true;
}

void Textfield::OnVisibleBoundsChanged() {
  if (touch_selection_controller_)
    touch_selection_controller_->SelectionChanged();
}

void Textfield::OnEnabledChanged() {
  View::OnEnabledChanged();
  if (GetInputMethod())
    GetInputMethod()->OnTextInputTypeChanged(this);
  SchedulePaint();
}

void Textfield::OnPaint(gfx::Canvas* canvas) {
  OnPaintBackground(canvas);
  PaintTextAndCursor(canvas);
  OnPaintBorder(canvas);
}

void Textfield::OnFocus() {
  GetRenderText()->set_focused(true);
  if (ShouldShowCursor()) {
    UpdateCursorViewPosition();
    cursor_view_.SetVisible(true);
  }
  if (GetInputMethod())
    GetInputMethod()->SetFocusedTextInputClient(this);
  OnCaretBoundsChanged();
  if (ShouldBlinkCursor())
    StartBlinkingCursor();
  if (use_focus_ring_) {
    FocusRing::Install(this, invalid_
                                 ? ui::NativeTheme::kColorId_AlertSeverityHigh
                                 : ui::NativeTheme::kColorId_NumColors);
  }
  SchedulePaint();
  View::OnFocus();
}

void Textfield::OnBlur() {
  gfx::RenderText* render_text = GetRenderText();
  render_text->set_focused(false);
  if (GetInputMethod())
    GetInputMethod()->DetachTextInputClient(this);
  StopBlinkingCursor();
  cursor_view_.SetVisible(false);

  DestroyTouchSelection();

  if (use_focus_ring_)
    FocusRing::Uninstall(this);
  SchedulePaint();
  View::OnBlur();
}

gfx::Point Textfield::GetKeyboardContextMenuLocation() {
  return GetCaretBounds().bottom_right();
}

void Textfield::OnNativeThemeChanged(const ui::NativeTheme* theme) {
  gfx::RenderText* render_text = GetRenderText();
  render_text->SetColor(GetTextColor());
  UpdateBackgroundColor();
  render_text->set_selection_color(GetSelectionTextColor());
  render_text->set_selection_background_focused_color(
      GetSelectionBackgroundColor());
  cursor_view_.layer()->SetColor(GetTextColor());
}

////////////////////////////////////////////////////////////////////////////////
// Textfield, TextfieldModel::Delegate overrides:

void Textfield::OnCompositionTextConfirmedOrCleared() {
  if (!skip_input_method_cancel_composition_)
    GetInputMethod()->CancelComposition(this);
}

////////////////////////////////////////////////////////////////////////////////
// Textfield, ContextMenuController overrides:

void Textfield::ShowContextMenuForView(View* source,
                                       const gfx::Point& point,
                                       ui::MenuSourceType source_type) {
  UpdateContextMenu();
  ignore_result(context_menu_runner_->RunMenuAt(GetWidget(),
                                                NULL,
                                                gfx::Rect(point, gfx::Size()),
                                                MENU_ANCHOR_TOPLEFT,
                                                source_type));
}

////////////////////////////////////////////////////////////////////////////////
// Textfield, DragController overrides:

void Textfield::WriteDragDataForView(View* sender,
                                     const gfx::Point& press_pt,
                                     OSExchangeData* data) {
  const base::string16& selected_text(GetSelectedText());
  data->SetString(selected_text);
  Label label(selected_text, GetFontList());
  label.SetBackgroundColor(GetBackgroundColor());
  label.SetSubpixelRenderingEnabled(false);
  gfx::Size size(label.GetPreferredSize());
  gfx::NativeView native_view = GetWidget()->GetNativeView();
  display::Display display =
      display::Screen::GetScreen()->GetDisplayNearestWindow(native_view);
  size.SetToMin(gfx::Size(display.size().width(), height()));
  label.SetBoundsRect(gfx::Rect(size));
  label.SetEnabledColor(GetTextColor());

  SkBitmap bitmap;
  float raster_scale = ScaleFactorForDragFromWidget(GetWidget());
#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
  // Desktop Linux Aura does not yet support transparency in drag images.
  SkColor color = GetBackgroundColor();
#else
  SkColor color = SK_ColorTRANSPARENT;
#endif
  label.Paint(
      ui::CanvasPainter(&bitmap, label.size(), raster_scale, color).context());
  const gfx::Vector2d kOffset(-15, 0);
  gfx::ImageSkia image(gfx::ImageSkiaRep(bitmap, raster_scale));
  drag_utils::SetDragImageOnDataObject(image, kOffset, data);
  if (controller_)
    controller_->OnWriteDragData(data);
}

int Textfield::GetDragOperationsForView(View* sender, const gfx::Point& p) {
  int drag_operations = ui::DragDropTypes::DRAG_COPY;
  if (!enabled() || text_input_type_ == ui::TEXT_INPUT_TYPE_PASSWORD ||
      !GetRenderText()->IsPointInSelection(p)) {
    drag_operations = ui::DragDropTypes::DRAG_NONE;
  } else if (sender == this && !read_only()) {
    drag_operations =
        ui::DragDropTypes::DRAG_MOVE | ui::DragDropTypes::DRAG_COPY;
  }
  if (controller_)
    controller_->OnGetDragOperationsForTextfield(&drag_operations);
  return drag_operations;
}

bool Textfield::CanStartDragForView(View* sender,
                                    const gfx::Point& press_pt,
                                    const gfx::Point& p) {
  return initiating_drag_ && GetRenderText()->IsPointInSelection(press_pt);
}

////////////////////////////////////////////////////////////////////////////////
// Textfield, WordLookupClient overrides:

bool Textfield::GetDecoratedWordAtPoint(const gfx::Point& point,
                                        gfx::DecoratedText* decorated_word,
                                        gfx::Point* baseline_point) {
  return GetRenderText()->GetDecoratedWordAtPoint(point, decorated_word,
                                                  baseline_point);
}

////////////////////////////////////////////////////////////////////////////////
// Textfield, SelectionControllerDelegate overrides:

bool Textfield::HasTextBeingDragged() const {
  return initiating_drag_;
}

////////////////////////////////////////////////////////////////////////////////
// Textfield, ui::TouchEditable overrides:

void Textfield::SelectRect(const gfx::Point& start, const gfx::Point& end) {
  if (GetTextInputType() == ui::TEXT_INPUT_TYPE_NONE)
    return;

  gfx::SelectionModel start_caret = GetRenderText()->FindCursorPosition(start);
  gfx::SelectionModel end_caret = GetRenderText()->FindCursorPosition(end);
  gfx::SelectionModel selection(
      gfx::Range(start_caret.caret_pos(), end_caret.caret_pos()),
      end_caret.caret_affinity());

  OnBeforeUserAction();
  SelectSelectionModel(selection);
  OnAfterUserAction();
}

void Textfield::MoveCaretTo(const gfx::Point& point) {
  SelectRect(point, point);
}

void Textfield::GetSelectionEndPoints(gfx::SelectionBound* anchor,
                                      gfx::SelectionBound* focus) {
  gfx::RenderText* render_text = GetRenderText();
  const gfx::SelectionModel& sel = render_text->selection_model();
  gfx::SelectionModel start_sel =
      render_text->GetSelectionModelForSelectionStart();
  gfx::Rect r1 = render_text->GetCursorBounds(start_sel, true);
  gfx::Rect r2 = render_text->GetCursorBounds(sel, true);

  anchor->SetEdge(gfx::PointF(r1.origin()), gfx::PointF(r1.bottom_left()));
  focus->SetEdge(gfx::PointF(r2.origin()), gfx::PointF(r2.bottom_left()));

  // Determine the SelectionBound's type for focus and anchor.
  // TODO(mfomitchev): Ideally we should have different logical directions for
  // start and end to support proper handle direction for mixed LTR/RTL text.
  const bool ltr = GetTextDirection() != base::i18n::RIGHT_TO_LEFT;
  size_t anchor_position_index = sel.selection().start();
  size_t focus_position_index = sel.selection().end();

  if (anchor_position_index == focus_position_index) {
    anchor->set_type(gfx::SelectionBound::CENTER);
    focus->set_type(gfx::SelectionBound::CENTER);
  } else if ((ltr && anchor_position_index < focus_position_index) ||
             (!ltr && anchor_position_index > focus_position_index)) {
    anchor->set_type(gfx::SelectionBound::LEFT);
    focus->set_type(gfx::SelectionBound::RIGHT);
  } else {
    anchor->set_type(gfx::SelectionBound::RIGHT);
    focus->set_type(gfx::SelectionBound::LEFT);
  }
}

gfx::Rect Textfield::GetBounds() {
  return GetLocalBounds();
}

gfx::NativeView Textfield::GetNativeView() const {
  return GetWidget()->GetNativeView();
}

void Textfield::ConvertPointToScreen(gfx::Point* point) {
  View::ConvertPointToScreen(this, point);
}

void Textfield::ConvertPointFromScreen(gfx::Point* point) {
  View::ConvertPointFromScreen(this, point);
}

bool Textfield::DrawsHandles() {
  return false;
}

void Textfield::OpenContextMenu(const gfx::Point& anchor) {
  DestroyTouchSelection();
  ShowContextMenu(anchor, ui::MENU_SOURCE_TOUCH_EDIT_MENU);
}

void Textfield::DestroyTouchSelection() {
  touch_selection_controller_.reset();
}

////////////////////////////////////////////////////////////////////////////////
// Textfield, ui::SimpleMenuModel::Delegate overrides:

bool Textfield::IsCommandIdChecked(int command_id) const {
  return true;
}

bool Textfield::IsCommandIdEnabled(int command_id) const {
  return Textfield::IsTextEditCommandEnabled(
      GetTextEditCommandFromMenuCommand(command_id, HasSelection()));
}

bool Textfield::GetAcceleratorForCommandId(int command_id,
                                           ui::Accelerator* accelerator) const {
  switch (command_id) {
    case IDS_APP_UNDO:
      *accelerator = ui::Accelerator(ui::VKEY_Z, kPlatformModifier);
      return true;

    case IDS_APP_CUT:
      *accelerator = ui::Accelerator(ui::VKEY_X, kPlatformModifier);
      return true;

    case IDS_APP_COPY:
      *accelerator = ui::Accelerator(ui::VKEY_C, kPlatformModifier);
      return true;

    case IDS_APP_PASTE:
      *accelerator = ui::Accelerator(ui::VKEY_V, kPlatformModifier);
      return true;

    case IDS_APP_SELECT_ALL:
      *accelerator = ui::Accelerator(ui::VKEY_A, kPlatformModifier);
      return true;

    default:
      return false;
  }
}

void Textfield::ExecuteCommand(int command_id, int event_flags) {
  Textfield::ExecuteTextEditCommand(
      GetTextEditCommandFromMenuCommand(command_id, HasSelection()));
}

////////////////////////////////////////////////////////////////////////////////
// Textfield, ui::TextInputClient overrides:

void Textfield::SetCompositionText(const ui::CompositionText& composition) {
  if (GetTextInputType() == ui::TEXT_INPUT_TYPE_NONE)
    return;

  OnBeforeUserAction();
  skip_input_method_cancel_composition_ = true;
  model_->SetCompositionText(composition);
  skip_input_method_cancel_composition_ = false;
  UpdateAfterChange(true, true);
  OnAfterUserAction();
}

void Textfield::ConfirmCompositionText() {
  if (!model_->HasCompositionText())
    return;

  OnBeforeUserAction();
  skip_input_method_cancel_composition_ = true;
  model_->ConfirmCompositionText();
  skip_input_method_cancel_composition_ = false;
  UpdateAfterChange(true, true);
  OnAfterUserAction();
}

void Textfield::ClearCompositionText() {
  if (!model_->HasCompositionText())
    return;

  OnBeforeUserAction();
  skip_input_method_cancel_composition_ = true;
  model_->CancelCompositionText();
  skip_input_method_cancel_composition_ = false;
  UpdateAfterChange(true, true);
  OnAfterUserAction();
}

void Textfield::InsertText(const base::string16& new_text) {
  // TODO(suzhe): Filter invalid characters.
  if (GetTextInputType() == ui::TEXT_INPUT_TYPE_NONE || new_text.empty())
    return;

  OnBeforeUserAction();
  skip_input_method_cancel_composition_ = true;
  model_->InsertText(new_text);
  skip_input_method_cancel_composition_ = false;
  UpdateAfterChange(true, true);
  OnAfterUserAction();
}

void Textfield::InsertChar(const ui::KeyEvent& event) {
  if (read_only()) {
    OnEditFailed();
    return;
  }

  // Filter out all control characters, including tab and new line characters,
  // and all characters with Alt modifier (and Search on ChromeOS, Ctrl on
  // Linux). But allow characters with the AltGr modifier. On Windows AltGr is
  // represented by Alt+Ctrl or Right Alt, and on Linux it's a different flag
  // that we don't care about.
  const base::char16 ch = event.GetCharacter();
  const bool should_insert_char = ((ch >= 0x20 && ch < 0x7F) || ch > 0x9F) &&
                                  !ui::IsSystemKeyModifier(event.flags()) &&
                                  !IsControlKeyModifier(event.flags());
  if (GetTextInputType() == ui::TEXT_INPUT_TYPE_NONE || !should_insert_char)
    return;

  DoInsertChar(ch);

  if (text_input_type_ == ui::TEXT_INPUT_TYPE_PASSWORD &&
      !GetPasswordRevealDuration().is_zero()) {
    const size_t change_offset = model_->GetCursorPosition();
    DCHECK_GT(change_offset, 0u);
    RevealPasswordChar(change_offset - 1);
  }
}

ui::TextInputType Textfield::GetTextInputType() const {
  if (read_only() || !enabled())
    return ui::TEXT_INPUT_TYPE_NONE;
  return text_input_type_;
}

ui::TextInputMode Textfield::GetTextInputMode() const {
  return ui::TEXT_INPUT_MODE_DEFAULT;
}

base::i18n::TextDirection Textfield::GetTextDirection() const {
  return GetRenderText()->GetDisplayTextDirection();
}

int Textfield::GetTextInputFlags() const {
  return text_input_flags_;
}

bool Textfield::CanComposeInline() const {
  return true;
}

gfx::Rect Textfield::GetCaretBounds() const {
  gfx::Rect rect = GetRenderText()->GetUpdatedCursorBounds();
  ConvertRectToScreen(this, &rect);
  return rect;
}

bool Textfield::GetCompositionCharacterBounds(uint32_t index,
                                              gfx::Rect* rect) const {
  DCHECK(rect);
  if (!HasCompositionText())
    return false;
  gfx::Range composition_range;
  model_->GetCompositionTextRange(&composition_range);
  DCHECK(!composition_range.is_empty());

  size_t text_index = composition_range.start() + index;
  if (composition_range.end() <= text_index)
    return false;
  gfx::RenderText* render_text = GetRenderText();
  if (!render_text->IsValidCursorIndex(text_index)) {
    text_index = render_text->IndexOfAdjacentGrapheme(
        text_index, gfx::CURSOR_BACKWARD);
  }
  if (text_index < composition_range.start())
    return false;
  const gfx::SelectionModel caret(text_index, gfx::CURSOR_BACKWARD);
  *rect = render_text->GetCursorBounds(caret, false);
  ConvertRectToScreen(this, rect);
  return true;
}

bool Textfield::HasCompositionText() const {
  return model_->HasCompositionText();
}

bool Textfield::GetTextRange(gfx::Range* range) const {
  if (!ImeEditingAllowed())
    return false;

  model_->GetTextRange(range);
  return true;
}

bool Textfield::GetCompositionTextRange(gfx::Range* range) const {
  if (!ImeEditingAllowed())
    return false;

  model_->GetCompositionTextRange(range);
  return true;
}

bool Textfield::GetSelectionRange(gfx::Range* range) const {
  if (!ImeEditingAllowed())
    return false;
  *range = GetRenderText()->selection();
  return true;
}

bool Textfield::SetSelectionRange(const gfx::Range& range) {
  if (!ImeEditingAllowed() || !range.IsValid())
    return false;
  OnBeforeUserAction();
  SelectRange(range);
  OnAfterUserAction();
  return true;
}

bool Textfield::DeleteRange(const gfx::Range& range) {
  if (!ImeEditingAllowed() || range.is_empty())
    return false;

  OnBeforeUserAction();
  model_->SelectRange(range);
  if (model_->HasSelection()) {
    model_->DeleteSelection();
    UpdateAfterChange(true, true);
  }
  OnAfterUserAction();
  return true;
}

bool Textfield::GetTextFromRange(const gfx::Range& range,
                                 base::string16* range_text) const {
  if (!ImeEditingAllowed() || !range.IsValid())
    return false;

  gfx::Range text_range;
  if (!GetTextRange(&text_range) || !text_range.Contains(range))
    return false;

  *range_text = model_->GetTextFromRange(range);
  return true;
}

void Textfield::OnInputMethodChanged() {}

bool Textfield::ChangeTextDirectionAndLayoutAlignment(
    base::i18n::TextDirection direction) {
  // Restore text directionality mode when the indicated direction matches the
  // current forced mode; otherwise, force the mode indicated. This helps users
  // manage BiDi text layout without getting stuck in forced LTR or RTL modes.
  const gfx::DirectionalityMode mode = direction == base::i18n::RIGHT_TO_LEFT ?
      gfx::DIRECTIONALITY_FORCE_RTL : gfx::DIRECTIONALITY_FORCE_LTR;
  if (mode == GetRenderText()->directionality_mode())
    GetRenderText()->SetDirectionalityMode(gfx::DIRECTIONALITY_FROM_TEXT);
  else
    GetRenderText()->SetDirectionalityMode(mode);
  SchedulePaint();
  return true;
}

void Textfield::ExtendSelectionAndDelete(size_t before, size_t after) {
  gfx::Range range = GetRenderText()->selection();
  DCHECK_GE(range.start(), before);

  range.set_start(range.start() - before);
  range.set_end(range.end() + after);
  gfx::Range text_range;
  if (GetTextRange(&text_range) && text_range.Contains(range))
    DeleteRange(range);
}

void Textfield::EnsureCaretNotInRect(const gfx::Rect& rect) {}

bool Textfield::IsTextEditCommandEnabled(ui::TextEditCommand command) const {
  base::string16 result;
  bool editable = !read_only();
  bool readable = text_input_type_ != ui::TEXT_INPUT_TYPE_PASSWORD;
  switch (command) {
    case ui::TextEditCommand::DELETE_BACKWARD:
    case ui::TextEditCommand::DELETE_FORWARD:
    case ui::TextEditCommand::DELETE_TO_BEGINNING_OF_LINE:
    case ui::TextEditCommand::DELETE_TO_BEGINNING_OF_PARAGRAPH:
    case ui::TextEditCommand::DELETE_TO_END_OF_LINE:
    case ui::TextEditCommand::DELETE_TO_END_OF_PARAGRAPH:
    case ui::TextEditCommand::DELETE_WORD_BACKWARD:
    case ui::TextEditCommand::DELETE_WORD_FORWARD:
      return editable;
    case ui::TextEditCommand::MOVE_BACKWARD:
    case ui::TextEditCommand::MOVE_BACKWARD_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_FORWARD:
    case ui::TextEditCommand::MOVE_FORWARD_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_LEFT:
    case ui::TextEditCommand::MOVE_LEFT_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_RIGHT:
    case ui::TextEditCommand::MOVE_RIGHT_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_TO_BEGINNING_OF_DOCUMENT:
    case ui::TextEditCommand::
        MOVE_TO_BEGINNING_OF_DOCUMENT_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_TO_BEGINNING_OF_LINE:
    case ui::TextEditCommand::MOVE_TO_BEGINNING_OF_LINE_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_TO_BEGINNING_OF_PARAGRAPH:
    case ui::TextEditCommand::
        MOVE_TO_BEGINNING_OF_PARAGRAPH_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_TO_END_OF_DOCUMENT:
    case ui::TextEditCommand::MOVE_TO_END_OF_DOCUMENT_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_TO_END_OF_LINE:
    case ui::TextEditCommand::MOVE_TO_END_OF_LINE_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_TO_END_OF_PARAGRAPH:
    case ui::TextEditCommand::MOVE_TO_END_OF_PARAGRAPH_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_PARAGRAPH_FORWARD_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_PARAGRAPH_BACKWARD_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_WORD_BACKWARD:
    case ui::TextEditCommand::MOVE_WORD_BACKWARD_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_WORD_FORWARD:
    case ui::TextEditCommand::MOVE_WORD_FORWARD_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_WORD_LEFT:
    case ui::TextEditCommand::MOVE_WORD_LEFT_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_WORD_RIGHT:
    case ui::TextEditCommand::MOVE_WORD_RIGHT_AND_MODIFY_SELECTION:
      return true;
    case ui::TextEditCommand::UNDO:
      return editable && model_->CanUndo();
    case ui::TextEditCommand::REDO:
      return editable && model_->CanRedo();
    case ui::TextEditCommand::CUT:
      return editable && readable && model_->HasSelection();
    case ui::TextEditCommand::COPY:
      return readable && model_->HasSelection();
    case ui::TextEditCommand::PASTE:
      ui::Clipboard::GetForCurrentThread()->ReadText(
          ui::CLIPBOARD_TYPE_COPY_PASTE, &result);
      return editable && !result.empty();
    case ui::TextEditCommand::SELECT_ALL:
      return !text().empty();
    case ui::TextEditCommand::TRANSPOSE:
      return editable && !model_->HasSelection() &&
             !model_->HasCompositionText();
    case ui::TextEditCommand::YANK:
      return editable;
    case ui::TextEditCommand::MOVE_DOWN:
    case ui::TextEditCommand::MOVE_DOWN_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_PAGE_DOWN:
    case ui::TextEditCommand::MOVE_PAGE_DOWN_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_PAGE_UP:
    case ui::TextEditCommand::MOVE_PAGE_UP_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_UP:
    case ui::TextEditCommand::MOVE_UP_AND_MODIFY_SELECTION:
// On Mac, the textfield should respond to Up/Down arrows keys and
// PageUp/PageDown.
#if defined(OS_MACOSX)
      return true;
#else
      return false;
#endif
    case ui::TextEditCommand::INSERT_TEXT:
    case ui::TextEditCommand::SET_MARK:
    case ui::TextEditCommand::UNSELECT:
    case ui::TextEditCommand::INVALID_COMMAND:
      return false;
  }
  NOTREACHED();
  return false;
}

void Textfield::SetTextEditCommandForNextKeyEvent(ui::TextEditCommand command) {
  DCHECK_EQ(ui::TextEditCommand::INVALID_COMMAND, scheduled_text_edit_command_);
  scheduled_text_edit_command_ = command;
}

////////////////////////////////////////////////////////////////////////////////
// Textfield, protected:

void Textfield::DoInsertChar(base::char16 ch) {
  OnBeforeUserAction();
  skip_input_method_cancel_composition_ = true;
  model_->InsertChar(ch);
  skip_input_method_cancel_composition_ = false;

  UpdateAfterChange(true, true);
  OnAfterUserAction();
}

gfx::RenderText* Textfield::GetRenderText() const {
  return model_->render_text();
}

gfx::Point Textfield::GetLastClickLocation() const {
  return selection_controller_.last_click_location();
}

base::string16 Textfield::GetSelectionClipboardText() const {
  base::string16 selection_clipboard_text;
  ui::Clipboard::GetForCurrentThread()->ReadText(
      ui::CLIPBOARD_TYPE_SELECTION, &selection_clipboard_text);
  return selection_clipboard_text;
}

void Textfield::ExecuteTextEditCommand(ui::TextEditCommand command) {
  DestroyTouchSelection();

  bool add_to_kill_buffer = false;

  // Some codepaths may bypass GetCommandForKeyEvent, so any selection-dependent
  // modifications of the command should happen here.
  switch (command) {
    case ui::TextEditCommand::DELETE_TO_BEGINNING_OF_LINE:
    case ui::TextEditCommand::DELETE_TO_BEGINNING_OF_PARAGRAPH:
    case ui::TextEditCommand::DELETE_TO_END_OF_LINE:
    case ui::TextEditCommand::DELETE_TO_END_OF_PARAGRAPH:
      add_to_kill_buffer = text_input_type_ != ui::TEXT_INPUT_TYPE_PASSWORD;
    // Fall through.
    case ui::TextEditCommand::DELETE_WORD_BACKWARD:
    case ui::TextEditCommand::DELETE_WORD_FORWARD:
      if (HasSelection())
        command = ui::TextEditCommand::DELETE_FORWARD;
      break;
    default:
      break;
  }

  // We only execute the commands enabled in Textfield::IsTextEditCommandEnabled
  // below. Hence don't do a virtual IsTextEditCommandEnabled call.
  if (!Textfield::IsTextEditCommandEnabled(command))
    return;

  bool text_changed = false;
  bool cursor_changed = false;
  bool rtl = GetTextDirection() == base::i18n::RIGHT_TO_LEFT;
  gfx::VisualCursorDirection begin = rtl ? gfx::CURSOR_RIGHT : gfx::CURSOR_LEFT;
  gfx::VisualCursorDirection end = rtl ? gfx::CURSOR_LEFT : gfx::CURSOR_RIGHT;
  gfx::SelectionModel selection_model = GetSelectionModel();

  OnBeforeUserAction();
  switch (command) {
    case ui::TextEditCommand::DELETE_BACKWARD:
      text_changed = cursor_changed = model_->Backspace(add_to_kill_buffer);
      break;
    case ui::TextEditCommand::DELETE_FORWARD:
      text_changed = cursor_changed = model_->Delete(add_to_kill_buffer);
      break;
    case ui::TextEditCommand::DELETE_TO_BEGINNING_OF_LINE:
    case ui::TextEditCommand::DELETE_TO_BEGINNING_OF_PARAGRAPH:
      model_->MoveCursor(gfx::LINE_BREAK, begin, gfx::SELECTION_RETAIN);
      text_changed = cursor_changed = model_->Backspace(add_to_kill_buffer);
      break;
    case ui::TextEditCommand::DELETE_TO_END_OF_LINE:
    case ui::TextEditCommand::DELETE_TO_END_OF_PARAGRAPH:
      model_->MoveCursor(gfx::LINE_BREAK, end, gfx::SELECTION_RETAIN);
      text_changed = cursor_changed = model_->Delete(add_to_kill_buffer);
      break;
    case ui::TextEditCommand::DELETE_WORD_BACKWARD:
      model_->MoveCursor(gfx::WORD_BREAK, begin, gfx::SELECTION_RETAIN);
      text_changed = cursor_changed = model_->Backspace(add_to_kill_buffer);
      break;
    case ui::TextEditCommand::DELETE_WORD_FORWARD:
      model_->MoveCursor(gfx::WORD_BREAK, end, gfx::SELECTION_RETAIN);
      text_changed = cursor_changed = model_->Delete(add_to_kill_buffer);
      break;
    case ui::TextEditCommand::MOVE_BACKWARD:
      model_->MoveCursor(gfx::CHARACTER_BREAK, begin, gfx::SELECTION_NONE);
      break;
    case ui::TextEditCommand::MOVE_BACKWARD_AND_MODIFY_SELECTION:
      model_->MoveCursor(gfx::CHARACTER_BREAK, begin, gfx::SELECTION_RETAIN);
      break;
    case ui::TextEditCommand::MOVE_FORWARD:
      model_->MoveCursor(gfx::CHARACTER_BREAK, end, gfx::SELECTION_NONE);
      break;
    case ui::TextEditCommand::MOVE_FORWARD_AND_MODIFY_SELECTION:
      model_->MoveCursor(gfx::CHARACTER_BREAK, end, gfx::SELECTION_RETAIN);
      break;
    case ui::TextEditCommand::MOVE_LEFT:
      model_->MoveCursor(gfx::CHARACTER_BREAK, gfx::CURSOR_LEFT,
                         gfx::SELECTION_NONE);
      break;
    case ui::TextEditCommand::MOVE_LEFT_AND_MODIFY_SELECTION:
      model_->MoveCursor(gfx::CHARACTER_BREAK, gfx::CURSOR_LEFT,
                         gfx::SELECTION_RETAIN);
      break;
    case ui::TextEditCommand::MOVE_RIGHT:
      model_->MoveCursor(gfx::CHARACTER_BREAK, gfx::CURSOR_RIGHT,
                         gfx::SELECTION_NONE);
      break;
    case ui::TextEditCommand::MOVE_RIGHT_AND_MODIFY_SELECTION:
      model_->MoveCursor(gfx::CHARACTER_BREAK, gfx::CURSOR_RIGHT,
                         gfx::SELECTION_RETAIN);
      break;
    case ui::TextEditCommand::MOVE_TO_BEGINNING_OF_DOCUMENT:
    case ui::TextEditCommand::MOVE_TO_BEGINNING_OF_LINE:
    case ui::TextEditCommand::MOVE_TO_BEGINNING_OF_PARAGRAPH:
    case ui::TextEditCommand::MOVE_UP:
    case ui::TextEditCommand::MOVE_PAGE_UP:
      model_->MoveCursor(gfx::LINE_BREAK, begin, gfx::SELECTION_NONE);
      break;
    case ui::TextEditCommand::
        MOVE_TO_BEGINNING_OF_DOCUMENT_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_TO_BEGINNING_OF_LINE_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::
        MOVE_TO_BEGINNING_OF_PARAGRAPH_AND_MODIFY_SELECTION:
      model_->MoveCursor(gfx::LINE_BREAK, begin, kLineSelectionBehavior);
      break;
    case ui::TextEditCommand::MOVE_PAGE_UP_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_UP_AND_MODIFY_SELECTION:
      model_->MoveCursor(gfx::LINE_BREAK, begin, gfx::SELECTION_RETAIN);
      break;
    case ui::TextEditCommand::MOVE_TO_END_OF_DOCUMENT:
    case ui::TextEditCommand::MOVE_TO_END_OF_LINE:
    case ui::TextEditCommand::MOVE_TO_END_OF_PARAGRAPH:
    case ui::TextEditCommand::MOVE_DOWN:
    case ui::TextEditCommand::MOVE_PAGE_DOWN:
      model_->MoveCursor(gfx::LINE_BREAK, end, gfx::SELECTION_NONE);
      break;
    case ui::TextEditCommand::MOVE_TO_END_OF_DOCUMENT_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_TO_END_OF_LINE_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_TO_END_OF_PARAGRAPH_AND_MODIFY_SELECTION:
      model_->MoveCursor(gfx::LINE_BREAK, end, kLineSelectionBehavior);
      break;
    case ui::TextEditCommand::MOVE_PAGE_DOWN_AND_MODIFY_SELECTION:
    case ui::TextEditCommand::MOVE_DOWN_AND_MODIFY_SELECTION:
      model_->MoveCursor(gfx::LINE_BREAK, end, gfx::SELECTION_RETAIN);
      break;
    case ui::TextEditCommand::MOVE_PARAGRAPH_BACKWARD_AND_MODIFY_SELECTION:
      model_->MoveCursor(gfx::LINE_BREAK, begin,
                         kMoveParagraphSelectionBehavior);
      break;
    case ui::TextEditCommand::MOVE_PARAGRAPH_FORWARD_AND_MODIFY_SELECTION:
      model_->MoveCursor(gfx::LINE_BREAK, end, kMoveParagraphSelectionBehavior);
      break;
    case ui::TextEditCommand::MOVE_WORD_BACKWARD:
      model_->MoveCursor(gfx::WORD_BREAK, begin, gfx::SELECTION_NONE);
      break;
    case ui::TextEditCommand::MOVE_WORD_BACKWARD_AND_MODIFY_SELECTION:
      model_->MoveCursor(gfx::WORD_BREAK, begin, kWordSelectionBehavior);
      break;
    case ui::TextEditCommand::MOVE_WORD_FORWARD:
      model_->MoveCursor(gfx::WORD_BREAK, end, gfx::SELECTION_NONE);
      break;
    case ui::TextEditCommand::MOVE_WORD_FORWARD_AND_MODIFY_SELECTION:
      model_->MoveCursor(gfx::WORD_BREAK, end, kWordSelectionBehavior);
      break;
    case ui::TextEditCommand::MOVE_WORD_LEFT:
      model_->MoveCursor(gfx::WORD_BREAK, gfx::CURSOR_LEFT,
                         gfx::SELECTION_NONE);
      break;
    case ui::TextEditCommand::MOVE_WORD_LEFT_AND_MODIFY_SELECTION:
      model_->MoveCursor(gfx::WORD_BREAK, gfx::CURSOR_LEFT,
                         kWordSelectionBehavior);
      break;
    case ui::TextEditCommand::MOVE_WORD_RIGHT:
      model_->MoveCursor(gfx::WORD_BREAK, gfx::CURSOR_RIGHT,
                         gfx::SELECTION_NONE);
      break;
    case ui::TextEditCommand::MOVE_WORD_RIGHT_AND_MODIFY_SELECTION:
      model_->MoveCursor(gfx::WORD_BREAK, gfx::CURSOR_RIGHT,
                         kWordSelectionBehavior);
      break;
    case ui::TextEditCommand::UNDO:
      text_changed = cursor_changed = model_->Undo();
      break;
    case ui::TextEditCommand::REDO:
      text_changed = cursor_changed = model_->Redo();
      break;
    case ui::TextEditCommand::CUT:
      text_changed = cursor_changed = Cut();
      break;
    case ui::TextEditCommand::COPY:
      Copy();
      break;
    case ui::TextEditCommand::PASTE:
      text_changed = cursor_changed = Paste();
      break;
    case ui::TextEditCommand::SELECT_ALL:
      SelectAll(false);
      break;
    case ui::TextEditCommand::TRANSPOSE:
      text_changed = cursor_changed = model_->Transpose();
      break;
    case ui::TextEditCommand::YANK:
      text_changed = cursor_changed = model_->Yank();
      break;
    case ui::TextEditCommand::INSERT_TEXT:
    case ui::TextEditCommand::SET_MARK:
    case ui::TextEditCommand::UNSELECT:
    case ui::TextEditCommand::INVALID_COMMAND:
      NOTREACHED();
      break;
  }

  cursor_changed |= GetSelectionModel() != selection_model;
  if (cursor_changed && HasSelection())
    UpdateSelectionClipboard();
  UpdateAfterChange(text_changed, cursor_changed);
  OnAfterUserAction();
}

////////////////////////////////////////////////////////////////////////////////
// Textfield, private:

////////////////////////////////////////////////////////////////////////////////
// Textfield, SelectionControllerDelegate overrides:

gfx::RenderText* Textfield::GetRenderTextForSelectionController() {
  return GetRenderText();
}

bool Textfield::IsReadOnly() const {
  return read_only();
}

bool Textfield::SupportsDrag() const {
  return true;
}

void Textfield::SetTextBeingDragged(bool value) {
  initiating_drag_ = value;
}

int Textfield::GetViewHeight() const {
  return height();
}

int Textfield::GetViewWidth() const {
  return width();
}

int Textfield::GetDragSelectionDelay() const {
  switch (ui::ScopedAnimationDurationScaleMode::duration_scale_mode()) {
    case ui::ScopedAnimationDurationScaleMode::NORMAL_DURATION:
      return 100;
    case ui::ScopedAnimationDurationScaleMode::FAST_DURATION:
      return 25;
    case ui::ScopedAnimationDurationScaleMode::SLOW_DURATION:
      return 400;
    case ui::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION:
      return 1;
    case ui::ScopedAnimationDurationScaleMode::ZERO_DURATION:
      return 0;
  }
  return 100;
}

void Textfield::OnBeforePointerAction() {
  OnBeforeUserAction();
  if (model_->HasCompositionText())
    model_->ConfirmCompositionText();
}

void Textfield::OnAfterPointerAction(bool text_changed,
                                     bool selection_changed) {
  OnAfterUserAction();
  UpdateAfterChange(text_changed, selection_changed);
}

bool Textfield::PasteSelectionClipboard() {
  DCHECK(performing_user_action_);
  DCHECK(!read_only());
  const base::string16 selection_clipboard_text = GetSelectionClipboardText();
  if (selection_clipboard_text.empty())
    return false;

  model_->InsertText(selection_clipboard_text);
  return true;
}

void Textfield::UpdateSelectionClipboard() {
#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
  if (text_input_type_ != ui::TEXT_INPUT_TYPE_PASSWORD) {
    ui::ScopedClipboardWriter(ui::CLIPBOARD_TYPE_SELECTION)
        .WriteText(GetSelectedText());
    if (controller_)
      controller_->OnAfterCutOrCopy(ui::CLIPBOARD_TYPE_SELECTION);
  }
#endif
}

void Textfield::UpdateBackgroundColor() {
  const SkColor color = GetBackgroundColor();
  if (ui::MaterialDesignController::IsSecondaryUiMaterial()) {
    set_background(Background::CreateBackgroundPainter(
        Painter::CreateSolidRoundRectPainter(
            color, FocusableBorder::kCornerRadiusDp)));
  } else {
    set_background(Background::CreateSolidBackground(color));
  }
  // Disable subpixel rendering when the background color is transparent
  // because it draws incorrect colors around the glyphs in that case.
  // See crbug.com/115198
  GetRenderText()->set_subpixel_rendering_suppressed(
      SkColorGetA(color) != SK_AlphaOPAQUE);
  SchedulePaint();
}

void Textfield::UpdateBorder() {
  auto border = base::MakeUnique<views::FocusableBorder>();
  if (invalid_)
    border->SetColorId(ui::NativeTheme::kColorId_AlertSeverityHigh);
  View::SetBorder(std::move(border));
}

void Textfield::UpdateAfterChange(bool text_changed, bool cursor_changed) {
  if (text_changed) {
    if (controller_)
      controller_->ContentsChanged(this, text());
    NotifyAccessibilityEvent(ui::AX_EVENT_TEXT_CHANGED, true);
  }
  if (cursor_changed) {
    UpdateCursorViewPosition();
    UpdateCursorVisibility();
    NotifyAccessibilityEvent(ui::AX_EVENT_TEXT_SELECTION_CHANGED, true);
  }
  if (text_changed || cursor_changed) {
    OnCaretBoundsChanged();
    SchedulePaint();
  }
}

void Textfield::UpdateCursorVisibility() {
  cursor_view_.SetVisible(ShouldShowCursor());
  if (ShouldBlinkCursor())
    StartBlinkingCursor();
  else
    StopBlinkingCursor();
}

void Textfield::UpdateCursorViewPosition() {
  gfx::Rect location(GetRenderText()->GetUpdatedCursorBounds());
  location.set_x(GetMirroredXForRect(location));
  cursor_view_.SetBoundsRect(location);
}

void Textfield::PaintTextAndCursor(gfx::Canvas* canvas) {
  TRACE_EVENT0("views", "Textfield::PaintTextAndCursor");
  canvas->Save();

  // Draw placeholder text if needed.
  gfx::RenderText* render_text = GetRenderText();
  if (text().empty() && !GetPlaceholderText().empty()) {
    canvas->DrawStringRect(GetPlaceholderText(), GetFontList(),
                           ui::MaterialDesignController::IsSecondaryUiMaterial()
                               ? SkColorSetA(GetTextColor(), 0x83)
                               : placeholder_text_color_,
                           render_text->display_rect());
  }

  render_text->Draw(canvas);

  // Draw the detached drop cursor that marks where the text will be dropped.
  if (drop_cursor_visible_) {
    canvas->FillRect(render_text->GetCursorBounds(drop_cursor_position_, true),
                     GetTextColor());
  }

  canvas->Restore();
}

void Textfield::MoveCursorTo(const gfx::Point& point, bool select) {
  if (model_->MoveCursorTo(point, select))
    UpdateAfterChange(false, true);
}

void Textfield::OnCaretBoundsChanged() {
  if (GetInputMethod())
    GetInputMethod()->OnCaretBoundsChanged(this);
  if (touch_selection_controller_)
    touch_selection_controller_->SelectionChanged();
}

void Textfield::OnBeforeUserAction() {
  DCHECK(!performing_user_action_);
  performing_user_action_ = true;
  if (controller_)
    controller_->OnBeforeUserAction(this);
}

void Textfield::OnAfterUserAction() {
  if (controller_)
    controller_->OnAfterUserAction(this);
  DCHECK(performing_user_action_);
  performing_user_action_ = false;
}

bool Textfield::Cut() {
  if (!read_only() && text_input_type_ != ui::TEXT_INPUT_TYPE_PASSWORD &&
      model_->Cut()) {
    if (controller_)
      controller_->OnAfterCutOrCopy(ui::CLIPBOARD_TYPE_COPY_PASTE);
    return true;
  }
  return false;
}

bool Textfield::Copy() {
  if (text_input_type_ != ui::TEXT_INPUT_TYPE_PASSWORD && model_->Copy()) {
    if (controller_)
      controller_->OnAfterCutOrCopy(ui::CLIPBOARD_TYPE_COPY_PASTE);
    return true;
  }
  return false;
}

bool Textfield::Paste() {
  if (!read_only() && model_->Paste()) {
    if (controller_)
      controller_->OnAfterPaste();
    return true;
  }
  return false;
}

void Textfield::UpdateContextMenu() {
  if (!context_menu_contents_.get()) {
    context_menu_contents_.reset(new ui::SimpleMenuModel(this));
    context_menu_contents_->AddItemWithStringId(IDS_APP_UNDO, IDS_APP_UNDO);
    context_menu_contents_->AddSeparator(ui::NORMAL_SEPARATOR);
    context_menu_contents_->AddItemWithStringId(IDS_APP_CUT, IDS_APP_CUT);
    context_menu_contents_->AddItemWithStringId(IDS_APP_COPY, IDS_APP_COPY);
    context_menu_contents_->AddItemWithStringId(IDS_APP_PASTE, IDS_APP_PASTE);
    context_menu_contents_->AddItemWithStringId(IDS_APP_DELETE, IDS_APP_DELETE);
    context_menu_contents_->AddSeparator(ui::NORMAL_SEPARATOR);
    context_menu_contents_->AddItemWithStringId(IDS_APP_SELECT_ALL,
                                                IDS_APP_SELECT_ALL);

    // If the controller adds menu commands, also override ExecuteCommand() and
    // IsCommandIdEnabled() as appropriate, for the commands added.
    if (controller_)
      controller_->UpdateContextMenu(context_menu_contents_.get());
  }
  context_menu_runner_.reset(new MenuRunner(context_menu_contents_.get(),
                                            MenuRunner::HAS_MNEMONICS |
                                                MenuRunner::CONTEXT_MENU |
                                                MenuRunner::ASYNC));
}

bool Textfield::ImeEditingAllowed() const {
  // Disallow input method editing of password fields.
  ui::TextInputType t = GetTextInputType();
  return (t != ui::TEXT_INPUT_TYPE_NONE && t != ui::TEXT_INPUT_TYPE_PASSWORD);
}

void Textfield::RevealPasswordChar(int index) {
  GetRenderText()->SetObscuredRevealIndex(index);
  SchedulePaint();

  if (index != -1) {
    password_reveal_timer_.Start(FROM_HERE, GetPasswordRevealDuration(),
        base::Bind(&Textfield::RevealPasswordChar,
                   weak_ptr_factory_.GetWeakPtr(), -1));
  }
}

void Textfield::CreateTouchSelectionControllerAndNotifyIt() {
  if (!HasFocus())
    return;

  if (!touch_selection_controller_) {
    touch_selection_controller_.reset(
        ui::TouchEditingControllerDeprecated::Create(this));
  }
  if (touch_selection_controller_)
    touch_selection_controller_->SelectionChanged();
}

void Textfield::OnEditFailed() {
  PlatformStyle::OnTextfieldEditFailed();
}

bool Textfield::ShouldShowCursor() const {
  return HasFocus() && !HasSelection() && enabled() && !read_only() &&
         !drop_cursor_visible_ && GetRenderText()->cursor_enabled();
}

bool Textfield::ShouldBlinkCursor() const {
  return ShouldShowCursor() && Textfield::GetCaretBlinkMs() != 0;
}

void Textfield::StartBlinkingCursor() {
  DCHECK(ShouldBlinkCursor());
  cursor_blink_timer_.Start(FROM_HERE, base::TimeDelta::FromMilliseconds(
                                           Textfield::GetCaretBlinkMs()),
                            this, &Textfield::OnCursorBlinkTimerFired);
}

void Textfield::StopBlinkingCursor() {
  cursor_blink_timer_.Stop();
}

void Textfield::OnCursorBlinkTimerFired() {
  DCHECK(ShouldBlinkCursor());
  cursor_view_.SetVisible(!cursor_view_.visible());
  UpdateCursorViewPosition();
}

}  // namespace views
