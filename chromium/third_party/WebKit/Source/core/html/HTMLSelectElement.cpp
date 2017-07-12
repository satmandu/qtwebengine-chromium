/*
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2009, 2010, 2011 Apple Inc. All rights
 * reserved.
 *           (C) 2006 Alexey Proskuryakov (ap@nypop.com)
 * Copyright (C) 2010 Google Inc. All rights reserved.
 * Copyright (C) 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "core/html/HTMLSelectElement.h"

#include "bindings/core/v8/ExceptionMessages.h"
#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/HTMLElementOrLong.h"
#include "bindings/core/v8/HTMLOptionElementOrHTMLOptGroupElement.h"
#include "core/HTMLNames.h"
#include "core/dom/AXObjectCache.h"
#include "core/dom/Attribute.h"
#include "core/dom/ElementTraversal.h"
#include "core/dom/MutationCallback.h"
#include "core/dom/MutationObserver.h"
#include "core/dom/MutationObserverInit.h"
#include "core/dom/MutationRecord.h"
#include "core/dom/NodeComputedStyle.h"
#include "core/dom/NodeListsNodeData.h"
#include "core/dom/NodeTraversal.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/events/GestureEvent.h"
#include "core/events/KeyboardEvent.h"
#include "core/events/MouseEvent.h"
#include "core/events/ScopedEventQueue.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/html/FormData.h"
#include "core/html/HTMLFormElement.h"
#include "core/html/HTMLHRElement.h"
#include "core/html/HTMLOptGroupElement.h"
#include "core/html/HTMLOptionElement.h"
#include "core/html/forms/FormController.h"
#include "core/input/EventHandler.h"
#include "core/input/InputDeviceCapabilities.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/layout/HitTestRequest.h"
#include "core/layout/HitTestResult.h"
#include "core/layout/LayoutListBox.h"
#include "core/layout/LayoutMenuList.h"
#include "core/layout/LayoutTheme.h"
#include "core/page/AutoscrollController.h"
#include "core/page/ChromeClient.h"
#include "core/page/Page.h"
#include "core/page/SpatialNavigation.h"
#include "platform/PopupMenu.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/text/PlatformLocale.h"

namespace blink {

using namespace HTMLNames;

// Upper limit of m_listItems. According to the HTML standard, options larger
// than this limit doesn't work well because |selectedIndex| IDL attribute is
// signed.
static const unsigned kMaxListItems = INT_MAX;

HTMLSelectElement::HTMLSelectElement(Document& document)
    : HTMLFormControlElementWithState(selectTag, document),
      type_ahead_(this),
      size_(0),
      last_on_change_option_(nullptr),
      is_multiple_(false),
      active_selection_state_(false),
      should_recalc_list_items_(false),
      is_autofilled_by_preview_(false),
      index_to_select_on_cancel_(-1),
      popup_is_visible_(false) {
  SetHasCustomStyleCallbacks();
}

HTMLSelectElement* HTMLSelectElement::Create(Document& document) {
  HTMLSelectElement* select = new HTMLSelectElement(document);
  select->EnsureUserAgentShadowRoot();
  return select;
}

HTMLSelectElement::~HTMLSelectElement() {}

const AtomicString& HTMLSelectElement::FormControlType() const {
  DEFINE_STATIC_LOCAL(const AtomicString, select_multiple, ("select-multiple"));
  DEFINE_STATIC_LOCAL(const AtomicString, select_one, ("select-one"));
  return is_multiple_ ? select_multiple : select_one;
}

bool HTMLSelectElement::HasPlaceholderLabelOption() const {
  // The select element has no placeholder label option if it has an attribute
  // "multiple" specified or a display size of non-1.
  //
  // The condition "size() > 1" is not compliant with the HTML5 spec as of Dec
  // 3, 2010. "size() != 1" is correct.  Using "size() > 1" here because
  // size() may be 0 in WebKit.  See the discussion at
  // https://bugs.webkit.org/show_bug.cgi?id=43887
  //
  // "0 size()" happens when an attribute "size" is absent or an invalid size
  // attribute is specified.  In this case, the display size should be assumed
  // as the default.  The default display size is 1 for non-multiple select
  // elements, and 4 for multiple select elements.
  //
  // Finally, if size() == 0 and non-multiple, the display size can be assumed
  // as 1.
  if (IsMultiple() || size() > 1)
    return false;

  // TODO(tkent): This function is called in CSS selector matching. Using
  // listItems() might have performance impact.
  if (GetListItems().size() == 0 || !isHTMLOptionElement(GetListItems()[0]))
    return false;
  return toHTMLOptionElement(GetListItems()[0])->value().IsEmpty();
}

String HTMLSelectElement::validationMessage() const {
  if (!willValidate())
    return String();
  if (CustomError())
    return CustomValidationMessage();
  if (ValueMissing())
    return GetLocale().QueryString(
        WebLocalizedString::kValidationValueMissingForSelect);
  return String();
}

bool HTMLSelectElement::ValueMissing() const {
  if (!willValidate())
    return false;

  if (!IsRequired())
    return false;

  int first_selection_index = selectedIndex();

  // If a non-placeholer label option is selected (firstSelectionIndex > 0),
  // it's not value-missing.
  return first_selection_index < 0 ||
         (!first_selection_index && HasPlaceholderLabelOption());
}

String HTMLSelectElement::DefaultToolTip() const {
  if (Form() && Form()->NoValidate())
    return String();
  return validationMessage();
}

void HTMLSelectElement::SelectMultipleOptionsByPopup(
    const Vector<int>& list_indices) {
  DCHECK(UsesMenuList());
  DCHECK(IsMultiple());
  for (size_t i = 0; i < list_indices.size(); ++i) {
    bool add_selection_if_not_first = i > 0;
    if (HTMLOptionElement* option = OptionAtListIndex(list_indices[i]))
      UpdateSelectedState(option, add_selection_if_not_first, false);
  }
  SetNeedsValidityCheck();
  // TODO(tkent): Using listBoxOnChange() is very confusing.
  ListBoxOnChange();
}

bool HTMLSelectElement::UsesMenuList() const {
  if (LayoutTheme::GetTheme().DelegatesMenuListRendering())
    return true;

  return !is_multiple_ && size_ <= 1;
}

int HTMLSelectElement::ActiveSelectionEndListIndex() const {
  HTMLOptionElement* option = ActiveSelectionEnd();
  return option ? option->ListIndex() : -1;
}

HTMLOptionElement* HTMLSelectElement::ActiveSelectionEnd() const {
  if (active_selection_end_)
    return active_selection_end_.Get();
  return LastSelectedOption();
}

void HTMLSelectElement::add(
    const HTMLOptionElementOrHTMLOptGroupElement& element,
    const HTMLElementOrLong& before,
    ExceptionState& exception_state) {
  HTMLElement* element_to_insert;
  DCHECK(!element.isNull());
  if (element.isHTMLOptionElement())
    element_to_insert = element.getAsHTMLOptionElement();
  else
    element_to_insert = element.getAsHTMLOptGroupElement();

  HTMLElement* before_element;
  if (before.isHTMLElement())
    before_element = before.getAsHTMLElement();
  else if (before.isLong())
    before_element = options()->item(before.getAsLong());
  else
    before_element = nullptr;

  InsertBefore(element_to_insert, before_element, exception_state);
  SetNeedsValidityCheck();
}

void HTMLSelectElement::remove(int option_index) {
  if (HTMLOptionElement* option = item(option_index))
    option->remove(IGNORE_EXCEPTION_FOR_TESTING);
}

String HTMLSelectElement::value() const {
  if (HTMLOptionElement* option = SelectedOption())
    return option->value();
  return "";
}

void HTMLSelectElement::setValue(const String& value, bool send_events) {
  HTMLOptionElement* option = nullptr;
  // Find the option with value() matching the given parameter and make it the
  // current selection.
  for (const auto& item : GetOptionList()) {
    if (item->value() == value) {
      option = item;
      break;
    }
  }

  HTMLOptionElement* previous_selected_option = SelectedOption();
  SetSuggestedOption(nullptr);
  if (is_autofilled_by_preview_)
    SetAutofilled(false);
  SelectOptionFlags flags = kDeselectOtherOptions | kMakeOptionDirty;
  if (send_events)
    flags |= kDispatchInputAndChangeEvent;
  SelectOption(option, flags);

  if (send_events && previous_selected_option != option && !UsesMenuList())
    ListBoxOnChange();
}

String HTMLSelectElement::SuggestedValue() const {
  return suggested_option_ ? suggested_option_->value() : "";
}

void HTMLSelectElement::SetSuggestedValue(const String& value) {
  if (value.IsNull()) {
    SetSuggestedOption(nullptr);
    return;
  }

  for (const auto& option : GetOptionList()) {
    if (option->value() == value) {
      SetSuggestedOption(option);
      is_autofilled_by_preview_ = true;
      return;
    }
  }

  SetSuggestedOption(nullptr);
}

bool HTMLSelectElement::IsPresentationAttribute(
    const QualifiedName& name) const {
  if (name == alignAttr) {
    // Don't map 'align' attribute. This matches what Firefox, Opera and IE do.
    // See http://bugs.webkit.org/show_bug.cgi?id=12072
    return false;
  }

  return HTMLFormControlElementWithState::IsPresentationAttribute(name);
}

void HTMLSelectElement::ParseAttribute(
    const AttributeModificationParams& params) {
  if (params.name == sizeAttr) {
    unsigned old_size = size_;
    // Set the attribute value to a number.
    // This is important since the style rules for this attribute can
    // determine the appearance property.
    unsigned size = params.new_value.GetString().ToUInt();
    AtomicString attr_size = AtomicString::Number(size);
    if (attr_size != params.new_value) {
      // FIXME: This is horribly factored.
      if (Attribute* size_attribute =
              EnsureUniqueElementData().Attributes().Find(sizeAttr))
        size_attribute->SetValue(attr_size);
    }
    size_ = size;
    SetNeedsValidityCheck();
    if (size_ != old_size) {
      if (InActiveDocument())
        LazyReattachIfAttached();
      ResetToDefaultSelection();
      if (!UsesMenuList())
        SaveListboxActiveSelection();
    }
  } else if (params.name == multipleAttr) {
    ParseMultipleAttribute(params.new_value);
  } else if (params.name == accesskeyAttr) {
    // FIXME: ignore for the moment.
    //
  } else {
    HTMLFormControlElementWithState::ParseAttribute(params);
  }
}

bool HTMLSelectElement::ShouldShowFocusRingOnMouseFocus() const {
  return true;
}

bool HTMLSelectElement::CanSelectAll() const {
  return !UsesMenuList();
}

LayoutObject* HTMLSelectElement::CreateLayoutObject(const ComputedStyle&) {
  if (UsesMenuList())
    return new LayoutMenuList(this);
  return new LayoutListBox(this);
}

HTMLCollection* HTMLSelectElement::selectedOptions() {
  return EnsureCachedCollection<HTMLCollection>(kSelectedOptions);
}

HTMLOptionsCollection* HTMLSelectElement::options() {
  return EnsureCachedCollection<HTMLOptionsCollection>(kSelectOptions);
}

void HTMLSelectElement::OptionElementChildrenChanged(
    const HTMLOptionElement& option) {
  SetNeedsValidityCheck();

  if (GetLayoutObject()) {
    if (option.Selected() && UsesMenuList())
      GetLayoutObject()->UpdateFromElement();
    if (AXObjectCache* cache =
            GetLayoutObject()->GetDocument().ExistingAXObjectCache())
      cache->ChildrenChanged(this);
  }
}

void HTMLSelectElement::AccessKeyAction(bool send_mouse_events) {
  focus();
  DispatchSimulatedClick(
      nullptr, send_mouse_events ? kSendMouseUpDownEvents : kSendNoEvents);
}

void HTMLSelectElement::setSize(unsigned size) {
  SetUnsignedIntegralAttribute(sizeAttr, size);
}

Element* HTMLSelectElement::namedItem(const AtomicString& name) {
  return options()->namedItem(name);
}

HTMLOptionElement* HTMLSelectElement::item(unsigned index) {
  return options()->item(index);
}

void HTMLSelectElement::SetOption(unsigned index,
                                  HTMLOptionElement* option,
                                  ExceptionState& exception_state) {
  int diff = index - length();
  // We should check |index >= maxListItems| first to avoid integer overflow.
  if (index >= kMaxListItems ||
      GetListItems().size() + diff + 1 > kMaxListItems) {
    GetDocument().AddConsoleMessage(ConsoleMessage::Create(
        kJSMessageSource, kWarningMessageLevel,
        String::Format("Blocked to expand the option list and set an option at "
                       "index=%u.  The maximum list length is %u.",
                       index, kMaxListItems)));
    return;
  }
  HTMLOptionElementOrHTMLOptGroupElement element;
  element.setHTMLOptionElement(option);
  HTMLElementOrLong before;
  // Out of array bounds? First insert empty dummies.
  if (diff > 0) {
    setLength(index, exception_state);
    // Replace an existing entry?
  } else if (diff < 0) {
    before.setHTMLElement(options()->item(index + 1));
    remove(index);
  }
  if (exception_state.HadException())
    return;
  // Finally add the new element.
  EventQueueScope scope;
  add(element, before, exception_state);
  if (diff >= 0 && option->Selected())
    OptionSelectionStateChanged(option, true);
}

void HTMLSelectElement::setLength(unsigned new_len,
                                  ExceptionState& exception_state) {
  // We should check |newLen > maxListItems| first to avoid integer overflow.
  if (new_len > kMaxListItems ||
      GetListItems().size() + new_len - length() > kMaxListItems) {
    GetDocument().AddConsoleMessage(ConsoleMessage::Create(
        kJSMessageSource, kWarningMessageLevel,
        String::Format("Blocked to expand the option list to %u items.  The "
                       "maximum list length is %u.",
                       new_len, kMaxListItems)));
    return;
  }
  int diff = length() - new_len;

  if (diff < 0) {  // Add dummy elements.
    do {
      AppendChild(
          GetDocument().createElement(optionTag, kCreatedByCreateElement),
          exception_state);
      if (exception_state.HadException())
        break;
    } while (++diff);
  } else {
    // Removing children fires mutation events, which might mutate the DOM
    // further, so we first copy out a list of elements that we intend to
    // remove then attempt to remove them one at a time.
    HeapVector<Member<HTMLOptionElement>> items_to_remove;
    size_t option_index = 0;
    for (const auto& option : GetOptionList()) {
      if (option_index++ >= new_len) {
        DCHECK(option->parentNode());
        items_to_remove.push_back(option);
      }
    }

    for (auto& item : items_to_remove) {
      if (item->parentNode())
        item->parentNode()->RemoveChild(item.Get(), exception_state);
    }
  }
  SetNeedsValidityCheck();
}

bool HTMLSelectElement::IsRequiredFormControl() const {
  return IsRequired();
}

HTMLOptionElement* HTMLSelectElement::OptionAtListIndex(int list_index) const {
  if (list_index < 0)
    return nullptr;
  const ListItems& items = GetListItems();
  if (static_cast<size_t>(list_index) >= items.size() ||
      !isHTMLOptionElement(items[list_index]))
    return nullptr;
  return toHTMLOptionElement(items[list_index]);
}

// Returns the 1st valid OPTION |skip| items from |listIndex| in direction
// |direction| if there is one.
// Otherwise, it returns the valid OPTION closest to that boundary which is past
// |listIndex| if there is one.
// Otherwise, it returns nullptr.
// Valid means that it is enabled and visible.
HTMLOptionElement* HTMLSelectElement::NextValidOption(int list_index,
                                                      SkipDirection direction,
                                                      int skip) const {
  DCHECK(direction == kSkipBackwards || direction == kSkipForwards);
  const ListItems& list_items = this->GetListItems();
  HTMLOptionElement* last_good_option = nullptr;
  int size = list_items.size();
  for (list_index += direction; list_index >= 0 && list_index < size;
       list_index += direction) {
    --skip;
    HTMLElement* element = list_items[list_index];
    if (!isHTMLOptionElement(*element))
      continue;
    if (toHTMLOptionElement(*element).IsDisplayNone())
      continue;
    if (element->IsDisabledFormControl())
      continue;
    if (!UsesMenuList() && !element->GetLayoutObject())
      continue;
    last_good_option = toHTMLOptionElement(element);
    if (skip <= 0)
      break;
  }
  return last_good_option;
}

HTMLOptionElement* HTMLSelectElement::NextSelectableOption(
    HTMLOptionElement* start_option) const {
  return NextValidOption(start_option ? start_option->ListIndex() : -1,
                         kSkipForwards, 1);
}

HTMLOptionElement* HTMLSelectElement::PreviousSelectableOption(
    HTMLOptionElement* start_option) const {
  return NextValidOption(
      start_option ? start_option->ListIndex() : GetListItems().size(),
      kSkipBackwards, 1);
}

HTMLOptionElement* HTMLSelectElement::FirstSelectableOption() const {
  // TODO(tkent): This is not efficient.  nextSlectableOption(nullptr) is
  // faster.
  return NextValidOption(GetListItems().size(), kSkipBackwards, INT_MAX);
}

HTMLOptionElement* HTMLSelectElement::LastSelectableOption() const {
  // TODO(tkent): This is not efficient.  previousSlectableOption(nullptr) is
  // faster.
  return NextValidOption(-1, kSkipForwards, INT_MAX);
}

// Returns the index of the next valid item one page away from |startIndex| in
// direction |direction|.
HTMLOptionElement* HTMLSelectElement::NextSelectableOptionPageAway(
    HTMLOptionElement* start_option,
    SkipDirection direction) const {
  const ListItems& items = GetListItems();
  // Can't use m_size because layoutObject forces a minimum size.
  int page_size = 0;
  if (GetLayoutObject()->IsListBox())
    page_size = ToLayoutListBox(GetLayoutObject())->size() -
                1;  // -1 so we still show context.

  // One page away, but not outside valid bounds.
  // If there is a valid option item one page away, the index is chosen.
  // If there is no exact one page away valid option, returns startIndex or
  // the most far index.
  int start_index = start_option ? start_option->ListIndex() : -1;
  int edge_index = (direction == kSkipForwards) ? 0 : (items.size() - 1);
  int skip_amount =
      page_size +
      ((direction == kSkipForwards) ? start_index : (edge_index - start_index));
  return NextValidOption(edge_index, direction, skip_amount);
}

void HTMLSelectElement::SelectAll() {
  DCHECK(!UsesMenuList());
  if (!GetLayoutObject() || !is_multiple_)
    return;

  // Save the selection so it can be compared to the new selectAll selection
  // when dispatching change events.
  SaveLastSelection();

  active_selection_state_ = true;
  SetActiveSelectionAnchor(NextSelectableOption(nullptr));
  SetActiveSelectionEnd(PreviousSelectableOption(nullptr));

  UpdateListBoxSelection(false, false);
  ListBoxOnChange();
  SetNeedsValidityCheck();
}

void HTMLSelectElement::SaveLastSelection() {
  if (UsesMenuList()) {
    last_on_change_option_ = SelectedOption();
    return;
  }

  last_on_change_selection_.Clear();
  for (auto& element : GetListItems())
    last_on_change_selection_.push_back(
        isHTMLOptionElement(*element) &&
        toHTMLOptionElement(element)->Selected());
}

void HTMLSelectElement::SetActiveSelectionAnchor(HTMLOptionElement* option) {
  active_selection_anchor_ = option;
  if (!UsesMenuList())
    SaveListboxActiveSelection();
}

void HTMLSelectElement::SaveListboxActiveSelection() {
  // Cache the selection state so we can restore the old selection as the new
  // selection pivots around this anchor index.
  // Example:
  // 1. Press the mouse button on the second OPTION
  //   m_activeSelectionAnchorIndex = 1
  // 2. Drag the mouse pointer onto the fifth OPTION
  //   m_activeSelectionEndIndex = 4, options at 1-4 indices are selected.
  // 3. Drag the mouse pointer onto the fourth OPTION
  //   m_activeSelectionEndIndex = 3, options at 1-3 indices are selected.
  //   updateListBoxSelection needs to clear selection of the fifth OPTION.
  cached_state_for_active_selection_.Resize(0);
  for (const auto& option : GetOptionList()) {
    cached_state_for_active_selection_.push_back(option->Selected());
  }
}

void HTMLSelectElement::SetActiveSelectionEnd(HTMLOptionElement* option) {
  active_selection_end_ = option;
}

void HTMLSelectElement::UpdateListBoxSelection(bool deselect_other_options,
                                               bool scroll) {
  DCHECK(GetLayoutObject());
  DCHECK(GetLayoutObject()->IsListBox() || is_multiple_);

  int active_selection_anchor_index =
      active_selection_anchor_ ? active_selection_anchor_->index() : -1;
  int active_selection_end_index =
      active_selection_end_ ? active_selection_end_->index() : -1;
  int start =
      std::min(active_selection_anchor_index, active_selection_end_index);
  int end = std::max(active_selection_anchor_index, active_selection_end_index);

  int i = 0;
  for (const auto& option : GetOptionList()) {
    if (option->IsDisabledFormControl() || !option->GetLayoutObject()) {
      ++i;
      continue;
    }
    if (i >= start && i <= end) {
      option->SetSelectedState(active_selection_state_);
      option->SetDirty(true);
    } else if (deselect_other_options ||
               i >= static_cast<int>(
                        cached_state_for_active_selection_.size())) {
      option->SetSelectedState(false);
      option->SetDirty(true);
    } else {
      option->SetSelectedState(cached_state_for_active_selection_[i]);
    }
    ++i;
  }

  SetNeedsValidityCheck();
  if (scroll)
    ScrollToSelection();
  NotifyFormStateChanged();
}

void HTMLSelectElement::ListBoxOnChange() {
  DCHECK(!UsesMenuList() || is_multiple_);

  const ListItems& items = GetListItems();

  // If the cached selection list is empty, or the size has changed, then fire
  // dispatchFormControlChangeEvent, and return early.
  // FIXME: Why? This looks unreasonable.
  if (last_on_change_selection_.IsEmpty() ||
      last_on_change_selection_.size() != items.size()) {
    DispatchChangeEvent();
    return;
  }

  // Update m_lastOnChangeSelection and fire dispatchFormControlChangeEvent.
  bool fire_on_change = false;
  for (unsigned i = 0; i < items.size(); ++i) {
    HTMLElement* element = items[i];
    bool selected = isHTMLOptionElement(*element) &&
                    toHTMLOptionElement(element)->Selected();
    if (selected != last_on_change_selection_[i])
      fire_on_change = true;
    last_on_change_selection_[i] = selected;
  }

  if (fire_on_change) {
    DispatchInputEvent();
    DispatchChangeEvent();
  }
}

void HTMLSelectElement::DispatchInputAndChangeEventForMenuList() {
  DCHECK(UsesMenuList());

  HTMLOptionElement* selected_option = this->SelectedOption();
  if (last_on_change_option_.Get() != selected_option) {
    last_on_change_option_ = selected_option;
    DispatchInputEvent();
    DispatchChangeEvent();
  }
}

void HTMLSelectElement::ScrollToSelection() {
  if (!IsFinishedParsingChildren())
    return;
  if (UsesMenuList())
    return;
  ScrollToOption(ActiveSelectionEnd());
  if (AXObjectCache* cache = GetDocument().ExistingAXObjectCache())
    cache->ListboxActiveIndexChanged(this);
}

void HTMLSelectElement::SetOptionsChangedOnLayoutObject() {
  if (LayoutObject* layout_object = this->GetLayoutObject()) {
    if (UsesMenuList())
      ToLayoutMenuList(layout_object)
          ->SetNeedsLayoutAndPrefWidthsRecalc(
              LayoutInvalidationReason::kMenuOptionsChanged);
  }
}

const HTMLSelectElement::ListItems& HTMLSelectElement::GetListItems() const {
  if (should_recalc_list_items_) {
    RecalcListItems();
  } else {
#if DCHECK_IS_ON()
    HeapVector<Member<HTMLElement>> items = list_items_;
    RecalcListItems();
    DCHECK(items == list_items_);
#endif
  }

  return list_items_;
}

void HTMLSelectElement::InvalidateSelectedItems() {
  if (HTMLCollection* collection =
          CachedCollection<HTMLCollection>(kSelectedOptions))
    collection->InvalidateCache();
}

void HTMLSelectElement::SetRecalcListItems() {
  // FIXME: This function does a bunch of confusing things depending on if it
  // is in the document or not.

  should_recalc_list_items_ = true;

  SetOptionsChangedOnLayoutObject();
  if (!isConnected()) {
    if (HTMLOptionsCollection* collection =
            CachedCollection<HTMLOptionsCollection>(kSelectOptions))
      collection->InvalidateCache();
    InvalidateSelectedItems();
  }

  if (GetLayoutObject()) {
    if (AXObjectCache* cache =
            GetLayoutObject()->GetDocument().ExistingAXObjectCache())
      cache->ChildrenChanged(this);
  }
}

void HTMLSelectElement::RecalcListItems() const {
  TRACE_EVENT0("blink", "HTMLSelectElement::recalcListItems");
  list_items_.Resize(0);

  should_recalc_list_items_ = false;

  for (Element* current_element = ElementTraversal::FirstWithin(*this);
       current_element && list_items_.size() < kMaxListItems;) {
    if (!current_element->IsHTMLElement()) {
      current_element =
          ElementTraversal::NextSkippingChildren(*current_element, this);
      continue;
    }
    HTMLElement& current = ToHTMLElement(*current_element);

    // We should ignore nested optgroup elements. The HTML parser flatten
    // them.  However we need to ignore nested optgroups built by DOM APIs.
    // This behavior matches to IE and Firefox.
    if (isHTMLOptGroupElement(current)) {
      if (current.parentNode() != this) {
        current_element = ElementTraversal::NextSkippingChildren(current, this);
        continue;
      }
      list_items_.push_back(&current);
      if (Element* next_element = ElementTraversal::FirstWithin(current)) {
        current_element = next_element;
        continue;
      }
    }

    if (isHTMLOptionElement(current))
      list_items_.push_back(&current);

    if (isHTMLHRElement(current))
      list_items_.push_back(&current);

    // In conforming HTML code, only <optgroup> and <option> will be found
    // within a <select>. We call NodeTraversal::nextSkippingChildren so
    // that we only step into those tags that we choose to. For web-compat,
    // we should cope with the case where odd tags like a <div> have been
    // added but we handle this because such tags have already been removed
    // from the <select>'s subtree at this point.
    current_element =
        ElementTraversal::NextSkippingChildren(*current_element, this);
  }
}

void HTMLSelectElement::ResetToDefaultSelection(ResetReason reason) {
  // https://html.spec.whatwg.org/multipage/forms.html#ask-for-a-reset
  if (IsMultiple())
    return;
  HTMLOptionElement* first_enabled_option = nullptr;
  HTMLOptionElement* last_selected_option = nullptr;
  bool did_change = false;
  int option_index = 0;
  // We can't use HTMLSelectElement::options here because this function is
  // called in Node::insertedInto and Node::removedFrom before invalidating
  // node collections.
  for (const auto& option : GetOptionList()) {
    if (option->Selected()) {
      if (last_selected_option) {
        last_selected_option->SetSelectedState(false);
        did_change = true;
      }
      last_selected_option = option;
    }
    if (!first_enabled_option && !option->IsDisabledFormControl()) {
      first_enabled_option = option;
      if (reason == kResetReasonSelectedOptionRemoved) {
        // There must be no selected OPTIONs.
        break;
      }
    }
    ++option_index;
  }
  if (!last_selected_option && size_ <= 1 &&
      (!first_enabled_option ||
       (first_enabled_option && !first_enabled_option->Selected()))) {
    SelectOption(first_enabled_option,
                 reason == kResetReasonSelectedOptionRemoved
                     ? 0
                     : kDeselectOtherOptions);
    last_selected_option = first_enabled_option;
    did_change = true;
  }
  if (did_change)
    SetNeedsValidityCheck();
  last_on_change_option_ = last_selected_option;
}

HTMLOptionElement* HTMLSelectElement::SelectedOption() const {
  for (const auto option : GetOptionList()) {
    if (option->Selected())
      return option;
  }
  return nullptr;
}

int HTMLSelectElement::selectedIndex() const {
  unsigned index = 0;

  // Return the number of the first option selected.
  for (const auto& option : GetOptionList()) {
    if (option->Selected())
      return index;
    ++index;
  }

  return -1;
}

void HTMLSelectElement::setSelectedIndex(int index) {
  SelectOption(item(index), kDeselectOtherOptions | kMakeOptionDirty);
}

int HTMLSelectElement::SelectedListIndex() const {
  int index = 0;
  for (const auto& item : GetListItems()) {
    if (isHTMLOptionElement(item) && toHTMLOptionElement(item)->Selected())
      return index;
    ++index;
  }
  return -1;
}

void HTMLSelectElement::SetSuggestedOption(HTMLOptionElement* option) {
  if (suggested_option_ == option)
    return;
  suggested_option_ = option;

  if (LayoutObject* layout_object = this->GetLayoutObject()) {
    layout_object->UpdateFromElement();
    ScrollToOption(option);
  }
  if (PopupIsVisible())
    popup_->UpdateFromElement(PopupMenu::kBySelectionChange);
}

void HTMLSelectElement::ScrollToOption(HTMLOptionElement* option) {
  if (!option)
    return;
  if (UsesMenuList())
    return;
  bool has_pending_task = option_to_scroll_to_;
  // We'd like to keep an HTMLOptionElement reference rather than the index of
  // the option because the task should work even if unselected option is
  // inserted before executing scrollToOptionTask().
  option_to_scroll_to_ = option;
  if (!has_pending_task)
    TaskRunnerHelper::Get(TaskType::kUserInteraction, &GetDocument())
        ->PostTask(BLINK_FROM_HERE,
                   WTF::Bind(&HTMLSelectElement::ScrollToOptionTask,
                             WrapPersistent(this)));
}

void HTMLSelectElement::ScrollToOptionTask() {
  HTMLOptionElement* option = option_to_scroll_to_.Release();
  if (!option || !isConnected())
    return;
  // optionRemoved() makes sure m_optionToScrollTo doesn't have an option with
  // another owner.
  DCHECK_EQ(option->OwnerSelectElement(), this);
  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  if (!GetLayoutObject() || !GetLayoutObject()->IsListBox())
    return;
  LayoutRect bounds = option->BoundingBox();
  ToLayoutListBox(GetLayoutObject())->ScrollToRect(bounds);
}

void HTMLSelectElement::OptionSelectionStateChanged(HTMLOptionElement* option,
                                                    bool option_is_selected) {
  DCHECK_EQ(option->OwnerSelectElement(), this);
  if (option_is_selected)
    SelectOption(option, IsMultiple() ? 0 : kDeselectOtherOptions);
  else if (!UsesMenuList() || IsMultiple())
    SelectOption(nullptr, IsMultiple() ? 0 : kDeselectOtherOptions);
  else
    SelectOption(NextSelectableOption(nullptr), kDeselectOtherOptions);
}

void HTMLSelectElement::OptionInserted(HTMLOptionElement& option,
                                       bool option_is_selected) {
  DCHECK_EQ(option.OwnerSelectElement(), this);
  SetRecalcListItems();
  if (option_is_selected) {
    SelectOption(&option, IsMultiple() ? 0 : kDeselectOtherOptions);
  } else {
    // No need to reset if we already have a selected option.
    if (!last_on_change_option_)
      ResetToDefaultSelection();
  }
  SetNeedsValidityCheck();
  last_on_change_selection_.Clear();
}

void HTMLSelectElement::OptionRemoved(HTMLOptionElement& option) {
  SetRecalcListItems();
  if (option.Selected())
    ResetToDefaultSelection(kResetReasonSelectedOptionRemoved);
  else if (!last_on_change_option_)
    ResetToDefaultSelection();
  if (last_on_change_option_ == &option)
    last_on_change_option_.Clear();
  if (option_to_scroll_to_ == &option)
    option_to_scroll_to_.Clear();
  if (active_selection_anchor_ == &option)
    active_selection_anchor_.Clear();
  if (active_selection_end_ == &option)
    active_selection_end_.Clear();
  if (suggested_option_ == &option)
    SetSuggestedOption(nullptr);
  if (option.Selected())
    SetAutofilled(false);
  SetNeedsValidityCheck();
  last_on_change_selection_.Clear();
}

void HTMLSelectElement::OptGroupInsertedOrRemoved(
    HTMLOptGroupElement& optgroup) {
  SetRecalcListItems();
  SetNeedsValidityCheck();
  last_on_change_selection_.Clear();
}

void HTMLSelectElement::HrInsertedOrRemoved(HTMLHRElement& hr) {
  SetRecalcListItems();
  last_on_change_selection_.Clear();
}

// TODO(tkent): This function is not efficient.  It contains multiple O(N)
// operations. crbug.com/577989.
void HTMLSelectElement::SelectOption(HTMLOptionElement* element,
                                     SelectOptionFlags flags) {
  TRACE_EVENT0("blink", "HTMLSelectElement::selectOption");

  bool should_update_popup = false;

  // selectedOption() is O(N).
  if (IsAutofilled() && SelectedOption() != element)
    SetAutofilled(false);

  if (element) {
    if (!element->Selected())
      should_update_popup = true;
    element->SetSelectedState(true);
    if (flags & kMakeOptionDirty)
      element->SetDirty(true);
  }

  // deselectItemsWithoutValidation() is O(N).
  if (flags & kDeselectOtherOptions)
    should_update_popup |= DeselectItemsWithoutValidation(element);

  // We should update active selection after finishing OPTION state change
  // because setActiveSelectionAnchorIndex() stores OPTION's selection state.
  if (element) {
    // setActiveSelectionAnchor is O(N).
    if (!active_selection_anchor_ || !IsMultiple() ||
        flags & kDeselectOtherOptions)
      SetActiveSelectionAnchor(element);
    if (!active_selection_end_ || !IsMultiple() ||
        flags & kDeselectOtherOptions)
      SetActiveSelectionEnd(element);
  }

  // Need to update m_lastOnChangeOption before
  // LayoutMenuList::updateFromElement.
  bool should_dispatch_events = false;
  if (UsesMenuList()) {
    should_dispatch_events = (flags & kDispatchInputAndChangeEvent) &&
                             last_on_change_option_ != element;
    last_on_change_option_ = element;
  }

  // For the menu list case, this is what makes the selected element appear.
  if (LayoutObject* layout_object = this->GetLayoutObject())
    layout_object->UpdateFromElement();
  // PopupMenu::updateFromElement() posts an O(N) task.
  if (PopupIsVisible() && should_update_popup)
    popup_->UpdateFromElement(PopupMenu::kBySelectionChange);

  ScrollToSelection();
  SetNeedsValidityCheck();

  if (UsesMenuList()) {
    if (should_dispatch_events) {
      DispatchInputEvent();
      DispatchChangeEvent();
    }
    if (LayoutObject* layout_object = this->GetLayoutObject()) {
      // Need to check usesMenuList() again because event handlers might
      // change the status.
      if (UsesMenuList()) {
        // didSelectOption() is O(N) because of HTMLOptionElement::index().
        ToLayoutMenuList(layout_object)->DidSelectOption(element);
      }
    }
  }

  NotifyFormStateChanged();
}

void HTMLSelectElement::DispatchFocusEvent(
    Element* old_focused_element,
    WebFocusType type,
    InputDeviceCapabilities* source_capabilities) {
  // Save the selection so it can be compared to the new selection when
  // dispatching change events during blur event dispatch.
  if (UsesMenuList())
    SaveLastSelection();
  HTMLFormControlElementWithState::DispatchFocusEvent(old_focused_element, type,
                                                      source_capabilities);
}

void HTMLSelectElement::DispatchBlurEvent(
    Element* new_focused_element,
    WebFocusType type,
    InputDeviceCapabilities* source_capabilities) {
  type_ahead_.ResetSession();
  // We only need to fire change events here for menu lists, because we fire
  // change events for list boxes whenever the selection change is actually
  // made.  This matches other browsers' behavior.
  if (UsesMenuList())
    DispatchInputAndChangeEventForMenuList();
  last_on_change_selection_.Clear();
  if (PopupIsVisible())
    HidePopup();
  HTMLFormControlElementWithState::DispatchBlurEvent(new_focused_element, type,
                                                     source_capabilities);
}

// Returns true if selection state of any OPTIONs is changed.
bool HTMLSelectElement::DeselectItemsWithoutValidation(
    HTMLOptionElement* exclude_element) {
  if (!IsMultiple() && UsesMenuList() && last_on_change_option_ &&
      last_on_change_option_ != exclude_element) {
    last_on_change_option_->SetSelectedState(false);
    return true;
  }
  bool did_update_selection = false;
  for (const auto& option : GetOptionList()) {
    if (option != exclude_element) {
      if (option->Selected())
        did_update_selection = true;
      option->SetSelectedState(false);
    }
  }
  return did_update_selection;
}

FormControlState HTMLSelectElement::SaveFormControlState() const {
  const ListItems& items = GetListItems();
  size_t length = items.size();
  FormControlState state;
  for (unsigned i = 0; i < length; ++i) {
    if (!isHTMLOptionElement(*items[i]))
      continue;
    HTMLOptionElement* option = toHTMLOptionElement(items[i]);
    if (!option->Selected())
      continue;
    state.Append(option->value());
    state.Append(String::Number(i));
    if (!IsMultiple())
      break;
  }
  return state;
}

size_t HTMLSelectElement::SearchOptionsForValue(const String& value,
                                                size_t list_index_start,
                                                size_t list_index_end) const {
  const ListItems& items = GetListItems();
  size_t loop_end_index = std::min(items.size(), list_index_end);
  for (size_t i = list_index_start; i < loop_end_index; ++i) {
    if (!isHTMLOptionElement(items[i]))
      continue;
    if (toHTMLOptionElement(items[i])->value() == value)
      return i;
  }
  return kNotFound;
}

void HTMLSelectElement::RestoreFormControlState(const FormControlState& state) {
  RecalcListItems();

  const ListItems& items = GetListItems();
  size_t items_size = items.size();
  if (items_size == 0)
    return;

  SelectOption(nullptr, kDeselectOtherOptions);

  // The saved state should have at least one value and an index.
  DCHECK_GE(state.ValueSize(), 2u);
  if (!IsMultiple()) {
    size_t index = state[1].ToUInt();
    if (index < items_size && isHTMLOptionElement(items[index]) &&
        toHTMLOptionElement(items[index])->value() == state[0]) {
      toHTMLOptionElement(items[index])->SetSelectedState(true);
      toHTMLOptionElement(items[index])->SetDirty(true);
      last_on_change_option_ = toHTMLOptionElement(items[index]);
    } else {
      size_t found_index = SearchOptionsForValue(state[0], 0, items_size);
      if (found_index != kNotFound) {
        toHTMLOptionElement(items[found_index])->SetSelectedState(true);
        toHTMLOptionElement(items[found_index])->SetDirty(true);
        last_on_change_option_ = toHTMLOptionElement(items[found_index]);
      }
    }
  } else {
    size_t start_index = 0;
    for (size_t i = 0; i < state.ValueSize(); i += 2) {
      const String& value = state[i];
      const size_t index = state[i + 1].ToUInt();
      if (index < items_size && isHTMLOptionElement(items[index]) &&
          toHTMLOptionElement(items[index])->value() == value) {
        toHTMLOptionElement(items[index])->SetSelectedState(true);
        toHTMLOptionElement(items[index])->SetDirty(true);
        start_index = index + 1;
      } else {
        size_t found_index =
            SearchOptionsForValue(value, start_index, items_size);
        if (found_index == kNotFound)
          found_index = SearchOptionsForValue(value, 0, start_index);
        if (found_index == kNotFound)
          continue;
        toHTMLOptionElement(items[found_index])->SetSelectedState(true);
        toHTMLOptionElement(items[found_index])->SetDirty(true);
        start_index = found_index + 1;
      }
    }
  }

  SetNeedsValidityCheck();
}

void HTMLSelectElement::ParseMultipleAttribute(const AtomicString& value) {
  bool old_multiple = is_multiple_;
  HTMLOptionElement* old_selected_option = SelectedOption();
  is_multiple_ = !value.IsNull();
  SetNeedsValidityCheck();
  LazyReattachIfAttached();
  // Restore selectedIndex after changing the multiple flag to preserve
  // selection as single-line and multi-line has different defaults.
  if (old_multiple != is_multiple_) {
    // Preserving the first selection is compatible with Firefox and
    // WebKit. However Edge seems to "ask for a reset" simply.  As of 2016
    // March, the HTML specification says nothing about this.
    if (old_selected_option)
      SelectOption(old_selected_option, kDeselectOtherOptions);
    else
      ResetToDefaultSelection();
  }
}

void HTMLSelectElement::AppendToFormData(FormData& form_data) {
  const AtomicString& name = this->GetName();
  if (name.IsEmpty())
    return;

  for (const auto& option : GetOptionList()) {
    if (option->Selected() && !option->IsDisabledFormControl())
      form_data.append(name, option->value());
  }
}

void HTMLSelectElement::ResetImpl() {
  for (const auto& option : GetOptionList()) {
    option->SetSelectedState(option->FastHasAttribute(selectedAttr));
    option->SetDirty(false);
  }
  ResetToDefaultSelection();
  SetNeedsValidityCheck();
}

void HTMLSelectElement::HandlePopupOpenKeyboardEvent(Event* event) {
  focus();
  // Calling focus() may cause us to lose our layoutObject. Return true so
  // that our caller doesn't process the event further, but don't set
  // the event as handled.
  if (!GetLayoutObject() || !GetLayoutObject()->IsMenuList() ||
      IsDisabledFormControl())
    return;
  // Save the selection so it can be compared to the new selection when
  // dispatching change events during selectOption, which gets called from
  // selectOptionByPopup, which gets called after the user makes a selection
  // from the menu.
  SaveLastSelection();
  ShowPopup();
  event->SetDefaultHandled();
  return;
}

bool HTMLSelectElement::ShouldOpenPopupForKeyDownEvent(
    KeyboardEvent* key_event) {
  const String& key = key_event->key();
  LayoutTheme& layout_theme = LayoutTheme::GetTheme();

  if (IsSpatialNavigationEnabled(GetDocument().GetFrame()))
    return false;

  return ((layout_theme.PopsMenuByArrowKeys() &&
           (key == "ArrowDown" || key == "ArrowUp")) ||
          (layout_theme.PopsMenuByAltDownUpOrF4Key() &&
           (key == "ArrowDown" || key == "ArrowUp") && key_event->altKey()) ||
          (layout_theme.PopsMenuByAltDownUpOrF4Key() &&
           (!key_event->altKey() && !key_event->ctrlKey() && key == "F4")));
}

bool HTMLSelectElement::ShouldOpenPopupForKeyPressEvent(KeyboardEvent* event) {
  LayoutTheme& layout_theme = LayoutTheme::GetTheme();
  int key_code = event->keyCode();

  return ((layout_theme.PopsMenuBySpaceKey() && event->keyCode() == ' ' &&
           !type_ahead_.HasActiveSession(event)) ||
          (layout_theme.PopsMenuByReturnKey() && key_code == '\r'));
}

void HTMLSelectElement::MenuListDefaultEventHandler(Event* event) {
  if (event->type() == EventTypeNames::keydown) {
    if (!GetLayoutObject() || !event->IsKeyboardEvent())
      return;

    KeyboardEvent* key_event = ToKeyboardEvent(event);
    if (ShouldOpenPopupForKeyDownEvent(key_event)) {
      HandlePopupOpenKeyboardEvent(event);
      return;
    }

    // When using spatial navigation, we want to be able to navigate away
    // from the select element when the user hits any of the arrow keys,
    // instead of changing the selection.
    if (IsSpatialNavigationEnabled(GetDocument().GetFrame())) {
      if (!active_selection_state_)
        return;
    }

    // The key handling below shouldn't be used for non spatial navigation
    // mode Mac
    if (LayoutTheme::GetTheme().PopsMenuByArrowKeys() &&
        !IsSpatialNavigationEnabled(GetDocument().GetFrame()))
      return;

    int ignore_modifiers = WebInputEvent::kShiftKey |
                           WebInputEvent::kControlKey | WebInputEvent::kAltKey |
                           WebInputEvent::kMetaKey;
    if (key_event->GetModifiers() & ignore_modifiers)
      return;

    const String& key = key_event->key();
    bool handled = true;
    const ListItems& list_items = this->GetListItems();
    HTMLOptionElement* option = SelectedOption();
    int list_index = option ? option->ListIndex() : -1;

    if (key == "ArrowDown" || key == "ArrowRight")
      option = NextValidOption(list_index, kSkipForwards, 1);
    else if (key == "ArrowUp" || key == "ArrowLeft")
      option = NextValidOption(list_index, kSkipBackwards, 1);
    else if (key == "PageDown")
      option = NextValidOption(list_index, kSkipForwards, 3);
    else if (key == "PageUp")
      option = NextValidOption(list_index, kSkipBackwards, 3);
    else if (key == "Home")
      option = NextValidOption(-1, kSkipForwards, 1);
    else if (key == "End")
      option = NextValidOption(list_items.size(), kSkipBackwards, 1);
    else
      handled = false;

    if (handled && option)
      SelectOption(option, kDeselectOtherOptions | kMakeOptionDirty |
                               kDispatchInputAndChangeEvent);

    if (handled)
      event->SetDefaultHandled();
  }

  if (event->type() == EventTypeNames::keypress) {
    if (!GetLayoutObject() || !event->IsKeyboardEvent())
      return;

    int key_code = ToKeyboardEvent(event)->keyCode();
    if (key_code == ' ' &&
        IsSpatialNavigationEnabled(GetDocument().GetFrame())) {
      // Use space to toggle arrow key handling for selection change or
      // spatial navigation.
      active_selection_state_ = !active_selection_state_;
      event->SetDefaultHandled();
      return;
    }

    KeyboardEvent* key_event = ToKeyboardEvent(event);
    if (ShouldOpenPopupForKeyPressEvent(key_event)) {
      HandlePopupOpenKeyboardEvent(event);
      return;
    }

    if (!LayoutTheme::GetTheme().PopsMenuByReturnKey() && key_code == '\r') {
      if (Form())
        Form()->SubmitImplicitly(event, false);
      DispatchInputAndChangeEventForMenuList();
      event->SetDefaultHandled();
    }
  }

  if (event->type() == EventTypeNames::mousedown && event->IsMouseEvent() &&
      ToMouseEvent(event)->button() ==
          static_cast<short>(WebPointerProperties::Button::kLeft)) {
    InputDeviceCapabilities* source_capabilities =
        GetDocument()
            .domWindow()
            ->GetInputDeviceCapabilities()
            ->FiresTouchEvents(ToMouseEvent(event)->FromTouch());
    focus(FocusParams(SelectionBehaviorOnFocus::kRestore, kWebFocusTypeNone,
                      source_capabilities));
    if (GetLayoutObject() && GetLayoutObject()->IsMenuList() &&
        !IsDisabledFormControl()) {
      if (PopupIsVisible()) {
        HidePopup();
      } else {
        // Save the selection so it can be compared to the new selection
        // when we call onChange during selectOption, which gets called
        // from selectOptionByPopup, which gets called after the user
        // makes a selection from the menu.
        SaveLastSelection();
        // TODO(lanwei): Will check if we need to add
        // InputDeviceCapabilities here when select menu list gets
        // focus, see https://crbug.com/476530.
        ShowPopup();
      }
    }
    event->SetDefaultHandled();
  }
}

void HTMLSelectElement::UpdateSelectedState(HTMLOptionElement* clicked_option,
                                            bool multi,
                                            bool shift) {
  DCHECK(clicked_option);
  // Save the selection so it can be compared to the new selection when
  // dispatching change events during mouseup, or after autoscroll finishes.
  SaveLastSelection();

  active_selection_state_ = true;

  bool shift_select = is_multiple_ && shift;
  bool multi_select = is_multiple_ && multi && !shift;

  // Keep track of whether an active selection (like during drag selection),
  // should select or deselect.
  if (clicked_option->Selected() && multi_select) {
    active_selection_state_ = false;
    clicked_option->SetSelectedState(false);
    clicked_option->SetDirty(true);
  }

  // If we're not in any special multiple selection mode, then deselect all
  // other items, excluding the clicked option. If no option was clicked, then
  // this will deselect all items in the list.
  if (!shift_select && !multi_select)
    DeselectItemsWithoutValidation(clicked_option);

  // If the anchor hasn't been set, and we're doing a single selection or a
  // shift selection, then initialize the anchor to the first selected index.
  if (!active_selection_anchor_ && !multi_select)
    SetActiveSelectionAnchor(SelectedOption());

  // Set the selection state of the clicked option.
  if (!clicked_option->IsDisabledFormControl()) {
    clicked_option->SetSelectedState(true);
    clicked_option->SetDirty(true);
  }

  // If there was no selectedIndex() for the previous initialization, or If
  // we're doing a single selection, or a multiple selection (using cmd or
  // ctrl), then initialize the anchor index to the listIndex that just got
  // clicked.
  if (!active_selection_anchor_ || !shift_select)
    SetActiveSelectionAnchor(clicked_option);

  SetActiveSelectionEnd(clicked_option);
  UpdateListBoxSelection(!multi_select);
}

HTMLOptionElement* HTMLSelectElement::EventTargetOption(const Event& event) {
  Node* target_node = event.target()->ToNode();
  if (!target_node || !isHTMLOptionElement(*target_node))
    return nullptr;
  return toHTMLOptionElement(target_node);
}

int HTMLSelectElement::ListIndexForOption(const HTMLOptionElement& option) {
  const ListItems& items = this->GetListItems();
  size_t length = items.size();
  for (size_t i = 0; i < length; ++i) {
    if (items[i].Get() == &option)
      return i;
  }
  return -1;
}

AutoscrollController* HTMLSelectElement::GetAutoscrollController() const {
  if (Page* page = GetDocument().GetPage())
    return &page->GetAutoscrollController();
  return nullptr;
}

void HTMLSelectElement::HandleMouseRelease() {
  // We didn't start this click/drag on any options.
  if (last_on_change_selection_.IsEmpty())
    return;
  ListBoxOnChange();
}

void HTMLSelectElement::ListBoxDefaultEventHandler(Event* event) {
  if (event->type() == EventTypeNames::gesturetap && event->IsGestureEvent()) {
    focus();
    // Calling focus() may cause us to lose our layoutObject or change the
    // layoutObject type, in which case do not want to handle the event.
    if (!GetLayoutObject() || !GetLayoutObject()->IsListBox())
      return;

    // Convert to coords relative to the list box if needed.
    GestureEvent& gesture_event = ToGestureEvent(*event);
    if (HTMLOptionElement* option = EventTargetOption(gesture_event)) {
      if (!IsDisabledFormControl()) {
        UpdateSelectedState(option, true, gesture_event.shiftKey());
        ListBoxOnChange();
      }
      event->SetDefaultHandled();
    }

  } else if (event->type() == EventTypeNames::mousedown &&
             event->IsMouseEvent() &&
             ToMouseEvent(event)->button() ==
                 static_cast<short>(WebPointerProperties::Button::kLeft)) {
    focus();
    // Calling focus() may cause us to lose our layoutObject, in which case
    // do not want to handle the event.
    if (!GetLayoutObject() || !GetLayoutObject()->IsListBox() ||
        IsDisabledFormControl())
      return;

    // Convert to coords relative to the list box if needed.
    MouseEvent* mouse_event = ToMouseEvent(event);
    if (HTMLOptionElement* option = EventTargetOption(*mouse_event)) {
      if (!IsDisabledFormControl()) {
#if OS(MACOSX)
        UpdateSelectedState(option, mouse_event->metaKey(),
                            mouse_event->shiftKey());
#else
        UpdateSelectedState(option, mouse_event->ctrlKey(),
                            mouse_event->shiftKey());
#endif
      }
      if (LocalFrame* frame = GetDocument().GetFrame())
        frame->GetEventHandler().SetMouseDownMayStartAutoscroll();

      event->SetDefaultHandled();
    }

  } else if (event->type() == EventTypeNames::mousemove &&
             event->IsMouseEvent()) {
    MouseEvent* mouse_event = ToMouseEvent(event);
    if (mouse_event->button() !=
            static_cast<short>(WebPointerProperties::Button::kLeft) ||
        !mouse_event->ButtonDown())
      return;

    if (LayoutObject* object = GetLayoutObject())
      object->GetFrameView()->UpdateAllLifecyclePhasesExceptPaint();

    if (Page* page = GetDocument().GetPage()) {
      page->GetAutoscrollController().StartAutoscrollForSelection(
          GetLayoutObject());
    }
    // Mousedown didn't happen in this element.
    if (last_on_change_selection_.IsEmpty())
      return;

    if (HTMLOptionElement* option = EventTargetOption(*mouse_event)) {
      if (!IsDisabledFormControl()) {
        if (is_multiple_) {
          // Only extend selection if there is something selected.
          if (!active_selection_anchor_)
            return;

          SetActiveSelectionEnd(option);
          UpdateListBoxSelection(false);
        } else {
          SetActiveSelectionAnchor(option);
          SetActiveSelectionEnd(option);
          UpdateListBoxSelection(true);
        }
      }
    }

  } else if (event->type() == EventTypeNames::mouseup &&
             event->IsMouseEvent() &&
             ToMouseEvent(event)->button() ==
                 static_cast<short>(WebPointerProperties::Button::kLeft) &&
             GetLayoutObject()) {
    if (GetDocument().GetPage() &&
        GetDocument().GetPage()->GetAutoscrollController().AutoscrollInProgress(
            ToLayoutBox(GetLayoutObject())))
      GetDocument().GetPage()->GetAutoscrollController().StopAutoscroll();
    else
      HandleMouseRelease();

  } else if (event->type() == EventTypeNames::keydown) {
    if (!event->IsKeyboardEvent())
      return;
    const String& key = ToKeyboardEvent(event)->key();

    bool handled = false;
    HTMLOptionElement* end_option = nullptr;
    if (!active_selection_end_) {
      // Initialize the end index
      if (key == "ArrowDown" || key == "PageDown") {
        HTMLOptionElement* start_option = LastSelectedOption();
        handled = true;
        if (key == "ArrowDown")
          end_option = NextSelectableOption(start_option);
        else
          end_option =
              NextSelectableOptionPageAway(start_option, kSkipForwards);
      } else if (key == "ArrowUp" || key == "PageUp") {
        HTMLOptionElement* start_option = SelectedOption();
        handled = true;
        if (key == "ArrowUp")
          end_option = PreviousSelectableOption(start_option);
        else
          end_option =
              NextSelectableOptionPageAway(start_option, kSkipBackwards);
      }
    } else {
      // Set the end index based on the current end index.
      if (key == "ArrowDown") {
        end_option = NextSelectableOption(active_selection_end_.Get());
        handled = true;
      } else if (key == "ArrowUp") {
        end_option = PreviousSelectableOption(active_selection_end_.Get());
        handled = true;
      } else if (key == "PageDown") {
        end_option = NextSelectableOptionPageAway(active_selection_end_.Get(),
                                                  kSkipForwards);
        handled = true;
      } else if (key == "PageUp") {
        end_option = NextSelectableOptionPageAway(active_selection_end_.Get(),
                                                  kSkipBackwards);
        handled = true;
      }
    }
    if (key == "Home") {
      end_option = FirstSelectableOption();
      handled = true;
    } else if (key == "End") {
      end_option = LastSelectableOption();
      handled = true;
    }

    if (IsSpatialNavigationEnabled(GetDocument().GetFrame())) {
      // Check if the selection moves to the boundary.
      if (key == "ArrowLeft" || key == "ArrowRight" ||
          ((key == "ArrowDown" || key == "ArrowUp") &&
           end_option == active_selection_end_))
        return;
    }

    if (end_option && handled) {
      // Save the selection so it can be compared to the new selection
      // when dispatching change events immediately after making the new
      // selection.
      SaveLastSelection();

      SetActiveSelectionEnd(end_option);

      bool select_new_item =
          !is_multiple_ || ToKeyboardEvent(event)->shiftKey() ||
          !IsSpatialNavigationEnabled(GetDocument().GetFrame());
      if (select_new_item)
        active_selection_state_ = true;
      // If the anchor is unitialized, or if we're going to deselect all
      // other options, then set the anchor index equal to the end index.
      bool deselect_others =
          !is_multiple_ ||
          (!ToKeyboardEvent(event)->shiftKey() && select_new_item);
      if (!active_selection_anchor_ || deselect_others) {
        if (deselect_others)
          DeselectItemsWithoutValidation();
        SetActiveSelectionAnchor(active_selection_end_.Get());
      }

      ScrollToOption(end_option);
      if (select_new_item) {
        UpdateListBoxSelection(deselect_others);
        ListBoxOnChange();
      } else {
        ScrollToSelection();
      }

      event->SetDefaultHandled();
    }

  } else if (event->type() == EventTypeNames::keypress) {
    if (!event->IsKeyboardEvent())
      return;
    int key_code = ToKeyboardEvent(event)->keyCode();

    if (key_code == '\r') {
      if (Form())
        Form()->SubmitImplicitly(event, false);
      event->SetDefaultHandled();
    } else if (is_multiple_ && key_code == ' ' &&
               IsSpatialNavigationEnabled(GetDocument().GetFrame())) {
      // Use space to toggle selection change.
      active_selection_state_ = !active_selection_state_;
      UpdateSelectedState(active_selection_end_.Get(), true /*multi*/,
                          false /*shift*/);
      ListBoxOnChange();
      event->SetDefaultHandled();
    }
  }
}

