/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 *           (C) 2006 Alexey Proskuryakov (ap@webkit.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2012 Apple Inc. All
 * rights reserved.
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2013 Google Inc. All rights reserved.
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
 */

#include "core/dom/StyleSheetCollection.h"

#include "core/css/CSSStyleSheet.h"
#include "core/css/RuleSet.h"

namespace blink {

StyleSheetCollection::StyleSheetCollection() {}

void StyleSheetCollection::Dispose() {
  style_sheets_for_style_sheet_list_.Clear();
  active_author_style_sheets_.Clear();
}

void StyleSheetCollection::Swap(StyleSheetCollection& other) {
  ::blink::swap(style_sheets_for_style_sheet_list_,
                other.style_sheets_for_style_sheet_list_, this, &other);
  active_author_style_sheets_.Swap(other.active_author_style_sheets_);
}

void StyleSheetCollection::SwapSheetsForSheetList(
    HeapVector<Member<StyleSheet>>& sheets) {
  // Only called for collection of HTML Imports that never has active sheets.
  DCHECK(active_author_style_sheets_.IsEmpty());
  ::blink::swap(style_sheets_for_style_sheet_list_, sheets, this);
}

void StyleSheetCollection::AppendActiveStyleSheet(
    const ActiveStyleSheet& active_sheet) {
  active_author_style_sheets_.push_back(active_sheet);
}

void StyleSheetCollection::AppendSheetForList(StyleSheet* sheet) {
  style_sheets_for_style_sheet_list_.push_back(
      TraceWrapperMember<StyleSheet>(this, sheet));
}

DEFINE_TRACE(StyleSheetCollection) {
  visitor->Trace(active_author_style_sheets_);
  visitor->Trace(style_sheets_for_style_sheet_list_);
}

DEFINE_TRACE_WRAPPERS(StyleSheetCollection) {
  for (auto sheet : style_sheets_for_style_sheet_list_) {
    visitor->TraceWrappers(sheet);
  }
}

}  // namespace blink
