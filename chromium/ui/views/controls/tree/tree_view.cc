// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/tree/tree_view.h"

#include <algorithm>

#include "base/i18n/rtl.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/material_design/material_design_controller.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/skia_util.h"
#include "ui/native_theme/native_theme.h"
#include "ui/resources/grit/ui_resources.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/prefix_selector.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/tree/tree_view_controller.h"
#include "ui/views/resources/grit/views_resources.h"
#include "ui/views/style/platform_style.h"
#include "ui/views/vector_icons.h"

using ui::TreeModel;
using ui::TreeModelNode;

namespace views {

// Insets around the view.
static const int kHorizontalInset = 2;
static const int kVerticalInset = 2;
// Padding before/after the image.
static const int kImagePadding = 4;
// Size of the arrow region.
static const int kArrowRegionSize = 12;
// Padding around the text (on each side).
static const int kTextVerticalPadding = 3;
static const int kTextHorizontalPadding = 2;
// How much children are indented from their parent.
static const int kIndent = 20;

// static
const char TreeView::kViewClassName[] = "TreeView";

namespace {

// Returns the color id for the background of selected text. |has_focus|
// indicates if the tree has focus.
ui::NativeTheme::ColorId text_background_color_id(bool has_focus) {
  return has_focus ?
      ui::NativeTheme::kColorId_TreeSelectionBackgroundFocused :
      ui::NativeTheme::kColorId_TreeSelectionBackgroundUnfocused;
}

// Returns the color id for text. |has_focus| indicates if the tree has focus
// and |is_selected| is true if the item is selected.
ui::NativeTheme::ColorId text_color_id(bool has_focus, bool is_selected) {
  if (is_selected) {
    if (has_focus)
      return ui::NativeTheme::kColorId_TreeSelectedText;
    return ui::NativeTheme::kColorId_TreeSelectedTextUnfocused;
  }
  return ui::NativeTheme::kColorId_TreeText;
}

bool EventIsDoubleTapOrClick(const ui::LocatedEvent& event) {
  if (event.type() == ui::ET_GESTURE_TAP)
    return event.AsGestureEvent()->details().tap_count() == 2;
  return !!(event.flags() & ui::EF_IS_DOUBLE_CLICK);
}

}  // namespace

TreeView::TreeView()
    : model_(NULL),
      selected_node_(NULL),
      editing_(false),
      editor_(NULL),
      focus_manager_(NULL),
      auto_expand_children_(false),
      editable_(true),
      controller_(NULL),
      root_shown_(true),
      row_height_(font_list_.GetHeight() + kTextVerticalPadding * 2) {
  // Always focusable, even on Mac (consistent with NSOutlineView).
  SetFocusBehavior(FocusBehavior::ALWAYS);
  closed_icon_ = *ui::ResourceBundle::GetSharedInstance().GetImageNamed(
      (base::i18n::IsRTL() ? IDR_FOLDER_CLOSED_RTL
                           : IDR_FOLDER_CLOSED)).ToImageSkia();
  open_icon_ = *ui::ResourceBundle::GetSharedInstance().GetImageNamed(
      (base::i18n::IsRTL() ? IDR_FOLDER_OPEN_RTL
                           : IDR_FOLDER_OPEN)).ToImageSkia();
  text_offset_ = closed_icon_.width() + kImagePadding + kImagePadding +
      kArrowRegionSize;
}

TreeView::~TreeView() {
  if (model_)
    model_->RemoveObserver(this);

  if (GetInputMethod() && selector_.get()) {
    // TreeView should have been blurred before destroy.
    DCHECK(selector_.get() != GetInputMethod()->GetTextInputClient());
  }

  if (focus_manager_) {
    focus_manager_->RemoveFocusChangeListener(this);
    focus_manager_ = NULL;
  }
}

View* TreeView::CreateParentIfNecessary() {
  ScrollView* scroll_view = ScrollView::CreateScrollViewWithBorder();
  scroll_view->SetContents(this);
  return scroll_view;
}

void TreeView::SetModel(TreeModel* model) {
  if (model == model_)
    return;
  if (model_)
    model_->RemoveObserver(this);

  CancelEdit();

  model_ = model;
  selected_node_ = NULL;
  icons_.clear();
  if (model_) {
    model_->AddObserver(this);
    model_->GetIcons(&icons_);

    root_.DeleteAll();
    ConfigureInternalNode(model_->GetRoot(), &root_);
    LoadChildren(&root_);
    root_.set_is_expanded(true);
    if (root_shown_)
      selected_node_ = &root_;
    else if (root_.child_count())
      selected_node_ = root_.GetChild(0);
  }
  DrawnNodesChanged();
}

void TreeView::SetEditable(bool editable) {
  if (editable == editable_)
    return;
  editable_ = editable;
  CancelEdit();
}

void TreeView::StartEditing(TreeModelNode* node) {
  DCHECK(node);
  // Cancel the current edit.
  CancelEdit();
  // Make sure all ancestors are expanded.
  if (model_->GetParent(node))
    Expand(model_->GetParent(node));
  // Select the node, else if the user commits the edit the selection reverts.
  SetSelectedNode(node);
  if (GetSelectedNode() != node)
    return;  // Selection failed for some reason, don't start editing.
  DCHECK(!editing_);
  editing_ = true;
  if (!editor_) {
    editor_ = new Textfield;
    // Add the editor immediately as GetPreferredSize returns the wrong thing if
    // not parented.
    AddChildView(editor_);
    editor_->SetFontList(font_list_);
    empty_editor_size_ = editor_->GetPreferredSize();
    editor_->set_controller(this);
  }
  editor_->SetText(selected_node_->model_node()->GetTitle());
  LayoutEditor();
  editor_->SetVisible(true);
  SchedulePaintForNode(selected_node_);
  editor_->RequestFocus();
  editor_->SelectAll(false);

  // Listen for focus changes so that we can cancel editing.
  focus_manager_ = GetFocusManager();
  if (focus_manager_)
    focus_manager_->AddFocusChangeListener(this);

  // Accelerators to commit/cancel edit.
  AddAccelerator(ui::Accelerator(ui::VKEY_RETURN, ui::EF_NONE));
  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
}

void TreeView::CancelEdit() {
  if (!editing_)
    return;

  // WARNING: don't touch |selected_node_|, it may be bogus.

  editing_ = false;
  if (focus_manager_) {
    focus_manager_->RemoveFocusChangeListener(this);
    focus_manager_ = NULL;
  }
  editor_->SetVisible(false);
  SchedulePaint();

  RemoveAccelerator(ui::Accelerator(ui::VKEY_RETURN, ui::EF_NONE));
  RemoveAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
}

void TreeView::CommitEdit() {
  if (!editing_)
    return;

  DCHECK(selected_node_);
  const bool editor_has_focus = editor_->HasFocus();
  model_->SetTitle(GetSelectedNode(), editor_->text());
  CancelEdit();
  if (editor_has_focus)
    RequestFocus();
}

TreeModelNode* TreeView::GetEditingNode() {
  return editing_ ? selected_node_->model_node() : NULL;
}

void TreeView::SetSelectedNode(TreeModelNode* model_node) {
  if (editing_ || model_node != selected_node_)
    CancelEdit();
  if (model_node && model_->GetParent(model_node))
    Expand(model_->GetParent(model_node));
  if (model_node && model_node == root_.model_node() && !root_shown_)
    return;  // Ignore requests to select the root when not shown.
  InternalNode* node = model_node ? GetInternalNodeForModelNode(
      model_node, CREATE_IF_NOT_LOADED) : NULL;
  bool was_empty_selection = (selected_node_ == NULL);
  bool changed = (selected_node_ != node);
  if (changed) {
    SchedulePaintForNode(selected_node_);
    selected_node_ = node;
    if (selected_node_ == &root_ && !root_shown_)
      selected_node_ = NULL;
    if (selected_node_ && selected_node_ != &root_)
      Expand(model_->GetParent(selected_node_->model_node()));
    SchedulePaintForNode(selected_node_);
  }

  if (selected_node_)
    ScrollRectToVisible(GetForegroundBoundsForNode(selected_node_));

  // Notify controller if the old selection was empty to handle the case of
  // remove explicitly resetting selected_node_ before invoking this.
  if (controller_ && (changed || was_empty_selection))
    controller_->OnTreeViewSelectionChanged(this);

  if (changed) {
    NotifyAccessibilityEvent(ui::AX_EVENT_TEXT_CHANGED, true);
    NotifyAccessibilityEvent(ui::AX_EVENT_SELECTION, true);
  }
}

TreeModelNode* TreeView::GetSelectedNode() {
  return selected_node_ ? selected_node_->model_node() : NULL;
}

void TreeView::Collapse(ui::TreeModelNode* model_node) {
  // Don't collapse the root if the root isn't shown, otherwise nothing is
  // displayed.
  if (model_node == root_.model_node() && !root_shown_)
    return;
  InternalNode* node =
      GetInternalNodeForModelNode(model_node, DONT_CREATE_IF_NOT_LOADED);
  if (!node)
    return;
  bool was_expanded = IsExpanded(model_node);
  if (node->is_expanded()) {
    if (selected_node_ && selected_node_->HasAncestor(node))
      SetSelectedNode(model_node);
    node->set_is_expanded(false);
  }
  if (was_expanded)
    DrawnNodesChanged();
}

void TreeView::Expand(TreeModelNode* node) {
  if (ExpandImpl(node))
    DrawnNodesChanged();
  // TODO: need to support auto_expand_children_.
}

void TreeView::ExpandAll(TreeModelNode* node) {
  DCHECK(node);
  // Expand the node.
  bool expanded_at_least_one = ExpandImpl(node);
  // And recursively expand all the children.
  for (int i = model_->GetChildCount(node) - 1; i >= 0; --i) {
    TreeModelNode* child = model_->GetChild(node, i);
    if (ExpandImpl(child))
      expanded_at_least_one = true;
  }
  if (expanded_at_least_one)
    DrawnNodesChanged();
}

bool TreeView::IsExpanded(TreeModelNode* model_node) {
  if (!model_node) {
    // NULL check primarily for convenience for uses in this class so don't have
    // to add NULL checks every where we look up the parent.
    return true;
  }
  InternalNode* node = GetInternalNodeForModelNode(
      model_node, DONT_CREATE_IF_NOT_LOADED);
  if (!node)
    return false;

  while (node) {
    if (!node->is_expanded())
      return false;
    node = node->parent();
  }
  return true;
}

void TreeView::SetRootShown(bool root_shown) {
  if (root_shown_ == root_shown)
    return;
  root_shown_ = root_shown;
  if (!root_shown_ && selected_node_ == &root_) {
    if (model_->GetChildCount(root_.model_node()))
      SetSelectedNode(model_->GetChild(root_.model_node(), 0));
    else
      SetSelectedNode(NULL);
  }
  DrawnNodesChanged();
}

ui::TreeModelNode* TreeView::GetNodeForRow(int row) {
  int depth = 0;
  InternalNode* node = GetNodeByRow(row, &depth);
  return node ? node->model_node() : NULL;
}

int TreeView::GetRowForNode(ui::TreeModelNode* node) {
  InternalNode* internal_node =
      GetInternalNodeForModelNode(node, DONT_CREATE_IF_NOT_LOADED);
  if (!internal_node)
    return -1;
  int depth = 0;
  return GetRowForInternalNode(internal_node, &depth);
}

void TreeView::Layout() {
  int width = preferred_size_.width();
  int height = preferred_size_.height();
  if (parent()) {
    width = std::max(parent()->width(), width);
    height = std::max(parent()->height(), height);
  }
  SetBounds(x(), y(), width, height);
  LayoutEditor();
}

gfx::Size TreeView::GetPreferredSize() const {
  return preferred_size_;
}

bool TreeView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_RETURN) {
    CommitEdit();
  } else {
    DCHECK_EQ(ui::VKEY_ESCAPE, accelerator.key_code());
    CancelEdit();
    RequestFocus();
  }
  return true;
}