void HTMLSelectElement::DefaultEventHandler(Event* event) {
  if (!GetLayoutObject())
    return;

  if (IsDisabledFormControl()) {
    HTMLFormControlElementWithState::DefaultEventHandler(event);
    return;
  }

  if (UsesMenuList())
    MenuListDefaultEventHandler(event);
  else
    ListBoxDefaultEventHandler(event);
  if (event->DefaultHandled())
    return;

  if (event->type() == EventTypeNames::keypress && event->IsKeyboardEvent()) {
    KeyboardEvent* keyboard_event = ToKeyboardEvent(event);
    if (!keyboard_event->ctrlKey() && !keyboard_event->altKey() &&
        !keyboard_event->metaKey() &&
        WTF::Unicode::IsPrintableChar(keyboard_event->charCode())) {
      TypeAheadFind(keyboard_event);
      event->SetDefaultHandled();
      return;
    }
  }
  HTMLFormControlElementWithState::DefaultEventHandler(event);
}

HTMLOptionElement* HTMLSelectElement::LastSelectedOption() const {
  const ListItems& items = GetListItems();
  for (size_t i = items.size(); i;) {
    if (HTMLOptionElement* option = OptionAtListIndex(--i)) {
      if (option->Selected())
        return option;
    }
  }
  return nullptr;
}

