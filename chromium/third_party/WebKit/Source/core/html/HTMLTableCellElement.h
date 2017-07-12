/*
 * Copyright (C) 1997 Martin Jones (mjones@kde.org)
 *           (C) 1997 Torben Weis (weis@kde.org)
 *           (C) 1998 Waldo Bastian (bastian@kde.org)
 *           (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2010 Apple Inc. All rights reserved.
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

#ifndef HTMLTableCellElement_h
#define HTMLTableCellElement_h

#include "core/CoreExport.h"
#include "core/html/HTMLTablePartElement.h"

namespace blink {

class CORE_EXPORT HTMLTableCellElement final : public HTMLTablePartElement {
  DEFINE_WRAPPERTYPEINFO();

 public:
  DECLARE_ELEMENT_FACTORY_WITH_TAGNAME(HTMLTableCellElement);

  int cellIndex() const;

  unsigned colSpan() const;
  unsigned rowSpan() const;

  void SetCellIndex(int);

  const AtomicString& Abbr() const;
  const AtomicString& Axis() const;
  void setColSpan(unsigned);
  const AtomicString& Headers() const;
  void setRowSpan(unsigned);

  // Rowspan: match Firefox's limit of 65,534. Edge has a higher limit, at
  // least 2^17.
  // Colspan: Firefox uses a limit of 1,000 for colspan and resets the value to
  // 1.
  // TODO(dgrogan): Change these to HTML's new specified behavior when
  // https://github.com/whatwg/html/issues/1198 is resolved.
  // Public so that HTMLColElement can use maxColSpan. maxRowSpan is only used
  // by this class but keeping them together seems desirable.
  static unsigned MaxColSpan() { return 8190u; }
  static unsigned MaxRowSpan() { return 65534u; }

 private:
  HTMLTableCellElement(const QualifiedName&, Document&);

  void ParseAttribute(const AttributeModificationParams&) override;
  bool IsPresentationAttribute(const QualifiedName&) const override;
  void CollectStyleForPresentationAttribute(const QualifiedName&,
                                            const AtomicString&,
                                            MutableStylePropertySet*) override;
  const StylePropertySet* AdditionalPresentationAttributeStyle() override;

  bool IsURLAttribute(const Attribute&) const override;
  bool HasLegalLinkAttribute(const QualifiedName&) const override;
  const QualifiedName& SubResourceAttributeName() const override;
};

inline bool IsHTMLTableCellElement(const HTMLElement& element) {
  return element.HasTagName(HTMLNames::tdTag) ||
         element.HasTagName(HTMLNames::thTag);
}

DEFINE_HTMLELEMENT_TYPE_CASTS_WITH_FUNCTION(HTMLTableCellElement);

}  // namespace blink

#endif  // HTMLTableCellElement_h