bool TreeView::OnMousePressed(const ui::MouseEvent& event) {
  return OnClickOrTap(event);
}

void TreeView::OnGestureEvent(ui::GestureEvent* event) {
  if (event->type() == ui::ET_GESTURE_TAP) {
    if (OnClickOrTap(*event))
      event->SetHandled();
  }
}

void TreeView::ShowContextMenu(const gfx::Point& p,
                               ui::MenuSourceType source_type) {
  if (!model_)
    return;
  if (source_type == ui::MENU_SOURCE_MOUSE) {
    // Only invoke View's implementation (which notifies the
    // ContextMenuController) if over a node.
    gfx::Point local_point(p);
    ConvertPointFromScreen(this, &local_point);
    if (!GetNodeAtPoint(local_point))
      return;
  }
  View::ShowContextMenu(p, source_type);
}

void TreeView::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  node_data->role = ui::AX_ROLE_TREE;
  node_data->AddStateFlag(ui::AX_STATE_READ_ONLY);
  if (!selected_node_)
    return;

  // Get selected item info.
  node_data->role = ui::AX_ROLE_TREE_ITEM;
  node_data->SetName(selected_node_->model_node()->GetTitle());
}

const char* TreeView::GetClassName() const {
  return kViewClassName;
}

void TreeView::TreeNodesAdded(TreeModel* model,
                              TreeModelNode* parent,
                              int start,
                              int count) {
  InternalNode* parent_node =
      GetInternalNodeForModelNode(parent, DONT_CREATE_IF_NOT_LOADED);
  if (!parent_node || !parent_node->loaded_children())
    return;
  for (int i = 0; i < count; ++i) {
    std::unique_ptr<InternalNode> child = base::MakeUnique<InternalNode>();
    ConfigureInternalNode(model_->GetChild(parent, start + i), child.get());
    parent_node->Add(std::move(child), start + i);
  }
  if (IsExpanded(parent))
    DrawnNodesChanged();
}