int HTMLSelectElement::IndexOfSelectedOption() const {
  return SelectedListIndex();
}

int HTMLSelectElement::OptionCount() const {
  return GetListItems().size();
}

String HTMLSelectElement::OptionAtIndex(int index) const {
  if (HTMLOptionElement* option = OptionAtListIndex(index)) {
    if (!option->IsDisabledFormControl())
      return option->DisplayLabel();
  }
  return String();
}

void HTMLSelectElement::TypeAheadFind(KeyboardEvent* event) {
  int index = type_ahead_.HandleEvent(
      event, TypeAhead::kMatchPrefix | TypeAhead::kCycleFirstChar);
  if (index < 0)
    return;
  SelectOption(OptionAtListIndex(index), kDeselectOtherOptions |
                                             kMakeOptionDirty |
                                             kDispatchInputAndChangeEvent);
  if (!UsesMenuList())
    ListBoxOnChange();
}

void HTMLSelectElement::SelectOptionByAccessKey(HTMLOptionElement* option) {
  // First bring into focus the list box.
  if (!IsFocused())
    AccessKeyAction(false);

  if (!option || option->OwnerSelectElement() != this)
    return;
  EventQueueScope scope;
  // If this index is already selected, unselect. otherwise update the
  // selected index.
  SelectOptionFlags flags =
      kDispatchInputAndChangeEvent | (IsMultiple() ? 0 : kDeselectOtherOptions);
  if (option->Selected()) {
    if (UsesMenuList())
      SelectOption(nullptr, flags);
    else
      option->SetSelectedState(false);
  } else {
    SelectOption(option, flags);
  }
  option->SetDirty(true);
  if (UsesMenuList())
    return;
  ListBoxOnChange();
  ScrollToSelection();
}

