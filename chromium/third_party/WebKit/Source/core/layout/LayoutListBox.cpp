/*
 * Copyright (C) 2006, 2007, 2008, 2011 Apple Inc. All rights reserved.
 *               2009 Torch Mobile Inc. All rights reserved.
 *                    (http://www.torchmobile.com/)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/layout/LayoutListBox.h"

#include "core/dom/ElementTraversal.h"
#include "core/html/HTMLDivElement.h"
#include "core/html/HTMLOptGroupElement.h"
#include "core/html/HTMLOptionElement.h"
#include "core/html/HTMLSelectElement.h"
#include "core/paint/PaintLayer.h"

namespace blink {

// Default size when the multiple attribute is present but size attribute is
// absent.
const int kDefaultSize = 4;

const int kDefaultPaddingBottom = 1;

LayoutListBox::LayoutListBox(Element* element) : LayoutBlockFlow(element) {
  DCHECK(element);
  DCHECK(element->IsHTMLElement());
  DCHECK(isHTMLSelectElement(element));
}

LayoutListBox::~LayoutListBox() {}

inline HTMLSelectElement* LayoutListBox::SelectElement() const {
  return toHTMLSelectElement(GetNode());
}

unsigned LayoutListBox::size() const {
  unsigned specified_size = SelectElement()->size();
  if (specified_size >= 1)
    return specified_size;

  return kDefaultSize;
}

LayoutUnit LayoutListBox::DefaultItemHeight() const {
  const SimpleFontData* font_data = Style()->GetFont().PrimaryFont();
  if (!font_data)
    return LayoutUnit();
  return LayoutUnit(font_data->GetFontMetrics().Height() +
                    kDefaultPaddingBottom);
}

LayoutUnit LayoutListBox::ItemHeight() const {
  HTMLSelectElement* select = SelectElement();
  if (!select)
    return LayoutUnit();

  const auto& items = select->GetListItems();
  if (items.IsEmpty())
    return DefaultItemHeight();

  LayoutUnit max_height;
  for (Element* element : items) {
    if (isHTMLOptGroupElement(element))
      element = &toHTMLOptGroupElement(element)->OptGroupLabelElement();
    LayoutObject* layout_object = element->GetLayoutObject();
    LayoutUnit item_height;
    if (layout_object && layout_object->IsBox())
      item_height = ToLayoutBox(layout_object)->Size().Height();
    else
      item_height = DefaultItemHeight();
    max_height = std::max(max_height, item_height);
  }
  return max_height;
}

void LayoutListBox::ComputeLogicalHeight(
    LayoutUnit,
    LayoutUnit logical_top,
    LogicalExtentComputedValues& computed_values) const {
  LayoutUnit height = ItemHeight() * size();
  // FIXME: The item height should have been added before updateLogicalHeight
  // was called to avoid this hack.
  SetIntrinsicContentLogicalHeight(height);

  height += BorderAndPaddingHeight();

  LayoutBox::ComputeLogicalHeight(height, logical_top, computed_values);
}

void LayoutListBox::StopAutoscroll() {
  HTMLSelectElement* select = SelectElement();
  if (select->IsDisabledFormControl())
    return;
  select->HandleMouseRelease();
}

void LayoutListBox::ComputeIntrinsicLogicalWidths(
    LayoutUnit& min_logical_width,
    LayoutUnit& max_logical_width) const {
  LayoutBlockFlow::ComputeIntrinsicLogicalWidths(min_logical_width,
                                                 max_logical_width);
  if (Style()->Width().IsPercentOrCalc())
    min_logical_width = LayoutUnit();
}

void LayoutListBox::ScrollToRect(const LayoutRect& rect) {
  if (HasOverflowClip()) {
    DCHECK(Layer());
    DCHECK(Layer()->GetScrollableArea());
    Layer()->GetScrollableArea()->ScrollIntoView(
        rect, ScrollAlignment::kAlignToEdgeIfNeeded,
        ScrollAlignment::kAlignToEdgeIfNeeded);
  }
}

}  // namespace blink