void TreeView::TreeNodesRemoved(TreeModel* model,
                                TreeModelNode* parent,
                                int start,
                                int count) {
  InternalNode* parent_node =
      GetInternalNodeForModelNode(parent, DONT_CREATE_IF_NOT_LOADED);
  if (!parent_node || !parent_node->loaded_children())
    return;
  bool reset_selection = false;
  for (int i = 0; i < count; ++i) {
    InternalNode* child_removing = parent_node->GetChild(start);
    if (selected_node_ && selected_node_->HasAncestor(child_removing))
      reset_selection = true;
    parent_node->Remove(child_removing);
  }
  if (reset_selection) {
    // selected_node_ is no longer valid (at the time we enter this function
    // its model_node() is likely deleted). Explicitly NULL out the field
    // rather than invoking SetSelectedNode() otherwise, we'll try and use a
    // deleted value.
    selected_node_ = NULL;
    TreeModelNode* to_select = parent;
    if (parent == root_.model_node() && !root_shown_) {
      to_select = model_->GetChildCount(parent) > 0 ?
          model_->GetChild(parent, 0) : NULL;
    }
    SetSelectedNode(to_select);
  }
  if (IsExpanded(parent))
    DrawnNodesChanged();
}

void TreeView::TreeNodeChanged(TreeModel* model, TreeModelNode* model_node) {
  InternalNode* node =
      GetInternalNodeForModelNode(model_node, DONT_CREATE_IF_NOT_LOADED);
  if (!node)
    return;
  int old_width = node->text_width();
  UpdateNodeTextWidth(node);
  if (old_width != node->text_width() &&
      ((node == &root_ && root_shown_) ||
       (node != &root_ && IsExpanded(node->parent()->model_node())))) {
    DrawnNodesChanged();
  }
}