unsigned HTMLSelectElement::length() const {
  unsigned options = 0;
  for (const auto& option : GetOptionList()) {
    ALLOW_UNUSED_LOCAL(option);
    ++options;
  }
  return options;
}

void HTMLSelectElement::FinishParsingChildren() {
  HTMLFormControlElementWithState::FinishParsingChildren();
  if (UsesMenuList())
    return;
  ScrollToOption(SelectedOption());
  if (AXObjectCache* cache = GetDocument().ExistingAXObjectCache())
    cache->ListboxActiveIndexChanged(this);
}

bool HTMLSelectElement::AnonymousIndexedSetter(
    unsigned index,
    HTMLOptionElement* value,
    ExceptionState& exception_state) {
  if (!value) {  // undefined or null
    remove(index);
    return true;
  }
  SetOption(index, value, exception_state);
  return true;
}

bool HTMLSelectElement::IsInteractiveContent() const {
  return true;
}

bool HTMLSelectElement::SupportsAutofocus() const {
  return true;
}

void HTMLSelectElement::UpdateListOnLayoutObject() {
  SetOptionsChangedOnLayoutObject();
}

DEFINE_TRACE(HTMLSelectElement) {
  visitor->Trace(list_items_);
  visitor->Trace(last_on_change_option_);
  visitor->Trace(active_selection_anchor_);
  visitor->Trace(active_selection_end_);
  visitor->Trace(option_to_scroll_to_);
  visitor->Trace(suggested_option_);
  visitor->Trace(popup_);
  visitor->Trace(popup_updater_);
  HTMLFormControlElementWithState::Trace(visitor);
}

void HTMLSelectElement::DidAddUserAgentShadowRoot(ShadowRoot& root) {
  HTMLContentElement* content = HTMLContentElement::Create(GetDocument());
  content->setAttribute(selectAttr, "option,optgroup,hr");
  root.AppendChild(content);
}

HTMLOptionElement* HTMLSelectElement::SpatialNavigationFocusedOption() {
  if (!IsSpatialNavigationEnabled(GetDocument().GetFrame()))
    return nullptr;
  HTMLOptionElement* focused_option = ActiveSelectionEnd();
  if (!focused_option)
    focused_option = FirstSelectableOption();
  return focused_option;
}

String HTMLSelectElement::ItemText(const Element& element) const {
  String item_string;
  if (isHTMLOptGroupElement(element))
    item_string = toHTMLOptGroupElement(element).GroupLabelText();
  else if (isHTMLOptionElement(element))
    item_string =
        toHTMLOptionElement(element).TextIndentedToRespectGroupLabel();

  if (GetLayoutObject())
    ApplyTextTransform(GetLayoutObject()->Style(), item_string, ' ');
  return item_string;
}

bool HTMLSelectElement::ItemIsDisplayNone(Element& element) const {
  if (isHTMLOptionElement(element))
    return toHTMLOptionElement(element).IsDisplayNone();
  if (const ComputedStyle* style = ItemComputedStyle(element))
    return style->Display() == EDisplay::kNone;
  return false;
}