void TreeView::ContentsChanged(Textfield* sender,
                               const base::string16& new_contents) {
}

bool TreeView::HandleKeyEvent(Textfield* sender,
                              const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::ET_KEY_PRESSED)
    return false;

  switch (key_event.key_code()) {
    case ui::VKEY_RETURN:
      CommitEdit();
      return true;

    case ui::VKEY_ESCAPE:
      CancelEdit();
      RequestFocus();
      return true;

    default:
      return false;
  }
}

void TreeView::OnWillChangeFocus(View* focused_before, View* focused_now) {
}

void TreeView::OnDidChangeFocus(View* focused_before, View* focused_now) {
  CommitEdit();
}

int TreeView::GetRowCount() {
  int row_count = root_.NumExpandedNodes();
  if (!root_shown_)
    row_count--;
  return row_count;
}

int TreeView::GetSelectedRow() {
  ui::TreeModelNode* model_node = GetSelectedNode();
  return model_node ? GetRowForNode(model_node) : -1;
}

void TreeView::SetSelectedRow(int row) {
  SetSelectedNode(GetNodeForRow(row));
}

base::string16 TreeView::GetTextForRow(int row) {
  return GetNodeForRow(row)->GetTitle();
}

gfx::Point TreeView::GetKeyboardContextMenuLocation() {
  int y = height() / 2;
  if (selected_node_) {
    gfx::Rect node_bounds(GetForegroundBoundsForNode(selected_node_));
    gfx::Rect vis_bounds(GetVisibleBounds());
    if (node_bounds.y() >= vis_bounds.y() &&
        node_bounds.y() < vis_bounds.bottom()) {
      y = node_bounds.y();
    }
  }
  gfx::Point screen_loc(0, y);
  if (base::i18n::IsRTL())
    screen_loc.set_x(width());
  ConvertPointToScreen(this, &screen_loc);
  return screen_loc;
}

bool TreeView::OnKeyPressed(const ui::KeyEvent& event) {
  if (!HasFocus())
    return false;

  switch (event.key_code()) {
    case ui::VKEY_F2:
      if (!editing_) {
        TreeModelNode* selected_node = GetSelectedNode();
        if (selected_node && (!controller_ ||
                              controller_->CanEdit(this, selected_node))) {
          StartEditing(selected_node);
        }
      }
      return true;

    case ui::VKEY_UP:
    case ui::VKEY_DOWN:
      IncrementSelection(event.key_code() == ui::VKEY_UP ?
                         INCREMENT_PREVIOUS : INCREMENT_NEXT);
      return true;

    case ui::VKEY_LEFT:
      if (base::i18n::IsRTL())
        ExpandOrSelectChild();
      else
        CollapseOrSelectParent();
      return true;

    case ui::VKEY_RIGHT:
      if (base::i18n::IsRTL())
        CollapseOrSelectParent();
      else
        ExpandOrSelectChild();
      return true;

    default:
      break;
  }
  return false;
}