const ComputedStyle* HTMLSelectElement::ItemComputedStyle(
    Element& element) const {
  return element.GetComputedStyle() ? element.GetComputedStyle()
                                    : element.EnsureComputedStyle();
}

LayoutUnit HTMLSelectElement::ClientPaddingLeft() const {
  if (GetLayoutObject() && GetLayoutObject()->IsMenuList())
    return ToLayoutMenuList(GetLayoutObject())->ClientPaddingLeft();
  return LayoutUnit();
}

LayoutUnit HTMLSelectElement::ClientPaddingRight() const {
  if (GetLayoutObject() && GetLayoutObject()->IsMenuList())
    return ToLayoutMenuList(GetLayoutObject())->ClientPaddingRight();
  return LayoutUnit();
}

void HTMLSelectElement::PopupDidHide() {
  popup_is_visible_ = false;
  UnobserveTreeMutation();
  if (AXObjectCache* cache = GetDocument().ExistingAXObjectCache()) {
    if (GetLayoutObject() && GetLayoutObject()->IsMenuList())
      cache->DidHideMenuListPopup(ToLayoutMenuList(GetLayoutObject()));
  }
}

void HTMLSelectElement::SetIndexToSelectOnCancel(int list_index) {
  index_to_select_on_cancel_ = list_index;
  if (GetLayoutObject())
    GetLayoutObject()->UpdateFromElement();
}