void TreeView::OnPaint(gfx::Canvas* canvas) {
  // Don't invoke View::OnPaint so that we can render our own focus border.
  canvas->DrawColor(GetNativeTheme()->GetSystemColor(
                        ui::NativeTheme::kColorId_TreeBackground));

  int min_y, max_y;
  {
    SkRect sk_clip_rect;
    if (canvas->sk_canvas()->getLocalClipBounds(&sk_clip_rect)) {
      // Pixels partially inside the clip rect should be included.
      gfx::Rect clip_rect = gfx::ToEnclosingRect(
          gfx::SkRectToRectF(sk_clip_rect));
      min_y = clip_rect.y();
      max_y = clip_rect.bottom();
    } else {
      gfx::Rect vis_bounds = GetVisibleBounds();
      min_y = vis_bounds.y();
      max_y = vis_bounds.bottom();
    }
  }

  int min_row = std::max(0, (min_y - kVerticalInset) / row_height_);
  int max_row = (max_y - kVerticalInset) / row_height_;
  if ((max_y - kVerticalInset) % row_height_ != 0)
    max_row++;
  int current_row = root_row();
  PaintRows(canvas, min_row, max_row, &root_, root_depth(), &current_row);
}

void TreeView::OnFocus() {
  if (GetInputMethod())
    GetInputMethod()->SetFocusedTextInputClient(GetPrefixSelector());
  View::OnFocus();
  SchedulePaintForNode(selected_node_);

  // Notify the InputMethod so that it knows to query the TextInputClient.
  if (GetInputMethod())
    GetInputMethod()->OnCaretBoundsChanged(GetPrefixSelector());

  SetHasFocusIndicator(true);
}

void TreeView::OnBlur() {
  if (GetInputMethod())
    GetInputMethod()->DetachTextInputClient(GetPrefixSelector());
  SchedulePaintForNode(selected_node_);
  if (selector_)
    selector_->OnViewBlur();
  SetHasFocusIndicator(false);
}

bool TreeView::OnClickOrTap(const ui::LocatedEvent& event) {
  CommitEdit();
  RequestFocus();

  InternalNode* node = GetNodeAtPoint(event.location());
  if (!node)
    return true;

  bool hits_arrow = IsPointInExpandControl(node, event.location());
  if (!hits_arrow)
    SetSelectedNode(node->model_node());

  if (hits_arrow || EventIsDoubleTapOrClick(event)) {
    if (node->is_expanded())
      Collapse(node->model_node());
    else
      Expand(node->model_node());
  }
  return true;
}

void TreeView::LoadChildren(InternalNode* node) {
  DCHECK_EQ(0, node->child_count());
  DCHECK(!node->loaded_children());
  node->set_loaded_children(true);
  for (int i = 0, child_count = model_->GetChildCount(node->model_node());
       i < child_count; ++i) {
    std::unique_ptr<InternalNode> child = base::MakeUnique<InternalNode>();
    ConfigureInternalNode(model_->GetChild(node->model_node(), i), child.get());
    node->Add(std::move(child), node->child_count());
  }
}

void TreeView::ConfigureInternalNode(TreeModelNode* model_node,
                                     InternalNode* node) {
  node->Reset(model_node);
  UpdateNodeTextWidth(node);
}

void TreeView::UpdateNodeTextWidth(InternalNode* node) {
  int width = 0, height = 0;
  gfx::Canvas::SizeStringInt(node->model_node()->GetTitle(), font_list_,
                             &width, &height, 0, gfx::Canvas::NO_ELLIPSIS);
  node->set_text_width(width);
}

void TreeView::DrawnNodesChanged() {
  UpdatePreferredSize();
  PreferredSizeChanged();
  SchedulePaint();
}

void TreeView::UpdatePreferredSize() {
  preferred_size_ = gfx::Size();
  if (!model_)
    return;

  preferred_size_.SetSize(
      root_.GetMaxWidth(text_offset_, root_shown_ ? 1 : 0) +
      kTextHorizontalPadding * 2,
      row_height_ * GetRowCount() + kVerticalInset * 2);
}

void TreeView::LayoutEditor() {
  if (!editing_)
    return;

  DCHECK(selected_node_);
  // Position the editor so that its text aligns with the text we drew.
  gfx::Rect row_bounds = GetForegroundBoundsForNode(selected_node_);
  row_bounds.set_x(
      GetMirroredXWithWidthInView(row_bounds.x(), row_bounds.width()));
  row_bounds.set_x(row_bounds.x() + text_offset_);
  row_bounds.set_width(row_bounds.width() - text_offset_);
  row_bounds.Inset(kTextHorizontalPadding, kTextVerticalPadding);
  row_bounds.Inset(-empty_editor_size_.width() / 2,
                   -(empty_editor_size_.height() - font_list_.GetHeight()) / 2);
  // Give a little extra space for editing.
  row_bounds.set_width(row_bounds.width() + 50);
  editor_->SetBoundsRect(row_bounds);
  editor_->Layout();
}

void TreeView::SchedulePaintForNode(InternalNode* node) {
  if (!node)
    return;  // Explicitly allow NULL to be passed in.
  SchedulePaintInRect(GetBackgroundBoundsForNode(node));
}

void TreeView::PaintRows(gfx::Canvas* canvas,
                         int min_row,
                         int max_row,
                         InternalNode* node,
                         int depth,
                         int* row) {
  if (*row >= max_row)
    return;

  if (*row >= min_row && *row < max_row)
    PaintRow(canvas, node, *row, depth);
  (*row)++;
  if (!node->is_expanded())
    return;
  depth++;
  for (int i = 0; i < node->child_count() && *row < max_row; ++i)
    PaintRows(canvas, min_row, max_row, node->GetChild(i), depth, row);
}

void TreeView::PaintRow(gfx::Canvas* canvas,
                        InternalNode* node,
                        int row,
                        int depth) {
  gfx::Rect bounds(GetForegroundBoundsForNodeImpl(node, row, depth));
  const SkColor selected_row_bg_color = GetNativeTheme()->GetSystemColor(
      text_background_color_id(HasFocus() || editing_));

  // Paint the row background.
  if (PlatformStyle::kTreeViewSelectionPaintsEntireRow &&
      selected_node_ == node) {
    canvas->FillRect(GetBackgroundBoundsForNode(node), selected_row_bg_color);
  }

  if (model_->GetChildCount(node->model_node()))
    PaintExpandControl(canvas, bounds, node->is_expanded());

  // Paint the icon.
  gfx::ImageSkia icon;
  int icon_index = model_->GetIconIndex(node->model_node());
  if (icon_index != -1)
    icon = icons_[icon_index];
  else if (node == selected_node_)
    icon = open_icon_;
  else
    icon = closed_icon_;
  int icon_x = kArrowRegionSize + kImagePadding +
               (open_icon_.width() - icon.width()) / 2;
  if (base::i18n::IsRTL())
    icon_x = bounds.right() - icon_x - open_icon_.width();
  else
    icon_x += bounds.x();
  canvas->DrawImageInt(
      icon, icon_x,
      bounds.y() + (bounds.height() - icon.height()) / 2);

  // Paint the text background and text. In edit mode, the selected node is a
  // separate editing control, so it does not need to be painted here.
  if (editing_ && selected_node_ == node)
    return;

  gfx::Rect text_bounds(GetTextBoundsForNode(node));
  if (base::i18n::IsRTL())
    text_bounds.set_x(bounds.x());

  // Paint the background on the selected row.
  if (!PlatformStyle::kTreeViewSelectionPaintsEntireRow &&
      node == selected_node_) {
    canvas->FillRect(text_bounds, selected_row_bg_color);
  }

  // Paint the text.
  const ui::NativeTheme::ColorId color_id =
      text_color_id(HasFocus(), node == selected_node_);
  const gfx::Rect internal_bounds(
      text_bounds.x() + kTextHorizontalPadding,
      text_bounds.y() + kTextVerticalPadding,
      text_bounds.width() - kTextHorizontalPadding * 2,
      text_bounds.height() - kTextVerticalPadding * 2);
  canvas->DrawStringRect(node->model_node()->GetTitle(), font_list_,
                         GetNativeTheme()->GetSystemColor(color_id),
                         internal_bounds);
}

void TreeView::PaintExpandControl(gfx::Canvas* canvas,
                                  const gfx::Rect& node_bounds,
                                  bool expanded) {
  gfx::ImageSkia arrow = gfx::CreateVectorIcon(
      kSubmenuArrowIcon,
      color_utils::DeriveDefaultIconColor(
          GetNativeTheme()->GetSystemColor(text_color_id(false, false))));
  if (expanded) {
    arrow = gfx::ImageSkiaOperations::CreateRotatedImage(
        arrow, base::i18n::IsRTL() ? SkBitmapOperations::ROTATION_270_CW
                                   : SkBitmapOperations::ROTATION_90_CW);
  }
  gfx::Rect arrow_bounds = node_bounds;
  arrow_bounds.Inset(gfx::Insets((node_bounds.height() - arrow.height()) / 2,
                                 (kArrowRegionSize - arrow.width()) / 2));
  canvas->DrawImageInt(arrow, base::i18n::IsRTL()
                                  ? arrow_bounds.right() - arrow.width()
                                  : arrow_bounds.x(),
                       arrow_bounds.y());
}