HTMLOptionElement* HTMLSelectElement::OptionToBeShown() const {
  if (HTMLOptionElement* option = OptionAtListIndex(index_to_select_on_cancel_))
    return option;
  if (suggested_option_)
    return suggested_option_;
  // TODO(tkent): We should not call optionToBeShown() in isMultiple() case.
  if (IsMultiple())
    return SelectedOption();
  DCHECK_EQ(SelectedOption(), last_on_change_option_);
  return last_on_change_option_;
}

void HTMLSelectElement::SelectOptionByPopup(int list_index) {
  DCHECK(UsesMenuList());
  // Check to ensure a page navigation has not occurred while the popup was
  // up.
  Document& doc = GetDocument();
  if (&doc != doc.GetFrame()->GetDocument())
    return;

  SetIndexToSelectOnCancel(-1);

  HTMLOptionElement* option = OptionAtListIndex(list_index);
  // Bail out if this index is already the selected one, to avoid running
  // unnecessary JavaScript that can mess up autofill when there is no actual
  // change (see https://bugs.webkit.org/show_bug.cgi?id=35256 and
  // <rdar://7467917>).  The selectOption function does not behave this way,
  // possibly because other callers need a change event even in cases where
  // the selected option is not change.
  if (option == SelectedOption())
    return;
  SelectOption(option, kDeselectOtherOptions | kMakeOptionDirty |
                           kDispatchInputAndChangeEvent);
}