TreeView::InternalNode* TreeView::GetInternalNodeForModelNode(
    ui::TreeModelNode* model_node,
    GetInternalNodeCreateType create_type) {
  if (model_node == root_.model_node())
    return &root_;
  InternalNode* parent_internal_node =
      GetInternalNodeForModelNode(model_->GetParent(model_node), create_type);
  if (!parent_internal_node)
    return NULL;
  if (!parent_internal_node->loaded_children()) {
    if (create_type == DONT_CREATE_IF_NOT_LOADED)
      return NULL;
    LoadChildren(parent_internal_node);
  }
  return parent_internal_node->GetChild(
      model_->GetIndexOf(parent_internal_node->model_node(), model_node));
}

gfx::Rect TreeView::GetBackgroundBoundsForNode(InternalNode* node) {
  if (!PlatformStyle::kTreeViewSelectionPaintsEntireRow)
    return GetForegroundBoundsForNode(node);

  int row, ignored_depth;
  row = GetRowForInternalNode(node, &ignored_depth);
  return gfx::Rect(bounds().x(), row * row_height_ + kVerticalInset,
                   bounds().width(), row_height_);
}

gfx::Rect TreeView::GetForegroundBoundsForNode(InternalNode* node) {
  int row, depth;
  row = GetRowForInternalNode(node, &depth);
  return GetForegroundBoundsForNodeImpl(node, row, depth);
}

gfx::Rect TreeView::GetTextBoundsForNode(InternalNode* node) {
  gfx::Rect bounds(GetForegroundBoundsForNode(node));
  bounds.Inset(text_offset_, 0, 0, 0);
  return bounds;
}

gfx::Rect TreeView::GetForegroundBoundsForNodeImpl(InternalNode* node,
                                                   int row,
                                                   int depth) {
  gfx::Rect rect(depth * kIndent + kHorizontalInset,
                 row * row_height_ + kVerticalInset,
                 text_offset_ + node->text_width() +
                 kTextHorizontalPadding * 2,
                 row_height_);
  rect.set_x(GetMirroredXWithWidthInView(rect.x(), rect.width()));
  return rect;
}

int TreeView::GetRowForInternalNode(InternalNode* node, int* depth) {
  DCHECK(!node->parent() || IsExpanded(node->parent()->model_node()));
  *depth = -1;
  int row = -1;
  InternalNode* tmp_node = node;
  while (tmp_node->parent()) {
    int index_in_parent = tmp_node->parent()->GetIndexOf(tmp_node);
    (*depth)++;
    row++;  // For node.
    for (int i = 0; i < index_in_parent; ++i)
      row += tmp_node->parent()->GetChild(i)->NumExpandedNodes();
    tmp_node = tmp_node->parent();
  }
  if (root_shown_) {
    (*depth)++;
    row++;
  }
  return row;
}

TreeView::InternalNode* TreeView::GetNodeAtPoint(const gfx::Point& point) {
  int row = (point.y() - kVerticalInset) / row_height_;
  int depth = -1;
  InternalNode* node = GetNodeByRow(row, &depth);
  if (!node)
    return nullptr;

  // If the entire row gets a selected background, clicking anywhere in the row
  // serves to hit this node.
  if (PlatformStyle::kTreeViewSelectionPaintsEntireRow)
    return node;
  gfx::Rect bounds(GetForegroundBoundsForNodeImpl(node, row, depth));
  return bounds.Contains(point) ? node : nullptr;
}

TreeView::InternalNode* TreeView::GetNodeByRow(int row, int* depth) {
  int current_row = root_row();
  *depth = 0;
  return GetNodeByRowImpl(&root_, row, root_depth(), &current_row, depth);
}

TreeView::InternalNode* TreeView::GetNodeByRowImpl(InternalNode* node,
                                                   int target_row,
                                                   int current_depth,
                                                   int* current_row,
                                                   int* node_depth) {
  if (*current_row == target_row) {
    *node_depth = current_depth;
    return node;
  }
  (*current_row)++;
  if (node->is_expanded()) {
    current_depth++;
    for (int i = 0; i < node->child_count(); ++i) {
      InternalNode* result = GetNodeByRowImpl(
          node->GetChild(i), target_row, current_depth, current_row,
          node_depth);
      if (result)
        return result;
    }
  }
  return NULL;
}