void HTMLSelectElement::PopupDidCancel() {
  if (index_to_select_on_cancel_ >= 0)
    SelectOptionByPopup(index_to_select_on_cancel_);
}

void HTMLSelectElement::ProvisionalSelectionChanged(unsigned list_index) {
  SetIndexToSelectOnCancel(list_index);
}

void HTMLSelectElement::ShowPopup() {
  if (PopupIsVisible())
    return;
  if (GetDocument().GetPage()->GetChromeClient().HasOpenedPopup())
    return;
  if (!GetLayoutObject() || !GetLayoutObject()->IsMenuList())
    return;
  if (VisibleBoundsInVisualViewport().IsEmpty())
    return;

  if (!popup_)
    popup_ = GetDocument().GetPage()->GetChromeClient().OpenPopupMenu(
        *GetDocument().GetFrame(), *this);
  popup_is_visible_ = true;
  ObserveTreeMutation();

  LayoutMenuList* menu_list = ToLayoutMenuList(GetLayoutObject());
  popup_->Show();
  if (AXObjectCache* cache = GetDocument().ExistingAXObjectCache())
    cache->DidShowMenuListPopup(menu_list);
}

void HTMLSelectElement::HidePopup() {
  if (popup_)
    popup_->Hide();
}

void HTMLSelectElement::DidRecalcStyle() {
  HTMLFormControlElementWithState::DidRecalcStyle();
  if (PopupIsVisible())
    popup_->UpdateFromElement(PopupMenu::kByStyleChange);
}

void HTMLSelectElement::DetachLayoutTree(const AttachContext& context) {
  HTMLFormControlElementWithState::DetachLayoutTree(context);
  if (popup_)
    popup_->DisconnectClient();
  popup_is_visible_ = false;
  popup_ = nullptr;
  UnobserveTreeMutation();
}

void HTMLSelectElement::ResetTypeAheadSessionForTesting() {
  type_ahead_.ResetSession();
}

// PopupUpdater notifies updates of the specified SELECT element subtree to
// a PopupMenu object.
class HTMLSelectElement::PopupUpdater : public MutationCallback {
 public:
  explicit PopupUpdater(HTMLSelectElement&);
  DECLARE_VIRTUAL_TRACE();

  void Dispose() { observer_->disconnect(); }

 private:
  void Call(const HeapVector<Member<MutationRecord>>& records,
            MutationObserver*) override {
    // We disconnect the MutationObserver when a popuup is closed.  However
    // MutationObserver can call back after disconnection.
    if (!select_->PopupIsVisible())
      return;
    for (const auto& record : records) {
      if (record->type() == "attributes") {
        const Element& element = *ToElement(record->target());
        if (record->oldValue() == element.getAttribute(record->attributeName()))
          continue;
      } else if (record->type() == "characterData") {
        if (record->oldValue() == record->target()->nodeValue())
          continue;
      }
      select_->DidMutateSubtree();
      return;
    }
  }

  ExecutionContext* GetExecutionContext() const override {
    return &select_->GetDocument();
  }

  Member<HTMLSelectElement> select_;
  Member<MutationObserver> observer_;
};

HTMLSelectElement::PopupUpdater::PopupUpdater(HTMLSelectElement& select)
    : select_(select) {
  observer_ = MutationObserver::Create(this);
  Vector<String> filter;
  filter.ReserveCapacity(4);
  // Observe only attributes which affect popup content.
  filter.push_back(String("disabled"));
  filter.push_back(String("label"));
  filter.push_back(String("selected"));
  filter.push_back(String("value"));
  MutationObserverInit init;
  init.setAttributeOldValue(true);
  init.setAttributes(true);
  init.setAttributeFilter(filter);
  init.setCharacterData(true);
  init.setCharacterDataOldValue(true);
  init.setChildList(true);
  init.setSubtree(true);
  observer_->observe(&select, init, ASSERT_NO_EXCEPTION);
}

DEFINE_TRACE(HTMLSelectElement::PopupUpdater) {
  visitor->Trace(select_);
  visitor->Trace(observer_);
  MutationCallback::Trace(visitor);
}

void HTMLSelectElement::ObserveTreeMutation() {
  DCHECK(!popup_updater_);
  popup_updater_ = new PopupUpdater(*this);
}

void HTMLSelectElement::UnobserveTreeMutation() {
  if (!popup_updater_)
    return;
  popup_updater_->Dispose();
  popup_updater_ = nullptr;
}

void HTMLSelectElement::DidMutateSubtree() {
  DCHECK(PopupIsVisible());
  DCHECK(popup_);
  popup_->UpdateFromElement(PopupMenu::kByDOMChange);
}

}  // namespace blink