void TreeView::IncrementSelection(IncrementType type) {
  if (!model_)
    return;

  if (!GetSelectedNode()) {
    // If nothing is selected select the first or last node.
    if (!root_.child_count())
      return;
    if (type == INCREMENT_PREVIOUS) {
      int row_count = GetRowCount();
      int depth = 0;
      DCHECK(row_count);
      InternalNode* node = GetNodeByRow(row_count - 1, &depth);
      SetSelectedNode(node->model_node());
    } else if (root_shown_) {
      SetSelectedNode(root_.model_node());
    } else {
      SetSelectedNode(root_.GetChild(0)->model_node());
    }
    return;
  }

  int depth = 0;
  int delta = type == INCREMENT_PREVIOUS ? -1 : 1;
  int row = GetRowForInternalNode(selected_node_, &depth);
  int new_row = std::min(GetRowCount() - 1, std::max(0, row + delta));
  if (new_row == row)
    return;  // At the end/beginning.
  SetSelectedNode(GetNodeByRow(new_row, &depth)->model_node());
}

void TreeView::CollapseOrSelectParent() {
  if (selected_node_) {
    if (selected_node_->is_expanded())
      Collapse(selected_node_->model_node());
    else if (selected_node_->parent())
      SetSelectedNode(selected_node_->parent()->model_node());
  }
}

void TreeView::ExpandOrSelectChild() {
  if (selected_node_) {
    if (!selected_node_->is_expanded())
      Expand(selected_node_->model_node());
    else if (selected_node_->child_count())
      SetSelectedNode(selected_node_->GetChild(0)->model_node());
  }
}

bool TreeView::ExpandImpl(TreeModelNode* model_node) {
  TreeModelNode* parent = model_->GetParent(model_node);
  if (!parent) {
    // Node should be the root.
    DCHECK_EQ(root_.model_node(), model_node);
    bool was_expanded = root_.is_expanded();
    root_.set_is_expanded(true);
    return !was_expanded;
  }

  // Expand all the parents.
  bool return_value = ExpandImpl(parent);
  InternalNode* internal_node =
      GetInternalNodeForModelNode(model_node, CREATE_IF_NOT_LOADED);
  DCHECK(internal_node);
  if (!internal_node->is_expanded()) {
    if (!internal_node->loaded_children())
      LoadChildren(internal_node);
    internal_node->set_is_expanded(true);
    return_value = true;
  }
  return return_value;
}

PrefixSelector* TreeView::GetPrefixSelector() {
  if (!selector_)
    selector_.reset(new PrefixSelector(this, this));
  return selector_.get();
}

bool TreeView::IsPointInExpandControl(InternalNode* node,
                                      const gfx::Point& point) {
  if (!model_->GetChildCount(node->model_node()))
    return false;

  int depth = -1;
  int row = GetRowForInternalNode(node, &depth);

  int arrow_dx = depth * kIndent + kHorizontalInset;
  gfx::Rect arrow_bounds(bounds().x() + arrow_dx,
                         row * row_height_ + kVerticalInset, kArrowRegionSize,
                         row_height_);
  if (base::i18n::IsRTL())
    arrow_bounds.set_x(bounds().width() - arrow_dx - kArrowRegionSize);
  return arrow_bounds.Contains(point);
}

void TreeView::SetHasFocusIndicator(bool shows) {
  // If this View is the grandchild of a ScrollView, use the grandparent
  // ScrollView for the focus ring instead of this View so that the focus ring
  // won't be scrolled.
  ScrollView* scroll_view = ScrollView::GetScrollViewForContents(this);
  if (scroll_view)
    scroll_view->SetHasFocusIndicator(shows);
}

// InternalNode ----------------------------------------------------------------

TreeView::InternalNode::InternalNode()
    : model_node_(NULL),
      loaded_children_(false),
      is_expanded_(false),
      text_width_(0) {
}

TreeView::InternalNode::~InternalNode() {
}

void TreeView::InternalNode::Reset(ui::TreeModelNode* node) {
  model_node_ = node;
  loaded_children_ = false;
  is_expanded_ = false;
  text_width_ = 0;
}

int TreeView::InternalNode::NumExpandedNodes() const {
  int result = 1;  // For this.
  if (!is_expanded_)
    return result;
  for (int i = 0; i < child_count(); ++i)
    result += GetChild(i)->NumExpandedNodes();
  return result;
}

int TreeView::InternalNode::GetMaxWidth(int indent, int depth) {
  int max_width = text_width_ + indent * depth;
  if (!is_expanded_)
    return max_width;
  for (int i = 0; i < child_count(); ++i) {
    max_width = std::max(max_width,
                         GetChild(i)->GetMaxWidth(indent, depth + 1));
  }
  return max_width;
}

}  // namespace views
